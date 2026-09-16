param(
    [switch]$SkipInstall,
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot '.elma-flasher-build'
$assetRoot = Join-Path $buildRoot 'assets'
$venvRoot = Join-Path $buildRoot 'venv310'
$versionHeader = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'include\version.h')
if ($versionHeader -notmatch '#define APP_VERSION "(\d+\.\d+\.\d+)"') {
    throw 'Unable to read APP_VERSION from include/version.h.'
}
$releaseVersion = $Matches[1]
$releaseTag = "v$releaseVersion"
$assetName = "ELMA-Flasher-$releaseTag"
$releaseRoot = Join-Path $projectRoot "release-assets\$releaseTag"
if ($OutputDirectory) { $releaseRoot = [IO.Path]::GetFullPath($OutputDirectory) }

$requiredFirmwareFiles = @(
    '.pio\build\esp32_notifier_hacs\bootloader.bin',
    '.pio\build\esp32_notifier_hacs\partitions.bin',
    '.pio\build\esp32s3_notifier_hacs\bootloader.bin',
    '.pio\build\esp32s3_notifier_hacs\partitions.bin',
    '.pio\build\esp32c3_designer_hacs\bootloader.bin',
    '.pio\build\esp32c3_designer_hacs\partitions.bin'
)
foreach ($relativePath in $requiredFirmwareFiles) {
    $fullPath = Join-Path $projectRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath)) {
        throw "Missing $relativePath. Build the ESP32, ESP32-S3 and C3 HACS PlatformIO environments first."
    }
}

New-Item -ItemType Directory -Force -Path (Join-Path $assetRoot 'esp32'), (Join-Path $assetRoot 'esp32s3'), (Join-Path $assetRoot 'esp32c3'), $releaseRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot '.pio\build\esp32_notifier_hacs\bootloader.bin') -Destination (Join-Path $assetRoot 'esp32\bootloader.bin') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot '.pio\build\esp32_notifier_hacs\partitions.bin') -Destination (Join-Path $assetRoot 'esp32\partitions.bin') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot '.pio\build\esp32s3_notifier_hacs\bootloader.bin') -Destination (Join-Path $assetRoot 'esp32s3\bootloader.bin') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot '.pio\build\esp32s3_notifier_hacs\partitions.bin') -Destination (Join-Path $assetRoot 'esp32s3\partitions.bin') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot '.pio\build\esp32c3_designer_hacs\bootloader.bin') -Destination (Join-Path $assetRoot 'esp32c3\bootloader.bin') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot '.pio\build\esp32c3_designer_hacs\partitions.bin') -Destination (Join-Path $assetRoot 'esp32c3\partitions.bin') -Force

if (-not (Test-Path -LiteralPath (Join-Path $venvRoot 'Scripts\python.exe'))) {
    $python310 = ''
    if (Get-Command py -ErrorAction SilentlyContinue) {
        $python310 = (& py -3.10 -c 'import sys; print(sys.executable)' 2>$null).Trim()
    }
    if ((-not $python310 -or -not (Test-Path -LiteralPath $python310)) -and (Get-Command python -ErrorAction SilentlyContinue)) {
        $candidate = (& python -c 'import sys, tkinter; print(sys.executable)' 2>$null).Trim()
        if ($LASTEXITCODE -eq 0) {
            $python310 = $candidate
        }
    }
    if (-not $python310 -or -not (Test-Path -LiteralPath $python310)) {
        throw 'Python 3.10 with Tcl/Tk is required to build ELMA Flasher.'
    }
    & $python310 -m venv $venvRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Creating the ELMA Flasher Python environment failed with exit code $LASTEXITCODE."
    }
}
$python = Join-Path $venvRoot 'Scripts\python.exe'
if (-not $SkipInstall) {
    & $python -m pip install --disable-pip-version-check --trusted-host pypi.org --trusted-host files.pythonhosted.org -r (Join-Path $projectRoot 'tools\elma_flasher\requirements-build.txt')
    if ($LASTEXITCODE -ne 0) {
        throw "ELMA Flasher build dependency installation failed with exit code $LASTEXITCODE."
    }
}
$qtRuntimeRoot = Join-Path $venvRoot 'Lib\site-packages\PySide6'
foreach ($runtimeName in @('MSVCP140.dll', 'VCRUNTIME140.dll', 'VCRUNTIME140_1.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $qtRuntimeRoot $runtimeName))) {
        throw "Missing bundled Qt runtime dependency $runtimeName. Reinstall the ELMA Flasher build requirements."
    }
}

$iconPath = Join-Path $buildRoot 'ELMA-Flasher.ico'
$iconSource = Join-Path $projectRoot 'web\elma_iot_favicon.ico'
if (-not (Test-Path -LiteralPath $iconSource)) {
    throw 'Missing the approved ELMA IoT icon at web\elma_iot_favicon.ico.'
}
Copy-Item -LiteralPath $iconSource -Destination $iconPath -Force
Copy-Item -LiteralPath $iconSource -Destination (Join-Path $assetRoot 'ELMA-Flasher.ico') -Force

$compilerName = 'ELMA-Compiler-Core'
& $python -m PyInstaller `
    --noconfirm `
    --clean `
    --onefile `
    --console `
    --name $compilerName `
    --collect-all platformio `
    --distpath (Join-Path $buildRoot 'compiler-dist') `
    --workpath (Join-Path $buildRoot 'compiler-work') `
    --specpath (Join-Path $buildRoot 'compiler-spec') `
    (Join-Path $projectRoot 'tools\elma_flasher\compiler_core.py')
if ($LASTEXITCODE -ne 0) {
    throw "ELMA portable compiler core build failed with exit code $LASTEXITCODE."
}
$compilerExe = Join-Path $buildRoot "compiler-dist\$compilerName.exe"
if (-not (Test-Path -LiteralPath $compilerExe)) {
    throw 'ELMA portable compiler core was not produced.'
}

