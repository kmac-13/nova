cmake_minimum_required( VERSION 3.16 )
project( NovaFlareBenchmarks LANGUAGES CXX )

set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )

# Add Google Benchmark
option( BENCHMARK_ENABLE_TESTING "Enable testing of the benchmark library." OFF )
option( BENCHMARK_ENABLE_INSTALL "Enable installation of benchmark." OFF )

# Try to find Google Benchmark first
find_package( benchmark QUIET )

if( NOT benchmark_FOUND )
	# Check if local copy exists
	if( EXISTS "${CMAKE_SOURCE_DIR}/../../googlebenchmark/CMakeLists.txt" )
		message( STATUS "Using local Google Benchmark from ../../googlebenchmark" )
		add_subdirectory( "../../googlebenchmark" ${CMAKE_CURRENT_BINARY_DIR}/googlebenchmark EXCLUDE_FROM_ALL )
	else()
		# Fall back to fetching from GitHub
		message( STATUS "Google Benchmark not found locally, fetching from GitHub..." )
		include( FetchContent )
		FetchContent_Declare(
			googlebenchmark
			GIT_REPOSITORY https://github.com/google/benchmark.git
			GIT_TAG v1.8.3
		)
		FetchContent_MakeAvailable( googlebenchmark )
	endif()
endif()

# Add Nova include directories
include_directories(
	${CMAKE_SOURCE_DIR}/../libs/nova/include
	${CMAKE_SOURCE_DIR}/../libs/nova_extras/include
	${CMAKE_SOURCE_DIR}/../libs/nova_flare/include
)

