
#include "pch.h"
#include "Simulator.h"
#include "PropertyGrid.h"
#include "Bridge.h"
#include "Port.h"
#include "Wire.h"

using namespace std;

static ATOM wndClassAtom;
static constexpr wchar_t PropertiesWindowWndClassName[] = L"PropertiesWindow-{24B42526-2970-4B3C-A753-2DABD22C4BB0}";

extern const PropertyGrid::PD* const BridgeProperties[];
extern const PropertyGrid::PD* const PortProperties[];
extern const PropertyGrid::PD* const WireProperties[];

class PropertiesWindow : public Window, public IPropertiesWindow
{
	using base = Window;

public:
	ISimulatorApp* const _app;
	IProjectWindow* const _projectWindow;
	IProjectPtr const _project;
	ISelectionPtr const _selection;
	PropertyGrid _pg;

	static constexpr DWORD Style = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

	PropertiesWindow (ISimulatorApp* app,
					  IProjectWindow* projectWindow,
					  IProject* project,
					  ISelection* selection,
					  const RECT& rect,
					  HWND hWndParent)
		: base (app->GetHInstance(), PropertiesWindowWndClassName, 0, Style, rect, hWndParent, nullptr)
		, _app(app)
		, _projectWindow(projectWindow)
		, _project(project)
		, _selection(selection)
		, _pg(app, project, this->Window::GetClientRectPixels(), GetHWnd(), app->GetDWriteFactory(), this, &GetPropertyDescriptors)
	{
		_selection->GetAddedToSelectionEvent().AddHandler (&OnObjectAddedToSelection, this);
		_selection->GetRemovingFromSelectionEvent().AddHandler (&OnObjectRemovingFromSelection, this);
		_selection->GetChangedEvent().AddHandler (&OnSelectionChanged, this);
		_projectWindow->GetSelectedVlanNumerChangedEvent().AddHandler (&OnSelectedVlanChanged, this);
	}

	~PropertiesWindow()
	{
		_projectWindow->GetSelectedVlanNumerChangedEvent().RemoveHandler (&OnSelectedVlanChanged, this);
		_selection->GetChangedEvent().RemoveHandler (&OnSelectionChanged, this);
		_selection->GetRemovingFromSelectionEvent().RemoveHandler (&OnObjectRemovingFromSelection, this);
		_selection->GetAddedToSelectionEvent().RemoveHandler (&OnObjectAddedToSelection, this);
	}

	static const PropertyGrid::PD* const* GetPropertyDescriptors (const Object* o)
	{
		if (dynamic_cast<const Bridge*>(o) != nullptr)
			return BridgeProperties;

		if (dynamic_cast<const Port*>(o) != nullptr)
			return PortProperties;

		if (dynamic_cast<const Wire*>(o) != nullptr)
			return WireProperties;

		throw not_implemented_exception();
	}

	static void OnObjectAddedToSelection (void* callbackArg, ISelection* selection, Object* o)
	{
		auto window = static_cast<PropertiesWindow*>(callbackArg);
		auto bridge = dynamic_cast<Bridge*>(o);
		if (bridge != nullptr)
			bridge->GetConfigChangedEvent().AddHandler (&OnBridgeConfigChanged, window);
	}

	static void OnObjectRemovingFromSelection (void* callbackArg, ISelection* selection, Object* o)
	{
		auto window = static_cast<PropertiesWindow*>(callbackArg);
		auto bridge = dynamic_cast<Bridge*>(o);
		if (bridge != nullptr)
			bridge->GetConfigChangedEvent().RemoveHandler (&OnBridgeConfigChanged, window);
	}

	static void OnSelectionChanged (void* callbackArg, ISelection* selection)
	{
		auto window = static_cast<PropertiesWindow*>(callbackArg);
		if (selection->GetObjects().empty())
			window->_pg.SelectObjects(nullptr, 0);
		else
			window->_pg.SelectObjects (&selection->GetObjects().at(0), selection->GetObjects().size());
	}

	static void OnBridgeConfigChanged (void* callbackArg, Bridge* b)
	{
		auto window = static_cast<PropertiesWindow*>(callbackArg);
		window->_pg.ReloadPropertyValues();
	}

	static void OnSelectedVlanChanged (void* callbackArg, IProjectWindow* pw, unsigned int vlanNumber)
	{
		auto window = static_cast<PropertiesWindow*>(callbackArg);
		window->_pg.ReloadPropertyValues();
	}

	virtual std::optional<LRESULT> WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override final
	{
		auto resultBaseClass = base::WindowProc (hwnd, msg, wParam, lParam);

		if (msg == WM_SIZE)
		{
			::MoveWindow (_pg.GetHWnd(), 0, 0, GetClientWidthPixels(), GetClientHeightPixels(), TRUE);
			::UpdateWindow (_pg.GetHWnd());
			return 0;
		}

		return resultBaseClass;
	}

	virtual HWND GetHWnd() const override final { return base::GetHWnd(); }
	virtual HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** ppvObject) override { return base::QueryInterface(riid, ppvObject); }
	virtual ULONG STDMETHODCALLTYPE AddRef() override { return base::AddRef(); }
	virtual ULONG STDMETHODCALLTYPE Release() override { return base::Release(); }
};

template <typename... Args>
static IPropertiesWindowPtr Create (Args... args)
{
	return IPropertiesWindowPtr(new PropertiesWindow (std::forward<Args>(args)...), false);
};

const PropertiesWindowFactory propertiesWindowFactory = &Create;

// ============================================================================

