cmake_minimum_required(VERSION 3.0.0)
project(paxos_lock VERSION 0.1.0)

find_package( 
    Boost 1.75 REQUIRED 
    COMPONENTS  json
)

set(CMAKE_BUILD_TYPE Debug)

include_directories(../json5/include)
include_directories(include)

add_executable(analyze_pdb_dump src/analyze_pdb_dump.cpp)

target_sources(analyze_pdb_dump PRIVATE
    src/pdb_analyzer.cpp
    src/pdb_results.cpp
    src/ctags_reader.cpp)

target_link_libraries(analyze_pdb_dump ${Boost_LIBRARIES})

target_compile_features(analyze_pdb_dump PRIVATE cxx_std_17)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
