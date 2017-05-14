#include "pch.h"
#include "Bridge.h"
#include "Port.h"
#include "Wire.h"
#include "UtilityFunctions.h"
#include "stp_md5.h"

using namespace std;
using namespace D2D1;

static constexpr UINT WM_ONE_SECOND_TIMER = WM_APP + 1;
static constexpr UINT WM_MAC_OPERATIONAL_TIMER = WM_APP + 2;
static constexpr UINT WM_PACKET_RECEIVED = WM_APP + 3;

static constexpr uint8_t BpduDestAddress[6] = { 1, 0x80, 0xC2, 0, 0, 0 };

HWND     Bridge::_helperWindow;
uint32_t Bridge::_helperWindowRefCount;

Bridge::Bridge (IProject* project, unsigned int portCount, unsigned int mstiCount, const std::array<uint8_t, 6>& macAddress)
	: _project(project)
{
	float offset = 0;

	for (unsigned int i = 0; i < portCount; i++)
	{
		offset += (Port::PortToPortSpacing / 2 + Port::InteriorWidth / 2);
		auto port = unique_ptr<Port>(new Port(this, i, Side::Bottom, offset));
		_ports.push_back (move(port));
		offset += (Port::InteriorWidth / 2 + Port::PortToPortSpacing / 2);
	}

	_x = 0;
	_y = 0;
	_width = max (offset, MinWidth);
	_height = DefaultHeight;

	if (_helperWindow == nullptr)
	{
		HINSTANCE hInstance;
		BOOL bRes = GetModuleHandleEx (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR) &StpCallbacks, &hInstance);
		if (!bRes)
			throw win32_exception(GetLastError());

		_helperWindow = CreateWindow (L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, 0, hInstance, 0);
		if (_helperWindow == nullptr)
			throw win32_exception(GetLastError());

		bRes = SetWindowSubclass (_helperWindow, HelperWindowProc, 0, 0);
		if (!bRes)
			throw win32_exception(GetLastError());
	}

	_helperWindowRefCount++;

	DWORD period = 950 + (std::random_device()() % 100);
	HANDLE handle;
	BOOL bRes = ::CreateTimerQueueTimer (&handle, nullptr, OneSecondTimerCallback, this, period, period, 0);
	if (!bRes)
		throw win32_exception(GetLastError());
	_oneSecondTimerHandle.reset(handle);

	period = 45 + (std::random_device()() % 10);
	bRes = ::CreateTimerQueueTimer (&handle, nullptr, MacOperationalTimerCallback, this, period, period, 0);
	if (!bRes)
		throw win32_exception(GetLastError());
	_macOperationalTimerHandle.reset(handle);

	_stpBridge = STP_CreateBridge (portCount, mstiCount, MaxVlanNumber, &StpCallbacks, &macAddress[0], 256);
	STP_EnableLogging (_stpBridge, true);
	STP_SetApplicationContext (_stpBridge, this);

	for (auto& port : _ports)
		port->GetInvalidateEvent().AddHandler(&OnPortInvalidate, this);
}

Bridge::~Bridge()
{
	for (auto& port : _ports)
		port->GetInvalidateEvent().RemoveHandler(&OnPortInvalidate, this);

	// First stop the timers, to be sure the mutex won't be acquired in a background thread (when we'll have background threads).
	_macOperationalTimerHandle = nullptr;
	_oneSecondTimerHandle = nullptr;

	_helperWindowRefCount--;
	if (_helperWindowRefCount == 0)
	{
		::RemoveWindowSubclass (_helperWindow, HelperWindowProc, 0);
		::DestroyWindow (_helperWindow);
	}

	STP_DestroyBridge (_stpBridge);
}

//static
void Bridge::OnPortInvalidate (void* callbackArg, Object* object)
{
	auto bridge = static_cast<Bridge*>(callbackArg);
	InvalidateEvent::InvokeHandlers (*bridge, bridge);
}

