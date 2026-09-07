add_executable(bench_core bench_core.cpp)
target_link_libraries(bench_core PRIVATE dpufabric_core dpufabric_synthetic)
target_include_directories(bench_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)