# Collect Nova files
file( GLOB NOVA_SOURCES
	${CMAKE_SOURCE_DIR}/../libs/nova/include/kmac/nova/*.h
	${CMAKE_SOURCE_DIR}/../libs/nova/include/kmac/nova/platform/*.h
)

# Collect Nova Extras files
file( GLOB NOVA_EXTRAS_SOURCES
	${CMAKE_SOURCE_DIR}/../libs/nova_extras/include/kmac/nova/extras/*.h
	${CMAKE_SOURCE_DIR}/../libs/nova_extras/src/kmac/nova/extras/*.cpp
)

# Collect Flare files
file( GLOB FLARE_SOURCES
	${CMAKE_SOURCE_DIR}/../libs/nova_flare/include/kmac/flare/*.h
	${CMAKE_SOURCE_DIR}/../libs/nova_flare/src/kmac/flare/*.cpp
)

# Helper function to create a benchmark executable
function( add_nova_benchmark benchmark_name benchmark_source )
	add_executable( ${benchmark_name}
		${benchmark_source}
		${NOVA_SOURCES}
		${NOVA_EXTRAS_SOURCES}
		${FLARE_SOURCES}
	)
	target_link_libraries( ${benchmark_name} PRIVATE benchmark::benchmark )

	# Add compiler optimizations for benchmarks
	if( CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" )
		target_compile_options( ${benchmark_name} PRIVATE -O3 -march=native )
	elseif( CMAKE_CXX_COMPILER_ID MATCHES "MSVC" )
		target_compile_options( ${benchmark_name} PRIVATE /O2 )
	endif()
endfunction()

# Specific Nova feature benchmarks
add_nova_benchmark( benchmark_nova_builders benchmark_nova_builders.cpp )
add_nova_benchmark( benchmark_nova_sinks benchmark_nova_sinks.cpp )
add_nova_benchmark( benchmark_memory_pool_async benchmark_memory_pool_async.cpp )
add_nova_benchmark( benchmark_flare_emergency benchmark_flare_emergency.cpp )

# Throughput benchmarks
add_nova_benchmark( benchmark_throughput benchmark_throughput.cpp )
add_nova_benchmark( benchmark_formatted_file_output benchmark_formatted_file_output.cpp )

# Latency benchmarks
add_nova_benchmark( benchmark_latency benchmark_latency.cpp )

# Benchmark launcher (interactive menu for Qt Creator)
add_executable( benchmark_launcher benchmark_launcher.cpp )
# No libraries needed - just launches other benchmarks

# ============================================================================
# Optional Third-Party Libraries
# ============================================================================
# These will be conditionally compiled based on availability.
# Libraries are NOT built as part of this project - they must be pre-installed
# or available via find_package/find_library.

# spdlog
find_package( spdlog QUIET )
if( spdlog_FOUND )
	message( STATUS "spdlog found - will include in benchmarks" )
	target_compile_definitions( benchmark_throughput PRIVATE HAVE_SPDLOG )
	target_link_libraries( benchmark_throughput PRIVATE spdlog::spdlog )

	target_compile_definitions( benchmark_formatted_file_output PRIVATE HAVE_SPDLOG )
	target_link_libraries( benchmark_formatted_file_output PRIVATE spdlog::spdlog )

	target_compile_definitions( benchmark_latency PRIVATE HAVE_SPDLOG )
	target_link_libraries( benchmark_latency PRIVATE spdlog::spdlog )
else()
	# try local build if exists
	if( EXISTS "${CMAKE_SOURCE_DIR}/../../logging/spdlog/CMakeLists.txt" )
		message( STATUS "Using local spdlog from ../../logging/spdlog" )
		add_subdirectory( "../../logging/spdlog" ${CMAKE_CURRENT_BINARY_DIR}/spdlog EXCLUDE_FROM_ALL )
		if( TARGET spdlog )
			set( spdlog_FOUND TRUE )
			target_compile_definitions( benchmark_throughput PRIVATE HAVE_SPDLOG )
			target_link_libraries( benchmark_throughput PRIVATE spdlog::spdlog )

			target_compile_definitions( benchmark_formatted_file_output PRIVATE HAVE_SPDLOG )
			target_link_libraries( benchmark_formatted_file_output PRIVATE spdlog::spdlog )

			target_compile_definitions( benchmark_latency PRIVATE HAVE_SPDLOG )
			target_link_libraries( benchmark_latency PRIVATE spdlog::spdlog )
		endif()
	else()
		message( STATUS "spdlog not found - skipping spdlog benchmarks" )
	endif()
endif()

# Options for controlling which libraries to benchmark
option( ENABLE_GLOG_BENCHMARKS "Enable glog benchmarks (may have issues on Windows)" OFF )

# glog
if( ENABLE_GLOG_BENCHMARKS )
	find_package( glog QUIET )
	if( glog_FOUND )
		message( STATUS "glog found - will include in benchmarks" )
		target_compile_definitions( benchmark_throughput PRIVATE HAVE_GLOG )
		target_link_libraries( benchmark_throughput PRIVATE glog::glog )
		
		target_compile_definitions( benchmark_latency PRIVATE HAVE_GLOG )
		target_link_libraries( benchmark_latency PRIVATE glog::glog )
	else()
		# Try local build if exists
		if( EXISTS "${CMAKE_SOURCE_DIR}/../../logging/glog/CMakeLists.txt" )
			message( STATUS "Attempting to build glog from source at ../../logging/glog" )

			# Force static library build
			set( BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE )
			set( BUILD_TESTING OFF CACHE BOOL "Disable glog tests" FORCE )
			set( WITH_GFLAGS OFF CACHE BOOL "Disable gflags" FORCE )
			set( WITH_UNWIND OFF CACHE BOOL "Disable libunwind" FORCE )

			add_subdirectory( "../../logging/glog" ${CMAKE_CURRENT_BINARY_DIR}/glog EXCLUDE_FROM_ALL )

			# Check what targets were created
			if( TARGET glog )
				set( glog_FOUND TRUE )
				message( STATUS "glog target created successfully - enabling glog benchmarks" )

				# Link directly to the glog target
				target_compile_definitions( benchmark_throughput PRIVATE HAVE_GLOG )
				target_link_libraries( benchmark_throughput PRIVATE glog )

				target_compile_definitions( benchmark_latency PRIVATE HAVE_GLOG )
				target_link_libraries( benchmark_latency PRIVATE glog )
			else()
				message( WARNING "glog subdirectory added but no 'glog' target created" )
				message( STATUS "glog benchmarks will be disabled" )
				set( glog_FOUND FALSE )
			endif()
		else()
			message( STATUS "glog source not found at ../../logging/glog - skipping glog benchmarks" )
			set( glog_FOUND FALSE )
		endif()
	endif()
else()
	message( STATUS "glog benchmarks disabled (ENABLE_GLOG_BENCHMARKS=OFF)" )
	message( STATUS "  To enable: cmake -DENABLE_GLOG_BENCHMARKS=ON .." )
	set( glog_FOUND FALSE )
endif()

# Boost.Log
find_package( Boost QUIET COMPONENTS log )
if( Boost_FOUND )
	message( STATUS "Boost.Log found - will include in benchmarks" )
	target_compile_definitions( benchmark_throughput PRIVATE HAVE_BOOST_LOG )
	target_link_libraries( benchmark_throughput PRIVATE Boost::log )

	target_compile_definitions( benchmark_latency PRIVATE HAVE_BOOST_LOG )
	target_link_libraries( benchmark_latency PRIVATE Boost::log )
else()
	message( STATUS "Boost.Log not found - skipping Boost.Log benchmarks" )
	message( STATUS "  Note: Boost.Log is not built from source, install via package manager" )
endif()

# log4cpp
find_library( LOG4CPP_LIBRARY NAMES log4cpp )
find_path( LOG4CPP_INCLUDE_DIR log4cpp/Category.hh )

if( LOG4CPP_LIBRARY AND LOG4CPP_INCLUDE_DIR )
	message( STATUS "log4cpp found - will include in benchmarks" )
	target_compile_definitions( benchmark_throughput PRIVATE HAVE_LOG4CPP )
	target_include_directories( benchmark_throughput PRIVATE ${LOG4CPP_INCLUDE_DIR} )
	target_link_libraries( benchmark_throughput PRIVATE ${LOG4CPP_LIBRARY} )

	target_compile_definitions( benchmark_latency PRIVATE HAVE_LOG4CPP )
	target_include_directories( benchmark_latency PRIVATE ${LOG4CPP_INCLUDE_DIR} )
	target_link_libraries( benchmark_latency PRIVATE ${LOG4CPP_LIBRARY} )

	set( LOG4CPP_FOUND TRUE )
else()
	# Try local build if exists (but log4cpp has issues on Windows)
	if( EXISTS "${CMAKE_SOURCE_DIR}/../../logging/log4cpp/CMakeLists.txt" AND NOT WIN32 )
		message( STATUS "Attempting to use local log4cpp from ../../logging/log4cpp" )
		message( STATUS "  Note: log4cpp may not build correctly on Windows" )
		add_subdirectory( "../../logging/log4cpp" ${CMAKE_CURRENT_BINARY_DIR}/log4cpp EXCLUDE_FROM_ALL )
		# Check if target was created successfully
		if( TARGET log4cpp )
			set( LOG4CPP_FOUND TRUE )
			target_compile_definitions( benchmark_throughput PRIVATE HAVE_LOG4CPP )
			target_link_libraries( benchmark_throughput PRIVATE log4cpp )

			target_compile_definitions( benchmark_latency PRIVATE HAVE_LOG4CPP )
			target_link_libraries( benchmark_latency PRIVATE log4cpp )
		else()
			message( STATUS "log4cpp not found - skipping log4cpp benchmarks" )
			set( LOG4CPP_FOUND FALSE )
		endif()
	else()
		message( STATUS "log4cpp not found - skipping log4cpp benchmarks" )
		set( LOG4CPP_FOUND FALSE )
	endif()
endif()

option( ENABLE_EASYLOGGINGPP_BENCHMARKS "Enable easylogging++ benchmarks (may have linking issues)" ON )
# set( ENABLE_EASYLOGGINGPP_BENCHMARKS ON )

if( ENABLE_EASYLOGGINGPP_BENCHMARKS )
	find_path( EASYLOGGINGPP_INCLUDE_DIR easylogging++.h
		PATHS
			/usr/include
			/usr/local/include
			${CMAKE_SOURCE_DIR}/../third_party/easyloggingpp/src
			${CMAKE_SOURCE_DIR}/../../logging/easyloggingpp/src
	)

	if( EASYLOGGINGPP_INCLUDE_DIR )
		if( EXISTS "${EASYLOGGINGPP_INCLUDE_DIR}/easylogging++.cc" )
			message( STATUS "easylogging++ found at ${EASYLOGGINGPP_INCLUDE_DIR}" )

			# create library with BOTH the implementation AND initialization
			add_library( easylogging_lib STATIC
				${EASYLOGGINGPP_INCLUDE_DIR}/easylogging++.cc  # ← Implementation
				easylogging_impl.cpp                           # ← Initialization (INITIALIZE_EASYLOGGINGPP)
			)

			target_compile_definitions( easylogging_lib PUBLIC
				HAVE_EASYLOGGINGPP
				ELPP_THREAD_SAFE
				ELPP_NO_DEFAULT_LOG_FILE
			)
			target_include_directories( easylogging_lib PUBLIC ${EASYLOGGINGPP_INCLUDE_DIR} )

			# link to benchmarks
			target_link_libraries( benchmark_throughput PRIVATE easylogging_lib )
			target_link_libraries( benchmark_formatted_file_output PRIVATE easylogging_lib )
			target_link_libraries( benchmark_latency PRIVATE easylogging_lib )

			set( EASYLOGGINGPP_FOUND TRUE )
		else()
			message( WARNING "easylogging++.cc is missing" )
			set( EASYLOGGINGPP_FOUND FALSE )
		endif()
	else()
		message( STATUS "easylogging++ not found" )
		set( EASYLOGGINGPP_FOUND FALSE )
	endif()
else()
	message( STATUS "easylogging++ benchmarks disabled" )
	set( EASYLOGGINGPP_FOUND FALSE )
endif()

# Quill
find_package( quill QUIET )
if( quill_FOUND )
	message( STATUS "Quill found - will include in benchmarks" )

	target_compile_definitions( benchmark_throughput PRIVATE HAVE_QUILL )
	target_link_libraries( benchmark_throughput PRIVATE quill::quill )
else()
	# Try local build if exists
	if( EXISTS "${CMAKE_SOURCE_DIR}/../../logging/quill/CMakeLists.txt" )
		message( STATUS "Using local Quill from ../../logging/quill" )
		
		# Quill build options
		set( QUILL_USE_BOUNDED_QUEUE OFF CACHE BOOL "Use bounded queue" FORCE )
		set( QUILL_BUILD_EXAMPLES OFF CACHE BOOL "Disable examples" FORCE )
		set( QUILL_BUILD_TESTS OFF CACHE BOOL "Disable tests" FORCE )
		
		add_subdirectory( "../../logging/quill" ${CMAKE_CURRENT_BINARY_DIR}/quill EXCLUDE_FROM_ALL )
		
		if( TARGET quill )
			set( quill_FOUND TRUE )

			target_compile_definitions( benchmark_throughput PRIVATE HAVE_QUILL )
			target_link_libraries( benchmark_throughput PRIVATE quill::quill )
		endif()
	else()
		message( STATUS "Quill not found - skipping Quill benchmarks" )
		message( STATUS "  To install Quill: git clone https://github.com/odygrd/quill.git ../../logging/quill" )
	endif()
endif()

# NanoLog
find_path( NANOLOG_INCLUDE_DIR NanoLog.hpp
	PATHS
		/usr/include
		/usr/local/include
		${CMAKE_SOURCE_DIR}/../third_party/NanoLog
		${CMAKE_SOURCE_DIR}/../third_party/nanolog
		${CMAKE_SOURCE_DIR}/../../logging/NanoLog
		${CMAKE_SOURCE_DIR}/../../logging/nanolog
)

if( NANOLOG_INCLUDE_DIR )
	message( STATUS "NanoLog found at ${NANOLOG_INCLUDE_DIR}" )
	target_compile_definitions( benchmark_throughput PRIVATE HAVE_NANOLOG )
	target_include_directories( benchmark_throughput PRIVATE ${NANOLOG_INCLUDE_DIR} )

	target_compile_definitions( benchmark_latency PRIVATE HAVE_NANOLOG )
	target_include_directories( benchmark_latency PRIVATE ${NANOLOG_INCLUDE_DIR} )

	set( NANOLOG_FOUND TRUE )
else()
	message( STATUS "NanoLog not found - skipping NanoLog benchmarks" )
	message( STATUS "  Download from: https://github.com/PlatformLab/NanoLog" )
	message( STATUS "  Note: NanoLog has complex build requirements and may not work on Windows" )
	set( NANOLOG_FOUND FALSE )
endif()

# Print summary
message( STATUS "" )
message( STATUS "===========================================" )
message( STATUS "Nova/Flare Benchmark Suite Configured" )
message( STATUS "===========================================" )
message( STATUS "Benchmark executables:" )
message( STATUS "  - benchmark_nova_builders" )
message( STATUS "  - benchmark_nova_sinks" )
message( STATUS "  - benchmark_flare_emergency" )
message( STATUS "  - benchmark_throughput" )
message( STATUS "  - benchmark_latency" )
message( STATUS "" )
message( STATUS "Third-party libraries:" )
message( STATUS "  - spdlog:        ${spdlog_FOUND}" )
message( STATUS "  - glog:          ${glog_FOUND}" )
message( STATUS "  - Boost.Log:     ${Boost_FOUND}" )
message( STATUS "  - log4cpp:       ${LOG4CPP_FOUND}" )
message( STATUS "  - easylogging++: ${EASYLOGGINGPP_FOUND}" )
message( STATUS "  - NanoLog:       ${NANOLOG_FOUND}" )
message( STATUS "===========================================" )
message( STATUS "" )

# Windows-specific warnings
if( WIN32 )
	message( STATUS "Windows Platform Notes:" )
	message( STATUS "  - log4cpp may not build correctly on Windows/MinGW" )
	message( STATUS "  - NanoLog requires POSIX and may not work on Windows" )
	message( STATUS "  - glog works but some features are limited on Windows" )
	message( STATUS "  - glog on Windows may have debug/release library name issues" )
	message( STATUS "    If you see 'libglogd.a not found', try Release build or disable glog" )
	message( STATUS "  - spdlog, easylogging++, and Boost.Log should work fine" )
	message( STATUS "" )
endif()
