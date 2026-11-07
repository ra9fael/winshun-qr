// Preview.qml - the one preview window: the QR image over its payload.
// Always on top, frameless, a tool (no taskbar, no alt-tab); Esc and a click
// outside close it — the controller's close() reaches QrPreview.cpp, which
// destroys the window. WinShun's panel hides when the preview opens, so the
// click-outside lands wherever the user points next.
import QtQuick

Window {
    id: root

    required property QtObject preview

    flags: Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint | Qt.Tool
    color: "#f5f5f2"
    title: "QR"
    width: 660
    height: 730

    Shortcut {
        sequence: "Esc"
        onActivated: root.preview.close()
    }
    onActiveChanged: if (!active)
        root.preview.close()

    Image {
        id: code

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 24
        width: 600
        height: 600
        fillMode: Image.PreserveAspectFit
        smooth: true
        cache: false
        source: "image://qr/" + root.preview.generation
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: hint.top
        width: 600
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
        text: root.preview.zh ? "Esc 或点击窗外关闭" : "Esc or a click outside closes"
        color: "#9a9a9a"
        font.pixelSize: 11
    }
}
