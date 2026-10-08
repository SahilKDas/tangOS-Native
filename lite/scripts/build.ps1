param([string]$BuildDir = "$PSScriptRoot/../out", [string]$Generator = 'MinGW Makefiles')
$ErrorActionPreference = 'Stop'
cmake -S "$PSScriptRoot/.." -B $BuildDir -G $Generator -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'CMake configure failed' }
cmake --build $BuildDir --config Release -j 4
if ($LASTEXITCODE) { throw 'Build failed' }
ctest --test-dir $BuildDir -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
python "$PSScriptRoot/../tests/test_agent_adapter.py"
if ($LASTEXITCODE) { throw 'Driver adapter tests failed' }
python "$PSScriptRoot/../tests/test_backend_cli.py" --exe "$BuildDir/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Packaged backend tests failed' }

python "$PSScriptRoot/../tests/test_reference_parity.py" --exe "$BuildDir/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original Console comparisons failed' }
python "$PSScriptRoot/../tests/test_statistics_reference.py" --exe "$BuildDir/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original statistics comparisons failed' }
python "$PSScriptRoot/../tests/test_preflight_reference.py" --exe "$BuildDir/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original preflight comparisons failed' }
python "$PSScriptRoot/../tests/test_help_reference.py" --exe "$BuildDir/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original help comparisons failed' }
python "$PSScriptRoot/../tests/test_layout_reference.py" --exe "$BuildDir/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original Atlas geometry comparisons failed' }
python "$PSScriptRoot/../tests/test_color_reference.py" --exe "$BuildDir/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original Atlas color comparisons failed' }
python "$PSScriptRoot/../tests/test_batches_reference.py" --exe "$BuildDir/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original batch comparisons failed' }
python "$PSScriptRoot/../tests/test_source_reference.py" --exe "$BuildDir/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original source comparisons failed' }
python "$PSScriptRoot/../tests/test_usage_reference.py" --exe "$BuildDir/TangOSLite.exe"
if ($LASTEXITCODE) { throw 'Original usage-stop comparisons failed' }
python "$PSScriptRoot/../tests/audit_parity.py"
if ($LASTEXITCODE) { throw 'Parity inventory failed' }
