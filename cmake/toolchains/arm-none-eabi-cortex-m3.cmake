# =============================================================================
# Toolchain: arm-none-eabi (ARM Cortex-M3, bare-metal)
# =============================================================================
#
# Targets ARM Cortex-M3 (e.g. STM32F103, STM32F205, lm3s6965).
# Cortex-M3 has no hardware FPU; floating-point is handled in software.
#
# This toolchain is used for the RTOS CI job because Cortex-M3 matches the
# lm3s6965evb QEMU machine target (the standard FreeRTOS demo board), keeping
# the option open for a future QEMU execution step.
#
# Usage:
#   cmake -B build \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-cortex-m3.cmake \
#     -DCMAKE_BUILD_TYPE=Release
#
# The toolchain binary must be on PATH, or set the full path via:
#   -DCMAKE_C_COMPILER=/path/to/arm-none-eabi-gcc
#   -DCMAKE_CXX_COMPILER=/path/to/arm-none-eabi-g++
# =============================================================================

# Tell CMake this is a bare-metal target (no OS)
set( CMAKE_SYSTEM_NAME      Generic )
set( CMAKE_SYSTEM_PROCESSOR arm )

# -----------------------------------------------------------------------------
# Toolchain binaries
# Defaults to arm-none-eabi-* on PATH.  Override with -DCMAKE_C_COMPILER=...
# -----------------------------------------------------------------------------
set( CMAKE_C_COMPILER   arm-none-eabi-gcc )
set( CMAKE_CXX_COMPILER arm-none-eabi-g++ )
set( CMAKE_ASM_COMPILER arm-none-eabi-gcc )
set( CMAKE_AR           arm-none-eabi-ar )
set( CMAKE_OBJCOPY      arm-none-eabi-objcopy )
set( CMAKE_OBJDUMP      arm-none-eabi-objdump )
set( CMAKE_SIZE         arm-none-eabi-size )

# -----------------------------------------------------------------------------
# CPU flags
# Cortex-M3 has no FPU; software float only.
# -----------------------------------------------------------------------------
set( CPU_FLAGS
	-mcpu=cortex-m3
	-mthumb
	-mfloat-abi=soft
)

# -----------------------------------------------------------------------------
# Common compiler flags
# -fno-exceptions and -fno-rtti are required for bare-metal C++ without
# a C++ runtime that supports them.
# -----------------------------------------------------------------------------
set( COMMON_FLAGS
	${CPU_FLAGS}
	-ffunction-sections    # allow linker to GC unused functions
	-fdata-sections        # allow linker to GC unused data
	-Wall
	-Wextra
)

# -fno-exceptions and -fno-rtti are C++ only; applying them to C files
# produces spurious "valid for C++/D/ObjC++ but not for C" warnings.
set( CXX_ONLY_FLAGS
	-fno-exceptions
	-fno-rtti
)

string( JOIN " " COMMON_FLAGS_STR ${COMMON_FLAGS} )
string( JOIN " " CXX_ONLY_FLAGS_STR ${CXX_ONLY_FLAGS} )

set( CMAKE_C_FLAGS_INIT   "${COMMON_FLAGS_STR}" )
set( CMAKE_CXX_FLAGS_INIT "${COMMON_FLAGS_STR} ${CXX_ONLY_FLAGS_STR} -std=c++17" )
set( CMAKE_ASM_FLAGS_INIT "${COMMON_FLAGS_STR} -x assembler-with-cpp" )

# -----------------------------------------------------------------------------
# Linker flags
# --gc-sections: discard unused sections (pairs with -ffunction/data-sections)
# --specs=nosys.specs: stub out OS syscalls (newlib-nano)
# --specs=nano.specs: use newlib-nano (smaller footprint)
# Provide your own linker script via:
#   target_link_options( my_target PRIVATE -T${CMAKE_SOURCE_DIR}/linker.ld )
# -----------------------------------------------------------------------------
set( CMAKE_EXE_LINKER_FLAGS_INIT
	"${COMMON_FLAGS_STR} -Wl,--gc-sections --specs=nosys.specs --specs=nano.specs"
)

# -----------------------------------------------------------------------------
# Compiler identification
# CMake's try_compile uses a static library (not an executable) to avoid
# needing a linker script during configuration.
# -----------------------------------------------------------------------------
set( CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY )

# -----------------------------------------------------------------------------
# Search paths
# Prevent CMake from finding host system headers/libraries.
# -----------------------------------------------------------------------------
set( CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER )
set( CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY  )
set( CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY  )
set( CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY  )
