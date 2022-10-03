cmake_minimum_required(VERSION 3.23)
project(mrgingham LANGUAGES CXX VERSION 1.0.0)

# C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenCV
find_package(OpenCV REQUIRED)
include_directories("${OpenCV_INCLUDE_DIRS}")

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
add_library(mrgingham_lib STATIC ${INCLUDE_HPP} ${SRC_HPP} ${SRC_CPP})
target_include_directories(mrgingham_lib PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(mrgingham_lib PUBLIC ${OpenCV_LIBS}) 
set_target_properties(mrgingham_lib PROPERTIES LINKER_LANGUAGE CXX)

# # Binary
# add_executable(mrgingham_bin "mrgingham-from-image.cc")
# target_link_libraries(mrgingham_bin PRIVATE mrgingham_lib)