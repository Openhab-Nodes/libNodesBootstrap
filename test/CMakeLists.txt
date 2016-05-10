## Compile for posix with posix sockets
## We actually do not use the cmake test system (we would use add_test for each test suite)
## but the gtest framework. We therefore add only "one" test case here, which internally
## performs many gtest testcases.

cmake_minimum_required(VERSION 3.1)
project(boostrapWifiTests C CXX)

SET(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(TEST_DIR ${REPO_ROOT}/test)

include("${REPO_ROOT}/src/boostrapWifi.cmake")

enable_testing()

add_compile_options(-pedantic-errors -ansi -Wextra -Wall -Wuninitialized -Wmissing-declarations -Wno-missing-field-initializers)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-elide-constructors -Woverloaded-virtual")

## Prepare gtest
if(EXISTS "${GTEST_DIR}")
    set(GTEST_DIR $ENV{GTEST_DIR} CACHE PATH "")
    set(GTEST_FILES ${GTEST_DIR}/src/gtest-all.cc ${GTEST_DIR}/src/gtest_main.cc)
    set(GTEST_INCLUDE_DIRS ${GTEST_DIR} ${GTEST_DIR}/include)
    add_definitions(-DGTEST_HAS_PTHREAD=0)
else()
    find_package(GTest REQUIRED)
    set(GTEST_FILES "")
endif()

## Prepare for coverage measuring
if(${COVERAGE})
    add_compile_options(-g -O0 -fprofile-arcs -ftest-coverage)
endif()

# All cpp files in this directory are considered testcase files.
file(GLOB TESTS_FILES ${TEST_DIR}/*.cpp ${TEST_DIR}/*.c ${TEST_DIR}/*.h)

add_executable(${PROJECT_NAME} ${BOOTSTRAP_WIFI_SOURCES} ${TESTS_FILES} ${GTEST_FILES} )

# We want C11 and C++11
target_compile_features(${PROJECT_NAME} PRIVATE cxx_range_for)
set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD 11)

target_include_directories(${PROJECT_NAME} PRIVATE ${GTEST_INCLUDE_DIRS} ${BOOTSTRAP_WIFI_INCLUDE_DIRS})
target_compile_definitions(${PROJECT_NAME} PUBLIC ${BOOTSTRAP_DEFINITIONS})


add_test(${PROJECT_NAME} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${PROJECT_NAME})

if (UNIX)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

if(NOT EXISTS "${GTEST_DIR}")
    target_link_libraries(${PROJECT_NAME} ${GTEST_BOTH_LIBRARIES})
endif()

