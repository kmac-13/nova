# Building Nova Examples with CMake

## Quick Start

```bash
# From the examples directory
mkdir build
cd build

# Build default example (01_basic_usage)
cmake ..
cmake --build .

# Run it
./01_basic_usage
```

## Switching Examples

### Method 1: Via CMake Command Line
```bash
# In your build directory
cmake -DEXAMPLE=02_multiple_sinks ..
cmake --build .
./02_multiple_sinks

# Or in one command
cmake -DEXAMPLE=03_custom_clock .. && cmake --build . && ./03_custom_clock
```

### Method 2: Using CMake GUI
```bash
cmake-gui ..
# Select EXAMPLE from dropdown
# Click Configure
# Click Generate
cmake --build .
```

### Method 3: Using ccmake (Terminal UI)
```bash
ccmake ..
# Press 't' for advanced options
# Navigate to EXAMPLE and press Enter
# Select desired example
# Press 'c' to configure
# Press 'g' to generate
cmake --build .
```

## Available Examples

| Example | Description |
|---------|-------------|
| `01_basic_usage` | Core concepts - tags, sinks, logging |
| `02_multiple_sinks` | Routing to multiple destinations |
| `03_custom_clock` | Custom timestamp sources |
| `04_multithreading` | Thread-safe logging |
| `05_filtering` | Runtime message filtering |
| `06_hierarchical_tags` | Subsystem + Severity organization |

## Setting Nova Include Directory

If Nova is not in the default location (`../../include` from examples/):

### Method 1: Environment Variable
```bash
export NOVA_INCLUDE_DIR=/path/to/nova/include
cmake ..
```

### Method 2: CMake Command Line
```bash
cmake -DNOVA_INCLUDE_DIR=/path/to/nova/include ..
```

### Method 3: Edit CMakeLists.txt
Edit the default in `CMakeLists.txt`:
```cmake
set(NOVA_INCLUDE_DIR "/your/custom/path/to/nova/include")
```

## Build Types

### Release (optimized)
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

### Debug (with symbols)
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
```

### With verbose output
```bash
cmake --build . --verbose
```

## Building All Examples

To build all examples, you can use a simple script:

```bash
#!/bin/bash
# build_all.sh

EXAMPLES=(
    "01_basic_usage"
    "02_multiple_sinks"
    "03_custom_clock"
    "04_multithreading"
    "05_filtering"
    "06_hierarchical_tags"
)

mkdir -p build
cd build

for example in "${EXAMPLES[@]}"; do
    echo "Building $example..."
    cmake -DEXAMPLE=$example ..
    cmake --build .
    echo ""
done

echo "All examples built!"
ls -lh 0*
```

## Platform-Specific Notes

### Windows (Visual Studio)
```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release

# Run
.\Release\01_basic_usage.exe
```

### macOS
```bash
mkdir build
cd build
cmake ..
cmake --build .
./01_basic_usage
```

### Linux
```bash
mkdir build
cd build
cmake ..
cmake --build . -j$(nproc)  # Parallel build
./01_basic_usage
```

## Troubleshooting

### "Nova headers not found"
Solution: Set `NOVA_INCLUDE_DIR` to point to your Nova installation
```bash
cmake -DNOVA_INCLUDE_DIR=/path/to/nova/include ..
```

### Threading errors (Example 04)
On some systems, you may need to explicitly link pthread:
```bash
cmake -DCMAKE_THREAD_LIBS_INIT="-lpthread" ..
```

### Compiler warnings as errors
If you want to disable `-Werror`:
Edit `CMakeLists.txt` and remove the `-Werror` flag.

## IDE Integration

### Visual Studio Code
1. Install CMake Tools extension
2. Open examples directory
3. Select kit (compiler)
4. Click "Build" button
5. Change EXAMPLE in CMake settings
6. Build again

### CLion
1. Open examples directory as project
2. CLion auto-detects CMakeLists.txt
3. Edit CMake options to change EXAMPLE
4. Build and run from IDE

### Visual Studio
```bash
# Generate Visual Studio solution
cmake -G "Visual Studio 16 2019" ..

# Open NovaExamples.sln in Visual Studio
```

## Custom Build Configuration

You can combine multiple options:

```bash
cmake \
    -DEXAMPLE=05_filtering \
    -DNOVA_INCLUDE_DIR=/custom/path \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=clang++ \
    ..

cmake --build .
```

## Clean Build

```bash
# Remove build directory
cd ..
rm -rf build

# Start fresh
mkdir build
cd build
cmake ..
cmake --build .
```

## Running Examples

After building:

```bash
# Basic run
./01_basic_usage

# With output redirection
./02_multiple_sinks > output.log

# Check exit code
./01_basic_usage && echo "Success!" || echo "Failed!"
```

## Integration with Your Project

To use this CMakeLists.txt as a template for your project:

1. Copy `CMakeLists.txt` to your project
2. Modify `EXAMPLE` to point to your main.cpp
3. Add your source files:
   ```cmake
   add_executable(my_app
       src/main.cpp
       src/other_file.cpp
   )
   ```
4. Build as normal

## Example: Complete Workflow

```bash
# 1. Clone or extract Nova
cd ~/projects
unzip nova.zip

# 2. Go to examples
cd nova/examples

# 3. Build first example
mkdir build && cd build
cmake ..
cmake --build .
./01_basic_usage

# 4. Try another example
cmake -DEXAMPLE=04_multithreading ..
cmake --build .
./04_multithreading

# 5. Try filtering example
cmake -DEXAMPLE=05_filtering ..
cmake --build .
./05_filtering

# Done!
```
