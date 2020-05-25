cmake_minimum_required(VERSION 3.16)
project(sk_radio)

set(CMAKE_CXX_STANDARD 17)

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost COMPONENTS program_options REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

add_executable(sk_radio src/radio-proxy.cpp src/err.cpp src/err.h)
target_link_libraries(sk_radio ${Boost_LIBRARIES})