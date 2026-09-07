cmake_minimum_required(VERSION 3.20)

project(DPUFabric
  VERSION 1.0.0
  DESCRIPTION "Vendor-neutral runtime for governing programmable infrastructure offload"
  LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(DPUFABRIC_BUILD_TOOLS "Build CLI and multiprocess tools" ON)
option(DPUFABRIC_BUILD_EXAMPLES "Build examples" ON)
option(DPUFABRIC_BUILD_TESTS "Build tests" ON)
option(DPUFABRIC_BUILD_BENCHMARKS "Build benchmarks" ON)
option(DPUFABRIC_BUILD_SYNTHETIC_BACKEND "Build the synthetic backend" ON)
option(DPUFABRIC_BUILD_SYSTEM_BACKEND "Build the system backend" ON)
option(DPUFABRIC_BUILD_UNSUPPORTED_BACKEND "Build the unsupported backend" ON)
option(DPUFABRIC_ENABLE_ASAN "Enable AddressSanitizer on core targets" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /Zc:__cplusplus)
  add_compile_options(/utf-8)
  if(DPUFABRIC_ENABLE_ASAN)
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
  if(DPUFABRIC_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
  endif()
endif()

# Core static library (vendor-neutral).
add_library(dpufabric_core STATIC
  src/engine.cpp
  src/eligibility.cpp
  src/persistence.cpp
  src/summarize.cpp
)
target_include_directories(dpufabric_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(dpufabric_core PUBLIC cxx_std_20)
set_target_properties(dpufabric_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Backend libraries depending on core.
if(DPUFABRIC_BUILD_SYNTHETIC_BACKEND)
  add_library(dpufabric_synthetic STATIC backends/synthetic/synthetic_backend.cpp)
  target_link_libraries(dpufabric_synthetic PUBLIC dpufabric_core)
  target_include_directories(dpufabric_synthetic PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
endif()
if(DPUFABRIC_BUILD_SYSTEM_BACKEND)
  add_library(dpufabric_system STATIC backends/system/system_backend.cpp)
  target_link_libraries(dpufabric_system PUBLIC dpufabric_core)
  target_link_libraries(dpufabric_system PRIVATE setupapi)
  target_include_directories(dpufabric_system PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
endif()
if(DPUFABRIC_BUILD_UNSUPPORTED_BACKEND)
  add_library(dpufabric_unsupported STATIC backends/unsupported/unsupported_backend.cpp)
  target_link_libraries(dpufabric_unsupported PUBLIC dpufabric_core)
  target_include_directories(dpufabric_unsupported PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
endif()

# Multiprocess helper library (framed TCP transport + message codec).
# Only built when tools are enabled (coordinators/workers use it).
set(DPUFABRIC_BUILD_MP OFF)
if(DPUFABRIC_BUILD_TOOLS)
  set(DPUFABRIC_BUILD_MP ON)
  add_library(dpufabric_mp STATIC src/mp_transport.cpp)
  target_link_libraries(dpufabric_mp PUBLIC dpufabric_core)
  target_link_libraries(dpufabric_mp PRIVATE ws2_32)
  target_include_directories(dpufabric_mp PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
endif()

# ---- Install / export ----
set(DPUFABRIC_EXPORT_TARGETS dpufabric_core)
if(DPUFABRIC_BUILD_MP)
  list(APPEND DPUFABRIC_EXPORT_TARGETS dpufabric_mp)
endif()
if(DPUFABRIC_BUILD_SYNTHETIC_BACKEND)
  list(APPEND DPUFABRIC_EXPORT_TARGETS dpufabric_synthetic)
endif()
if(DPUFABRIC_BUILD_SYSTEM_BACKEND)
  list(APPEND DPUFABRIC_EXPORT_TARGETS dpufabric_system)
endif()
if(DPUFABRIC_BUILD_UNSUPPORTED_BACKEND)
  list(APPEND DPUFABRIC_EXPORT_TARGETS dpufabric_unsupported)
endif()

install(TARGETS ${DPUFABRIC_EXPORT_TARGETS}
  EXPORT DPUFabricTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/dpufabric DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT DPUFabricTargets
  FILE DPUFabricTargets.cmake
  NAMESPACE DPUFabric::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DPUFabric
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DPUFabricConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/DPUFabricConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DPUFabric
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/DPUFabricConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/DPUFabricConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/DPUFabricConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DPUFabric
)

if(DPUFABRIC_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
if(DPUFABRIC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(DPUFABRIC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(DPUFABRIC_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
