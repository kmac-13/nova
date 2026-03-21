# Nova/Flare Google Test Suite

This directory contains a comprehensive Google Test (gtest) based unit test suite for the Nova logging library, Nova extras, and Flare crash-resilient forensic logging library.

## Test Organization

### Core Nova Tests
- **test_nova_core.cpp** - Core Nova functionality (Record, Logger, LoggerTraits)
- **test_nova_scoped_configurator.cpp** - ScopedConfigurator
- **test_nova_record_builders.cpp** - TruncatingRecordBuilder and ContinuationRecordBuilder (extras)
- **test_nova_logger.cpp** - Logging macros (NOVA_LOG, NOVA_LOG_CONT)

### Nova Extras Tests
- **test_nova_sinks.cpp** - OStreamSink, NullSink, FilterSink
- **test_nova_formatters.cpp** - FormattingSink, FormattingSinkISO8601, custom formatters
- **test_nova_synchronization.cpp** - SynchronizedSink, SpinlockSink (thread-safety)
- **test_nova_composite.cpp** - CompositeSink, FixedCompositeSink

### Flare Tests
- **test_flare_emergency.cpp** - EmergencySink (crash-safe logging)
- **test_flare_reader.cpp** - Reader (parsing binary records)
- **test_flare_scanner.cpp** - Scanner (locating records in binary data)

### Integration Tests
- **test_integration.cpp** - End-to-end scenarios combining Nova and Flare

## Building and Running

### Prerequisites
- CMake 3.16 or later
- Google Test (gtest) library
- C++17 compiler
- pthread library

### Build Instructions

```bash
# From the gtest_tests directory
mkdir build
cd build

# Configure
cmake ..

# Build
cmake --build .

# Run all tests
ctest --output-on-failure

# Or run individual tests
./test_nova_core
./test_nova_record_builders
./test_integration
```

### Running Tests Individually

Each test executable can be run standalone:

```bash
# Run with verbose output
./test_nova_core --gtest_verbose

# Run specific test
./test_nova_core --gtest_filter=NovaCore.LoggerBinding

# List all tests
./test_nova_core --gtest_list_tests

# Run with color output
./test_nova_core --gtest_color=yes
```

### Using gtest_discover_tests

The CMakeLists.txt uses `gtest_discover_tests()` which automatically discovers and registers all tests. This means you can run tests through CTest:

```bash
# Run all tests
ctest

# Run specific test
ctest -R nova_core

# Run with verbose output
ctest -V

# Run tests in parallel
ctest -j4
```

## Installing Google Test

### Ubuntu/Debian
```bash
sudo apt-get install libgtest-dev
cd /usr/src/gtest
sudo cmake CMakeLists.txt
sudo make
sudo cp lib/*.a /usr/lib
```

### macOS (Homebrew)
```bash
brew install googletest
```

### From Source
```bash
git clone https://github.com/google/googletest.git
cd googletest
mkdir build && cd build
cmake ..
make
sudo make install
```

### As CMake FetchContent (Recommended)
Add to your CMakeLists.txt:
```cmake
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(googletest)
```

## Test Coverage

### Nova Core (test_nova_core.cpp)
- ✓ Record structure validation
- ✓ Logger traits configuration
- ✓ Logger binding/unbinding
- ✓ multiple tags with separate/shared sinks
- ✓ disabled tag behavior
- ✓ timestamp generation
- ✓ file/line/function metadata capture

### Scoped Configurator (test_nova_scoped_configurator.cpp)
- ✓ default and custom capacity
- ✓ single and multiple bindings
- ✓ automatic unbinding on destruction
- ✓ reverse order unbinding (LIFO)
- ✓ explicit unbinding
- ✓ bindFrom functionality
- ✓ duplicate binding handling
- ✓ null sink binding
- ✓ exact capacity usage
- ✓ capacity enforcement and overflow detection
- ✓ TryBind/TryBindFrom return values
- ✓ zero heap allocation verification
- ✓ empty configurator behavior
- ✓ unbind non-bound tag handling

### Record Builders (test_nova_record_builders.cpp)
- ✓ TruncatingRecordBuilder basic functionality
- ✓ TruncatingRecordBuilder with numbers and mixed types
- ✓ TruncatingRecordBuilder truncation behavior
- ✓ ContinuationRecordBuilder basic functionality
- ✓ ContinuationRecordBuilder overflow handling
- ✓ ContinuationRecordBuilder multiple overflows
- ✓ builder operation without bound sink
- ✓ metadata preservation

### Logger Macros (test_nova_logger.cpp)
- ✓ NOVA_LOG basic usage
- ✓ NOVA_LOG_CONT basic usage (requires continuation_logging.h)
- ✓ macro streaming with various types
- ✓ __FILE__, __FUNCTION__, __LINE__ capture
- ✓ multiple sequential macro calls
- ✓ macros with different tags
- ✓ macro usage in conditionals and loops

