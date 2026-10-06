param([Parameter(Mandatory)][string]$Executable, [string]$FixtureDir = "$PSScriptRoot/../out/gui-fixture")
$ErrorActionPreference = 'Stop'
$fixture = [IO.Path]::GetFullPath($FixtureDir)
if (Test-Path -LiteralPath $fixture) { throw "Use a fresh fixture directory: $fixture" }
$repo = Join-Path $fixture 'repository with spaces'
New-Item -ItemType Directory -Force "$repo/tools" | Out-Null
Set-Content -LiteralPath "$repo/.tangos-lite-test-fixture" -Value 'Explicit disposable GUI fixture'
Set-Content -LiteralPath "$repo/tools/port_refcheck.py" -Value "import time`nprint('port/fixture.cpp:42: actionable fixture diagnostic', flush=True)`ntime.sleep(2)`nprint('fixture checks passed', flush=True)"
git -C $repo init -b main
if ($LASTEXITCODE) { throw 'Fixture git init failed' }
git -C $repo -c user.name=Lite -c user.email=lite@example.invalid add tools/port_refcheck.py
git -C $repo -c user.name=Lite -c user.email=lite@example.invalid commit -m 'GUI fixture'
if ($LASTEXITCODE) { throw 'Fixture commit failed' }
$exe = (Resolve-Path -LiteralPath $Executable).Path
$hashFixture = Join-Path $fixture 'hash-fixture.txt'
[IO.File]::WriteAllText($hashFixture, 'abc', [Text.Encoding]::ASCII)
$knownDigest = 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'
foreach ($expected in @($knownDigest, ('0' * 64))) {
  $hashArgs = '--verify-rom "{0}" {1}' -f $hashFixture, $expected
  $hashOutput = Join-Path $fixture "hash-$expected.txt"
  $hashProcess = Start-Process -FilePath $exe -ArgumentList $hashArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $hashOutput
  if (-not $hashProcess.WaitForExit(10000)) { Stop-Process -Id $hashProcess.Id; throw 'Native hash verification timed out' }
  $wantedCode = if ($expected -eq $knownDigest) { 0 } else { 1 }
  if ($hashProcess.ExitCode -ne $wantedCode -or -not ((Get-Content -LiteralPath $hashOutput -Raw).Contains($knownDigest))) { throw 'Packaged native hash verification failed' }
}
$ini = Join-Path $fixture 'settings.ini'
# Quoting is required because both the executable and repository paths can contain spaces.
$arguments = '--smoke-test "{0}" "{1}"' -f $repo, $ini
$process = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
if (-not $process.WaitForExit(30000)) { Stop-Process -Id $process.Id; throw 'GUI workflow timed out' }
$report = Join-Path $fixture 'gui-smoke-report.txt'
if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $report)) { throw "GUI workflow failed: exit $($process.ExitCode)" }
$result = Get-Content -LiteralPath $report -Raw
if (-not $result.StartsWith('PASS')) { throw $result }
foreach ($image in @('landing', 'controller', 'repository', 'workspace', 'theme-0', 'theme-1', 'theme-2', 'theme-3', 'theme-4')) {
  $path = Join-Path $fixture "$image.bmp"
  if (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -lt 100000) { throw "Missing native window render: $image" }
}
Write-Output $result
# This repository was created by this script. Resolve and confine cleanup before removal.
$resolvedRepo = (Resolve-Path -LiteralPath $repo).Path
$resolvedFixture = (Resolve-Path -LiteralPath $fixture).Path
if (-not $resolvedRepo.StartsWith($resolvedFixture + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($resolvedRepo) -ne 'repository with spaces') { throw 'Fixture cleanup path escaped its directory' }
Remove-Item -LiteralPath $resolvedRepo -Recurse -Force
