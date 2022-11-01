cmake_minimum_required(VERSION 3.0.0)
project(paxos_lock VERSION 0.1.0)

set(CMAKE_BUILD_TYPE Debug)

add_executable(analyze_pdb_dump analyze_pdb_dump.cpp)

target_compile_features(analyze_pdb_dump PRIVATE cxx_std_17)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
