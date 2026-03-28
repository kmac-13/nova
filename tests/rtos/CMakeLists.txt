cmake_minimum_required( VERSION 3.16 )
project( NovaRtosTest LANGUAGES C CXX ASM )

set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

# ============================================================================
# FreeRTOS-Kernel via FetchContent (pinned tag, no submodule)
# ============================================================================
#
# FREERTOS_PORT selects the portable layer.
#   GCC_POSIX   - hosted Linux simulation (native execution, no QEMU needed)
#   GCC_ARM_CM3 - Cortex-M3 (targets mps2-an385 under QEMU)
#
# FREERTOS_HEAP selects the heap implementation.
#   4 - best-fit with coalescing; adequate for the test binary's small
#       fixed-allocation pattern.  heap_3 (malloc wrapper) is unavailable
#       on bare-metal where malloc is a stub.
#
# The port variable must be set before FetchContent_MakeAvailable so the
# FreeRTOS-Kernel CMakeLists picks it up when it calls
# add_subdirectory(portable/${FREERTOS_PORT}).

include( FetchContent )

FetchContent_Declare(
	freertos_kernel
	GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
	GIT_TAG V11.1.0
	GIT_SHALLOW TRUE
)

if( CMAKE_CROSSCOMPILING )
	set( FREERTOS_PORT "GCC_ARM_CM3" CACHE STRING "" FORCE )
else()
	set( FREERTOS_PORT "GCC_POSIX"   CACHE STRING "" FORCE )
endif()

set( FREERTOS_HEAP "4" CACHE STRING "" FORCE )

# freertos_config target: the FreeRTOS-Kernel CMake build requires a
# user-supplied interface target named 'freertos_config' that exposes the
# include path containing FreeRTOSConfig.h
add_library( freertos_config INTERFACE )
target_include_directories( freertos_config INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/freertos_config
)

# POSIX port requires _GNU_SOURCE for pthread extensions (pthread_setname_np,
# sigaction with SA_RESTART, etc), which is harmless on ARM (ignored by the port)
target_compile_definitions( freertos_config INTERFACE
	_GNU_SOURCE
	$<$<NOT:$<BOOL:${CMAKE_CROSSCOMPILING}>>:projCOVERAGE_TEST=0>
)

# Pass CPU flags through freertos_config so the FreeRTOS kernel subproject
# inherits them.  Without this, FetchContent compiles the kernel without
# -mcpu=cortex-m3, __ARM_ARCH_7M__ is not defined, and FreeRTOSConfig.h's
# ARM-specific block (configCPU_CLOCK_HZ, NVIC priorities, handler names) is
# skipped entirely, causing configASSERT failures at scheduler startup.
if( CMAKE_CROSSCOMPILING )
	set( CMAKE_C_FLAGS
		"${CMAKE_C_FLAGS} -mcpu=cortex-m3 -mthumb -mfloat-abi=soft"
		CACHE STRING "" FORCE
	)
	set( CMAKE_CXX_FLAGS
		"${CMAKE_CXX_FLAGS} -mcpu=cortex-m3 -mthumb -mfloat-abi=soft"
		CACHE STRING "" FORCE
	)
endif()

FetchContent_MakeAvailable( freertos_kernel )

# ============================================================================
# Nova core include path
# ============================================================================

# Nova core is header-only; just add the include directory directly rather
# than depending on the parent CMakeLists (which may not be in scope when
# this subdirectory is built standalone via -DNOVA_RTOS_TEST_STANDALONE=ON)
set( NOVA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../nova/include" )
if( NOT EXISTS "${NOVA_INCLUDE_DIR}/kmac/nova/nova.h" )
	message( FATAL_ERROR
	    "Nova core headers not found at ${NOVA_INCLUDE_DIR}.  "
	    "Run CMake from the repository root or set NOVA_INCLUDE_DIR manually." )
endif()

# ============================================================================
# test_nova_rtos executable
# ============================================================================

set( TEST_SOURCES
	test_nova_rtos.cpp
	qemu/semihosting.c
)

# startup file is only needed for the ARM cross-compile
if( CMAKE_CROSSCOMPILING )
	list( APPEND TEST_SOURCES qemu/startup_lm3s6965evb.c )
endif()

add_executable( test_nova_rtos ${TEST_SOURCES} )

target_include_directories( test_nova_rtos PRIVATE
	${NOVA_INCLUDE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}  # for qemu/semihosting.h
)

target_link_libraries( test_nova_rtos PRIVATE
	freertos_kernel
	freertos_kernel_port
)

# NOVA_RTOS tells Nova's platform/config.h to activate the RTOS path
# (NOVA_NO_TLS implied).  NOVA_PLATFORM_FREERTOS is intentionally omitted:
# Nova auto-detects FreeRTOS from the INC_FREERTOS_H macro that FreeRTOS.h
# defines when included, so repeating it here causes a redefinition warning.
target_compile_definitions( test_nova_rtos PRIVATE NOVA_RTOS )

if( CMAKE_CROSSCOMPILING )
	# NOVA_NO_CHARCONV: arm-none-eabi with newlib-nano does not provide a
	#   working std::to_chars, so Nova's fallback implementations in
	#   platform/int_to_chars.h and platform/float_to_chars.h are used instead.
	#
	# NOVA_NO_ATOMIC: std::atomic on arm-none-eabi requires libatomic for load/
	#   store on Cortex-M3 (no native atomic instructions), which is not linked
	#   here.  The volatile-pointer fallback in platform/atomic.h is safe for
	#   this test: sink binding happens from a single task with no concurrent
	#   access.
	target_compile_definitions( test_nova_rtos PRIVATE
		NOVA_NO_CHARCONV
		NOVA_NO_ATOMIC
	)
endif()

# ============================================================================
# ARM linker script
# ============================================================================

if( CMAKE_CROSSCOMPILING )
	target_link_options( test_nova_rtos PRIVATE
		-T${CMAKE_CURRENT_SOURCE_DIR}/qemu/mps2-an385.ld
		-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/test_nova_rtos.map
	)

	# print section sizes after link to help diagnose flash/RAM overflows
	add_custom_command( TARGET test_nova_rtos POST_BUILD
		COMMAND ${CMAKE_SIZE} $<TARGET_FILE:test_nova_rtos>
		COMMENT "Section sizes for test_nova_rtos"
	)
endif()

# ============================================================================
# POSIX port: link pthreads
# ============================================================================

if( NOT CMAKE_CROSSCOMPILING )
	find_package( Threads REQUIRED )
	target_link_libraries( test_nova_rtos PRIVATE Threads::Threads )
endif()

# ============================================================================
# Summary
# ============================================================================

if( CMAKE_CROSSCOMPILING )
	message( STATUS "Nova RTOS test: ARM Cortex-M3 / mps2-an385 (QEMU)" )
else()
	message( STATUS "Nova RTOS test: POSIX simulator (hosted)" )
endif()
