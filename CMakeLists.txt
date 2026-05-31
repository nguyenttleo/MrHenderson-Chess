cmake_minimum_required(VERSION 3.16)

project(MrHenderson VERSION 0.1.0 LANGUAGES CXX)

add_executable(MrHenderson src/main.cpp)
target_compile_features(MrHenderson PRIVATE cxx_std_17)

if (MSVC)
    target_compile_options(MrHenderson PRIVATE /O2 /W4)
else()
    target_compile_options(MrHenderson PRIVATE -O3 -Wall -Wextra -Wpedantic)
endif()
