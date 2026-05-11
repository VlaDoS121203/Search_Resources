cmake_minimum_required(VERSION 3.16)
project(QtLocalSearch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Требуется Widgets + Network
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

qt_add_executable(QtLocalSearch main.cpp)

target_link_libraries(QtLocalSearch PRIVATE Qt6::Widgets Qt6::Network)