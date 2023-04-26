cmake_minimum_required(VERSION 3.0.0)
project(analysis VERSION 0.1.0)

# 1.78 if windows, switch to 1.75 if on Linux
find_package( 
    Boost 1.78 REQUIRED 
    # uncomment if on Linux
    # COMPONENTS  json
)

set(CMAKE_BUILD_TYPE Debug)

include_directories(include)

include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

add_executable(analyze_pdb_dump src/analyze_pdb_dump.cpp)

target_sources(analyze_pdb_dump PRIVATE
    src/analyze_pdb_dump.cpp
    src/pdb_organizer.cpp
    src/pdb_parser.cpp
    src/pdb_results_generator.cpp
    src/type_data_instances.cpp
    src/utils.cpp)

target_link_libraries(analyze_pdb_dump ${Boost_LIBRARIES})

target_compile_features(analyze_pdb_dump PRIVATE cxx_std_20)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
