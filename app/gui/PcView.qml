import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import ComputerModel 1.0

import ComputerManager 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0

Item {
    property ComputerModel computerModel : createModel()

    // Dynamic tile sizing based on preference scale (50-200%)
    property real tileScale: pcTileSizeSlider.value / 100.0
    property int tileCellWidth: Math.round(310 * tileScale)
    property int tileCellHeight: Math.round(330 * tileScale)
    property int tileItemWidth: Math.round(300 * tileScale)
    property int tileItemHeight: Math.round(320 * tileScale)
    property int tileIconSize: Math.round(200 * tileScale)

    property bool showSections: StreamingPreferences.pcShowSections
    property var collapsedSections: ({})

    id: pcViewRoot
    objectName: qsTr("Computers")

    Component.onCompleted: {
        // Nothing special needed
    }

    StackView.onActivated: {
        ComputerManager.computerAddCompleted.connect(addComplete)

        // Give focus to the active view for gamepad/keyboard navigation
        if (showSections) {
            pcListView.forceActiveFocus()
            if (SdlGamepadKeyNavigation.getConnectedGamepads() > 0) {
                pcListView.currentIndex = 0
            }
        } else {
            pcGrid.forceActiveFocus()
            if (SdlGamepadKeyNavigation.getConnectedGamepads() > 0) {
                pcGrid.currentIndex = 0
            }
        }
    }

    StackView.onDeactivating: {
        ComputerManager.computerAddCompleted.disconnect(addComplete)
    }

    function pairingComplete(error)
    {
        pairDialog.close()

        if (error !== undefined) {
            errorDialog.text = error
            errorDialog.helpText = ""
            errorDialog.open()
        }
    }

    function addComplete(success, detectedPortBlocking)
    {
        if (!success) {
            errorDialog.text = qsTr("Unable to connect to the specified PC.")

            if (detectedPortBlocking) {
                errorDialog.text += "\n\n" + qsTr("This PC's Internet connection is blocking Moonlight. Streaming over the Internet may not work while connected to this network.")
            }
            else {
                errorDialog.helpText = qsTr("Click the Help button for possible solutions.")
            }

            errorDialog.open()
        }
    }

    function createModel()
    {
        var model = Qt.createQmlObject('import ComputerModel 1.0; ComputerModel {}', parent, '')
        model.initialize(ComputerManager)
        model.pairingCompleted.connect(pairingComplete)
        model.connectionTestCompleted.connect(testConnectionDialog.connectionTestComplete)
        return model
    }

    // PC action functions (shared between grid and list delegates)
    function pcClicked(modelIndex, modelOnline, modelPaired, modelServerSupported, modelName, modelUuid) {
        if (modelOnline) {
            if (!modelServerSupported) {
                errorDialog.text = qsTr("The version of GeForce Experience on %1 is not supported by this build of Moonlight. You must update Moonlight to stream from %1.").arg(modelName)
                errorDialog.helpText = ""
                errorDialog.open()
            }
            else if (modelPaired) {
                var component = Qt.createComponent("AppView.qml")
                var appView = component.createObject(stackView, {"computerUuid": modelUuid, "objectName": modelName})
                stackView.push(appView)
            }
            else {
                var pin = computerModel.generatePinString()
                computerModel.pairComputer(modelIndex, pin)
                pairDialog.pin = pin
                pairDialog.open()
            }
        } else if (!modelOnline) {
            // For offline PCs, open the context menu
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ==================== TOOLBAR ====================
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: "#404040"
            z: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 10

                Label {
                    text: qsTr("Computers")
                    font.pointSize: 14
                    font.bold: true
                    Layout.fillWidth: true
                    verticalAlignment: Text.AlignVCenter
                }

                // Sort mode selector
                ComboBox {
                    id: pcSortModeCombo
                    Layout.preferredWidth: 160
                    model: [qsTr("Alphabetical"), qsTr("Custom Order")]
                    currentIndex: computerModel.getSortMode()
                    onActivated: {
                        computerModel.setSortMode(index)
                        if (showSections) pcListView.forceActiveFocus()
                        else pcGrid.forceActiveFocus()
                    }
                    ToolTip.text: qsTr("Sort order")
                    ToolTip.visible: hovered
                    ToolTip.delay: 1000
                }

                // Show sections toggle
                ToolButton {
                    id: sectionsButton
                    text: showSections ? "▤" : "▥"
                    font.pointSize: 18
                    onClicked: {
                        StreamingPreferences.pcShowSections = !StreamingPreferences.pcShowSections
                        StreamingPreferences.save()
                        showSections = StreamingPreferences.pcShowSections
                        computerModel.refreshSort()
                        // Transfer focus to the newly active view
                        if (showSections) pcListView.forceActiveFocus()
                        else pcGrid.forceActiveFocus()
                    }
                    ToolTip.text: showSections ? qsTr("Hide Sections") : qsTr("Show Sections (Online / Not Paired / Offline)")
                    ToolTip.visible: hovered
                    ToolTip.delay: 1000
                }

                // Tile size slider
                Label {
                    text: "🔍"
                    font.pointSize: 14
                }
                Slider {
                    id: pcTileSizeSlider
                    Layout.preferredWidth: 120
                    from: 50
                    to: 200
                    stepSize: 10
                    value: StreamingPreferences.pcTileScale
                    onMoved: {
                        StreamingPreferences.pcTileScale = value
                        StreamingPreferences.save()
                    }
                    ToolTip {
                        parent: pcTileSizeSlider.handle
                        visible: pcTileSizeSlider.pressed
                        text: Math.round(pcTileSizeSlider.value) + "%"
                    }
                }
            }
        }

        // ==================== SECTION-BASED LIST VIEW ====================
        ListView {
            id: pcListView
            visible: showSections
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: showSections
            activeFocusOnTab: true
            topMargin: 10
            bottomMargin: 5
            clip: true

            Component.onCompleted: {
                currentIndex = -1
            }

            model: computerModel

            section.property: "section"
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                width: pcListView.width
                height: 40
                color: "transparent"

                property bool isCollapsed: !!collapsedSections[section]

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var cs = Object.assign({}, collapsedSections)
                        if (cs[section]) {
                            delete cs[section]
                        } else {
                            cs[section] = true
                        }
                        collapsedSections = cs
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 15

                    Label {
                        text: isCollapsed ? "▸" : "▾"
                        font.pointSize: 14
                        color: section === qsTr("Online") ? "#4CAF50" :
                               section === qsTr("Not Paired") ? "#FF9800" : "#9E9E9E"
                    }

                    Rectangle {
                        width: 12
                        height: 12
                        radius: 6
                        color: section === qsTr("Online") ? "#4CAF50" :
                               section === qsTr("Not Paired") ? "#FF9800" : "#9E9E9E"
                    }

                    Label {
                        text: section
                        font.pointSize: 16
                        font.bold: true
                        Layout.fillWidth: true
                        verticalAlignment: Text.AlignVCenter
                        color: section === qsTr("Online") ? "#4CAF50" :
                               section === qsTr("Not Paired") ? "#FF9800" : "#9E9E9E"
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: "#555555"
                    }
                }
            }

            delegate: ItemDelegate {
                width: pcListView.width
                height: collapsedSections[model.section] ? 0 : Math.round(80 * tileScale)
                visible: !collapsedSections[model.section]
                highlighted: pcListView.activeFocus && pcListView.currentIndex === index

                function activate() {
                    pcClicked(index, model.online, model.paired, model.serverSupported, model.name, model.uuid)
                }
                function openContextMenu() {
                    pcSectionContextMenu.pcIndex = index
                    pcSectionContextMenu.pcName = model.name
                    pcSectionContextMenu.pcOnline = model.online
                    pcSectionContextMenu.pcPaired = model.paired
                    pcSectionContextMenu.pcWakeable = !model.online && model.wakeable
                    pcSectionContextMenu.pcUuid = model.uuid
                    pcSectionContextMenu.pcDetails = model.details
                    if (pcSectionContextMenu.popup) {
                        pcSectionContextMenu.popup()
                    } else {
                        pcSectionContextMenu.open()
                    }
                }
                function deleteAction() {
                    deletePcDialog.pcIndex = index
                    deletePcDialog.pcName = model.name
                    deletePcDialog.open()
                }

                // Gamepad/keyboard navigation with collapsed section skip
                Keys.onDownPressed: {
                    var next = index + 1
                    while (next < pcListView.count && !!collapsedSections[computerModel.getSectionAt(next)]) {
                        next++
                    }
                    if (next < pcListView.count) pcListView.currentIndex = next
                }
                Keys.onUpPressed: {
                    var prev = index - 1
                    while (prev >= 0 && !!collapsedSections[computerModel.getSectionAt(prev)]) {
                        prev--
                    }
                    if (prev >= 0) pcListView.currentIndex = prev
                }
                Keys.onReturnPressed: activate()
                Keys.onEnterPressed: activate()
                Keys.onMenuPressed: openContextMenu()
                Keys.onDeletePressed: deleteAction()

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 25
                    anchors.rightMargin: 15
                    spacing: 15

                    Image {
                        source: "qrc:/res/desktop_windows-48px.svg"
                        sourceSize {
                            width: Math.round(48 * tileScale)
                            height: Math.round(48 * tileScale)
                        }
                        opacity: model.online ? 1.0 : 0.4
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: model.name
                            font.pointSize: Math.round(18 * tileScale)
                            elide: Label.ElideRight
                            Layout.fillWidth: true
                        }

                        Label {
                            text: model.online ?
                                      (model.paired ? qsTr("Online - Paired") : qsTr("Online - Not Paired")) :
                                      qsTr("Offline")
                            font.pointSize: Math.round(11 * tileScale)
                            opacity: 0.6
                            color: model.online ? (model.paired ? "#4CAF50" : "#FF9800") : "#9E9E9E"
                        }
                    }

                    // Status icons
                    Image {
                        visible: !model.online
                        source: "qrc:/res/warning_FILL1_wght300_GRAD200_opsz24.svg"
                        sourceSize {
                            width: Math.round(24 * tileScale)
                            height: Math.round(24 * tileScale)
                        }
                    }
                    Image {
                        visible: model.online && !model.paired
                        source: "qrc:/res/baseline-lock-24px.svg"
                        sourceSize {
                            width: Math.round(24 * tileScale)
                            height: Math.round(24 * tileScale)
                        }
                    }

                    BusyIndicator {
                        visible: model.statusUnknown
                        running: visible
                        Layout.preferredWidth: Math.round(32 * tileScale)
                        Layout.preferredHeight: Math.round(32 * tileScale)
                    }
                }

                onClicked: activate()

                onPressAndHold: openContextMenu()

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: parent.pressAndHold()
                }
            }

            // Initial focus key handler (when currentIndex is -1)
            Keys.onDownPressed: {
                if (currentIndex < 0 && count > 0) currentIndex = 0
            }

            ScrollBar.vertical: ScrollBar {}
        }

        // ==================== FLAT GRID VIEW (no sections) ====================
        CenteredGridView {
            id: pcGrid
            visible: !showSections
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: !showSections
            activeFocusOnTab: true
            clip: true
            topMargin: 20
            bottomMargin: 5
            cellWidth: tileCellWidth
            cellHeight: tileCellHeight

            Component.onCompleted: {
                currentIndex = -1
            }

            model: computerModel

            delegate: NavigableItemDelegate {
                width: tileItemWidth
                height: tileItemHeight
                grid: pcGrid

                property alias pcContextMenu : pcContextMenuLoader.item

                Image {
                    id: pcIcon
                    anchors.horizontalCenter: parent.horizontalCenter
                    source: "qrc:/res/desktop_windows-48px.svg"
                    sourceSize {
                        width: tileIconSize
                        height: tileIconSize
                    }
                }

                Image {
                    id: stateIcon
                    anchors.horizontalCenter: pcIcon.horizontalCenter
                    anchors.verticalCenter: pcIcon.verticalCenter
                    anchors.verticalCenterOffset: !model.online ? Math.round(-18 * tileScale) : Math.round(-16 * tileScale)
                    visible: !model.statusUnknown && (!model.online || !model.paired)
                    source: !model.online ? "qrc:/res/warning_FILL1_wght300_GRAD200_opsz24.svg" : "qrc:/res/baseline-lock-24px.svg"
                    sourceSize {
                        width: !model.online ? Math.round(75 * tileScale) : Math.round(70 * tileScale)
                        height: !model.online ? Math.round(75 * tileScale) : Math.round(70 * tileScale)
                    }
                }

                BusyIndicator {
                    id: statusUnknownSpinner
                    anchors.horizontalCenter: pcIcon.horizontalCenter
                    anchors.verticalCenter: pcIcon.verticalCenter
                    anchors.verticalCenterOffset: Math.round(-15 * tileScale)
                    width: Math.round(75 * tileScale)
                    height: Math.round(75 * tileScale)
                    visible: model.statusUnknown
                    running: visible
                }

                Label {
                    id: pcNameText
                    text: model.name

                    width: parent.width
                    anchors.top: pcIcon.bottom
                    anchors.bottom: parent.bottom
                    font.pointSize: Math.round(36 * tileScale)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                }

                Loader {
                    id: pcContextMenuLoader
                    asynchronous: true
                    sourceComponent: NavigableMenu {
                        id: pcContextMenu
                        MenuItem {
                            text: qsTr("PC Status: %1").arg(model.online ? qsTr("Online") : qsTr("Offline"))
                            font.bold: true
                            enabled: false
                        }
                        NavigableMenuItem {
                            text: qsTr("View All Apps")
                            onTriggered: {
                                var component = Qt.createComponent("AppView.qml")
                                var appView = component.createObject(stackView, {"computerUuid": model.uuid, "objectName": model.name, "showHiddenGames": true})
                                stackView.push(appView)
                            }
                            visible: model.online && model.paired
                        }
                        NavigableMenuItem {
                            text: qsTr("Wake PC")
                            onTriggered: computerModel.wakeComputer(index)
                            visible: !model.online && model.wakeable
                        }
                        NavigableMenuItem {
                            text: qsTr("Test Network")
                            onTriggered: {
                                computerModel.testConnectionForComputer(index)
                                testConnectionDialog.open()
                            }
                        }

                        NavigableMenuItem {
                            text: qsTr("PC Settings")
                            onTriggered: {
                                clientSettingsDialog.clientName = model.name
                                clientSettingsDialog.clientUuid = model.uuid
                                clientSettingsDialog.open()
                            }
                        }

                        NavigableMenuItem {
                            text: qsTr("Rename PC")
                            onTriggered: {
                                renamePcDialog.pcIndex = index
                                renamePcDialog.originalName = model.name
                                renamePcDialog.open()
                            }
                        }
                        NavigableMenuItem {
                            text: qsTr("Delete PC")
                            onTriggered: {
                                deletePcDialog.pcIndex = index
                                deletePcDialog.pcName = model.name
                                deletePcDialog.open()
                            }
                        }
                        NavigableMenuItem {
                            text: qsTr("View Details")
                            onTriggered: {
                                showPcDetailsDialog.pcDetails = model.details
                                showPcDetailsDialog.open()
                            }
                        }

                        MenuSeparator { visible: computerModel.getSortMode() === 1 }

                        NavigableMenuItem {
                            text: qsTr("Move Up")
                            visible: computerModel.getSortMode() === 1
                            enabled: index > 0
                            onTriggered: computerModel.moveComputer(index, index - 1)
                        }
                        NavigableMenuItem {
                            text: qsTr("Move Down")
                            visible: computerModel.getSortMode() === 1
                            enabled: index < computerModel.count() - 1
                            onTriggered: computerModel.moveComputer(index, index + 1)
                        }
                        NavigableMenuItem {
                            text: qsTr("Move to Position...")
                            visible: computerModel.getSortMode() === 1
                            onTriggered: {
                                movePcToPositionDialog.currentIndex = index
                                movePcToPositionDialog.maxCount = computerModel.count()
                                movePcToPositionDialog.open()
                            }
                        }
                    }
                }

                onClicked: {
                    pcClicked(index, model.online, model.paired, model.serverSupported, model.name, model.uuid)
                }

                onPressAndHold: {
                    if (pcContextMenu.popup) {
                        pcContextMenu.popup()
                    }
                    else {
                        pcContextMenu.open()
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton;
                    onClicked: {
                        parent.pressAndHold()
                    }
                }

                Keys.onMenuPressed: {
                    pcContextMenu.open()
                }

                Keys.onDeletePressed: {
                    deletePcDialog.pcIndex = index
                    deletePcDialog.pcName = model.name
                    deletePcDialog.open()
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }
    }

    // Searching indicator
    Row {
        anchors.centerIn: parent
        spacing: 5
        visible: (showSections ? pcListView.count : pcGrid.count) === 0

        BusyIndicator {
            id: searchSpinner
            visible: StreamingPreferences.enableMdns
            running: visible
        }

        Label {
            height: searchSpinner.height
            elide: Label.ElideRight
            text: StreamingPreferences.enableMdns ? qsTr("Searching for compatible hosts on your local network...")
                                                  : qsTr("Automatic PC discovery is disabled. Add your PC manually.")
            font.pointSize: 20
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    // Shared context menu for section-based list view
    NavigableMenu {
        id: pcSectionContextMenu
        property int pcIndex: -1
        property string pcName: ""
        property bool pcOnline: false
        property bool pcPaired: false
        property bool pcWakeable: false
        property string pcUuid: ""
        property string pcDetails: ""

        MenuItem {
            text: qsTr("PC Status: %1").arg(pcSectionContextMenu.pcOnline ? qsTr("Online") : qsTr("Offline"))
            font.bold: true
            enabled: false
        }
        NavigableMenuItem {
            text: qsTr("View All Apps")
            onTriggered: {
                var component = Qt.createComponent("AppView.qml")
                var appView = component.createObject(stackView, {"computerUuid": pcSectionContextMenu.pcUuid, "objectName": pcSectionContextMenu.pcName, "showHiddenGames": true})
                stackView.push(appView)
            }
            visible: pcSectionContextMenu.pcOnline && pcSectionContextMenu.pcPaired
        }
        NavigableMenuItem {
            text: qsTr("Wake PC")
            onTriggered: computerModel.wakeComputer(pcSectionContextMenu.pcIndex)
            visible: pcSectionContextMenu.pcWakeable
        }
        NavigableMenuItem {
            text: qsTr("Test Network")
            onTriggered: {
                computerModel.testConnectionForComputer(pcSectionContextMenu.pcIndex)
                testConnectionDialog.open()
            }
        }
        NavigableMenuItem {
            text: qsTr("PC Settings")
            onTriggered: {
                clientSettingsDialog.clientName = pcSectionContextMenu.pcName
                clientSettingsDialog.clientUuid = pcSectionContextMenu.pcUuid
                clientSettingsDialog.open()
            }
        }
        NavigableMenuItem {
            text: qsTr("Rename PC")
            onTriggered: {
                renamePcDialog.pcIndex = pcSectionContextMenu.pcIndex
                renamePcDialog.originalName = pcSectionContextMenu.pcName
                renamePcDialog.open()
            }
        }
        NavigableMenuItem {
            text: qsTr("Delete PC")
            onTriggered: {
                deletePcDialog.pcIndex = pcSectionContextMenu.pcIndex
                deletePcDialog.pcName = pcSectionContextMenu.pcName
                deletePcDialog.open()
            }
        }
        NavigableMenuItem {
            text: qsTr("View Details")
            onTriggered: {
                showPcDetailsDialog.pcDetails = pcSectionContextMenu.pcDetails
                showPcDetailsDialog.open()
            }
        }

        MenuSeparator { visible: computerModel.getSortMode() === 1 }

        NavigableMenuItem {
            text: qsTr("Move Up")
            visible: computerModel.getSortMode() === 1
            enabled: pcSectionContextMenu.pcIndex > 0
            onTriggered: computerModel.moveComputer(pcSectionContextMenu.pcIndex, pcSectionContextMenu.pcIndex - 1)
        }
        NavigableMenuItem {
            text: qsTr("Move Down")
            visible: computerModel.getSortMode() === 1
            enabled: pcSectionContextMenu.pcIndex < computerModel.count() - 1
            onTriggered: computerModel.moveComputer(pcSectionContextMenu.pcIndex, pcSectionContextMenu.pcIndex + 1)
        }
        NavigableMenuItem {
            text: qsTr("Move to Position...")
            visible: computerModel.getSortMode() === 1
            onTriggered: {
                movePcToPositionDialog.currentIndex = pcSectionContextMenu.pcIndex
                movePcToPositionDialog.maxCount = computerModel.count()
                movePcToPositionDialog.open()
            }
        }
    }

    ErrorMessageDialog {
        id: errorDialog
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide"
    }

    ClientSettingsDialog {
        id: clientSettingsDialog
    }

    NavigableMessageDialog {
        id: pairDialog
        modal: true
        closePolicy: Popup.CloseOnEscape

        property string pin : "0000"
        text:qsTr("Please enter %1 on your host PC. This dialog will close when pairing is completed.").arg(pin)+"\n\n"+
             qsTr("If your host PC is running Sunshine, navigate to the Sunshine web UI to enter the PIN.")
        standardButtons: Dialog.Cancel
        onRejected: {
        }
    }

    NavigableMessageDialog {
        id: deletePcDialog
        property int pcIndex : -1
        property string pcName : ""
        text: qsTr("Are you sure you want to remove '%1'?").arg(pcName)
        standardButtons: Dialog.Yes | Dialog.No

        onAccepted: {
            computerModel.deleteComputer(pcIndex)
        }
    }

    NavigableMessageDialog {
        id: testConnectionDialog
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Ok

        onAboutToShow: {
            testConnectionDialog.text = qsTr("Moonlight is testing your network connection to determine if any required ports are blocked.") + "\n\n" + qsTr("This may take a few seconds…")
            showSpinner = true
        }

        function connectionTestComplete(result, blockedPorts)
        {
            if (result === -1) {
                text = qsTr("The network test could not be performed because none of Moonlight's connection testing servers were reachable from this PC. Check your Internet connection or try again later.")
                imageSrc = "qrc:/res/baseline-warning-24px.svg"
            }
            else if (result === 0) {
                text = qsTr("This network does not appear to be blocking Moonlight. If you still have trouble connecting, check your PC's firewall settings.") + "\n\n" + qsTr("If you are trying to stream over the Internet, install the Moonlight Internet Hosting Tool on your gaming PC and run the included Internet Streaming Tester to check your gaming PC's Internet connection.")
                imageSrc = "qrc:/res/baseline-check_circle_outline-24px.svg"
            }
            else {
                text = qsTr("Your PC's current network connection seems to be blocking Moonlight. Streaming over the Internet may not work while connected to this network.") + "\n\n" + qsTr("The following network ports were blocked:") + "\n"
                text += blockedPorts
                imageSrc = "qrc:/res/baseline-error_outline-24px.svg"
            }

            showSpinner = false
        }
    }

    NavigableDialog {
        id: renamePcDialog
        property string label: qsTr("Enter the new name for this PC:")
        property string originalName
        property int pcIndex : -1;

        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            editText.forceActiveFocus()
        }

        onClosed: {
            editText.clear()
        }

        onAccepted: {
            if (editText.text) {
                computerModel.renameComputer(pcIndex, editText.text)
            }
        }

        ColumnLayout {
            Label {
                text: renamePcDialog.label
                font.bold: true
            }

            TextField {
                id: editText
                placeholderText: renamePcDialog.originalName
                Layout.fillWidth: true
                focus: true

                Keys.onReturnPressed: {
                    renamePcDialog.accept()
                }

                Keys.onEnterPressed: {
                    renamePcDialog.accept()
                }
            }
        }
    }

    NavigableMessageDialog {
        id: showPcDetailsDialog
        property string pcDetails : "";
        text: showPcDetailsDialog.pcDetails
        imageSrc: "qrc:/res/baseline-help_outline-24px.svg"
        standardButtons: Dialog.Ok
    }

    NavigableDialog {
        id: movePcToPositionDialog
        property int currentIndex: 0
        property int maxCount: 1

        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            pcPositionSpinBox.forceActiveFocus()
            pcPositionSpinBox.value = currentIndex + 1
        }

        onAccepted: {
            var targetIndex = pcPositionSpinBox.value - 1
            if (targetIndex !== currentIndex && targetIndex >= 0 && targetIndex < maxCount) {
                computerModel.moveComputer(currentIndex, targetIndex)
            }
        }

        ColumnLayout {
            Label {
                text: qsTr("Move to position (1-%1):").arg(movePcToPositionDialog.maxCount)
                font.bold: true
            }

            SpinBox {
                id: pcPositionSpinBox
                from: 1
                to: movePcToPositionDialog.maxCount
                Layout.fillWidth: true

                Keys.onReturnPressed: movePcToPositionDialog.accept()
                Keys.onEnterPressed: movePcToPositionDialog.accept()
            }
        }
    }
}
