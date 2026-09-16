param([string]$SdkRoot = $env:ANDROID_HOME, [switch]$PlayRelease)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$androidRoot = Join-Path (Split-Path -Parent $projectRoot) 'Android'
if (!$SdkRoot -or !(Test-Path -LiteralPath (Join-Path $SdkRoot 'platforms/android-36/android.jar'))) {
    throw 'Pass -SdkRoot with an Android SDK containing platform android-36 and build-tools 36.0.0.'
}
$python = Join-Path $projectRoot '.elma-flasher-build/venv310/Scripts/python.exe'
if (!(Test-Path -LiteralPath $python)) { $python = 'python' }
if ($PlayRelease) {
    & $python (Join-Path $PSScriptRoot 'check_android_play_release.py')
    if ($LASTEXITCODE -ne 0) { throw 'Play release prerequisites are incomplete.' }
}
& $python (Join-Path $PSScriptRoot 'prepare_android.py')
if ($LASTEXITCODE -ne 0) { throw 'Android asset preparation failed.' }
$sdkProperty = $SdkRoot.Replace('\','/').Replace(':','\:')
Set-Content -LiteralPath (Join-Path $androidRoot 'local.properties') -Value "sdk.dir=$sdkProperty" -Encoding ascii
Push-Location $androidRoot
try {
    & ./gradlew.bat --no-daemon assembleDebug bundleRelease testDebugUnitTest lint
    if ($LASTEXITCODE -ne 0) { throw 'Android APK compilation failed.' }
} finally { Pop-Location }
$release = Join-Path $projectRoot 'release-assets/android/v1.0.0'
New-Item -ItemType Directory -Path $release -Force | Out-Null
$apk = Join-Path $release 'ELMA-Flasher-v1.0.0.apk'
Copy-Item -LiteralPath (Join-Path $androidRoot 'app/build/outputs/apk/debug/app-debug.apk') -Destination $apk -Force
Copy-Item -LiteralPath $apk -Destination (Join-Path $release "ELMA-Flasher-v1.0.0-android-preview.apk") -Force
Write-Output "Built development-signed APK: $apk"
Get-FileHash -LiteralPath $apk -Algorithm SHA256

Copy-Item -LiteralPath (Join-Path $androidRoot "app/build/outputs/bundle/release/app-release.aab") -Destination (Join-Path $release "ELMA-Flasher-v1.0.0.aab") -Force
