[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('pluginval', 'clap-validator')][string]$Validator,
    [Parameter(Mandatory)][string]$StageDirectory,
    [string]$ReportDirectory = 'dist/validator-reports'
)

$ErrorActionPreference = 'Stop'
$stage = (Resolve-Path -LiteralPath $StageDirectory).Path
New-Item -ItemType Directory -Force -Path $ReportDirectory | Out-Null
$reports = (Resolve-Path -LiteralPath $ReportDirectory).Path
$toolDirectory = Join-Path ([IO.Path]::GetTempPath()) "tonetrace-$Validator"
New-Item -ItemType Directory -Force -Path $toolDirectory | Out-Null

if ($Validator -eq 'pluginval') {
    $url = 'https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Windows.zip'
    $expectedHash = 'c08e61ce3b96db41636f8ec7e76f4c7e2c13ebdac7fa1b5a1f52b4f32ec715ab'
    $plugin = Join-Path $stage 'plugins/vst3/Tone Trace EQ.vst3'
    $arguments = "--strictness-level 7 --random-seed 42712 --timeout-ms 120000 --validate `"$plugin`""
} else {
    $url = 'https://github.com/free-audio/clap-validator/releases/download/0.4.1/clap-validator-0.4.1-127-g152b982-windows.zip'
    $expectedHash = 'd935c3af0a45c3911ea2e900f4aa5d6709dac82bb485f0c4ce28648ab2cd0c10'
    $plugin = Join-Path $stage 'plugins/clap/Tone Trace EQ.clap'
    $arguments = "validate --json --hide-output --jobs 1 `"$plugin`""
}
if (-not (Test-Path -LiteralPath $plugin)) { throw "Missing staged plug-in: $plugin" }
$archive = Join-Path $toolDirectory 'validator.zip'
Invoke-WebRequest -Uri $url -OutFile $archive
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) {
    throw "Checksum mismatch for $Validator download"
}
Expand-Archive -LiteralPath $archive -DestinationPath $toolDirectory -Force
$executables = @(Get-ChildItem -LiteralPath $toolDirectory -Recurse -Filter "$Validator.exe")
if ($executables.Count -ne 1) { throw "Expected one $Validator executable" }
$output = Join-Path $reports "$Validator.stdout.txt"
$errors = Join-Path $reports "$Validator.stderr.txt"
"Validator: $Validator`nURL: $url`nArchive SHA256: $expectedHash`nArguments: $arguments" |
    Set-Content -LiteralPath (Join-Path $reports "$Validator.invocation.txt") -Encoding utf8

# Start-Process waits for GUI-subsystem executables such as pluginval too.
$process = Start-Process -FilePath $executables[0].FullName -ArgumentList $arguments `
    -PassThru -WindowStyle Hidden -RedirectStandardOutput $output -RedirectStandardError $errors
if (-not $process.WaitForExit(600000)) {
    & taskkill /PID $process.Id /T /F
    throw "$Validator exceeded the ten-minute validation limit"
}
$process.WaitForExit()
$process.Refresh()
Get-Content -LiteralPath $output
Get-Content -LiteralPath $errors
if ($process.ExitCode -ne 0) { throw "$Validator exited with code $($process.ExitCode)" }

if ($Validator -eq 'pluginval') {
    if (-not (Select-String -LiteralPath $output -Pattern '^SUCCESS\s*$' -Quiet)) {
        throw 'pluginval did not report successful validation'
    }
    $summary = 'pluginval passed at strictness 7, including GUI tests (seed 42712).'
} else {
    $report = Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
    $results = @($report.results)
    if ($results.Count -eq 0 -or @($results | Where-Object { $_.status.code -eq 'success' }).Count -eq 0) {
        throw 'clap-validator returned no successful test results'
    }
    $failures = @($results | Where-Object { $_.status.code -notin @('success', 'skipped', 'warning') })
    $warnings = @($results | Where-Object { $_.status.code -eq 'warning' })
    foreach ($warning in $warnings) {
        # Shared-runner timing varies. Retain and surface this one measured warning.
        if ($warning.test.'plugin-instance'.test -ne 'process-audio-denormals') {
            throw "Unexpected CLAP warning: $($warning.status.details)"
        }
        Write-Warning "CLAP denormal timing: $($warning.status.details)"
    }
    if ($failures.Count -gt 0) { throw "CLAP validation has $($failures.Count) failed or unknown results" }
    $skipped = @($results | Where-Object { $_.status.code -eq 'skipped' }).Count
    $summary = "clap-validator completed $($results.Count) tests with zero failures, $($warnings.Count) denormal timing warnings, and $skipped unsupported-feature skips."
}
Write-Output $summary
if ($env:GITHUB_STEP_SUMMARY) { Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value $summary }
