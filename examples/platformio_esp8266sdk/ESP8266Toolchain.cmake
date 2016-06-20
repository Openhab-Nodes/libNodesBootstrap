# this one is important
SET(CMAKE_SYSTEM_NAME Generic)
#this one not so much
SET(CMAKE_SYSTEM_VERSION 1)

INCLUDE(CMakeForceCompiler)

SET(CMAKE_C_COMPILER   "$ENV{HOME}/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-gcc")
SET(CMAKE_CXX_COMPILER "$ENV{HOME}/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-g++")

CMAKE_FORCE_C_COMPILER($ENV{HOME}/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-gcc GNU)
CMAKE_FORCE_CXX_COMPILER($ENV{HOME}/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-g++ GNU)

# specify the cross compiler

# where is the target environment 
#SET(CMAKE_FIND_ROOT_PATH  "$ENV{HOME}/.platformio/packages/toolchain-xtensa")

# search for programs in the build host directories
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# for libraries and headers in the target directories
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
