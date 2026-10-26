import QtQuick
import QtQuick.Controls
import NotepadQtQuick
import "./editorComponents"

Item {
    id: root

    required property QtObject appPalette
    property alias text: editor.text
    property alias textEdit: editor

    property int editorFontSize: 13
    property string editorFontFamily: "Monospace"
    property string diskContent: ""
    property string originalContent: ""
    property string currentFilePath: ""
    property bool isProgrammaticChange: false

    property var stickyLinesData: []

    MultiCursor { id: multiCaret }
    StickyScroll { id: stickyScroll } // New

    property int caretTick: 0
    property int prevCaret: 0
    property int curCaret: 0

    Connections {
        target: multiCaret
        function onPositionsChanged() { Qt.callLater(function() { root.caretTick++; }); }
    }

    signal textModified()
    signal zoomRequested(int delta)

    function realText() { return lineGutter.realText(); }

    // Scroll so the text position `pos` is on screen. If it is off-screen,
    // it gets centred; if already comfortably visible, nothing moves.
    function ensureVisible(pos) {
        var r = editor.positionToRectangle(pos);
        var p = editor.mapToItem(editorFlickable.contentItem, r.x, r.y);

        // Calculate the height of roughly 5 lines of text
        var lineHeight = editor.font.pixelSize + 4; // Adjust based on your text line spacing
        var lineOffset = lineHeight * 5;

        // Target is the function position minus 5 lines so it lands comfortably below the sticky header
        var target = p.y - lineOffset;

        var maxY = Math.max(0, editorFlickable.contentHeight - editorFlickable.height);
        editorFlickable.contentY = Math.max(0, Math.min(maxY, target));
    }

    // Line-comment token by file type; "" = no line comments for this type
    function commentTokenFor(path) {
        var p = path.toLowerCase();
        var name = p.substring(p.lastIndexOf("/") + 1);
        if (name === "cmakelists.txt" || name === "makefile" || name === "dockerfile")
            return "#";
        var dot = name.lastIndexOf(".");
        var ext = dot >= 0 ? name.substring(dot + 1) : "";
        switch (ext) {
            case "py": case "sh": case "bash": case "zsh": case "fish": case "rb":
            case "pl": case "r": case "yml": case "yaml": case "toml": case "conf":
            case "cmake": case "mk": case "gitignore":
                return "#";
            case "lua": case "sql": case "hs":
                return "--";
            case "ini":
                return ";";
            case "txt": case "md": case "log": case "html": case "htm": case "xml": case "css": case "json":
                return "";
            default:
                return "//";
        }
    }

    // Scroll the minimum needed to keep the caret on screen
    function followCaret() {
        var r = editor.cursorRectangle;
        var p = editor.mapToItem(editorFlickable.contentItem, r.x, r.y);
        var h = r.height > 0 ? r.height : editor.font.pixelSize + 4;
        var margin = editor.font.pixelSize * 1.5;
        var maxY = Math.max(0, editorFlickable.contentHeight - editorFlickable.height);

        if (p.y + h > editorFlickable.contentY + editorFlickable.height - margin)
            editorFlickable.contentY = Math.min(maxY, p.y + h - editorFlickable.height + margin);
        else if (p.y < editorFlickable.contentY + margin)
            editorFlickable.contentY = Math.max(0, p.y - margin);
    }

    Minimap {
        id: minimap
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 70
        z: 20
        editorFlickable: editorFlickable
        editorTextEdit: editor
        appPalette: root.appPalette
    }

    Flickable {
        id: editorFlickable
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: minimap.left
        clip: false
        pixelAligned: true
        boundsBehavior: Flickable.StopAtBounds

        contentWidth: containerItem.width
        contentHeight: Math.max(height, containerItem.height)

        property string wordToHighlight: ""

        // Sticky lines for the current scroll position, fresh from the document
        function computeSticky() {
            if (contentY <= 0) return [];
            var pos = editor.positionAt(editor.leftPadding + 1, contentY + editor.topPadding + 1);
            var rawData = stickyScroll.getStickyLinesForPosition(pos);
            var parsed = [];
            for (var i = 0; i < rawData.length; ++i)
                parsed.push(rawData[i]);
            return parsed;
        }

        function refreshSticky() { root.stickyLinesData = computeSticky(); }

        onContentYChanged: refreshSticky()

        Timer {
            id: selectionTimer
            interval: 120
            onTriggered: {
                var len = editor.selectionEnd - editor.selectionStart;
                var word = "";
                if (len > 0 && len <= 64) {
                    var t = editor.selectedText;
                    if (t.indexOf("\n") === -1) word = t;
                }
                editorFlickable.wordToHighlight = word;
                highlighter.cursorPosition = editor.cursorPosition;
            }
        }

        Connections {
            target: editor
            function onSelectionStartChanged() { selectionTimer.restart(); }
            function onSelectionEndChanged()   { selectionTimer.restart(); }
            function onCursorPositionChanged() {
                root.prevCaret = root.curCaret;
                root.curCaret = editor.cursorPosition;
                multiCaret.mergeWith(editor.cursorPosition);
                selectionTimer.restart();
                highlighter.cursorPosition = editor.cursorPosition;
                editor.cursorVisible = false;
                Qt.callLater(function() {
                    editor.cursorVisible = true;
                });
            }
            function onCursorRectangleChanged() {
                if (editor.activeFocus)
                    Qt.callLater(root.followCaret);
            }
        }

        CppHighlighter {
            id: highlighter
            highlightedWord: editorFlickable.wordToHighlight
            isDark: root.appPalette.isDark
        }

        function updateHighlighter() {
            if (!editor.textDocument) return;

            highlighter.filePath = root.currentFilePath;
            highlighter.setDocument(editor.textDocument);
            highlighter.cursorPosition = editor.cursorPosition;
        }

        Connections {
            target: root
            function onCurrentFilePathChanged() { editorFlickable.updateHighlighter(); }
        }

        MouseArea {
            anchors.fill: parent
            z: 15
            acceptedButtons: Qt.NoButton

            onWheel: function(wheel) {
                if (wheel.modifiers & Qt.ControlModifier) {
                    if (wheel.angleDelta.y > 0) {
                        root.zoomRequested(1);
                    } else if (wheel.angleDelta.y < 0) {
                        root.zoomRequested(-1);
                    }
                    wheel.accepted = true;
                } else {
                    wheel.accepted = false;
                }
            }
        }

        Item {
            id: containerItem
            width: editorFlickable.width
            height: Math.max(editor.implicitHeight + 40, editorFlickable.height)

            Row {
                id: contentRow
                anchors.fill: parent
                spacing: 0

                EditorLineGutter {
                    id: lineGutter
                    height: parent.height
                    editor: editor
                    flick: editorFlickable
                    appPalette: root.appPalette
                    diskContent: root.diskContent
                    originalContent: root.originalContent
                    editorFontFamily: root.editorFontFamily
                    editorFontSize: root.editorFontSize
                }

                TextEdit {
                    id: editor
                    z: 2
                    width: editorFlickable.width - lineGutter.width - 12
                    height: Math.max(implicitHeight, editorFlickable.height)
                    topPadding: 10
                    leftPadding: 6
                    bottomPadding: 20
                    color: root.appPalette.textMain
                    font.family: root.editorFontFamily
                    font.pixelSize: root.editorFontSize
                    selectByMouse: true
                    persistentSelection: true
                    focus: true
                    activeFocusOnTab: true
                  //  cursorVisible: activeFocus
                    wrapMode: TextEdit.WrapAnywhere
                    selectionColor: root.appPalette.textSelectionBg
                    selectedTextColor: root.appPalette.textSelectedColor

                    property bool isHandlingSpecialKey: false

                    EditorKeyHandler {
                        id: keyHandler
                        editor: editor
                        multi: multiCaret
                        commentToken: root.commentTokenFor(root.currentFilePath)
                    }

                    Component.onCompleted: {
                        editorFlickable.updateHighlighter();
                        editor.forceActiveFocus();
                        multiCaret.setDocument(editor.textDocument);
                        stickyScroll.setDocument(editor.textDocument);
                    }

                    MouseArea {
                        id: emptyAreaBelowText
                        anchors.fill: parent
                        z: 0
                        cursorShape: Qt.IBeamCursor
                        acceptedButtons: Qt.LeftButton
                        hoverEnabled: true
                        enabled: emptyAreaBelowText.mouseY > editor.contentHeight + editor.topPadding

                        onClicked: {
                            editor.forceActiveFocus();
                            editor.cursorPosition = editor.text.length;
                        }
                    }

                    Repeater {
                        model: multiCaret.positions
                        delegate: Rectangle {
                            required property int modelData
                            readonly property rect caretRect: {
                                root.caretTick; editor.width; editor.contentHeight; editor.font.pixelSize;
                                return editor.positionToRectangle(modelData);
                            }
                            x: caretRect.x
                            y: caretRect.y
                            width: 2
                            height: caretRect.height > 0 ? caretRect.height : editor.font.pixelSize + 4
                            color: editor.color
                            visible: editor.activeFocus
                            z: 5
                        }
                    }

                    // Alt+click adds a caret
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        acceptedModifiers: Qt.AltModifier
                        onTapped: function(eventPoint) {
                            var clicked = editor.positionAt(eventPoint.position.x, eventPoint.position.y);
                            if (clicked === root.curCaret && root.prevCaret !== root.curCaret)
                                multiCaret.addCursor(root.prevCaret);   // the click moved the main caret; keep the old spot too
                                else if (clicked !== root.curCaret)
                                    multiCaret.addCursor(clicked);
                        }
                    }

                    // A normal click goes back to a single caret
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        acceptedModifiers: Qt.NoModifier
                        onTapped: multiCaret.clear()
                    }

                    Keys.onPressed: function(event) {
                        keyHandler.handleKeyPress(event);
                    }

                    onTextChanged: {
                        if (!isProgrammaticChange && !editor.isHandlingSpecialKey) {
                            root.textModified();
                        }
                    }

                    // onCursorRectangleChanged: {
                    //     var curY = cursorRectangle.y;
                    //     var curH = cursorRectangle.height > 0 ? cursorRectangle.height : font.pixelSize + 4;
                    //     var maxY = Math.max(0, editorFlickable.contentHeight - editorFlickable.height);
                    //
                    //     if (curY + curH > editorFlickable.contentY + editorFlickable.height - 30) {
                    //         editorFlickable.contentY = Math.min(maxY, (curY + curH) - editorFlickable.height + 30);
                    //     } else if (curY < editorFlickable.contentY + 10) {
                    //         editorFlickable.contentY = Math.max(0, curY - 10);
                    //     }
                    // }
                }
            }

            // Full-Width Sticky Scroll Overlay
            Rectangle {
                id: stickyHeaderItem
                width: editorFlickable.width
                // Bulletproof height: Array length * explicit row height
                height: (root.stickyLinesData ? root.stickyLinesData.length : 0) * (root.editorFontSize + 12)
                x: 0
                y: editorFlickable.contentY > 0 ? editorFlickable.contentY : 0

                color: root.appPalette.editorBg
                visible: height > 0 // Only show if we physically have height
                z: 10

                // Bottom separator line
                Rectangle {
                    width: parent.width
                    height: 1
                    anchors.bottom: parent.bottom
                    color: root.appPalette.textSelectionBg
                    opacity: 0.8
                    z: 1 //
                }

                Column {
                    id: stickyCol
                    width: parent.width

                    Repeater {
                        model: root.stickyLinesData

                        delegate: Item {
                            width: stickyCol.width
                            height: root.editorFontSize + 12 // Explicit row height

                            // NEW: Make the entire sticky line clickable
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: function(mouse) {
                                    var row = index;
                                    var blockSelect = (mouse.modifiers & Qt.ShiftModifier) !== 0;

                                    // Re-read from the document: the pinned data can be stale after typing
                                    var d = editorFlickable.computeSticky()[row];
                                    Qt.callLater(editorFlickable.refreshSticky);
                                    if (!d) return;

                                    editor.forceActiveFocus();
                                    if (blockSelect && d.endPosition > d.position) {
                                        multiCaret.clear();
                                        editor.cursorPosition = d.endPosition;
                                        editor.moveCursorSelection(d.position, TextEdit.SelectCharacters);
                                    } else {
                                        editor.cursorPosition = d.position;
                                    }
                                    root.ensureVisible(d.position);
                                }
                            }

                            // 1. The Gutter Overlay (Line Numbers)
                            Rectangle {
                                width: lineGutter.width
                                height: parent.height
                                color: root.appPalette.editorBg

                                Text {
                                    text: modelData.lineNum
                                    color: root.appPalette.textMuted
                                    font.family: root.editorFontFamily
                                    font.pixelSize: root.editorFontSize
                                    anchors.fill: parent
                                    anchors.rightMargin: 8
                                    horizontalAlignment: Text.AlignRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            // 2. The Code Overlay
                            Text {
                                id: stickyText
                                x: lineGutter.width + editor.leftPadding
                                width: parent.width - x
                                height: parent.height
                                verticalAlignment: Text.AlignVCenter
                                color: root.appPalette.textMain
                                font.family: root.editorFontFamily
                                font.pixelSize: root.editorFontSize
                                text: modelData.text
                                wrapMode: Text.NoWrap
                            }
                        }
                    }
                }
            }
        }
    }
}
