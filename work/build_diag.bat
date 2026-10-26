@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d X:\coding\DeepGlow
cl /nologo /std:c++17 /O2 /EHsc /I src work\diag_text.cpp src\DGPipeline.cpp /Fe:work\diag_text.exe /Fo:work\
if errorlevel 1 exit /b 1
mkdir smoke_out 2>nul
work\diag_text.exe
if errorlevel 1 exit /b 1
ffmpeg -y -loglevel error -i smoke_out\diag_512x288.ppm smoke_out\diag_512x288.png
ffmpeg -y -loglevel error -i smoke_out\diag_512x512.ppm smoke_out\diag_512x512.png
