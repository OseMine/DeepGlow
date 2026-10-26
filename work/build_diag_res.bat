@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d X:\coding\DeepGlow
cl /nologo /std:c++17 /O2 /EHsc /I src work\diag_res.cpp src\DGPipeline.cpp /Fe:work\diag_res.exe /Fo:work\
if errorlevel 1 exit /b 1
work\diag_res.exe
if errorlevel 1 exit /b 1
ffmpeg -y -loglevel error -i smoke_out\our_1920x1080.ppm smoke_out\our_1920x1080.png
