import QtQuick

Canvas {
    id: chart

    property var values: []
    property color lineColor: Theme.accent
    property real lowerBound: 0
    property real upperBound: -1
    property real minimumSpan: 8
    readonly property var scale: calculateScale()

    implicitWidth: 280
    implicitHeight: 52
    antialiasing: true

    onValuesChanged: requestPaint()
    onLineColorChanged: requestPaint()
    onLowerBoundChanged: requestPaint()
    onUpperBoundChanged: requestPaint()
    onMinimumSpanChanged: requestPaint()
    onScaleChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function calculateScale() {
        let minimum = Number.POSITIVE_INFINITY;
        let maximum = Number.NEGATIVE_INFINITY;
        for (const entry of values || []) {
            const value = Number(entry);
            if (!Number.isFinite(value))
                continue;
            minimum = Math.min(minimum, value);
            maximum = Math.max(maximum, value);
        }
        if (!Number.isFinite(minimum) || !Number.isFinite(maximum))
            return {
                "minimum": lowerBound,
                "maximum": Math.max(lowerBound + 1, minimumSpan)
            };

        const observedSpan = maximum - minimum;
        const desiredSpan = Math.max(minimumSpan, observedSpan * 1.35);
        const midpoint = (minimum + maximum) / 2;
        let scaledMinimum = midpoint - desiredSpan / 2;
        let scaledMaximum = midpoint + desiredSpan / 2;
        scaledMinimum = Math.max(lowerBound, scaledMinimum);
        if (upperBound >= lowerBound)
            scaledMaximum = Math.min(upperBound, scaledMaximum);
        if (scaledMaximum - scaledMinimum < Math.max(1, minimumSpan * 0.5)) {
            scaledMinimum = Math.max(lowerBound, scaledMaximum - Math.max(1, minimumSpan));
            if (upperBound >= lowerBound)
                scaledMaximum = Math.min(upperBound, scaledMinimum + Math.max(1, minimumSpan));
        }
        return {
            "minimum": scaledMinimum,
            "maximum": Math.max(scaledMinimum + 1, scaledMaximum)
        };
    }

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
        if (!values || values.length === 0) {
            return;
        }
        const minValue = scale.minimum;
        const maxValue = scale.maximum;
        const scaleSpan = Math.max(1, maxValue - minValue);
        context.strokeStyle = lineColor;
        context.lineWidth = 1.5;
        context.lineJoin = "round";
        context.lineCap = "round";
        context.beginPath();
        for (let index = 0; index < values.length; ++index) {
            const value = Math.max(minValue, Math.min(maxValue, Number(values[index]) || 0));
            const x = index * width / Math.max(1, values.length - 1);
            const y = height - 3 - ((value - minValue) / scaleSpan) * (height - 7);
            if (index === 0) {
                context.moveTo(x, y);
            } else {
                context.lineTo(x, y);
            }
        }
        context.stroke();

        context.fillStyle = lineColor;
        for (let index = 0; index < values.length; ++index) {
            const value = Math.max(minValue, Math.min(maxValue, Number(values[index]) || 0));
            const x = index * width / Math.max(1, values.length - 1);
            const y = height - 3 - ((value - minValue) / scaleSpan) * (height - 7);
            context.beginPath();
            context.arc(x, y, 1.6, 0, Math.PI * 2);
            context.fill();
        }
    }
}
