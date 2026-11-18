@echo off
rem Configures and builds the shim. Output: build\bin\driver_vrlink.dll
rem Needs CMake 3.20+ and Visual Studio with the "Desktop development with C++" workload.
cmake -S "%~dp0." -B "%~dp0build" -A x64 || exit /b 1
cmake --build "%~dp0build" --config Release || exit /b 1
echo BUILD OK: build\bin\driver_vrlink.dll