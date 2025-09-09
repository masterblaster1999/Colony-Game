:: From your project root (where CMakeLists.txt is)
mkdir build && cd build
cmake -G "Visual Studio 17 2022" ..
cmake --build . --config Release
.\Release\colonygame.exe --res 1920x1080 --fullscreen --vsync true --profile Commander --seed 1234 --config "%APPDATA%\MarsColonySim\settings.ini"
