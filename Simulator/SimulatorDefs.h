#pragma once
#include "EventManager.h"
#include "Win32Defs.h"
#include "PhysicalBridge.h"

struct IProject;
struct IProjectWindow;
struct ISelection;

enum class MouseButton
{
	None = 0,
	Left = 1,
	Right = 2,
	Middle = 4,
};

struct AddedToSelectionEvent : public Event<AddedToSelectionEvent, void(ISelection*, Object*)> { };
struct RemovingFromSelectionEvent : public Event<RemovingFromSelectionEvent, void(ISelection*, Object*)> { };
struct SelectionChangedEvent : public Event<SelectionChangedEvent, void(ISelection*)> { };

struct ISelection abstract : public IUnknown
{
	virtual ~ISelection() { }
	virtual const std::vector<Object*>& GetObjects() const = 0;
	virtual void Select (Object* o) = 0;
	virtual void Clear() = 0;
	virtual AddedToSelectionEvent::Subscriber GetAddedToSelectionEvent() = 0;
	virtual RemovingFromSelectionEvent::Subscriber GetRemovingFromSelectionEvent() = 0;
	virtual SelectionChangedEvent::Subscriber GetSelectionChangedEvent() = 0;
};

using SelectionFactory = ComPtr<ISelection>(*const)();
extern const SelectionFactory selectionFactory;

// ============================================================================

struct IEditArea abstract : public IWin32Window, public IUnknown
{
	virtual void SelectVlan (unsigned int vlanNumber) = 0;
};

using EditAreaFactory = ComPtr<IEditArea>(*const)(IProject* project, IProjectWindow* pw, ISelection* selection, IUIFramework* rf, const RECT& rect, ID3D11DeviceContext1* deviceContext, IDWriteFactory* dWriteFactory, IWICImagingFactory2* wicFactory);
extern const EditAreaFactory editAreaFactory;

// ============================================================================

struct ProjectWindowClosingEvent : public Event<ProjectWindowClosingEvent, void(IProjectWindow* pw, bool* cancelClose)> { };
struct SelectedTreeIndexChangedEvent : public Event<SelectedTreeIndexChangedEvent, void(IProjectWindow*, unsigned int)> { };

struct IProjectWindow : public IUnknown, public IWin32Window
{
	virtual ProjectWindowClosingEvent::Subscriber GetProjectWindowClosingEvent() = 0;
	virtual void ShowAtSavedWindowLocation(const wchar_t* regKeyPath) = 0;
	virtual void SaveWindowLocation(const wchar_t* regKeyPath) const = 0;
	virtual unsigned int GetSelectedTreeIndex() const = 0;
	virtual SelectedTreeIndexChangedEvent::Subscriber GetSelectedTreeIndexChangedEvent() = 0;
};

using ProjectWindowFactory = ComPtr<IProjectWindow>(*const)(IProject* project, HINSTANCE rfResourceHInstance, const wchar_t* rfResourceName,
	ISelection* selection, EditAreaFactory editAreaFactory, ID3D11DeviceContext1* deviceContext, IDWriteFactory* dWriteFactory, IWICImagingFactory2* wicFactory);
extern const ProjectWindowFactory projectWindowFactory;

// ============================================================================

struct BridgeInsertedEvent : public Event<BridgeInsertedEvent, void(IProject*, size_t index, PhysicalBridge*)> { };
struct BridgeRemovingEvent : public Event<BridgeRemovingEvent, void(IProject*, size_t index, PhysicalBridge*)> { };
struct ProjectInvalidateEvent : public Event<ProjectInvalidateEvent, void(IProject*)> { };

struct IProject abstract : public IUnknown
{
	virtual const std::vector<ComPtr<PhysicalBridge>>& GetBridges() const = 0;
	virtual void InsertBridge (size_t index, PhysicalBridge* bridge) = 0;
	virtual void RemoveBridge (size_t index) = 0;
	virtual BridgeInsertedEvent::Subscriber GetBridgeInsertedEvent() = 0;
	virtual BridgeRemovingEvent::Subscriber GetBridgeRemovingEvent() = 0;
	virtual ProjectInvalidateEvent::Subscriber GetProjectInvalidateEvent() = 0;
	virtual std::array<uint8_t, 6> AllocNextMacAddress() = 0;

	void AddBridge (PhysicalBridge* bridge) { InsertBridge (GetBridges().size(), bridge); }
};

using ProjectFactory = ComPtr<IProject>(*const)();
extern const ProjectFactory projectFactory;

// ============================================================================

unsigned int GetTimestampMilliseconds();
