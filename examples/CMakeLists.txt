cmake_minimum_required( VERSION 3.15 )
project( NovaExamples VERSION 1.0 LANGUAGES CXX )

# C++17 is required
set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

# Compiler warnings
if( MSVC )
	add_compile_options( /W4 /WX )
else()
	add_compile_options( -Wall -Wextra -Wpedantic -Werror )
endif()

#
# Configuration: Choose which example to build
#
# Options:
#   01_basic_usage
#   02_multiple_sinks
#   03_custom_clock
#   04_multithreading
#   05_filtering
#   06_hierarchical_tags
#   07_flare
#   08_bare_metal
#   09_diagnostics
#
set( EXAMPLE "09_diagnostics" CACHE STRING "Which example to build" )
set_property( CACHE EXAMPLE PROPERTY STRINGS
    "01_basic_usage"
    "02_multiple_sinks"
    "03_custom_clock"
    "04_multithreading"
    "05_filtering"
    "06_hierarchical_tags"
    "07_flare"
    "08_bare_metal"
    "09_diagnostics"
)

#
# Nova include path
#
# Adjust this to point to your Nova installation
# Options:
#   1. Set NOVA_INCLUDE_DIR environment variable
#   2. Set via command line: cmake -DNOVA_INCLUDE_DIR=/path/to/nova/include ..
#   3. Modify the default below
#
if( DEFINED ENV{NOVA_INCLUDE_DIR} )
	set( NOVA_INCLUDE_DIR $ENV{ NOVA_INCLUDE_DIR} )
elseif( NOT DEFINED NOVA_INCLUDE_DIR )
	# Default: assume Nova is two directories up from examples
	set( NOVA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../nova/include" )
endif()

message( STATUS "Nova include directory: ${NOVA_INCLUDE_DIR}" )

if( NOT EXISTS "${NOVA_INCLUDE_DIR}/kmac/nova/logger.h" )
	message( FATAL_ERROR
		"Nova headers not found at: ${NOVA_INCLUDE_DIR}\n"
		"Please set NOVA_INCLUDE_DIR to your Nova installation.\n"
		"Example: cmake -DNOVA_INCLUDE_DIR=/path/to/nova/include .."
	)
endif()

#
# Nova extras include path
#
if( DEFINED ENV{NOVA_EXTRAS_DIR} )
	set( NOVA_EXTRAS_DIR $ENV{NOVA_EXTRAS_DIR} )
elseif( NOT DEFINED NOVA_EXTRAS_DIR )
	set( NOVA_EXTRAS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../nova_extras" )
endif()

#
# Flare include path
#
if( DEFINED ENV{NOVA_FLARE_DIR} )
	set( NOVA_FLARE_DIR $ENV{NOVA_FLARE_DIR} )
elseif( NOT DEFINED NOVA_FLARE_DIR )
	set( NOVA_FLARE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../flare" )
endif()

FILE( GLOB NOVA_FILES
	"${NOVA_INCLUDE_DIR}/kmac/nova/*.h"
	"${NOVA_INCLUDE_DIR}/kmac/nova/*.cpp"
	"${NOVA_INCLUDE_DIR}/kmac/nova/platform/*.h"
	"${NOVA_EXTRAS_DIR}/include/kmac/nova/extras/*.h"
	"${NOVA_EXTRAS_DIR}/src/kmac/nova/extras/*.cpp"
	"${NOVA_FLARE_DIR}/include/kmac/flare/*.h"
	"${NOVA_FLARE_DIR}/src/kmac/flare/*.cpp"
)

#
# Example executables
#
add_executable( nova_example ${EXAMPLE}/main.cpp ${NOVA_FILES} )

target_include_directories( nova_example PRIVATE ${NOVA_INCLUDE_DIR} ${NOVA_EXTRAS_DIR}/include ${NOVA_FLARE_DIR}/include )

# Example 04 requires threading support
if( EXAMPLE STREQUAL "04_multithreading" )
	find_package( Threads REQUIRED )
	target_link_libraries( nova_example PRIVATE Threads::Threads )
endif()

# Set output name to match example
set_target_properties( nova_example PROPERTIES OUTPUT_NAME ${EXAMPLE} )

#
# Installation (optional)
#
install( TARGETS nova_example DESTINATION bin )

#
# Display configuration
#
message( STATUS "========================================" )
message( STATUS "Nova Examples Configuration" )
message( STATUS "========================================" )
message( STATUS "Example to build: ${EXAMPLE}" )
message( STATUS "Output executable: ${EXAMPLE}" )
message( STATUS "Nova include dir: ${NOVA_INCLUDE_DIR}" )
message( STATUS "C++ standard: ${CMAKE_CXX_STANDARD}" )
message( STATUS "========================================" )

#
# Usage help
#
if( CMAKE_BUILD_TYPE STREQUAL "" )
	message( STATUS "Build type not set, defaulting to Release" )
	set( CMAKE_BUILD_TYPE Release )
endif()

message( STATUS "" )
message( STATUS "To build a different example:" )
message( STATUS "  cmake -DEXAMPLE=02_multiple_sinks .." )
message( STATUS "  cmake --build ." )
message( STATUS "" )
message( STATUS "To set Nova include directory:" )
message( STATUS "  cmake -DNOVA_INCLUDE_DIR=/path/to/nova/include .." )
message( STATUS "" )
message( STATUS "Available examples:" )
message( STATUS "  01_basic_usage       - Core concepts" )
message( STATUS "  02_multiple_sinks    - Routing strategies" )
message( STATUS "  03_custom_clock      - Timestamp customization" )
message( STATUS "  04_multithreading    - Thread safety" )
message( STATUS "  05_filtering         - Runtime filtering" )
message( STATUS "  06_hierarchical_tags - Traditional logging model" )
message( STATUS "" )
