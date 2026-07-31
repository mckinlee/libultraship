#pragma once

#include <cstdint>
#include <string>

#include <imgui.h>

struct SDL_Window;

namespace Ship {

class Mobile {
  public:
    static void ImGuiProcessEvent(SDL_Window* window, bool wantsTextInput);
};
}; // namespace Ship
