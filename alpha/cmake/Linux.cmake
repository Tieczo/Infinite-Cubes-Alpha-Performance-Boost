# The native Linux build (64-bit GCC or Clang). platform/linux holds the Win32 subset the game uses on
# top of POSIX (windows.h, winsock2.h, ...), ahead of DXVK-native's own windows.h, which declares the
# basic types and COM. See platform/linux/README.md.
#
# DXVK_NATIVE_DIR: the DXVK-native install prefix (include/dxvk, lib/libdxvk_d3d9.so).
set(DXVK_NATIVE_DIR "" CACHE PATH "DXVK-native install prefix (include/dxvk, lib)")
find_package(PkgConfig REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# The Direct3D 9 declarations (both programs use its types).
find_path(DXVK_INCLUDE_DIR d3d9.h PATHS "${DXVK_NATIVE_DIR}/include/dxvk" NO_DEFAULT_PATH)
find_path(DXVK_INCLUDE_DIR d3d9.h PATH_SUFFIXES dxvk)
if(NOT DXVK_INCLUDE_DIR)
 message(FATAL_ERROR "DXVK-native not found: set DXVK_NATIVE_DIR to its install prefix")
endif()
add_library(dxvk_headers INTERFACE IMPORTED)
set_target_properties(dxvk_headers PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${DXVK_INCLUDE_DIR}")

# FreeImage's own Linux makefile builds with these.
target_compile_definitions(FreeImage PRIVATE __ANSI__ NO_LCMS)
# The MinGW build hides the JPEG-XR SDK's guiddef.h; Linux has none of its own.
target_include_directories(FreeImage PRIVATE "${FI}/Source/LibJXR/common/include")
target_compile_options(FreeImage PRIVATE -fPIC
 $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration -Wno-incompatible-pointer-types -Wno-int-conversion>)

target_sources(Server PRIVATE platform/linux/Thread.cpp)
foreach(app Cube Server)
 # platform/linux comes first: its windows.h extends DXVK's.
 target_include_directories(${app} BEFORE PRIVATE platform/linux)
 target_include_directories(${app} PRIVATE include plasma/include platform)
 target_compile_options(${app} PRIVATE -fwrapv -fno-strict-aliasing -Wno-multichar)
 # MSVC keywords in declarations that do not include windows.h.
 target_compile_definitions(${app} PRIVATE "__stdcall=" "__int64=long long")
 target_link_libraries(${app} PRIVATE pugixml sqlite zlibstatic dxvk_headers Threads::Threads ${CMAKE_DL_LIBS})
 # The game's files (data*.db, Save/, ...) are next to the executable, its libraries in lib/.
 set_target_properties(${app} PROPERTIES INSTALL_RPATH "\$ORIGIN/lib" BUILD_WITH_INSTALL_RPATH ON)
endforeach()

# The client: Direct3D 9 from DXVK-native's library, the window and input from SDL3, XAudio2 from
# FAudio (FAUDIO_DIR: its install prefix; empty: the system's FAudio through pkg-config) and the
# polygon tessellator from Mesa's libGLU.
set(FAUDIO_DIR "" CACHE PATH "FAudio install prefix (empty: the system's FAudio)")
find_library(DXVK_D3D9_LIBRARY dxvk_d3d9 PATHS "${DXVK_NATIVE_DIR}/lib" NO_DEFAULT_PATH)
find_library(DXVK_D3D9_LIBRARY dxvk_d3d9)
if(NOT DXVK_D3D9_LIBRARY)
 message(FATAL_ERROR "libdxvk_d3d9.so not found: set DXVK_NATIVE_DIR to the DXVK-native install prefix")
endif()
add_library(dxvk_d3d9 SHARED IMPORTED)
set_target_properties(dxvk_d3d9 PROPERTIES IMPORTED_LOCATION "${DXVK_D3D9_LIBRARY}")
if(FAUDIO_DIR)
 find_path(FAUDIO_INCLUDE_DIR FAudio.h PATHS "${FAUDIO_DIR}/include" NO_DEFAULT_PATH REQUIRED)
 find_library(FAUDIO_LIBRARY FAudio PATHS "${FAUDIO_DIR}/lib" "${FAUDIO_DIR}/lib64" NO_DEFAULT_PATH REQUIRED)
 add_library(faudio UNKNOWN IMPORTED)
 set_target_properties(faudio PROPERTIES IMPORTED_LOCATION "${FAUDIO_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${FAUDIO_INCLUDE_DIR}")
else()
 pkg_check_modules(FAUDIO_PC REQUIRED IMPORTED_TARGET FAudio)
 add_library(faudio INTERFACE IMPORTED)
 target_link_libraries(faudio INTERFACE PkgConfig::FAUDIO_PC)
endif()
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(GLU REQUIRED IMPORTED_TARGET glu)
target_sources(Cube PRIVATE platform/linux/main.cpp platform/linux/Thread.cpp)
target_link_libraries(Cube PRIVATE dxvk_d3d9 faudio PkgConfig::SDL3 PkgConfig::GLU)
