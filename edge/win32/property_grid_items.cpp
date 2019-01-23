
#include "pch.h"
#include "property_grid_items.h"
#include "property_grid.h"
#include "utility_functions.h"

using namespace edge;
using namespace D2D1;

root_item* pgitem::root()
{
	return _parent->root();
}

void expandable_item::expand()
{
	assert (!_expanded);
	_children = this->create_children();
	_expanded = true;
}

#pragma region object_item
object_item::object_item (expandable_item* parent, object* const* objects, size_t size)
	: base(parent), _objects(objects, objects + size)
{
	for (auto obj : _objects)
	{
		obj->property_changing().add_handler(&on_property_changing, this);
		obj->property_changed().add_handler(&on_property_changed, this);
	}
}

object_item::~object_item()
{
	for (auto obj : _objects)
	{
		obj->property_changed().remove_handler(&on_property_changed, this);
		obj->property_changing().remove_handler(&on_property_changing, this);
	}
}

//static
void object_item::on_property_changing (void* arg, object* obj, const property* prop)
{
}

//static
void object_item::on_property_changed (void* arg, object* obj, const property* prop)
{
	auto oi = static_cast<object_item*>(arg);
	auto root_item = oi->root();
	for (auto& child_item : oi->children())
	{
		if (auto value_item = dynamic_cast<value_pgitem*>(child_item.get()); value_item->_prop == prop)
		{
			value_item->recreate_value_text_layout();
			break;
		}
	}
}

pgitem* object_item::find_child (const property* prop) const
{
	for (auto& item : children())
	{
		if (auto value_item = dynamic_cast<value_pgitem*>(item.get()); value_item->_prop == prop)
			return item.get();
	}

	return nullptr;
}

std::vector<std::unique_ptr<pgitem>> object_item::create_children()
{
	std::vector<std::unique_ptr<pgitem>> items;

	if (!_objects.empty())
	{
		auto type = _objects[0]->type();

		if (std::all_of (_objects.begin(), _objects.end(), [type](object* o) { return o->type() == type; }))
		{
			for (auto prop : type->make_property_list())
			{
				if (auto value_prop = dynamic_cast<const value_property*>(prop))
					items.push_back (std::make_unique<value_pgitem>(this, value_prop));
				else
					assert(false);// not implemented
			}
		}
	}

	return items;
}
#pragma endregion
#pragma region value_pgitem
value_pgitem::value_pgitem (object_item* parent, const value_property* prop)
	: base(parent), _prop(prop)
{ }

void value_pgitem::create_text_layouts (IDWriteFactory* factory, IDWriteTextFormat* format, float name_width, float value_width)
{
	_name = text_layout::create (factory, format, _prop->_name, name_width);
	_value = text_layout::create (factory, format, convert_to_string(), value_width);
}

void value_pgitem::recreate_value_text_layout()
{
	auto grid = root()->_grid;
	auto old_height = _value.metrics.height;
	_value = text_layout::create (grid->dwrite_factory(), grid->text_format(), convert_to_string(), grid->value_text_width());
	if (_value.metrics.height != old_height)
		grid->perform_layout();
	else
		grid->invalidate();
}

void value_pgitem::render_name  (const render_context& rc, const item_layout& l, bool selected, bool focused) const
{
	if (selected)
		rc.dc->FillRectangle (l.name_rect, focused ? rc.selected_back_brush_focused.get() : rc.selected_back_brush_not_focused.get());

	auto fore = selected ? rc.selected_fore_brush.get() : rc.fore_brush.get();
	rc.dc->DrawTextLayout ({ l.name_rect.left + text_lr_padding, l.name_rect.top }, _name.layout, fore);
}

void value_pgitem::render_value (const render_context& rc, const item_layout& l, bool selected, bool focused) const
{
	if (selected)
		rc.dc->FillRectangle (l.value_rect, focused ? rc.selected_back_brush_focused.get() : rc.selected_back_brush_not_focused.get());

	bool canEdit = (_prop->_customEditor != nullptr) || _prop->has_setter();
	auto fore = !canEdit ? rc.disabled_fore_brush.get() : (selected ? rc.selected_fore_brush.get() : rc.fore_brush.get());
	rc.dc->DrawTextLayout ({ l.value_rect.left + text_lr_padding, l.value_rect.top }, _value.layout, fore);
}

float value_pgitem::text_height() const
{
	return std::max (_name.metrics.height, _value.metrics.height);
}

HCURSOR value_pgitem::cursor (D2D1_SIZE_F offset) const
{
	bool canEdit = (_prop->_customEditor != nullptr) || _prop->has_setter();
	return ::LoadCursor (nullptr, canEdit ? IDC_IBEAM : IDC_ARROW);
}

std::string value_pgitem::convert_to_string() const
{
	assert (parent()->objects().size() == 1);
	auto obj = parent()->objects()[0];
	return _prop->get_to_string(obj);
}
#pragma endregion