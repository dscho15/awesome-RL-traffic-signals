cmake_minimum_required(VERSION 3.16)
project(auction_tsc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED ENV{SUMO_HOME})
  message(FATAL_ERROR "SUMO_HOME is not set. Export SUMO_HOME to your SUMO installation.")
endif()

set(SUMO_HOME $ENV{SUMO_HOME})

find_library(SUMO_LIBTRACI NAMES traci libtraci PATHS
  "${SUMO_HOME}/lib"
  "${SUMO_HOME}/lib64"
  "${SUMO_HOME}/bin"
)

if(NOT SUMO_LIBTRACI)
  message(FATAL_ERROR "Could not find libtraci. Ensure SUMO is built with libtraci and SUMO_HOME is correct.")
endif()

add_executable(auction_tsc
  src/main.cpp
  src/auction_controller.cpp
)

target_include_directories(auction_tsc PRIVATE
  "${SUMO_HOME}/src"
)

target_link_libraries(auction_tsc PRIVATE ${SUMO_LIBTRACI})
