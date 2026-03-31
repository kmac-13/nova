cmake_minimum_required( VERSION 3.16 )
project( NovaBareMetalTest LANGUAGES C CXX ASM )

set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

# ============================================================================
# Nova core include path
# ============================================================================

# Nova core is header-only; add the include directory directly rather than
# depending on the parent CMakeLists so this project can be built standalone.
set( NOVA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/nova/include" )
if( NOT EXISTS "${NOVA_INCLUDE_DIR}/kmac/nova/nova.h" )
	message( FATAL_ERROR
		"Nova core headers not found at ${NOVA_INCLUDE_DIR}.  "
		"Run CMake from the repository root or set NOVA_INCLUDE_DIR manually." )
endif()

# ============================================================================
# test_nova_bare_metal executable
# ============================================================================

set( TEST_SOURCES
	test_nova_bare_metal.cpp
	qemu/semihosting.c
)

# startup file is only needed for the ARM cross-compile; on a hosted build the
# native C runtime provides equivalent initialisation
if( CMAKE_CROSSCOMPILING )
	list( APPEND TEST_SOURCES qemu/startup_cortex_m3.c )
endif()

add_executable( test_nova_bare_metal ${TEST_SOURCES} )

target_include_directories( test_nova_bare_metal PRIVATE
	${NOVA_INCLUDE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}  # for qemu/semihosting.h
)

# NOVA_BARE_METAL is defined in the source file so it is set before the first
# Nova header inclusion, which is the documented correct approach.
# No additional compile_definitions are required here.

if( CMAKE_CROSSCOMPILING )
	# NOVA_NO_CHARCONV: arm-none-eabi with newlib-nano does not provide a
	#   working std::to_chars, so Nova's fallback implementations in
	#   platform/int_to_chars.h and platform/float_to_chars.h are used instead.
	#   NOVA_BARE_METAL already implies this; the explicit definition here is
	#   belt-and-suspenders for clarity and to satisfy any static analyser that
	#   might not follow macro implication chains.
	#
	# NOVA_NO_ATOMIC: std::atomic on arm-none-eabi requires libatomic for load/
	#   store on Cortex-M3 (no native atomic instructions), which is not linked
	#   here.  The volatile-pointer fallback in platform/atomic.h is safe for
	#   this test: sink binding happens from main() with no concurrent access.
	#   NOVA_BARE_METAL already implies this.
	target_compile_definitions( test_nova_bare_metal PRIVATE
		NOVA_NO_CHARCONV
		NOVA_NO_ATOMIC
	)
endif()

# ============================================================================
# ARM linker script
# ============================================================================

if( CMAKE_CROSSCOMPILING )
	target_link_options( test_nova_bare_metal PRIVATE
		-T${CMAKE_CURRENT_SOURCE_DIR}/qemu/mps2-an385.ld
		-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/test_nova_bare_metal.map
	)

	# print section sizes after link to help diagnose flash/RAM overflows
	add_custom_command( TARGET test_nova_bare_metal POST_BUILD
		COMMAND ${CMAKE_SIZE} $<TARGET_FILE:test_nova_bare_metal>
		COMMENT "Section sizes for test_nova_bare_metal"
	)
endif()

# ============================================================================
# Summary
# ============================================================================

if( CMAKE_CROSSCOMPILING )
	message( STATUS "Nova bare-metal test: ARM Cortex-M3 / mps2-an385 (QEMU)" )
else()
	message( STATUS "Nova bare-metal test: hosted Linux (native execution)" )
endif()
