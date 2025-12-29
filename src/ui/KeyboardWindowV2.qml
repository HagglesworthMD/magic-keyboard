/**
 * Magic Keyboard UI v2.0
 * Pointer-Optimized QML Layout for Apple Magic Trackpad on Steam Deck
 * 
 * Design Principles:
 * - Pointer-first, not touch-first
 * - No tiny keys (min 56×48 px visual)
 * - Instant hover feedback
 * - Click targets > visual size (+4px padding)
 * - Generous 8px gaps
 */

import QtQuick
import QtQuick.Window
import QtQuick.Layouts

Window {
    id: keyboard
    
    // Steam Deck optimized dimensions
    width: 1200
    height: 320
    visible: false
    color: "#1a1a2e"
    title: "Magic Keyboard"
    
    // ═══════════════════════════════════════════════════════════════════
    // GEOMETRY MODEL
    // ═══════════════════════════════════════════════════════════════════
    
    // Scale factor for different displays
    readonly property real scaleFactor: {
        if (Screen.width <= 1280) return 1.0;       // Steam Deck native
        if (Screen.width <= 1920) return 1.15;      // 1080p
        return Math.min(1.4, Screen.width / 1280);  // Cap scaling
    }
    
    // Grid unit system
    readonly property real gridUnit: 64 * scaleFactor       // Base unit (1u)
    readonly property real keyHeight: 52 * scaleFactor      // Key visual height
    readonly property real keyGap: 8 * scaleFactor          // Gap between keys
    readonly property real hitPadding: 4                     // Extended hit zone
    readonly property real cornerRadius: 10 * scaleFactor   // Key corner radius
    readonly property real rowHeight: keyHeight + keyGap    // Total row height
    
    // ═══════════════════════════════════════════════════════════════════
    // STATE
    // ═══════════════════════════════════════════════════════════════════
    
    property bool shiftActive: false
    property bool isSwiping: false
    property point startPos: Qt.point(0, 0)
    property real startTime: 0
    property var currentPath: []
    property var activeKey: null
    property var hoverKey: null
    property var debugKeys: []
    property var swipeCandidates: []
    
    // Swipe configuration
    readonly property real deadzone: 12 * scaleFactor
    readonly property real timeThreshold: 40  // ms
    readonly property real smoothingAlpha: 0.35
    readonly property real resampleDist: 8 * scaleFactor
    
    // ═══════════════════════════════════════════════════════════════════
    // KEY COMPONENT
    // ═══════════════════════════════════════════════════════════════════
    
    component KeyBtn: Item {
        id: keyRoot
        
        property string label: ""
        property string code: label.toLowerCase()
        property real kw: 1.0  // Grid units
        property bool special: false
        property bool action: false
        property bool isKeyBtn: true
        property bool isPressed: false
        property bool isHovered: false
        
        // Size with hit padding
        width: kw * keyboard.gridUnit + keyboard.keyGap
        height: keyboard.keyHeight + keyboard.keyGap
        
        Rectangle {
            id: keyVisual
            
            // Center within hit zone
            anchors.centerIn: parent
            width: keyRoot.kw * keyboard.gridUnit
            height: keyboard.keyHeight
            radius: keyboard.cornerRadius
            
            // Visual states - NO animation on hover-in (instant feedback)
            color: {
                if (keyRoot.isPressed) return "#5a5a9a";
                if (keyRoot.isHovered) return "#3a3a6a";
                return "#2a2a4a";
            }
            
            border.width: keyRoot.isHovered ? 2 : 1
            border.color: keyRoot.isHovered ? "#88c0d0" : "#4a4a6a"
            
            // Subtle press animation only
            scale: keyRoot.isPressed ? 0.96 : 1.0
            Behavior on scale { NumberAnimation { duration: 40 } }
            
            // Minimal transition for aesthetics on hover-out only
            Behavior on color {
                enabled: !keyRoot.isHovered
                ColorAnimation { duration: 60 }
            }
            
            Text {
                anchors.centerIn: parent
                text: {
                    if (keyboard.shiftActive && !keyRoot.special && !keyRoot.action && keyRoot.label.length === 1)
                        return keyRoot.label.toUpperCase();
                    return keyRoot.label;
                }
                color: keyRoot.special || keyRoot.action ? "#88c0d0" : "#eceff4"
                font {
                    family: "Inter, Roboto, sans-serif"
                    pixelSize: (keyRoot.special || keyRoot.action) ? 14 * keyboard.scaleFactor : 20 * keyboard.scaleFactor
                    weight: Font.Medium
                }
            }
            
            // Hover glow effect
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.width: keyRoot.isHovered ? 1 : 0
                border.color: "#88c0d0"
                opacity: 0.3
                anchors.margins: -2
                visible: keyRoot.isHovered
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // KEY DATA
    // ═══════════════════════════════════════════════════════════════════
    
    readonly property var row1: ["q","w","e","r","t","y","u","i","o","p"]
    readonly property var row2: ["a","s","d","f","g","h","j","k","l"]
    readonly property var row3: ["z","x","c","v","b","n","m"]
    
    // ═══════════════════════════════════════════════════════════════════
    // FUNCTIONS
    // ═══════════════════════════════════════════════════════════════════
    
    function sendKey(key) {
        let text = key;
        if (shiftActive && key.length === 1 && key.match(/[a-z]/i)) {
            text = key.toUpperCase();
            shiftActive = false;
        }
        bridge.sendKey(text);
    }
    
    function getKeyAt(x, y) {
        // Map to keys container coordinates
        let mapped = keyboard.mapToItem(keysContainer, x, y);
        let item = keysContainer.childAt(mapped.x, mapped.y);
        
        // Traverse up to find KeyBtn
        while (item && !item.isKeyBtn) {
            item = item.parent;
        }
        return (item && item.isKeyBtn) ? item : null;
    }
    
    function commitKey(key) {
        if (key.code === "shift") {
            shiftActive = !shiftActive;
        } else {
            sendKey(key.code);
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // BRIDGE CONNECTIONS
    // ═══════════════════════════════════════════════════════════════════
    
    Connections {
        target: bridge
        
        function onSwipeKeysReceived(keys) {
            keyboard.debugKeys = keys;
            debugTimer.restart();
        }
        
        function onSwipeCandidatesReceived(candidates) {
            keyboard.swipeCandidates = candidates && candidates.length > 0 ? candidates : [];
            console.log("UI: candidates received =", keyboard.swipeCandidates.length);
        }
    }
    
    Timer {
        id: debugTimer
        interval: 3000
        onTriggered: keyboard.debugKeys = []
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // SWIPE TRAIL VISUALIZATION
    // ═══════════════════════════════════════════════════════════════════
    
    Canvas {
        id: trailCanvas
        anchors.fill: parent
        z: 100
        
        property real trailOpacity: 0.6
        
        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            if (keyboard.currentPath.length < 2) return;
            
            // Smooth trail with consistent stroke
            ctx.strokeStyle = Qt.rgba(0.533, 0.753, 0.816, trailOpacity);
            ctx.lineWidth = 4 * keyboard.scaleFactor;
            ctx.lineCap = "round";
            ctx.lineJoin = "round";
            
            ctx.beginPath();
            ctx.moveTo(keyboard.currentPath[0].wx, keyboard.currentPath[0].wy);
            for (var i = 1; i < keyboard.currentPath.length; i++) {
                ctx.lineTo(keyboard.currentPath[i].wx, keyboard.currentPath[i].wy);
            }
            ctx.stroke();
        }
        
        Timer {
            id: fadeTimer
            interval: 300
            onTriggered: {
                keyboard.currentPath = [];
                trailCanvas.requestPaint();
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // GLOBAL MOUSE HANDLER
    // ═══════════════════════════════════════════════════════════════════
    
    MouseArea {
        id: masterMouse
        anchors.fill: parent
        hoverEnabled: true
        
        onPositionChanged: (mouse) => {
            // Instant hover feedback
            let key = keyboard.getKeyAt(mouse.x, mouse.y);
            if (keyboard.hoverKey !== key) {
                if (keyboard.hoverKey) keyboard.hoverKey.isHovered = false;
                keyboard.hoverKey = key;
                if (keyboard.hoverKey) keyboard.hoverKey.isHovered = true;
            }
            
            // Swipe handling during press
            if (pressed) {
                let dx = mouse.x - keyboard.startPos.x;
                let dy = mouse.y - keyboard.startPos.y;
                let dist = Math.sqrt(dx*dx + dy*dy);
                let dt = Date.now() - keyboard.startTime;
                
                if (!keyboard.isSwiping) {
                    if (dist > keyboard.deadzone && dt > keyboard.timeThreshold) {
                        keyboard.isSwiping = true;
                        if (keyboard.activeKey) keyboard.activeKey.isPressed = false;
                        
                        let lp = keyboard.mapToItem(keysContainer, mouse.x, mouse.y);
                        keyboard.currentPath = [{wx: mouse.x, wy: mouse.y, x: lp.x, y: lp.y}];
                        trailCanvas.requestPaint();
                    }
                } else {
                    // Accumulate path with smoothing
                    let last = keyboard.currentPath[keyboard.currentPath.length - 1];
                    let nwx = keyboard.smoothingAlpha * mouse.x + (1 - keyboard.smoothingAlpha) * last.wx;
                    let nwy = keyboard.smoothingAlpha * mouse.y + (1 - keyboard.smoothingAlpha) * last.wy;
                    
                    let rdist = Math.sqrt(Math.pow(nwx - last.wx, 2) + Math.pow(nwy - last.wy, 2));
                    
                    if (rdist >= keyboard.resampleDist) {
                        let nlp = keyboard.mapToItem(keysContainer, nwx, nwy);
                        keyboard.currentPath.push({wx: nwx, wy: nwy, x: nlp.x, y: nlp.y});
                        trailCanvas.requestPaint();
                    }
                }
            }
        }
        
        onPressed: (mouse) => {
            keyboard.startPos = Qt.point(mouse.x, mouse.y);
            keyboard.startTime = Date.now();
            keyboard.isSwiping = false;
            keyboard.currentPath = [];
            keyboard.swipeCandidates = [];
            
            let key = keyboard.getKeyAt(mouse.x, mouse.y);
            keyboard.activeKey = key;
            if (keyboard.activeKey) keyboard.activeKey.isPressed = true;
            
            fadeTimer.stop();
            trailCanvas.requestPaint();
        }
        
        onReleased: (mouse) => {
            if (keyboard.isSwiping) {
                let duration = Date.now() - keyboard.startTime;
                console.log("Swipe: points=" + keyboard.currentPath.length + " duration=" + duration + "ms");
                
                // Send path to engine
                let enginePath = [];
                for (let p of keyboard.currentPath) {
                    enginePath.push({x: p.x, y: p.y});
                }
                bridge.sendSwipePath(enginePath);
                
                fadeTimer.start();
            } else {
                if (keyboard.activeKey) {
                    keyboard.activeKey.isPressed = false;
                    keyboard.commitKey(keyboard.activeKey);
                }
            }
            
            keyboard.activeKey = null;
            keyboard.isSwiping = false;
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // LAYOUT
    // ═══════════════════════════════════════════════════════════════════
    
    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        anchors.margins: 12 * keyboard.scaleFactor
        spacing: keyGap
        
        // ─────────────────────────────────────────────────────────────
        // CANDIDATE BAR
        // ─────────────────────────────────────────────────────────────
        
        Rectangle {
            id: candidateBar
            Layout.fillWidth: true
            height: 48 * keyboard.scaleFactor
            color: "#0f0f1a"
            radius: keyboard.cornerRadius
            border.color: "#2a2a4a"
            border.width: 1
            
            // Status text (when no candidates)
            Text {
                anchors.centerIn: parent
                visible: !keyboard.isSwiping && keyboard.swipeCandidates.length === 0
                text: keyboard.debugKeys.length > 0 
                    ? keyboard.debugKeys.join(" → ") 
                    : "Swipe to type"
                color: keyboard.debugKeys.length > 0 ? "#88c0d0" : "#555"
                font {
                    family: "Inter, Roboto, sans-serif"
                    pixelSize: 14 * keyboard.scaleFactor
                }
            }
            
            // Swiping indicator
            Text {
                anchors.centerIn: parent
                visible: keyboard.isSwiping
                text: "Swiping..."
                color: "#88c0d0"
                font {
                    family: "Inter, Roboto, sans-serif"
                    pixelSize: 16 * keyboard.scaleFactor
                    weight: Font.Medium
                }
            }
            
            // Candidate buttons
            RowLayout {
                anchors.fill: parent
                anchors.margins: 6 * keyboard.scaleFactor
                spacing: 10 * keyboard.scaleFactor
                visible: !keyboard.isSwiping && keyboard.swipeCandidates.length > 0
                
                Repeater {
                    model: keyboard.swipeCandidates
                    
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.preferredWidth: Math.max(80 * keyboard.scaleFactor, candText.implicitWidth + 24)
                        color: candMa.pressed ? "#4a4a8a" : (candMa.containsMouse ? "#3a3a5a" : "#2a2a4a")
                        radius: 6 * keyboard.scaleFactor
                        border.color: candMa.containsMouse ? "#88c0d0" : "#3a3a5a"
                        border.width: candMa.containsMouse ? 2 : 1
                        
                        Text {
                            id: candText
                            anchors.centerIn: parent
                            text: modelData
                            color: "#eceff4"
                            font {
                                family: "Inter, Roboto, sans-serif"
                                pixelSize: 16 * keyboard.scaleFactor
                            }
                        }
                        
                        MouseArea {
                            id: candMa
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                bridge.commitCandidate(modelData);
                                keyboard.swipeCandidates = [];
                                keyboard.debugKeys = [];
                            }
                        }
                    }
                }
                
                // Spacer
                Item { Layout.fillWidth: true }
            }
        }
        
        // ─────────────────────────────────────────────────────────────
        // KEYBOARD ROWS
        // ─────────────────────────────────────────────────────────────
        
        ColumnLayout {
            id: keysContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0  // Gaps handled by key items
            
            // Row 1: QWERTY + Backspace
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 0
                
                Repeater {
                    model: keyboard.row1
                    KeyBtn { label: modelData; kw: 1.0 }
                }
                
                KeyBtn { label: "⌫"; code: "backspace"; kw: 1.5; special: true }
            }
            
            // Row 2: Home row + Enter
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 0
                
                // Half-unit offset for stagger
                Item { width: keyboard.gridUnit * 0.25 }
                
                Repeater {
                    model: keyboard.row2
                    KeyBtn { label: modelData; kw: 1.0 }
                }
                
                KeyBtn { label: "↵"; code: "enter"; kw: 1.5; special: true }
            }
            
            // Row 3: Shift + bottom row + punctuation
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 0
                
                KeyBtn { 
                    label: "⇧"
                    code: "shift"
                    kw: 1.5
                    special: true
                    isPressed: keyboard.shiftActive
                }
                
                Repeater {
                    model: keyboard.row3
                    KeyBtn { label: modelData; kw: 1.0 }
                }
                
                KeyBtn { label: ","; code: ","; kw: 1.0 }
                KeyBtn { label: "."; code: "."; kw: 1.0 }
            }
            
            // Row 4: Action row
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 0
                
                KeyBtn { label: "Copy"; code: "copy"; kw: 1.25; action: true }
                KeyBtn { label: "Paste"; code: "paste"; kw: 1.25; action: true }
                KeyBtn { label: ""; code: "space"; kw: 5.0; special: true }
                KeyBtn { label: "Cut"; code: "cut"; kw: 1.25; action: true }
                KeyBtn { label: "SelAll"; code: "selectall"; kw: 1.25; action: true }
            }
        }
    }
}
