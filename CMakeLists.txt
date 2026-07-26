cmake_minimum_required(VERSION 3.24.0)

# Enable /Z7 (embedded debug info) instead of /Zi (shared PDB) on MSVC so that
# sccache can cache object files without racing over a shared .pdb file.
# CMP0141 was introduced in CMake 3.25, so guard it for older versions.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

option(NON_PORTABLE "Build a non-portable version" OFF)
option(DISABLE_DLL_LOADER "Disable dynamic library loading support" OFF)
option(ENABLE_SCRIPTING "Enable TCC-based C mod compilation and scripting system" OFF)
set(LUS_SDL2_TARGET "SDL2::SDL2" CACHE STRING
    "SDL2 CMake target used by libultraship")
set(LUS_GLEW_TARGET "GLEW::GLEW" CACHE STRING
    "GLEW CMake target used by libultraship")
set(LUS_SPDLOG_TARGET "spdlog::spdlog" CACHE STRING
    "spdlog CMake target used by libultraship")
set(LUS_LIBZIP_TARGET "libzip::zip" CACHE STRING
    "libzip CMake target used by libultraship")

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    option(SIGN_LIBRARY "Enable xcode signing" OFF)
    option(BUNDLE_ID "Bundle ID for xcode signing" "com.example.libultraship")
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
endif()

project(libultraship LANGUAGES C CXX)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)
    set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS} -fobjc-arc")
    set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	# Default to embedded debug info (/Z7) so sccache can cache object files
	# without multiple CL.EXE processes racing over a shared .pdb file.
	if(POLICY CMP0141)
		set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:Embedded>")
	endif()
endif()

include(cmake/Utils.cmake)
set(ADDITIONAL_LIB_INCLUDES "")

# ========= Configuration Options =========
option(INCLUDE_MPQ_SUPPORT "Enable StormLib and MPQ archive support" OFF)
option(GBI_UCODE "Specify the GBI ucode version" F3DEX_GBI_2)

# =========== Dependencies =============
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(cmake/dependencies/windows-vcpkg.cmake)
endif()

include(cmake/dependencies/common.cmake)
if(TARGET prism AND NOT LUS_SPDLOG_TARGET STREQUAL "spdlog::spdlog")
    get_target_property(_lus_prism_links prism LINK_LIBRARIES)
    list(REMOVE_ITEM _lus_prism_links spdlog::spdlog)
    set_target_properties(prism PROPERTIES LINK_LIBRARIES "${_lus_prism_links}")
    get_target_property(_lus_prism_interface prism INTERFACE_LINK_LIBRARIES)
    list(REMOVE_ITEM _lus_prism_interface spdlog::spdlog)
    set_target_properties(prism PROPERTIES
        INTERFACE_LINK_LIBRARIES "${_lus_prism_interface}")
    target_link_libraries(prism PUBLIC "${LUS_SPDLOG_TARGET}")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
	include(cmake/dependencies/android.cmake)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    include(cmake/dependencies/mac.cmake)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    include(cmake/dependencies/ios.cmake)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/dependencies/linux.cmake)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
    include(cmake/dependencies/openbsd.cmake)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(cmake/dependencies/windows.cmake)
endif()

# === Platform Specific Configuration ===
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    include(cmake/ios-toolchain-populate.cmake)
endif()

# =========== Sources =============
add_subdirectory("src")

# =========== Tests =============
option(LUS_BUILD_TESTS "Build unit tests" OFF)
if(LUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory("tests")
endif()
