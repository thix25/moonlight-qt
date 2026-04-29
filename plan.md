# Plan: USB & Bluetooth Passthrough for Moonlight-QT

## TL;DR

Add USB and Bluetooth device passthrough to Moonlight-QT so that devices connected to the client appear as **truly native USB devices** on the Sunshine host (not virtual/emulated). The client-side is integrated into Moonlight-QT; the server-side is a standalone C++ companion service that runs alongside Sunshine. Both sides are Windows. Uses USB/IP protocol over an independent TCP connection on port 47990. The server uses the usbip-win VHCI driver to create real USB device nodes — the OS and anti-cheat see them as physically connected hardware.

**Important context**: Gamepads already work through Moonlight's native input. This feature targets USB drives, mice, webcams, BT devices (Wii controllers), and other peripherals. ViGEmBus is deliberately avoided (triggers anti-cheat like BattlEye).

## Architecture Overview

```
[Moonlight Client (Windows)]              [Companion Server (Windows)]
                                            (alongside Sunshine)
USB/BT devices physically here              Games/apps run here
         |                                          |
   Device Capture Layer                     Native Device Layer
   ├─ libusb (USB device capture)           └─ VHCI driver (usbip-win)
   └─ Windows BT API                           Creates REAL USB device nodes
         |                                      (seen as native hardware)
   PassthroughClient                        PassthroughService
         |              TCP:47990                   |
         +------------------------------------------+
              USB/IP protocol + Device metadata

Server OS / anti-cheat sees:  "USB\VID_xxxx&PID_xxxx\serial"
                               ↑ real device node, not virtual
```

**Why VHCI (not ViGEmBus/vmulti/SendInput)**:
- VHCI creates a virtual USB host controller in the OS. Devices attached to it appear as genuine USB hardware with real VID/PID/serial.
- Anti-cheat software (BattlEye, EAC) sees standard USB device stack — indistinguishable from physical.
- Works for ANY device type: storage, webcam, audio, HID, BT-over-USB.

## Protocol Design

**Port**: 47990 (configurable, above Sunshine ports 47984-47989)

**Transport**: TCP with USB/IP v1.1.1 binary protocol, wrapped in a thin handshake layer.

**Connection flow**:
1. Client connects TCP to server:47990
2. Handshake: `HELLO` (version, session ID) → `HELLO_ACK` (server capabilities, VHCI status)
3. Client sends `DEVICE_LIST` (all local USB + BT devices with metadata)
4. User picks devices in Moonlight UI
5. For each selected device: `DEVICE_ATTACH` → server creates VHCI port → `DEVICE_ATTACH_ACK`
6. USB/IP URB traffic flows: `USBIP_SUBMIT` (client→server) / `USBIP_RETURN` (server→client)
7. On disconnect: `DEVICE_DETACH` → server removes VHCI port

**Message Format (all little-endian)**:
```
[4B magic "MLPT"] [2B version] [2B msg_type] [4B payload_length] [payload...]
```

**Message Types**:
| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0x01 | HELLO | C→S | Version handshake + session ID |
| 0x02 | HELLO_ACK | S→C | Server capabilities, VHCI driver status |
| 0x10 | DEVICE_LIST | C→S | All enumerated USB+BT devices with full metadata |
| 0x11 | DEVICE_ATTACH | C→S | Request to forward a specific device (includes USB descriptors) |
| 0x12 | DEVICE_ATTACH_ACK | S→C | VHCI port created, device ready |
| 0x13 | DEVICE_DETACH | C→S | Stop forwarding a device |
| 0x14 | DEVICE_DETACH_ACK | S→C | VHCI port removed |
| 0x30 | USBIP_SUBMIT | C→S | USB/IP URB submit (all transfer types) |
| 0x31 | USBIP_RETURN | S→C | USB/IP URB return |
| 0x32 | USBIP_UNLINK | S→C | Cancel pending URB |
| 0x40 | BT_DEVICE_INFO | C→S | BT metadata (battery, RSSI, services) — supplemental |
| 0xFF | KEEPALIVE | Both | Connection health |

## Phases

### Phase 1: Foundation — Protocol, TCP Transport, Device Enumeration UI

**Goal**: Communication layer works, devices are listed in the Moonlight UI, user can select which to forward.

**Steps**:

1. **Create project structure**
   - `app/streaming/passthrough/` — Client-side module in Moonlight
   - `passthrough-server/` — Standalone C++ companion server at project root
   - `passthrough-server/CMakeLists.txt` — CMake build (no Qt, links Winsock2)

