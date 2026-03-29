cmake_minimum_required(VERSION 4.2)
project(Homework_1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Homework_1
        main.cpp
)

find_package(Boost REQUIRED COMPONENTS thread)
target_link_libraries(Homework_1 PRIVATE Boost::thread)
