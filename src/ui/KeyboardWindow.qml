import QtQuick
import QtQuick.Window
import QtQuick.Layouts

Window {
    id: root
    width: 800; height: 280
    visible: false  // Start hidden, controlled by bridge
    color: "#1a1a2e"
    title: "Magic Keyboard"
    
    property bool shiftActive: false
    
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
    
    component KeyBtn: Rectangle {
        property string label: ""
        property string code: label.toLowerCase()
        property real kw: 60
        property bool special: false
        
        width: kw; height: 50; radius: 8
        color: ma.pressed ? "#3a3a5a" : "#2a2a4a"
        border.color: "#4a4a6a"; border.width: 1
        
        Text {
            anchors.centerIn: parent
            text: root.shiftActive && !parent.special && parent.label.length === 1
                  ? parent.label.toUpperCase() 
                  : parent.label
            color: parent.special ? "#88c0d0" : "#eceff4"
            font.pixelSize: parent.special ? 14 : 18
            font.family: "sans-serif"
        }
        
        MouseArea {
            id: ma
            anchors.fill: parent
            onClicked: {
                if (!parent.special) {
                    root.sendKey(parent.code)
                } else if (parent.code !== "shift") {
                    root.sendKey(parent.code)
                }
            }
        }
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6
        
        // Candidate placeholder
        Rectangle {
            Layout.fillWidth: true
            height: 36
            color: "#0f0f1a"
            radius: 6
            Text {
                anchors.centerIn: parent
                text: "Candidates (v0.2)"
                color: "#555"
                font.pixelSize: 12
            }
        }
        
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
            
            Rectangle {
                width: 80; height: 50; radius: 8
                color: root.shiftActive ? "#4a4a6a" : (shiftMa.pressed ? "#3a3a5a" : "#2a2a4a")
                border.color: "#4a4a6a"; border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "⇧"
                    color: "#88c0d0"
                    font.pixelSize: 18
                }
                MouseArea {
                    id: shiftMa
                    anchors.fill: parent
                    onClicked: root.shiftActive = !root.shiftActive
                }
            }
            
            Repeater { model: root.row3; KeyBtn { label: modelData } }
            KeyBtn { label: ","; code: "," }
            KeyBtn { label: "."; code: "." }
        }
        
        // Row 4: Space bar
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 6
            
            // Placeholder for copy/paste (not wired in v0.1)
            Rectangle {
                width: 70; height: 50; radius: 8
                color: "#1a1a2e"; border.color: "#333"; border.width: 1
                Text { anchors.centerIn: parent; text: "Copy"; color: "#444"; font.pixelSize: 12 }
            }
            Rectangle {
                width: 70; height: 50; radius: 8
                color: "#1a1a2e"; border.color: "#333"; border.width: 1
                Text { anchors.centerIn: parent; text: "Paste"; color: "#444"; font.pixelSize: 12 }
            }
            
            KeyBtn { label: "space"; code: "space"; kw: 300; special: true }
            
            Rectangle {
                width: 70; height: 50; radius: 8
                color: "#1a1a2e"; border.color: "#333"; border.width: 1
                Text { anchors.centerIn: parent; text: "Cut"; color: "#444"; font.pixelSize: 12 }
            }
            Rectangle {
                width: 70; height: 50; radius: 8
                color: "#1a1a2e"; border.color: "#333"; border.width: 1
                Text { anchors.centerIn: parent; text: "SelAll"; color: "#444"; font.pixelSize: 12 }
            }
        }
    }
}
