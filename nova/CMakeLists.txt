cmake_minimum_required( VERSION 3.21 )

project( KMac_Nova )

file( GLOB headers "./include/kmac/nova/*.h" )
file( GLOB sources "./src/kmac/nova/*.cpp" )
add_library( KMac_Nova OBJECT ${headers} ${sources} )

target_include_directories( KMac_Nova PRIVATE ./include )
