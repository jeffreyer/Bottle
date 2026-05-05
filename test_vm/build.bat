@echo off
REM Bottle VM test build script
REM Uses llvm-mingw for native Windows compilation

echo Building Bottle VM test...

set COMPILER=D:\llvm-mingw-20260421-msvcrt-x86_64\bin\clang++.exe

if not exist "%COMPILER%" (
    echo ERROR: Compiler not found at %COMPILER%
    echo Please update the COMPILER path in this script
    exit /b 1
)

echo Using llvm-mingw...
echo.

REM Clean previous build
if exist minimal_test.exe del minimal_test.exe

REM Compile the test
%COMPILER% -std=c++17 -I. -I../include -o minimal_test.exe minimal_test.cpp

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Compilation failed!
    exit /b 1
)

echo.
echo Compilation successful!
echo Running test...
echo.

REM Run the test
minimal_test.exe

exit /b %ERRORLEVEL%
