cmake_minimum_required(VERSION 3.22)
project(mrgingham LANGUAGES CXX VERSION 1.0.0)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenCV
find_package(OpenCV REQUIRED)
include_directories("${OpenCV_INCLUDE_DIRS}")

FIND_PACKAGE( Boost 1.52 )
INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR} )

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
target_include_directories(mrgingham PUBLIC ${PROJECT_SOURCE_DIR} ${JNI_INCLUDE_DIRS})
target_link_libraries(mrgingham PUBLIC ${OpenCV_LIBS} ${Boost_LIBRARIES}) 
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
