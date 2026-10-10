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
  # Process.Start retains the handle even when this small command exits before
  # PowerShell 5.1's Start-Process returns; otherwise ExitCode can be null.
  $hashProcess = New-Object Diagnostics.Process
  $hashProcess.StartInfo.FileName = $exe
  $hashProcess.StartInfo.Arguments = $hashArgs
  $hashProcess.StartInfo.UseShellExecute = $false
  $hashProcess.StartInfo.CreateNoWindow = $true
  $hashProcess.StartInfo.WindowStyle = 'Hidden'
  $hashProcess.StartInfo.RedirectStandardOutput = $true
  [void]$hashProcess.Start()
  $hashRead = $hashProcess.StandardOutput.ReadToEndAsync()
  if (-not $hashProcess.WaitForExit(10000)) { Stop-Process -Id $hashProcess.Id; throw 'Native hash verification timed out' }
  [IO.File]::WriteAllText($hashOutput, $hashRead.Result)
  $wantedCode = if ($expected -eq $knownDigest) { 0 } else { 1 }
  if ($hashProcess.ExitCode -ne $wantedCode -or -not ((Get-Content -LiteralPath $hashOutput -Raw).Contains($knownDigest))) { throw 'Packaged native hash verification failed' }
  $hashProcess.Dispose()
}
$ini = Join-Path $fixture 'settings.ini'
# Quoting is required because both the executable and repository paths can contain spaces.
$arguments = '--smoke-test "{0}" "{1}"' -f $repo, $ini
$process = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
$null = $process.Handle
# The expanded matrix captures overlays, three window sizes, every attached monitor
# and a 25,000-function fixture. Bound the whole run separately from individual checks.
if (-not $process.WaitForExit(240000)) { Stop-Process -Id $process.Id; throw 'GUI workflow timed out' }
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
$display = Get-Content -LiteralPath (Join-Path $fixture 'display-validation.json') -Raw | ConvertFrom-Json
if ($display.monitors.Count -lt 1 -or @($display.layouts | Where-Object state -eq 'passed').Count -ne 9) { throw 'Display layout matrix incomplete' }
if (-not (Test-Path -LiteralPath (Join-Path $fixture 'help-complete-reference.bmp'))) { throw 'Complete native reference was not rendered' }
foreach ($overlay in @('helper-floating-tips', 'tour-overlay-centered', 'tour-overlay-spotlight')) {
  if (-not (Test-Path -LiteralPath (Join-Path $fixture "$overlay.bmp"))) { throw "Native overlay missing: $overlay" }
}
$helper = Get-Content -LiteralPath (Join-Path $fixture 'helper-overlay.json') -Raw | ConvertFrom-Json
$pixels = [IO.File]::ReadAllBytes((Join-Path $fixture 'helper-floating-tips.bmp'))
$pixelWidth = [BitConverter]::ToInt32($pixels, 18)
$pixelHeight = [BitConverter]::ToInt32($pixels, 22)
if ([BitConverter]::ToInt16($pixels, 28) -ne 32 -or $pixelHeight -le 0) { throw 'Unexpected native capture format' }
$sampleX = [int]$helper.panel.x + 130
$sampleY = [int]$helper.panel.y + 20
if ($sampleX -ge $pixelWidth -or $sampleY -ge $pixelHeight) { throw 'Floating helper is outside the capture' }
$pixel = [BitConverter]::ToInt32($pixels, 10) + (($pixelHeight - 1 - $sampleY) * $pixelWidth + $sampleX) * 4
if ($pixels[$pixel] -ge 100 -or $pixels[$pixel + 1] -lt 190 -or $pixels[$pixel + 1] - $pixels[$pixel + 2] -lt 70) { throw 'Floating helper was painted behind the Console pane' }
foreach ($image in @('controller-reference-idle', 'controller-cart')) {
  $path = Join-Path $fixture "$image.bmp"
  if (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -lt 100000) { throw "Missing Controller reference render: $image" }
}
if (-not (Get-Content -LiteralPath (Join-Path $fixture 'controller-cart-report.txt') -Raw).StartsWith('PASS')) { throw 'Simple-mode cart workflow failed' }
if (-not (Get-Content -LiteralPath (Join-Path $fixture 'controller-advanced-report.txt') -Raw).StartsWith('PASS')) { throw 'Advanced Controller workflow failed' }
if (-not (Get-Content -LiteralPath (Join-Path $fixture 'controller-view-report.txt') -Raw).StartsWith('PASS')) { throw 'Controller live telemetry workflow failed' }
if (-not (Get-Content -LiteralPath (Join-Path $fixture 'module-window-report.txt') -Raw).StartsWith('PASS')) { throw 'Native module-window layout failed' }
if (-not (Get-Content -LiteralPath (Join-Path $fixture 'report-overlay-report.txt') -Raw).StartsWith('PASS')) { throw 'Native report overlay workflow failed' }
if (-not (Get-Content -LiteralPath (Join-Path $fixture 'remote-viewer-gui-report.txt') -Raw).StartsWith('PASS')) { throw 'Remote viewer workflow failed' }
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
