
#include "pch.h"
#include "PropertyGrid.h"

using namespace std;

static constexpr float CellLRPadding = 3.0f;

PropertyGrid::PropertyGrid (HINSTANCE hInstance, const RECT& rect, HWND hWndParent, IDWriteFactory* dWriteFactory, void* appContext, PropertyCollectionGetter propertyCollectionGetter)
	: base (hInstance, 0, WS_CHILD | WS_VISIBLE, rect, hWndParent, nullptr, dWriteFactory)
	, _appContext(appContext)
	, _propertyCollectionGetter(propertyCollectionGetter)
{
	auto hr = dWriteFactory->CreateTextFormat (L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
											   DWRITE_FONT_STRETCH_NORMAL, 12, L"en-US", &_textFormat); ThrowIfFailed(hr);

	hr = dWriteFactory->CreateTextFormat (L"Wingdings", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
										  DWRITE_FONT_STRETCH_NORMAL, 14, L"en-US", &_wingdings); ThrowIfFailed(hr);

	GetRenderTarget()->CreateSolidColorBrush (GetD2DSystemColor(COLOR_WINDOW), &_windowBrush);
	GetRenderTarget()->CreateSolidColorBrush (GetD2DSystemColor(COLOR_WINDOWTEXT), &_windowTextBrush);
	GetRenderTarget()->CreateSolidColorBrush (GetD2DSystemColor(COLOR_GRAYTEXT), &_grayTextBrush);
}

PropertyGrid::~PropertyGrid()
{ }

PropertyGrid::EnumPD::EnumPD (const wchar_t* name, Getter getter, Setter setter, const NVP* nameValuePairs)
	: TypedPD(name, getter, setter), _nameValuePairs(nameValuePairs)
{ }

std::wstring PropertyGrid::EnumPD::to_wstring (const PropertyGrid* pg, const void* so) const
{
	auto value = _getter(pg, so);
	for (auto nvp = _nameValuePairs; nvp->first != nullptr; nvp++)
	{
		if (nvp->second == value)
			return nvp->first;
	}

	return L"??";
}

std::optional<LRESULT> PropertyGrid::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto resultBaseClass = base::WindowProc (hwnd, msg, wParam, lParam);

	if (msg == WM_SIZE)
	{
		CreateLabelTextLayouts();
		CreateValueTextLayouts();
		return 0;
	}

	if (msg == WM_SETCURSOR)
	{
		if (((HWND) wParam == GetHWnd()) && (LOWORD (lParam) == HTCLIENT))
		{
			// Let's check the result because GetCursorPos fails when the input desktop is not the current desktop
			// (happens for example when the monitor goes to sleep and then the lock screen is displayed).
			POINT pt;
			if (::GetCursorPos (&pt))
			{
				if (ScreenToClient (GetHWnd(), &pt))
				{
					this->ProcessWmSetCursor(pt);
					return TRUE;
				}
			}
		}

		return nullopt;
	}

	return resultBaseClass;
}

void PropertyGrid::ProcessWmSetCursor (POINT pt) const
{
	auto dipLocation = GetDipLocationFromPixelLocation(pt);
	auto item = GetItemAt(dipLocation);
	if ((item != nullptr) && !item->pd->IsReadOnly())
	{
		if (dipLocation.x >= GetNameColumnWidth())
			::SetCursor (::LoadCursor (nullptr, IDC_HAND));
		else
			::SetCursor (::LoadCursor (nullptr, IDC_ARROW));
	}
	else
		::SetCursor (::LoadCursor (nullptr, IDC_ARROW));
}

const PropertyGrid::Item* PropertyGrid::GetItemAt (D2D1_POINT_2F location) const
{
	auto item = EnumItems ([location](float textY, float lineY, float lineWidth, const Item& item, bool& stopEnum)
	{
		stopEnum = lineY + lineWidth > location.y;
	});

	return item;
}

const PropertyGrid::Item* PropertyGrid::EnumItems (std::function<void(float textY, float lineY, float lineWidth, const Item& item, bool& stopEnum)> func) const
{
	float pixelWidth = GetDipSizeFromPixelSize ({ 1, 0 }).width;
	float lineWidth = roundf(1.0f / pixelWidth) * pixelWidth;
	float y = 0;
	bool cancelEnum = false;
	for (auto& item : _items)
	{
		float lineY = y + std::max (item.labelTL.metrics.height, item.valueTL.metrics.height);
		lineY = roundf (lineY / pixelWidth) * pixelWidth + lineWidth / 2;
		func (y, lineY, lineWidth, item, cancelEnum);
		if (cancelEnum)
			return &item;
		y = lineY + lineWidth;
	}

	return nullptr;
}

