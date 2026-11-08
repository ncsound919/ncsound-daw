<#
    Fetches the native dependencies that are deliberately not committed:

      third_party/JUCE   JUCE at the commit recorded in third_party/JUCE_PIN.txt
      deps/webview2      the Microsoft.Web.WebView2 package (hub UI only)

    Both must exist before configure. Without JUCE the top-level CMakeLists skips
    the app and both plug-ins and still reports success - which is how CI used to
    "build the full app" while compiling only the engine. Without WebView2,
    juce_add_gui_app(NEEDS_WEBVIEW2) fails configure.

    Idempotent: a checkout already at the pinned commit and an already extracted
    package are left alone, so repeated runs are cheap.

    Paths are written with forward slashes on purpose: PowerShell and CMake both
    accept them on Windows, and they survive being copied between shells.
#>
[CmdletBinding()]
param(
    [string] $JuceDir = (Join-Path $PSScriptRoot '../third_party/JUCE'),
    [string] $WebView2Dir = (Join-Path $PSScriptRoot '../deps/webview2'),
    [string] $WebView2Version = '1.0.4314.35'
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$JuceDir = [IO.Path]::GetFullPath($JuceDir)
$WebView2Dir = [IO.Path]::GetFullPath($WebView2Dir)

# --- JUCE, at the commit the pin file records ---------------------------------
$pinFile = Join-Path $repoRoot 'third_party/JUCE_PIN.txt'
$pin = Select-String -Path $pinFile -Pattern '^commit:\s*([0-9a-f]{40})' | Select-Object -First 1
if (-not $pin) { throw "No 'commit: <sha1>' line in $pinFile" }
$expected = $pin.Matches[0].Groups[1].Value

$current = ''
if (Test-Path (Join-Path $JuceDir '.git')) {
    $current = (& git -C $JuceDir rev-parse HEAD | Out-String).Trim()
}

if ($current -eq $expected) {
    Write-Host "JUCE already at $expected"
} else {
    if (Test-Path $JuceDir) { Remove-Item $JuceDir -Recurse -Force }
    & git clone --quiet --depth 1 --branch 8.0.17-security https://github.com/juce-framework/JUCE.git $JuceDir
    if ($LASTEXITCODE -ne 0) { throw 'git clone of JUCE failed' }
    $current = (& git -C $JuceDir rev-parse HEAD | Out-String).Trim()
    if ($current -ne $expected) {
        throw "JUCE checkout is $current but third_party/JUCE_PIN.txt pins $expected"
    }
    Write-Host "JUCE checked out at $expected"
}

# --- WebView2, pinned to one SDK version --------------------------------------
$packageDir = Join-Path $WebView2Dir "Microsoft.Web.WebView2.$WebView2Version"
$header = Join-Path $packageDir 'build/native/include/WebView2.h'

if (Test-Path $header) {
    Write-Host "WebView2 $WebView2Version already extracted"
} else {
    # A .nupkg is a zip, but Expand-Archive refuses the .nupkg extension, so the
    # archive is downloaded under a .zip name.
    $archive = Join-Path ([IO.Path]::GetTempPath()) "webview2-$WebView2Version.zip"
    Invoke-WebRequest -UseBasicParsing -Uri "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$WebView2Version" -OutFile $archive
    if (Test-Path $packageDir) { Remove-Item $packageDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
    Expand-Archive -Path $archive -DestinationPath $packageDir -Force
    Remove-Item $archive -Force
    if (-not (Test-Path $header)) {
        throw "WebView2 $WebView2Version extracted without $header"
    }
    Write-Host "WebView2 $WebView2Version extracted"
}

Write-Host "JUCE:     $JuceDir"
Write-Host "WebView2: $WebView2Dir"
