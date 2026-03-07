# Helper functions for applying sanitizers to CMake targets

# Function to enable Address Sanitizer
function(enable_asan target)
    target_compile_options(${target} PRIVATE -fsanitize=address)
    target_link_options(${target} PRIVATE -fsanitize=address)
endfunction()

# Function to enable Memory Sanitizer
function(enable_msan target)
    target_compile_options(${target} PRIVATE -fsanitize=memory)
    target_link_options(${target} PRIVATE -fsanitize=memory)
endfunction()

# Function to enable Thread Sanitizer
function(enable_tsan target)
    target_compile_options(${target} PRIVATE -fsanitize=thread)
    target_link_options(${target} PRIVATE -fsanitize=thread)
endfunction()

# Function to enable Undefined Behavior Sanitizer
function(enable_ubsan target)
    target_compile_options(${target} PRIVATE -fsanitize=undefined)
    target_link_options(${target} PRIVATE -fsanitize=undefined)
endfunction()

# Function to enable Leak Sanitizer
function(enable_lsan target)
    target_compile_options(${target} PRIVATE -fsanitize=leak)
    target_link_options(${target} PRIVATE -fsanitize=leak)
endfunction()

# Function to enable Data Flow Sanitizer
function(enable_dfsan target)
    target_compile_options(${target} PRIVATE -fsanitize=dataflow)
    target_link_options(${target} PRIVATE -fsanitize=dataflow)
endfunction()

# Function to enable LibFuzzer
function(enable_libfuzzer target)
    target_link_libraries(${target} PRIVATE -fsanitize=fuzzer)
endfunction()