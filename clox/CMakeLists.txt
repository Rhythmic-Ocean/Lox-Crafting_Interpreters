cmake_minimum_required(VERSION 3.10)
project(clox C)

# Enable AddressSanitizer in debug builds
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "AddressSanitizer enabled for Debug build")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif()

file(GLOB SOURCES "src/*.c")

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/include)
