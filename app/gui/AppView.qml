import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2
import QtQuick.Layouts 1.3

import AppModel 1.0
import ComputerManager 1.0
import SdlGamepadKeyNavigation 1.0
import StreamingPreferences 1.0

Item {
    property int computerIndex
    property AppModel appModel : createModel()
    property bool activated
    property bool showHiddenGames
    property bool showGames

    // Dynamic tile sizing based on preference scale (50-200%)
    property real tileScale: StreamingPreferences.appTileScale / 100.0
    property int gridCellWidth: Math.round(230 * tileScale)
    property int gridCellHeight: Math.round(297 * tileScale)
    property int gridItemWidth: Math.round(220 * tileScale)
    property int gridItemHeight: Math.round(287 * tileScale)
    property int gridIconWidth: Math.round(200 * tileScale)
    property int gridIconHeight: Math.round(267 * tileScale)

    // View mode: 0=grid, 1=list
    property int viewMode: StreamingPreferences.appViewMode

    // Current folder path
    property string currentFolder: ""

    id: appViewRoot
    objectName: qsTr("Apps")

    function computerLost()
    {
        stackView.pop()
    }

    Component.onCompleted: {
        // Nothing special needed here
    }

    StackView.onActivated: {
        appModel.computerLost.connect(computerLost)
        activated = true

        if (!showGames && !showHiddenGames) {
            var directLaunchAppIndex = appModel.getDirectLaunchAppIndex();
            if (directLaunchAppIndex >= 0) {
                if (viewMode === 0) {
                    appGrid.currentIndex = directLaunchAppIndex
                    appGrid.currentItem.launchOrResumeSelectedApp(false)
                } else {
                    // List view direct launch
                    listLaunchOrResumeApp(directLaunchAppIndex, appModel.getAppName(directLaunchAppIndex), appModel.getAppId(directLaunchAppIndex), false)
                }
                showGames = true
            }
        }
    }

    StackView.onDeactivating: {
        appModel.computerLost.disconnect(computerLost)
        activated = false
    }

    function createModel()
    {
        var model = Qt.createQmlObject('import AppModel 1.0; AppModel {}', parent, '')
        model.initialize(ComputerManager, computerIndex, showHiddenGames)
        return model
    }

    // ==================== TOOLBAR ====================
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Top toolbar for sort/view/folder controls
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

                // Folder breadcrumb / back
                ToolButton {
                    visible: currentFolder !== ""
                    icon.source: "qrc:/res/arrow_left.svg"
                    icon.width: 24
                    icon.height: 24
                    onClicked: {
                        currentFolder = ""
                        appModel.setCurrentFolder("")
                    }
                    ToolTip.text: qsTr("Back to root")
                    ToolTip.visible: hovered
                    ToolTip.delay: 1000
                }

                Label {
                    text: currentFolder !== "" ? ("📁 " + currentFolder) : qsTr("All Apps")
                    font.pointSize: 14
                    font.bold: true
                    Layout.fillWidth: true
                    elide: Label.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }

                // Sort mode selector
                ComboBox {
                    id: sortModeCombo
                    Layout.preferredWidth: 160
                    model: [qsTr("Alphabetical"), qsTr("Custom Order")]
                    currentIndex: appModel.getSortMode()
                    onActivated: {
                        appModel.setSortMode(index)
                    }
                    ToolTip.text: qsTr("Sort order")
                    ToolTip.visible: hovered
                    ToolTip.delay: 1000
                }

                // View mode toggle (grid / list)
                ToolButton {
                    id: viewModeButton
                    text: viewMode === 0 ? "☰" : "⊞"
                    font.pointSize: 18
                    onClicked: {
                        viewMode = viewMode === 0 ? 1 : 0
                        StreamingPreferences.appViewMode = viewMode
                        StreamingPreferences.save()
                    }
                    ToolTip.text: viewMode === 0 ? qsTr("Switch to List View") : qsTr("Switch to Grid View")
                    ToolTip.visible: hovered
                    ToolTip.delay: 1000
                }

                // Tile size slider
                Label {
                    text: "🔍"
                    font.pointSize: 14
                }
                Slider {
                    id: tileSizeSlider
                    Layout.preferredWidth: 120
                    from: 50
                    to: 200
                    stepSize: 10
                    value: StreamingPreferences.appTileScale
                    onMoved: {
                        StreamingPreferences.appTileScale = value
                        StreamingPreferences.save()
                    }
                    ToolTip {
                        parent: tileSizeSlider.handle
                        visible: tileSizeSlider.pressed
                        text: Math.round(tileSizeSlider.value) + "%"
                    }
                }

                // Folder management button
                ToolButton {
                    id: folderButton
                    text: "📁+"
                    font.pointSize: 14
                    onClicked: folderMenu.open()
                    ToolTip.text: qsTr("Manage Folders")
                    ToolTip.visible: hovered
                    ToolTip.delay: 1000

                    Menu {
                        id: folderMenu
                        y: folderButton.height

                        MenuItem {
                            text: qsTr("Create New Folder...")
                            onTriggered: createFolderDialog.open()
                        }

                        MenuSeparator { visible: folderRepeater.count > 0 }

                        Repeater {
                            id: folderRepeater
                            model: StreamingPreferences.getAppFolders(appModel.getComputerUuid())

                            MenuItem {
                                text: "📁 " + modelData
                                onTriggered: {
                                    currentFolder = modelData
                                    appModel.setCurrentFolder(modelData)
                                }
                            }
                        }

                        MenuSeparator { visible: currentFolder !== "" }

                        MenuItem {
                            text: qsTr("Show All (Exit Folder)")
                            visible: currentFolder !== ""
                            onTriggered: {
                                currentFolder = ""
                                appModel.setCurrentFolder("")
                            }
                        }
                    }
                }
            }
        }

        // ==================== GRID VIEW ====================
        CenteredGridView {
            id: appGrid
            visible: viewMode === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: viewMode === 0
            activeFocusOnTab: true
            clip: true
            topMargin: 20
            bottomMargin: 5
            cellWidth: gridCellWidth
            cellHeight: gridCellHeight

            Component.onCompleted: {
                currentIndex = -1
            }

            model: appModel

            // Folder items at the top when in root view
            // (Folders shown as special delegates when currentFolder is empty)

            delegate: NavigableItemDelegate {
                width: gridItemWidth
                height: gridItemHeight
                grid: appGrid

                property alias appContextMenu: appContextMenuLoader.item
                property alias appNameText: appNameTextLoader.item
                property int delegateAppId: model.appid
                property int delegateIndex: index

                opacity: model.hidden ? 0.4 : 1.0

                Image {
                    property bool isPlaceholder: false

                    id: appIcon
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Math.round(10 * tileScale)
                    source: model.boxart

                    onSourceSizeChanged: {
                        if (!model.appCollectorGame &&
                            ((sourceSize.width === 130 && sourceSize.height === 180) ||
                             (sourceSize.width === 628 && sourceSize.height === 888) ||
                             (sourceSize.width === 200 && sourceSize.height === 266)))
                        {
                            isPlaceholder = true
                        }
                        else
                        {
                            isPlaceholder = false
                        }

                        width = gridIconWidth
                        height = gridIconHeight
                    }

                    ToolTip.text: model.name
                    ToolTip.delay: 1000
                    ToolTip.timeout: 5000
                    ToolTip.visible: (parent.hovered || parent.highlighted) && (!appNameText || appNameText.truncated)
                }

                Loader {
                    active: model.running
                    asynchronous: true
                    anchors.fill: appIcon

                    sourceComponent: Item {
                        RoundButton {
                            anchors.horizontalCenterOffset: appIcon.isPlaceholder ? Math.round(-47 * tileScale) : 0
                            anchors.verticalCenterOffset: appIcon.isPlaceholder ? Math.round(-75 * tileScale) : Math.round(-60 * tileScale)
                            anchors.centerIn: parent
                            implicitWidth: Math.round(85 * tileScale)
                            implicitHeight: Math.round(85 * tileScale)

                            icon.source: "qrc:/res/play_arrow_FILL1_wght700_GRAD200_opsz48.svg"
                            icon.width: Math.round(75 * tileScale)
                            icon.height: Math.round(75 * tileScale)

                            onClicked: {
                                launchOrResumeSelectedApp(true)
                            }

                            ToolTip.text: qsTr("Resume Game")
                            ToolTip.delay: 1000
                            ToolTip.timeout: 3000
                            ToolTip.visible: hovered

                            Material.background: "#D0808080"
                        }

                        RoundButton {
                            anchors.horizontalCenterOffset: appIcon.isPlaceholder ? Math.round(47 * tileScale) : 0
                            anchors.verticalCenterOffset: appIcon.isPlaceholder ? Math.round(-75 * tileScale) : Math.round(60 * tileScale)
                            anchors.centerIn: parent
                            implicitWidth: Math.round(85 * tileScale)
                            implicitHeight: Math.round(85 * tileScale)

                            icon.source: "qrc:/res/stop_FILL1_wght700_GRAD200_opsz48.svg"
                            icon.width: Math.round(75 * tileScale)
                            icon.height: Math.round(75 * tileScale)

                            onClicked: {
                                doQuitGame()
                            }

                            ToolTip.text: qsTr("Quit Game")
                            ToolTip.delay: 1000
                            ToolTip.timeout: 3000
                            ToolTip.visible: hovered

                            Material.background: "#D0808080"
                        }
                    }
                }

                Loader {
                    id: appNameTextLoader
                    active: appIcon.isPlaceholder

                    width: appIcon.width
                    height: model.running ? Math.round(175 * tileScale) : appIcon.height

                    anchors.left: appIcon.left
                    anchors.right: appIcon.right
                    anchors.bottom: appIcon.bottom

                    sourceComponent: Label {
                        id: appNameText
                        text: model.name
                        font.pointSize: Math.round(22 * tileScale)
                        leftPadding: Math.round(20 * tileScale)
                        rightPadding: Math.round(20 * tileScale)
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        elide: Text.ElideRight
                    }
                }

                function launchOrResumeSelectedApp(quitExistingApp)
                {
                    var runningId = appModel.getRunningAppId()
                    if (runningId !== 0 && runningId !== model.appid) {
                        if (quitExistingApp) {
                            quitAppDialog.appName = appModel.getRunningAppName()
                            quitAppDialog.segueToStream = true
                            quitAppDialog.nextAppName = model.name
                            quitAppDialog.nextAppIndex = index
                            quitAppDialog.open()
                        }
                        return
                    }

                    var component = Qt.createComponent("StreamSegue.qml")
                    var segue = component.createObject(stackView, {
                                                           "appName": model.name,
                                                           "session": appModel.createSessionForApp(index),
                                                           "isResume": runningId === model.appid
                                                       })
                    stackView.push(segue)
                }

                onClicked: {
                    if (!model.running) {
                        launchOrResumeSelectedApp(true)
                    }
                }

                onPressAndHold: {
                    if (appContextMenu.popup) {
                        appContextMenu.popup()
                    }
                    else {
                        appContextMenu.open()
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton;
                    onClicked: {
                        parent.pressAndHold()
                    }
                }

                Keys.onReturnPressed: {
                    if (model.running) {
                        appContextMenu.open()
                    }
                }

                Keys.onEnterPressed: {
                    if (model.running) {
                        appContextMenu.open()
                    }
                }

                Keys.onMenuPressed: {
                    appContextMenu.open()
                }

                function doQuitGame() {
                    quitAppDialog.appName = appModel.getRunningAppName()
                    quitAppDialog.segueToStream = false
                    quitAppDialog.open()
                }

                Loader {
                    id: appContextMenuLoader
                    asynchronous: true
                    sourceComponent: NavigableMenu {
                        id: appContextMenu
                        NavigableMenuItem {
                            text: model.running ? qsTr("Resume Game") : qsTr("Launch Game")
                            onTriggered: launchOrResumeSelectedApp(true)
                        }
                        NavigableMenuItem {
                            text: qsTr("Quit Game")
                            onTriggered: doQuitGame()
                            visible: model.running
                        }
                        NavigableMenuItem {
                            checkable: true
                            checked: model.directLaunch
                            text: qsTr("Direct Launch")
                            onTriggered: appModel.setAppDirectLaunch(model.index, !model.directLaunch)
                            enabled: !model.hidden

                            ToolTip.text: qsTr("Launch this app immediately when the host is selected, bypassing the app selection grid.")
                            ToolTip.delay: 1000
                            ToolTip.timeout: 3000
                            ToolTip.visible: hovered
                        }
                        NavigableMenuItem {
                            checkable: true
                            checked: model.hidden
                            text: qsTr("Hide Game")
                            onTriggered: appModel.setAppHidden(model.index, !model.hidden)
                            enabled: model.hidden || (!model.running && !model.directLaunch)

                            ToolTip.text: qsTr("Hide this game from the app grid. To access hidden games, right-click on the host and choose %1.").arg(qsTr("View All Apps"))
                            ToolTip.delay: 1000
                            ToolTip.timeout: 5000
                            ToolTip.visible: hovered
                        }

                        MenuSeparator {}

                        // Move up/down for custom ordering
                        NavigableMenuItem {
                            text: qsTr("Move Up")
                            visible: appModel.getSortMode() === 1
                            enabled: model.index > 0
                            onTriggered: appModel.moveApp(model.index, model.index - 1)
                        }
                        NavigableMenuItem {
                            text: qsTr("Move Down")
                            visible: appModel.getSortMode() === 1
                            enabled: model.index < appModel.count() - 1
                            onTriggered: appModel.moveApp(model.index, model.index + 1)
                        }

                        MenuSeparator { visible: folderSubMenu.visible }

                        // Folder management
                        Menu {
                            id: folderSubMenu
                            title: qsTr("Move to Folder...")

                            MenuItem {
                                text: qsTr("(No Folder)")
                                onTriggered: {
                                    var compUuid = appModel.getComputerUuid()
                                    var currentFolderName = StreamingPreferences.getAppFolder(compUuid, String(delegateAppId))
                                    if (currentFolderName !== "") {
                                        StreamingPreferences.removeAppFromFolder(compUuid, currentFolderName, String(delegateAppId))
                                    }
                                }
                            }

                            MenuSeparator {}

                            Repeater {
                                model: StreamingPreferences.getAppFolders(appModel.getComputerUuid())
                                MenuItem {
                                    text: "📁 " + modelData
                                    onTriggered: {
                                        var compUuid = appModel.getComputerUuid()
                                        var appIdStr = String(delegateAppId)
                                        // Remove from any current folder first
                                        var oldFolder = StreamingPreferences.getAppFolder(compUuid, appIdStr)
                                        if (oldFolder !== "") {
                                            StreamingPreferences.removeAppFromFolder(compUuid, oldFolder, appIdStr)
                                        }
                                        StreamingPreferences.addAppToFolder(compUuid, modelData, appIdStr)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }

        // ==================== LIST VIEW ====================
        ListView {
            id: appListView
            visible: viewMode === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: viewMode === 1
            activeFocusOnTab: true
            topMargin: 10
            bottomMargin: 5
            spacing: 2
            clip: true

            Component.onCompleted: {
                currentIndex = -1
            }

            model: appModel

            delegate: ItemDelegate {
                width: appListView.width
                height: Math.round(60 * tileScale)

                opacity: model.hidden ? 0.4 : 1.0
                highlighted: appListView.activeFocus && appListView.currentIndex === index

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 15
                    anchors.rightMargin: 15
                    spacing: 15

                    Image {
                        source: model.boxart
                        Layout.preferredWidth: Math.round(45 * tileScale)
                        Layout.preferredHeight: Math.round(60 * tileScale)
                        fillMode: Image.PreserveAspectFit
                    }

                    Label {
                        text: model.name
                        font.pointSize: Math.round(16 * tileScale)
                        Layout.fillWidth: true
                        elide: Label.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    // Running indicator
                    Label {
                        visible: model.running
                        text: "▶"
                        font.pointSize: Math.round(16 * tileScale)
                        color: "#4CAF50"
                    }

                    // Folder indicator
                    Label {
                        visible: model.folder !== ""
                        text: "📁"
                        font.pointSize: Math.round(12 * tileScale)
                        opacity: 0.6
                    }
                }

                onClicked: {
                    if (!model.running) {
                        listLaunchOrResumeApp(index, model.name, model.appid, true)
                    }
                }

                onPressAndHold: {
                    listContextMenu.appIndex = index
                    listContextMenu.appName = model.name
                    listContextMenu.appRunning = model.running
                    listContextMenu.appHidden = model.hidden
                    listContextMenu.appDirectLaunch = model.directLaunch
                    listContextMenu.appId = model.appid
                    if (listContextMenu.popup) {
                        listContextMenu.popup()
                    } else {
                        listContextMenu.open()
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: {
                        parent.pressAndHold()
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }

        // ==================== FOLDER ITEMS AT TOP (when in root, grid mode) ====================
        // Folders are displayed as a separate Flow before the grid when in root
    }

    function listLaunchOrResumeApp(appIndex, appName, appId, quitExistingApp) {
        var runningId = appModel.getRunningAppId()
        if (runningId !== 0 && runningId !== appId) {
            if (quitExistingApp) {
                quitAppDialog.appName = appModel.getRunningAppName()
                quitAppDialog.segueToStream = true
                quitAppDialog.nextAppName = appName
                quitAppDialog.nextAppIndex = appIndex
                quitAppDialog.open()
            }
            return
        }

        var component = Qt.createComponent("StreamSegue.qml")
        var segue = component.createObject(stackView, {
                                               "appName": appName,
                                               "session": appModel.createSessionForApp(appIndex),
                                               "isResume": runningId === appId
                                           })
        stackView.push(segue)
    }

    // Context menu for list view
    NavigableMenu {
        id: listContextMenu
        property int appIndex: -1
        property string appName: ""
        property bool appRunning: false
        property bool appHidden: false
        property bool appDirectLaunch: false
        property int appId: 0

        NavigableMenuItem {
            text: listContextMenu.appRunning ? qsTr("Resume Game") : qsTr("Launch Game")
            onTriggered: listLaunchOrResumeApp(listContextMenu.appIndex, listContextMenu.appName, listContextMenu.appId, true)
        }
        NavigableMenuItem {
            text: qsTr("Quit Game")
            onTriggered: {
                quitAppDialog.appName = appModel.getRunningAppName()
                quitAppDialog.segueToStream = false
                quitAppDialog.open()
            }
            visible: listContextMenu.appRunning
        }
        NavigableMenuItem {
            checkable: true
            checked: listContextMenu.appDirectLaunch
            text: qsTr("Direct Launch")
            onTriggered: appModel.setAppDirectLaunch(listContextMenu.appIndex, !listContextMenu.appDirectLaunch)
            enabled: !listContextMenu.appHidden

            ToolTip.text: qsTr("Launch this app immediately when the host is selected, bypassing the app selection grid.")
            ToolTip.delay: 1000
            ToolTip.timeout: 3000
            ToolTip.visible: hovered
        }
        NavigableMenuItem {
            checkable: true
            checked: listContextMenu.appHidden
            text: qsTr("Hide Game")
            onTriggered: appModel.setAppHidden(listContextMenu.appIndex, !listContextMenu.appHidden)
            enabled: listContextMenu.appHidden || (!listContextMenu.appRunning && !listContextMenu.appDirectLaunch)

            ToolTip.text: qsTr("Hide this game from the app grid. To access hidden games, right-click on the host and choose %1.").arg(qsTr("View All Apps"))
            ToolTip.delay: 1000
            ToolTip.timeout: 5000
            ToolTip.visible: hovered
        }

        MenuSeparator {}

        NavigableMenuItem {
            text: qsTr("Move Up")
            visible: appModel.getSortMode() === 1
            enabled: listContextMenu.appIndex > 0
            onTriggered: appModel.moveApp(listContextMenu.appIndex, listContextMenu.appIndex - 1)
        }
        NavigableMenuItem {
            text: qsTr("Move Down")
            visible: appModel.getSortMode() === 1
            enabled: listContextMenu.appIndex < appModel.count() - 1
            onTriggered: appModel.moveApp(listContextMenu.appIndex, listContextMenu.appIndex + 1)
        }

        MenuSeparator {}

        // Folder management in list view
        Menu {
            id: listFolderSubMenu
            title: qsTr("Move to Folder...")

            MenuItem {
                text: qsTr("(No Folder)")
                onTriggered: {
                    var compUuid = appModel.getComputerUuid()
                    var currentFolderName = StreamingPreferences.getAppFolder(compUuid, String(listContextMenu.appId))
                    if (currentFolderName !== "") {
                        StreamingPreferences.removeAppFromFolder(compUuid, currentFolderName, String(listContextMenu.appId))
                    }
                }
            }

            MenuSeparator {}

            Repeater {
                model: StreamingPreferences.getAppFolders(appModel.getComputerUuid())
                MenuItem {
                    text: "📁 " + modelData
                    onTriggered: {
                        var compUuid = appModel.getComputerUuid()
                        var appIdStr = String(listContextMenu.appId)
                        var oldFolder = StreamingPreferences.getAppFolder(compUuid, appIdStr)
                        if (oldFolder !== "") {
                            StreamingPreferences.removeAppFromFolder(compUuid, oldFolder, appIdStr)
                        }
                        StreamingPreferences.addAppToFolder(compUuid, modelData, appIdStr)
                    }
                }
            }
        }
    }

    Row {
        anchors.centerIn: parent
        spacing: 5
        visible: (viewMode === 0 ? appGrid.count : appListView.count) === 0

        Label {
            text: currentFolder !== "" ?
                      qsTr("This folder is empty") :
                      qsTr("This computer doesn't seem to have any applications or some applications are hidden")
            font.pointSize: 20
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    // Create Folder Dialog
    NavigableDialog {
        id: createFolderDialog
        property string label: qsTr("Enter the folder name:")

        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            folderNameText.forceActiveFocus()
        }

        onClosed: {
            folderNameText.clear()
        }

        onAccepted: {
            if (folderNameText.text) {
                StreamingPreferences.createAppFolder(appModel.getComputerUuid(), folderNameText.text)
                // Refresh the folder repeater
                folderRepeater.model = StreamingPreferences.getAppFolders(appModel.getComputerUuid())
            }
        }

        ColumnLayout {
            Label {
                text: createFolderDialog.label
                font.bold: true
            }

            TextField {
                id: folderNameText
                Layout.fillWidth: true
                focus: true

                Keys.onReturnPressed: {
                    createFolderDialog.accept()
                }
                Keys.onEnterPressed: {
                    createFolderDialog.accept()
                }
            }
        }
    }

    NavigableMessageDialog {
        id: quitAppDialog
        property string appName : ""
        property bool segueToStream : false
        property string nextAppName: ""
        property int nextAppIndex: 0
        text:qsTr("Are you sure you want to quit %1? Any unsaved progress will be lost.").arg(appName)
        standardButtons: Dialog.Yes | Dialog.No

        function quitApp() {
            var component = Qt.createComponent("QuitSegue.qml")
            var params = {"appName": appName, "quitRunningAppFn": function() { appModel.quitRunningApp() }}
            if (segueToStream) {
                params.nextAppName = nextAppName
                params.nextSession = appModel.createSessionForApp(nextAppIndex)
            }
            else {
                params.nextAppName = null
                params.nextSession = null
            }

            stackView.push(component.createObject(stackView, params))
        }

        onAccepted: quitApp()
    }
}
