.pragma library

function tokenForIndex(index) {
    return index === 1 ? "portable" : index === 2 ? "session" : "system";
}

function label(token) {
    return token === "portable" ? qsTranslate("SettingsPane", "Portable encrypted vault") : token === "session" ? qsTranslate("SettingsPane", "Session only") : qsTranslate("SettingsPane", "Windows Credential Manager");
}
