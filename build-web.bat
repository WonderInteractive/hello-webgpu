@echo off
rem Builds the Emscripten implementation (on Windows)
rem TODO: CMake...
rem 

if "%~1"=="/d" (
  set DEBUG=true
) else (
  set DEBUG=false
)

set CPP_FLAGS=-std=c++20 -Wcast-align -Wover-aligned -Wno-nonportable-include-path -fno-exceptions -fno-rtti
set EMS_FLAGS= --output_eol linux -lpthread -s USE_PTHREADS -s ALLOW_MEMORY_GROWTH=0  -s NO_EXIT_RUNTIME=1 -s NO_FILESYSTEM=1 -s USE_WEBGPU=1 -s PTHREAD_POOL_SIZE=4 -s PTHREAD_POOL_SIZE_STRICT=2 -s OFFSCREENCANVAS_SUPPORT=1
set OPT_FLAGS=

if %DEBUG%==true (
  set CPP_FLAGS=%CPP_FLAGS% -g3 -D_DEBUG=1
  set EMS_FLAGS=%EMS_FLAGS% -s ASSERTIONS=2 -s DEMANGLE_SUPPORT=1 -s SAFE_HEAP=1 -s STACK_OVERFLOW_CHECK=2
  set OPT_FLAGS=%OPT_FLAGS% -O0
) else (
  set CPP_FLAGS=%CPP_FLAGS% -g3 -DNDEBUG=1
  set EMS_FLAGS=%EMS_FLAGS% -g3 -s ASSERTIONS=2 -s EVAL_CTORS=0 -s SUPPORT_ERRNO=1 
  set OPT_FLAGS=%OPT_FLAGS% -O3
)

set SRC=
for %%f in (src/ems/*.cpp) do call set SRC=%%SRC%%src/ems/%%f 
for %%f in (src/*.cpp) do call set SRC=%%SRC%%src/%%f 

set INC=-Iinc

set OUT=out/web/index
if not exist out\web mkdir out\web

rem Grab the Binaryen path from the ".emscripten" file (which needs to have
rem been set). We then swap the Unix-style slashes.
rem 
for /f "tokens=*" %%t in ('em-config BINARYEN_ROOT') do (set BINARYEN_ROOT=%%t)
set "BINARYEN_ROOT=%BINARYEN_ROOT:/=\%"

%SystemRoot%\system32\cmd /c "em++ %CPP_FLAGS% %OPT_FLAGS% %EMS_FLAGS% %INC% %SRC% -o %OUT%.html"
set EMCC_ERR=%errorlevel%


if %EMCC_ERR%==0 (
  echo Success!
)
exit /b %EMCC_ERR%
