@echo off

set SUBTARGET=mame
set SUFFIX=""

set COMMONFLAGS=SUBTARGET=%SUBTARGET% MSVC_BUILD=1 MAXOPT= PREFIX= SUFFIX=%SUFFIX% WINUI=

set MINGW_ROOT=..\mingw\mingw64-w64
set PATH=%MINGW_ROOT%\bin;%PATH%

set PSDK_DIR=%ProgramFiles%\Microsoft SDKs\Windows\v7.0A
set PATH=%PSDK_DIR%\bin\;%PATH%
set INCLUDE=extravc\include\;%PSDK_DIR%\Include\
set LIB=extravc\lib\;extravc\lib\x64;%PSDK_DIR%\Lib\

call "%VS100COMNTOOLS%\..\..\VC\bin\amd64\vcvarsamd64.bat"

gcc -v

make %COMMONFLAGS% maketree obj/windows/mamep%SUFFIX%/osd/windows/vconv.exe
make %COMMONFLAGS% NO_FORCEINLINE=1 obj/windows/mamep%SUFFIX%/emu/cpu/m6809/m6809.o
make %COMMONFLAGS% NO_FORCEINLINE=1 obj/windows/mamep%SUFFIX%/emu/cpu/mips/mips3drc.o
make %COMMONFLAGS% OPTIMIZE=ng obj/windows/mamep%SUFFIX%/emu/mconfig.o

make %COMMONFLAGS% -j3 >compile.log
pause
