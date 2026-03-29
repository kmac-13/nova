cmake_minimum_required( VERSION 3.16 )

project( Nova VERSION 0.1.0 LANGUAGES CXX )

# Note: This CMakeLists.txt uses GLOB to automatically discover source files.
# While CMake documentation traditionally discourages GLOB, it's acceptable here because:
# 1. Nova is typically used as a subproject (add_subdirectory or FetchContent)
# 2. Adding new files naturally triggers a reconfigure when the parent project rebuilds
# 3. Simplifies maintenance when files are added/removed
# 4. IDE integration benefits from seeing all headers
#
# If you prefer explicit file lists, you can replace the file(GLOB_RECURSE ...) commands
# with explicit lists of source files.

# ============================================================================
# Load Sanitizers Helper Module
# ============================================================================
include( ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Sanitizers.cmake )

# build options
option( NOVA_BUILD_EXTRAS        "Build Nova Extras library"             ON  )
option( NOVA_BUILD_FLARE         "Build Flare emergency logging library" ON  )
option( NOVA_BUILD_EXAMPLES      "Build example programs"                OFF )
option( NOVA_BUILD_TESTS         "Build unit tests"                      OFF )
option( NOVA_BUILD_FUZZ          "Build LibFuzzer fuzz targets"          OFF )
option( NOVA_PLATFORM_BARE_METAL "Configure for bare-metal targets"      OFF )
option( NOVA_PLATFORM_RTOS       "Configure for RTOS targets"            OFF )

# ============================================================================
# Bare-Metal Mode
# ============================================================================

if( NOVA_PLATFORM_BARE_METAL )
	message( STATUS "Nova: bare-metal mode enabled" )

	# Nova Extras requires std::thread, std::mutex, FILE* - not available
	# on bare-metal targets.  Disable unconditionally.
	if( NOVA_BUILD_EXTRAS )
		message( STATUS "  Nova Extras: disabled (not available in bare-metal mode)" )
		set( NOVA_BUILD_EXTRAS OFF CACHE BOOL "Build Nova Extras library" FORCE )
	endif()

	# Flare's FileWriter depends on POSIX file descriptors.  Bare-metal targets
	# must provide their own IWriter implementation.  Disable the compiled library
	# so users are not forced to stub out FileWriter; they link against Nova core
	# and Flare headers directly with their own IWriter.
	if( NOVA_BUILD_FLARE )
		message( STATUS "  Flare: disabled (FileWriter requires POSIX; provide IWriter instead)" )
		set( NOVA_BUILD_FLARE OFF CACHE BOOL "Build Flare library" FORCE )
	endif()

	# Tests and examples both depend on Nova Extras - disable them too.
	if( NOVA_BUILD_TESTS )
		message( STATUS "  Tests: disabled (require Nova Extras)" )
		set( NOVA_BUILD_TESTS OFF CACHE BOOL "Build unit tests" FORCE )
	endif()

	if( NOVA_BUILD_EXAMPLES )
		message( STATUS "  Examples: disabled (require Nova Extras)" )
		set( NOVA_BUILD_EXAMPLES OFF CACHE BOOL "Build example programs" FORCE )
	endif()

	# Pass NOVA_BARE_METAL to all targets that link against Nova core so the
	# platform abstraction layer activates correctly.
	add_compile_definitions( NOVA_BARE_METAL )
endif()

# ============================================================================
# RTOS Mode
# ============================================================================

if( NOVA_PLATFORM_RTOS )
	message( STATUS "Nova: RTOS mode enabled" )

	# Nova Extras requires std::thread and std::mutex which are not available
	# on most RTOS targets without a POSIX compatibility layer.  Disable
	# unconditionally; users who have a POSIX layer can re-enable manually.
	if( NOVA_BUILD_EXTRAS )
		message( STATUS "  Nova Extras: disabled (std::thread/mutex not available on RTOS)" )
		set( NOVA_BUILD_EXTRAS OFF CACHE BOOL "Build Nova Extras library" FORCE )
	endif()

	# Tests and examples depend on Nova Extras.
	if( NOVA_BUILD_TESTS )
		message( STATUS "  Tests: disabled (require Nova Extras)" )
		set( NOVA_BUILD_TESTS OFF CACHE BOOL "Build unit tests" FORCE )
	endif()

	if( NOVA_BUILD_EXAMPLES )
		message( STATUS "  Examples: disabled (require Nova Extras)" )
		set( NOVA_BUILD_EXAMPLES OFF CACHE BOOL "Build example programs" FORCE )
	endif()

	# Pass NOVA_RTOS to all targets so the platform abstraction layer
	# activates correctly (notably NOVA_NO_TLS).
	add_compile_definitions( NOVA_RTOS )
endif()

# C++17 required
set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

