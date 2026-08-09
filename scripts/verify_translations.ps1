param(
    [Parameter(Mandatory = $true)]
    [string] $CatalogPath,

    [Parameter(Mandatory = $true)]
    [string] $SourceRoot
)

$ErrorActionPreference = 'Stop'
$catalogFile = (Resolve-Path -LiteralPath $CatalogPath).Path
$sourceDirectory = (Resolve-Path -LiteralPath $SourceRoot).Path
[xml] $catalog = Get-Content -LiteralPath $catalogFile -Raw
$errors = [System.Collections.Generic.List[string]]::new()
$messages = @($catalog.SelectNodes('/TS/context/message'))

if ($catalog.TS.language -ne 'zh_CN') {
    $errors.Add("Expected zh_CN catalog, found '$($catalog.TS.language)'.")
}
if ($catalog.TS.sourcelanguage -ne 'en') {
    $errors.Add("Expected canonical en source language, found '$($catalog.TS.sourcelanguage)'.")
}
if ($messages.Count -eq 0) {
    $errors.Add('Translation catalog contains no messages.')
}

function Get-PlaceholderSignature([string] $value) {
    return [string]::Join('|', @(
        [regex]::Matches($value, '%(?:L?[1-9][0-9]*|n)') |
            ForEach-Object { $_.Value } |
            Sort-Object
    ))
}

foreach ($message in $messages) {
    $context = $message.ParentNode.SelectSingleNode('name').InnerText
    $source = $message.SelectSingleNode('source').InnerText
    $translation = $message.SelectSingleNode('translation')
    $state = $translation.GetAttribute('type')
    if ($state -in @('unfinished', 'obsolete', 'vanished')) {
        $errors.Add("${context}: '$source' is $state.")
        continue
    }

    $forms = if ($message.HasAttribute('numerus')) {
        @($translation.SelectNodes('numerusform'))
    } else {
        @($translation)
    }
    if ($forms.Count -eq 0) {
        $errors.Add("${context}: '$source' has no translation form.")
        continue
    }
    foreach ($form in $forms) {
        if ([string]::IsNullOrWhiteSpace($form.InnerText)) {
            $errors.Add("${context}: '$source' has an empty translation.")
            continue
        }
        $sourcePlaceholders = Get-PlaceholderSignature $source
        $translationPlaceholders = Get-PlaceholderSignature $form.InnerText
        if ($sourcePlaceholders -ne $translationPlaceholders) {
            $errors.Add("${context}: placeholder mismatch for '$source' ($sourcePlaceholders != $translationPlaceholders).")
        }
    }
}

$allowedQmlLiterals = [System.Collections.Generic.HashSet[string]]::new(
    [string[]] @('user@host[:port]', 'server.example.com or 192.0.2.10', '#22C55E', 'Cascadia Mono', 'ZTERMY',
                 'UTF-8', 'Aa', 'xterm-256color'))
$qmlPattern = '(?:text|placeholderText|Accessible\.name|title|description|toolTip|statusText)\s*:\s*"([^"]*[A-Za-z][^"]*)"'
foreach ($qmlFile in Get-ChildItem -LiteralPath (Join-Path $sourceDirectory 'src/ui/qml') -Filter '*.qml') {
    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $qmlFile.FullName) {
        ++$lineNumber
        foreach ($match in [regex]::Matches($line, $qmlPattern)) {
            if (-not $allowedQmlLiterals.Contains($match.Groups[1].Value)) {
                $errors.Add("$($qmlFile.Name):$lineNumber bypasses qsTr(): $($match.Groups[1].Value)")
            }
        }
    }
}

$allowedCppLiterals = [System.Collections.Generic.HashSet[string]]::new([string[]] @('Cascadia Mono', 'Consolas'))
$cppPattern = 'QStringLiteral\("([A-Z][^"]*)"\)'
$criticalCppFiles = @(
    'src/application/AppController.cpp',
    'src/application/ssh/SshTerminalSession.cpp',
    'src/application/terminal/LocalTerminalSession.cpp',
    'src/ui/terminal/TerminalItem.cpp',
    'src/ui/terminal/TerminalItem.h'
)
foreach ($relativePath in $criticalCppFiles) {
    $filePath = Join-Path $sourceDirectory $relativePath
    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $filePath) {
        ++$lineNumber
        foreach ($match in [regex]::Matches($line, $cppPattern)) {
            if (-not $allowedCppLiterals.Contains($match.Groups[1].Value)) {
                $errors.Add("$relativePath`:$lineNumber bypasses tr()/translate(): $($match.Groups[1].Value)")
            }
        }
    }
}

if ($errors.Count -ne 0) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "Verified $($messages.Count) translations and localization call sites."
