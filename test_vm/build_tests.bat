@echo off
REM Unit test build script for Bottle VM
REM Uses llvm-mingw for native Windows compilation

echo Building Bottle VM unit tests...

set COMPILER=D:\llvm-mingw-20260421-msvcrt-x86_64\bin\clang++.exe

if not exist "%COMPILER%" (
    echo ERROR: Compiler not found at %COMPILER%
    echo Please update the COMPILER path in this script
    exit /b 1
)

echo Using llvm-mingw...
echo.

REM Clean previous build
if exist unit_tests.exe del unit_tests.exe

REM Compile unit tests
%COMPILER% -std=c++17 -I. -I../include -o unit_tests.exe unit_tests.cpp ../src/bottle_compiler.cpp ../src/bottle_vm.cpp bottle_builtins_mock.cpp ../src/bottle_error.cpp

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Compilation failed!
    exit /b 1
)

echo.
echo Compilation successful!
echo Running unit tests...
echo.

REM Run the tests
unit_tests.exe

set TEST_RESULT=%ERRORLEVEL%

echo.
if %TEST_RESULT% EQU 0 (
    echo All tests passed!
) else (
    echo Some tests failed!
)

exit /b %TEST_RESULT%
