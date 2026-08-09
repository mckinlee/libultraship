#include <gtest/gtest.h>

#include "ship/core/Context.h"
#include "ship/window/gui/Gui.h"
#include "ship/window/gui/GuiMenuBar.h"
#include "ship/window/gui/GuiWindow.h"

#include <imgui.h>

namespace Ship {
namespace {

class ContextRecordingMenu final : public GuiWindow {
  public:
    ContextRecordingMenu() : GuiWindow("gTestMenu", false, "Test Menu") {
    }

  protected:
    void OnInit(const nlohmann::json& initArgs) override {
        Component::OnInit(initArgs);
    }

    void DrawElement() override {
    }

    void UpdateElement() override {
    }
};

class ContextRecordingMenuBar final : public GuiMenuBar {
  public:
    ContextRecordingMenuBar() : GuiMenuBar("gTestMenuBar", false) {
    }

  protected:
    void OnInit(const nlohmann::json& initArgs) override {
        Component::OnInit(initArgs);
    }

    void DrawElement() override {
    }

    void UpdateElement() override {
    }
};

TEST(GuiLifecycleTest, ShutdownWithoutContextIsSafe) {
    ASSERT_EQ(ImGui::GetCurrentContext(), nullptr);
    Gui gui;

    gui.ShutDownImGui(nullptr);

    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
}

TEST(GuiLifecycleTest, MenuAndMenuBarInheritGuiContextBeforeInitialization) {
    auto context = Context::CreateInstance("Gui context test", "gui-context-test");
    auto gui = std::make_shared<Gui>();
    context->GetChildren().Add(gui);
    auto menu = std::make_shared<ContextRecordingMenu>();
    auto menuBar = std::make_shared<ContextRecordingMenuBar>();

    gui->SetMenu(menu);
    gui->SetMenuBar(menuBar);

    EXPECT_EQ(menu->GetContext(), context);
    EXPECT_EQ(menuBar->GetContext(), context);
    EXPECT_TRUE(menu->IsInitialized());
    EXPECT_TRUE(menuBar->IsInitialized());
}

} // namespace
} // namespace Ship
