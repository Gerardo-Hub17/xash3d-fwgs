# Compilacion de comprobacion del cliente de CS 1.6 para Wii (devkitPPC),
# como libreria ESTATICA. Se usa desde .github/workflows/wii-compile-check.yml.
# Objetivo de esta etapa: ver los errores de PPC (endianness, SSE, etc.).
cmake_minimum_required(VERSION 3.10)
project(cs16-client-wii C CXX)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

file(GLOB CS_CLIENT_SRC "${ROOT}/cl_dll/*.cpp")
file(GLOB CS_EV_SRC "${ROOT}/cl_dll/events/*.cpp")
file(GLOB CS_WPN_SRC "${ROOT}/cl_dll/cs_wpn/*.cpp")
file(GLOB CS_WPNSH_SRC "${ROOT}/dlls/wpn_shared/*.cpp")
file(GLOB CS_HUD_SRC "${ROOT}/cl_dll/hud/*.cpp")
file(GLOB CS_STUDIO_SRC "${ROOT}/cl_dll/studio/*.cpp")
file(GLOB CS_INPUT_SRC "${ROOT}/cl_dll/input/*.cpp")
file(GLOB CS_PM_SRC "${ROOT}/cs16-src/pm_shared/*.cpp")
file(GLOB CS_GAMSH_SRC "${ROOT}/game_shared/*.cpp")
file(GLOB MINIUTL_SRC "${ROOT}/3rdparty/mainui/miniutl/*.cpp")
file(GLOB CS_PARTICLEMAN_SRC "${ROOT}/cl_dll/particleman/*.cpp")

list(REMOVE_ITEM CS_CLIENT_SRC "${ROOT}/cl_dll/ev_hldm.cpp" "${ROOT}/cl_dll/inputw32.cpp")
list(REMOVE_ITEM CS_INPUT_SRC "${ROOT}/cl_dll/input/input_sdl.cpp")
list(REMOVE_ITEM CS_WPN_SRC "${ROOT}/cl_dll/cs_wpn/cs_baseentity.cpp")
list(APPEND CS_CLIENT_SRC ${ROOT}/cs16-src/public/utflib.cpp
	${ROOT}/cs16-src/public/strlcpy.c ${ROOT}/cs16-src/public/strlcat.c)
list(APPEND CS_CLIENT_SRC ${CS_EV_SRC} ${CS_WPN_SRC} ${CS_WPNSH_SRC} ${CS_HUD_SRC}
	${CS_STUDIO_SRC} ${CS_PM_SRC} ${CS_INPUT_SRC} ${CS_GAMSH_SRC} ${MINIUTL_SRC}
	${CS_PARTICLEMAN_SRC})

add_library(cs_client STATIC ${CS_CLIENT_SRC})

target_include_directories(cs_client PRIVATE
	${ROOT}/cl_dll/include ${ROOT}/cl_dll/include/hud ${ROOT}/cl_dll/include/studio
	${ROOT}/cl_dll/include/math ${ROOT}/cl_dll ${ROOT}/cs16-src/common ${ROOT}/engine
	${ROOT}/cs16-src/pm_shared ${ROOT}/dlls ${ROOT}/game_shared ${ROOT}/cs16-src/public
	${ROOT}/public/cl_dll ${ROOT}/3rdparty/mainui
	${ROOT}/3rdparty/mainui/controls ${ROOT}/3rdparty/mainui/menus
	${ROOT}/3rdparty/mainui/miniutl ${ROOT}/cl_dll/particleman
	${ROOT}/cs16-src/public/dlls)

target_compile_definitions(cs_client PRIVATE
	CLIENT_WEAPONS CLIENT_DLL XASH_OGC STDINT_H=<cstdint>
	LINUX _LINUX
	stricmp=strcasecmp strnicmp=strncasecmp _strnicmp=strncasecmp
	_snprintf=snprintf _vsnprintf=vsnprintf
	uint32=uint32_t
	_inline=inline __inline=inline
	XASH_OGC XASH_WII GEKKO HW_RVL
	__BYTE_ORDER__=__ORDER_BIG_ENDIAN__)

target_compile_options(cs_client PRIVATE
	-fpermissive -fno-exceptions -fno-rtti -fms-extensions -Wno-write-strings
	-ffunction-sections -fno-unwind-tables -fno-asynchronous-unwind-tables)