//static
void CALLBACK Bridge::OneSecondTimerCallback (void* lpParameter, BOOLEAN TimerOrWaitFired)
{
	auto bridge = static_cast<Bridge*>(lpParameter);
	::PostMessage (bridge->_helperWindow, WM_ONE_SECOND_TIMER, (WPARAM) bridge, 0);
}

//static
void CALLBACK Bridge::MacOperationalTimerCallback (void* lpParameter, BOOLEAN TimerOrWaitFired)
{
	auto bridge = static_cast<Bridge*>(lpParameter);
	::PostMessage (bridge->_helperWindow, WM_MAC_OPERATIONAL_TIMER, (WPARAM) bridge, 0);
}

// static
LRESULT CALLBACK Bridge::HelperWindowProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	if (uMsg == WM_ONE_SECOND_TIMER)
	{
		auto bridge = static_cast<Bridge*>((void*)wParam);

		if (STP_IsBridgeStarted(bridge->_stpBridge))
			STP_OnOneSecondTick (bridge->_stpBridge, GetTimestampMilliseconds());

		return 0;
	}
	else if (uMsg == WM_MAC_OPERATIONAL_TIMER)
	{
		auto bridge = static_cast<Bridge*>((void*)wParam);
		bridge->ComputeMacOperational();
		return 0;
	}
	else if (uMsg == WM_PACKET_RECEIVED)
	{
		auto bridge = static_cast<Bridge*>((void*)wParam);
		bridge->ProcessReceivedPacket();
		return 0;
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Checks the wires and computes macOperational for each port on this bridge.
void Bridge::ComputeMacOperational()
{
	auto timestamp = GetTimestampMilliseconds();

	bool invalidate = false;
	for (unsigned int portIndex = 0; portIndex < _ports.size(); portIndex++)
	{
		Port* port = _ports[portIndex].get();

		bool newMacOperational = (_project->FindConnectedPort(port) != nullptr);
		if (port->_macOperational != newMacOperational)
		{
			if (port->_macOperational)
			{
				// port just disconnected
				STP_OnPortDisabled (_stpBridge, portIndex, timestamp);
			}

			port->_macOperational = newMacOperational;

			if (port->_macOperational)
			{
				// port just connected
				STP_OnPortEnabled (_stpBridge, portIndex, 100, true, timestamp);
			}

			invalidate = true;
		}
	}

	if (invalidate)
		InvalidateEvent::InvokeHandlers(*this, this);
}

void Bridge::ProcessReceivedPacket()
{
	auto rp = move(_rxQueue.front());
	_rxQueue.pop();

	bool invalidate = false;
	if (!_ports[rp.portIndex]->_macOperational)
	{
		_ports[rp.portIndex]->_macOperational = true;
		invalidate = true;
	}

	if (memcmp (&rp.data[0], BpduDestAddress, 6) == 0)
	{
		// It's a BPDU.
		if (STP_IsBridgeStarted(_stpBridge))
		{
			if (!STP_GetPortEnabled(_stpBridge, rp.portIndex))
			{
				STP_OnPortEnabled (_stpBridge, rp.portIndex, 100, true, rp.timestamp);
				invalidate = true;
			}

			STP_OnBpduReceived (_stpBridge, rp.portIndex, &rp.data[21], (unsigned int) (rp.data.size() - 21), rp.timestamp);
		}
		else
		{
			// broadcast it to the other ports.
			for (size_t i = 0; i < _ports.size(); i++)
			{
				if (i != rp.portIndex)
				{
					auto txPortAddress = GetPortAddress(i);

					// If it already went through this port, we have a loop that would hang our UI.
					if (find (rp.txPortPath.begin(), rp.txPortPath.end(), txPortAddress) != rp.txPortPath.end())
					{
						// We don't do anything here; we have code in Wire.cpp that shows loops to the user - as thick red wires.
					}
					else
					{
						auto rxPort = _project->FindConnectedPort(_ports[i].get());
						if (rxPort != nullptr)
						{
							RxPacketInfo info = rp;
							info.portIndex = rxPort->_portIndex;
							info.txPortPath.push_back(txPortAddress);
							rxPort->_bridge->_rxQueue.push(move(info));
							::PostMessage (rxPort->_bridge->_helperWindow, WM_PACKET_RECEIVED, (WPARAM)(void*)rxPort->_bridge, 0);
						}
					}
				}
			}
		}
	}

	if (invalidate)
		InvalidateEvent::InvokeHandlers(*this, this);
}

void Bridge::SetLocation(float x, float y)
{
	if ((_x != x) || (_y != y))
	{
		_x = x;
		_y = y;
		InvalidateEvent::InvokeHandlers(*this, this);
	}
}

void Bridge::Render (ID2D1RenderTarget* dc, const DrawingObjects& dos, unsigned int vlanNumber, const D2D1_COLOR_F& configIdColor) const
{
	auto treeIndex = STP_GetTreeIndexFromVlanNumber (_stpBridge, vlanNumber);

	wstringstream text;
	float bridgeOutlineWidth = OutlineWidth;
	if (STP_IsBridgeStarted(_stpBridge))
	{
		auto stpVersion = STP_GetStpVersion(_stpBridge);
		auto treeIndex = STP_GetTreeIndexFromVlanNumber(_stpBridge, vlanNumber);
		bool isCistRoot = STP_IsRootBridge(_stpBridge);
		bool isRegionalRoot = (treeIndex > 0) && STP_IsRegionalRootBridge(_stpBridge, treeIndex);

		if ((treeIndex == 0) ? isCistRoot : isRegionalRoot)
			bridgeOutlineWidth *= 2;

		text << uppercase << setfill(L'0') << setw(4) << hex << STP_GetBridgePriority(_stpBridge, treeIndex) << L'.' << GetBridgeAddressAsString() << endl;
		text << L"STP enabled (" << STP_GetVersionString(stpVersion) << L")" << endl;
		text << (isCistRoot ? L"CIST Root Bridge\r\n" : L"");
		if (stpVersion >= STP_VERSION_MSTP)
		{
			text << L"VLAN " << dec << vlanNumber << L". Spanning tree: " << ((treeIndex == 0) ? L"CIST(0)" : (wstring(L"MSTI") + to_wstring(treeIndex)).c_str()) << endl;
			text << (isRegionalRoot ? L"Regional Root\r\n" : L"");
		}
	}
	else
	{
		text << uppercase << setfill(L'0') << hex << GetBridgeAddressAsString() << endl << L"STP disabled\r\n(right-click to enable)";
	}

	// Draw bridge outline.
	D2D1_ROUNDED_RECT rr = RoundedRect (GetBounds(), RoundRadius, RoundRadius);
	InflateRoundedRect (&rr, -bridgeOutlineWidth / 2);
	ID2D1SolidColorBrushPtr brush;
	dc->CreateSolidColorBrush (configIdColor, &brush);
	dc->FillRoundedRectangle (&rr, brush/*_powered ? dos._poweredFillBrush : dos._unpoweredBrush*/);
	dc->DrawRoundedRectangle (&rr, dos._brushWindowText, bridgeOutlineWidth);

	// Draw bridge text.
	auto tl = TextLayout::Create (dos._dWriteFactory, dos._regularTextFormat, text.str().c_str());
	dc->DrawTextLayout ({ _x + OutlineWidth * 2 + 3, _y + OutlineWidth * 2 + 3}, tl.layout, dos._brushWindowText);

	for (auto& port : _ports)
		port->Render (dc, dos, vlanNumber);
}

void Bridge::RenderSelection (const IZoomable* zoomable, ID2D1RenderTarget* rt, const DrawingObjects& dos) const
{
	auto oldaa = rt->GetAntialiasMode();
	rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

	auto tl = zoomable->GetDLocationFromWLocation ({ _x - OutlineWidth / 2, _y - OutlineWidth / 2 });
	auto br = zoomable->GetDLocationFromWLocation ({ _x + _width + OutlineWidth / 2, _y + _height + OutlineWidth / 2 });
	rt->DrawRectangle ({ tl.x - 10, tl.y - 10, br.x + 10, br.y + 10 }, dos._brushHighlight, 2, dos._strokeStyleSelectionRect);

	rt->SetAntialiasMode(oldaa);
}

HTResult Bridge::HitTest (const IZoomable* zoomable, D2D1_POINT_2F dLocation, float tolerance)
{
	for (auto& p : _ports)
	{
		auto ht = p->HitTest (zoomable, dLocation, tolerance);
		if (ht.object != nullptr)
			return ht;
	}

	auto tl = zoomable->GetDLocationFromWLocation ({ _x, _y });
	auto br = zoomable->GetDLocationFromWLocation ({ _x + _width, _y + _height });

	if ((dLocation.x >= tl.x) && (dLocation.y >= tl.y) && (dLocation.x < br.x) && (dLocation.y < br.y))
		return { this, HTCodeInner };

	return {};
}

std::wstring Bridge::GetBridgeAddressAsString() const
{
	array<unsigned char, 6> address;
	STP_GetBridgeAddress (_stpBridge, address.data());

	wstringstream ss;
	ss << uppercase << setfill(L'0') << hex
		<< setw(2) << address[0] << setw(2) << address[1] << setw(2) << address[2]
		<< setw(2) << address[3] << setw(2) << address[4] << setw(2) << address[5];
	return ss.str();
}

std::array<uint8_t, 6> Bridge::GetPortAddress (size_t portIndex) const
{
	std::array<uint8_t, 6> pa;
	STP_GetBridgeAddress (_stpBridge, pa.data());
	pa[5]++;
	if (pa[5] == 0)
	{
		pa[4]++;
		if (pa[4] == 0)
		{
			pa[3]++;
			if (pa[3] == 0)
				throw not_implemented_exception();
		}
	}

	return pa;
}

static const _bstr_t BridgeString = "Bridge";
static const _bstr_t IndexString = "Index";
static const _bstr_t AddressString = "Address";
static const _bstr_t StpEnabledString = L"STPEnabled";
static const _bstr_t TrueString = L"True";
static const _bstr_t FalseString = L"False";
static const _bstr_t StpVersionString = L"StpVersion";
static const _bstr_t PortCountString = L"PortCount";
static const _bstr_t MstiCountString = L"MstiCount";

IXMLDOMElementPtr Bridge::Serialize (IXMLDOMDocument3* doc) const
{
	IXMLDOMElementPtr element;
	HRESULT hr = doc->createElement (BridgeString, &element); ThrowIfFailed(hr);

	auto it = find_if (_project->GetBridges().begin(), _project->GetBridges().end(), [this](auto& up) { return up.get() == this; });
	auto bridgeIndex = it - _project->GetBridges().begin();
	hr = element->setAttribute (IndexString, _variant_t(bridgeIndex)); ThrowIfFailed(hr);

	hr = element->setAttribute (AddressString, _variant_t(GetBridgeAddressAsString().c_str())); ThrowIfFailed(hr);

	hr = element->setAttribute (StpEnabledString, _variant_t(STP_IsBridgeStarted(_stpBridge))); ThrowIfFailed(hr);

	hr = element->setAttribute (StpVersionString, _variant_t(STP_GetVersionString(STP_GetStpVersion(_stpBridge)))); ThrowIfFailed(hr);

	hr = element->setAttribute (PortCountString, _variant_t(_ports.size())); ThrowIfFailed(hr);

	hr = element->setAttribute (MstiCountString, _variant_t(STP_GetMstiCount(_stpBridge))); ThrowIfFailed(hr);

	return element;
}

//static
unique_ptr<Bridge> Bridge::Deserialize (IXMLDOMElement* element)
{
	_variant_t value;
	HRESULT hr = element->getAttribute (AddressString, &value); ThrowIfFailed(hr);

	return nullptr;
}

void Bridge::SetCoordsForInteriorPort (Port* _port, D2D1_POINT_2F proposedLocation)
{
	float mouseX = proposedLocation.x - _x;
	float mouseY = proposedLocation.y - _y;

	float wh = _width / _height;

	// top side
	if ((mouseX > mouseY * wh) && (_width - mouseX) > mouseY * wh)
	{
		_port->_side = Side::Top;

		if (mouseX < Port::InteriorWidth / 2)
			_port->_offset = Port::InteriorWidth / 2;
		else if (mouseX > _width - Port::InteriorWidth / 2)
			_port->_offset = _width - Port::InteriorWidth / 2;
		else
			_port->_offset = mouseX;
	}

	// bottom side
	else if ((mouseX <= mouseY * wh) && (_width - mouseX) <= mouseY * wh)
	{
		_port->_side = Side::Bottom;

		if (mouseX < Port::InteriorWidth / 2 + 1)
			_port->_offset = Port::InteriorWidth / 2 + 1;
		else if (mouseX > _width - Port::InteriorWidth / 2)
			_port->_offset = _width - Port::InteriorWidth / 2;
		else
			_port->_offset = mouseX;
	}

	// left side
	if ((mouseX <= mouseY * wh) && (_width - mouseX) > mouseY * wh)
	{
		_port->_side = Side::Left;

		if (mouseY < Port::InteriorWidth / 2)
			_port->_offset = Port::InteriorWidth / 2;
		else if (mouseY > _height - Port::InteriorWidth / 2)
			_port->_offset = _height - Port::InteriorWidth / 2;
		else
			_port->_offset = mouseY;
	}

	// right side
	if ((mouseX > mouseY * wh) && (_width - mouseX) <= mouseY * wh)
	{
		_port->_side = Side::Right;

		if (mouseY < Port::InteriorWidth / 2)
			_port->_offset = Port::InteriorWidth / 2;
		else if (mouseY > _height - Port::InteriorWidth / 2)
			_port->_offset = _height - Port::InteriorWidth / 2;
		else
			_port->_offset = mouseY;
	}

	InvalidateEvent::InvokeHandlers (*this, this);
}

#pragma region STP Callbacks
const STP_CALLBACKS Bridge::StpCallbacks =
{
	StpCallback_EnableLearning,
	StpCallback_EnableForwarding,
	StpCallback_TransmitGetBuffer,
	StpCallback_TransmitReleaseBuffer,
	StpCallback_FlushFdb,
	StpCallback_DebugStrOut,
	StpCallback_OnTopologyChange,
	StpCallback_OnNotifiedTopologyChange,
	StpCallback_OnPortRoleChanged,
	StpCallback_OnConfigChanged,
	StpCallback_AllocAndZeroMemory,
	StpCallback_FreeMemory,
};

void* Bridge::StpCallback_AllocAndZeroMemory(unsigned int size)
{
	void* p = malloc(size);
	memset (p, 0, size);
	return p;
}

void Bridge::StpCallback_FreeMemory(void* p)
{
	free(p);
}

void* Bridge::StpCallback_TransmitGetBuffer (STP_BRIDGE* bridge, unsigned int portIndex, unsigned int bpduSize, unsigned int timestamp)
{
	Bridge* b = static_cast<Bridge*>(STP_GetApplicationContext(bridge));
	Port* txPort = b->_ports[portIndex].get();
	Port* rxPort = b->_project->FindConnectedPort(txPort);
	if (rxPort == nullptr)
		return nullptr; // The port was disconnected and our port polling code hasn't reacted yet. This is what happens in a real system too.

	b->_txPacketData.resize (bpduSize + 21);
	memcpy (&b->_txPacketData[0], BpduDestAddress, 6);
	memcpy (&b->_txPacketData[6], &b->GetPortAddress(portIndex)[0], 6);
	b->_txTransmittingPort = txPort;
	b->_txReceivingPort = rxPort;
	b->_txTimestamp = timestamp;
	return &b->_txPacketData[21];
}

void Bridge::StpCallback_TransmitReleaseBuffer (STP_BRIDGE* bridge, void* bufferReturnedByGetBuffer)
{
	auto b = static_cast<Bridge*>(STP_GetApplicationContext(bridge));

	RxPacketInfo info;
	info.data = move(b->_txPacketData);
	info.portIndex = b->_txReceivingPort->_portIndex;
	info.timestamp = b->_txTimestamp;
	info.txPortPath.push_back (b->GetPortAddress(b->_txTransmittingPort->GetPortIndex()));
	Bridge* receivingBridge = b->_txReceivingPort->_bridge;
	receivingBridge->_rxQueue.push (move(info));
	::PostMessage (receivingBridge->_helperWindow, WM_PACKET_RECEIVED, (WPARAM)(void*)receivingBridge, 0);
}

void Bridge::StpCallback_EnableLearning(STP_BRIDGE* bridge, unsigned int portIndex, unsigned int treeIndex, unsigned int enable, unsigned int timestamp)
{
	auto b = static_cast<Bridge*>(STP_GetApplicationContext(bridge));
	InvalidateEvent::InvokeHandlers (*b, b);
}

void Bridge::StpCallback_EnableForwarding(STP_BRIDGE* bridge, unsigned int portIndex, unsigned int treeIndex, unsigned int enable, unsigned int timestamp)
{
	auto b = static_cast<Bridge*>(STP_GetApplicationContext(bridge));
	InvalidateEvent::InvokeHandlers(*b, b);
}

void Bridge::StpCallback_FlushFdb (STP_BRIDGE* bridge, unsigned int portIndex, unsigned int treeIndex, enum STP_FLUSH_FDB_TYPE flushType)
{
	auto b = static_cast<Bridge*>(STP_GetApplicationContext(bridge));
}

void Bridge::StpCallback_DebugStrOut (STP_BRIDGE* bridge, int portIndex, int treeIndex, const char* nullTerminatedString, unsigned int stringLength, unsigned int flush)
{
	auto b = static_cast<Bridge*>(STP_GetApplicationContext(bridge));

	if (stringLength > 0)
	{
		if (b->_currentLogLine.text.empty())
		{
			b->_currentLogLine.text.assign (nullTerminatedString, (size_t) stringLength);
			b->_currentLogLine.portIndex = portIndex;
			b->_currentLogLine.treeIndex = treeIndex;
		}
		else
		{
			if ((b->_currentLogLine.portIndex != portIndex) || (b->_currentLogLine.treeIndex != treeIndex))
			{
				b->_logLines.push_back(make_unique<BridgeLogLine>(move(b->_currentLogLine)));
				BridgeLogLineGenerated::InvokeHandlers (*b, b, b->_logLines.back().get());
			}

			b->_currentLogLine.text.append (nullTerminatedString, (size_t) stringLength);
		}

		if (!b->_currentLogLine.text.empty() && (b->_currentLogLine.text.back() == L'\n'))
		{
			b->_logLines.push_back(make_unique<BridgeLogLine>(move(b->_currentLogLine)));
			BridgeLogLineGenerated::InvokeHandlers (*b, b, b->_logLines.back().get());
		}
	}

	if (flush && !b->_currentLogLine.text.empty())
	{
		b->_logLines.push_back(make_unique<BridgeLogLine>(move(b->_currentLogLine)));
		BridgeLogLineGenerated::InvokeHandlers (*b, b, b->_logLines.back().get());
	}
}

void Bridge::StpCallback_OnTopologyChange (STP_BRIDGE* bridgetimestamp)
{
}

void Bridge::StpCallback_OnNotifiedTopologyChange (STP_BRIDGE* bridge, unsigned int portIndex, unsigned int treeIndex, unsigned int timestamp)
{
}

void Bridge::StpCallback_OnPortRoleChanged (STP_BRIDGE* bridge, unsigned int portIndex, unsigned int treeIndex, STP_PORT_ROLE role, unsigned int timestamp)
{
	auto b = static_cast<Bridge*>(STP_GetApplicationContext(bridge));
	InvalidateEvent::InvokeHandlers(*b, b);
}

void Bridge::StpCallback_OnConfigChanged (struct STP_BRIDGE* bridge, unsigned int timestamp)
{
	auto b = static_cast<Bridge*>(STP_GetApplicationContext(bridge));
	BridgeConfigChangedEvent::InvokeHandlers (*b, b);
	InvalidateEvent::InvokeHandlers(*b, b);
}
#pragma endregion
