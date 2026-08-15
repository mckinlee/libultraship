#include <gtest/gtest.h>

#include <imgui.h>

#include "ship/window/gui/Gui.h"

namespace Ship {
namespace {

TEST(GuiShutdown, RepeatedShutdownIsSafe) {
    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    ASSERT_NE(context, nullptr);
    ImGui::SetCurrentContext(context);

    Gui gui;
    gui.ShutDownImGui(nullptr);
    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
    EXPECT_NO_THROW(gui.ShutDownImGui(nullptr));

    ImGui::SetCurrentContext(previousContext);
}

} // namespace
} // namespace Ship
