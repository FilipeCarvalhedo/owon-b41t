cmake_minimum_required(VERSION 2.4)

project(projectb41t)
add_definitions("-Wall -O2 -std=c++17")

add_executable(owoncli owoncli.cpp data_parser.cpp)
