cmake_minimum_required(VERSION 3.23)
project(mrgingham LANGUAGES CXX VERSION 1.0.0)

# C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenCV
find_package(OpenCV REQUIRED)
include_directories("${OpenCV_INCLUDE_DIRS}")

FIND_PACKAGE( Boost 1.52 )
INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR} )

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
)

# Library
add_library(mrgingham_lib ${INCLUDE_HPP} ${SRC_HPP} ${SRC_CPP})
target_include_directories(mrgingham_lib PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(mrgingham_lib PUBLIC ${OpenCV_LIBS} ${Boost_LIBRARIES}) 
set_target_properties(mrgingham_lib PROPERTIES LINKER_LANGUAGE CXX)

if (MSVC)
    add_executable(mrgingham_bin mrgingham-from-image_win.cc)
else()
    add_executable(mrgingham_bin mrgingham-from-image.cc)
endif()
target_link_libraries(mrgingham_bin PUBLIC mrgingham_lib) 

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

add_executable(opencv_test opencv_test.cc)
target_link_libraries(opencv_test PUBLIC ${OpenCV_LIBS}) 
