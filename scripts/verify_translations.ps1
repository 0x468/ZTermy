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
                 'UTF-8', 'Aa', 'xterm-256color', '$ ls -la ~/src'))
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

$allowedCppLiterals = [System.Collections.Generic.HashSet[string]]::new([string[]] @('Cascadia Mono', 'Consolas', 'Skills'))
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

# Source coverage: every qsTr()/tr() string in src/ must already be in the
# catalog. lupdate writes a scratch copy and reports the entries it would
# add as "unfinished"; committing a new string without its zh_CN translation
# therefore fails this test instead of silently shipping English.
$lupdate = if ($env:ZTERMY_LUPDATE) { $env:ZTERMY_LUPDATE } else { (Get-Command lupdate -ErrorAction SilentlyContinue).Source }
if ($lupdate) {
    $scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("ztermy_translations_" + [guid]::NewGuid().ToString('N') + '.ts')
    try {
        Copy-Item -LiteralPath $catalogFile -Destination $scratch
        $sources = Join-Path $sourceDirectory 'src'
        & $lupdate -silent -no-obsolete -locations none $sources -ts $scratch 2>$null | Out-Null
        if ($LASTEXITCODE -ne 0) {
            $errors.Add("lupdate failed with exit code $LASTEXITCODE.")
        } else {
            [xml] $updated = Get-Content -LiteralPath $scratch -Raw
            foreach ($message in @($updated.SelectNodes('/TS/context/message'))) {
                $translation = $message.SelectSingleNode('translation')
                if ($translation.GetAttribute('type') -eq 'unfinished') {
                    $context = $message.ParentNode.SelectSingleNode('name').InnerText
                    $errors.Add("${context}: '$($message.SelectSingleNode('source').InnerText)' is in the sources but missing from the catalog (run lupdate and translate it).")
                }
            }
        }
    } finally {
        Remove-Item -LiteralPath $scratch -Force -ErrorAction SilentlyContinue
    }
} else {
    Write-Warning 'lupdate not found; source coverage check skipped.'
}

if ($errors.Count -ne 0) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "Verified $($messages.Count) translations and localization call sites."
