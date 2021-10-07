cmake_minimum_required(VERSION 3.1)
project(ExternalScanControl)

add_executable (ExternalScan main.cpp)
set_property(TARGET ExternalScan PROPERTY CXX_STANDARD 11)

find_path(NIDAQmx_INCLUDE_DIR NIDAQmx.h ${CMAKE_CURRENT_SOURCE_DIR})
find_library(NIDAQmx_LIBRARY NIDAQmx ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${NIDAQmx_INCLUDE_DIR})
target_link_libraries(ExternalScan ${NIDAQmx_LIBRARY})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/fftw)	
find_library(FFTW_LIBRARY_1 libfftw3-3 ${CMAKE_CURRENT_SOURCE_DIR}/fftw)
find_library(FFTW_LIBRARY_2 libfftw3f-3 ${CMAKE_CURRENT_SOURCE_DIR}/fftw)
find_library(FFTW_LIBRARY_3 libfftw3l-3 ${CMAKE_CURRENT_SOURCE_DIR}/fftw)
target_link_libraries(ExternalScan ${FFTW_LIBRARY_1} ${FFTW_LIBRARY_2} ${FFTW_LIBRARY_3})