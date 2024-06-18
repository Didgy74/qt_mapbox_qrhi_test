import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import MyQmlApp 1.0

ApplicationWindow {
    width: 600
    height: 800
    visible: true
    title: qsTr("Hello World")

    ColumnLayout {
        anchors.fill: parent

        Text {
            text: "Hello World!"
            font.family: "Helvetica"
            font.pointSize: 24
            color: "red"
        }

        QQuickMap {
            id: myMap
            viewportZoom: 0

            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.margins: 10
                z: 1

                Button {
                    text: "In"
                    //font.pointSize: 24
                    onClicked: { myMap.viewportZoom += 0.1 }
                    Layout.fillWidth: true
                }
                Button {
                    text: "Out"
                    //font.pointSize: 24
                    onClicked: { myMap.viewportZoom -= 0.1 }
                    Layout.fillWidth: true
                }
            }
        }
    }
}
