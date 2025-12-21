import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.2

import StreamingPreferences 1.0
import SystemProperties 1.0

NavigableDialog {
    id: clientSettingsDialog
    
    property string clientName: ""
    property string clientUuid: ""
    property bool hasCustomSettings: false
    
    standardButtons: Dialog.Ok | Dialog.Cancel | Dialog.RestoreDefaults
    
    title: qsTr("Settings for %1").arg(clientName)
    
    onOpened: {
        // Load client-specific settings when dialog opens
        if (clientUuid !== "") {
            hasCustomSettings = StreamingPreferences.hasClientSettings(clientUuid)
            if (hasCustomSettings) {
                StreamingPreferences.loadForClient(clientUuid)
            }
            updateUIFromPreferences()
        }
    }
    
    onAccepted: {
        // Save settings to client-specific profile
        updatePreferencesFromUI()
        if (clientUuid !== "") {
            StreamingPreferences.saveForClient(clientUuid)
        }
    }
    
    onRejected: {
        // Reload global settings without saving
        StreamingPreferences.reload()
    }
    
    onReset: {
        // Reset to global defaults
        if (clientUuid !== "") {
            StreamingPreferences.resetClientSettings(clientUuid)
            hasCustomSettings = false
            updateUIFromPreferences()
        }
    }
    
    function updateUIFromPreferences() {
        resolutionComboBox.currentIndex = 0
        for (var i = 0; i < resolutionListModel.count; i++) {
            if (resolutionListModel.get(i).width === StreamingPreferences.width &&
                resolutionListModel.get(i).height === StreamingPreferences.height) {
                resolutionComboBox.currentIndex = i
                break
            }
        }
        
        fpsComboBox.currentIndex = 0
        for (i = 0; i < fpsListModel.count; i++) {
            if (fpsListModel.get(i).fps === StreamingPreferences.fps) {
                fpsComboBox.currentIndex = i
                break
            }
        }
        
        bitrateSlider.value = StreamingPreferences.bitrateKbps
        
        codecComboBox.currentIndex = 0
        for (i = 0; i < codecListModel.count; i++) {
            if (codecListModel.get(i).val === StreamingPreferences.videoCodecConfig) {
                codecComboBox.currentIndex = i
                break
            }
        }
        
        enableHdrCheckbox.checked = StreamingPreferences.enableHdr
    }
    
    function updatePreferencesFromUI() {
        StreamingPreferences.width = resolutionListModel.get(resolutionComboBox.currentIndex).width
        StreamingPreferences.height = resolutionListModel.get(resolutionComboBox.currentIndex).height
        StreamingPreferences.fps = fpsListModel.get(fpsComboBox.currentIndex).fps
        StreamingPreferences.bitrateKbps = bitrateSlider.value
        StreamingPreferences.videoCodecConfig = codecListModel.get(codecComboBox.currentIndex).val
        StreamingPreferences.enableHdr = enableHdrCheckbox.checked
    }
    
    ColumnLayout {
        width: parent.width
        spacing: 10
        
        Label {
            Layout.fillWidth: true
            text: hasCustomSettings ? 
                qsTr("This PC has custom streaming settings. Click 'Restore Defaults' to use global settings.") :
                qsTr("This PC uses global streaming settings. Changes here will create a custom profile for this PC.")
            wrapMode: Text.Wrap
            font.pointSize: 10
            color: hasCustomSettings ? "lightblue" : "lightgray"
        }
        
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Video Settings")
            font.pointSize: 12
            
            ColumnLayout {
                anchors.fill: parent
                spacing: 5
                
                Label {
                    text: qsTr("Resolution")
                    font.pointSize: 12
                }
                
                AutoResizingComboBox {
                    id: resolutionComboBox
                    Layout.fillWidth: true
                    textRole: "text"
                    model: ListModel {
                        id: resolutionListModel
                        ListElement { text: "720p (1280x720)"; width: 1280; height: 720 }
                        ListElement { text: "1080p (1920x1080)"; width: 1920; height: 1080 }
                        ListElement { text: "1440p (2560x1440)"; width: 2560; height: 1440 }
                        ListElement { text: "4K (3840x2160)"; width: 3840; height: 2160 }
                    }
                }
                
                Label {
                    text: qsTr("Frame Rate")
                    font.pointSize: 12
                }
                
                AutoResizingComboBox {
                    id: fpsComboBox
                    Layout.fillWidth: true
                    textRole: "text"
                    model: ListModel {
                        id: fpsListModel
                        ListElement { text: "30 FPS"; fps: 30 }
                        ListElement { text: "60 FPS"; fps: 60 }
                        ListElement { text: "90 FPS"; fps: 90 }
                        ListElement { text: "120 FPS"; fps: 120 }
                    }
                }
                
                Label {
                    text: qsTr("Video Codec")
                    font.pointSize: 12
                }
                
                AutoResizingComboBox {
                    id: codecComboBox
                    Layout.fillWidth: true
                    textRole: "text"
                    model: ListModel {
                        id: codecListModel
                        ListElement { text: "Automatic"; val: 0 } // VCC_AUTO
                        ListElement { text: "H.264"; val: 1 }      // VCC_FORCE_H264
                        ListElement { text: "HEVC (H.265)"; val: 2 } // VCC_FORCE_HEVC
                        ListElement { text: "AV1"; val: 4 }        // VCC_FORCE_AV1
                    }
                }
                
                CheckBox {
                    id: enableHdrCheckbox
                    text: qsTr("Enable HDR")
                    font.pointSize: 12
                    enabled: SystemProperties.supportsHdr
                }
                
                Label {
                    text: qsTr("Bitrate: %1 Mbps").arg((bitrateSlider.value / 1000).toFixed(1))
                    font.pointSize: 12
                }
                
                Slider {
                    id: bitrateSlider
                    Layout.fillWidth: true
                    from: 500
                    to: 150000
                    stepSize: 500
                    value: 10000
                }
            }
        }
        
        Label {
            Layout.fillWidth: true
            text: qsTr("Note: Only key streaming settings are shown here. For advanced settings, use the global Settings menu.")
            wrapMode: Text.Wrap
            font.pointSize: 9
            font.italic: true
        }
    }
}
