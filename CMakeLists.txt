cmake_minimum_required(VERSION 3.16)
project(cleanroom_src CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Engine core. The Android build will pull this in with add_subdirectory().
add_library(core STATIC src/vpk.cpp src/filesystem.cpp src/bsp.cpp)
target_include_directories(core PUBLIC src)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Desktop tools and tests.
if(NOT ANDROID)
  add_executable(vpktool tools/vpktool.cpp)
  add_executable(bsp2obj tools/bsp2obj.cpp)
  add_executable(fs_test tests/fs_test.cpp)
  foreach(t vpktool bsp2obj fs_test)
    target_link_libraries(${t} PRIVATE core)
  endforeach()
endif()
