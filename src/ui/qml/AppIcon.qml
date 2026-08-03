pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

Item {
    id: icon

    required property string name
    property color color: Theme.text
    readonly property real effectiveDevicePixelRatio: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    readonly property int rasterWidth: Math.max(20, Math.ceil(width * effectiveDevicePixelRatio))
    readonly property int rasterHeight: Math.max(20, Math.ceil(height * effectiveDevicePixelRatio))
    readonly property string encodedColor: color.toString().replace("#", "")

    implicitWidth: 16
    implicitHeight: 16

    Image {
        anchors.fill: parent
        source: icon.name.length > 0 ? "image://ztermy-icons/" + icon.name + "/" + icon.encodedColor + "/" + icon.rasterWidth + "x" + icon.rasterHeight : ""
        sourceSize.width: icon.rasterWidth
        sourceSize.height: icon.rasterHeight
        fillMode: Image.PreserveAspectFit
        asynchronous: false
        cache: true
        smooth: true
        mipmap: false
    }
}
