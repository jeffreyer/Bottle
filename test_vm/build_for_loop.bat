@echo off
setlocal

set COMPILER=D:\llvm-mingw-20260421-msvcrt-x86_64\bin\clang++.exe

echo Building for-loop test...

"%COMPILER%" ^
  -std=c++17 ^
  -I. ^
  -I../include ^
  -o test_for_loop.exe ^
  test_for_loop.cpp ^
  ../src/bottle_compiler.cpp ^
  ../src/bottle_vm.cpp ^
  ../src/bottle_error.cpp ^
  bottle_builtins_mock.cpp

if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b 1
)

echo Build successful! Running test...
echo.
test_for_loop.exe

endlocal
