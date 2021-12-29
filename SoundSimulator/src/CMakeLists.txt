cmake_minimum_required(VERSION 3.17)

set(CMAKE_CXX_STANDARD 20)

# Attention! You should revise your SDL library path here!
set(SDL2_PATH D:/Program/211222learning/C01_SDL_Loop/SDL2/SDL2-2.0.18/x86_64-w64-mingw32)
set(SDL2_INCLUDE ${SDL2_PATH}/include/SDL2)
set(SDL2_LIB ${SDL2_PATH}/lib)
include_directories(${SDL2_INCLUDE})

# The main project
project(C06)
add_executable(C06 cpp/C06.cpp test1.rc)
target_link_libraries(C06 -lmingw32 ${SDL2_LIB}/libSDL2main.a ${SDL2_LIB}/libSDL2.dll.a wsock32 ws2_32)