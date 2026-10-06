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
$ini = Join-Path $fixture 'settings.ini'
# Quoting is required because both the executable and repository paths can contain spaces.
$arguments = '--smoke-test "{0}" "{1}"' -f $repo, $ini
$process = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
if (-not $process.WaitForExit(30000)) { Stop-Process -Id $process.Id; throw 'GUI workflow timed out' }
$report = Join-Path $fixture 'gui-smoke-report.txt'
if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $report)) { throw "GUI workflow failed: exit $($process.ExitCode)" }
$result = Get-Content -LiteralPath $report -Raw
if (-not $result.StartsWith('PASS')) { throw $result }
Write-Output $result
