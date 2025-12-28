import QtQuick
import QtQuick.Window
import QtQuick.Layouts

Window {
    id: root
    width: 800; height: 280; visible: true
    color: "#1a1a2e"; title: "Magic Keyboard"
    
    property bool shiftActive: false
    signal keyPressed(string key)
    signal actionTriggered(string action)
    
    readonly property var row1: ["q","w","e","r","t","y","u","i","o","p"]
    readonly property var row2: ["a","s","d","f","g","h","j","k","l"]
    readonly property var row3: ["z","x","c","v","b","n","m"]
    
    component KeyBtn: Rectangle {
        property string label: ""; property string code: label.toLowerCase()
        property real kw: 60; property bool special: false
        width: kw; height: 50; radius: 8
        color: ma.pressed ? "#3a3a5a" : "#2a2a4a"
        border.color: "#4a4a6a"; border.width: 1
        Text {
            anchors.centerIn: parent
            text: root.shiftActive && !parent.special ? parent.label.toUpperCase() : parent.label
            color: parent.special ? "#88c0d0" : "#eceff4"; font.pixelSize: 18
        }
        MouseArea { id: ma; anchors.fill: parent
            onClicked: {
                if (parent.special) { root.keyPressed(parent.code) }
                else { root.keyPressed(root.shiftActive ? parent.code.toUpperCase() : parent.code); root.shiftActive = false }
            }
        }
    }
    
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 6
        Rectangle { Layout.fillWidth: true; height: 36; color: "#0f0f1a"; radius: 6
            Text { anchors.centerIn: parent; text: "Candidates here (v0.2)"; color: "#666" }
        }
        RowLayout { Layout.alignment: Qt.AlignHCenter; spacing: 6
            Repeater { model: root.row1; KeyBtn { label: modelData } }
            KeyBtn { label: "⌫"; code: "backspace"; kw: 80; special: true }
        }
        RowLayout { Layout.alignment: Qt.AlignHCenter; spacing: 6
            Item { width: 15 }
            Repeater { model: root.row2; KeyBtn { label: modelData } }
            KeyBtn { label: "↵"; code: "enter"; kw: 80; special: true }
        }
        RowLayout { Layout.alignment: Qt.AlignHCenter; spacing: 6
            KeyBtn { label: "⇧"; code: "shift"; kw: 80; special: true
                MouseArea { anchors.fill: parent; onClicked: root.shiftActive = !root.shiftActive }
            }
            Repeater { model: root.row3; KeyBtn { label: modelData } }
            KeyBtn { label: ","; code: "," }
            KeyBtn { label: "."; code: "." }
        }
        RowLayout { Layout.alignment: Qt.AlignHCenter; spacing: 6
            KeyBtn { label: "Copy"; code: "copy"; kw: 70; special: true; MouseArea { anchors.fill: parent; onClicked: root.actionTriggered("copy") } }
            KeyBtn { label: "Paste"; code: "paste"; kw: 70; special: true; MouseArea { anchors.fill: parent; onClicked: root.actionTriggered("paste") } }
            KeyBtn { label: "space"; code: "space"; kw: 300; special: true }
            KeyBtn { label: "Cut"; code: "cut"; kw: 70; special: true; MouseArea { anchors.fill: parent; onClicked: root.actionTriggered("cut") } }
            KeyBtn { label: "SelAll"; code: "selectall"; kw: 70; special: true; MouseArea { anchors.fill: parent; onClicked: root.actionTriggered("selectall") } }
        }
    }
    onKeyPressed: k => console.log("Key:", k)
    onActionTriggered: a => console.log("Action:", a)
}
