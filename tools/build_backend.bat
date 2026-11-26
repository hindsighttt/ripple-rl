@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0..\backend-cpp"
if not exist "build\CMakeCache.txt" (
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 exit /b 1
)
cmake --build build --config Release
