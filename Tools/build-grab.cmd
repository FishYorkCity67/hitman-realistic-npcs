@echo off
rem Baut grab.exe (Bildschirmfotos per DXGI Desktop Duplication, klappt auch mit Vollbild-Spielen).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /O2 /EHsc /std:c++17 /utf-8 grab.cpp /Fe:grab.exe
exit /b %ERRORLEVEL%
