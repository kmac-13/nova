cmake_minimum_required( VERSION 3.21 )

project( KMac_NovaExtras )

file( GLOB headers "./include/kmac/nova/extras/*.h" )
file( GLOB sources "./src/kmac/nova/extras/*.cpp" )
add_library( KMac_NovaExtras OBJECT ${headers} ${sources} )

target_include_directories( KMac_NovaExtras PRIVATE ./include )
target_include_directories( KMac_NovaExtras PRIVATE ../nova/include )
