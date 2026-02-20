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
    property bool showPcInfo: StreamingPreferences.showPcInfo

    id: pcViewRoot
    objectName: qsTr("Computers")

    Component.onCompleted: {
        // Nothing special needed
    }

    StackView.onActivated: {
        ComputerManager.computerAddCompleted.connect(addComplete)

        // Refresh model data (e.g. after changing global settings)
        computerModel.refreshSort()

        // Give focus to the active view for gamepad/keyboard navigation
        if (showSections) {
            pcListView.forceActiveFocus()
            if (SdlGamepadKeyNavigation.getConnectedGamepads() > 0) {
                pcListView.currentIndex = 0
            }
        } else {
            pcGridFlickable.forceActiveFocus()
            if (SdlGamepadKeyNavigation.getConnectedGamepads() > 0) {
                pcGridFlickable.currentGridIndex = 0
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
                        else pcGridFlickable.forceActiveFocus()
                    }

                    // Refresh when model resets or rows move (e.g. after moveComputer auto-switches to custom)
                    Connections {
                        target: computerModel
                        function onModelReset() {
                            pcSortModeCombo.currentIndex = computerModel.getSortMode()
                        }
                        function onRowsMoved() {
                            pcSortModeCombo.currentIndex = computerModel.getSortMode()
                        }
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
                        else pcGridFlickable.forceActiveFocus()
                    }
                    ToolTip.text: showSections ? qsTr("Hide Sections") : qsTr("Show Sections (Online / Not Paired / Offline)")
                    ToolTip.visible: hovered
                    ToolTip.delay: 1000
                }

                // Toggle PC info overlay
                ToolButton {
                    id: infoToggleButton
                    text: "ℹ"
                    font.pointSize: 18
                    opacity: showPcInfo ? 1.0 : 0.5
                    onClicked: {
                        StreamingPreferences.showPcInfo = !StreamingPreferences.showPcInfo
                        StreamingPreferences.save()
                        showPcInfo = StreamingPreferences.showPcInfo
                        // Force model refresh to update settings summaries
                        computerModel.refreshSort()
                    }
                    ToolTip.text: showPcInfo ? qsTr("Hide PC Settings Info") : qsTr("Show PC Settings Info (resolution, FPS, codec...)")
                    ToolTip.visible: hovered
                    ToolTip.delay: 1000

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width * 0.6
                        height: 2
                        color: "#4CAF50"
                        visible: showPcInfo
                    }
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

                    Canvas {
                        width: 16
                        height: 16
                        property bool collapsed: isCollapsed
                        property color arrowColor: section === qsTr("Online") ? "#4CAF50" :
                               section === qsTr("Not Paired") ? "#FF9800" : "#9E9E9E"
                        onCollapsedChanged: requestPaint()
                        onArrowColorChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            ctx.fillStyle = arrowColor
                            ctx.beginPath()
                            if (collapsed) {
                                ctx.moveTo(4, 2)
                                ctx.lineTo(13, 8)
                                ctx.lineTo(4, 14)
                            } else {
                                ctx.moveTo(2, 4)
                                ctx.lineTo(14, 4)
                                ctx.lineTo(8, 13)
                            }
                            ctx.closePath()
                            ctx.fill()
                        }
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
                    pcSectionContextMenu.pcHasClientSettings = model.hasClientSettings
                    pcSectionContextMenu.pcHasSavedClientSettings = model.hasSavedClientSettings
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

                    // Left: PC name + status
                    ColumnLayout {
                        Layout.preferredWidth: Math.round(200 * tileScale)
                        Layout.maximumWidth: Math.round(280 * tileScale)
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

                    // Middle: settings info (fills available space)
                    Label {
                        visible: showPcInfo
                        text: model.settingsSummary || ""
                        font.pointSize: Math.max(7, Math.round(9 * tileScale))
                        color: "#9E9E9E"
                        opacity: 0.8
                        elide: Label.ElideRight
                        Layout.fillWidth: true
                    }

                    // Spacer when info not visible
                    Item {
                        visible: !showPcInfo
                        Layout.fillWidth: true
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

                    // Quick toggle: Custom/Global checkbox
                    Rectangle {
                        visible: showPcInfo
                        width: listToggleRow.implicitWidth + 12
                        height: listToggleRow.implicitHeight + 6
                        radius: 4
                        color: model.hasClientSettings ? "#1A2196F3" : "#1A666666"
                        border.color: model.hasClientSettings ? "#2196F3" : "#555555"
                        border.width: 1

                        RowLayout {
                            id: listToggleRow
                            anchors.centerIn: parent
                            spacing: 4

                            Rectangle {
                                width: Math.round(14 * tileScale)
                                height: Math.round(14 * tileScale)
                                radius: 3
                                color: model.hasClientSettings ? "#2196F3" : "transparent"
                                border.color: model.hasClientSettings ? "#2196F3" : "#888888"
                                border.width: 1

                                Label {
                                    anchors.centerIn: parent
                                    text: "✓"
                                    font.pointSize: Math.max(6, Math.round(8 * tileScale))
                                    color: "white"
                                    visible: model.hasClientSettings
                                }
                            }

                            Label {
                                text: model.hasClientSettings ? qsTr("Custom") : qsTr("Global")
                                font.pointSize: Math.max(7, Math.round(8 * tileScale))
                                color: model.hasClientSettings ? "#64B5F6" : "#999999"
                            }
                        }

                        MouseArea {
                            id: listToggleMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (model.hasClientSettings) {
                                    StreamingPreferences.deactivateClientSettings(model.uuid)
                                } else {
                                    StreamingPreferences.activateClientSettings(model.uuid)
                                }
                                computerModel.refreshData()
                            }
                        }

                        ToolTip.text: model.hasClientSettings ?
                            qsTr("Click to use global settings") :
                            qsTr("Click to use custom settings for this PC")
                        ToolTip.visible: listToggleMouseArea.containsMouse
                        ToolTip.delay: 800
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
            Keys.onUpPressed: {
                if (currentIndex < 0 && count > 0) currentIndex = 0
            }
            Keys.onReturnPressed: {
                if (currentIndex < 0 && count > 0) currentIndex = 0
            }

            ScrollBar.vertical: ScrollBar {}
        }

        // ==================== GRID VIEW (with inline section headers) ====================
        Flickable {
            id: pcGridFlickable
            visible: !showSections
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: pcGridColumn.height
            clip: true
            activeFocusOnTab: true
            focus: !showSections
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.OvershootBounds

            // Counter to force reactive updates when model data changes
            property int dataVersion: 0

            Connections {
                target: computerModel
                function onDataChanged() { pcGridFlickable.dataVersion++ }
                function onModelReset() { pcGridFlickable.dataVersion++ }
                function onRowsMoved() { pcGridFlickable.dataVersion++ }
            }

            // Track a virtual "current index" for gamepad navigation
            property int currentGridIndex: -1
            property int gridItemCount: dataVersion >= 0 ? computerModel.count() : 0

            function navigateGrid(delta) {
                var newIdx = currentGridIndex + delta
                if (newIdx >= 0 && newIdx < gridItemCount) {
                    currentGridIndex = newIdx
                    // Ensure the focused item is visible
                    var item = pcGridRepeater.itemAt(newIdx)
                    if (item) {
                        var yPos = item.mapToItem(pcGridColumn, 0, 0).y
                        if (yPos < contentY) contentY = Math.max(0, yPos - 20)
                        else if (yPos + item.height > contentY + height) contentY = yPos + item.height - height + 20
                    }
                }
            }

            Keys.onDownPressed: {
                if (currentGridIndex < 0 && gridItemCount > 0) { currentGridIndex = 0; return }
                // Move down by items per row
                var itemsPerRow = Math.max(1, Math.floor((width - 20) / tileCellWidth))
                navigateGrid(itemsPerRow)
            }
            Keys.onUpPressed: {
                if (currentGridIndex < 0 && gridItemCount > 0) { currentGridIndex = 0; return }
                var itemsPerRow = Math.max(1, Math.floor((width - 20) / tileCellWidth))
                navigateGrid(-itemsPerRow)
            }
            Keys.onRightPressed: {
                if (currentGridIndex < 0 && gridItemCount > 0) { currentGridIndex = 0; return }
                navigateGrid(1)
            }
            Keys.onLeftPressed: {
                if (currentGridIndex < 0 && gridItemCount > 0) { currentGridIndex = 0; return }
                navigateGrid(-1)
            }
            Keys.onReturnPressed: {
                if (currentGridIndex < 0 && gridItemCount > 0) { currentGridIndex = 0; return }
                var item = pcGridRepeater.itemAt(currentGridIndex)
                if (item) item.clicked()
            }
            Keys.onEnterPressed: {
                if (currentGridIndex < 0 && gridItemCount > 0) { currentGridIndex = 0; return }
                var item = pcGridRepeater.itemAt(currentGridIndex)
                if (item) item.clicked()
            }
            Keys.onMenuPressed: {
                if (currentGridIndex >= 0) {
                    var item = pcGridRepeater.itemAt(currentGridIndex)
                    if (item) item.pressAndHold()
                }
            }

            Column {
                id: pcGridColumn
                width: parent.width
                topPadding: 10

                Repeater {
                    id: pcSectionRepeater
                    model: {
                        // Depend on dataVersion to rebuild when data changes
                        var v = pcGridFlickable.dataVersion
                        var sections = []
                        var seen = {}
                        for (var i = 0; i < computerModel.count(); i++) {
                            var sec = computerModel.getSectionAt(i)
                            if (!seen[sec]) {
                                seen[sec] = true
                                sections.push(sec)
                            }
                        }
                        return sections
                    }

                    Column {
                        width: pcGridColumn.width
                        property string sectionName: modelData

                        // Section header (clickable to collapse/expand)
                        Rectangle {
                            width: parent.width
                            height: 40
                            color: "transparent"

                            property bool isCollapsed: !!collapsedSections[sectionName]

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    var cs = Object.assign({}, collapsedSections)
                                    if (cs[sectionName]) {
                                        delete cs[sectionName]
                                    } else {
                                        cs[sectionName] = true
                                    }
                                    collapsedSections = cs
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 15

                                Canvas {
                                    width: 16
                                    height: 16
                                    property bool collapsed: isCollapsed
                                    property color arrowColor: sectionName === qsTr("Online") ? "#4CAF50" :
                                           sectionName === qsTr("Not Paired") ? "#FF9800" : "#9E9E9E"
                                    onCollapsedChanged: requestPaint()
                                    onArrowColorChanged: requestPaint()
                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.reset()
                                        ctx.fillStyle = arrowColor
                                        ctx.beginPath()
                                        if (collapsed) {
                                            // Right-pointing triangle
                                            ctx.moveTo(4, 2)
                                            ctx.lineTo(13, 8)
                                            ctx.lineTo(4, 14)
                                        } else {
                                            // Down-pointing triangle
                                            ctx.moveTo(2, 4)
                                            ctx.lineTo(14, 4)
                                            ctx.lineTo(8, 13)
                                        }
                                        ctx.closePath()
                                        ctx.fill()
                                    }
                                }
                                Rectangle {
                                    width: 12; height: 12; radius: 6
                                    color: sectionName === qsTr("Online") ? "#4CAF50" :
                                           sectionName === qsTr("Not Paired") ? "#FF9800" : "#9E9E9E"
                                }
                                Label {
                                    text: sectionName
                                    font.pointSize: 16
                                    font.bold: true
                                    Layout.fillWidth: true
                                    verticalAlignment: Text.AlignVCenter
                                    color: sectionName === qsTr("Online") ? "#4CAF50" :
                                           sectionName === qsTr("Not Paired") ? "#FF9800" : "#9E9E9E"
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 1
                                    color: "#555555"
                                }
                            }
                        }

                        // Grid of PCs for this section
                        Flow {
                            visible: !collapsedSections[sectionName]
                            width: parent.width
                            leftPadding: 10
                            rightPadding: 10
                            spacing: 10

                            Repeater {
                                id: pcGridRepeater
                                model: {
                                    // Depend on dataVersion for reactivity
                                    var v = pcGridFlickable.dataVersion
                                    var indices = []
                                    for (var i = 0; i < computerModel.count(); i++) {
                                        if (computerModel.getSectionAt(i) === sectionName) {
                                            indices.push(i)
                                        }
                                    }
                                    return indices
                                }

                                NavigableItemDelegate {
                                    property int pcModelIndex: modelData

                                    // Reactive data properties (re-evaluated when dataVersion changes)
                                    property string pcName: pcGridFlickable.dataVersion >= 0 ? (computerModel.data(computerModel.index(pcModelIndex, 0), 256) || "") : ""
                                    property bool pcOnline: pcGridFlickable.dataVersion >= 0 && !!computerModel.data(computerModel.index(pcModelIndex, 0), 257)
                                    property bool pcPaired: pcGridFlickable.dataVersion >= 0 && !!computerModel.data(computerModel.index(pcModelIndex, 0), 258)
                                    property bool pcWakeable: pcGridFlickable.dataVersion >= 0 && !!computerModel.data(computerModel.index(pcModelIndex, 0), 260)
                                    property bool pcStatusUnknown: pcGridFlickable.dataVersion >= 0 && !!computerModel.data(computerModel.index(pcModelIndex, 0), 261)
                                    property bool pcServerSupported: pcGridFlickable.dataVersion >= 0 && !!computerModel.data(computerModel.index(pcModelIndex, 0), 262)
                                    property string pcDetails: pcGridFlickable.dataVersion >= 0 ? (computerModel.data(computerModel.index(pcModelIndex, 0), 263) || "") : ""
                                    property string pcUuid: pcGridFlickable.dataVersion >= 0 ? (computerModel.data(computerModel.index(pcModelIndex, 0), 264) || "") : ""
                                    property bool pcHasClientSettings: pcGridFlickable.dataVersion >= 0 && !!computerModel.data(computerModel.index(pcModelIndex, 0), 266)
                                    property string pcSettingsSummary: pcGridFlickable.dataVersion >= 0 ? (computerModel.data(computerModel.index(pcModelIndex, 0), 267) || "") : ""
                                    property bool pcHasSavedClientSettings: pcGridFlickable.dataVersion >= 0 && !!computerModel.data(computerModel.index(pcModelIndex, 0), 268)

                                    width: tileItemWidth
                                    height: tileItemHeight
                                    grid: pcGridFlickable
                                    highlighted: pcGridFlickable.activeFocus && pcGridFlickable.currentGridIndex === pcModelIndex

                                    property alias pcContextMenu : pcCtxMenuLoader.item

                                    Image {
                                        id: gridPcIcon
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        source: "qrc:/res/desktop_windows-48px.svg"
                                        sourceSize { width: tileIconSize; height: tileIconSize }
                                    }

                                    Image {
                                        anchors.horizontalCenter: gridPcIcon.horizontalCenter
                                        anchors.verticalCenter: gridPcIcon.verticalCenter
                                        anchors.verticalCenterOffset: Math.round(-17 * tileScale)
                                        visible: !pcStatusUnknown && (!pcOnline || !pcPaired)
                                        source: !pcOnline ? "qrc:/res/warning_FILL1_wght300_GRAD200_opsz24.svg" : "qrc:/res/baseline-lock-24px.svg"
                                        sourceSize { width: Math.round(72 * tileScale); height: Math.round(72 * tileScale) }
                                    }

                                    Label {
                                        text: pcName
                                        width: parent.width
                                        anchors.top: gridPcIcon.bottom
                                        anchors.bottom: pcInfoColumn.visible ? pcInfoColumn.top : parent.bottom
                                        font.pointSize: Math.round(36 * tileScale)
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.Wrap
                                        elide: Text.ElideRight
                                    }

                                    // Settings info overlay at the bottom of the tile
                                    Column {
                                        id: pcInfoColumn
                                        visible: showPcInfo
                                        anchors.bottom: parent.bottom
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.margins: 4
                                        spacing: 2

                                        // Clickable Global/Custom toggle
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: gridToggleRow.implicitWidth + 10
                                            height: gridToggleRow.implicitHeight + 4
                                            radius: 3
                                            color: pcHasClientSettings ? "#332196F3" : "#33555555"
                                            border.color: pcHasClientSettings ? "#2196F3" : "#666666"
                                            border.width: 1

                                            Row {
                                                id: gridToggleRow
                                                anchors.centerIn: parent
                                                spacing: 3

                                                Rectangle {
                                                    width: Math.round(12 * tileScale)
                                                    height: Math.round(12 * tileScale)
                                                    radius: 2
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    color: pcHasClientSettings ? "#2196F3" : "transparent"
                                                    border.color: pcHasClientSettings ? "#2196F3" : "#888888"
                                                    border.width: 1

                                                    Label {
                                                        anchors.centerIn: parent
                                                        text: "✓"
                                                        font.pointSize: Math.max(5, Math.round(6 * tileScale))
                                                        color: "white"
                                                        visible: pcHasClientSettings
                                                    }
                                                }

                                                Label {
                                                    text: pcHasClientSettings ? qsTr("Custom") : qsTr("Global")
                                                    font.pointSize: Math.max(6, Math.round(7 * tileScale))
                                                    color: pcHasClientSettings ? "#64B5F6" : "#AAAAAA"
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                hoverEnabled: true
                                                onClicked: {
                                                    if (pcHasClientSettings) {
                                                        StreamingPreferences.deactivateClientSettings(pcUuid)
                                                    } else {
                                                        StreamingPreferences.activateClientSettings(pcUuid)
                                                    }
                                                    computerModel.refreshData()
                                                }
                                            }
                                        }

                                        // Compact settings summary
                                        Label {
                                            width: parent.width
                                            text: pcSettingsSummary || ""
                                            font.pointSize: Math.max(6, Math.round(7 * tileScale))
                                            color: "#AAAAAA"
                                            horizontalAlignment: Text.AlignHCenter
                                            wrapMode: Text.Wrap
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }
                                    }

                                    onClicked: pcClicked(pcModelIndex, pcOnline, pcPaired, pcServerSupported, pcName, pcUuid)

                                    BusyIndicator {
                                        anchors.horizontalCenter: gridPcIcon.horizontalCenter
                                        anchors.verticalCenter: gridPcIcon.verticalCenter
                                        anchors.verticalCenterOffset: Math.round(-15 * tileScale)
                                        width: Math.round(75 * tileScale)
                                        height: Math.round(75 * tileScale)
                                        visible: pcStatusUnknown
                                        running: visible
                                    }

                                    Loader {
                                        id: pcCtxMenuLoader
                                        asynchronous: true
                                        sourceComponent: NavigableMenu {
                                            id: pcCtxMenu

                                            MenuItem {
                                                text: qsTr("PC Status: %1").arg(pcOnline ? qsTr("Online") : qsTr("Offline"))
                                                font.bold: true
                                                enabled: false
                                            }
                                            NavigableMenuItem {
                                                text: qsTr("View All Apps")
                                                onTriggered: {
                                                    var component = Qt.createComponent("AppView.qml")
                                                    var appView = component.createObject(stackView, {
                                                        "computerUuid": pcUuid,
                                                        "objectName": pcName,
                                                        "showHiddenGames": true
                                                    })
                                                    stackView.push(appView)
                                                }
                                                visible: pcOnline && pcPaired
                                            }
                                            NavigableMenuItem {
                                                text: qsTr("Wake PC")
                                                onTriggered: computerModel.wakeComputer(pcModelIndex)
                                                visible: !pcOnline && pcWakeable
                                            }
                                            NavigableMenuItem {
                                                text: qsTr("Test Network")
                                                onTriggered: {
                                                    computerModel.testConnectionForComputer(pcModelIndex)
                                                    testConnectionDialog.open()
                                                }
                                            }
                                            NavigableMenuItem {
                                                text: qsTr("PC Settings")
                                                onTriggered: {
                                                    clientSettingsDialog.clientName = pcName
                                                    clientSettingsDialog.clientUuid = pcUuid
                                                    clientSettingsDialog.open()
                                                }
                                            }
                                            MenuItem {
                                                text: pcHasClientSettings ? "✓ " + qsTr("Uses Custom Settings") : qsTr("Uses Global Settings")
                                                enabled: false
                                                font.italic: true
                                            }
                                            NavigableMenuItem {
                                                text: pcHasClientSettings ? qsTr("Switch to Global Settings") : (pcHasSavedClientSettings ? qsTr("Switch to Custom Settings") : qsTr("Create Custom Settings"))
                                                onTriggered: {
                                                    if (pcHasClientSettings) {
                                                        StreamingPreferences.deactivateClientSettings(pcUuid)
                                                        computerModel.refreshData()
                                                    } else if (pcHasSavedClientSettings) {
                                                        StreamingPreferences.activateClientSettings(pcUuid)
                                                        computerModel.refreshData()
                                                    } else {
                                                        clientSettingsDialog.clientName = pcName
                                                        clientSettingsDialog.clientUuid = pcUuid
                                                        clientSettingsDialog.open()
                                                    }
                                                }
                                            }
                                            NavigableMenuItem {
                                                text: qsTr("Rename PC")
                                                onTriggered: {
                                                    renamePcDialog.pcIndex = pcModelIndex
                                                    renamePcDialog.originalName = pcName
                                                    renamePcDialog.open()
                                                }
                                            }
                                            NavigableMenuItem {
                                                text: qsTr("Delete PC")
                                                onTriggered: {
                                                    deletePcDialog.pcIndex = pcModelIndex
                                                    deletePcDialog.pcName = pcName
                                                    deletePcDialog.open()
                                                }
                                            }
                                            NavigableMenuItem {
                                                text: qsTr("View Details")
                                                onTriggered: {
                                                    showPcDetailsDialog.pcDetails = pcDetails
                                                    showPcDetailsDialog.open()
                                                }
                                            }

                                            MenuSeparator {}

                                            NavigableMenuItem {
                                                text: qsTr("Move Up")
                                                enabled: pcModelIndex > 0
                                                onTriggered: computerModel.moveComputer(pcModelIndex, pcModelIndex - 1)
                                            }
                                            NavigableMenuItem {
                                                text: qsTr("Move Down")
                                                enabled: pcModelIndex < computerModel.count() - 1
                                                onTriggered: computerModel.moveComputer(pcModelIndex, pcModelIndex + 1)
                                            }
                                            NavigableMenuItem {
                                                text: qsTr("Move to Position...")
                                                onTriggered: {
                                                    movePcToPositionDialog.currentIndex = pcModelIndex
                                                    movePcToPositionDialog.maxCount = computerModel.count()
                                                    movePcToPositionDialog.open()
                                                }
                                            }
                                        }
                                    }

                                    onPressAndHold: {
                                        if (pcContextMenu.popup) pcContextMenu.popup()
                                        else pcContextMenu.open()
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.RightButton
                                        onClicked: parent.pressAndHold()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }
    }

    // Searching indicator
    Row {
        anchors.centerIn: parent
        spacing: 5
        visible: (showSections ? pcListView.count : computerModel.count()) === 0

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
        property bool pcHasClientSettings: false
        property bool pcHasSavedClientSettings: false

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
        MenuItem {
            text: pcSectionContextMenu.pcHasClientSettings ? "✓ " + qsTr("Uses Custom Settings") : qsTr("Uses Global Settings")
            enabled: false
            font.italic: true
        }
        NavigableMenuItem {
            text: pcSectionContextMenu.pcHasClientSettings ? qsTr("Switch to Global Settings") : (pcSectionContextMenu.pcHasSavedClientSettings ? qsTr("Switch to Custom Settings") : qsTr("Create Custom Settings"))
            onTriggered: {
                if (pcSectionContextMenu.pcHasClientSettings) {
                    StreamingPreferences.deactivateClientSettings(pcSectionContextMenu.pcUuid)
                    computerModel.refreshData()
                } else if (pcSectionContextMenu.pcHasSavedClientSettings) {
                    StreamingPreferences.activateClientSettings(pcSectionContextMenu.pcUuid)
                    computerModel.refreshData()
                } else {
                    clientSettingsDialog.clientName = pcSectionContextMenu.pcName
                    clientSettingsDialog.clientUuid = pcSectionContextMenu.pcUuid
                    clientSettingsDialog.open()
                }
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

        MenuSeparator {}

        NavigableMenuItem {
            text: qsTr("Move Up")
            enabled: pcSectionContextMenu.pcIndex > 0
            onTriggered: computerModel.moveComputer(pcSectionContextMenu.pcIndex, pcSectionContextMenu.pcIndex - 1)
        }
        NavigableMenuItem {
            text: qsTr("Move Down")
            enabled: pcSectionContextMenu.pcIndex < computerModel.count() - 1
            onTriggered: computerModel.moveComputer(pcSectionContextMenu.pcIndex, pcSectionContextMenu.pcIndex + 1)
        }
        NavigableMenuItem {
            text: qsTr("Move to Position...")
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
            pcPositionField.text = String(currentIndex + 1)
            pcPositionField.forceActiveFocus()
            pcPositionField.selectAll()
        }

        onAccepted: {
            var val = parseInt(pcPositionField.text)
            if (!isNaN(val)) {
                var targetIndex = val - 1
                if (targetIndex !== currentIndex && targetIndex >= 0 && targetIndex < maxCount) {
                    computerModel.moveComputer(currentIndex, targetIndex)
                }
            }
        }

        ColumnLayout {
            Label {
                text: qsTr("Move to position (1-%1):").arg(movePcToPositionDialog.maxCount)
                font.bold: true
            }

            TextField {
                id: pcPositionField
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: movePcToPositionDialog.maxCount }
                placeholderText: "1-" + movePcToPositionDialog.maxCount

                Keys.onReturnPressed: movePcToPositionDialog.accept()
                Keys.onEnterPressed: movePcToPositionDialog.accept()
            }
        }
    }
}
