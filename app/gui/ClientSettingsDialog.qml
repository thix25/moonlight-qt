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
    property bool useGlobalSettings: false
    
    standardButtons: Dialog.Ok | Dialog.Cancel | Dialog.RestoreDefaults
    
    title: qsTr("Settings for %1").arg(clientName)
    
    width: 800
    height: 650
    
    Component.onCompleted: {
        // Connect to the reset signal
        reset.connect(function() {
            // Reset to global defaults
            if (clientUuid !== "") {
                StreamingPreferences.resetClientSettings(clientUuid)
                hasCustomSettings = false
                useGlobalSettings = true
                StreamingPreferences.reload()
                updateUIFromPreferences()
            }
        })
    }
    
    onOpened: {
        // Load client-specific settings when dialog opens
        if (clientUuid !== "") {
            hasCustomSettings = StreamingPreferences.hasClientSettings(clientUuid)
            useGlobalSettings = !hasCustomSettings
            
            if (hasCustomSettings) {
                StreamingPreferences.loadForClient(clientUuid)
            } else {
                // Load global settings to start with
                StreamingPreferences.reload()
            }
            updateUIFromPreferences()
        }
    }
    
    onAccepted: {
        // Only save settings if not using global settings
        if (!useGlobalSettings && clientUuid !== "") {
            updatePreferencesFromUI()
            StreamingPreferences.saveForClient(clientUuid)
        } else if (useGlobalSettings && hasCustomSettings && clientUuid !== "") {
            // User switched to global settings, remove custom settings
            StreamingPreferences.resetClientSettings(clientUuid)
        }
        // Reload global settings after saving
        StreamingPreferences.reload()
    }
    
    onRejected: {
        // Reload global settings without saving
        StreamingPreferences.reload()
    }
    
    function updateUIFromPreferences() {
        // Video settings
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
        enableYUV444Checkbox.checked = StreamingPreferences.enableYUV444
        unlockBitrateCheckbox.checked = StreamingPreferences.unlockBitrate
        
        windowModeComboBox.currentIndex = 0
        for (i = 0; i < windowModeListModel.count; i++) {
            if (windowModeListModel.get(i).val === StreamingPreferences.windowMode) {
                windowModeComboBox.currentIndex = i
                break
            }
        }
        
        vsyncCheckbox.checked = StreamingPreferences.enableVsync
        framePacingCheckbox.checked = StreamingPreferences.framePacing
        
        decoderComboBox.currentIndex = 0
        for (i = 0; i < decoderListModel.count; i++) {
            if (decoderListModel.get(i).val === StreamingPreferences.videoDecoderSelection) {
                decoderComboBox.currentIndex = i
                break
            }
        }
        
        // Audio settings
        audioComboBox.currentIndex = 0
        for (i = 0; i < audioListModel.count; i++) {
            if (audioListModel.get(i).val === StreamingPreferences.audioConfig) {
                audioComboBox.currentIndex = i
                break
            }
        }
        
        muteHostCheckbox.checked = !StreamingPreferences.playAudioOnHost
        muteOnFocusLossCheckbox.checked = StreamingPreferences.muteOnFocusLoss
        
        // Input settings
        absoluteMouseCheckbox.checked = StreamingPreferences.absoluteMouseMode
        absoluteTouchCheckbox.checked = !StreamingPreferences.absoluteTouchMode
        swapMouseButtonsCheckbox.checked = StreamingPreferences.swapMouseButtons
        reverseScrollCheckbox.checked = StreamingPreferences.reverseScrollDirection
        
        captureSysKeysCheckbox.checked = StreamingPreferences.captureSysKeysMode !== StreamingPreferences.CSK_OFF
        if (StreamingPreferences.captureSysKeysMode === StreamingPreferences.CSK_FULLSCREEN) {
            captureSysKeysModeComboBox.currentIndex = 0
        } else {
            captureSysKeysModeComboBox.currentIndex = 1
        }
        
        // Gamepad settings
        swapFaceButtonsCheckbox.checked = StreamingPreferences.swapFaceButtons
        singleControllerCheckbox.checked = !StreamingPreferences.multiController
        gamepadMouseCheckbox.checked = StreamingPreferences.gamepadMouse
        backgroundGamepadCheckbox.checked = StreamingPreferences.backgroundGamepad
        
        // Host settings
        optimizeGameSettingsCheckbox.checked = StreamingPreferences.gameOptimizations
        quitAppAfterCheckbox.checked = StreamingPreferences.quitAppAfter
        
        // UI/Network settings
        connectionWarningsCheckbox.checked = StreamingPreferences.connectionWarnings
        configurationWarningsCheckbox.checked = StreamingPreferences.configurationWarnings
        richPresenceCheckbox.checked = StreamingPreferences.richPresence
        keepAwakeCheckbox.checked = StreamingPreferences.keepAwake
        enableMdnsCheckbox.checked = StreamingPreferences.enableMdns
        detectNetworkBlockingCheckbox.checked = StreamingPreferences.detectNetworkBlocking
        showPerformanceOverlayCheckbox.checked = StreamingPreferences.showPerformanceOverlay
    }
    
    function updatePreferencesFromUI() {
        // Video settings
        StreamingPreferences.width = resolutionListModel.get(resolutionComboBox.currentIndex).width
        StreamingPreferences.height = resolutionListModel.get(resolutionComboBox.currentIndex).height
        StreamingPreferences.fps = fpsListModel.get(fpsComboBox.currentIndex).fps
        StreamingPreferences.bitrateKbps = bitrateSlider.value
        StreamingPreferences.videoCodecConfig = codecListModel.get(codecComboBox.currentIndex).val
        StreamingPreferences.enableHdr = enableHdrCheckbox.checked
        StreamingPreferences.enableYUV444 = enableYUV444Checkbox.checked
        StreamingPreferences.unlockBitrate = unlockBitrateCheckbox.checked
        StreamingPreferences.windowMode = windowModeListModel.get(windowModeComboBox.currentIndex).val
        StreamingPreferences.enableVsync = vsyncCheckbox.checked
        StreamingPreferences.framePacing = framePacingCheckbox.checked
        StreamingPreferences.videoDecoderSelection = decoderListModel.get(decoderComboBox.currentIndex).val
        
        // Audio settings
        StreamingPreferences.audioConfig = audioListModel.get(audioComboBox.currentIndex).val
        StreamingPreferences.playAudioOnHost = !muteHostCheckbox.checked
        StreamingPreferences.muteOnFocusLoss = muteOnFocusLossCheckbox.checked
        
        // Input settings
        StreamingPreferences.absoluteMouseMode = absoluteMouseCheckbox.checked
        StreamingPreferences.absoluteTouchMode = !absoluteTouchCheckbox.checked
        StreamingPreferences.swapMouseButtons = swapMouseButtonsCheckbox.checked
        StreamingPreferences.reverseScrollDirection = reverseScrollCheckbox.checked
        
        if (!captureSysKeysCheckbox.checked) {
            StreamingPreferences.captureSysKeysMode = StreamingPreferences.CSK_OFF
        } else {
            StreamingPreferences.captureSysKeysMode = captureSysKeysModeListModel.get(captureSysKeysModeComboBox.currentIndex).val
        }
        
        // Gamepad settings
        StreamingPreferences.swapFaceButtons = swapFaceButtonsCheckbox.checked
        StreamingPreferences.multiController = !singleControllerCheckbox.checked
        StreamingPreferences.gamepadMouse = gamepadMouseCheckbox.checked
        StreamingPreferences.backgroundGamepad = backgroundGamepadCheckbox.checked
        
        // Host settings
        StreamingPreferences.gameOptimizations = optimizeGameSettingsCheckbox.checked
        StreamingPreferences.quitAppAfter = quitAppAfterCheckbox.checked
        
        // UI/Network settings
        StreamingPreferences.connectionWarnings = connectionWarningsCheckbox.checked
        StreamingPreferences.configurationWarnings = configurationWarningsCheckbox.checked
        StreamingPreferences.richPresence = richPresenceCheckbox.checked
        StreamingPreferences.keepAwake = keepAwakeCheckbox.checked
        StreamingPreferences.enableMdns = enableMdnsCheckbox.checked
        StreamingPreferences.detectNetworkBlocking = detectNetworkBlockingCheckbox.checked
        StreamingPreferences.showPerformanceOverlay = showPerformanceOverlayCheckbox.checked
    }
    
    Flickable {
        anchors.fill: parent
        contentWidth: mainLayout.width
        contentHeight: mainLayout.height
        clip: true
        
        ScrollBar.vertical: ScrollBar {}
        
        ColumnLayout {
            id: mainLayout
            width: clientSettingsDialog.availableWidth
            spacing: 10
            
            // Global settings checkbox
            CheckBox {
                id: useGlobalSettingsCheckbox
                Layout.fillWidth: true
                text: qsTr("Use global settings for this PC")
                font.pointSize: 12
                font.bold: true
                checked: useGlobalSettings
                onCheckedChanged: {
                    useGlobalSettings = checked
                    settingsContent.enabled = !checked
                }
            }
            
            Label {
                Layout.fillWidth: true
                text: useGlobalSettings ? 
                    qsTr("This PC will use the global streaming settings. Uncheck to create custom settings for this PC.") :
                    qsTr("This PC has custom streaming settings. Check the box above to use global settings instead.")
                wrapMode: Text.Wrap
                font.pointSize: 10
                color: useGlobalSettings ? "lightgray" : "lightblue"
            }
            
            // Main settings content
            ColumnLayout {
                id: settingsContent
                Layout.fillWidth: true
                spacing: 10
                enabled: !useGlobalSettings
                
                // Video Settings
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
                                ListElement { text: "8K (7680x4320)"; width: 7680; height: 4320 }
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
                                ListElement { text: "100 FPS"; fps: 100 }
                                ListElement { text: "120 FPS"; fps: 120 }
                                ListElement { text: "144 FPS"; fps: 144 }
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
                                ListElement { text: "Automatic"; val: 0 }
                                ListElement { text: "H.264"; val: 1 }
                                ListElement { text: "HEVC (H.265)"; val: 2 }
                                ListElement { text: "AV1"; val: 4 }
                            }
                        }
                        
                        Label {
                            text: qsTr("Video Decoder")
                            font.pointSize: 12
                        }
                        
                        AutoResizingComboBox {
                            id: decoderComboBox
                            Layout.fillWidth: true
                            textRole: "text"
                            model: ListModel {
                                id: decoderListModel
                                ListElement { text: "Automatic (Recommended)"; val: 0 }
                                ListElement { text: "Force software decoding"; val: 2 }
                                ListElement { text: "Force hardware decoding"; val: 1 }
                            }
                        }
                        
                        Label {
                            text: qsTr("Display Mode")
                            font.pointSize: 12
                            visible: SystemProperties.hasDesktopEnvironment
                        }
                        
                        AutoResizingComboBox {
                            id: windowModeComboBox
                            Layout.fillWidth: true
                            textRole: "text"
                            visible: SystemProperties.hasDesktopEnvironment
                            model: ListModel {
                                id: windowModeListModel
                                ListElement { text: "Fullscreen"; val: 0 }
                                ListElement { text: "Borderless windowed"; val: 1 }
                                ListElement { text: "Windowed"; val: 2 }
                            }
                        }
                        
                        CheckBox {
                            id: enableHdrCheckbox
                            text: qsTr("Enable HDR")
                            font.pointSize: 12
                            enabled: SystemProperties.supportsHdr
                        }
                        
                        CheckBox {
                            id: enableYUV444Checkbox
                            text: qsTr("Enable YUV 4:4:4 (Experimental)")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: unlockBitrateCheckbox
                            text: qsTr("Unlock bitrate limit (Experimental)")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: vsyncCheckbox
                            text: qsTr("V-Sync")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: framePacingCheckbox
                            text: qsTr("Frame pacing")
                            font.pointSize: 12
                            enabled: vsyncCheckbox.checked
                        }
                        
                        Label {
                            text: qsTr("Bitrate: %1 Mbps").arg((bitrateSlider.value / 1000).toFixed(1))
                            font.pointSize: 12
                        }
                        
                        Slider {
                            id: bitrateSlider
                            Layout.fillWidth: true
                            from: 500
                            to: unlockBitrateCheckbox.checked ? 500000 : 150000
                            stepSize: 500
                            value: 10000
                        }
                    }
                }
                
                // Audio Settings
                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Audio Settings")
                    font.pointSize: 12
                    
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 5
                        
                        Label {
                            text: qsTr("Audio configuration")
                            font.pointSize: 12
                        }
                        
                        AutoResizingComboBox {
                            id: audioComboBox
                            Layout.fillWidth: true
                            textRole: "text"
                            model: ListModel {
                                id: audioListModel
                                ListElement { text: "Stereo"; val: 0 }
                                ListElement { text: "5.1 surround sound"; val: 1 }
                                ListElement { text: "7.1 surround sound"; val: 2 }
                            }
                        }
                        
                        CheckBox {
                            id: muteHostCheckbox
                            text: qsTr("Mute host PC speakers while streaming")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: muteOnFocusLossCheckbox
                            text: qsTr("Mute audio stream when Moonlight is not the active window")
                            font.pointSize: 12
                            visible: SystemProperties.hasDesktopEnvironment
                        }
                    }
                }
                
                // Input Settings
                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Input Settings")
                    font.pointSize: 12
                    
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 5
                        
                        CheckBox {
                            id: absoluteMouseCheckbox
                            text: qsTr("Optimize mouse for remote desktop instead of games")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: absoluteTouchCheckbox
                            text: qsTr("Use touchscreen as a virtual trackpad")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: swapMouseButtonsCheckbox
                            text: qsTr("Swap left and right mouse buttons")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: reverseScrollCheckbox
                            text: qsTr("Reverse mouse scrolling direction")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: captureSysKeysCheckbox
                            text: qsTr("Capture system keyboard shortcuts")
                            font.pointSize: 12
                            enabled: SystemProperties.hasDesktopEnvironment
                        }
                        
                        AutoResizingComboBox {
                            id: captureSysKeysModeComboBox
                            Layout.fillWidth: true
                            textRole: "text"
                            enabled: captureSysKeysCheckbox.checked && SystemProperties.hasDesktopEnvironment
                            visible: captureSysKeysCheckbox.checked && SystemProperties.hasDesktopEnvironment
                            model: ListModel {
                                id: captureSysKeysModeListModel
                                ListElement { text: "in fullscreen"; val: 1 }
                                ListElement { text: "always"; val: 2 }
                            }
                        }
                    }
                }
                
                // Gamepad Settings
                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Gamepad Settings")
                    font.pointSize: 12
                    
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 5
                        
                        CheckBox {
                            id: swapFaceButtonsCheckbox
                            text: qsTr("Swap A/B and X/Y gamepad buttons")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: singleControllerCheckbox
                            text: qsTr("Force gamepad #1 always connected")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: gamepadMouseCheckbox
                            text: qsTr("Enable mouse control with gamepads by holding the 'Start' button")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: backgroundGamepadCheckbox
                            text: qsTr("Process gamepad input when Moonlight is in the background")
                            font.pointSize: 12
                            visible: SystemProperties.hasDesktopEnvironment
                        }
                    }
                }
                
                // Host Settings
                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Host Settings")
                    font.pointSize: 12
                    
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 5
                        
                        CheckBox {
                            id: optimizeGameSettingsCheckbox
                            text: qsTr("Optimize game settings for streaming")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: quitAppAfterCheckbox
                            text: qsTr("Quit app on host PC after ending stream")
                            font.pointSize: 12
                        }
                    }
                }
                
                // UI & Network Settings
                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("UI & Network Settings")
                    font.pointSize: 12
                    
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 5
                        
                        CheckBox {
                            id: connectionWarningsCheckbox
                            text: qsTr("Show connection quality warnings")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: configurationWarningsCheckbox
                            text: qsTr("Show configuration warnings")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: richPresenceCheckbox
                            text: qsTr("Discord Rich Presence integration")
                            font.pointSize: 12
                            visible: SystemProperties.hasDiscordIntegration
                        }
                        
                        CheckBox {
                            id: keepAwakeCheckbox
                            text: qsTr("Keep the display awake while streaming")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: enableMdnsCheckbox
                            text: qsTr("Automatically find PCs on the local network (Recommended)")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: detectNetworkBlockingCheckbox
                            text: qsTr("Automatically detect blocked connections (Recommended)")
                            font.pointSize: 12
                        }
                        
                        CheckBox {
                            id: showPerformanceOverlayCheckbox
                            text: qsTr("Show performance stats while streaming")
                            font.pointSize: 12
                        }
                    }
                }
            }
        }
    }
}
