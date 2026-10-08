param([Parameter(Mandatory)][string]$Executable, [string]$FixtureDir = "$PSScriptRoot/../out/gui-fixture", [switch]$MissingDescriptor, [switch]$InvalidDescriptor)
$ErrorActionPreference = 'Stop'
$fixture = [IO.Path]::GetFullPath($FixtureDir)
if (Test-Path -LiteralPath $fixture) { throw "Use a fresh fixture directory: $fixture" }
$repo = Join-Path $fixture 'repository with spaces'
New-Item -ItemType Directory -Force "$repo/tools", "$repo/port" | Out-Null
Set-Content -LiteralPath "$repo/.tangos-lite-test-fixture" -Value 'Explicit disposable GUI fixture'
Set-Content -LiteralPath "$repo/tools/port_refcheck.py" -Value "import time`nprint('port/fixture.cpp:42: actionable fixture diagnostic', flush=True)`ntime.sleep(2)`nprint('fixture checks passed', flush=True)"
[IO.File]::WriteAllText("$repo/port/source.cpp", "int fixture_source() { return 42; }", [Text.UTF8Encoding]::new($false))
$descriptor = @{
  tangosVersion = '1'
  project = @{ name = 'native-gui-fixture'; title = 'Native GUI fixture'; tagline = 'Portable descriptor-driven Console' }
  runtime = @{ python = 'python'; envKeys = @('TEST_API_KEY') }
  data = @{ dbPath = 'chaos-db.json' }
  tools = @(@{ id = 'port_reference'; label = 'Port references'; category = 'verification'; description = 'Run independent port reference verification'; readOnly = $true; command = '{python} tools/port_refcheck.py'; args = @() })
}
[IO.File]::WriteAllText("$repo/tangos.json", ($descriptor | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText("$repo/chaos-db.json", '{"functions":[{"id":"1","name":"fixture_port_init","module":"port","size":80,"matched":true,"author":"FixtureContributor","srcPath":"port/source.cpp"},{"id":"2","name":"fixture_port_render","module":"port","size":120,"matched":false,"claim":null},{"id":"3","name":"fixture_port_check","module":"port","size":60,"matched":false,"div":2}]}', [Text.UTF8Encoding]::new($false))
git -C $repo init -b main
if ($LASTEXITCODE) { throw 'Fixture git init failed' }
git -C $repo -c user.name=Lite -c user.email=lite@example.invalid add tools/port_refcheck.py port/source.cpp tangos.json chaos-db.json
git -C $repo -c user.name=Lite -c user.email=lite@example.invalid commit -m 'GUI fixture'
if ($LASTEXITCODE) { throw 'Fixture commit failed' }
if ($MissingDescriptor) { Remove-Item -LiteralPath "$repo/tangos.json" }
if ($InvalidDescriptor) { [IO.File]::WriteAllText("$repo/tangos.json", '{"invalid":"fixture"}') }

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
if (-not $process.WaitForExit(60000)) { Stop-Process -Id $process.Id; throw 'GUI workflow timed out' }
$report = Join-Path $fixture 'gui-smoke-report.txt'
if (-not (Test-Path -LiteralPath $report)) { throw "GUI workflow failed without a report: exit $($process.ExitCode)" }
if ($process.ExitCode -ne 0) { throw (Get-Content -LiteralPath $report -Raw) }
$result = Get-Content -LiteralPath $report -Raw
if (-not $result.StartsWith('PASS')) { throw $result }
$images = @('landing', 'controller', 'repository', 'workspace', 'theme-0', 'theme-1', 'theme-2', 'theme-3', 'theme-4', 'console-0', 'console-1', 'console-2', 'console-3', 'console-4', 'console-5', 'console-6', 'console-8', 'console-9', 'console-10', 'console-11', 'console-12', 'tour-expression', 'tips')
if ($MissingDescriptor -or $InvalidDescriptor) { $images = @('landing','controller','repository','workspace','theme-0','theme-1','theme-2','theme-3','theme-4','descriptor-missing','descriptor-generated','descriptor-review') }
foreach ($image in $images) {
  $path = Join-Path $fixture "$image.bmp"
  if (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -lt 100000) { throw "Missing native window render: $image" }
}
if (-not ($MissingDescriptor -or $InvalidDescriptor)) {
if (-not (Get-Content -LiteralPath (Join-Path $fixture 'fleet-gui-report.txt') -Raw).StartsWith('PASS')) { throw 'Packaged fleet workflow failed' }
if (-not (Get-Content -LiteralPath (Join-Path $fixture 'viewer-gui-report.txt') -Raw).StartsWith('PASS')) { throw 'Native viewer parity workflow failed' }
} else {
if (-not (Get-Content -LiteralPath (Join-Path $fixture 'descriptor-gui-report.txt') -Raw).StartsWith('PASS')) { throw 'Descriptor gate workflow failed' }
}
Write-Output $result
# This repository was created by this script. Resolve and confine cleanup before removal.
$resolvedRepo = (Resolve-Path -LiteralPath $repo).Path
$resolvedFixture = (Resolve-Path -LiteralPath $fixture).Path
if (-not $resolvedRepo.StartsWith($resolvedFixture + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($resolvedRepo) -ne 'repository with spaces') { throw 'Fixture cleanup path escaped its directory' }
Remove-Item -LiteralPath $resolvedRepo -Recurse -Force
