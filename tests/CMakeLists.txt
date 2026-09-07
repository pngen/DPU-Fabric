# DPU Fabric tests.

set(DPUFABRIC_TEST_TARGETS test_core test_eligibility test_deployment test_persistence
    test_protocol test_synthetic test_authority test_adversarial test_property
    test_concurrency)

function(add_dpu_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE dpufabric_core dpufabric_synthetic dpufabric_system dpufabric_unsupported)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

foreach(t ${DPUFABRIC_TEST_TARGETS})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${t}.cpp)
        add_dpu_test(${t})
    endif()
endforeach()

# Multiprocess proof tests need the transport library (only built with tools).
if(DPUFABRIC_BUILD_MP)
    foreach(t test_mp_worker test_mp_coordinator)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${t}.cpp)
            add_executable(${t} ${t}.cpp)
            target_link_libraries(${t} PRIVATE dpufabric_core dpufabric_mp)
            target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
            add_test(NAME ${t} COMMAND ${t})
        endif()
    endforeach()
endif()