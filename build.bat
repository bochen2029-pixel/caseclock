@echo off
rem caseclock build - MSVC (VS2022), static CRT, /W4 /WX, then the import-table gate.
rem Produces caseclock.exe (console: every mode) and caseclockw.exe (windows subsystem: the strip,
rem no console flash - double-click or pin it to the taskbar).
setlocal enabledelayedexpansion
where cl >nul 2>nul
if errorlevel 1 (
  set "VCV=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  if not exist "!VCV!" (
    set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    for /f "usebackq tokens=*" %%i in (`""!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath"`) do set "VSDIR=%%i"
    set "VCV=!VSDIR!\VC\Auxiliary\Build\vcvars64.bat"
  )
  if not exist "!VCV!" ( echo build: vcvars64.bat not found & exit /b 1 )
  call "!VCV!" >nul
)
cd /d "%~dp0"
if not exist obj mkdir obj
set CXXFLAGS=/nologo /c /std:c++20 /O2 /W4 /WX /permissive- /EHsc /utf-8 /MT /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Fo:obj\
set SRCS=src\caseclock.cpp src\strip.cpp src\closure.cpp src\json.cpp src\hash.cpp src\sys.cpp src\tape.cpp src\casefile.cpp src\clock.cpp src\signout.cpp
set OBJS=obj\caseclock.obj obj\strip.obj obj\closure.obj obj\json.obj obj\hash.obj obj\sys.obj obj\tape.obj obj\casefile.obj obj\clock.obj obj\signout.obj
rem OS APIs only: user32 gdi32 gdiplus crypt32 (DPAPI, base64) bcrypt (SHA-256) shell32 (tray, print verb); kernel32 implied.
set LIBS=user32.lib gdi32.lib gdiplus.lib crypt32.lib bcrypt.lib shell32.lib
cl %CXXFLAGS% %SRCS% || exit /b 1
rc /nologo /fo obj\caseclock.res src\caseclock.rc || exit /b 1
link /nologo /SUBSYSTEM:CONSOLE /OUT:caseclock.exe %OBJS% obj\caseclock.res %LIBS% || exit /b 1
link /nologo /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup /OUT:caseclockw.exe %OBJS% obj\caseclock.res %LIBS% || exit /b 1

rem The gate: no network stack, by construction. The build fails if the exe imports any of these.
set BAD=0
for %%E in (caseclock.exe caseclockw.exe) do (
  dumpbin /nologo /imports %%E > obj\imports-%%E.txt || exit /b 1
  for %%D in (ws2_32 wininet winhttp urlmon dnsapi iphlpapi wsock32 mswsock winrt) do (
    findstr /i /c:"%%D.dll" obj\imports-%%E.txt >nul && ( echo GATE FAILED: %%E imports %%D.dll & set BAD=1 )
  )
)
if "%BAD%"=="1" exit /b 1
echo imports (caseclock.exe):
findstr /i /r /c:"^ *[a-z0-9_-]*\.dll" obj\imports-caseclock.exe.txt
echo OK: caseclock.exe caseclockw.exe - no network DLL in the import table
