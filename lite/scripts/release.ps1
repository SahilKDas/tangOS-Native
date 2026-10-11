param([string]$OutputDir = "$PSScriptRoot/../release", [double]$MinimumFreeGiB = 25, [switch]$KeepBuildArtifacts, [switch]$Resume)
$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath("$PSScriptRoot/..")
$out = [IO.Path]::GetFullPath($OutputDir)
if ((Test-Path -LiteralPath $out) -and -not $Resume) { throw "Use a fresh release directory: $out" }
New-Item -ItemType Directory -Force $out | Out-Null
$out = (Resolve-Path -LiteralPath $out).Path
function Assert-FreeSpace {
  $drive = [IO.Path]::GetPathRoot($out).Substring(0, 1)
  $free = (Get-PSDrive -Name $drive).Free
  if ($free -lt (($MinimumFreeGiB + 0.75) * 1GB)) {
    throw "Release paused: insufficient headroom to keep $MinimumFreeGiB GiB free."
  }
}
function Remove-GeneratedDirectory([string]$path) {
  $absolute = [IO.Path]::GetFullPath($path)
  if (-not $absolute.StartsWith($out + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Cleanup escaped release directory' }
  if (Test-Path -LiteralPath $absolute) {
    if ((Resolve-Path -LiteralPath $absolute).Path -ne $absolute -or (Get-Item -LiteralPath $absolute -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unexpected cleanup target' }
    Remove-Item -LiteralPath $absolute -Recurse -Force
  }
}
$checkpointPath = Join-Path $out 'first-build.json'
$repoRoot = [IO.Path]::GetFullPath("$source/..")
$inputs = & git -C $repoRoot ls-files -- lite
if ($LASTEXITCODE) { throw 'Cannot fingerprint release source' }
$inputHashes = foreach ($inputPath in $inputs) {
  "$inputPath $((Get-FileHash -LiteralPath (Join-Path $repoRoot $inputPath) -Algorithm SHA256).Hash)"
}
$hasher = [Security.Cryptography.SHA256]::Create()
try {
  $sourceHash = [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes(($inputHashes -join "`n")))).Replace('-', '')
} finally { $hasher.Dispose() }
$referenceHash = $null
if ($Resume) {
  if (-not (Test-Path -LiteralPath $checkpointPath)) { throw 'No completed first-build checkpoint; use a fresh directory' }
  $checkpoint = Get-Content -LiteralPath $checkpointPath -Raw | ConvertFrom-Json
  if ($checkpoint.sourceHash -ne $sourceHash -or $checkpoint.hash -notmatch '^[A-F0-9]{64}$') { throw 'Release source changed; use a fresh directory' }
  $referenceHash = $checkpoint.hash
}
foreach ($name in @('build-b', 'build-a')) {
  if ($name -eq 'build-b' -and $referenceHash) { continue }
  Assert-FreeSpace
  cmake -S $source -B "$out/$name" -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release
  if ($LASTEXITCODE) { throw 'Configure failed' }
  if ($name -eq 'build-b') {
    cmake --build "$out/$name" --target TangOSLite -j 2
  } else {
    cmake --build "$out/$name" -j 2
  }
  if ($LASTEXITCODE) { throw 'Build failed' }
  if ($name -eq 'build-b') {
    $referenceHash = (Get-FileHash -LiteralPath "$out/$name/TangOSLite.exe" -Algorithm SHA256).Hash
    @{ hash = $referenceHash; sourceHash = $sourceHash } | ConvertTo-Json | Set-Content -LiteralPath $checkpointPath -Encoding utf8
    if (-not $KeepBuildArtifacts) { Remove-GeneratedDirectory "$out/$name" }
  }
}
Assert-FreeSpace
ctest --test-dir "$out/build-a" --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
python "$source/tests/test_agent_adapter.py"
if ($LASTEXITCODE) { throw 'Driver adapter tests failed' }
python "$source/tests/test_check_verdict.py"
if ($LASTEXITCODE) { throw 'Verification outcome tests failed' }
python "$source/tests/test_remote_confirmation.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Remote confirmation safety tests failed' }
python "$source/tests/test_backend_cli.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Packaged backend tests failed' }
python "$source/tests/test_workflows_cli.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Packaged Git workflow tests failed' }
python "$source/tests/test_project_import_cli.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Packaged ZIP import tests failed' }
python "$source/tests/test_reference_parity.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original Console comparisons failed' }
python "$source/tests/test_statistics_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE -ne 0) { throw 'Statistics reference tests failed' }
python "$source/tests/test_activity_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original statistics comparisons failed' }
python "$source/tests/test_size_order_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original equal-rate size ordering comparisons failed' }
python "$source/tests/test_auto_role_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original Simple-mode role comparisons failed' }
python "$source/tests/test_preflight_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original preflight comparisons failed' }
python "$source/tests/test_help_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original help comparisons failed' }
python "$source/tests/test_rich_text_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original styled help comparisons failed' }
python "$source/tests/test_layout_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original Atlas geometry comparisons failed' }
python "$source/tests/test_sort_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original Viewer function-list sort comparisons failed' }
python "$source/tests/test_color_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original Atlas color comparisons failed' }
python "$source/tests/test_batches_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original batch comparisons failed' }
python "$source/tests/test_source_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original source comparisons failed' }
python "$source/tests/test_driver_tokens_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original driver token-summary comparisons failed' }
python "$source/tests/test_usage_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original usage-stop comparisons failed' }
python "$source/tests/test_presence_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original presence comparisons failed' }
python "$source/tests/test_controller_view_reference.py" --exe "$out/build-a/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original Controller telemetry comparisons failed' }

$a = "$out/build-a/TangOSLite.exe"
$hash = (Get-FileHash -LiteralPath $a -Algorithm SHA256).Hash
if ($hash -ne $referenceHash) { throw 'Two clean builds were not byte reproducible' }
Copy-Item -LiteralPath $a -Destination "$out/TangOSLite.exe"
$size = (Get-Item -LiteralPath "$out/TangOSLite.exe").Length
if ($size -ge 50000000) { throw 'Executable exceeds strict 50 MB limit' }
Assert-FreeSpace
& "$source/scripts/gui-smoke.ps1" -Executable "$out/TangOSLite.exe" -FixtureDir "$out/gui-fixture"
& "$source/scripts/gui-smoke.ps1" -Executable "$out/TangOSLite.exe" -FixtureDir "$out/gui-descriptor-missing" -MissingDescriptor
& "$source/scripts/gui-smoke.ps1" -Executable "$out/TangOSLite.exe" -FixtureDir "$out/gui-descriptor-invalid" -InvalidDescriptor
$imports = & objdump -p "$out/TangOSLite.exe" | Select-String 'DLL Name:' | ForEach-Object { $_.Line.Trim() }
$compiler = (& g++ --version | Select-Object -First 1)
$cmake = (& cmake --version | Select-Object -First 1)
$report = "TangOS Lite 0.31.0 Windows x64`nBytes: $size`nDecimal MB: $([math]::Round($size / 1000000, 3))`nSHA256: $hash`nTwo clean builds: identical`n$compiler`n$cmake`nImports:`n$($imports -join "`n")`n"
Set-Content -LiteralPath "$out/SIZE.txt" -Value $report -Encoding utf8
Copy-Item -LiteralPath "$source/README.md" -Destination "$out/README.md"
Copy-Item -LiteralPath "$source/../LICENSE" -Destination "$out/LICENSE"
Copy-Item -LiteralPath "$source/docs" -Destination "$out/docs" -Recurse
Copy-Item -LiteralPath "$source/THIRD_PARTY.md" -Destination "$out/THIRD_PARTY.md"
$evidence = Join-Path $out 'verification-evidence'
New-Item -ItemType Directory -Force $evidence | Out-Null
Copy-Item -LiteralPath "$out/build-a/Testing/Temporary/LastTest.log" -Destination (Join-Path $evidence 'native-tests.log')
Copy-Item -LiteralPath "$out/build-a/splash-fixture/report.json" -Destination (Join-Path $evidence 'splash-report.json')
if (-not $KeepBuildArtifacts) {
  $testLog = Get-Content -LiteralPath (Join-Path $evidence 'native-tests.log') -Raw
  $tempRoot = (Resolve-Path -LiteralPath ([IO.Path]::GetTempPath())).Path.TrimEnd('\')
  foreach ($match in [regex]::Matches($testLog, '(?m)^(?:Fixture retained for inspection: |[0-9]+ backend assertions passed; fixture=)([^\r\n]+)')) {
    $fixturePath = [IO.Path]::GetFullPath($match.Groups[1].Value.Trim())
    if ([IO.Path]::GetDirectoryName($fixturePath) -ne $tempRoot -or [IO.Path]::GetFileName($fixturePath) -notmatch '^(?:TangOS (fleet|update) |lite-backend-)[0-9a-f]+$') { throw 'Test fixture cleanup escaped expected Temp roots' }
    if (Test-Path -LiteralPath $fixturePath) {
      if ((Resolve-Path -LiteralPath $fixturePath).Path -ne $fixturePath -or (Get-Item -LiteralPath $fixturePath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unexpected test fixture cleanup target' }
      Remove-Item -LiteralPath $fixturePath -Recurse -Force
    }
  }
}
foreach ($fixture in @('gui-fixture', 'gui-descriptor-missing', 'gui-descriptor-invalid')) {
  $destination = Join-Path $evidence $fixture
  New-Item -ItemType Directory -Force $destination | Out-Null
  Get-ChildItem -LiteralPath "$out/$fixture" -File -Filter '*report.txt' | Copy-Item -Destination $destination
  if (-not $KeepBuildArtifacts) { Remove-GeneratedDirectory "$out/$fixture" }
}
if (-not $KeepBuildArtifacts) { Remove-GeneratedDirectory "$out/build-a" }
Remove-Item -LiteralPath $checkpointPath -Force
Write-Output $report
