# CMake Integration Examples

This document shows how to use Nova in your CMake projects with different configurations.

## Method 1: add_subdirectory (Recommended for Development)

Add Nova as a subdirectory in your project:

### Option A: Include Everything (Nova + Extras + Flare)

```cmake
# your project's CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(ExampleApp)

# add Nova with all components (a common convention is external/ for dependencies)
add_subdirectory(external/nova)

add_executable(exampleapp main.cpp)
target_link_libraries(exampleapp PRIVATE
	Nova::Core      # Nova core (Header-only)
	Nova::Extras    # Nova Extras sinks/formatters
	Nova::Flare     # Flare emergency logging
)
```

### Option B: Nova Core Only (Header-Only)

```cmake
# add Nova with only core (header-only)
set(NOVA_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(NOVA_BUILD_FLARE OFF CACHE BOOL "" FORCE)
add_subdirectory(external/nova)

add_executable(exampleapp main.cpp)
target_link_libraries(exampleapp PRIVATE Nova::Core)
```

### Option C: Nova Core + Extras (No Flare)

```cmake
# add Nova with Extras but without Flare
set(NOVA_BUILD_EXTRAS ON CACHE BOOL "" FORCE)
set(NOVA_BUILD_FLARE OFF CACHE BOOL "" FORCE)
add_subdirectory(external/nova)

add_executable(exampleapp main.cpp)
target_link_libraries(exampleapp PRIVATE
	Nova::Core
	Nova::Extras
)
```

### Option D: Let User Choose via CMake GUI/ccmake

```cmake
# don't force options - let user configure
add_subdirectory(external/nova)

add_executable(exampleapp main.cpp)

# link based on what's available
target_link_libraries(exampleapp PRIVATE Nova::Core)

if(TARGET Nova::Extras)
	target_link_libraries(exampleapp PRIVATE Nova::Extras)
endif()

if(TARGET Nova::Flare)
	target_link_libraries(exampleapp PRIVATE Nova::Flare)
endif()
```

---

## Method 2: FetchContent (Recommended for Easy Setup)

Download Nova automatically during configure:

### Fetch Everything

```cmake
cmake_minimum_required(VERSION 3.16)
project(ExampleApp)

include(FetchContent)

FetchContent_Declare(
	nova
	GIT_REPOSITORY https://github.com/kmac-13/nova.git
	GIT_TAG v1.0.0
)

# Optional: configure before making available
set(NOVA_BUILD_EXTRAS ON CACHE BOOL "" FORCE)
set(NOVA_BUILD_FLARE ON CACHE BOOL "" FORCE)
set(NOVA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NOVA_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nova)

add_executable(exampleapp main.cpp)
target_link_libraries(exampleapp PRIVATE
	Nova::Core
	Nova::Extras
	Nova::Flare
)
```

### Fetch Core Only

```cmake
FetchContent_Declare(
	nova
	GIT_REPOSITORY https://github.com/kmac-13/nova.git
	GIT_TAG v1.0.0
)

set(NOVA_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(NOVA_BUILD_FLARE OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nova)

add_executable(exampleapp main.cpp)
target_link_libraries(exampleapp PRIVATE Nova::Core)
```

---

## Method 3: find_package (After Installation)

First, install Nova:

```bash
cd /path/to/nova
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make
sudo make install
```

Then use in your project:

### Find Everything

```cmake
cmake_minimum_required(VERSION 3.16)
project(ExampleApp)

find_package(Nova 1.0 REQUIRED COMPONENTS Core Extras Flare)

add_executable(exampleapp main.cpp)
target_link_libraries(exampleapp PRIVATE
	Nova::Core
	Nova::Extras
	Nova::Flare
)
```

### Find Core Only

```cmake
find_package(Nova 1.0 REQUIRED COMPONENTS Core)

add_executable(exampleapp main.cpp)
target_link_libraries(exampleapp PRIVATE Nova::Core)
```

### Find Core + Extras (Optional Flare)

```cmake
find_package(Nova 1.0 REQUIRED COMPONENTS Core Extras)
find_package(Nova 1.0 COMPONENTS Flare)  # Optional

add_executable(exampleapp main.cpp)
target_link_libraries(exampleapp PRIVATE Nova::Core Nova::Extras)

if(Nova_Flare_FOUND)
	target_link_libraries(exampleapp PRIVATE Nova::Flare)
endif()
```

---

## Build Options Reference

