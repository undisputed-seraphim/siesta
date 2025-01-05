# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.25)
project(siesta VERSION 0.1.0)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Default installation to build/install" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.81 REQUIRED COMPONENTS system json program_options)
find_package(simdjson 3.11 REQUIRED)
message(STATUS "simdjson version ${simdjson_VERSION}")
find_package(fmt 9 REQUIRED)
message(STATUS "fmt ${fmt_VERSION}")

add_subdirectory(generator)

file(GLOB_RECURSE lib_src "${CMAKE_CURRENT_SOURCE_DIR}/src/*")
add_library(siesta STATIC ${lib_src})
target_include_directories(siesta PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(siesta PUBLIC
	Boost::boost
	Boost::json
	Boost::system
	fmt::fmt
)


#add_subdirectory(output)

add_subdirectory(tests)

install(TARGETS siesta EXPORT ${PROJECT_NAME}_Targets)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
write_basic_package_version_file("${PROJECT_NAME}ConfigVersion.cmake" VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
configure_package_config_file("${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}.cmake.in" "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake" INSTALL_DESTINATION ${CMAKE_INSTALL_FULL_DATAROOTDIR}/${PROJECT_NAME}/cmake)
install(FILES
	${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
	${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
DESTINATION
	${CMAKE_INSTALL_FULL_DATAROOTDIR}/${PROJECT_NAME}/cmake
)
install(EXPORT
	${PROJECT_NAME}_Targets
FILE
	${PROJECT_NAME}Targets.cmake
NAMESPACE
	${PROJECT_NAME}::
DESTINATION
	${CMAKE_INSTALL_FULL_DATAROOTDIR}/${PROJECT_NAME}/cmake
)
