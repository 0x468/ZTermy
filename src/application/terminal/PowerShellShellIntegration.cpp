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
        $output += "`e]633;D;$exitCode`a"
        $global:__ztermyCommandStarted = $false
    }
    $output += "`e]633;A`a`e]633;P;Cwd=$($PWD.Path)`a`e]633;P;HasRichCommandDetection=True`a"
    $promptText = if ($global:__ztermyOriginalPrompt) {
        & $global:__ztermyOriginalPrompt
    } else {
        "PS $PWD> "
    }
    $output += $promptText
    $output += "`e]633;B`a"
    return $output
}

try {
    Import-Module PSReadLine -ErrorAction Stop
    Set-PSReadLineKeyHandler -Chord Enter -ScriptBlock {
        param($key, $arg)
        $line = $null
        $cursor = $null
        [Microsoft.PowerShell.PSConsoleReadLine]::GetBufferState([ref] $line, [ref] $cursor)
        $encoded = __ztermyEscapeCommand $line
        [Console]::Write("`e]633;E;$encoded;$global:__ztermyNonce`a`e]633;C`a")
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
    command.append(L" -NoLogo -NoExit -EncodedCommand ");
    command.append(QString::fromLatin1(encoded).toStdWString());
    return command;
}

} // namespace ztermy::terminal
