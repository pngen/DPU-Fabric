# DPU Fabric examples (link core + synthetic backend).

add_executable(basic_device_registry basic_device_registry.cpp)
target_link_libraries(basic_device_registry PRIVATE dpufabric_core dpufabric_synthetic)
target_include_directories(basic_device_registry PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

add_executable(capability_query capability_query.cpp)
target_link_libraries(capability_query PRIVATE dpufabric_core dpufabric_synthetic)
target_include_directories(capability_query PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

add_executable(offload_selection offload_selection.cpp)
target_link_libraries(offload_selection PRIVATE dpufabric_core dpufabric_synthetic)
target_include_directories(offload_selection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

add_executable(explicit_fallback explicit_fallback.cpp)
target_link_libraries(explicit_fallback PRIVATE dpufabric_core dpufabric_synthetic)
target_include_directories(explicit_fallback PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

add_executable(deployment_lifecycle deployment_lifecycle.cpp)
target_link_libraries(deployment_lifecycle PRIVATE dpufabric_core dpufabric_synthetic)
target_include_directories(deployment_lifecycle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

add_executable(stale_authority_rejection stale_authority_rejection.cpp)
target_link_libraries(stale_authority_rejection PRIVATE dpufabric_core dpufabric_synthetic)
target_include_directories(stale_authority_rejection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

add_executable(synthetic_multi_dpu synthetic_multi_dpu.cpp)
target_link_libraries(synthetic_multi_dpu PRIVATE dpufabric_core dpufabric_synthetic)
target_include_directories(synthetic_multi_dpu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

add_executable(persistence_recovery persistence_recovery.cpp)
target_link_libraries(persistence_recovery PRIVATE dpufabric_core dpufabric_synthetic)
target_include_directories(persistence_recovery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)