2. **Define shared protocol** (`app/streaming/passthrough/protocol.h`)
   - Header-only, no Qt dependency (portable between client and server)
   - Message structures, magic bytes, version, serialization helpers
   - Symlinked or copied into `passthrough-server/src/`

3. **Client TCP transport** (`app/streaming/passthrough/passthroughclient.h/.cpp`)
   - `PassthroughClient` class using `QTcpSocket`
   - Connects to companion server at `m_Computer->activeAddress` + port 47990
   - Lifecycle: start after `connectionStarted()` in Session, stop on `connectionTerminated()`
   - Async message send/receive via Qt signals/slots
   - Reconnection with backoff, connection status exposed to QML

4. **Server TCP transport** (`passthrough-server/src/server.h/.cpp`)
   - Winsock2 TCP server, listens on configurable port (default 47990)
   - Accept connections, parse protocol messages
   - Worker thread per client

5. **Device enumeration — client** (`app/streaming/passthrough/deviceenumerator.h/.cpp`)
   - USB: Windows SetupAPI (`SetupDiGetClassDevs`, `SetupDiEnumDeviceInfo`)
     - Collect: VID, PID, name, serial, device class, instance path
   - Bluetooth: `BluetoothFindFirstDevice`/`BluetoothGetDeviceInfo`
     - Collect: name, address, CoD, paired/connected, battery, RSSI
   - Expose as `QAbstractListModel` for QML (columns: name, type, VID:PID, status)
   - Hot-plug: `RegisterDeviceNotification` for dynamic updates

6. **UI — Device picker** (`app/gui/PassthroughView.qml`)
   - Accessible during active streaming session (button in streaming overlay)
   - Lists all local USB + BT devices with type icons
   - Toggle switch per device to enable/disable forwarding
   - Status badge: "Connected" / "Forwarding" / "Error" / "Not available"
   - "Auto-forward" checkbox per device (remembered for next session)
   - Filter/search bar for large device lists
   - Group by type: USB Devices / Bluetooth Devices

7. **Settings integration**
   - `StreamingPreferences`: add `enablePassthrough` (bool), `passthroughPort` (int, default 47990)
   - Per-client device memory: `clients/<uuid>/passthroughDevices` (JSON array of VID:PID:serial + auto-forward flag)
   - `ClientSettingsDialog.qml`: add Passthrough section with enable toggle + port field

8. **Hook into Session lifecycle**
   - `session.h`: add `PassthroughClient*` member
   - `session.cpp`: create after `connectionStarted()`, auto-forward remembered devices, destroy on `connectionTerminated()`

**Verification**:
- Companion server starts on port 47990
- Start streaming in Moonlight → TCP connection established
- Device list shows correct USB + BT devices with metadata
- HELLO/HELLO_ACK handshake completes
- Hot-plug a USB device → appears in list dynamically

---

### Phase 2: USB/IP Passthrough — Native Device Forwarding

**Goal**: Forward USB devices via USB/IP so they appear as genuinely native hardware on the server.

**Steps**:

9. **libusb integration — client**
   - Add libusb-1.0 to `libs/windows/` (prebuilt binaries)
   - Link in `app.pro`: `LIBS += -L$PWD/../libs/windows/lib/x64 -lusb-1.0`
   - `UsbIpExporter` class (`app/streaming/passthrough/usbipexporter.h/.cpp`):
     - Open device via libusb
     - Detach kernel driver (WinUSB auto-detach)
     - Claim all interfaces
     - Export full USB descriptors (device, config, interface, endpoint)
     - Handle URB submit/return loop (async libusb transfers)

10. **USB/IP protocol implementation** (shared between client and server)
    - Implement USB/IP v1.1.1 message handling within `protocol.h`
    - URB submit: transfer type (control/bulk/interrupt/isochronous), endpoint, direction, data
    - URB return: status, actual length, data
    - Sequence number tracking for multiple concurrent async transfers
    - ISO packet descriptors for isochronous transfers (webcams, audio)

11. **VHCI driver — server side**
    - `VhciManager` class (`passthrough-server/src/vhci_manager.h/.cpp`):
      - Load usbip-win VHCI driver (`usbip_vhci.sys`)
      - Open VHCI device handle
      - Create virtual USB port per attached device
      - Feed USB/IP URBs to VHCI driver via IOCTL
      - Device appears in Device Manager with real VID/PID
    - Bundle usbip-win driver files in `passthrough-server/driver/`

12. **Device hot-plug handling**
    - Client: `RegisterDeviceNotification` → auto `DEVICE_ATTACH`/`DEVICE_DETACH`
    - Server: VHCI port create/destroy, clean device removal
    - Handle surprise removal gracefully (USB cable pull)

