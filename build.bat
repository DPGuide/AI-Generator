@echo off
:: Pfad für VS 2022 Professional
set "VS_ENV=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VS_ENV%" (
    echo [ERROR] vcvars64.bat nicht gefunden unter %VS_ENV%
    pause
    exit
)

call "%VS_ENV%"

echo [BASTION] Kompiliere vid_ki.cpp...
cl.exe /EHsc /std:c++17 vid_ki.cpp /I"C:\Pfad\Zu\Gdiplus" /link /OUT:BastionGen.exe user32.lib gdi32.lib gdiplus.lib comdlg32.lib shlwapi.lib

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] BastionGen.exe wurde erstellt!
) else (
    echo [FAIL] Fehler beim Kompilieren.
)
pause