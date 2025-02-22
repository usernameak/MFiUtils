cmake_minimum_required(VERSION 3.27)
project(MFiUtils)

set(CMAKE_CXX_STANDARD 17)

add_executable(MFi2MIDI main.cpp)

target_compile_definitions(MFi2MIDI PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(MFi2MIDI PRIVATE ws2_32)
