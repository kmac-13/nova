cmake_minimum_required( VERSION 3.21 )

project( KMac_NovaFlare )

file( GLOB headers "./include/kmac/*.h" "./include/kmac/flare/*.h" )
file( GLOB sources "./src/kmac/flare/*.cpp" )

# include the ARM bare-metal fault handler assembly stub when targeting ARM;
# on hosted builds CMake will simply not invoke the assembler for it
if( CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM" )
	enable_language( ASM )
	file( GLOB asm_sources "./src/kmac/flare/*.s" )
endif()

add_library( KMac_NovaFlare OBJECT ${headers} ${sources} ${asm_sources} )

target_include_directories( KMac_NovaFlare PRIVATE ./include )
target_include_directories( KMac_NovaFlare PRIVATE ../nova/include )
