# Detects whether this is a top-level project
get_directory_property(HAS_PARENT PARENT_DIRECTORY)

if(HAS_PARENT)
    set(FAST_ENVELOPE_TOPLEVEL_PROJECT OFF)
else()
    set(FAST_ENVELOPE_TOPLEVEL_PROJECT ON)
endif()

# Check required CMake version
set(REQUIRED_CMAKE_VERSION "3.25.0")
if(FAST_ENVELOPE_TOPLEVEL_PROJECT)
    cmake_minimum_required(VERSION ${REQUIRED_CMAKE_VERSION})
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0.0")
        message(WARNING "CMake version is ${CMAKE_VERSION}, setting stuff for dependencies.")
        SET(CMAKE_POLICY_VERSION_MINIMUM ${REQUIRED_CMAKE_VERSION})
    endif()
else()
    # Don't use cmake_minimum_required here to avoid implicitly overriding parent policies
    if(${CMAKE_VERSION} VERSION_LESS ${REQUIRED_CMAKE_VERSION})
        message(FATAL_ERROR "CMake required version to build FastEnvelope is ${REQUIRED_CMAKE_VERSION}")
    endif()
endif()

# Enable ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    option(FAST_ENVELOPE_WITH_CCACHE "Enable ccache when building FastEnvelope" ${FAST_ENVELOPE_TOPLEVEL_PROJECT})
else()
    option(FAST_ENVELOPE_WITH_CCACHE "Enable ccache when building FastEnvelope" OFF)
endif()
if(FAST_ENVELOPE_WITH_CCACHE AND CCACHE_PROGRAM)
    set(ccacheEnv
        CCACHE_BASEDIR=${CMAKE_BINARY_DIR}
        CCACHE_SLOPPINESS=clang_index_store,include_file_ctime,include_file_mtime,locale,pch_defines,time_macros
    )
    foreach(lang IN ITEMS C CXX)
        set(CMAKE_${lang}_COMPILER_LAUNCHER
            ${CMAKE_COMMAND} -E env ${ccacheEnv} ${CCACHE_PROGRAM}
        )
    endforeach()
endif()

################################################################################
# CMake Policies
################################################################################

cmake_policy(SET CMP0054 NEW) # Only interpret if() arguments as variables or keywords when unquoted.
cmake_policy(SET CMP0076 NEW) # target_sources() command converts relative paths to absolute.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
  cmake_policy(SET CMP0135 NEW) # Set the timestamps of all extracted contents to the time of the extraction.
endif()

# ###############################################################################
project(FastEnvelope)

# ###############################################################################

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/recipes)

# Color output
include(UseColors)

# Prepend function
include(PrependCurrentPath)

# FastEnvelope utils
include(FastEnvelopeUtils)

# Extra warnings
include(Warnings)

include(fast_envelope_copy_dll)

# Sort projects inside the solution
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Generate position independent code by default
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

################################################################################


################################################################################

# FastEnvelope options
option(FAST_ENVELOPE_WITH_UNIT_TESTS   			"unit test project" 			ON)
option(FAST_ENVELOPE_ENABLE_TBB        			"Enable TBB"                    ON)
# option(FAST_ENVELOPE_USE_FLOAT          		"Use floats instead of double"  OFF)

option(FAST_ENVELOPE_WITH_GMP						"Use gmp to have rational predicates"	  OFF)
option(FAST_ENVELOPE_WITH_TIMER   					"use timer in the library for debug" 	  OFF)

# Sanitizer options
option(SANITIZE_ADDRESS   "Sanitize Address"       OFF)
option(SANITIZE_MEMORY    "Sanitize Memory"        OFF)
option(SANITIZE_THREAD    "Sanitize Thread"        OFF)
option(SANITIZE_UNDEFINED "Sanitize Undefined"     OFF)


################################################################################
# FastEnvelope library
################################################################################

add_library(${PROJECT_NAME})
add_library(FastEnvelope::FastEnvelope ALIAS ${PROJECT_NAME})
# C++ standard
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

