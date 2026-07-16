cmake_minimum_required(VERSION 3.10)

project(rme)

include(CTest)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)

find_package(OpenGL REQUIRED)

# LibArchive support is disabled by default. To enable OTGZ support, define
# OTGZ_SUPPORT in source/definitions.h and add LibArchive back to the build.
# if(APPLE)
#     set(CMAKE_PREFIX_PATH /usr/local/opt/libarchive)
# endif()
# find_package(LibArchive REQUIRED)

find_package(wxWidgets COMPONENTS html aui gl adv core net base REQUIRED)

find_package(ZLIB REQUIRED)

include(${wxWidgets_USE_FILE})
include(source/CMakeLists.txt)
add_executable(${PROJECT_NAME} ${rme_H} ${rme_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED ON)

include_directories(${OPENGL_INCLUDE_DIR} ${ZLIB_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME}
	${wxWidgets_LIBRARIES}
	${OPENGL_LIBRARIES}
	${ZLIB_LIBRARIES}
	glad::glad
	fmt::fmt
	nlohmann_json::nlohmann_json
	pugixml::pugixml
)

if(BUILD_TESTING)
	add_executable(item_id_mapping_tests
		tests/item_id_mapping_tests.cpp
		source/item_id_mapping.cpp
	)
	target_include_directories(item_id_mapping_tests PRIVATE source)
	set_target_properties(item_id_mapping_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/item_id_mapping_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/item_id_mapping_tests"
	)
	add_test(NAME item_id_mapping_tests COMMAND item_id_mapping_tests)

	add_executable(spawn_format_tests
		tests/spawn_format_tests.cpp
		source/spawn_format.cpp
	)
	target_include_directories(spawn_format_tests PRIVATE source)
	target_link_libraries(spawn_format_tests PRIVATE pugixml::pugixml)
	set_target_properties(spawn_format_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/spawn_format_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/spawn_format_tests"
	)
	add_test(NAME spawn_format_tests COMMAND spawn_format_tests)
endif()
