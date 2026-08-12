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

class ContextOwningGui final : public Gui {
  public:
    void OwnContext(ImGuiContext* context) {
        mImGuiContext = context;
        ImGui::SetCurrentContext(context);
        mImGuiIo = &ImGui::GetIO();
    }

    int rendererShutdowns = 0;
    int windowManagerShutdowns = 0;

  protected:
    void ImGuiBackendShutdown() override {
        ++rendererShutdowns;
    }

    void ImGuiWMShutdown() override {
        ++windowManagerShutdowns;
    }
};

TEST(GuiLifecycleTest, ShutdownWithoutContextIsSafe) {
    ASSERT_EQ(ImGui::GetCurrentContext(), nullptr);
    Gui gui;

    gui.ShutDownImGui(nullptr);

    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
}

TEST(GuiLifecycleTest, ShutdownDestroysOnlyTheOwnedContext) {
    ContextOwningGui first;
    ContextOwningGui second;
    first.OwnContext(ImGui::CreateContext());
    ImGuiContext* secondContext = ImGui::CreateContext();
    second.OwnContext(secondContext);

    first.ShutDownImGui(nullptr);

    EXPECT_EQ(ImGui::GetCurrentContext(), secondContext);
    EXPECT_EQ(first.rendererShutdowns, 1);
    EXPECT_EQ(first.windowManagerShutdowns, 1);
    EXPECT_EQ(second.rendererShutdowns, 0);
    EXPECT_EQ(second.windowManagerShutdowns, 0);

    second.ShutDownImGui(nullptr);
    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
}

TEST(GuiLifecycleTest, UninitializedGuiDoesNotDestroyAnotherContext) {
    Gui uninitialized;
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);

    uninitialized.ShutDownImGui(nullptr);

    EXPECT_EQ(ImGui::GetCurrentContext(), context);
    ImGui::DestroyContext(context);
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