### Sinks (test_nova_sinks.cpp)
- ✓ OStreamSink output validation
- ✓ OStreamSink metadata inclusion
- ✓ NullSink silent discard
- ✓ FilterSink acceptance/rejection logic
- ✓ FilterSink complex filtering
- ✓ filter chaining

### Formatters (test_nova_formatters.cpp)
- ✓ FormattingSink basic formatting
- ✓ FormattingSink metadata inclusion
- ✓ FormattingSinkISO8601 timestamp formatting
- ✓ custom formatter functions
- ✓ various formatting styles
- ✓ DefaultFormatter

### Synchronization (test_nova_synchronization.cpp)
- ✓ SynchronizedSink single-threaded operation
- ✓ SynchronizedSink multi-threaded safety
- ✓ SpinlockSink single-threaded operation
- ✓ SpinlockSink multi-threaded safety
- ✓ race condition prevention
- ✓ stress testing
- ✓ message ordering preservation

### Composite Sinks (test_nova_composite.cpp)
- ✓ CompositeSink with zero/one/multiple children
- ✓ CompositeSink add/remove operations
- ✓ FixedCompositeSink with static sizing
- ✓ nested composite sinks
- ✓ sink reuse

### Flare Emergency Sink (test_flare_emergency.cpp)
- ✓ EmergencySink construction
- ✓ single and multiple record writes
- ✓ integration with Nova Logger
- ✓ large message handling
- ✓ flush behavior
- ✓ empty message handling
- ✓ process info capture

### Flare Reader (test_flare_reader.cpp)
- ✓ single record parsing
- ✓ multiple record parsing
- ✓ sequence number verification
- ✓ tag hash consistency
- ✓ empty file handling
- ✓ large message handling
- ✓ record status interpretation
- ✓ process info parsing

### Flare Scanner (test_flare_scanner.cpp)
- ✓ empty data handling
- ✓ magic number validation
- ✓ multiple record location
- ✓ invalid data rejection
- ✓ start offset management
- ✓ record location in large buffers
- ✓ consecutive scanning

### Integration (test_integration.cpp)
- ✓ basic logging pipeline
- ✓ multiple tags to composite sink
- ✓ filtered logging pipeline
- ✓ dual output (console + emergency file)
- ✓ thread-safe multi-tag logging
- ✓ formatted and emergency logging combined
- ✓ complex multi-sink architecture
- ✓ real-world crash scenario simulation

## Test Statistics

- **Total test files**: 12
- **Total test cases**: ~170+
- **Code coverage targets**:
  - Nova core: ~95%
  - Nova Extras: ~90%
  - Flare: ~90%

## Google Test Features Used

### Test Fixtures
All tests use `TEST_F` with test fixture classes:
```cpp
class NovaCore : public ::testing::Test {
protected:
    void SetUp() override { /* setup code */ }
    void TearDown() override { /* cleanup code */ }
};

TEST_F(NovaCore, LoggerBinding) {
    // Test code
}
```

### Assertions
- `EXPECT_EQ(a, b)` - Equality comparison
- `EXPECT_NE(a, b)` - Inequality comparison
- `EXPECT_TRUE(condition)` - Boolean true
- `EXPECT_FALSE(condition)` - Boolean false
- `EXPECT_STREQ(s1, s2)` - String equality
- `EXPECT_GT/GE/LT/LE` - Numeric comparisons
- `SUCCEED()` - Explicit success marker

### Death Tests (if needed)
```cpp
TEST(NovaCore, CrashTest) {
    ASSERT_DEATH(someFunction(), "error message");
}
```

## Troubleshooting

### Google Test Not Found
```bash
# Set GTEST_ROOT or install gtest
export GTEST_ROOT=/path/to/googletest
# Or install system-wide
sudo apt-get install libgtest-dev
```

### Test Failures
- run with `--gtest_verbose` flag for detailed output
- check that Nova/Flare source files compile correctly
- verify file system permissions for temporary directory tests

### Memory Issues
- all tests are designed to avoid memory leaks
- run with valgrind for memory analysis:
  ```bash
  valgrind --leak-check=full ./test_nova_core
  ```

### Thread Sanitizer
- verify thread safety with ThreadSanitizer:
  ```bash
  cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" ..
  make
  ./test_nova_synchronization
  ```

## Contributing

When adding new tests:
1. follow existing naming conventions
2. add test to CMakeLists.txt using `add_nova_gtest()`
3. update this README with test coverage information
4. ensure tests are isolated and deterministic
5. include both positive and negative test cases
6. use descriptive test names that explain what is being tested

## Performance

The test suite is designed for fast execution:
- core tests: < 1 second each
- synchronization tests: 1-3 seconds (multi-threaded)
- integration tests: 1-2 seconds
- total suite: < 30 seconds

## License

Tests follow the same license as the Nova/Flare libraries.