# ============================================================================
# Nova Core (Header-Only)
# ============================================================================

# collect all header files for IDE support
file( GLOB_RECURSE NOVA_CORE_HEADERS
	RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
	"nova/include/*.h"
	"nova/include/*.hpp"
)

add_library( nova INTERFACE )
add_library( Nova::Core ALIAS nova )

target_include_directories( nova INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/nova/include>
	$<INSTALL_INTERFACE:include>
)

target_compile_features( nova INTERFACE cxx_std_17 )

# add headers to target for IDE support
target_sources( nova INTERFACE
	$<BUILD_INTERFACE:${NOVA_CORE_HEADERS}>
)

# export Nova core
set_target_properties( nova PROPERTIES
	EXPORT_NAME Core
)

message( STATUS "Nova Core: Header-only library configured" )

# ============================================================================
# Nova Extras (Optional)
# ============================================================================

if( NOVA_BUILD_EXTRAS )
	# collect all header files (for IDE support)
	file( GLOB_RECURSE NOVA_EXTRAS_HEADERS
		"nova_extras/include/*.h"
		"nova_extras/include/*.hpp"
	)

	# collect all source files
	file( GLOB_RECURSE NOVA_EXTRAS_SOURCES
		"nova_extras/src/*.cpp"
		"nova_extras/src/*.cc"
		"nova_extras/src/*.cxx"
	)

	add_library( nova_extras ${NOVA_EXTRAS_SOURCES} ${NOVA_EXTRAS_HEADERS} )
	add_library( Nova::Extras ALIAS nova_extras )

	target_include_directories( nova_extras PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/nova_extras/include>
		$<INSTALL_INTERFACE:include>
	)

	target_link_libraries( nova_extras PUBLIC nova )

	target_compile_features( nova_extras PUBLIC cxx_std_17 )

	set_target_properties( nova_extras PROPERTIES
		EXPORT_NAME Extras
		VERSION ${PROJECT_VERSION}
		SOVERSION ${PROJECT_VERSION_MAJOR}
	)

	message( STATUS "Nova Extras: Library configured with ${CMAKE_CURRENT_LIST_DIR}/nova_extras/src sources" )
else()
	message( STATUS "Nova Extras: Disabled (set NOVA_BUILD_EXTRAS=ON to enable)" )
endif()

# ============================================================================
# Flare (Optional)
# ============================================================================

if( NOVA_BUILD_FLARE )
	# collect all header files (for IDE support)
	file( GLOB_RECURSE FLARE_HEADERS
		"flare/include/*.h"
		"flare/include/*.hpp"
	)

	# collect all source files
	file( GLOB_RECURSE FLARE_SOURCES
		"flare/src/*.cpp"
		"flare/src/*.cc"
		"flare/src/*.cxx"
	)

	add_library( flare ${FLARE_SOURCES} ${FLARE_HEADERS} )
	add_library( Nova::Flare ALIAS flare )

	target_include_directories( flare PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/flare/include>
		$<INSTALL_INTERFACE:include>
	)

	target_link_libraries( flare PUBLIC nova )

	target_compile_features( flare PUBLIC cxx_std_17 )

	set_target_properties( flare PROPERTIES
		EXPORT_NAME Flare
		VERSION ${PROJECT_VERSION}
		SOVERSION ${PROJECT_VERSION_MAJOR}
	)

	message( STATUS "Flare: Library configured with ${CMAKE_CURRENT_LIST_DIR}/flare/src sources" )
else()
	message( STATUS "Flare: Disabled (set NOVA_BUILD_FLARE=ON to enable)" )
endif()

# ============================================================================
# Examples (Optional)
# ============================================================================

if( NOVA_BUILD_EXAMPLES )
	if( NOT NOVA_BUILD_EXTRAS )
		message( WARNING "Examples require Nova Extras - enabling NOVA_BUILD_EXTRAS" )
		set( NOVA_BUILD_EXTRAS ON CACHE BOOL "Build Nova Extras library" FORCE )
	endif()

	add_subdirectory( examples )
	message( STATUS "Examples: Enabled" )
else()
	message( STATUS "Examples: Disabled (set NOVA_BUILD_EXAMPLES=ON to enable)" )
endif()

# ============================================================================
# Tests (Optional)
# ============================================================================

if( NOVA_BUILD_TESTS )
	enable_testing()

	if( NOT NOVA_BUILD_EXTRAS )
		message( WARNING "Tests require Nova Extras - enabling NOVA_BUILD_EXTRAS" )
		set( NOVA_BUILD_EXTRAS ON CACHE BOOL "Build Nova Extras library" FORCE )
	endif()

	if( NOT NOVA_BUILD_FLARE )
		message( WARNING "Tests require Flare - enabling NOVA_BUILD_FLARE" )
		set( NOVA_BUILD_FLARE ON CACHE BOOL "Build Flare library" FORCE )
	endif()

	add_subdirectory( tests )
	message( STATUS "Tests: Enabled" )
