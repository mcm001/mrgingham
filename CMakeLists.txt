cmake_minimum_required(VERSION 3.18)
project(mrgingham LANGUAGES CXX VERSION 1.0.0)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenCV
#set(OpenCV_STATIC ON)
#find_package(OpenCV REQUIRED)
#include_directories("${OpenCV_INCLUDE_DIRS}")

# Grab boost
include(FetchContent)

FetchContent_Declare(
  boost_polygon
  GIT_REPOSITORY https://github.com/boostorg/polygon
  GIT_TAG 8ba35b57c1436c4b36f7544aadd78c2b24acc7db
)
FetchContent_MakeAvailable(boost_polygon)

FetchContent_Declare(
  boost_config
  GIT_REPOSITORY https://github.com/boostorg/config
  GIT_TAG a1cf5d531405e62927b0257b5cb
)
FetchContent_MakeAvailable(boost_config)

set(OPENCV_YEAR "frc2023")
set(OPENCV_VERSION "4.6.0-5")
# set(OPENCV_ARCH "linuxx86-64")

# type can be "", "debug", "static", or "staticdebug"
set(OPENCV_TYPE "static")

# Download opencv, and save the path
FetchContent_Declare(
    opencv_lib
    URL https://frcmaven.wpi.edu/artifactory/release/edu/wpi/first/thirdparty/${OPENCV_YEAR}/opencv/opencv-cpp/${OPENCV_VERSION}/opencv-cpp-${OPENCV_VERSION}-${OPENCV_ARCH}${OPENCV_TYPE}.zip
)
FetchContent_MakeAvailable(opencv_lib)

# download OpenCV headers
FetchContent_Declare(
    opencv_header
    URL https://frcmaven.wpi.edu/artifactory/release/edu/wpi/first/thirdparty/${OPENCV_YEAR}/opencv/opencv-cpp/${OPENCV_VERSION}/opencv-cpp-${OPENCV_VERSION}-headers.zip
)
FetchContent_MakeAvailable(opencv_header)


# This probably doesn't work great with shared libraries, but I don't care about those right now
file(GLOB_RECURSE OPENCV_LIB_PATH 
  "${opencv_lib_SOURCE_DIR}/**/*"
)
SET(OPENCV_INCLUDE_PATH ${opencv_header_SOURCE_DIR})

message("OpenCV lib path: ${OPENCV_LIB_PATH}")


find_package(JNI)
if (JNI_FOUND)
    # Fixes odd AWT dependency
    set (JNI_INCLUDE_DIRS ${JAVA_INCLUDE_PATH} ${JAVA_INCLUDE_PATH2})
    message (STATUS "JNI_INCLUDE_DIRS=${JNI_INCLUDE_DIRS}")
endif()

# headers and sources
set(
    INCLUDE_HPP
)
set(
    SRC_HPP
    "ChESS.h"
    "mrgingham-internal.h"
    "find_blobs.hh"
    "find_chessboard_corners.hh"
    "mrgingham.hh"
    "point.hh"
)
set(
    SRC_CPP
    "ChESS.cc"
    "find_blobs.cc"
    "find_chessboard_corners.cc"
    "find_grid.cc"
    "mrgingham.cc"
    "MrginghamJNI.cpp"
)

# Library
add_library(mrgingham ${INCLUDE_HPP} ${SRC_HPP} ${SRC_CPP})
target_include_directories(mrgingham PUBLIC ${PROJECT_SOURCE_DIR} ${JNI_INCLUDE_DIRS} ${OPENCV_INCLUDE_PATH})
target_link_libraries(mrgingham PUBLIC ${OpenCV_LIBS} Boost::polygon ${OPENCV_LIB_PATH})
set_target_properties(mrgingham PROPERTIES LINKER_LANGUAGE CXX)

if (MSVC)
    add_executable(mrgingham_bin mrgingham-from-image_win.cc)
else()
    add_executable(mrgingham_bin mrgingham-from-image.cc)
endif()
target_link_libraries(mrgingham_bin PUBLIC mrgingham) 

# Gennerate usage header
find_package( PythonInterp 3 REQUIRED )
add_custom_target(
 generate_usage ALL
 COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_usage.py 
 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
 BYPRODUCTS mrgingham.usage.h
 COMMENT "Generating usage header"
)
add_dependencies(mrgingham_bin generate_usage)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(mrgingham_bin PRIVATE Threads::Threads)

# add_executable(opencv_test opencv_test.cc)
# target_link_libraries(opencv_test PUBLIC ${OpenCV_LIBS}) 
