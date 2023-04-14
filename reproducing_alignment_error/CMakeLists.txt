# Set project-level settings and specs
cmake_minimum_required(VERSION 3.20)

project("example")

# We are going to be fetching content, so include tools to do so.
include(FetchContent)
find_library(LIBURING_LIBRARY NAMES uring)

add_executable(example main.cpp)

target_link_libraries(
    example 
    PUBLIC 
    ${LIBURING_LIBRARY}
)

target_include_directories(
    example 
    PUBLIC 
    ${LIBURING_INCLUDE_DIR} 
)