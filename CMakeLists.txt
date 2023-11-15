cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(FFmpegLearn LANGUAGES CXX)

if(CMAKE_BUILD_TYPE STREQUAL Debug)
    message(STATUS "Debug Mode.")
    add_definitions(-DDebug)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -g")
else()
    message(STATUS "Release Mode.")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()


# --------------------------------------------------------------------------
# FFmpeg library
set(ffmpeg_include_dir /usr/local/ffmpeg/include)
include_directories(${ffmpeg_include_dir})

set(ffmpeg_lib_dir /usr/local/ffmpeg/lib)
file(GLOB ffmpeg_solibs ${ffmpeg_lib_dir}/*.so)
link_directories(${ffmpeg_lib_dir})

message(${ffmpeg_solibs})


# --------------------------------------------------------------------------
# Project files
include_directories(${PROJECT_SOURCE_DIR}/inc)

set(demo_dir ${PROJECT_SOURCE_DIR}/demo)
file(GLOB demo_codes ${demo_dir}/*.cpp)

set(core_dir ${PROJECT_SOURCE_DIR}/src)

set(core_codes "")
file(GLOB_RECURSE core_codes ${core_dir}/*.cpp)


# --------------------------------------------------------------------------
#
# add_subdirectory(./src)
# add_subdirectory(./demo)

foreach (demo ${demo_codes})
    get_filename_component(demo_basename ${demo} NAME_WE)
    add_executable(${demo_basename} ${demo} ${core_codes})
    target_link_libraries(${demo_basename} PRIVATE ${ffmpeg_solibs})
endforeach()
