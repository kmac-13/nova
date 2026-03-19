# =============================================================================
# Toolchain: arm-none-eabi (ARM Cortex-M4, bare-metal)
# =============================================================================
#
# Targets ARM Cortex-M4 with FPU (e.g. STM32F4, STM32F7, NXP i.MX RT).
# Adjust the CPU/FPU flags below for your specific variant - see the README
# in this directory for a table of common Cortex-M targets and their flags.
#
# Usage:
#   cmake -B build \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-cortex-m4.cmake \
#     -DNOVA_PLATFORM_BARE_METAL=ON \
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
# CPU / FPU flags
# Cortex-M4 with FPU (single-precision hardware float).
# For software float (no FPU), replace with:
#   -mfloat-abi=soft
# For Cortex-M4 without FPU, remove -mfpu entirely.
# -----------------------------------------------------------------------------
set( CPU_FLAGS
	-mcpu=cortex-m4
	-mthumb
	-mfpu=fpv4-sp-d16
	-mfloat-abi=hard
)

# -----------------------------------------------------------------------------
# Common compiler flags
# -fno-exceptions and -fno-rtti are required for bare-metal C++ without
# a C++ runtime that supports them.
# -----------------------------------------------------------------------------
set( COMMON_FLAGS
	${CPU_FLAGS}
	-fno-exceptions
	-fno-rtti
	-ffunction-sections    # allow linker to GC unused functions
	-fdata-sections        # allow linker to GC unused data
	-Wall
	-Wextra
)

string( JOIN " " COMMON_FLAGS_STR ${COMMON_FLAGS} )

set( CMAKE_C_FLAGS_INIT   "${COMMON_FLAGS_STR}" )
set( CMAKE_CXX_FLAGS_INIT "${COMMON_FLAGS_STR} -std=c++17" )
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
# Sysroot (optional)
# Uncomment and set if your toolchain has a dedicated sysroot:
# set( CMAKE_SYSROOT /path/to/arm-none-eabi/sysroot )
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# Search paths
# Prevent CMake from finding host system headers/libraries.
# -----------------------------------------------------------------------------
set( CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER )
set( CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY  )
set( CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY  )
set( CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY  )
