cmake_minimum_required(VERSION 3.20)
project(dpi_aware_test LANGUAGES CXX)

add_executable(dpi_aware_test dpi_aware_test.cpp)
target_compile_features(dpi_aware_test PRIVATE cxx_std_17)
target_compile_definitions(dpi_aware_test PRIVATE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_link_libraries(dpi_aware_test PRIVATE User32 Gdi32 Shcore)
