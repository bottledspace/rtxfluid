cmake_minimum_required (VERSION 3.5.1)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")
project (rtxfluid)
add_library(flip SHARED flip.c database.c)

add_custom_command(OUTPUT raygen.spv
	COMMAND C:\\VulkanSDK\\1.3.261.1\\Bin\\glslc.exe --target-env=vulkan1.3 --target-spv=spv1.6 -o raygen.spv raygen.rgen
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	DEPENDS raygen.rgen
	COMMENT "Compiling raygen")
# VIM turns out to be an easy way to get xxd on Windows, a small unix util which
# we can use to pack binary data into a C header.
add_custom_command(OUTPUT raygen.h
	COMMAND C:\\vim\\vim90\\xxd -i raygen.spv >raygen.h
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	DEPENDS raygen.spv
	COMMENT "Generating raygen header")

# Only copy+pasting the commands verbatim (as opposed to a loop or function)
# seems to work with VS :-(

add_custom_command(OUTPUT intersection.spv
	COMMAND C:\\VulkanSDK\\1.3.261.1\\Bin\\glslc.exe --target-env=vulkan1.3 --target-spv=spv1.6 -o intersection.spv intersection.rint
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	DEPENDS intersection.rint
	COMMENT "Compiling intersection")
add_custom_command(OUTPUT intersection.h
	COMMAND C:\\vim\\vim90\\xxd -i intersection.spv >intersection.h
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	DEPENDS intersection.spv
	COMMENT "Generating intersection header")

add_custom_command(OUTPUT miss.spv
	COMMAND C:\\VulkanSDK\\1.3.261.1\\Bin\\glslc.exe --target-env=vulkan1.3 --target-spv=spv1.6 -o miss.spv miss.rmiss
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	DEPENDS miss.rmiss
	COMMENT "Compiling miss")
add_custom_command(OUTPUT miss.h
	COMMAND C:\\vim\\vim90\\xxd -i miss.spv >miss.h
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	DEPENDS miss.spv
	COMMENT "Generating miss header")

add_custom_command(OUTPUT closesthit.spv
	COMMAND C:\\VulkanSDK\\1.3.261.1\\Bin\\glslc.exe --target-env=vulkan1.3 --target-spv=spv1.6 -o closesthit.spv closesthit.rchit
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	DEPENDS closesthit.rchit
	COMMENT "Compiling closesthit")
add_custom_command(OUTPUT closesthit.h
	COMMAND C:\\vim\\vim90\\xxd -i closesthit.spv >closesthit.h
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	DEPENDS closesthit.spv
	COMMENT "Generating closesthit header")

add_executable(rtxfluid rtxfluid.c plugin.c database.c renderer_vk.c raygen.h intersection.h miss.h closesthit.h)
include_directories(rtxfluid C:\\VulkanSDK\\1.3.261.1\\Include)
target_link_libraries(rtxfluid C:\\VulkanSDK\\1.3.261.1\\Lib\\vulkan-1.lib)
