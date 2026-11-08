import QtQuick

Window {
    id: root
    required property QtObject preview
    flags: Qt.WindowStaysOnTopHint | Qt.Tool | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint
    color: "#f5f5f2"
    title: "QR"
    width: 660
    height: 730
    minimumWidth: Math.max(320, preview.modules + 48)
    minimumHeight: Math.max(260, preview.modules + 112)
    Shortcut {
        sequence: "Esc"
        enabled: root.active
        onActivated: { if (menu.visible) menu.visible = false; else root.preview.close() }
    }
    Shortcut { sequences: ["Ctrl+D", "Ctrl+W"]; enabled: root.active; onActivated: root.preview.close() }
    Image {
        id: code
        anchors.horizontalCenter: parent.horizontalCenter
        y: 20 + Math.max(0, (root.height - 112 - height) / 2)
        // Whole physical pixels per QR module, including the quiet zone.
        readonly property real dpr: root.screen.devicePixelRatio
        width: Math.max(1, Math.floor(Math.min(root.width - 48, root.height - 112) * dpr / root.preview.modules)) * root.preview.modules / dpr
        height: width
        smooth: false
        cache: false
        source: "image://qr/" + root.preview.generation
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: hint.top
        anchors.bottomMargin: 8
        width: root.width - 40
        text: root.preview.text
        color: "#3a3a3a"
        font.pixelSize: 13
        elide: Text.ElideMiddle
        wrapMode: Text.WrapAnywhere
        maximumLineCount: 2
        horizontalAlignment: Text.AlignHCenter
    }
    Text {
        id: hint
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: root.width - 24
        text: root.preview.feedback || (root.preview.zh ? "右键菜单 · Esc / Ctrl+D / Ctrl+W 关闭" : "Right-click menu · Esc / Ctrl+D / Ctrl+W to close")
        color: "#666666"
        font.pixelSize: 11
        elide: Text.ElideMiddle
        horizontalAlignment: Text.AlignHCenter
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onWheel: function(wheel) {
            if (root.active && !menu.visible)
                root.preview.zoom(wheel.angleDelta.y / 120)
            wheel.accepted = true
        }
        onPressed: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                menu.x = Math.max(0, Math.min(mouse.x, root.width - menu.width))
                menu.y = Math.max(0, Math.min(mouse.y, root.height - menu.height))
                menu.visible = true
                menu.forceActiveFocus()
            } else {
                menu.visible = false
                root.startSystemMove()
            }
        }
    }
    Rectangle {
        id: menu
        visible: false
        width: 210
        height: 152
        radius: 5
        color: "white"
        border.color: "#aaaaaa"
        property int selected: 0
        onVisibleChanged: if (visible) selected = 0
        function invoke(index) {
            visible = false
            if (index === 0) root.preview.copyImage()
            else if (index === 1) root.preview.saveImage()
            else if (index === 2) root.preview.restoreSize()
            else root.preview.close()
        }
        Keys.onUpPressed: selected = (selected + 3) % 4
        Keys.onDownPressed: selected = (selected + 1) % 4
        Keys.onReturnPressed: invoke(selected)
        Column {
            anchors.fill: parent
            anchors.margins: 4
            Repeater {
                model: root.preview.zh ? ["复制图片", "保存 PNG", "恢复默认大小", "关闭"] : ["Copy image", "Save PNG", "Restore default size", "Close"]
                Rectangle {
                    required property int index
                    required property string modelData
                    width: menu.width - 8
                    height: 36
                    color: menu.selected === index ? "#e5effa" : "transparent"
                    Text { anchors.verticalCenter: parent.verticalCenter; x: 12; text: parent.modelData; color: "#222222" }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onEntered: menu.selected = parent.index
                        onClicked: menu.invoke(parent.index)
                    }
                }
            }
        }
    }
}
