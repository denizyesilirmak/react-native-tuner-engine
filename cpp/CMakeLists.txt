cmake_minimum_required(VERSION 3.20)

project(TunerEngineCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(tuner_engine_core STATIC
    src/NoteMapper.cpp
    src/Window.cpp
    src/BiquadHpf.cpp
    src/SnrEstimator.cpp
    src/OnsetDetector.cpp
    src/PostProcessor.cpp
    src/Pipeline.cpp
    src/TunerEngine.cpp
    src/AudioFrameDispatcher.cpp
    src/CepstrumPitchDetector.cpp
    src/PyinPitchDetector.cpp
    src/DetectorFusion.cpp
    src/StringMatcher.cpp
)

target_include_directories(tuner_engine_core PUBLIC include)