13. **Driver setup**
    - `passthrough-server/scripts/install-driver.bat` — install VHCI driver (admin required)
    - Requires Windows Test Mode (`bcdedit /set testsigning on`) OR signed driver
    - First-run wizard in companion server: check driver status, prompt installation

**Verification**:
- Plug USB flash drive into client PC
- Select in Moonlight UI → "Forwarding" status
- Drive appears in server's File Explorer with correct label/size
- Read/write files successfully
- Plug USB webcam → appears in server's Camera app
- Plug USB mouse → works natively in server's games
- Hot-unplug device → clean removal on server, UI updates
- Anti-cheat test: device appears as real USB hardware in Device Manager

---

### Phase 3: Bluetooth Passthrough (Two Modes)

**Goal**: Forward BT devices with two approaches depending on use case.

**Mode A — Adapter Forwarding (for Dolphin/Wii, full native BT)**:
Forward an entire USB Bluetooth dongle via USB/IP (Phase 2 mechanism). The server sees a real physical BT adapter. Software like Dolphin can discover and pair with BT devices natively. Requires a dedicated USB BT dongle on the client (the client's integrated BT remains for local use).

**Mode B — Individual Device Forwarding (for BT mice/keyboards, no dongle needed)**:
Forward individual BT HID devices as USB HID via VHCI. The server sees them as USB HID devices (not BT), but they work fully for mice, keyboards, etc. Uses Windows HID API to capture reports, constructs USB HID descriptors for VHCI. No second adapter required.

**Steps**:

14. **Rich BT enumeration — client**
    - Extend `DeviceEnumerator`:
      - Battery level: GATT Battery Service or `IOCTL_BTH_GET_DEVICE_INFO`
      - RSSI: WinRT `BluetoothLEDevice.BluetoothSignalStrengthFilter`
      - Service list: SDP records (classic BT) / GATT services (BLE)
      - Device type classification: HID, audio, serial, etc.
      - Identify USB BT adapters separately (for Adapter mode)
    - Periodic refresh every 30s for battery/RSSI
    - Display in UI: BT adapters in "Bluetooth Adapters" section, BT devices in "Bluetooth Devices" section

15. **Mode A: USB BT adapter forwarding**
    - Already supported via Phase 2 USB/IP path
    - UI: "Forward entire adapter" option on BT adapter entries
    - Warning: "This will disconnect all BT devices from this adapter on the client"
    - On server: BT adapter appears as real USB BT dongle, user can pair Wii Remote etc. via Dolphin or Windows BT settings

16. **Mode B: Individual BT HID forwarding**
    - `BtHidCapture` class (`app/streaming/passthrough/bthidcapture.h/.cpp`)
    - Open BT HID device via Windows HID API (`HidD_GetAttributes`, `HidP_GetCaps`)
    - Read HID report descriptor, construct USB HID device descriptor
    - Async read loop for input reports
    - Send as USB/IP HID device to server → VHCI creates USB HID device with matching report descriptor
    - Bidirectional: output reports (LEDs) flow back from server

17. **BT metadata forwarding**
    - `BT_DEVICE_INFO` messages carry battery, RSSI, services as supplemental data
    - Companion server displays in system tray tooltip
    - Client UI shows battery % and signal next to BT devices

18. **BT audio (stretch goal)**
    - A2DP/HFP capture → forward audio data → virtual audio device on server

**Verification**:
- Mode A: Forward USB BT dongle → appears in server Device Manager as BT adapter → pair Wii Remote via Dolphin → works
- Mode B: Forward BT mouse individually → appears as USB HID mouse on server → works in games
- BT keyboard via Mode B → typing works on server
- Battery % shown in Moonlight UI for BT devices
- Mode A warning shown when forwarding adapter

---

### Phase 4: Polish & Production

19. **Auto-forwarding** — restore device selections on session start (VID:PID + serial fingerprint)

20. **Performance** — `TCP_NODELAY`, high-priority I/O threads, batched URBs for bulk transfers

21. **Companion server system tray** — Win32 system tray icon, status popup, right-click menu (connected clients, forwarded devices, settings, driver status), auto-start with Windows option

22. **Error handling & resilience** — graceful recovery from server crash, client disconnect, driver errors. Status messages in Moonlight UI.

23. **Build script integration** — optional `--passthrough-server` flag in `build-personal.bat` to build companion server alongside Moonlight

## Extensibility: Gamepad Passthrough (Not Implemented)

The architecture deliberately supports adding gamepad passthrough in the future without ViGEmBus:
- **Via USB/IP (Phase 2 path)**: Forward a USB gamepad via VHCI → server sees native USB gamepad with real VID/PID. No virtual device layer, anti-cheat safe. Recommended future path.
- **Via BT adapter forwarding (Phase 3 Mode A)**: Forward USB BT dongle → pair BT gamepad on server natively.
- **Via individual BT HID (Phase 3 Mode B)**: Forward BT gamepad as USB HID → works but appears as generic USB HID, not BT gamepad.
- Gamepad-specific logic (report mapping, force feedback routing) can be added in `UsbIpExporter` or `BtHidCapture` without architectural changes.
- Currently not implemented because Moonlight's native input handles gamepads well.

## Relevant Files

### Client-side — New files
- `app/streaming/passthrough/protocol.h` — Shared protocol (header-only, no Qt)
- `app/streaming/passthrough/passthroughclient.h/.cpp` — TCP client
- `app/streaming/passthrough/deviceenumerator.h/.cpp` — USB + BT enumeration + QAbstractListModel
- `app/streaming/passthrough/usbipexporter.h/.cpp` — libusb device capture + USB/IP URB handling
- `app/streaming/passthrough/bthidcapture.h/.cpp` — BT HID individual device forwarding (Phase 3 Mode B)
- `app/gui/PassthroughView.qml` — Device picker UI

### Client-side — Existing files to modify
- `app/streaming/session.h` — Add `PassthroughClient*` member
- `app/streaming/session.cpp` — Hook `connectionStarted()`/`connectionTerminated()`
- `app/settings/streamingpreferences.h/.cpp` — Add passthrough settings
- `app/gui/ClientSettingsDialog.qml` — Add passthrough section
- `app/gui/StreamSegue.qml` — Add passthrough panel button during streaming
- `app/app.pro` — Link `setupapi.lib`, `bthprops.lib`, `hid.lib`, libusb-1.0
- `app/qml.qrc` — Register PassthroughView.qml

### Server-side — New files
- `passthrough-server/CMakeLists.txt` — CMake build
- `passthrough-server/src/main.cpp` — Entry point (console + service mode)
- `passthrough-server/src/server.h/.cpp` — Winsock2 TCP server
- `passthrough-server/src/protocol.h` — Copy of shared protocol
- `passthrough-server/src/vhci_manager.h/.cpp` — VHCI driver interaction
- `passthrough-server/src/systray.h/.cpp` — Win32 system tray (Phase 4)
- `passthrough-server/driver/` — usbip-win VHCI driver files
- `passthrough-server/scripts/install-driver.bat` — Driver installer

### Build system
- `moonlight-qt.pro` — NOT modified (server builds separately with CMake)
- `scripts/build-personal.bat` — Optional `--passthrough-server` flag (Phase 4)

## Third-Party Dependencies

| Library | License | Side | Phase | Purpose |
|---------|---------|------|-------|---------|
| libusb-1.0 | LGPL-2.1 | Client | 2 | USB device capture (user-mode, no kernel driver) |
| usbip-win VHCI | GPL-3.0 | Server | 2 | Virtual USB host controller (native device nodes) |

Note: ViGEmBus deliberately excluded — triggers anti-cheat (BattlEye). VHCI approach creates real USB hardware nodes instead.

## Decisions

- **USB/IP via VHCI** (not ViGEmBus/vmulti) — creates genuinely native USB devices, invisible to anti-cheat
- **Independent TCP on port 47990** — no Moonlight protocol or Sunshine changes needed
- **Gamepads excluded** — already work through Moonlight's native input forwarding
- **libusb on client side** — user-mode USB access, no custom kernel driver needed on client
- **VHCI driver on server** — the only kernel component, creates real USB device nodes
- **Companion server is standalone C++** — minimal deps (Winsock2 + VHCI IOCTL), no Qt
- **BT devices forwarded as USB HID via VHCI** — native appearance on server

## Further Considerations

1. **VHCI driver signing**: Requires Windows Test Mode (`bcdedit /set testsigning on`) for personal use. For distribution, an EV cert or attestation signing is needed. Test-signing is fine for your personal builds.

2. **Device exclusion**: The UI should warn when trying to forward the system mouse/keyboard currently used for Moonlight control. Devices claimed by libusb become unavailable on the client until released.

3. **libusb device detach**: When a device is forwarded, libusb detaches it from Windows drivers on the client — the device disappears from the client PC and appears on the server. This is the expected VirtualHere-like behavior. On disconnect/stream end, the device is re-attached to client drivers automatically.