$inheritedPath = $env:PATH
$basePythonRoot = (& $python -c 'import pathlib, sys; print(pathlib.Path(sys.base_prefix))').Trim()
$env:PATH = @(
    (Join-Path $venvRoot 'Scripts'),
    $basePythonRoot,
    (Join-Path $basePythonRoot 'Scripts'),
    (Join-Path $env:SystemRoot 'System32'),
    $env:SystemRoot,
    (Join-Path $env:SystemRoot 'System32\Wbem')
) -join ';'
try {
    $coreName = 'ELMA-Flasher-Core'
    $coreDistRoot = Join-Path $buildRoot 'core-dist'
    $resolvedBuildRoot = [IO.Path]::GetFullPath($buildRoot).TrimEnd('\') + '\'
    $resolvedCoreDistRoot = [IO.Path]::GetFullPath($coreDistRoot)
    if (-not $resolvedCoreDistRoot.StartsWith($resolvedBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe ELMA runtime build path: $resolvedCoreDistRoot"
    }
    if (Test-Path -LiteralPath $coreDistRoot) {
        Remove-Item -LiteralPath $coreDistRoot -Recurse -Force
    }
    & $python -m PyInstaller `
        --noconfirm `
        --clean `
        --windowed `
        --name $coreName `
        --icon $iconPath `
        --add-data "$assetRoot;assets" `
        --add-data "$(Join-Path $projectRoot 'web');web" `
        --add-data "$(Join-Path $projectRoot 'src');builder_project\src" `
        --add-data "$(Join-Path $projectRoot 'include');builder_project\include" `
        --add-data "$(Join-Path $projectRoot 'scripts');builder_project\scripts" `
        --add-data "$(Join-Path $projectRoot 'partitions');builder_project\partitions" `
        --add-data "$(Join-Path $projectRoot 'web');builder_project\web" `
        --add-data "$(Join-Path (Split-Path -Parent $projectRoot) 'Android\web\mobile.css');android_preview" `
        --add-data "$(Join-Path $projectRoot 'platformio.ini');builder_project" `
        --add-data "$(Join-Path $projectRoot 'sdkconfig.defaults');builder_project" `
        --add-data "$(Join-Path $projectRoot 'package.json');builder_project" `
        --add-data "$(Join-Path $projectRoot 'package-lock.json');builder_project" `
        --add-binary "$compilerExe;." `
        --add-binary "$(Join-Path $qtRuntimeRoot 'MSVCP140.dll');." `
        --add-binary "$(Join-Path $qtRuntimeRoot 'VCRUNTIME140.dll');." `
        --add-binary "$(Join-Path $qtRuntimeRoot 'VCRUNTIME140_1.dll');." `
        --collect-all esptool `
        --hidden-import PySide6.QtCore `
        --hidden-import PySide6.QtGui `
        --hidden-import PySide6.QtWidgets `
        --hidden-import PySide6.QtWebEngineCore `
        --hidden-import PySide6.QtWebEngineWidgets `
        --hidden-import serial.tools.list_ports `
        --distpath $coreDistRoot `
        --workpath (Join-Path $buildRoot 'pyinstaller-work') `
        --specpath (Join-Path $buildRoot 'pyinstaller-spec') `
        (Join-Path $projectRoot 'tools\elma_flasher\elma_flasher.py')
    $packagingExitCode = $LASTEXITCODE
} finally {
    $env:PATH = $inheritedPath
}
if ($packagingExitCode -ne 0) {
    throw "ELMA Flasher runtime packaging failed with exit code $packagingExitCode."
}

$coreDirectory = Join-Path $coreDistRoot 'ELMA-Flasher-Core'
$coreExe = Join-Path $coreDirectory 'ELMA-Flasher-Core.exe'
if (-not (Test-Path -LiteralPath $coreExe)) {
    throw 'PyInstaller completed without producing the cached ELMA runtime.'
}
$payloadZip = Join-Path $buildRoot 'ELMA-Flasher-payload.zip'
& $python (Join-Path $projectRoot 'tools\elma_flasher\package_payload.py') $coreDirectory $payloadZip
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $payloadZip)) {
    throw 'Packaging the cached ELMA runtime payload failed.'
}
$payloadFingerprint = (Get-FileHash -LiteralPath $payloadZip -Algorithm SHA256).Hash
$payloadFingerprintPath = Join-Path $buildRoot 'ELMA-Flasher-payload.sha256'
Set-Content -LiteralPath $payloadFingerprintPath -Value $payloadFingerprint -Encoding ascii -NoNewline

$dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
if (-not $dotnet) {
    throw '.NET 8 SDK is required to build the fast portable ELMA launcher.'
}
$dotnetExe = $dotnet.Source
$launcherProject = Join-Path $projectRoot 'tools\elma_flasher\launcher\ELMA.Flasher.Launcher.csproj'
& $dotnetExe publish $launcherProject `
    --configuration Release `
    --runtime win-x64 `
    --self-contained true `
    --output $releaseRoot `
    -p:AssemblyName=$assetName `
    -p:Version=$releaseVersion `
    -p:PayloadPath=$payloadZip `
    -p:PayloadFingerprintPath=$payloadFingerprintPath `
    -p:LauncherIcon=$iconPath
if ($LASTEXITCODE -ne 0) {
    throw "ELMA fast portable launcher build failed with exit code $LASTEXITCODE."
}

$exePath = Join-Path $releaseRoot "$assetName.exe"
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "The fast portable launcher did not produce $assetName.exe."
}
$hash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
Write-Host "Built $exePath"
Write-Host "SHA256 $hash"
