import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.2

import StreamingPreferences 1.0

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
            spacing: 12

            // ─── Title bar ───
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

            // ─── VHCI warning ───
            Rectangle {
                Layout.fillWidth: true
                height: vhciWarningText.implicitHeight + 16
                color: "#4D6B1F1F"
                radius: 4
                border.color: "#FF5252"
                border.width: 1
                visible: passthroughClient && passthroughClient.connected && !passthroughClient.vhciAvailable

                Label {
                    id: vhciWarningText
                    anchors.fill: parent
                    anchors.margins: 8
                    text: qsTr("VHCI driver not available on server. Install the driver to enable device forwarding.")
                    color: "#FF5252"
                    wrapMode: Text.WordWrap
                }
            }

            // ─── Section: USB Devices ───
            Label {
                text: qsTr("USB Devices")
                font.pointSize: 12
                font.bold: true
                color: "#B0B0B0"
                visible: usbListView.count > 0
            }

            ListView {
                id: usbListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: contentHeight
                Layout.maximumHeight: parent.height * 0.35
                clip: true
                spacing: 4
                visible: count > 0

                model: passthroughClient ? passthroughClient.deviceEnumerator : null

                delegate: Loader {
                    width: usbListView.width
                    // Only show USB devices (transport === 1)
                    active: transport === 1
                    visible: active
                    height: active ? item.height : 0

                    sourceComponent: deviceDelegate
                    property int delegateIndex: index
                    property var listViewRef: usbListView
                }
            }

            // ─── Section: Bluetooth Devices ───
            Label {
                text: qsTr("Bluetooth Devices")
                font.pointSize: 12
                font.bold: true
                color: "#B0B0B0"
                visible: btListView.count > 0
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Bluetooth HID devices (keyboards, mice) are forwarded as USB HID devices on the server. Non-HID Bluetooth devices are not supported.")
                color: "#707070"
                font.pointSize: 8
                wrapMode: Text.WordWrap
                visible: btListView.count > 0
            }

            ListView {
                id: btListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: contentHeight
                Layout.maximumHeight: parent.height * 0.35
                clip: true
                spacing: 4
                visible: count > 0

                model: passthroughClient ? passthroughClient.deviceEnumerator : null

                delegate: Loader {
                    width: btListView.width
                    // Only show Bluetooth devices (transport === 2)
                    active: transport === 2
                    visible: active
                    height: active ? item.height : 0

                    sourceComponent: deviceDelegate
                    property int delegateIndex: index
                    property var listViewRef: btListView
                }
            }

            // ─── Hint text ───
            Label {
                Layout.fillWidth: true
                text: qsTr("Toggle devices to forward them to the server. USB devices are forwarded natively. Bluetooth HID devices are presented as USB HID on the server.")
                color: "#808080"
                font.pointSize: 9
                wrapMode: Text.WordWrap
            }
        }
    }

    Component {
        id: deviceDelegate

        Rectangle {
            width: parent ? parent.width : 100
            height: deviceRow.implicitHeight + 12
            color: isForwarding ? "#1A4CAF50" : "#1AFFFFFF"
            radius: 4
            border.color: isForwarding ? "#4CAF50" : "transparent"
            border.width: isForwarding ? 1 : 0

            RowLayout {
                id: deviceRow
                anchors.fill: parent
                anchors.margins: 6
                spacing: 10

                // Device icon placeholder
                Rectangle {
                    width: 32
                    height: 32
                    radius: 4
                    color: "#404040"

                    Label {
                        anchors.centerIn: parent
                        text: {
                            switch (deviceClass) {
                            case 1: return "⌨"  // Keyboard
                            case 2: return "🖱"  // Mouse
                            case 3: return "🎮"  // Gamepad
                            case 5: return "💾"  // Storage
                            case 6: return "🔊"  // Audio
                            case 7: return "📷"  // Webcam
                            case 8: return "📡"  // BT Adapter
                            default: return "🔌"  // Other
                            }
                        }
                        font.pointSize: 14
                    }
                }

                // Device info
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

                        Label {
                            text: deviceClassName
                            font.pointSize: 9
                            color: "#B0B0B0"
                        }

                        Label {
                            text: vidPidText
                            font.pointSize: 9
                            color: "#808080"
                            font.family: "Consolas"
                        }

                        Label {
                            text: transport === 2 && batteryPercent >= 0
                                  ? qsTr("Battery: %1%").arg(batteryPercent)
                                  : ""
                            font.pointSize: 9
                            color: batteryPercent > 20 ? "#4CAF50" : "#FF5252"
                            visible: text !== ""
                        }

                        Label {
                            text: transport === 2
                                  ? (btConnected ? qsTr("Connected") : qsTr("Disconnected"))
                                  : ""
                            font.pointSize: 9
                            color: btConnected ? "#4CAF50" : "#FF5252"
                            visible: text !== ""
                        }
                    }
                }

                // Status
                Label {
                    text: statusText
                    font.pointSize: 9
                    color: isForwarding ? "#4CAF50" : "#808080"
                }

                // Auto-forward checkbox
                CheckBox {
                    id: autoFwdCheck
                    text: qsTr("Auto")
                    checked: autoForward
                    onCheckedChanged: {
                        if (passthroughClient) {
                            passthroughClient.deviceEnumerator.setAutoForward(index, checked)
                        }
                    }

                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Automatically forward this device when streaming starts")
                }

                // Forward toggle
                Switch {
                    checked: isForwarding
                    enabled: passthroughClient && passthroughClient.connected && passthroughClient.vhciAvailable
                             && (transport !== 2 || btConnected)
                             && (transport !== 2 || deviceClass === 1 || deviceClass === 2 || deviceClass === 3 || deviceClass === 4)
                    onClicked: {
                        if (passthroughClient) {
                            if (!checked) {
                                passthroughClient.attachDevice(deviceId)
                            } else {
                                passthroughClient.detachDevice(deviceId)
                            }
                        }
                    }
                }
            }
        }
    }
}
