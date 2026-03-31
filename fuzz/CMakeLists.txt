cmake_minimum_required( VERSION 3.16 )

# ============================================================================
# Fuzz targets
# ============================================================================
#
# Each fuzz target is a standalone executable built with -fsanitize=fuzzer,address.
# LibFuzzer provides main(); the target supplies LLVMFuzzerTestOneInput().
#
# Requirements:
#   - clang++ (GCC does not ship LibFuzzer)
#   - NOVA_BUILD_FUZZ=ON in the root CMake invocation
#   - NOVA_BUILD_EXTRAS=ON and NOVA_BUILD_FLARE=ON (auto-enabled below)
#
# Usage:
#   cmake -B build \
#     -DCMAKE_CXX_COMPILER=clang++ \
#     -DCMAKE_C_COMPILER=clang \
#     -DCMAKE_BUILD_TYPE=Debug \
#     -DNOVA_BUILD_FUZZ=ON
#   cmake --build build --parallel
#   ./build/fuzz/fuzz_record_builder -max_total_time=60 fuzz/corpus/record_builder
#
# Corpus directories are passed as the last positional argument to each target.
# LibFuzzer reads existing corpus entries as seeds and writes newly discovered
# inputs back to the same directory.

include( ${CMAKE_SOURCE_DIR}/cmake/Sanitizers.cmake )

# ============================================================================
# Helper: add_nova_fuzz_target
# ============================================================================
#
# Registers a single fuzz target executable, applies fuzzer+asan flags, and
# links against the Nova libraries the target needs.
#
# Arguments:
#   TARGET_NAME   name of the executable (e.g. fuzz_record_builder)
#   SOURCE        source file (e.g. fuzz_record_builder.cpp)
#   LIBS          Nova library targets to link (e.g. nova nova_extras flare)

function( add_nova_fuzz_target TARGET_NAME SOURCE )
	cmake_parse_arguments( ARG "" "" "LIBS" ${ARGN} )

	add_executable( ${TARGET_NAME} ${SOURCE} )

	target_include_directories( ${TARGET_NAME} PRIVATE
		${CMAKE_SOURCE_DIR}/libs/nova/include
		${CMAKE_SOURCE_DIR}/libs/nova_extras/include
		${CMAKE_SOURCE_DIR}/libs/nova_flare/include
	)

	if( ARG_LIBS )
		target_link_libraries( ${TARGET_NAME} PRIVATE ${ARG_LIBS} )
	endif()

	enable_libfuzzer( ${TARGET_NAME} )

	# place all fuzz binaries in build/fuzz/ regardless of where CMake is
	# invoked from, so the workflow run step can reference a fixed path
	set_target_properties( ${TARGET_NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz
	)
endfunction()

# ============================================================================
# Fuzz targets
# ============================================================================

# TruncatingRecordBuilder: exercises string/char append and truncation boundary
add_nova_fuzz_target( fuzz_record_builder
	fuzz_record_builder.cpp
	LIBS nova
)

# ContinuationRecordBuilder: exercises continuation emission and buffer fill/flush
add_nova_fuzz_target( fuzz_continuation_builder
	fuzz_continuation_builder.cpp
	LIBS nova nova_extras
)

# Flare Scanner: exercises magic search, size validation, and resynchronization
add_nova_fuzz_target( fuzz_flare_scanner
	fuzz_flare_scanner.cpp
	LIBS nova_flare
)

# Flare Reader: exercises TLV parser in isolation with arbitrary byte inputs
add_nova_fuzz_target( fuzz_flare_reader
	fuzz_flare_reader.cpp
	LIBS nova_flare
)
