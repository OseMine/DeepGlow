@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d X:\coding\DeepGlow
cl /nologo /std:c++17 /O2 /EHsc /I src work\profile.cpp src\DGPipeline.cpp /Fe:work\profile.exe /Fo:work\
if errorlevel 1 exit /b 1
work\profile.exe
