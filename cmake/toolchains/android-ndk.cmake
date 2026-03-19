# =============================================================================
# Toolchain: Android NDK (Bionic)
# =============================================================================
#
# Thin wrapper around the NDK's own toolchain file.  The NDK ships a complete
# CMake toolchain at $NDK/build/cmake/android.toolchain.cmake which handles
# ABI selection, API level, STL choice, and sysroot setup.  This file sets
# sensible defaults for Nova and delegates to it.
#
# Usage:
#   cmake -B build \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/android-ndk.cmake \
#     -DANDROID_ABI=arm64-v8a \
#     -DANDROID_PLATFORM=android-26 \
#     -DNDK_PATH=/path/to/ndk
#
# Or rely on $ANDROID_NDK_HOME (set automatically on GitHub Actions runners):
#   cmake -B build \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/android-ndk.cmake \
#     -DANDROID_ABI=arm64-v8a
#
# Supported ABI values: arm64-v8a, armeabi-v7a, x86_64
# (x86 is 32-bit and deprecated; omitted intentionally)
# =============================================================================

# -----------------------------------------------------------------------------
# Locate the NDK
# Preference order: -DNDK_PATH, $ANDROID_NDK_HOME, $ANDROID_NDK, $ANDROID_HOME/ndk/*
# -----------------------------------------------------------------------------
if( NOT DEFINED NDK_PATH )
	if( DEFINED ENV{ANDROID_NDK_HOME} )
		set( NDK_PATH "$ENV{ANDROID_NDK_HOME}" )
	elseif( DEFINED ENV{ANDROID_NDK} )
		set( NDK_PATH "$ENV{ANDROID_NDK}" )
	elseif( DEFINED ENV{ANDROID_HOME} )
		# Pick the highest version under $ANDROID_HOME/ndk/
		file( GLOB _ndk_candidates "$ENV{ANDROID_HOME}/ndk/*" )
		if( _ndk_candidates )
			list( SORT _ndk_candidates ORDER DESCENDING )
			list( GET _ndk_candidates 0 NDK_PATH )
		endif()
	endif()
endif()

if( NOT NDK_PATH OR NOT EXISTS "${NDK_PATH}" )
	message( FATAL_ERROR
		"Android NDK not found. Provide the path via:\n"
		"  -DNDK_PATH=/path/to/ndk\n"
		"  or set ANDROID_NDK_HOME, ANDROID_NDK, or ANDROID_HOME environment variables."
	)
endif()

message( STATUS "Android NDK: ${NDK_PATH}" )

# -----------------------------------------------------------------------------
# Defaults (override on the cmake command line)
# -----------------------------------------------------------------------------

# ABI: arm64-v8a | armeabi-v7a | x86_64
if( NOT DEFINED ANDROID_ABI )
	set( ANDROID_ABI "arm64-v8a" )
endif()

# Minimum API level.  Nova requires C++17; NDK C++17 support is solid from API 21.
# API 26 is recommended - it is the minimum for 64-bit-only devices and gives
# access to full Bionic pthread and clock_gettime without polyfills.
if( NOT DEFINED ANDROID_PLATFORM )
	set( ANDROID_PLATFORM "android-26" )
endif()

# STL: c++_static (recommended for libraries to avoid ABI issues),
# c++_shared (for applications or when multiple .so files share state)
if( NOT DEFINED ANDROID_STL )
	set( ANDROID_STL "c++_static" )
endif()

message( STATUS "Android ABI:      ${ANDROID_ABI}" )
message( STATUS "Android platform: ${ANDROID_PLATFORM}" )
message( STATUS "Android STL:      ${ANDROID_STL}" )

# -----------------------------------------------------------------------------
# Delegate to the NDK's own toolchain file
# -----------------------------------------------------------------------------
set( _ndk_toolchain "${NDK_PATH}/build/cmake/android.toolchain.cmake" )

if( NOT EXISTS "${_ndk_toolchain}" )
	message( FATAL_ERROR
		"NDK toolchain file not found at:\n  ${_ndk_toolchain}\n"
		"Check that NDK_PATH points to the root of the NDK installation."
	)
endif()

include( "${_ndk_toolchain}" )
