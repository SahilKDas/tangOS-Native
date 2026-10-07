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