static const PropertyGrid::TypedPD<wstring> BridgePropAddress
(
	L"Bridge Address",
	[](const PropertyGrid* pg, const Object* o) { return static_cast<const Bridge*>(o)->GetBridgeAddressAsWString(); },
	[](const PropertyGrid* pg, Object* obj, wstring str, unsigned int timestamp)
	{
		auto newAddress = ConvertStringToBridgeAddress(str.c_str());
		auto window = static_cast<PropertiesWindow*>(pg->GetAppContext());
		STP_SetBridgeAddress (static_cast<Bridge*>(obj)->GetStpBridge(), newAddress.bytes, timestamp);
		window->_project->SetModified(true);
	}
);

static const PropertyGrid::TypedPD<bool> BridgePropStpEnabled
(
	L"STP Enabled",
	[](const PropertyGrid* pg, const Object* o) { return (bool) STP_IsBridgeStarted(static_cast<const Bridge*>(o)->GetStpBridge()); },
	[](const PropertyGrid* pg, Object* obj, bool value, unsigned int timestamp)
	{
		auto stpb = static_cast<Bridge*>(obj)->GetStpBridge();
		if (value && !STP_IsBridgeStarted(stpb))
			STP_StartBridge (stpb, timestamp);
		else if (!value && STP_IsBridgeStarted(stpb))
			STP_StopBridge (stpb, timestamp);
	}
);

static const PropertyGrid::NVP StpVersionNVPs[] = { { L"LegacySTP", STP_VERSION_LEGACY_STP }, { L"RSTP", STP_VERSION_RSTP }, { L"MSTP", STP_VERSION_MSTP }, { 0, 0 } };

static const PropertyGrid::EnumPD BridgePropStpVersion
{
	L"STP Version",
	[](const PropertyGrid* pg, const Object* o) { return (int) STP_GetStpVersion(static_cast<const Bridge*>(o)->GetStpBridge()); },
	[](const PropertyGrid* pg, Object* obj, int value, unsigned int timestamp)
	{
		auto stpb = static_cast<Bridge*>(obj)->GetStpBridge();
		auto newVersion = (STP_VERSION) value;
		if (STP_GetStpVersion(stpb) != newVersion)
			STP_SetStpVersion(stpb, newVersion, timestamp);
	},
	StpVersionNVPs
};

static const PropertyGrid::TypedPD<unsigned int> BridgePropPortCount
{
	L"Port Count",
	[](const PropertyGrid* pg, const Object* o) { return (unsigned int) static_cast<const Bridge*>(o)->GetPorts().size(); },
	nullptr
};

static const PropertyGrid::TypedPD<unsigned int> BridgePropMstiCount
{
	L"MSTI Count",
	[](const PropertyGrid* pg, const Object* o) { return STP_GetMstiCount(static_cast<const Bridge*>(o)->GetStpBridge()); },
	nullptr
};

static const PropertyGrid::TypedPD<unsigned short> BridgePropPrio
{
	L"Bridge Priority",
	[](const PropertyGrid* pg, const Object* o)
	{
		auto window = static_cast<PropertiesWindow*>(pg->GetAppContext());
		auto b = static_cast<const Bridge*>(o);
		auto treeIndex = STP_GetTreeIndexFromVlanNumber (b->GetStpBridge(), window->_projectWindow->GetSelectedVlanNumber());
		return STP_GetBridgePriority(b->GetStpBridge(), treeIndex);
	},
	nullptr,
	[](const PropertyGrid* pg, const std::vector<Object*>& objs)
	{
		auto window = static_cast<PropertiesWindow*>(pg->GetAppContext());
		auto vlanNumber = window->_projectWindow->GetSelectedVlanNumber();
		auto treeIndex = STP_GetTreeIndexFromVlanNumber(dynamic_cast<Bridge*>(objs[0])->GetStpBridge(), vlanNumber);
		auto begin = reinterpret_cast<Bridge* const*>(&objs[0]);
		auto end = begin + objs.size();
		bool allSameTree = all_of (begin, end, [vlanNumber, treeIndex](Bridge* b) { return STP_GetTreeIndexFromVlanNumber(b->GetStpBridge(), vlanNumber) == treeIndex; });

		if (!allSameTree)
			return wstring(L"Bridge Priority");

		wstringstream label;
		label << L"Bridge Priority (";
		if (treeIndex == 0)
			label << L"CIST)";
		else
			label << L"MSTI " << treeIndex << L")";
		return label.str();
	},
};

static const PropertyGrid::PD* const BridgeProperties[] =
{
	&BridgePropAddress,
	&BridgePropStpEnabled,
	&BridgePropStpVersion,
	&BridgePropPortCount,
	&BridgePropMstiCount,
	&BridgePropPrio,
	nullptr,
};

static const PropertyGrid::TypedPD<bool> PortPropAdminEdge
(
	L"AdminEdge",
	[](const PropertyGrid* pg, const Object* o)
	{
		auto port = static_cast<const Port*>(o);
		return (bool) STP_GetPortAdminEdge(port->GetBridge()->GetStpBridge(), (unsigned int) port->GetPortIndex());
	},
	nullptr
);

static const PropertyGrid::TypedPD<bool> PortPropAutoEdge
(
	L"AutoEdge",
	[](const PropertyGrid* pg, const Object* o)
	{
		auto port = static_cast<const Port*>(o);
		return (bool) STP_GetPortAutoEdge(port->GetBridge()->GetStpBridge(), (unsigned int) port->GetPortIndex());
	},
	nullptr
);

static const PropertyGrid::PD* const PortProperties[] =
{
	&PortPropAutoEdge,
	&PortPropAdminEdge,
	nullptr
};

static const PropertyGrid::PD* const WireProperties[] = { nullptr };
