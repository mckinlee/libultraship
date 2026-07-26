#=================== ImGui ===================
target_sources(ImGui
	PRIVATE
	${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
)

find_package(SDL2 CONFIG REQUIRED)
if(NOT TARGET "${LUS_SDL2_TARGET}")
    message(FATAL_ERROR
        "LUS_SDL2_TARGET does not name an available target: ${LUS_SDL2_TARGET}")
endif()
target_link_libraries(ImGui PUBLIC "${LUS_SDL2_TARGET}" SDL2::SDL2main)

find_package(GLEW REQUIRED)
if(NOT TARGET "${LUS_GLEW_TARGET}")
    message(FATAL_ERROR
        "LUS_GLEW_TARGET does not name an available target: ${LUS_GLEW_TARGET}")
endif()
target_link_libraries(ImGui PUBLIC opengl32 "${LUS_GLEW_TARGET}")
