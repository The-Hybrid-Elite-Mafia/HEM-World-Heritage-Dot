param([string]$Compiler='clang++')
$ErrorActionPreference='Stop'
Set-Location -LiteralPath $PSScriptRoot
New-Item -ItemType Directory ../build -Force | Out-Null
$flags=@('-std=c++23','-O2','-mavx2','-static','-DUNICODE','-D_UNICODE','-municode')
$core=@('sample.cpp','mmap_streamer.cpp','dsp.cpp','audio.cpp','asio_backend.cpp')
$libs=@('-lmfplat','-lmfreadwrite','-lmfuuid','-lole32','-lpropsys','-luuid','-lwinmm','-lshell32','-lksuser','-ladvapi32')
$rc=Join-Path (Split-Path $Compiler) 'x86_64-w64-mingw32-windres.exe'
& $rc version.rc -O coff -o ../build/version.o
if($LASTEXITCODE){throw 'Resource compile failed'}
& $Compiler @flags -mwindows workstation.cpp ../build/version.o @core @libs -ld2d1 -ldwrite -o ../build/WorldHeritageWorkstation.exe
if($LASTEXITCODE){throw 'Workstation compile failed'}
foreach($test in @('workstation_tests','portable_pack_tests','knob_tests','audio_preferences_test','nonblocking_metadata_test')){
 & $Compiler @flags "$test.cpp" @core @libs -o "../build/$test.exe"
 if($LASTEXITCODE){throw "Compile failed: $test"}
}

& $Compiler @flags offscreen_render.cpp @core @libs -ld2d1 -ldwrite -lwindowscodecs -o ../build/offscreen_render.exe
if($LASTEXITCODE){throw "Offscreen renderer compile failed"}

& $Compiler @flags ui_model_tests.cpp @core @libs -ld2d1 -ldwrite -o ../build/ui_model_tests.exe
if($LASTEXITCODE){throw "UI model tests compile failed"}