void PropertyGrid::Render (ID2D1RenderTarget* rt) const
{
	rt->Clear(GetD2DSystemColor(COLOR_WINDOW));

	if (_selectedObjects.empty())
	{
		auto tl = TextLayout::Create (GetDWriteFactory(), _textFormat, L"(no selection)");
		D2D1_POINT_2F p = { GetClientWidthDips() / 2 - tl.metrics.width / 2, GetClientHeightDips() / 2 - tl.metrics.height / 2};
		rt->DrawTextLayout (p, tl.layout, _windowTextBrush);
		return;
	}

	float lastLineY = 0;
	EnumItems ([this, rt, &lastLineY](float textY, float lineY, float lineWidth, const Item& item, bool& stopEnum)
	{
		auto& brush = item.pd->IsReadOnly() ? _grayTextBrush : _windowTextBrush;
		rt->DrawTextLayout ({ CellLRPadding, textY }, item.labelTL.layout, brush);
		rt->DrawTextLayout ({ GetNameColumnWidth() + lineWidth + CellLRPadding, textY }, item.valueTL.layout, brush);
		rt->DrawLine ({ 0, lineY }, { GetClientWidthDips(), lineY }, _grayTextBrush, lineWidth);
		lastLineY = lineY;
	});

	float pixelWidth = GetDipSizeFromPixelSize ({ 1, 0 }).width;
	float lineWidth = roundf(1.0f / pixelWidth) * pixelWidth;
	float x = GetNameColumnWidth() + lineWidth / 2;
	rt->DrawLine ({ x, 0 }, { x, lastLineY }, _grayTextBrush, lineWidth);
}

float PropertyGrid::GetNameColumnWidth() const
{
	float w = std::max (100.0f, GetClientWidthDips()) * _nameColumnSize;
	float pixelWidth = GetDipSizeFromPixelSize ({ 1, 0 }).width;
	w = roundf (w / pixelWidth) * pixelWidth;
	return w;
}

void PropertyGrid::DiscardEditor()
{
}

void PropertyGrid::SelectObjects (void* const* objects, size_t count)
{
	bool sameSelection = false;
	if (_selectedObjects.size() == count)
	{
		sameSelection = true;
		for (size_t i = 0; i < count; i++)
		{
			if (_selectedObjects[i] != objects[i])
			{
				sameSelection = false;
				break;
			}
		}
	}

	if (sameSelection)
		return;

	DiscardEditor();

	_selectedObjects.assign (objects, objects + count);
	_items.clear();

	if (!_selectedObjects.empty())
	{
		for (auto ppd = _propertyCollectionGetter(_selectedObjects[0]); *ppd != nullptr; ppd++)
			_items.push_back (Item { *ppd });

		CreateLabelTextLayouts();
		CreateValueTextLayouts();
	}

	::InvalidateRect (GetHWnd(), NULL, FALSE);
}

void PropertyGrid::CreateLabelTextLayouts()
{
	float maxWidth = GetNameColumnWidth() - 2 * CellLRPadding;
	for (auto& item : _items)
	{
		if (item.pd->_labelGetter != nullptr)
			item.labelTL = TextLayout::Create (GetDWriteFactory(), _textFormat, item.pd->_labelGetter(this).c_str(), maxWidth);
		else
			item.labelTL = TextLayout::Create (GetDWriteFactory(), _textFormat, item.pd->_name, maxWidth);
	}
}

void PropertyGrid::CreateValueTextLayouts()
{
	float pixelWidth = GetDipSizeFromPixelSize ({ 1, 0 }).width;
	float lineWidth = roundf(1.0f / pixelWidth) * pixelWidth;
	float maxWidth = std::max (100.0f, GetClientWidthDips()) - GetNameColumnWidth() - lineWidth - 2 * CellLRPadding;

	for (auto& item : _items)
	{
		auto str = item.pd->to_wstring(this, _selectedObjects[0]);
		item.valueTL = TextLayout::Create (GetDWriteFactory(), _textFormat, str.c_str(), maxWidth);
	}
}

void PropertyGrid::ReloadPropertyValues()
{
	DiscardEditor();

	//throw not_implemented_exception()
	CreateValueTextLayouts();
	::InvalidateRect (GetHWnd(), NULL, FALSE);
}