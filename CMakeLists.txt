cmake_minimum_required(VERSION 3.20)
project(pathfinder-single)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Geode REQUIRED CONFIG)

geode_add_library(pathfinder-single
    src/main.cpp
)

geode_install_mod(pathfinder-single)
