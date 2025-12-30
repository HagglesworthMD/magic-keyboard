import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import MagicKeyboard 1.0

Window {
    id: root
    width: 800; height: 320 // Slightly taller for candidate bar
    // Visibility is controlled by main.cpp -> bridge.state
    
    // Passive Mode: Translucent when just focused, Opaque when active
    opacity: bridge.state === KeyboardBridge.Passive ? 0.8 : 1.0
    
    Behavior on opacity { NumberAnimation { duration: 150 } }

    color: "#1a1a2e"
    title: "Magic Keyboard"
    
    property bool shiftActive: false
    
    // Swipe state ... (unchanged)
    property var currentPath: []
    property bool isSwiping: false
    property point startPos: Qt.point(0, 0)
    property real startTime: 0
    property var activeKey: null
    property var hoverKey: null
    
    property var debugKeys: []
    property var swipeCandidates: []
    
    Connections {
        target: bridge
        function onSwipeKeysReceived(keys) {
            root.debugKeys = keys
            debugTimer.restart()
            
            // Temporary highlight for keys in the sequence
            for (let i = 0; i < keys.length; i++) {
                let kid = keys[i]
                highlightKey(kid, i * 100)
            }
        }
        function onSwipeCandidatesReceived(candidates) {
            if (!candidates || candidates.length === 0) {
                root.swipeCandidates = []
            } else {
                root.swipeCandidates = candidates
            }
            console.log("SwipeUI candidates=" + root.swipeCandidates.length)
        }
    }

    // Safety Hatch: If UI accidentally grabs focus, Esc must hide it.
    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        onActivated: {
            console.log("Escape safety hatch (Shortcut) triggered")
            bridge.requestState(KeyboardBridge.Hidden, "escape_shortcut")
        }
    }

    // Redundant Safety Hatch: Raw key handler
    Item {
        anchors.fill: parent
        focus: true // Ensure we get key events if window has focus
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) {
                console.log("Escape safety hatch (Keys) triggered")
                bridge.requestState(KeyboardBridge.Hidden, "escape_key")
                event.accepted = true
            }
        }
    }

    function highlightKey(id, delay) {
        // This is costly O(N) but fine for debug overlay
        findAndHighlight(keysContainer, id, delay)
    }

    function findAndHighlight(container, id, delay) {
        for (let i = 0; i < container.children.length; i++) {
            let item = container.children[i]
            if (item.isKeyBtn && item.code === id) {
                let t = highlightTimerComponent.createObject(item, {delay: delay})
                return true
            }
            if (item.children && item.children.length > 0) {
                if (findAndHighlight(item, id, delay)) return true
            }
        }
        return false
    }

    Component {
        id: highlightTimerComponent
        Item {
            property int delay: 0
            Timer {
                interval: parent.delay; running: true
                onTriggered: {
                    parent.parent.isHovered = true
                    clearTimer.start()
                }
            }
            Timer {
                id: clearTimer
                interval: 800; onTriggered: parent.parent.isHovered = false
            }
        }
    }

    Timer {
        id: debugTimer
        interval: 2000
        onTriggered: root.debugKeys = []
    }

    // Config constants
    readonly property real deadzone: Math.max(10, 0.18 * 50)
    readonly property real timeThreshold: 35 // ms
    readonly property real smoothingAlpha: 0.4
    readonly property real resampleDist: 7 // px

    readonly property var row1: ["q","w","e","r","t","y","u","i","o","p"]
    readonly property var row2: ["a","s","d","f","g","h","j","k","l"]
    readonly property var row3: ["z","x","c","v","b","n","m"]
    
    function sendKey(key) {
        let text = key
        if (shiftActive && key.length === 1 && key.match(/[a-z]/)) {
            text = key.toUpperCase()
            shiftActive = false
        }
        bridge.sendKey(text)
    }

    function getKeyAt(x, y) {
        let item = keysContainer.childAt(x - keysContainer.x, y - keysContainer.y)
        while (item && !item.isKeyBtn) {
            // Traverse up layouts
            item = item.parent
        }
        return (item && item.isKeyBtn) ? item : null
    }
    
    component KeyBtn: Rectangle {
        id: keyRoot
        property string label: ""
        property string code: label.toLowerCase()
        property string action: ""  // New property for shortcuts
        property real kw: 60
        property bool special: false
        property bool isKeyBtn: true
        property bool isPressed: false
        property bool isHovered: false
        
        width: kw; height: 50; radius: 8
        color: isPressed ? "#4a4a8a" : (isHovered ? "#3a3a5a" : "#2a2a4a")
        border.color: isHovered ? "#88c0d0" : "#4a4a6a"
        border.width: isHovered ? 2 : 1
        
        Text {
            anchors.centerIn: parent
            text: root.shiftActive && !parent.special && parent.label.length === 1
                  ? parent.label.toUpperCase() 
                  : parent.label
            color: (parent.special || parent.action !== "") ? "#88c0d0" : "#eceff4"
            font.pixelSize: (parent.special || parent.action !== "") ? 14 : 18
            font.family: "sans-serif"
        }
    }
    
    // Visual Trail
    Canvas {
        id: trailCanvas
        anchors.fill: parent
        z: 100
        opacity: 0.6 // Increased transparency, no glow
        
        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            if (root.currentPath.length < 2) return;
            
            // Guard against malformed path entries
            var first = root.currentPath[0];
            if (!first || first.wx === undefined) return;
            
            ctx.strokeStyle = "#88c0d0"; // Theme neutral
            ctx.lineWidth = 3;
            ctx.lineCap = "round";
            ctx.lineJoin = "round";
            
            ctx.beginPath();
            ctx.moveTo(first.wx, first.wy);
            for (var i = 1; i < root.currentPath.length; i++) {
                var pt = root.currentPath[i];
                if (pt && pt.wx !== undefined) {
                    ctx.lineTo(pt.wx, pt.wy);
                }
            }
            ctx.stroke();
        }
        
        Timer {
            id: fadeTimer
            interval: 500
            onTriggered: {
                root.currentPath = []
                trailCanvas.requestPaint()
            }
        }
    }

    MouseArea {
        id: masterMouse
        anchors.fill: parent
        hoverEnabled: true
        
        // Intent Router: Input guarded by early returns in handlers
 

        // --- Gesture thresholds (Steam Deck friendly, minimal tuning knobs)
        property real tapMaxMovePx: 12      // <= this: treat as tap candidate
        property int  tapMaxMs: 220         // <= this: tap candidate
        property real swipeMinMovePx: 18    // >= this: swipe candidate
        property int  swipeMinPoints: 4     // >= this: swipe candidate
        property real sampleMinDistPx: 3    // ignore micro-jitter points

        property double _pressMs: 0
        property var _path: []
        property real _lastX: 0
        property real _lastY: 0

        function _nowMs() { return Date.now(); }

        function _dist2(ax, ay, bx, by) {
            var dx = ax - bx; var dy = ay - by;
            return dx*dx + dy*dy;
        }

        function _pathReset(x, y) {
            _pressMs = _nowMs();
            _path = [];
            _lastX = x; _lastY = y;
            _path.push([x, y]);
        }

        function _pathAdd(x, y) {
            // Dedup jitter / too-close samples
            if (_dist2(x, y, _lastX, _lastY) < (sampleMinDistPx * sampleMinDistPx))
                return false;
            _lastX = x; _lastY = y;
            _path.push([x, y]);
            return true;
        }

        function _totalMove2() {
            if (_path.length < 2) return 0;
            var a = _path[0];
            var b = _path[_path.length - 1];
            return _dist2(a[0], a[1], b[0], b[1]);
        }

        function _commitGesture() {
            var dt = _nowMs() - _pressMs;
            var move2 = _totalMove2();

            var isTap = (dt <= tapMaxMs) && (move2 <= (tapMaxMovePx * tapMaxMovePx));
            var isSwipe = (move2 >= (swipeMinMovePx * swipeMinMovePx)) && (_path.length >= swipeMinPoints);

            if (isTap) {
                return "tap";
            }

            if (isSwipe) {
                return "swipe";
            }

            // Neither: reject quietly (or minimal log)
            return "reject";
        }

        onPositionChanged: (mouse) => {
            if (bridge.state === KeyboardBridge.Hidden) return
            
            let key = root.getKeyAt(mouse.x, mouse.y)
            if (root.hoverKey !== key) {
                if (root.hoverKey) root.hoverKey.isHovered = false
                root.hoverKey = key
                if (root.hoverKey) root.hoverKey.isHovered = true
            }

            if (pressed) {
                if (_pathAdd(mouse.x, mouse.y)) {
                    // Update visual trail (UI state)
                    let lp = keysContainer.mapFromItem(masterMouse, mouse.x, mouse.y)
                    root.currentPath.push({wx: mouse.x, wy: mouse.y, x: lp.x, y: lp.y})
                    
                    // Live feedback: if we moved significantly, show swipe UI state
                    if (!root.isSwiping) {
                         if (_totalMove2() > (tapMaxMovePx * tapMaxMovePx)) {
                             root.isSwiping = true
                             if (root.activeKey) {
                                 root.activeKey.isPressed = false
                                 root.activeKey = null
                             }
                         }
                    }
                    trailCanvas.requestPaint()
                }
            }
        }
        
        onPressed: (mouse) => {
            if (bridge.state === KeyboardBridge.Hidden) return

            _pathReset(mouse.x, mouse.y)
            
            root.startPos = Qt.point(mouse.x, mouse.y)
            root.startTime = Date.now()
            root.isSwiping = false
            root.currentPath = []
            root.swipeCandidates = []
            
            // Initial point for visual trail
            let lp = keysContainer.mapFromItem(masterMouse, mouse.x, mouse.y)
            root.currentPath = [{wx: mouse.x, wy: mouse.y, x: lp.x, y: lp.y}]
            
            let key = root.getKeyAt(mouse.x, mouse.y)
            root.activeKey = key
            if (root.activeKey) root.activeKey.isPressed = true
            
            fadeTimer.stop()
            trailCanvas.requestPaint()
        }
        
        onReleased: (mouse) => {
            if (bridge.state === KeyboardBridge.Hidden) return

            var decision = _commitGesture()
            console.log("Gesture decision: " + decision + " pts=" + _path.length)

            if (decision === "swipe") {
                 var enginePath = []
                 for (var i = 0; i < _path.length; i++) {
                     var p = _path[i]
                     var lp = keysContainer.mapFromItem(masterMouse, p[0], p[1])
                     enginePath.push({x: lp.x, y: lp.y})
                 }
                 bridge.sendSwipePath(enginePath)
                 fadeTimer.start()
            } else if (decision === "tap") {
                 if (root.activeKey) {
                    console.log("Intent: Key Tap code=" + root.activeKey.code)
                     if (root.activeKey.code === "shift") {
                        root.shiftActive = !root.shiftActive
                    } else if (root.activeKey.action !== "") {
                        bridge.sendAction(root.activeKey.action)
                    } else {
                        root.sendKey(root.activeKey.code)
                    }
                 }
            } else {
                console.log("Gesture rejected")
            }
            
            if (root.activeKey) root.activeKey.isPressed = false
            root.activeKey = null
            root.isSwiping = false
            // Clear hover state on release to avoid stuck hover
            if (root.hoverKey) {
                root.hoverKey.isHovered = false
                root.hoverKey = null
            }
        }
        
        onCanceled: {
            if (root.activeKey) root.activeKey.isPressed = false
            root.activeKey = null
            root.isSwiping = false
            if (root.hoverKey) {
                root.hoverKey.isHovered = false
                root.hoverKey = null
            }
        }
    }
    
    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6
        
        // Candidate placeholder
        Rectangle {
            id: candidateBar
            Layout.fillWidth: true
            height: 40
            color: "#0f0f1a"
            radius: 6
            
            Text {
                anchors.centerIn: parent
                visible: root.isSwiping || (root.swipeCandidates.length === 0 && root.debugKeys.length === 0)
                text: root.isSwiping ? "Swiping..." : "Candidates (v0.2)"
                color: root.isSwiping ? "#00d2ff" : "#555"
                font.pixelSize: 14
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 8
                visible: !root.isSwiping && (root.swipeCandidates.length > 0 || root.debugKeys.length > 0)
                
                Repeater {
                    model: root.swipeCandidates.length > 0 ? root.swipeCandidates : root.debugKeys
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.preferredWidth: root.swipeCandidates.length > 0 ? 80 : 40
                        color: root.swipeCandidates.length > 0 
                               ? (candidateMa.pressed ? "#4a4a8a" : "#2a2a4a")
                               : "#1a1a2e"
                        radius: 4
                        border.color: root.swipeCandidates.length > 0 ? "#3a3a5a" : "#2a2a4a"
                        border.width: 1
                        
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: root.swipeCandidates.length > 0 ? "#eceff4" : "#88c0d0"
                            font.pixelSize: 14
                        }
                        
                        MouseArea {
                            id: candidateMa
                            anchors.fill: parent
                            enabled: root.swipeCandidates.length > 0
                            onClicked: {
                                bridge.commitCandidate(modelData)
                                root.swipeCandidates = []
                                root.debugKeys = []
                            }
                        }
                    }
                }
            }
        }
        
        ColumnLayout {
            id: keysContainer
            Layout.fillWidth: true
            spacing: 6
            
            // Row 1
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6
                Repeater { model: root.row1; KeyBtn { label: modelData } }
                KeyBtn { 
                    label: "⌫"; code: "backspace"; kw: 80; special: true
                    
                    // Independent MouseArea for press-and-hold repeat
                    MouseArea {
                        anchors.fill: parent
                        onPressed: {
                            parent.isPressed = true
                            bridge.backspaceHoldBegin()
                        }
                        onReleased: {
                            parent.isPressed = false
                            bridge.backspaceHoldEnd()
                        }
                        onCanceled: {
                            parent.isPressed = false
                            bridge.backspaceHoldEnd()
                        }
                        // Stop propagation so masterMouse doesn't trigger "tap" logic
                        propagateComposedEvents: false
                    }
                }
            }
            
            // Row 2
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6
                Item { width: 15 }
                Repeater { model: root.row2; KeyBtn { label: modelData } }
                KeyBtn { label: "↵"; action: "enter"; kw: 80; special: true }
            }
            
            // Row 3
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6
                
                KeyBtn { 
                    label: "⇧"; code: "shift"; kw: 80; special: true 
                    isPressed: root.shiftActive
                }
                
                Repeater { model: root.row3; KeyBtn { label: modelData } }
                KeyBtn { label: ","; code: "," }
                KeyBtn { label: "."; code: "." }
            }
            
            // Row 4: Space bar + Shortcuts
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6
                
                KeyBtn { label: "Copy"; action: "copy"; kw: 80 }
                KeyBtn { label: "Paste"; action: "paste"; kw: 80 }
                
                KeyBtn { label: "←"; action: "left"; kw: 60 }
                KeyBtn { label: "→"; action: "right"; kw: 60 }

                KeyBtn { label: "space"; code: "space"; kw: 200; special: true }
                
                KeyBtn { label: "Cut"; action: "cut"; kw: 80 }
                KeyBtn { label: "Select All"; action: "selectall"; kw: 100 }
            }
        }
    }

    // Manual Toggle Button (Top-Right)
    Rectangle {
        id: toggleButton
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 12
        width: 50; height: 26
        radius: 4
        color: toggleMouse.pressed ? "#444" : "#ff4757"
        border.color: "white"
        border.width: 1
        opacity: 0.9
        z: 999 

        Text {
            anchors.centerIn: parent
            text: bridge.state === KeyboardBridge.Hidden ? "Show" : "Hide"
            color: "white"
            font.pixelSize: 11
            font.bold: true
        }

        MouseArea {
            id: toggleMouse
            anchors.fill: parent
            onClicked: bridge.toggleVisibility()
        }
    }
}
