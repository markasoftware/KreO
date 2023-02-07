cmake_minimum_required(VERSION 3.0.0)
project(analysis VERSION 0.1.0)

find_package( 
    Boost 1.75 REQUIRED 
    COMPONENTS  json
)

set(CMAKE_BUILD_TYPE Debug)

include_directories(include)

add_executable(analyze_pdb_dump src/analyze_pdb_dump.cpp)
add_executable(evaluation src/evaluation.cpp)
add_executable(extract_gt_methods src/extract_gt_methods.cpp)

target_sources(analyze_pdb_dump PRIVATE
    src/utils.cpp
    src/pdb_organizer.cpp
    src/pdb_parser.cpp
    src/type_data_instances.cpp
    src/pdb_results_generator.cpp)

target_link_libraries(analyze_pdb_dump ${Boost_LIBRARIES})
target_link_libraries(evaluation ${Boost_LIBRARIES})
target_link_libraries(extract_gt_methods ${Boost_LIBRARIES})

target_compile_features(analyze_pdb_dump PRIVATE cxx_std_17)
target_compile_features(evaluation PRIVATE cxx_std_17)
target_compile_features(extract_gt_methods PRIVATE cxx_std_17)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
