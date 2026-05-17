cmake_minimum_required(VERSION 3.22.1)

project(tunerengine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pull in the shared C++ core
add_subdirectory(
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../cpp
  ${CMAKE_BINARY_DIR}/tuner_engine_core
)

find_package(oboe REQUIRED CONFIG)

add_library(tunerengine SHARED
  TunerEngineJni.cpp
  OboeAudioSource.cpp
)

target_include_directories(tunerengine PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(tunerengine
  PRIVATE
    tuner_engine_core
    oboe::oboe
    android
    log
)
