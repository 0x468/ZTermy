import QtQuick

Canvas {
    id: chart

    property var values: []
    property color lineColor: Theme.accent
    property real maximum: 100

    implicitWidth: 280
    implicitHeight: 52
    antialiasing: true

    onValuesChanged: requestPaint()
    onLineColorChanged: requestPaint()
    onMaximumChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const context = getContext("2d");
        context.reset();
        context.clearRect(0, 0, width, height);
        context.strokeStyle = Theme.border;
        context.lineWidth = 1;
        context.beginPath();
        context.moveTo(0, height - 0.5);
        context.lineTo(width, height - 0.5);
        context.stroke();
        if (!values || values.length < 2) {
            return;
        }
        const maxValue = Math.max(1, maximum);
        context.strokeStyle = lineColor;
        context.lineWidth = 1.5;
        context.lineJoin = "round";
        context.lineCap = "round";
        context.beginPath();
        for (let index = 0; index < values.length; ++index) {
            const value = Math.max(0, Math.min(maxValue, Number(values[index]) || 0));
            const x = index * width / Math.max(1, values.length - 1);
            const y = height - 3 - (value / maxValue) * (height - 7);
            if (index === 0) {
                context.moveTo(x, y);
            } else {
                context.lineTo(x, y);
            }
        }
        context.stroke();
    }
}
