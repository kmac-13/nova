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
	${CMAKE_SOURCE_DIR}/../libs/nova/include/kmac/*.h
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
	${CMAKE_SOURCE_DIR}/../libs/nova_flare/include/kmac/*.h
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

# Multi-threaded Nova vs spdlog vs Quill comparison
# does not use Google Benchmark - controls thread lifecycle directly to avoid
# the Quill ScopedThreadContext incompatibility with Google Benchmark's thread pool
add_executable( benchmark_multithreaded
	benchmark_multithreaded.cpp
	${NOVA_SOURCES}
	${NOVA_EXTRAS_SOURCES}
)
if( CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" )
	target_compile_options( benchmark_multithreaded PRIVATE -O3 -march=native )
elseif( CMAKE_CXX_COMPILER_ID MATCHES "MSVC" )
	target_compile_options( benchmark_multithreaded PRIVATE /O2 )
endif()

# Benchmark launcher (interactive menu for Qt Creator)
add_executable( benchmark_launcher benchmark_launcher.cpp )
# No libraries needed - just launches other benchmarks

# ============================================================================
# Optional Third-Party Libraries
# ============================================================================
# spdlog, Quill, and easylogging++ are fetched automatically via FetchContent
# if not found via find_package.
#
# Boost.Log and log4cpp have build systems that do not lend themselves to
# FetchContent; install them via your package manager:
#   Ubuntu/Debian: sudo apt install libboost-log-dev liblog4cpp5-dev
#   Fedora/RHEL:   sudo dnf install boost-devel log4cpp-devel
#   macOS:         brew install boost log4cpp

include( FetchContent )

# helper: apply HAVE_* define and link a third-party target to all
# comparison benchmark executables that accept it
function( link_third_party define target )
	foreach( bench benchmark_throughput benchmark_latency benchmark_formatted_file_output )
		if( TARGET ${bench} )
			target_compile_definitions( ${bench} PRIVATE ${define} )
			target_link_libraries( ${bench} PRIVATE ${target} )
		endif()
	endforeach()
endfunction()

# ----------------------------------------------------------------------------
# spdlog
# ----------------------------------------------------------------------------
find_package( spdlog QUIET )
if( NOT spdlog_FOUND )
	message( STATUS "spdlog not found via find_package - fetching from GitHub" )
	FetchContent_Declare( spdlog
		GIT_REPOSITORY https://github.com/gabime/spdlog.git
		GIT_TAG        v1.14.1
		GIT_SHALLOW    TRUE
	)
	set( SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE )
	set( SPDLOG_BUILD_TESTS    OFF CACHE BOOL "" FORCE )
	set( SPDLOG_INSTALL        OFF CACHE BOOL "" FORCE )
	FetchContent_MakeAvailable( spdlog )
	set( spdlog_FOUND TRUE )
endif()
message( STATUS "spdlog: enabled" )
link_third_party( HAVE_SPDLOG spdlog::spdlog )

# ----------------------------------------------------------------------------
# Boost.Log  (find-or-skip; not suitable for FetchContent)
# ----------------------------------------------------------------------------
find_package( Boost QUIET COMPONENTS log )
if( Boost_FOUND )
	message( STATUS "Boost.Log: enabled" )
	link_third_party( HAVE_BOOST_LOG Boost::log )
else()
	message( STATUS "Boost.Log: not found - skipping" )
	message( STATUS "  Ubuntu/Debian: sudo apt install libboost-log-dev" )
	message( STATUS "  Fedora/RHEL:   sudo dnf install boost-devel" )
	message( STATUS "  macOS:         brew install boost" )
endif()

# ----------------------------------------------------------------------------
# log4cpp  (find-or-skip; unmaintained, no FetchContent support)
# ----------------------------------------------------------------------------
find_library( LOG4CPP_LIBRARY  NAMES log4cpp )
find_path(    LOG4CPP_INCLUDE_DIR log4cpp/Category.hh )
if( LOG4CPP_LIBRARY AND LOG4CPP_INCLUDE_DIR )
	message( STATUS "log4cpp: enabled" )
	add_library( log4cpp_imported UNKNOWN IMPORTED )
	set_target_properties( log4cpp_imported PROPERTIES
		IMPORTED_LOCATION             "${LOG4CPP_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${LOG4CPP_INCLUDE_DIR}"
	)
	link_third_party( HAVE_LOG4CPP log4cpp_imported )
	set( LOG4CPP_FOUND TRUE )
else()
	message( STATUS "log4cpp: not found - skipping" )
	message( STATUS "  Ubuntu/Debian: sudo apt install liblog4cpp5-dev" )
	message( STATUS "  Fedora/RHEL:   sudo dnf install log4cpp-devel" )
	message( STATUS "  macOS:         brew install log4cpp" )
	set( LOG4CPP_FOUND FALSE )
endif()

# ----------------------------------------------------------------------------
# easylogging++
# ----------------------------------------------------------------------------
find_path( EASYLOGGINGPP_INCLUDE_DIR easylogging++.h
	PATHS /usr/include /usr/local/include
)
if( NOT EASYLOGGINGPP_INCLUDE_DIR )
	message( STATUS "easylogging++ not found via find_path - fetching from GitHub" )
	FetchContent_Declare( easyloggingpp
		GIT_REPOSITORY https://github.com/abumq/easyloggingpp.git
		GIT_TAG        v9.97.1
		GIT_SHALLOW    TRUE
	)
	FetchContent_MakeAvailable( easyloggingpp )
	set( EASYLOGGINGPP_INCLUDE_DIR "${easyloggingpp_SOURCE_DIR}/src" )
endif()

if( EXISTS "${EASYLOGGINGPP_INCLUDE_DIR}/easylogging++.cc" )
	message( STATUS "easylogging++: enabled (${EASYLOGGINGPP_INCLUDE_DIR})" )
	add_library( easylogging_lib STATIC
		${EASYLOGGINGPP_INCLUDE_DIR}/easylogging++.cc
		easylogging_impl.cpp
	)
	target_compile_definitions( easylogging_lib PUBLIC
		HAVE_EASYLOGGINGPP
		ELPP_THREAD_SAFE
		ELPP_NO_DEFAULT_LOG_FILE
	)
	target_include_directories( easylogging_lib PUBLIC ${EASYLOGGINGPP_INCLUDE_DIR} )
	foreach( bench benchmark_throughput benchmark_latency benchmark_formatted_file_output )
		if( TARGET ${bench} )
			target_link_libraries( ${bench} PRIVATE easylogging_lib )
		endif()
	endforeach()
	set( EASYLOGGINGPP_FOUND TRUE )
else()
	message( WARNING "easylogging++.cc not found at ${EASYLOGGINGPP_INCLUDE_DIR} - skipping" )
	set( EASYLOGGINGPP_FOUND FALSE )
endif()

# ----------------------------------------------------------------------------
# Quill
# ----------------------------------------------------------------------------
find_package( quill QUIET )
if( NOT quill_FOUND )
	message( STATUS "Quill not found via find_package - fetching from GitHub" )
	FetchContent_Declare( quill
		GIT_REPOSITORY https://github.com/odygrd/quill.git
		GIT_TAG        v8.2.0
		GIT_SHALLOW    TRUE
	)
	set( QUILL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE )
	set( QUILL_BUILD_TESTS    OFF CACHE BOOL "" FORCE )
	FetchContent_MakeAvailable( quill )
	set( quill_FOUND TRUE )
endif()
message( STATUS "Quill: enabled" )
# Quill is excluded from benchmark_latency (async backend not meaningful for
# per-call latency measurement) but included in throughput and file output
target_compile_definitions( benchmark_throughput            PRIVATE HAVE_QUILL )
target_link_libraries(      benchmark_throughput            PRIVATE quill::quill )
target_compile_definitions( benchmark_formatted_file_output PRIVATE HAVE_QUILL )
target_link_libraries(      benchmark_formatted_file_output PRIVATE quill::quill )
target_compile_definitions( benchmark_multithreaded         PRIVATE HAVE_QUILL )
target_link_libraries(      benchmark_multithreaded         PRIVATE quill::quill )
if( spdlog_FOUND OR TARGET spdlog::spdlog )
	target_compile_definitions( benchmark_multithreaded PRIVATE HAVE_SPDLOG )
	target_link_libraries(      benchmark_multithreaded PRIVATE spdlog::spdlog )
endif()

# Delivery latency benchmark (guaranteed delivery, blocking queue)
add_executable( benchmark_delivery_latency
	benchmark_delivery_latency.cpp
	${NOVA_SOURCES}
	${NOVA_EXTRAS_SOURCES}
)
if( CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" )
	target_compile_options( benchmark_delivery_latency PRIVATE -O3 -march=native )
elseif( CMAKE_CXX_COMPILER_ID MATCHES "MSVC" )
	target_compile_options( benchmark_delivery_latency PRIVATE /O2 )
endif()
target_compile_definitions( benchmark_delivery_latency PRIVATE HAVE_QUILL )
target_link_libraries(      benchmark_delivery_latency PRIVATE quill::quill )
if( spdlog_FOUND OR TARGET spdlog::spdlog )
	target_compile_definitions( benchmark_delivery_latency PRIVATE HAVE_SPDLOG )
	target_link_libraries(      benchmark_delivery_latency PRIVATE spdlog::spdlog )
endif()
# ============================================================================
# Summary
# ============================================================================
message( STATUS "" )
message( STATUS "===========================================" )
message( STATUS "Nova Benchmark Suite" )
message( STATUS "===========================================" )
message( STATUS "  benchmark_nova_builders" )
message( STATUS "  benchmark_nova_sinks" )
message( STATUS "  benchmark_memory_pool_async" )
message( STATUS "  benchmark_flare_emergency" )
message( STATUS "  benchmark_throughput" )
message( STATUS "  benchmark_formatted_file_output" )
message( STATUS "  benchmark_latency" )
message( STATUS "  benchmark_multithreaded" )
message( STATUS "  benchmark_delivery_latency" )
message( STATUS "" )
message( STATUS "Third-party comparison libraries:" )
message( STATUS "  spdlog:        enabled" )
message( STATUS "  Boost.Log:     ${Boost_FOUND}" )
message( STATUS "  log4cpp:       ${LOG4CPP_FOUND}" )
message( STATUS "  easylogging++: ${EASYLOGGINGPP_FOUND}" )
message( STATUS "  Quill:         enabled" )
message( STATUS "===========================================" )
message( STATUS "" )

if( WIN32 )
	message( STATUS "Windows notes:" )
	message( STATUS "  log4cpp may not build correctly on MinGW" )
	message( STATUS ""  )
endif()
