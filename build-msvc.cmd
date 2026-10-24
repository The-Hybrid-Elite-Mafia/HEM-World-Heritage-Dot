@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist ..\msvc-build mkdir ..\msvc-build
rc /nologo /fo ..\msvc-build\version.res version.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /arch:AVX2 /EHsc /utf-8 /O2 /DUNICODE /D_UNICODE /Fo..\msvc-build\ /Fe..\msvc-build\WorldHeritageWorkstation.exe workstation.cpp sample.cpp mmap_streamer.cpp dsp.cpp audio.cpp asio_backend.cpp ..\msvc-build\version.res /link /SUBSYSTEM:WINDOWS d2d1.lib dwrite.lib mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib propsys.lib uuid.lib winmm.lib shell32.lib ksuser.lib advapi32.lib user32.lib gdi32.lib

if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /arch:AVX2 /EHsc /utf-8 /O2 /DUNICODE /D_UNICODE /Fo..\msvc-build\ /Fe..\msvc-build\workstation_tests.exe workstation_tests.cpp sample.cpp mmap_streamer.cpp dsp.cpp audio.cpp asio_backend.cpp /link /SUBSYSTEM:CONSOLE mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib propsys.lib uuid.lib winmm.lib shell32.lib ksuser.lib advapi32.lib user32.lib gdi32.lib
