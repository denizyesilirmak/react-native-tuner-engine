cmake_minimum_required(VERSION 3.20)

project(TunerEngineCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(tuner_engine_core
    ../src/NoteMapper.cpp
    ../src/YinPitchDetector.cpp
    ../src/TunerEngine.cpp
)

target_include_directories(tuner_engine_core PUBLIC ../include)

add_executable(tuner_engine_tests
    main.cpp
)

target_link_libraries(tuner_engine_tests PRIVATE tuner_engine_core)
