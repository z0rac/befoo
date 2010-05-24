@ECHO OFF
REM Copyright (C) 2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
REM
REM This software comes with ABSOLUTELY NO WARRANTY; for details of
REM the license terms, see the LICENSE.txt file included with the program.

SETLOCAL
SET OUTDIR=%1
SET INTDIR=%1\icons
SET ICONSDIR=..\icons

SHIFT
IF "%1"=="clean" GOTO clean
IF "%1"=="build" GOTO build
EXIT 1

:build
SHIFT
IF "%1"=="" EXIT
rc /I%ICONSDIR% /Fo%INTDIR%\%1.res %ICONSDIR%\%1.rc
IF ERRORLEVEL 1 EXIT 1
link /DLL /NOENTRY /MACHINE:X86 /OUT:%OUTDIR%\%1.ico %INTDIR%\%1.res
IF ERRORLEVEL 1 EXIT 1
GOTO build

:clean
SHIFT
IF "%1"=="" EXIT
DEL /Q %INTDIR%\%1.res %OUTDIR%\%1.ico
GOTO clean
