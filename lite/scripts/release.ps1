param([string]$OutputDir = "$PSScriptRoot/../release")
$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath("$PSScriptRoot/..")
$out = [IO.Path]::GetFullPath($OutputDir)
if (Test-Path -LiteralPath $out) { throw "Use a fresh release directory: $out" }
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($name in @('build-a', 'build-b')) {
  cmake -S $source -B "$out/$name" -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release
  if ($LASTEXITCODE) { throw 'Configure failed' }
  cmake --build "$out/$name" -j 4
  if ($LASTEXITCODE) { throw 'Build failed' }
}
ctest --test-dir "$out/build-a" --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
python "$source/tests/test_agent_adapter.py"
if ($LASTEXITCODE) { throw 'Driver adapter tests failed' }
$a = "$out/build-a/TangOSLite.exe"
$b = "$out/build-b/TangOSLite.exe"
$hash = (Get-FileHash -LiteralPath $a -Algorithm SHA256).Hash
if ($hash -ne (Get-FileHash -LiteralPath $b -Algorithm SHA256).Hash) { throw 'Two clean builds were not byte reproducible' }
Copy-Item -LiteralPath $a -Destination "$out/TangOSLite.exe"
$size = (Get-Item -LiteralPath "$out/TangOSLite.exe").Length
if ($size -ge 50000000) { throw 'Executable exceeds strict 50 MB limit' }
& "$source/scripts/gui-smoke.ps1" -Executable "$out/TangOSLite.exe" -FixtureDir "$out/gui-fixture"
$imports = & objdump -p "$out/TangOSLite.exe" | Select-String 'DLL Name:' | ForEach-Object { $_.Line.Trim() }
$compiler = (& g++ --version | Select-Object -First 1)
$cmake = (& cmake --version | Select-Object -First 1)
$report = "TangOS Lite 0.3.0 Windows x64`nBytes: $size`nDecimal MB: $([math]::Round($size / 1000000, 3))`nSHA256: $hash`nTwo clean builds: identical`n$compiler`n$cmake`nImports:`n$($imports -join "`n")`n"
Set-Content -LiteralPath "$out/SIZE.txt" -Value $report -Encoding utf8
Copy-Item -LiteralPath "$source/README.md" -Destination "$out/README.md"
Copy-Item -LiteralPath "$source/../LICENSE" -Destination "$out/LICENSE"
Copy-Item -LiteralPath "$source/docs" -Destination "$out/docs" -Recurse
Copy-Item -LiteralPath "$source/THIRD_PARTY.md" -Destination "$out/THIRD_PARTY.md"
Write-Output $report
