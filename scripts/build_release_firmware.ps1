param([string]$PlatformIO = 'pio')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$versionHeader = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'include/version.h')
if ($versionHeader -notmatch '#define APP_VERSION "(\d+\.\d+\.\d+)"') { throw 'Missing APP_VERSION.' }
$releaseVersion = $Matches[1]
$releaseRoot = Join-Path $projectRoot "release-assets/v$releaseVersion"
New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null
$profiles = @(
    @('esp32_notifier', 'esp32-notifier', 0, 8),
    @('esp32_notifier_hacs', 'esp32-notifier-hacs', 0, 8),
    @('esp32_notifier_hacs_slim', 'esp32-notifier-hacs-slim', 0, 8),
    @('esp32_notifier_hacs_legacy_ota', 'esp32-notifier-hacs-legacy-ota', 0, 8, 0x190000),
    @('esp32s3_notifier', 'esp32s3-notifier', 9, 1),
    @('esp32s3_notifier_hacs', 'esp32s3-notifier-hacs', 9, 1),
    @('esp32s3_notifier_hacs_slim', 'esp32s3-notifier-hacs-slim', 9, 1),
    @('esp32c3_designer_hacs', 'esp32c3-notifier-hacs', 5, 11),
    @('esp32_ota_bridge', 'esp32-ota-bridge', 0, 8),
    @('esp32s3_ota_bridge', 'esp32s3-ota-bridge', 9, 1),
    @('esp32c3_ota_bridge', 'esp32c3-ota-bridge', 5, 11)
)
$previousPortable = $env:ELMA_PORTABLE_BUILDER
$previousBoard = $env:ELMA_SELECTED_BOARD_PROFILE_ID
$previousStaPower = $env:ELMA_STA_TX_POWER_DBM
$previousApPower = $env:ELMA_AP_TX_POWER_DBM
try {
    $env:ELMA_STA_TX_POWER_DBM = '15.0'
    $env:ELMA_AP_TX_POWER_DBM = '15.0'
    # Regenerate the shared assets once; subsequent builds compile the selected
    # board's guarded assets from that same generated source.
    $env:ELMA_PORTABLE_BUILDER = ''
    foreach ($profile in $profiles) {
        $environment = [string]$profile[0]
        $env:ELMA_SELECTED_BOARD_PROFILE_ID = [string]$profile[3]
        & $PlatformIO run --project-dir $projectRoot --environment $environment |
            Tee-Object -FilePath (Join-Path $releaseRoot "$environment.build.log")
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $environment" }
        $firmwarePath = Join-Path $projectRoot ".pio/build/$environment/firmware.bin"
        $firmwareBytes = [IO.File]::ReadAllBytes($firmwarePath)
        if ($firmwareBytes.Length -lt 80 -or $firmwareBytes[0] -ne 0xE9) { throw "Invalid image: $environment" }
        $chipId = [int]$firmwareBytes[12] + 256 * [int]$firmwareBytes[13]
        if ($chipId -ne [int]$profile[2]) { throw "Wrong chip in $environment" }
        $maximumImageSize = if ($profile.Count -ge 5) { [int]$profile[4] } else { 0x1F0000 }
        if ($firmwareBytes.Length -gt $maximumImageSize) {
            throw "Image exceeds the $maximumImageSize-byte OTA partition: $environment"
        }
        # Arduino's prebuilt IDF descriptor identifies arduino-lib-builder, not
        # APP_VERSION. Verify the application's own null-terminated version.
        $hasVersion = [Text.Encoding]::ASCII.GetString($firmwareBytes).Contains($releaseVersion + [char]0)
        if (-not $hasVersion) { throw "APP_VERSION $releaseVersion is missing from $environment" }
        $assetPath = Join-Path $releaseRoot "$($profile[1])-v$releaseVersion.bin"
        Copy-Item -LiteralPath $firmwarePath -Destination $assetPath -Force
        Write-Output "VERIFIED $($profile[1]): v$releaseVersion, chip $chipId, $($firmwareBytes.Length) bytes"
        $env:ELMA_PORTABLE_BUILDER = '1'
    }
} finally {
    $env:ELMA_PORTABLE_BUILDER = $previousPortable
    $env:ELMA_SELECTED_BOARD_PROFILE_ID = $previousBoard
    $env:ELMA_STA_TX_POWER_DBM = $previousStaPower
    $env:ELMA_AP_TX_POWER_DBM = $previousApPower
}
