cmake_minimum_required( VERSION 3.16 )
project( NovaFlareTests LANGUAGES CXX )

set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )

# Load sanitizers module from parent directory
# include( ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/Sanitizers.cmake )

# If GTest is installed in a non-standard location, like a custom build
# set( GTEST_ROOT "../../../googletest/" )
include( FetchContent )
FetchContent_Declare(
	googletest
	URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable( googletest )

# Otherwise, hard-code the local path
# add_subdirectory( "../../../googletest/" ${CMAKE_CURRENT_BINARY_DIR}/googletest )

# Find GTest
# find_package( GTest REQUIRED )
include( GoogleTest )

# Add Nova include directories
include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../nova/include
	${CMAKE_CURRENT_SOURCE_DIR}/../nova_extras/include
	${CMAKE_CURRENT_SOURCE_DIR}/../flare/include

#	${CMAKE_CURRENT_SOURCE_DIR}/../nova_extras/src
#	${CMAKE_CURRENT_SOURCE_DIR}/../flare/src
)

# Collect Nova files
file( GLOB NOVA_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/../nova/include/kmac/nova/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/../nova/include/kmac/nova/platform/*.h
)

# Collect Nova Extras files
file( GLOB NOVA_EXTRAS_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/../nova_extras/include/kmac/nova/extras/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/../nova_extras/src/kmac/nova/extras/*.cpp
)

# Collect Flare files
file( GLOB FLARE_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/../flare/include/kmac/flare/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/../flare/src/kmac/flare/*.cpp
)

# Helper function to create a gtest test
function( add_nova_gtest test_name test_source )
	add_executable( ${test_name} ${test_source} ${NOVA_SOURCES} ${NOVA_EXTRAS_SOURCES} ${FLARE_SOURCES} )
	target_link_libraries( ${test_name} PRIVATE GTest::gtest GTest::gtest_main pthread )

	# sanitizers run automatically in repo, so don't force them here
	# enable_asan( ${test_name} )
	# enable_msan( ${test_name} )
	# enable_tsan( ${test_name} )
	# enable_ubsan( ${test_name} )
	# enable_lsan( ${test_name} )
	# enable_dfsan( ${test_name} )
	# enable_libfuzzer( ${test_name} )

	gtest_discover_tests( ${test_name} )
endfunction()

# Enable testing
enable_testing()

# Core Nova Tests
add_nova_gtest( test_nova_core test_nova_core.cpp )
add_nova_gtest( test_nova_record_builders test_nova_record_builders.cpp )
add_nova_gtest( test_nova_logger test_nova_logger.cpp )
add_nova_gtest( test_nova_scoped_configurator test_nova_scoped_configurator.cpp )

# Nova Extras Tests
add_nova_gtest( test_nova_mpsc_queue test_nova_mpsc_queue.cpp )
add_nova_gtest( test_nova_sinks test_nova_sinks.cpp )
add_nova_gtest( test_nova_formatters test_nova_formatters.cpp )
add_nova_gtest( test_nova_synchronization test_nova_synchronization.cpp )
add_nova_gtest( test_nova_composite test_nova_composite.cpp )
add_nova_gtest( test_nova_memory_pool_async_sink test_nova_memory_pool_async_sink.cpp )
add_nova_gtest( test_nova_circular_file_sink test_nova_circular_file_sink.cpp )
add_nova_gtest( test_nova_rolling_file_sink test_nova_rolling_file_sink.cpp )

# Flare Tests
add_nova_gtest( test_flare_emergency test_flare_emergency.cpp )
add_nova_gtest( test_flare_reader test_flare_reader.cpp )
add_nova_gtest( test_flare_scanner test_flare_scanner.cpp )

# Integration Tests
add_nova_gtest( test_integration test_integration.cpp )

# Print summary
message( STATUS "Nova/Flare gtest suite configured" )
message( STATUS "  GTest version: ${GTest_VERSION}" )
message( STATUS "  Tests: 16 test executables" )
