import QtQuick

QtObject {
    required property real minimum
    required property real maximum
    required property real defaultValue
    property var points: [defaultValue]
    property real heldPoint: NaN

    function clear() {
        heldPoint = NaN;
    }
    function bounded(value) {
        return Math.max(minimum, Math.min(maximum, value));
    }
    function reset() {
        clear();
        return bounded(defaultValue);
    }
    function resolve(value) {
        const raw = bounded(value);
        if (isFinite(heldPoint) && heldPoint >= minimum && heldPoint <= maximum && Math.abs(raw - heldPoint) <= 14)
            return heldPoint;
        clear();
        let distance = 8;
        for (const point of points) {
            if (point >= minimum && point <= maximum && Math.abs(raw - point) <= distance) {
                heldPoint = point;
                distance = Math.abs(raw - point);
            }
        }
        return isFinite(heldPoint) ? heldPoint : raw;
    }
}
