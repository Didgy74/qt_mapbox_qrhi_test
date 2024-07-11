import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import MyQmlApp 1.0
import com.example

ApplicationWindow {
    width: 800
    height: 600
    visible: true
    title: qsTr("Hello World")

    TileLoader {
        id: myLoader
    }

    ColumnLayout {
        anchors.fill: parent

        /*
        Text {
            text: "Hello World!"
            font.family: "Helvetica"
            //font.pointSize: 18
            color: "red"
        }
        */

        QQuickMap {
            id: myMap
            tileLoader: myLoader
            viewportZoom: 8
            viewportX: 0.53
            viewportY: 0.29
            /*
            Component.onCompleted: {
                myMap.setViewportCoordDegrees(10.765248, 59.949584413);
                myMap.viewportZoom = 12;
            }*/


            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.left: parent.left
                anchors.top: parent.top

                Button {
                    Layout.fillWidth: true
                    text: "Oslo"
                    onClicked: {
                        myMap.setViewportCoordDegrees(10.765248, 59.949584413);
                        myMap.viewportZoom = 12;
                    }
                }
            }

            GridLayout {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                opacity: 0.5

                columns: 3

                Button {
                    Layout.fillWidth: true
                    text: "RotLeft"
                    //font.pointSize: 18
                    onClicked: { myMap.viewportRotation += 15 }
                }
                Button {
                    Layout.fillWidth: true
                    text: "Up"
                    //font.pointSize: 18
                    onClicked: { myMap.viewportY -= 0.1 }
                }
                Button {
                    Layout.fillWidth: true
                    text: "RotRight"
                    //font.pointSize: 18
                    onClicked: { myMap.viewportRotation -= 15 }
                }

                Button {
                    Layout.fillWidth: true
                    text: "Left"
                    //font.pointSize: 18
                    onClicked: { myMap.viewportX -= 0.1 }
                }
                Button {
                    text: "Center"
                    //font.pointSize: 18
                    onClicked: {
                        myMap.viewportX = 0.5;
                        myMap.viewportY = 0.5;
                        myMap.viewportRotation = 0;
                    }
                }
                Button {
                    Layout.fillWidth: true
                    text: "Right"
                    //font.pointSize: 18
                    onClicked: { myMap.viewportX += 0.1 }
                }

                Item {}
                Button {
                    text: "Down"
                    //font.pointSize: 18
                    onClicked: { myMap.viewportY += 0.1 }
                }
                Item {}
            }


            ColumnLayout {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.margins: 10
                z: 1
                opacity: 0.5

                GridLayout {
                    columns: 2
                    Text {
                        text: "VpZoom"
                        //font.pointSize: 18
                    }
                    Text {
                        text: myMap.viewportZoom.toFixed(2)
                        //font.pointSize: 18
                    }

                    Text {
                        text: "VpRot"
                        //font.pointSize: 18
                    }
                    Text {
                        text: myMap.viewportRotation.toFixed(2)
                        //font.pointSize: 18
                    }

                    Text {
                        text: "VpX"
                        //font.pointSize: 18
                    }
                    Text {
                        text: myMap.viewportX.toFixed(2)
                        //font.pointSize: 18
                    }

                    Text {
                        text: "VpY"
                        //font.pointSize: 18
                    }
                    Text {
                        text: myMap.viewportY.toFixed(2)
                        //font.pointSize: 18
                    }
                }

                Button {
                    text: "In"
                    //font.pointSize: 24
                    onClicked: { myMap.viewportZoom += 0.25 }
                    Layout.fillWidth: true

                    //font.pointSize: 18
                }
                Button {
                    text: "Out"
                    //font.pointSize: 18
                    onClicked: { myMap.viewportZoom -= 0.25 }
                    Layout.fillWidth: true
                }
            }
        }
    }
}
