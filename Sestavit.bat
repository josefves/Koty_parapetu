@echo off
rem Sestaveni doplnku KotyParapetu pro Archicad 30 (Windows)
rem Potrebuje: Visual Studio 2022 (C++), CMake a Python 3.10+
cd /d "%~dp0"
python Tools\BuildAddOn.py --configFile config.json --acVersion 30 --buildConfig RelWithDebInfo
if errorlevel 1 (
    echo.
    echo Sestaveni selhalo. Chybove hlaseni najdete vyse.
    pause
    exit /b 1
)
for /r Build %%f in (KotyParapetu.apx) do if exist "%%f" copy /y "%%f" "%~dp0KotyParapetu.apx" >nul
echo.
echo Hotovo: %~dp0KotyParapetu.apx
pause