| Option | Default | Description |
|--------|---------|-------------|
| `NOVA_BUILD_EXTRAS` | `ON` | Build Nova Extras library |
| `NOVA_BUILD_FLARE` | `ON` | Build Flare emergency logging |
| `NOVA_BUILD_EXAMPLES` | `OFF` | Build example programs |
| `NOVA_BUILD_TESTS` | `OFF` | Build unit tests |

### Command Line

```bash
# build everything
cmake .. -DNOVA_BUILD_EXTRAS=ON -DNOVA_BUILD_FLARE=ON

# build core only
cmake .. -DNOVA_BUILD_EXTRAS=OFF -DNOVA_BUILD_FLARE=OFF

# build with examples and tests
cmake .. -DNOVA_BUILD_EXAMPLES=ON -DNOVA_BUILD_TESTS=ON
```

---

## Complete Example Project Structure

```
example_project/
├── CMakeLists.txt
├── external/
│   └── nova/          # Nova as git submodule or subdirectory
└── src/
    └── main.cpp
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(ExampleProject VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# add Nova
set(NOVA_BUILD_EXTRAS ON CACHE BOOL "" FORCE)
set(NOVA_BUILD_FLARE ON CACHE BOOL "" FORCE)
set(NOVA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NOVA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/nova)

# build your application
add_executable(exampleapp src/main.cpp)
target_link_libraries(exampleapp PRIVATE
	Nova::Core
	Nova::Extras
	Nova::Flare
)

# optional: Enable warnings
if(MSVC)
	target_compile_options(exampleapp PRIVATE /W4)
else()
	target_compile_options(exampleapp PRIVATE -Wall -Wextra -Wpedantic)
endif()
```

### src/main.cpp

```cpp
#include <kmac/nova.h>
#include <kmac/nova/extras/ostream_sink.h>
#include <kmac/nova/extras/severities.h>

using namespace kmac::nova::extras;

int main() {
    // configure logging
    OStreamSink console(std::cout);
    kmac::nova::ScopedConfigurator config;
    config.bind<InfoTag>(&console);
    config.bind<ErrorTag>(&console);
    
    // log messages
    NOVA_LOG_INFO() << "Application started";
    NOVA_LOG_ERROR() << "Example error";
    
    return 0;
}
```

### Build

```bash
mkdir build && cd build
cmake ..
make
./exampleapp
```

---

## Git Submodule Setup

To add Nova as a git submodule:

```bash
cd /path/to/your/project
git submodule add https://github.com/kmac-13/nova.git external/nova
git submodule update --init --recursive
```

Then use `add_subdirectory(external/nova)` in your CMakeLists.txt.

---

## Troubleshooting

### Issue: "Target Nova::Extras not found"

**Cause:** Nova Extras was not built

**Solution:** Enable it explicitly:
```cmake
set(NOVA_BUILD_EXTRAS ON CACHE BOOL "" FORCE)
add_subdirectory(external/nova)
```

### Issue: "C++ standard version not set"

**Cause:** No C++ standard has been set for the target.  Nova requires C++11 as a minimum; C++17 is recommended for `if constexpr` language guarantees on disabled logging domains (i.e. tags) and for `std::to_chars` availability.

**Solution:**
```cmake
set(CMAKE_CXX_STANDARD 17)  # or 11/14 if targeting older compilers
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

### Issue: Header files not found

**Cause:** Include directories not set correctly

**Solution:** Use target_link_libraries instead of target_include_directories:
```cmake
target_link_libraries(exampleapp PRIVATE Nova::Core)  # Automatically adds include dirs
```

---

## Best Practices

1. **Use PRIVATE linking** for your executables:
   ```cmake
   target_link_libraries(exampleapp PRIVATE Nova::Core)
   ```

2. **Use PUBLIC linking** if your library exposes Nova types:
   ```cmake
   target_link_libraries(examplelib PUBLIC Nova::Core)
   ```

3. **Always specify components** with find_package:
   ```cmake
   find_package(Nova REQUIRED COMPONENTS Core Extras)
   ```

4. **Force options before add_subdirectory** to ensure they take effect:
   ```cmake
   set(NOVA_BUILD_EXTRAS ON CACHE BOOL "" FORCE)
   ```

5. **Check target existence** before linking optional components:
   ```cmake
   if(TARGET Nova::Flare)
       target_link_libraries(exampleapp PRIVATE Nova::Flare)
   endif()
   ```