# Public include directory for FastEnvelope
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
	# Fix Windows issue with ambiguity of std::byte
	# target_compile_definitions(${PROJECT_NAME} PUBLIC _HAS_STD_BYTE=0)
	target_compile_definitions(${PROJECT_NAME} PUBLIC WIN32_LEAN_AND_MEAN)
endif()
# Extra warnings
target_link_libraries(${PROJECT_NAME} PRIVATE warnings::all)

if(FAST_ENVELOPE_WITH_SANITIZERS)
	add_sanitizers(${PROJECT_NAME})
endif()


################################################################################
# Required libraries
################################################################################


# TBB
if(FAST_ENVELOPE_ENABLE_TBB)
	include(onetbb)
endif()


# if(${FAST_ENVELOPE_USE_FLOAT})
# 	target_compile_definitions(${PROJECT_NAME} PUBLIC -DFAST_ENVELOPE_USE_FLOAT)
# endif()

include(cli11)
include(spdlog)

#spdlog
target_link_libraries(${PROJECT_NAME} PUBLIC spdlog::spdlog)
target_link_libraries(${PROJECT_NAME} PUBLIC CLI11::CLI11)

# Predicates
include(ipred)
target_link_libraries(${PROJECT_NAME} PUBLIC indirectPredicates)

if(FAST_ENVELOPE_WITH_TIMER)
	target_compile_definitions(${PROJECT_NAME} PRIVATE FAST_ENVELOPE_USE_TIMER)
endif()

# Assume eigen exists in the other lib
include(eigen)
target_link_libraries(${PROJECT_NAME} PUBLIC Eigen3::Eigen)

################################################################################
# Subdirectories
################################################################################

# Sources
add_subdirectory(src/fastenvelope)

if(FAST_ENVELOPE_WITH_GMP)
	find_package(GMP REQUIRED)

	target_link_libraries(${PROJECT_NAME} PUBLIC gmp::gmp)
	target_compile_definitions(${PROJECT_NAME} PUBLIC ENVELOPE_WITH_GMP)
endif()


################################################################################
# FastEnvelope tests
################################################################################

if(FAST_ENVELOPE_TOPLEVEL_PROJECT AND FAST_ENVELOPE_WITH_UNIT_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

################################################################################
# FastEnvelope binary
################################################################################

# Main executable
if(FAST_ENVELOPE_TOPLEVEL_PROJECT)
	# add an application library
	#add_subdirectory(app/benchmark)
	set(APP_SOURCES
		app/benchmark/MeshIO.hpp
		app/benchmark/MeshIO.cpp
		app/benchmark/mesh_AABB.h
		app/benchmark/mesh_AABB.cpp
		app/benchmark/AABBWrapper.cpp
		app/benchmark/AABBWrapper.h
		app/benchmark/sampling.h
		app/benchmark/sampling.cpp
	)
	add_library(${PROJECT_NAME}_app ${APP_SOURCES})
	target_include_directories(${PROJECT_NAME}_app PUBLIC ${PROJECT_BINARY_DIR}/include)
	target_include_directories(${PROJECT_NAME}_app PUBLIC app)

	target_link_libraries(${PROJECT_NAME}_app PUBLIC ${PROJECT_NAME})

	# executable
	add_executable(${PROJECT_NAME}_bin app/main.cpp)
	target_link_libraries(${PROJECT_NAME}_bin PUBLIC ${PROJECT_NAME}_app)

	target_compile_features(${PROJECT_NAME}_bin PUBLIC ${CXX14_FEATURES})
	if(FAST_ENVELOPE_WITH_SANITIZERS)
		add_sanitizers(${PROJECT_NAME}_bin)
	endif()


	if(FAST_ENVELOPE_ENABLE_TBB)
		target_link_libraries(${PROJECT_NAME}_bin PUBLIC TBB::tbb)
	endif()

	if(NOT (${CMAKE_VERSION} VERSION_LESS "3.6.0"))
		set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME}_bin)
	endif()
	fast_envelope_copy_dll(${PROJECT_NAME}_bin)
endif()