else()
	message( STATUS "Tests: Disabled (set NOVA_BUILD_TESTS=ON to enable)" )
endif()

# ============================================================================
# Fuzz Targets (Optional)
# ============================================================================

if( NOVA_BUILD_FUZZ )
	# fuzz targets require clang++ (LibFuzzer is a clang-only tool)
	if( NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
		message( FATAL_ERROR
			"NOVA_BUILD_FUZZ requires clang++.  "
			"Set -DCMAKE_CXX_COMPILER=clang++ and -DCMAKE_C_COMPILER=clang." )
	endif()

	if( NOT NOVA_BUILD_EXTRAS )
		message( STATUS
			"  Fuzz: enabling NOVA_BUILD_EXTRAS (required by fuzz_continuation_builder)" )
		set( NOVA_BUILD_EXTRAS ON CACHE BOOL "Build Nova Extras library" FORCE )
	endif()

	if( NOT NOVA_BUILD_FLARE )
		message( STATUS
			"  Fuzz: enabling NOVA_BUILD_FLARE (required by fuzz_flare_scanner and fuzz_flare_reader)" )
		set( NOVA_BUILD_FLARE ON CACHE BOOL "Build Flare library" FORCE )
	endif()

	add_subdirectory( fuzz )
	message( STATUS "Fuzz targets: Enabled" )
else()
	message( STATUS "Fuzz targets: Disabled (set NOVA_BUILD_FUZZ=ON with clang++ to enable)" )
endif()

# ============================================================================
# Installation
# ============================================================================

include( GNUInstallDirs )
include( CMakePackageConfigHelpers )

# install Nova core headers
install( DIRECTORY nova/include/
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
	FILES_MATCHING PATTERN "*.h"
)

# install Nova Extras (if built)
if( NOVA_BUILD_EXTRAS )
	install( TARGETS nova_extras
		EXPORT NovaTargets
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	)

	install( DIRECTORY nova_extras/include/
		DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
		FILES_MATCHING PATTERN "*.h"
	)
endif()

# install Flare (if built)
if( NOVA_BUILD_FLARE )
	install( TARGETS flare
		EXPORT NovaTargets
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	)

	install( DIRECTORY flare/include/
		DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
		FILES_MATCHING PATTERN "*.h"
	)
endif()

# install Nova core target
install( TARGETS nova
	EXPORT NovaTargets
)

# generate and install CMake config files
write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/NovaConfigVersion.cmake"
	VERSION ${PROJECT_VERSION}
	COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake/NovaConfig.cmake.in"
	"${CMAKE_CURRENT_BINARY_DIR}/NovaConfig.cmake"
	INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nova
)

install( FILES
	"${CMAKE_CURRENT_BINARY_DIR}/NovaConfig.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/NovaConfigVersion.cmake"
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nova
)

install( EXPORT NovaTargets
	FILE NovaTargets.cmake
	NAMESPACE Nova::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nova
)

# ============================================================================
# Print Configuration Summary
# ============================================================================

message( STATUS "" )
message( STATUS "===========================================" )
message( STATUS "Nova Configuration Summary" )
message( STATUS "===========================================" )
message( STATUS "Version:           ${PROJECT_VERSION}" )
message( STATUS "C++ Standard:      C++${CMAKE_CXX_STANDARD}" )
message( STATUS "Build Type:        ${CMAKE_BUILD_TYPE}" )
message( STATUS "Target Platform:   ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}" )
message( STATUS "" )
message( STATUS "Components:" )
message( STATUS "  Nova Core:       ON (header-only)" )
message( STATUS "  Nova Extras:     ${NOVA_BUILD_EXTRAS}" )
message( STATUS "  Flare:           ${NOVA_BUILD_FLARE}" )
message( STATUS "" )
message( STATUS "Optional:" )
message( STATUS "  Examples:        ${NOVA_BUILD_EXAMPLES}" )
message( STATUS "  Tests:           ${NOVA_BUILD_TESTS}" )
message( STATUS "  Fuzz targets:    ${NOVA_BUILD_FUZZ}" )
message( STATUS "" )
message( STATUS "Platform:" )
message( STATUS "  Bare-Metal Mode: ${NOVA_PLATFORM_BARE_METAL}" )
message( STATUS "  RTOS Mode:       ${NOVA_PLATFORM_RTOS}" )
message( STATUS "" )
message( STATUS "Install Prefix:    ${CMAKE_INSTALL_PREFIX}" )
message( STATUS "===========================================" )
message( STATUS "" )
