import QtQuick
import QtQuick.Window
import QtQuick.Layouts

Window {
    id: root
    width: 800; height: 320 // Slightly taller for candidate bar
    visible: true   // Start visible when manually launched
    color: "#1a1a2e"
    title: "Magic Keyboard"
    
    property bool shiftActive: false
    
    // Swipe state
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
            
            ctx.strokeStyle = "#88c0d0"; // Theme neutral
            ctx.lineWidth = 3;
            ctx.lineCap = "round";
            ctx.lineJoin = "round";
            
            ctx.beginPath();
            ctx.moveTo(root.currentPath[0].wx, root.currentPath[0].wy);
            for (var i = 1; i < root.currentPath.length; i++) {
                ctx.lineTo(root.currentPath[i].wx, root.currentPath[i].wy);
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
        
        onPositionChanged: (mouse) => {
            let key = root.getKeyAt(mouse.x, mouse.y)
            if (root.hoverKey !== key) {
                if (root.hoverKey) root.hoverKey.isHovered = false
                root.hoverKey = key
                if (root.hoverKey) root.hoverKey.isHovered = true
            }

            if (pressed) {
                let dx = mouse.x - root.startPos.x
                let dy = mouse.y - root.startPos.y
                let dist = Math.sqrt(dx*dx + dy*dy)
                let dt = Date.now() - root.startTime

                if (!root.isSwiping) {
                    if (dist > root.deadzone && dt > root.timeThreshold) {
                        root.isSwiping = true
                        if (root.activeKey) root.activeKey.isPressed = false
                        // Store both window (wx, wy) and layout (x, y) coordinates
                        let lp = root.mapToItem(keysContainer, mouse.x, mouse.y)
                        root.currentPath = [{wx: mouse.x, wy: mouse.y, x: lp.x, y: lp.y}]
                        trailCanvas.requestPaint()
                    }
                } else {
                    // Smoothing (EMA) on window space for visual consistency
                    // Path may be empty/null during rapid reinit or lifecycle transitions
                    let last = root.currentPath[root.currentPath.length - 1]
                    if (!last) return;
                    let nwx = root.smoothingAlpha * mouse.x + (1 - root.smoothingAlpha) * last.wx
                    let nwy = root.smoothingAlpha * mouse.y + (1 - root.smoothingAlpha) * last.wy
                    
                    let rdist = Math.sqrt(Math.pow(nwx - last.wx, 2) + Math.pow(nwy - last.wy, 2))
                    
                    if (rdist >= root.resampleDist) {
                        let nlp = root.mapToItem(keysContainer, nwx, nwy)
                        root.currentPath.push({wx: nwx, wy: nwy, x: nlp.x, y: nlp.y})
                        trailCanvas.requestPaint()
                    }
                }
            }
        }
        
        onPressed: (mouse) => {
            root.startPos = Qt.point(mouse.x, mouse.y)
            root.startTime = Date.now()
            root.isSwiping = false
            root.currentPath = []
            root.swipeCandidates = []
            
            let key = root.getKeyAt(mouse.x, mouse.y)
            root.activeKey = key
            if (root.activeKey) root.activeKey.isPressed = true
            
            fadeTimer.stop()
            trailCanvas.requestPaint()
        }
        
        onReleased: (mouse) => {
            if (root.isSwiping) {
                let duration = Date.now() - root.startTime
                console.log("SwipeUI points=" + root.currentPath.length + " resample=" + root.resampleDist + "px deadzone=" + root.deadzone.toFixed(1) + "px duration=" + duration + "ms sent=1")
                
                // Construct points for engine (layout space)
                let enginePath = []
                for (let p of root.currentPath) enginePath.push({x: p.x, y: p.y})
                bridge.sendSwipePath(enginePath)
                
                fadeTimer.start()
            } else {
                if (root.activeKey) {
                    root.activeKey.isPressed = false
                    if (root.activeKey.code === "shift") {
                        root.shiftActive = !root.shiftActive
                    } else if (root.activeKey.action !== "") {
                        bridge.sendAction(root.activeKey.action)
                    } else {
                        root.sendKey(root.activeKey.code)
                    }
                }
            }
            root.activeKey = null
            root.isSwiping = false
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
                visible: root.isSwiping || root.swipeCandidates.length === 0
                text: root.isSwiping ? "Swiping..." : (root.debugKeys.length > 0 ? root.debugKeys.join("-") : "Candidates (v0.2)")
                color: root.isSwiping || root.debugKeys.length > 0 ? "#00d2ff" : "#555"
                font.pixelSize: 14
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 8
                visible: !root.isSwiping && root.swipeCandidates.length > 0
                
                Repeater {
                    model: root.swipeCandidates
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.preferredWidth: 80
                        color: candidateMa.pressed ? "#4a4a8a" : "#2a2a4a"
                        radius: 4
                        border.color: "#3a3a5a"; border.width: 1
                        
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: "#eceff4"
                            font.pixelSize: 14
                        }
                        
                        MouseArea {
                            id: candidateMa
                            anchors.fill: parent
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
                KeyBtn { label: "⌫"; code: "backspace"; kw: 80; special: true }
            }
            
            // Row 2
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6
                Item { width: 15 }
                Repeater { model: root.row2; KeyBtn { label: modelData } }
                KeyBtn { label: "↵"; code: "enter"; kw: 80; special: true }
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
                
                KeyBtn { label: "space"; code: "space"; kw: 300; special: true }
                
                KeyBtn { label: "Cut"; action: "cut"; kw: 80 }
                KeyBtn { label: "Select All"; action: "selectall"; kw: 100 }
            }
        }
    }
}
