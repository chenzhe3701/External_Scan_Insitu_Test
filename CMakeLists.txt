cmake_minimum_required(VERSION 3.1)
project(ScanControl)

set(CMAKE_CXX_STANDARD 11)
add_executable(ScanControlTest main.cpp)
find_library(NI_LIBRARIES NIDAQmx ${CMAKE_CURRENT_SOURCE_DIR})

#set SFML_ROOT for module to work
set(SFML_ROOT "SFML_ROOT-NOTFOUND" CACHE FILEPATH "sfml install location")
set(CMAKE_MODULE_PATH "${SFML_ROOT}/cmake/Modules" ${CMAKE_MODULE_PATH})
find_package(SFML 2.3 REQUIRED system window graphics)
include_directories(${SFML_INCLUDE_DIR})

target_link_libraries(ScanControlTest ${NI_LIBRARIES} ${SFML_LIBRARIES})
