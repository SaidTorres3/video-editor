cmake_minimum_required(VERSION 3.10)
project(MiVentana CXX)

add_executable(MiVentana WIN32 main.cpp)

# Link against the Windows libraries
target_link_libraries(MiVentana PRIVATE user32 gdi32)