# Hosted Windows only. No installs, credentials, signing, release or audio streams.
$ErrorActionPreference='Stop'
Set-Location -LiteralPath $PSScriptRoot
$expectedRepository='The-Hybrid-Elite-Mafia/HEM-World-Heritage-Dot'
if($env:GITHUB_ACTIONS -ne 'true' -or $env:GITHUB_REPOSITORY -ne $expectedRepository){throw 'Run only on the authorized hosted GitHub Actions repository.'}
$out=Join-Path $PSScriptRoot 'ci-output'
New-Item -ItemType Directory -Force "$out/logs","$out/delivery","$out/obj","$out/tests" | Out-Null
Start-Transcript -Path "$out/logs/build-transcript.txt"
try {
  $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
  $instances=& $vswhere -all -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
  $vs=$instances | Where-Object {[version]$_.installationVersion -ge [version]'17.13'} | Sort-Object {[version]$_.installationVersion} -Descending | Select-Object -First 1
  if(!$vs){throw 'Modern MSVC x64 components >= VS17.13 unavailable; no installation attempted.'}
  $vs | Select-Object installationVersion,installationPath,displayName | ConvertTo-Json | Set-Content "$out/logs/toolchain.json"
  Import-Module (Join-Path $vs.installationPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
  Enter-VsDevShell -VsInstallPath $vs.installationPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
  @'
#include <expected>
#include <bit>
#include <iostream>
#if !defined(_MSC_VER) || !defined(_M_X64) || _MSC_VER < 1943
#error Modern Microsoft x64 compiler required
#endif
static_assert(_MSVC_LANG >= 202302L, "Actual C++23 mode required");
static_assert(__cplusplus >= 202302L, "C++23 macro required");
int main(){std::expected<int,int> value=23;std::cout<<"MSC="<<_MSC_VER<<" MSVC_LANG="<<_MSVC_LANG<<" CPLUSPLUS="<<__cplusplus<<" expected="<<*value<<" byteswap="<<std::byteswap(0x01020304u)<<'\n';}
'@ | Set-Content "$out/standard-probe.cpp" -Encoding utf8
  $standard=$null
  foreach($mode in @('/std:c++23','/std:c++23preview')){
    & cl /nologo /WX $mode /Zc:__cplusplus /EHsc /arch:AVX2 /MT "/Fo$out/obj/probe.obj" "/Fe$out/standard-probe.exe" "$out/standard-probe.cpp" 2>&1 | Tee-Object "$out/logs/standard-probe.txt" -Append
    if($LASTEXITCODE -eq 0){$standard=$mode;break}
  }
  if(!$standard){throw 'Actual C++23 mode not proven; no older-mode fallback.'}
  & "$out/standard-probe.exe" | Tee-Object "$out/logs/standard-version.txt"
  if($LASTEXITCODE){throw 'Standard probe execution failed'}
  $fixture=Join-Path $PSScriptRoot 'ci-fixtures/ganjo-C5.wav'
  if((Get-FileHash $fixture -Algorithm SHA256).Hash.ToLower() -ne (Get-Content ci-fixtures/SHA256.txt -Raw).Trim()){throw 'Acoustic fixture checksum mismatch'}
  $common=@('/nologo',$standard,'/Zc:__cplusplus','/arch:AVX2','/EHsc','/utf-8','/O2','/MT','/DUNICODE','/D_UNICODE')
  $core=@('sample.cpp','mmap_streamer.cpp','dsp.cpp','audio.cpp','asio_backend.cpp')
  $libs=@('mfplat.lib','mfreadwrite.lib','mfuuid.lib','ole32.lib','propsys.lib','uuid.lib','winmm.lib','shell32.lib','ksuser.lib','advapi32.lib','user32.lib','gdi32.lib','d2d1.lib','dwrite.lib','windowscodecs.lib')
  & rc /nologo /fo "$out/obj/version.res" version.rc
  if($LASTEXITCODE){throw 'Resource build failed'}
  $targets=@('workstation','workstation_tests','knob_tests','portable_pack_tests','audio_preferences_test','nonblocking_metadata_test','ui_model_tests','repair_ui_tests','output_tests','level_tests','offscreen_render')
  foreach($target in $targets){
    $dest=if($target -eq 'workstation'){"$out/delivery/WorldHeritageWorkstation.exe"}else{"$out/$target.exe"}
    $subsystem=if($target -eq 'workstation'){'/SUBSYSTEM:WINDOWS'}else{'/SUBSYSTEM:CONSOLE'}
    & cl @common "/Fo$out/obj/" "/Fe$dest" "$target.cpp" @core "$out/obj/version.res" /link $subsystem @libs 2>&1 | Tee-Object "$out/logs/$target-build.txt"
    if($LASTEXITCODE){throw "Build failed: $target"}
  }
  function Run-Check([string]$Name,[string[]]$Arguments){& "$out/$Name.exe" @Arguments 2>&1 | Tee-Object "$out/logs/$Name-test.txt";if($LASTEXITCODE){throw "Offline test failed: $Name"}}
  Run-Check workstation_tests @($fixture,"$out/tests")
  Run-Check knob_tests @($fixture)
  Run-Check portable_pack_tests @($fixture,"$out/tests")
  Run-Check audio_preferences_test @("$out/tests/preferences.conf")
  Run-Check nonblocking_metadata_test @($fixture)
  Run-Check ui_model_tests @($fixture)
  Run-Check repair_ui_tests @()
  Run-Check output_tests @()
  Run-Check level_tests @($fixture)
  & "$out/offscreen_render.exe" "$out/delivery/production-1366x768.png" 1366 768 "$out/tests/roundtrip.whic" | Tee-Object "$out/logs/offscreen.txt"
  if($LASTEXITCODE){throw 'Production offscreen render failed'}
  & "$out/offscreen_render.exe" "$out/delivery/zone-editor-1366x768.png" 1366 768 "$out/tests/roundtrip.whic" zone
  if($LASTEXITCODE){throw 'Editor production render failed'}
  Copy-Item LICENSE,THIRD_PARTY_NOTICES.md,README.md,REPAIRS-0.4.1.md "$out/delivery/"
  & git archive --format=zip "--output=$out/delivery/source.zip" HEAD
  if($LASTEXITCODE){throw 'Source archive failed'}
  Get-ChildItem "$out/delivery" -File | Get-FileHash -Algorithm SHA256 | Select-Object Hash,@{Name='File';Expression={Split-Path $_.Path -Leaf}} | ConvertTo-Json | Set-Content "$out/delivery/SHA256.json"
  'Build/offline checks passed. No app UI, stream, playback, driver install or publication.' | Set-Content "$out/delivery/STATUS.txt"
} finally { Stop-Transcript }
