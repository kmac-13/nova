# Nova CMake Toolchain Files

Cross-compilation toolchain files for Nova.  Each file is a starting point - copy and adapt it for your specific hardware rather than modifying it in place.

---

## arm-none-eabi-cortex-m4.cmake

Targets ARM Cortex-M4 with FPU using the `arm-none-eabi` GCC toolchain.

**Prerequisites**

The `arm-none-eabi-g++` toolchain must be on `PATH`, or the compiler path provided explicitly:

```sh
cmake -B build \
	-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-cortex-m4.cmake \
	-DCMAKE_C_COMPILER=/opt/arm-none-eabi/bin/arm-none-eabi-gcc \
	-DCMAKE_CXX_COMPILER=/opt/arm-none-eabi/bin/arm-none-eabi-g++ \
	-DNOVA_PLATFORM_BARE_METAL=ON \
	-DCMAKE_BUILD_TYPE=Release
```

**Adapting for other Cortex-M variants**

| Target | `-mcpu` | `-mfpu` | `-mfloat-abi` |
|---|---|---|---|
| Cortex-M0 / M0+ | `cortex-m0` | - | `soft` |
| Cortex-M3 | `cortex-m3` | - | `soft` |
| Cortex-M4 (no FPU) | `cortex-m4` | - | `soft` |
| Cortex-M4 (FPU) | `cortex-m4` | `fpv4-sp-d16` | `hard` |
| Cortex-M7 (SP FPU) | `cortex-m7` | `fpv5-sp-d16` | `hard` |
| Cortex-M7 (DP FPU) | `cortex-m7` | `fpv5-d16` | `hard` |
| Cortex-M33 | `cortex-m33` | `fpv5-sp-d16` | `hard` |

Copy the toolchain file and update the three CPU/FPU lines in the `CPU_FLAGS`
block near the top.

**Linker script**

The toolchain file does not include a linker script - that is hardware-specific.  Add yours to your application target:

```cmake
target_link_options( my_app PRIVATE
	-T${CMAKE_SOURCE_DIR}/linker/stm32f407.ld
)
```

**newlib-nano**

The default flags use `--specs=nosys.specs --specs=nano.specs` which stubs out OS syscalls and uses the smaller newlib-nano C library.  If your BSP provides its own syscall stubs, remove `nosys.specs`.

---

## android-ndk.cmake

Thin wrapper around the NDK's own toolchain file.  Handles ABI selection, API level, STL choice, and sysroot setup.

**Prerequisites**

The Android NDK must be installed.  On GitHub Actions runners it is available via `$ANDROID_NDK_HOME`. Locally, download from [developer.android.com/ndk/downloads](https://developer.android.com/ndk/downloads) and set `ANDROID_NDK_HOME`.

**Usage**

```sh
cmake -B build/arm64 \
	-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/android-ndk.cmake \
	-DANDROID_ABI=arm64-v8a \
	-DANDROID_PLATFORM=android-26 \
	-DCMAKE_BUILD_TYPE=Release

cmake -B build/armv7 \
	-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/android-ndk.cmake \
	-DANDROID_ABI=armeabi-v7a \
	-DANDROID_PLATFORM=android-26 \
	-DCMAKE_BUILD_TYPE=Release

cmake -B build/x86_64 \
	-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/android-ndk.cmake \
	-DANDROID_ABI=x86_64 \
	-DANDROID_PLATFORM=android-26 \
	-DCMAKE_BUILD_TYPE=Release
```

**Key parameters**

| Parameter | Default | Notes |
|---|---|---|
| `NDK_PATH` | auto-detected | Explicit NDK root; falls back to `$ANDROID_NDK_HOME` |
| `ANDROID_ABI` | `arm64-v8a` | `arm64-v8a`, `armeabi-v7a`, or `x86_64` |
| `ANDROID_PLATFORM` | `android-26` | Minimum API level |
| `ANDROID_STL` | `c++_static` | `c++_static` for libraries, `c++_shared` for apps |

**API level guidance**

Nova requires C++17.  NDK C++17 support is solid from API 21 onwards.  API 26 is the recommended minimum - it is the threshold for 64-bit-only devices and provides full `clock_gettime` and pthread support without polyfills.

**STL choice**

Use `c++_static` when shipping Nova as part of a `.so` to avoid STL ABI conflicts between libraries.  Use `c++_shared` only when you control all libraries in the process and can guarantee a single STL instance.

**TLS on JNI-attached threads**

Threads attached to the JVM via `AttachCurrentThread()` have a smaller default stack than native pthreads.  If Nova is used in a library loaded into a Java-driven process, consider defining `NOVA_NO_TLS` to use stack-based builders everywhere and avoid injecting per-thread state into the host process:

```cmake
target_compile_definitions( my_lib PRIVATE NOVA_NO_TLS )
```

---

## Adding a new target

1. copy the closest existing toolchain file
2. update `CMAKE_SYSTEM_NAME`, `CMAKE_SYSTEM_PROCESSOR`, compiler names, and CPU flags for your target
3. set `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY` if the target cannot link executables during CMake configuration (typical for bare-metal)
4. add a corresponding entry to the build matrix CI workflow if you want automated build verification
