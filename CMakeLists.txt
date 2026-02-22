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

# build options
option( NOVA_BUILD_EXTRAS "Build Nova Extras library" ON )
option( NOVA_BUILD_FLARE "Build Flare emergency logging library" ON )
option( NOVA_BUILD_EXAMPLES "Build example programs" OFF )
option( NOVA_BUILD_TESTS "Build unit tests" OFF )

# C++17 required
set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

# ============================================================================
# Nova Core (Header-Only)
# ============================================================================

# collect all header files for IDE support
file( GLOB_RECURSE NOVA_CORE_HEADERS
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
message( STATUS "" )
message( STATUS "Components:" )
message( STATUS "  Nova Core:       ON (header-only)" )
message( STATUS "  Nova Extras:     ${NOVA_BUILD_EXTRAS}" )
message( STATUS "  Flare:           ${NOVA_BUILD_FLARE}" )
message( STATUS "" )
message( STATUS "Optional:" )
message( STATUS "  Examples:        ${NOVA_BUILD_EXAMPLES}" )
message( STATUS "  Tests:           ${NOVA_BUILD_TESTS}" )
message( STATUS "" )
message( STATUS "Install Prefix:    ${CMAKE_INSTALL_PREFIX}" )
message( STATUS "===========================================" )
message( STATUS "" )
