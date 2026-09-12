#include "application/terminal/PowerShellShellIntegration.h"

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <cctype>

namespace ztermy::terminal
{

namespace
{

[[nodiscard]] bool validNonce(const std::string_view nonce) noexcept
{
    return !nonce.empty() && nonce.size() <= 128 && std::ranges::all_of(nonce, [](const unsigned char value) {
        return std::isalnum(value) != 0 || value == '-';
    });
}

[[nodiscard]] QString integrationScript(const std::string_view nonce)
{
    const QString script = QString::fromUtf8(R"ps1(
$global:__ztermyNonce = '%1'
$global:__ztermyCommandStarted = $false
$global:__ztermyOriginalPrompt = $function:prompt
$global:__ztermyEsc = [char] 27
$global:__ztermyBel = [char] 7

function global:__ztermyEscapeCommand([string] $value) {
    $result = [System.Text.StringBuilder]::new()
    foreach ($character in $value.ToCharArray()) {
        $code = [int] $character
        if ($code -eq 92) {
            [void] $result.Append('\\')
        } elseif ($code -eq 59 -or $code -le 32) {
            foreach ($byte in [System.Text.Encoding]::UTF8.GetBytes([string] $character)) {
                [void] $result.Append(('\x{0:x2}' -f $byte))
            }
        } else {
            [void] $result.Append($character)
        }
    }
    return $result.ToString()
}

function global:__ztermyLastExitCode {
    if ($?) { return 0 }
    $history = Get-History -Count 1
    if ($history -and $Error.Count -gt 0 -and $Error[0].InvocationInfo.HistoryId -eq $history.Id) { return -1 }
    return $global:LASTEXITCODE
}

function global:prompt {
    $exitCode = __ztermyLastExitCode
    $output = ''
    if ($global:__ztermyCommandStarted) {
        $output += "${global:__ztermyEsc}]633;D;$exitCode${global:__ztermyBel}"
        $global:__ztermyCommandStarted = $false
    }
    $output += "${global:__ztermyEsc}]633;A${global:__ztermyBel}${global:__ztermyEsc}]633;P;Cwd=$($PWD.Path)${global:__ztermyBel}${global:__ztermyEsc}]633;P;HasRichCommandDetection=True${global:__ztermyBel}"
    $promptText = if ($global:__ztermyOriginalPrompt) {
        & $global:__ztermyOriginalPrompt
    } else {
        "PS $PWD> "
    }
    $output += $promptText
    $output += "${global:__ztermyEsc}]633;B${global:__ztermyBel}"
    return $output
}

try {
    Import-Module PSReadLine -ErrorAction Stop
    if ($env:ZTERMY_TEST_SHELL_HISTORY) {
        Set-PSReadLineOption -HistorySavePath $env:ZTERMY_TEST_SHELL_HISTORY -HistorySaveStyle SaveNothing
    }
    Set-PSReadLineKeyHandler -Chord Enter -ScriptBlock {
        param($key, $arg)
        $line = $null
        $cursor = $null
        [Microsoft.PowerShell.PSConsoleReadLine]::GetBufferState([ref] $line, [ref] $cursor)
        $encoded = __ztermyEscapeCommand $line
        [Console]::Write("${global:__ztermyEsc}]633;E;$encoded;$global:__ztermyNonce${global:__ztermyBel}${global:__ztermyEsc}]633;C${global:__ztermyBel}")
        $global:__ztermyCommandStarted = $true
        [Microsoft.PowerShell.PSConsoleReadLine]::AcceptLine()
    }
} catch {
    # Prompt/CWD lifecycle markers remain available as basic integration.
}
)ps1")
                               .arg(QString::fromLatin1(nonce.data(), static_cast<qsizetype>(nonce.size())));
    return script;
}

[[nodiscard]] QByteArray utf16LittleEndian(const QString &value)
{
    return {reinterpret_cast<const char *>(value.utf16()), value.size() * static_cast<qsizetype>(sizeof(char16_t))};
}

} // namespace

std::optional<std::wstring> powerShellLaunchCommand(const std::wstring_view executable, const std::string_view nonce)
{
    if (executable.empty() || !validNonce(nonce))
    {
        return std::nullopt;
    }
    const QByteArray encoded = utf16LittleEndian(integrationScript(nonce)).toBase64();
    std::wstring command(executable);
    command.append(L" -NoLogo");
    if (!qEnvironmentVariableIsEmpty("ZTERMY_TEST_SHELL_HISTORY"))
        command.append(L" -NoProfile");
    command.append(L" -NoExit -EncodedCommand ");
    command.append(QString::fromLatin1(encoded).toStdWString());
    return command;
}

} // namespace ztermy::terminal
