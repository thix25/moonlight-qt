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
        width: Math.min(parent.width * 0.85, 700)
        height: Math.min(parent.height * 0.85, 600)
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
            spacing: 10

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

            // ─── unified device list ───
            // Uses ListView section headers (transport "1" = USB, "2" = Bluetooth).
            // DeviceEnumerator always produces USB devices before BT devices so the
            // sections are naturally contiguous – no proxy sort needed.
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

                model: passthroughClient ? passthroughClient.deviceEnumerator : null

                // Section header – only rendered when that transport type is present
                section.property: "transport"
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
                // All model roles (deviceName, transport, deviceClass, …) are
                // accessible here because the delegate IS the ListView delegate scope.
                // Do NOT extract this into a file-scope Component.
                delegate: Rectangle {
                    width: deviceListView.width
                    height: rowContent.implicitHeight + 12
                    color: isForwarding ? "#1A4CAF50" : "#1AFFFFFF"
                    radius: 4
                    border.color: isForwarding ? "#4CAF50" : "transparent"
                    border.width: isForwarding ? 1 : 0

                    RowLayout {
                        id: rowContent
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 10

                        // ── device class icon ──
                        Rectangle {
                            width: 32
                            height: 32
                            radius: 4
                            color: "#404040"

                            Text {
                                anchors.centerIn: parent
                                font.pointSize: 16
                                font.family: "Segoe UI Emoji,Apple Color Emoji,Noto Color Emoji,sans-serif"
                                text: {
                                    switch (deviceClass) {
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

                            Label {
                                text: deviceName
                                font.pointSize: 11
                                color: "white"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            RowLayout {
                                spacing: 8
                                Layout.fillWidth: true

                                Label {
                                    text: deviceClassName
                                    font.pointSize: 9
                                    color: "#B0B0B0"
                                }

                                Label {
                                    text: vidPidText
                                    font.pointSize: 9
                                    color: "#808080"
                                    font.family: "Consolas,monospace"
                                }

                                Label {
                                    visible: transport === 2 && batteryPercent >= 0
                                    height: visible ? implicitHeight : 0
                                    text: visible ? qsTr("Battery: %1%").arg(batteryPercent) : ""
                                    font.pointSize: 9
                                    color: batteryPercent > 20 ? "#4CAF50" : "#FF5252"
                                }

                                Label {
                                    visible: transport === 2
                                    height: visible ? implicitHeight : 0
                                    text: visible ? (btConnected ? qsTr("Connected") : qsTr("Disconnected")) : ""
                                    font.pointSize: 9
                                    color: btConnected ? "#4CAF50" : "#FF5252"
                                }
                            }
                        }

                        // ── forwarding status badge ──
                        Label {
                            text: statusText
                            font.pointSize: 9
                            color: isForwarding ? "#4CAF50" : "#808080"
                        }

                        // ── auto-forward checkbox ──
                        CheckBox {
                            text: qsTr("Auto")
                            checked: autoForward
                            // onClicked fires only on user interaction,
                            // not on programmatic model updates via the binding.
                            onClicked: {
                                if (passthroughClient) {
                                    passthroughClient.deviceEnumerator.setAutoForward(index, checked)
                                }
                            }

                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Automatically forward this device when streaming starts")
                        }

                        // ── forwarding toggle ──
                        Switch {
                            checked: isForwarding
                            enabled: passthroughClient
                                     && passthroughClient.connected
                                     && passthroughClient.vhciAvailable
                                     && (transport !== 2 || btConnected)
                                     && (transport !== 2
                                         || deviceClass === 1
                                         || deviceClass === 2
                                         || deviceClass === 3
                                         || deviceClass === 4)

                            // Use the MODEL's isForwarding (not `checked`) to determine
                            // the desired action so the logic is independent of QML
                            // binding-revert timing.
                            onClicked: {
                                if (passthroughClient) {
                                    if (isForwarding) {
                                        passthroughClient.detachDevice(deviceId)
                                    } else {
                                        passthroughClient.attachDevice(deviceId)
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
                text: qsTr("No devices found. Click Refresh to scan again.")
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
}
