# DPU Fabric tools (CLI + multiprocess coordinator/worker).

# CLI
add_executable(dpu_fabric_cli dpu_fabric_cli.cpp)
target_link_libraries(dpu_fabric_cli PRIVATE dpufabric_core dpufabric_synthetic dpufabric_system dpufabric_unsupported)
target_include_directories(dpu_fabric_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Multiprocess control-plane tools (need the transport library).
add_executable(dpu_fabric_coordinator dpu_fabric_coordinator.cpp)
target_link_libraries(dpu_fabric_coordinator PRIVATE dpufabric_core dpufabric_mp dpufabric_synthetic)
target_include_directories(dpu_fabric_coordinator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(dpu_fabric_worker dpu_fabric_worker.cpp)
target_link_libraries(dpu_fabric_worker PRIVATE dpufabric_core dpufabric_mp dpufabric_synthetic)
target_include_directories(dpu_fabric_worker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})