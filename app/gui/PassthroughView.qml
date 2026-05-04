import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.2

import StreamingPreferences 1.0

// Device Passthrough overlay – shown during streaming via Ctrl+Alt+Shift+P
//
// IMPORTANT: The device list uses a SINGLE unified ListView with inline delegates.
// Do NOT refactor the delegate into a file-scope Component: doing so breaks model
// role access because the Component's creation context is the document root, not
// the ListView delegate scope that carries role properties (deviceName, etc.).
Item {
    id: passthroughView

    property var passthroughClient: null
    property string searchText: ""
    property int sortMode: 0       // 0=default, 1=name, 2=VID:PID, 3=last added, 4=class
    property int filterTransport: 0 // 0=all, 1=USB only, 2=BT only
    property int filterClass: 0     // 0=all, 1=keyboard, 2=mouse, 3=gamepad, 5=storage, 6=audio, 7=webcam

    signal closeRequested()

    Rectangle {
        anchors.fill: parent
        color: "#e0000000"

        MouseArea {
            anchors.fill: parent
            onClicked: closeRequested()
        }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.92, 960)
        height: Math.min(parent.height * 0.90, 720)
        color: "#303030"
        radius: 8
        border.color: "#505050"
        border.width: 1

        // Prevent clicks from closing
        MouseArea {
            anchors.fill: parent
            onClicked: {} // absorb
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8

            // ─── title bar ───
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: qsTr("Device Passthrough")
                    font.pointSize: 16
                    font.bold: true
                    color: "white"
                    Layout.fillWidth: true
                }

                Label {
                    id: statusLabel
                    text: passthroughClient ? passthroughClient.statusText : qsTr("Not available")
                    font.pointSize: 10
                    color: passthroughClient && passthroughClient.connected ? "#4CAF50" : "#FF9800"
                    elide: Text.ElideRight
                }

                Button {
                    text: qsTr("Refresh")
                    onClicked: {
                        if (passthroughClient) passthroughClient.refreshDevices()
                    }
                }

                Button {
                    text: qsTr("Close")
                    onClicked: closeRequested()
                }
            }

            // ─── VHCI driver warning ───
            Rectangle {
                Layout.fillWidth: true
                height: vhciWarningText.implicitHeight + 16
                color: "#4D6B1F1F"
                radius: 4
                border.color: "#FF5252"
                border.width: 1
                visible: passthroughClient
                         && passthroughClient.connected
                         && !passthroughClient.vhciAvailable

                Label {
                    id: vhciWarningText
                    anchors.fill: parent
                    anchors.margins: 8
                    text: qsTr("VHCI driver not available on server. Install the driver to enable device forwarding.")
                    color: "#FF5252"
                    wrapMode: Text.WordWrap
                }
            }

            // ─── search & filter bar ───
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                // Search field
                Rectangle {
                    Layout.fillWidth: true
                    height: 32
                    color: "#404040"
                    radius: 4
                    border.color: searchField.activeFocus ? "#4CAF50" : "#555555"
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 4

                        Label {
                            text: "\uD83D\uDD0D" // 🔍
                            font.pointSize: 12
                            color: "#909090"
                        }

                        TextInput {
                            id: searchField
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: "white"
                            font.pointSize: 10
                            clip: true
                            verticalAlignment: TextInput.AlignVCenter
                            selectByMouse: true

                            onTextChanged: searchText = text

                            // Placeholder
                            Text {
                                anchors.fill: parent
                                verticalAlignment: Text.AlignVCenter
                                text: qsTr("Search by name, VID:PID, manufacturer, serial...")
                                color: "#707070"
                                font.pointSize: 10
                                visible: searchField.text.length === 0 && !searchField.activeFocus
                            }
                        }

                        // Clear button
                        Label {
                            text: "\u2715" // ✕
                            font.pointSize: 10
                            color: "#909090"
                            visible: searchField.text.length > 0

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { searchField.text = "" }
                            }
                        }
                    }
                }

                // Transport filter
                ComboBox {
                    id: transportFilter
                    implicitWidth: 110
                    model: [qsTr("All types"), qsTr("USB only"), qsTr("BT only")]
                    currentIndex: filterTransport
                    onCurrentIndexChanged: filterTransport = currentIndex
                }

                // Class filter
                ComboBox {
                    id: classFilter
                    implicitWidth: 130
                    model: [
                        qsTr("All classes"),
                        qsTr("Keyboards"),
                        qsTr("Mice"),
                        qsTr("Gamepads"),
                        qsTr("Storage"),
                        qsTr("Audio"),
                        qsTr("Webcams")
                    ]
                    currentIndex: 0
                    onCurrentIndexChanged: {
                        var classMap = [0, 1, 2, 3, 5, 6, 7]
                        filterClass = classMap[currentIndex] || 0
                    }
                }

                // Sort
                ComboBox {
                    id: sortCombo
                    implicitWidth: 130
                    model: [
                        qsTr("Default order"),
                        qsTr("Sort by name"),
                        qsTr("Sort by VID:PID"),
                        qsTr("Last added first"),
                        qsTr("Sort by class")
                    ]
                    currentIndex: sortMode
                    onCurrentIndexChanged: sortMode = currentIndex
                }
            }

            // ─── device count label ───
            Label {
                Layout.fillWidth: true
                text: {
                    var total = passthroughClient ? passthroughClient.deviceEnumerator.count : 0
                    var shown = filteredModel.count
                    if (shown === total)
                        return qsTr("%1 device(s)").arg(total)
                    return qsTr("Showing %1 of %2 device(s)").arg(shown).arg(total)
                }
                font.pointSize: 9
                color: "#909090"
            }

            // ─── unified device list ───
            // Uses a JS-based filter/sort layer on top of the C++ model.
            ListView {
                id: deviceListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4

                ScrollBar.vertical: ScrollBar {
                    policy: deviceListView.contentHeight > deviceListView.height
                            ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                }

                model: filteredModel

                // Section header – only rendered when that transport type is present
                section.property: "sectionKey"
                section.criteria: ViewSection.FullString
                section.delegate: Column {
                    width: deviceListView.width
                    topPadding: 4
                    bottomPadding: 2
                    spacing: 4

                    Label {
                        width: parent.width
                        text: section === "1" ? qsTr("USB Devices") : qsTr("Bluetooth Devices")
                        font.pointSize: 12
                        font.bold: true
                        color: "#B0B0B0"
                    }

                    // BT-only informational note
                    Label {
                        width: parent.width
                        visible: section === "2"
                        height: visible ? implicitHeight : 0
                        text: qsTr("Bluetooth HID devices (keyboards, mice) are forwarded as USB HID devices on the server. Non-HID Bluetooth devices are not supported.")
                        color: "#707070"
                        font.pointSize: 8
                        wrapMode: Text.WordWrap
                    }
                }

                // ─── inline delegate ───
                delegate: Rectangle {
                    width: deviceListView.width
                    height: rowContent.implicitHeight + 12
                    color: model.isForwarding ? "#1A4CAF50" : "#1AFFFFFF"
                    radius: 4
                    border.color: model.isForwarding ? "#4CAF50" : "transparent"
                    border.width: model.isForwarding ? 1 : 0

                    RowLayout {
                        id: rowContent
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 10

                        // ── device class icon ──
                        Rectangle {
                            width: 36
                            height: 36
                            radius: 4
                            color: "#404040"

                            Text {
                                anchors.centerIn: parent
                                font.pointSize: 16
                                font.family: "Segoe UI Emoji,Apple Color Emoji,Noto Color Emoji,sans-serif"
                                text: {
                                    switch (model.deviceClass) {
                                    case 1: return "\u2328"      // ⌨ keyboard
                                    case 2: return "\uD83D\uDDB1" // 🖱 mouse
                                    case 3: return "\uD83C\uDFAE" // 🎮 gamepad
                                    case 5: return "\uD83D\uDCBE" // 💾 storage
                                    case 6: return "\uD83D\uDD0A" // 🔊 audio
                                    case 7: return "\uD83D\uDCF7" // 📷 webcam
                                    case 8: return "\uD83D\uDCE1" // 📡 BT adapter
                                    default: return "\uD83D\uDD0C" // 🔌 other USB
                                    }
                                }
                            }
                        }

                        // ── device name & metadata ──
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            // Row 1: device name
                            Label {
                                text: model.deviceName
                                font.pointSize: 11
                                color: "white"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            // Row 2: class, VID:PID, manufacturer
                            RowLayout {
                                spacing: 8
                                Layout.fillWidth: true

                                Label {
                                    text: model.deviceClassName
                                    font.pointSize: 9
                                    color: "#B0B0B0"
                                }

                                Label {
                                    text: model.vidPidText
                                    font.pointSize: 9
                                    color: "#808080"
                                    font.family: "Consolas,monospace"
                                }

                                Label {
                                    visible: model.manufacturer !== ""
                                    height: visible ? implicitHeight : 0
                                    text: visible ? model.manufacturer : ""
                                    font.pointSize: 8
                                    color: "#70A0D0"
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: 180
                                }
                            }

                            // Row 3: serial, location, driver, battery, added time
                            RowLayout {
                                spacing: 8
                                Layout.fillWidth: true

                                Label {
                                    visible: model.serialNumber !== ""
                                    height: visible ? implicitHeight : 0
                                    text: visible ? "S/N: " + model.serialNumber : ""
                                    font.pointSize: 8
                                    color: "#909060"
                                    font.family: "Consolas,monospace"
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: 150
                                }

                                Label {
                                    visible: model.transport === 1 && model.locationInfo !== ""
                                    height: visible ? implicitHeight : 0
                                    text: visible ? "\uD83D\uDCCD " + model.locationInfo : "" // 📍
                                    font.pointSize: 8
                                    color: "#7090B0"
                                }

                                Label {
                                    visible: model.transport === 1 && model.driver !== ""
                                    height: visible ? implicitHeight : 0
                                    text: visible ? "[" + model.driver + "]" : ""
                                    font.pointSize: 8
                                    color: "#808060"
                                    font.family: "Consolas,monospace"
                                }

                                Label {
                                    visible: model.transport === 2 && model.batteryPercent >= 0
                                    height: visible ? implicitHeight : 0
                                    text: visible ? qsTr("Battery: %1%").arg(model.batteryPercent) : ""
                                    font.pointSize: 9
                                    color: model.batteryPercent > 20 ? "#4CAF50" : "#FF5252"
                                }

                                Label {
                                    visible: model.transport === 2
                                    height: visible ? implicitHeight : 0
                                    text: visible ? (model.btConnected ? qsTr("Connected") : qsTr("Disconnected")) : ""
                                    font.pointSize: 9
                                    color: model.btConnected ? "#4CAF50" : "#FF5252"
                                }

                                // Device ID (useful for identification)
                                Label {
                                    text: "#" + model.deviceId
                                    font.pointSize: 7
                                    color: "#606060"
                                    font.family: "Consolas,monospace"
                                }
                            }
                        }

                        // ── forwarding status badge ──
                        Label {
                            text: model.statusText
                            font.pointSize: 9
                            color: model.isForwarding ? "#4CAF50" : "#808080"
                        }

                        // ── auto-forward checkbox ──
                        CheckBox {
                            text: qsTr("Auto")
                            checked: model.autoForward
                            onClicked: {
                                if (passthroughClient) {
                                    passthroughClient.deviceEnumerator.setAutoForward(model.sourceIndex, checked)
                                }
                            }

                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Automatically forward this device when streaming starts")
                        }

                        // ── forwarding toggle ──
                        Switch {
                            checked: model.isForwarding
                            enabled: passthroughClient
                                     && passthroughClient.connected
                                     && passthroughClient.vhciAvailable
                                     && (model.transport !== 2 || model.btConnected)
                                     && (model.transport !== 2
                                         || model.deviceClass === 1
                                         || model.deviceClass === 2
                                         || model.deviceClass === 3
                                         || model.deviceClass === 4)

                            onClicked: {
                                if (passthroughClient) {
                                    if (model.isForwarding) {
                                        passthroughClient.detachDevice(model.deviceId)
                                    } else {
                                        passthroughClient.attachDevice(model.deviceId)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ─── empty state placeholder ───
            Label {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                visible: deviceListView.count === 0
                text: {
                    if (searchText.length > 0 || filterTransport > 0 || filterClass > 0)
                        return qsTr("No devices match the current search/filter. Try clearing your filters.")
                    return qsTr("No devices found. Click Refresh to scan again.")
                }
                color: "#808080"
                font.pointSize: 10
                horizontalAlignment: Text.AlignHCenter
            }

            // ─── bottom hint ───
            Label {
                Layout.fillWidth: true
                text: qsTr("Toggle devices to forward them to the server. USB devices are forwarded natively. Bluetooth HID devices are presented as USB HID on the server.")
                color: "#808080"
                font.pointSize: 9
                wrapMode: Text.WordWrap
            }
        }
    }

    // ─── Filtered/sorted model (JS ListModel) ───
    // Rebuilds whenever the source model changes, or search/filter/sort changes.
    ListModel {
        id: filteredModel
    }

    Connections {
        target: passthroughClient ? passthroughClient.deviceEnumerator : null
        onDevicesChanged: rebuildFilteredModel()
        onDataChanged: rebuildFilteredModel()
    }

    onSearchTextChanged: rebuildFilteredModel()
    onSortModeChanged: rebuildFilteredModel()
    onFilterTransportChanged: rebuildFilteredModel()
    onFilterClassChanged: rebuildFilteredModel()
    onPassthroughClientChanged: rebuildFilteredModel()

    function rebuildFilteredModel() {
        filteredModel.clear()

        if (!passthroughClient) return
        var enumerator = passthroughClient.deviceEnumerator
        if (!enumerator) return

        var items = []
        var count = enumerator.count

        for (var i = 0; i < count; i++) {
            var idx = enumerator.index(i, 0)
            var item = {
                deviceId: enumerator.data(idx, 257),       // DeviceIdRole
                deviceName: enumerator.data(idx, 258),      // NameRole
                vendorId: enumerator.data(idx, 259),        // VendorIdRole
                productId: enumerator.data(idx, 260),       // ProductIdRole
                transport: enumerator.data(idx, 261),       // TransportRole
                deviceClass: enumerator.data(idx, 262),     // DeviceClassRole
                serialNumber: enumerator.data(idx, 263),    // SerialNumberRole
                isForwarding: enumerator.data(idx, 264),    // IsForwardingRole
                autoForward: enumerator.data(idx, 265),     // AutoForwardRole
                statusText: enumerator.data(idx, 266),      // StatusTextRole
                deviceClassName: enumerator.data(idx, 267), // DeviceClassNameRole
                vidPidText: enumerator.data(idx, 268),      // VidPidTextRole
                batteryPercent: enumerator.data(idx, 269),  // BatteryPercentRole
                rssi: enumerator.data(idx, 270),            // RssiRole
                btPaired: enumerator.data(idx, 271),        // BtPairedRole
                btConnected: enumerator.data(idx, 272),     // BtConnectedRole
                locationInfo: enumerator.data(idx, 273),    // LocationInfoRole
                driver: enumerator.data(idx, 274),          // DriverRole
                manufacturer: enumerator.data(idx, 275),    // ManufacturerRole
                sourceIndex: i,
                sectionKey: "" + enumerator.data(idx, 261)  // transport as string for section
            }

            // Apply transport filter
            if (filterTransport === 1 && item.transport !== 1) continue
            if (filterTransport === 2 && item.transport !== 2) continue

            // Apply class filter
            if (filterClass > 0 && item.deviceClass !== filterClass) continue

            // Apply search filter
            if (searchText.length > 0) {
                var q = searchText.toLowerCase()
                var haystack = (item.deviceName + " " + item.vidPidText + " " +
                                item.manufacturer + " " + item.serialNumber + " " +
                                item.deviceClassName + " " + item.driver + " " +
                                item.locationInfo).toLowerCase()
                if (haystack.indexOf(q) < 0) continue
            }

            items.push(item)
        }

        // Apply sort
        if (sortMode === 1) {
            items.sort(function(a, b) { return a.deviceName.localeCompare(b.deviceName) })
        } else if (sortMode === 2) {
            items.sort(function(a, b) { return a.vidPidText.localeCompare(b.vidPidText) })
        } else if (sortMode === 3) {
            items.reverse() // newest first (devices are enumerated in order)
        } else if (sortMode === 4) {
            items.sort(function(a, b) { return a.deviceClass - b.deviceClass })
        }

        for (var j = 0; j < items.length; j++) {
            filteredModel.append(items[j])
        }
    }
}
