import QtQuick
import QtQuick.Controls
import NotepadQtQuick

Item {
    id: minimapRoot

    required property Flickable editorFlickable
    required property TextEdit editorTextEdit
    property QtObject appPalette

    implicitWidth: 100
    clip: true

    // Background track
    Rectangle {
        anchors.fill: parent
        color: appPalette ? appPalette.editorBg : "#14161b"

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: appPalette ? appPalette.border : "#282c34"
        }
    }

    // The document, painted as colored blocks (C++)
    MinimapView {
        id: view
        anchors.fill: parent
        document: minimapRoot.editorTextEdit.textDocument
        textColor: minimapRoot.appPalette ? minimapRoot.appPalette.textMain : "#abb2bf"

        // Only used when the map is taller than the item (very long files)
        scrollProgress: {
            var maxScroll = minimapRoot.editorFlickable.contentHeight - minimapRoot.editorFlickable.height;
            return maxScroll > 0 ? minimapRoot.editorFlickable.contentY / maxScroll : 0;
        }
    }

    // Viewport slider: the part of the document visible in the editor
    Rectangle {
        id: viewportSlider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 2
        anchors.rightMargin: 2
        radius: 3

        color: minimapRoot.appPalette ? minimapRoot.appPalette.highlight : "#528bff"
        opacity: mapMouse.pressed ? 0.35 : (mapMouse.containsMouse ? 0.25 : 0.16)
        border.color: minimapRoot.appPalette ? minimapRoot.appPalette.highlight : "#61afef"
        border.width: 1

        height: Math.min(view.mapHeight, Math.max(12, editorFlickable.height * view.mapScale))
        y: editorFlickable.contentY * view.mapScale - view.mapOffset
    }

    // Click or drag anywhere to scroll the editor there
    MouseArea {
        id: mapMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor

        function scrollTo(mouseY) {
            if (view.mapScale <= 0) return;
            var fl = minimapRoot.editorFlickable;
            var target = (mouseY + view.mapOffset - viewportSlider.height / 2) / view.mapScale;
            var maxScroll = fl.contentHeight - fl.height;
            if (maxScroll > 0)
                fl.contentY = Math.max(0, Math.min(maxScroll, target));
        }

        onPressed: function(mouse) { scrollTo(mouse.y); }
        onPositionChanged: function(mouse) { if (pressed) scrollTo(mouse.y); }
    }
}
