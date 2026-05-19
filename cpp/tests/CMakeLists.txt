cmake_minimum_required(VERSION 3.20)

project(TunerEngineTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(.. ${CMAKE_BINARY_DIR}/tuner_engine_core)

add_executable(tuner_engine_tests
    main.cpp
)

target_link_libraries(tuner_engine_tests PRIVATE tuner_engine_core)

enable_testing()
add_test(NAME tuner_engine_tests COMMAND tuner_engine_tests)
