@ECHO OFF
REM Copyright (C) 2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
REM
REM This software comes with ABSOLUTELY NO WARRANTY; for details of
REM the license terms, see the LICENSE.txt file included with the program.

SET ICONS=simple
SET OUTDIR=%1
SET INTDIR=%1\icons
SET ICONSDIR=..\icons

IF "%2"=="clean" GOTO clean

cl /O1 /Oi /Os /Fo%INTDIR%\main.obj /c %ICONSDIR%\main.cpp
IF ERRORLEVEL 1 GOTO abort
FOR %%I IN (%ICONS%) DO CALL :build %%I
EXIT

:build
rc /I%ICONSDIR%\%1 /Fo%INTDIR%\%1.res %ICONSDIR%\%1\icon.rc
IF ERRORLEVEL 1 GOTO abort
link /DLL /OPT:REF /OPT:ICF /OUT:%OUTDIR%\%1.ico %INTDIR%\main.obj %INTDIR%\%1.res
IF ERRORLEVEL 1 GOTO abort
EXIT /B

:clean
DEL /Q %INTDIR%\*.obj
DEL /Q %INTDIR%\*.res
FOR %%I IN (%ICONS%) DO DEL /Q %OUTDIR%\%%I.ico
EXIT

:abort
EXIT 1
