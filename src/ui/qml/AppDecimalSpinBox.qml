pragma ComponentBehavior: Bound

import QtQuick

AppSpinBox {
    id: control

    readonly property real realValue: value / 100

    editable: true
    stepSize: 50
    validator: DoubleValidator {
        bottom: control.from / 100
        top: control.to / 100
        decimals: 2
        notation: DoubleValidator.StandardNotation
        locale: control.locale.name
    }
    textFromValue: function (value, locale) {
        const decimals = value % 100 === 0 ? 0 : value % 10 === 0 ? 1 : 2;
        return Number(value / 100).toLocaleString(locale, "f", decimals);
    }
    valueFromText: function (text, locale) {
        return Math.round(Number.fromLocaleString(locale, text) * 100);
    }
}
