cmake_minimum_required(VERSION 3.2)

include(CMakeListsPrivate.txt)

set(CMAKE_TOOLCHAIN_FILE ESP8266Toolchain.cmake) # Toolchain

project(plattformio_esp8266sdk) #CPP C

add_definitions(-DF_CPU=80000000L)
add_definitions(-D__ets__)
add_definitions(-DICACHE_FLASH)
add_definitions(-DESP8266)
add_definitions(-DARDUINO_ARCH_ESP8266)
add_definitions(-DARDUINO_ESP8266_ESP12)
add_definitions(-DARDUINO=20100)
add_definitions(-DPLATFORMIO=020806)

add_compile_options(-fno-elide-constructors -pedantic-errors -ansi -Wextra -Wall
    -Woverloaded-virtual -Wuninitialized -Wmissing-declarations -Wno-missing-field-initializers)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11")

add_custom_target(
    PLATFORMIO_BUILD ALL
    COMMAND ${PLATFORMIO_CMD} -f -c clion run
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES "src/main.cpp" "platformio.ini"
)

add_custom_target(
    PLATFORMIO_UPLOAD ALL
    COMMAND ${PLATFORMIO_CMD} -f -c clion run --target upload
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(
    PLATFORMIO_CLEAN
    COMMAND ${PLATFORMIO_CMD} -f -c clion run --target clean
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(
    PLATFORMIO_PROGRAM
    COMMAND ${PLATFORMIO_CMD} -f -c clion run --target program
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(
    PLATFORMIO_UPLOADFS
    COMMAND ${PLATFORMIO_CMD} -f -c clion run --target uploadfs
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(
    PLATFORMIO_UPDATE_ALL
    COMMAND ${PLATFORMIO_CMD} -f -c clion update
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

#add_executable(${PROJECT_NAME}
#    src/main.cpp
#)

#target_compile_features(${PROJECT_NAME} PRIVATE cxx_range_for)

## We want C99 and C++11
#set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
#set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 14)
#set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD 11)
