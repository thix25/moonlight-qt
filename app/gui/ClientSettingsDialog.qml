import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.2

import StreamingPreferences 1.0
import SystemProperties 1.0
import GamepadMapping 1.0

NavigableDialog {
    id: clientSettingsDialog
    
    property string clientName: ""
    property string clientUuid: ""
    property bool hasCustomSettings: false
    property bool useGlobalSettings: false
    
    // Store original global settings to restore on cancel
    property var originalGlobalSettings: ({})
    
    standardButtons: Dialog.Ok | Dialog.Cancel | Dialog.RestoreDefaults
    
    title: qsTr("Settings for %1").arg(clientName)
    
    width: 800
    height: 550
    
    Component.onCompleted: {
        // Connect to the reset signal
        reset.connect(function() {
            // Reset to global defaults
            if (clientUuid !== "") {
                // resetClientSettings() already calls reload() internally when UUID matches
                StreamingPreferences.resetClientSettings(clientUuid)
                hasCustomSettings = false
                useGlobalSettings = true
                updateUIFromPreferences()
            }
        })

        // Populate combo box models using enum constants for future-proofing
        codecListModel.clear()
        codecListModel.append({text: qsTr("Automatic"), val: StreamingPreferences.VCC_AUTO})
        codecListModel.append({text: "H.264", val: StreamingPreferences.VCC_FORCE_H264})
        codecListModel.append({text: "HEVC (H.265)", val: StreamingPreferences.VCC_FORCE_HEVC})
        codecListModel.append({text: "AV1", val: StreamingPreferences.VCC_FORCE_AV1})

        decoderListModel.clear()
        decoderListModel.append({text: qsTr("Automatic (Recommended)"), val: StreamingPreferences.VDS_AUTO})
        decoderListModel.append({text: qsTr("Force software decoding"), val: StreamingPreferences.VDS_FORCE_SOFTWARE})
        decoderListModel.append({text: qsTr("Force hardware decoding"), val: StreamingPreferences.VDS_FORCE_HARDWARE})

        windowModeListModel.clear()
        windowModeListModel.append({text: qsTr("Fullscreen"), val: StreamingPreferences.WM_FULLSCREEN})
        windowModeListModel.append({text: qsTr("Borderless windowed"), val: StreamingPreferences.WM_FULLSCREEN_DESKTOP})
        windowModeListModel.append({text: qsTr("Windowed"), val: StreamingPreferences.WM_WINDOWED})

        audioListModel.clear()
        audioListModel.append({text: qsTr("Stereo"), val: StreamingPreferences.AC_STEREO})
        audioListModel.append({text: qsTr("5.1 surround sound"), val: StreamingPreferences.AC_51_SURROUND})
        audioListModel.append({text: qsTr("7.1 surround sound"), val: StreamingPreferences.AC_71_SURROUND})

        captureSysKeysModeListModel.clear()
        captureSysKeysModeListModel.append({text: qsTr("in fullscreen"), val: StreamingPreferences.CSK_FULLSCREEN})
        captureSysKeysModeListModel.append({text: qsTr("always"), val: StreamingPreferences.CSK_ALWAYS})
    }
    
    onOpened: {
        if (clientUuid !== "") {
            // Snapshot all global settings at once (replaces manual field-by-field backup)
            originalGlobalSettings = StreamingPreferences.snapshotSettings()
            
            hasCustomSettings = StreamingPreferences.hasClientSettings(clientUuid)
            useGlobalSettings = !hasCustomSettings
            
            if (hasCustomSettings) {
                StreamingPreferences.loadForClient(clientUuid)
            }
            updateUIFromPreferences()
        }
    }
    
    onAccepted: {
        if (!useGlobalSettings && clientUuid !== "") {
            updatePreferencesFromUI()
            StreamingPreferences.saveForClient(clientUuid)
        } else if (useGlobalSettings && hasCustomSettings && clientUuid !== "") {
            // User switched to global settings, remove custom settings
            StreamingPreferences.resetClientSettings(clientUuid)
        }
        // Restore the original global settings (clears client UUID state and emits signals)
        if (originalGlobalSettings) {
            StreamingPreferences.restoreSettings(originalGlobalSettings)
        }
    }
    
    onRejected: {
        // Restore the original global settings (clears client UUID state and emits signals)
        if (clientUuid !== "" && originalGlobalSettings) {
            StreamingPreferences.restoreSettings(originalGlobalSettings)
        }
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
    
    contentItem: Flickable {
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
                                // Populated dynamically in Component.onCompleted using enum constants
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
                                // Populated dynamically in Component.onCompleted using enum constants
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
                                // Populated dynamically in Component.onCompleted using enum constants
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
                            
                            onMoved: {
                                // Manual slider drag disables auto-adjust bitrate
                                StreamingPreferences.autoAdjustBitrate = false
                            }
                        }
                        
                        Button {
                            text: qsTr("Use Default Bitrate")
                            font.pointSize: 10
                            onClicked: {
                                var selectedWidth = resolutionListModel.get(resolutionComboBox.currentIndex).width
                                var selectedHeight = resolutionListModel.get(resolutionComboBox.currentIndex).height
                                var selectedFps = fpsListModel.get(fpsComboBox.currentIndex).fps
                                StreamingPreferences.autoAdjustBitrate = true
                                bitrateSlider.value = StreamingPreferences.getDefaultBitrate(
                                    selectedWidth, selectedHeight, selectedFps, enableYUV444Checkbox.checked)
                            }
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
                                // Populated dynamically in Component.onCompleted using enum constants
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
                                // Populated dynamically in Component.onCompleted using enum constants
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

                        // Per-client controller assignment
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Controller Player Assignment")
                            font.pointSize: 12
                            font.bold: true
                            Layout.topMargin: 10
                        }

                        CheckBox {
                            id: clientMappingOverrideCheckbox
                            text: qsTr("Use per-PC controller assignments (overrides global)")
                            font.pointSize: 11
                            checked: clientUuid !== "" ? GamepadMapping.isClientMappingEnabled(clientUuid) : false
                            onCheckedChanged: {
                                if (clientUuid !== "") {
                                    GamepadMapping.setClientMappingEnabled(clientUuid, checked)
                                }
                                clientControllerContent.enabled = checked
                            }
                        }

                        ColumnLayout {
                            id: clientControllerContent
                            Layout.fillWidth: true
                            spacing: 5
                            enabled: clientMappingOverrideCheckbox.checked

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Assign each controller to a player number for this PC. When disabled, the global assignments are used.")
                                font.pointSize: 10
                                wrapMode: Text.Wrap
                                color: clientControllerContent.enabled ? "lightgray" : "gray"
                            }

                            Button {
                                text: qsTr("Refresh Controllers")
                                font.pointSize: 10
                                onClicked: {
                                    clientControllerRepeater.model = GamepadMapping.getConnectedGamepads()
                                }
                            }

                            Repeater {
                                id: clientControllerRepeater
                                model: GamepadMapping.getConnectedGamepads()

                                delegate: ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    // Separator line between controllers
                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 1
                                        color: "#444444"
                                        visible: index > 0
                                    }

                                    RowLayout {
                                        spacing: 10
                                        Layout.fillWidth: true

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 1

                                            Label {
                                                text: modelData.name
                                                font.pointSize: 11
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                            }

                                            Label {
                                                text: "GUID: " + modelData.guid
                                                font.pointSize: 8
                                                font.family: "Courier New"
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                                color: "#999999"
                                            }
                                        }

                                        AutoResizingComboBox {
                                            id: clientPlayerAssignCombo
                                            textRole: "text"
                                            font.pointSize: 11
                                            implicitWidth: Math.max(desiredWidth, 150)
                                            model: ListModel {
                                                ListElement { text: qsTr("Automatic"); val: -1 }
                                                ListElement { text: qsTr("Player 1"); val: 0 }
                                                ListElement { text: qsTr("Player 2"); val: 1 }
                                                ListElement { text: qsTr("Player 3"); val: 2 }
                                                ListElement { text: qsTr("Player 4"); val: 3 }
                                            }

                                            Component.onCompleted: {
                                                recalculateWidth()
                                                var saved = GamepadMapping.getClientMapping(clientUuid, modelData.guid)
                                                currentIndex = 0
                                                for (var i = 0; i < model.count; i++) {
                                                    if (model.get(i).val === saved) {
                                                        currentIndex = i
                                                        break
                                                    }
                                                }
                                            }

                                            onActivated: {
                                                GamepadMapping.setClientMapping(clientUuid, modelData.guid, model.get(currentIndex).val)
                                            }
                                        }
                                    }
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("No controllers detected. Connect a controller and click 'Refresh Controllers'.")
                                font.pointSize: 10
                                font.italic: true
                                color: "gray"
                                visible: clientControllerRepeater.count === 0
                                wrapMode: Text.Wrap
                            }
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
