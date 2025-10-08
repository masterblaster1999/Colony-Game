cmake_minimum_required(VERSION 3.16)

add_executable(poisson_scatter main.cpp)
target_link_libraries(poisson_scatter PRIVATE pcg_poisson)
target_compile_features(poisson_scatter PRIVATE cxx_std_17)
