cmake_minimum_required(VERSION 3.16)

project(MrHenderson VERSION 0.1.0 LANGUAGES CXX)

add_executable(MrHenderson src/main.cpp)
target_compile_features(MrHenderson PRIVATE cxx_std_17)

if (MSVC)
    target_compile_options(MrHenderson PRIVATE /O2 /W4)
else()
    target_compile_options(MrHenderson PRIVATE -O3 -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(search_tests tests/search_tests.cpp)
    target_compile_features(search_tests PRIVATE cxx_std_17)
    add_test(NAME search_regressions COMMAND search_tests)
endif()
