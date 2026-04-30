import QtQuick 2.0
import QtQuick.Controls 2.2

ItemDelegate {
    property var grid: null

    highlighted: grid && grid.activeFocus && grid.currentItem === this

    Keys.onLeftPressed: {
        if (grid && grid.moveCurrentIndexLeft) grid.moveCurrentIndexLeft()
    }
    Keys.onRightPressed: {
        if (grid && grid.moveCurrentIndexRight) grid.moveCurrentIndexRight()
    }
    Keys.onDownPressed: {
        if (grid && grid.moveCurrentIndexDown) grid.moveCurrentIndexDown()
    }
    Keys.onUpPressed: {
        if (grid && grid.moveCurrentIndexUp) grid.moveCurrentIndexUp()
    }
    Keys.onReturnPressed: {
        clicked()
    }
    Keys.onEnterPressed: {
        clicked()
    }
}
