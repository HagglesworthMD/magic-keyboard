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

    // Window dimensions computed from grid system
    // These will update automatically when scaleFactor changes
    width: 720 * scaleFactor
    height: 310 * scaleFactor
    visible: false
    color: "transparent"  // Allow opacity control
    title: "Magic Keyboard"

    // Opacity from settings
    opacity: bridge.windowOpacity
    
    // ═══════════════════════════════════════════════════════════════════
    // GEOMETRY MODEL
    // ═══════════════════════════════════════════════════════════════════
    
    // Scale factor combines DPI scaling with user preference
    // Window size is controlled by baseWidth/Height * windowScale
    // Internal key dimensions scale proportionally
    readonly property real scaleFactor: {
        // Base DPI factor
        let dpi = 1.0;
        if (Screen.width <= 1280) dpi = 1.0;       // Steam Deck native
        else if (Screen.width <= 1920) dpi = 1.15; // 1080p
        else dpi = Math.min(1.4, Screen.width / 1280);
        
        // Apply user scale preference
        return dpi * bridge.windowScale;
    }
    
    // Grid unit system - scaled to fill window proportionally
    // These are the base values at scale 1.0, scaled by scaleFactor
    readonly property real gridUnit: 52 * scaleFactor       // Base unit (1u)
    readonly property real keyHeight: 42 * scaleFactor      // Key visual height
    readonly property real keyGap: 6 * scaleFactor          // Gap between keys
    readonly property real hitPadding: 4                     // Extended hit zone
    readonly property real cornerRadius: 8 * scaleFactor    // Key corner radius
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
    
    // Layer state: 0=ABC, 1=123, 2=#+=
    property int currentLayer: 0
    
    // Swipe configuration (from settings)
    // Note: Increased thresholds to prevent trackpad tap jitter from triggering swipe
    readonly property real deadzone: Math.max(35, bridge.swipeThreshold * scaleFactor)  // Min 35px
    readonly property real timeThreshold: 80  // ms (increased from 40)
    readonly property real smoothingAlpha: bridge.pathSmoothing
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
                if (keyRoot.isPressed) return bridge.themeKeyPressed;
                if (keyRoot.isHovered) return bridge.themeKeyHover;
                return bridge.themeKeyBackground;
            }

            border.width: keyRoot.isHovered ? 2 : 1
            border.color: keyRoot.isHovered ? bridge.themeKeyBorderHover : bridge.themeKeyBorder

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
                color: keyRoot.special || keyRoot.action ? bridge.themeSpecialKeyText : bridge.themeKeyText
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
                border.color: bridge.themeKeyBorderHover
                opacity: 0.3
                anchors.margins: -2
                visible: keyRoot.isHovered
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // KEY DATA - MULTI-LAYER SUPPORT
    // ═══════════════════════════════════════════════════════════════════
    
    // Layer 0: ABC (QWERTY letters)
    readonly property var abcLayer: {
        "row1": ["q","w","e","r","t","y","u","i","o","p"],
        "row2": ["a","s","d","f","g","h","j","k","l"],
        "row3": ["z","x","c","v","b","n","m"],
        "row3Right": [",", "."],
        "layerKey": "123",
        "nextLayer": 1
    }
    
    // Layer 1: 123 (Numbers + primary symbols)
    readonly property var numLayer: {
        "row1": ["1","2","3","4","5","6","7","8","9","0"],
        "row2": ["@","#","$","%","&","-","+","(",")"],
        "row3": ["!","\"","'",":",";","/","?"],
        "row3Right": ["_", "="],
        "layerKey": "#+=",
        "nextLayer": 2
    }
    
    // Layer 2: #+= (Extended symbols)
    readonly property var symLayer: {
        "row1": ["~","`","|","•","√","π","÷","×","{","}"],
        "row2": ["€","£","¥","^","°","=","[","]","<",">"],
        "row3": ["\\","©","®","™","¶","§","«","»"],
        "row3Right": ["…", "•"],
        "layerKey": "123",
        "nextLayer": 1
    }
    
    // Current layer data accessor
    readonly property var currentLayerData: {
        if (currentLayer === 0) return abcLayer;
        if (currentLayer === 1) return numLayer;
        return symLayer;
    }
    
    // Legacy aliases for backward compatibility with swipe logic
    readonly property var row1: currentLayerData.row1
    readonly property var row2: currentLayerData.row2
    readonly property var row3: currentLayerData.row3
    
    // Layer switching functions
    function cycleToNextLayer() {
        currentLayer = currentLayerData.nextLayer;
    }
    
    function switchToABC() {
        currentLayer = 0;
    }
    
    function switchToLayer(layerIndex) {
        currentLayer = layerIndex;
    }
    
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
        // Recursive search through children to find KeyBtn at position
        function findKeyRecursive(parent, px, py) {
            if (!parent || !parent.children) return null;
            
            for (let i = 0; i < parent.children.length; i++) {
                let child = parent.children[i];
                
                // Check if this is a KeyBtn
                if (child.isKeyBtn) {
                    // Map point to child coordinates
                    let localPt = child.mapFromItem(masterMouse, px, py);
                    if (localPt.x >= 0 && localPt.x < child.width &&
                        localPt.y >= 0 && localPt.y < child.height) {
                        return child;
                    }
                }
                
                // Recursively search children
                let found = findKeyRecursive(child, px, py);
                if (found) return found;
            }
            return null;
        }
        
        return findKeyRecursive(keysContainer, x, y);
    }
    
    function commitKey(key) {
        console.log("commitKey: code=" + key.code + " action=" + key.action);
        
        // Layer switching
        if (key.code === "layer_123") {
            currentLayer = 1;
            return;
        } else if (key.code === "layer_abc") {
            currentLayer = 0;
            return;
        } else if (key.code === "layer_cycle") {
            cycleToNextLayer();
            return;
        } else if (key.code === "globe") {
            // Future: emoji picker
            return;
        }
        
        if (key.code === "shift") {
            shiftActive = !shiftActive;
        } else if (key.code === "paste") {
            // Paste uses direct clipboard access (more reliable than Ctrl+V forwarding)
            console.log("Using pasteFromClipboard");
            bridge.pasteFromClipboard();
        } else if (key.action) {
            // Other action keys route through sendAction (copy, cut, selectall)
            console.log("Sending action: " + key.code);
            bridge.sendAction(key.code);
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

            // Smooth trail with consistent stroke using theme color
            var trailColor = bridge.themeSwipeTrail;
            // Parse hex color and apply opacity
            var r = parseInt(trailColor.slice(1, 3), 16) / 255;
            var g = parseInt(trailColor.slice(3, 5), 16) / 255;
            var b = parseInt(trailColor.slice(5, 7), 16) / 255;
            ctx.strokeStyle = Qt.rgba(r, g, b, trailOpacity);
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
                        
                        let lp = keysContainer.mapFromItem(masterMouse, mouse.x, mouse.y);
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
                        let nlp = keysContainer.mapFromItem(masterMouse, nwx, nwy);
                        keyboard.currentPath.push({wx: nwx, wy: nwy, x: nlp.x, y: nlp.y});
                        trailCanvas.requestPaint();
                    }
                }
            }
        }
        
        onPressed: (mouse) => {
            console.log("onPressed: x=" + mouse.x + " y=" + mouse.y);
            keyboard.startPos = Qt.point(mouse.x, mouse.y);
            keyboard.startTime = Date.now();
            keyboard.isSwiping = false;
            keyboard.currentPath = [];
            keyboard.swipeCandidates = [];
            
            let key = keyboard.getKeyAt(mouse.x, mouse.y);
            console.log("onPressed: key=" + (key ? key.code : "null"));
            keyboard.activeKey = key;
            if (keyboard.activeKey) keyboard.activeKey.isPressed = true;
            
            fadeTimer.stop();
            trailCanvas.requestPaint();
        }
        
        onClicked: (mouse) => {
            console.log("onClicked: x=" + mouse.x + " y=" + mouse.y);
            // If we get here and have an activeKey, commit it
            if (keyboard.activeKey && !keyboard.isSwiping) {
                console.log("onClicked: Committing key: " + keyboard.activeKey.code);
                keyboard.commitKey(keyboard.activeKey);
            }
        }
        
        onReleased: (mouse) => {
            console.log("onReleased: isSwiping=" + keyboard.isSwiping + " activeKey=" + (keyboard.activeKey ? keyboard.activeKey.code : "null"));
            if (keyboard.isSwiping) {
                let duration = Date.now() - keyboard.startTime;
                console.log("Swipe complete: points=" + keyboard.currentPath.length + " duration=" + duration + "ms");
                
                // Send normalized path to engine (divide by scaleFactor to match engine's base model)
                let enginePath = [];
                for (let p of keyboard.currentPath) {
                    enginePath.push({
                        x: p.x / keyboard.scaleFactor, 
                        y: p.y / keyboard.scaleFactor
                    });
                }
                
                
                // Send keys encountered during swipe for fallback/reinforcement
                bridge.sendSwipePath(enginePath);
                
                fadeTimer.start();
            } else {
                if (keyboard.activeKey) {
                    console.log("Committing key: " + keyboard.activeKey.code);
                    keyboard.activeKey.isPressed = false;
                    keyboard.commitKey(keyboard.activeKey);
                } else {
                    console.log("No activeKey to commit!");
                }
            }
            
            keyboard.activeKey = null;
            keyboard.isSwiping = false;
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // BACKGROUND (for transparency support)
    // ═══════════════════════════════════════════════════════════════════

    Rectangle {
        id: keyboardBackground
        anchors.fill: parent
        color: bridge.themeBackground
        radius: 12 * keyboard.scaleFactor

        // Subtle border for visibility at low opacity
        border.color: bridge.themeKeyBorder
        border.width: 1
    }

    // ═══════════════════════════════════════════════════════════════════
    // RESIZE HANDLES
    // ═══════════════════════════════════════════════════════════════════

    // Corner resize handle (bottom-right)
    Rectangle {
        id: resizeHandle
        width: 24 * keyboard.scaleFactor
        height: 24 * keyboard.scaleFactor
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 4
        color: resizeArea.containsMouse ? "#5a5a9a" : "transparent"
        radius: 4

        // Resize indicator lines
        Canvas {
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                ctx.strokeStyle = "#88c0d0";
                ctx.lineWidth = 2;
                ctx.lineCap = "round";

                // Draw diagonal lines
                for (var i = 0; i < 3; i++) {
                    var offset = 6 + i * 5;
                    ctx.beginPath();
                    ctx.moveTo(width - 4, offset);
                    ctx.lineTo(offset, height - 4);
                    ctx.stroke();
                }
            }
        }

        MouseArea {
            id: resizeArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeFDiagCursor

            property point startPos
            property real startWidth
            property real startHeight

            onPressed: (mouse) => {
                startPos = Qt.point(mouse.x, mouse.y);
                startWidth = keyboard.width;
                startHeight = keyboard.height;
            }

            onPositionChanged: (mouse) => {
                if (!pressed) return;

                var dx = mouse.x - startPos.x;
                var dy = mouse.y - startPos.y;

                // Maintain aspect ratio
                var newScale = Math.max(0.5, Math.min(2.0,
                    bridge.windowScale + (dx + dy) / 400));

                bridge.updateSetting("window_scale", newScale);
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // DRAG HANDLE (for moving window)
    // ═══════════════════════════════════════════════════════════════════

    Rectangle {
        id: dragHandle
        height: 24 * keyboard.scaleFactor
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 50 * keyboard.scaleFactor  // Leave room for settings button
        anchors.rightMargin: 50 * keyboard.scaleFactor // Leave room for close button
        anchors.topMargin: 4
        color: dragArea.containsMouse ? Qt.rgba(1, 1, 1, 0.1) : "transparent"
        radius: 6 * keyboard.scaleFactor
        z: 150

        // Grip indicator (horizontal lines)
        Column {
            anchors.centerIn: parent
            spacing: 3 * keyboard.scaleFactor
            
            Repeater {
                model: 3
                Rectangle {
                    width: 40 * keyboard.scaleFactor
                    height: 2 * keyboard.scaleFactor
                    radius: 1
                    color: dragArea.containsMouse ? "#88c0d0" : "#4a4a6a"
                    opacity: dragArea.containsMouse ? 0.8 : 0.4
                }
            }
        }

        MouseArea {
            id: dragArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeAllCursor

            property point lastPos

            onPressed: (mouse) => {
                lastPos = Qt.point(mouse.x, mouse.y);
            }

            onPositionChanged: (mouse) => {
                if (!pressed) return;
                
                var dx = mouse.x - lastPos.x;
                var dy = mouse.y - lastPos.y;
                
                keyboard.x += dx;
                keyboard.y += dy;
            }
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
            color: bridge.themeCandidateBar
            radius: keyboard.cornerRadius
            border.color: bridge.themeKeyBorder
            border.width: 1
            
            // Status text (when no candidates)
            Text {
                anchors.centerIn: parent
                visible: !keyboard.isSwiping && keyboard.swipeCandidates.length === 0
                text: keyboard.debugKeys.length > 0
                    ? keyboard.debugKeys.join(" → ")
                    : "Swipe to type"
                color: keyboard.debugKeys.length > 0 ? bridge.themeSpecialKeyText : "#555"
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
                color: bridge.themeSpecialKeyText
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
                        id: candRect
                        Layout.fillHeight: true
                        Layout.preferredWidth: Math.max(80 * keyboard.scaleFactor, candText.implicitWidth + 28)

                        // First candidate gets special highlight
                        property bool isTopPick: index === 0

                        color: candMa.pressed ? bridge.themeKeyPressed :
                               (candMa.containsMouse ? bridge.themeKeyHover :
                               (isTopPick ? bridge.themeKeyHover : bridge.themeKeyBackground))
                        radius: 8 * keyboard.scaleFactor
                        border.color: candMa.containsMouse || isTopPick ? bridge.themeKeyBorderHover : bridge.themeKeyBorder
                        border.width: candMa.containsMouse ? 2 : (isTopPick ? 2 : 1)

                        // Subtle scale animation on press
                        scale: candMa.pressed ? 0.95 : 1.0
                        Behavior on scale { NumberAnimation { duration: 50 } }

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 4

                            // Top pick indicator
                            Text {
                                visible: candRect.isTopPick
                                text: "★"
                                color: bridge.themeSpecialKeyText
                                font.pixelSize: 10 * keyboard.scaleFactor
                            }

                            Text {
                                id: candText
                                text: modelData
                                color: candRect.isTopPick ? bridge.themeSpecialKeyText : bridge.themeKeyText
                                font {
                                    family: "Inter, Roboto, sans-serif"
                                    pixelSize: 16 * keyboard.scaleFactor
                                    weight: candRect.isTopPick ? Font.Bold : Font.Normal
                                }
                            }
                        }

                        MouseArea {
                            id: candMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
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
            
            // Row 1: Top row (letters or numbers/symbols) + Backspace
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 0
                
                Repeater {
                    model: keyboard.currentLayerData.row1
                    KeyBtn { label: modelData; kw: 1.0 }
                }
                
                KeyBtn { label: "⌫"; code: "backspace"; kw: 1.5; special: true }
            }
            
            // Row 2: Middle row + Enter
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 0
                
                // Half-unit offset for stagger (ABC layer only)
                Item { width: keyboard.currentLayer === 0 ? keyboard.gridUnit * 0.25 : 0 }
                
                Repeater {
                    model: keyboard.currentLayerData.row2
                    KeyBtn { label: modelData; kw: 1.0 }
                }
                
                KeyBtn { label: "↵"; code: "enter"; kw: 1.5; special: true }
            }
            
            // Row 3: Bottom letter row with shift/layer key + punctuation
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 0
                
                // Left side: Shift (ABC) or Layer switch (#+=)
                KeyBtn { 
                    label: keyboard.currentLayer === 0 ? "⇧" : keyboard.currentLayerData.layerKey
                    code: keyboard.currentLayer === 0 ? "shift" : "layer_cycle"
                    kw: 1.5
                    special: true
                    isPressed: keyboard.currentLayer === 0 && keyboard.shiftActive
                }
                
                Repeater {
                    model: keyboard.currentLayerData.row3
                    KeyBtn { label: modelData; kw: 1.0 }
                }
                
                // Right side punctuation from layer data
                Repeater {
                    model: keyboard.currentLayerData.row3Right
                    KeyBtn { label: modelData; code: modelData; kw: 1.0 }
                }
            }
            
            // Row 4: Layer switch + Space + common punctuation
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 0

                // Layer switch key (123/ABC)
                KeyBtn { 
                    label: keyboard.currentLayer === 0 ? "123" : "ABC"
                    code: keyboard.currentLayer === 0 ? "layer_123" : "layer_abc"
                    kw: 1.2
                    special: true
                }
                
                // Globe/emoji placeholder (future)
                KeyBtn { label: "🌐"; code: "globe"; kw: 0.8; special: true }
                
                // Space bar
                KeyBtn { label: ""; code: "space"; kw: 5.0; special: true }
                
                // Common punctuation that stays across layers
                KeyBtn { label: "'"; code: "'"; kw: 0.8 }
                KeyBtn { label: "?"; code: "?"; kw: 0.8 }
                
                // Arrow keys
                KeyBtn { label: "←"; code: "left"; kw: 0.9; special: true }
                KeyBtn { label: "→"; code: "right"; kw: 0.9; special: true }
            }
            
            // Row 5: Action row
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 0

                KeyBtn { label: "Tab"; code: "tab"; kw: 1.0; special: true }
                KeyBtn { label: "Copy"; code: "copy"; kw: 1.1; action: true }
                KeyBtn { label: "Paste"; code: "paste"; kw: 1.1; action: true }
                KeyBtn { label: "Cut"; code: "cut"; kw: 1.0; action: true }
                KeyBtn { label: "Sel"; code: "selectall"; kw: 1.0; action: true }
                
                // Filler
                Item { width: keyboard.gridUnit * 2.5 }
                
                KeyBtn { label: "↑"; code: "up"; kw: 0.8; special: true }
                KeyBtn { label: "↓"; code: "down"; kw: 0.8; special: true }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // SETTINGS BUTTON (top-left corner)
    // ═══════════════════════════════════════════════════════════════════

    Rectangle {
        id: settingsButton
        width: 32 * keyboard.scaleFactor
        height: 32 * keyboard.scaleFactor
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        color: settingsButtonArea.containsMouse ? "#3a3a6a" : "#2a2a4a"
        radius: 6
        border.color: settingsButtonArea.containsMouse ? "#88c0d0" : "#4a4a6a"
        border.width: 1
        z: 200

        Text {
            anchors.centerIn: parent
            text: "⚙"
            color: "#88c0d0"
            font.pixelSize: 18 * keyboard.scaleFactor
        }

        MouseArea {
            id: settingsButtonArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: bridge.settingsVisible = !bridge.settingsVisible
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // HIDE BUTTON (top-right corner)
    // ═══════════════════════════════════════════════════════════════════

    Rectangle {
        id: hideButton
        width: 32 * keyboard.scaleFactor
        height: 32 * keyboard.scaleFactor
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        color: hideButtonArea.containsMouse ? "#5a3a3a" : "#3a2a2a"
        radius: 6
        border.color: hideButtonArea.containsMouse ? "#d08888" : "#6a4a4a"
        border.width: 1
        z: 300

        Text {
            anchors.centerIn: parent
            text: "✕"
            color: hideButtonArea.containsMouse ? "#ff9999" : "#d08888"
            font.pixelSize: 16 * keyboard.scaleFactor
            font.bold: true
        }

        MouseArea {
            id: hideButtonArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: bridge.toggleVisibility()
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // SETTINGS PANEL
    // ═══════════════════════════════════════════════════════════════════

    Rectangle {
        id: settingsPanel
        visible: bridge.settingsVisible
        width: 300 * keyboard.scaleFactor
        height: parent.height - 20
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        color: "#0f0f1a"
        radius: 12 * keyboard.scaleFactor
        border.color: "#3a3a5a"
        border.width: 1
        z: 150

        // Prevent clicks from passing through
        MouseArea {
            anchors.fill: parent
            onClicked: {} // Absorb click
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16 * keyboard.scaleFactor
            spacing: 12 * keyboard.scaleFactor

            // Header
            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "Settings"
                    color: "#eceff4"
                    font {
                        family: "Inter, Roboto, sans-serif"
                        pixelSize: 18 * keyboard.scaleFactor
                        weight: Font.Bold
                    }
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    width: 28 * keyboard.scaleFactor
                    height: 28 * keyboard.scaleFactor
                    color: closeArea.containsMouse ? "#5a5a9a" : "transparent"
                    radius: 4

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: "#88c0d0"
                        font.pixelSize: 16 * keyboard.scaleFactor
                    }

                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: bridge.settingsVisible = false
                    }
                }
            }

            // Divider
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#3a3a5a"
            }

            // Opacity slider
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Text {
                        text: "Opacity"
                        color: "#88c0d0"
                        font.pixelSize: 14 * keyboard.scaleFactor
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: Math.round(bridge.windowOpacity * 100) + "%"
                        color: "#eceff4"
                        font.pixelSize: 14 * keyboard.scaleFactor
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 24 * keyboard.scaleFactor
                    color: "#2a2a4a"
                    radius: 4

                    Rectangle {
                        width: parent.width * (bridge.windowOpacity - 0.3) / 0.7
                        height: parent.height
                        color: "#5a5a9a"
                        radius: 4
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: (mouse) => {
                            var value = 0.3 + (mouse.x / width) * 0.7;
                            value = Math.max(0.3, Math.min(1.0, value));
                            bridge.updateSetting("window_opacity", value);
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed) return;
                            var value = 0.3 + (mouse.x / width) * 0.7;
                            value = Math.max(0.3, Math.min(1.0, value));
                            bridge.updateSetting("window_opacity", value);
                        }
                    }
                }
            }

            // Scale slider
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Text {
                        text: "Size"
                        color: "#88c0d0"
                        font.pixelSize: 14 * keyboard.scaleFactor
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: Math.round(bridge.windowScale * 100) + "%"
                        color: "#eceff4"
                        font.pixelSize: 14 * keyboard.scaleFactor
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 24 * keyboard.scaleFactor
                    color: "#2a2a4a"
                    radius: 4

                    Rectangle {
                        width: parent.width * (bridge.windowScale - 0.5) / 1.5
                        height: parent.height
                        color: "#5a5a9a"
                        radius: 4
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: (mouse) => {
                            var value = 0.5 + (mouse.x / width) * 1.5;
                            value = Math.max(0.5, Math.min(2.0, value));
                            bridge.updateSetting("window_scale", value);
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed) return;
                            var value = 0.5 + (mouse.x / width) * 1.5;
                            value = Math.max(0.5, Math.min(2.0, value));
                            bridge.updateSetting("window_scale", value);
                        }
                    }
                }
            }

            // Swipe sensitivity slider
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Text {
                        text: "Swipe Sensitivity"
                        color: "#88c0d0"
                        font.pixelSize: 14 * keyboard.scaleFactor
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: Math.round(bridge.swipeThreshold) + "px"
                        color: "#eceff4"
                        font.pixelSize: 14 * keyboard.scaleFactor
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 24 * keyboard.scaleFactor
                    color: "#2a2a4a"
                    radius: 4

                    Rectangle {
                        // Inverted: lower threshold = more sensitive
                        width: parent.width * (1 - (bridge.swipeThreshold - 5) / 45)
                        height: parent.height
                        color: "#5a5a9a"
                        radius: 4
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: (mouse) => {
                            // Inverted slider: right = more sensitive (lower threshold)
                            var value = 50 - (mouse.x / width) * 45;
                            value = Math.max(5, Math.min(50, value));
                            bridge.updateSetting("swipe_threshold_px", value);
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed) return;
                            var value = 50 - (mouse.x / width) * 45;
                            value = Math.max(5, Math.min(50, value));
                            bridge.updateSetting("swipe_threshold_px", value);
                        }
                    }
                }
            }

            // Path smoothing slider
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Text {
                        text: "Path Smoothing"
                        color: "#88c0d0"
                        font.pixelSize: 14 * keyboard.scaleFactor
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: Math.round(bridge.pathSmoothing * 100) + "%"
                        color: "#eceff4"
                        font.pixelSize: 14 * keyboard.scaleFactor
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 24 * keyboard.scaleFactor
                    color: "#2a2a4a"
                    radius: 4

                    Rectangle {
                        width: parent.width * bridge.pathSmoothing
                        height: parent.height
                        color: "#5a5a9a"
                        radius: 4
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: (mouse) => {
                            var value = mouse.x / width;
                            value = Math.max(0, Math.min(1.0, value));
                            bridge.updateSetting("path_smoothing", value);
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed) return;
                            var value = mouse.x / width;
                            value = Math.max(0, Math.min(1.0, value));
                            bridge.updateSetting("path_smoothing", value);
                        }
                    }
                }
            }

            // Theme selector
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "Theme"
                    color: "#88c0d0"
                    font.pixelSize: 14 * keyboard.scaleFactor
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6 * keyboard.scaleFactor

                    Repeater {
                        model: bridge.availableThemes

                        Rectangle {
                            Layout.fillWidth: true
                            height: 32 * keyboard.scaleFactor
                            color: themeBtnArea.containsMouse ? "#3a3a5a" : (bridge.activeTheme === modelData || (bridge.activeTheme === "" && modelData === "default") ? "#4a4a7a" : "#2a2a4a")
                            radius: 6 * keyboard.scaleFactor
                            border.color: (bridge.activeTheme === modelData || (bridge.activeTheme === "" && modelData === "default")) ? "#88c0d0" : "#3a3a5a"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                                color: "#eceff4"
                                font.pixelSize: 11 * keyboard.scaleFactor
                            }

                            MouseArea {
                                id: themeBtnArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: bridge.setActiveTheme(modelData)
                            }
                        }
                    }
                }
            }

            // Snap-to-caret selector
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "Position Mode"
                    color: "#88c0d0"
                    font.pixelSize: 14 * keyboard.scaleFactor
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6 * keyboard.scaleFactor

                    Repeater {
                        model: ["Fixed", "Below", "Above", "Smart"]

                        Rectangle {
                            Layout.fillWidth: true
                            height: 28 * keyboard.scaleFactor
                            color: snapBtnArea.containsMouse ? "#3a3a5a" : (bridge.snapToCaretMode === index ? "#4a4a7a" : "#2a2a4a")
                            radius: 6 * keyboard.scaleFactor
                            border.color: bridge.snapToCaretMode === index ? "#88c0d0" : "#3a3a5a"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: "#eceff4"
                                font.pixelSize: 10 * keyboard.scaleFactor
                            }

                            MouseArea {
                                id: snapBtnArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: bridge.updateSetting("snap_to_caret_mode", index)
                            }
                        }
                    }
                }
            }

            // Spacer
            Item { Layout.fillHeight: true }

            // Version info
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Magic Keyboard v0.2"
                color: "#555"
                font.pixelSize: 11 * keyboard.scaleFactor
            }
        }
    }
}
