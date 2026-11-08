param([switch]$Tests, [string]$QtPath = $env:Qt6_DIR)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $testSetting = if ($Tests) { 'ON' } else { 'OFF' }
    $arguments = @('-S', '.', '-B', 'build', '-DCMAKE_BUILD_TYPE=Debug', "-DBUILD_TESTING=$testSetting")
    if ($QtPath) { $arguments += "-DCMAKE_PREFIX_PATH=$QtPath" }
    & cmake @arguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cmake --build build --config Debug --parallel
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
