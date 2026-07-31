file(GLOB_RECURSE Source_Files__GX RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cpp")

source_group("gx" FILES ${Source_Files__GX})
target_sources(libultraship PRIVATE ${Source_Files__GX})
