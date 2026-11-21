param(
    [string]$SdkPath = $env:ANDROID_HOME,
    [string]$JavaHome = $env:JAVA_HOME,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$SdkPath) { $SdkPath = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Android/Sdk' }
if (!$JavaHome) {
    $candidate = Get-ChildItem (Join-Path $env:ProgramFiles 'Android/openjdk') -Directory -Filter 'jdk-21*' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($candidate) { $JavaHome = $candidate.FullName }
}
if (!$JavaHome -or !(Test-Path (Join-Path $JavaHome 'bin/java.exe'))) { throw 'JDK 21 is required. Set JAVA_HOME or pass -JavaHome.' }
Set-Location (Join-Path $root 'android')
$env:ANDROID_HOME = $SdkPath
$env:JAVA_HOME = $JavaHome
if ($Configuration -eq 'Release' -and !(Test-Path 'signing.properties')) { throw 'Release signing requires android/signing.properties. See docs/RELEASE.md.' }
./gradlew.bat --no-daemon "assemble$Configuration"
if ($LASTEXITCODE) { throw 'Android build failed' }
$variant = $Configuration.ToLowerInvariant()
$apk = Join-Path $root "android/app/build/outputs/apk/$variant/app-$variant.apk"
python (Join-Path $root 'scripts/check-android-alignment.py') $apk
if ($LASTEXITCODE) { throw 'Android 16 KB alignment check failed' }
