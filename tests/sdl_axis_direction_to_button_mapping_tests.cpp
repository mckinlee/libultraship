#include <gtest/gtest.h>
#include <imgui.h>

#include <algorithm>
#include <vector>

#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldevice/controller/mapping/sdl/SDLAxisDirectionToButtonMapping.h"
#include "ship/window/Window.h"

namespace Ship {
namespace {

class TestWindow final : public Window {
  public:
    TestWindow() {
        MarkInitialized();
    }

    void Close() override {
    }
    void RunGuiOnly() override {
    }
    void StartFrame() override {
    }
    void EndFrame() override {
    }
    bool IsFrameReady() override {
        return true;
    }
    void HandleEvents() override {
    }
    void SetCursorVisibility(bool) override {
    }
    uint32_t GetWidth() override {
        return 1;
    }
    uint32_t GetHeight() override {
        return 1;
    }
    float GetAspectRatio() override {
        return 1.0f;
    }
    int32_t GetPosX() override {
        return 0;
    }
    int32_t GetPosY() override {
        return 0;
    }
    void SetMousePos(Coords) override {
    }
    Coords GetMousePos() override {
        return {};
    }
    Coords GetMouseDelta() override {
        return {};
    }
    CoordsF GetMouseWheel() override {
        return {};
    }
    bool GetMouseState(MouseBtn) override {
        return false;
    }
    void SetMouseCapture(bool) override {
    }
    bool IsMouseCaptured() override {
        return false;
    }
    uint32_t GetCurrentRefreshRate() override {
        return 60;
    }
    bool SupportsWindowedFullscreen() override {
        return false;
    }
    bool CanDisableVerticalSync() override {
        return false;
    }
    void SetResolutionMultiplier(float) override {
    }
    void SetMsaaLevel(uint32_t) override {
    }
    void SetFullscreen(bool) override {
    }
    bool IsFullscreen() override {
        return false;
    }
    bool IsRunning() override {
        return true;
    }
    const char* GetKeyName(int32_t) override {
        return "";
    }
    uintptr_t GetGfxFrameBuffer() override {
        return 0;
    }
    void SetCurrentDimensions(uint32_t, uint32_t) override {
    }
    void SetCurrentDimensions(uint32_t, uint32_t, int32_t, int32_t) override {
    }
    void SetCurrentDimensions(bool, uint32_t, uint32_t) override {
    }
    void SetCurrentDimensions(bool, uint32_t, uint32_t, int32_t, int32_t) override {
    }
    WindowRect GetPrimaryMonitorRect() override {
        return {};
    }
};

class TestControlDeck final : public ControlDeck {
  public:
    explicit TestControlDeck(std::shared_ptr<Window> window)
        : ControlDeck({}, nullptr, { { 1, "Trigger" } }, std::move(window), std::make_shared<ConsoleVariable>()) {
    }

    void WriteToPad(void*) override {
    }
};

class SDLAxisDirectionToButtonMappingTest : public testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
        mPreviousImGuiContext = ImGui::GetCurrentContext();
        mImGuiContext = ImGui::CreateContext();
        ASSERT_NE(mImGuiContext, nullptr);

        mInstanceId = AttachVirtualGamepad(0x0002);
        ASSERT_NE(mInstanceId, 0u) << SDL_GetError();

        mWindow = std::make_shared<TestWindow>();
        mControlDeck = std::make_shared<TestControlDeck>(mWindow);
        mControlDeck->GetConnectedPhysicalDeviceManager()->RefreshConnectedSDLGamepads();
        IgnoreUnexpectedGamepads();
        mJoystick = SDL_GetJoystickFromID(mInstanceId);
        ASSERT_NE(mJoystick, nullptr) << SDL_GetError();
    }

    SDL_JoystickID AttachVirtualGamepad(Uint16 productId) {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.vendor_id = 0x1209;
        desc.product_id = productId;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.axis_mask = (1 << SDL_GAMEPAD_AXIS_COUNT) - 1;
        desc.name = "LUS virtual axis gamepad";

        const auto instanceId = SDL_AttachVirtualJoystick(&desc);
        if (instanceId != 0) {
            mInstanceIds.push_back(instanceId);
        }
        return instanceId;
    }

    void TearDown() override {
        mControlDeck.reset();
        mWindow.reset();
        if (ImGui::GetCurrentContext() == mImGuiContext) {
            ImGui::DestroyContext(mImGuiContext);
        }
        mImGuiContext = nullptr;
        ImGui::SetCurrentContext(mPreviousImGuiContext);
        for (const auto instanceId : mInstanceIds) {
            EXPECT_TRUE(SDL_DetachVirtualJoystick(instanceId)) << SDL_GetError();
        }
        mInstanceIds.clear();
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }

    void SetAxisValue(SDL_Joystick* joystick, SDL_GamepadAxis axis, Sint16 value) {
        ASSERT_TRUE(SDL_SetJoystickVirtualAxis(joystick, axis, value)) << SDL_GetError();
        SDL_UpdateJoysticks();
    }

    void SetAxisValue(Sint16 value) {
        SetAxisValue(mJoystick, SDL_GAMEPAD_AXIS_LEFTX, value);
    }

    void IgnoreUnexpectedGamepads() {
        auto manager = mControlDeck->GetConnectedPhysicalDeviceManager();
        for (const auto& [instanceId, name] : manager->GetConnectedSDLGamepadNames()) {
            (void)name;
            if (std::find(mInstanceIds.begin(), mInstanceIds.end(), instanceId) == mInstanceIds.end()) {
                manager->IgnoreInstanceIdForPort(0, instanceId);
            }
        }
    }

    SDL_JoystickID mInstanceId = 0;
    std::vector<SDL_JoystickID> mInstanceIds;
    SDL_Joystick* mJoystick = nullptr;
    ImGuiContext* mPreviousImGuiContext = nullptr;
    ImGuiContext* mImGuiContext = nullptr;
    std::shared_ptr<TestWindow> mWindow;
    std::shared_ptr<TestControlDeck> mControlDeck;
};

TEST_F(SDLAxisDirectionToButtonMappingTest, ReportsFullPositiveAxisTravel) {
    SDLAxisDirectionToButtonMapping mapping(0, 1, SDL_GAMEPAD_AXIS_LEFTX, POSITIVE, mControlDeck,
                                            std::make_shared<ConsoleVariable>());

    SetAxisValue(SDL_JOYSTICK_AXIS_MIN);
    EXPECT_EQ(mapping.GetAnalogValue(), 0);

    SetAxisValue(0);
    EXPECT_EQ(mapping.GetAnalogValue(), 0);

    SetAxisValue(SDL_JOYSTICK_AXIS_MAX / 2);
    EXPECT_NEAR(mapping.GetAnalogValue(), 128, 1);

    SetAxisValue(SDL_JOYSTICK_AXIS_MAX);
    EXPECT_EQ(mapping.GetAnalogValue(), 255);
}

TEST_F(SDLAxisDirectionToButtonMappingTest, ReportsFullNegativeAxisTravel) {
    SDLAxisDirectionToButtonMapping mapping(0, 1, SDL_GAMEPAD_AXIS_LEFTX, NEGATIVE, mControlDeck,
                                            std::make_shared<ConsoleVariable>());

    SetAxisValue(SDL_JOYSTICK_AXIS_MAX);
    EXPECT_EQ(mapping.GetAnalogValue(), 0);

    SetAxisValue(SDL_JOYSTICK_AXIS_MIN);
    EXPECT_EQ(mapping.GetAnalogValue(), 255);
}

TEST_F(SDLAxisDirectionToButtonMappingTest, UsesTheStrongestConnectedDevice) {
    SDLAxisDirectionToButtonMapping mapping(0, 1, SDL_GAMEPAD_AXIS_LEFTX, POSITIVE, mControlDeck,
                                            std::make_shared<ConsoleVariable>());
    SetAxisValue(SDL_JOYSTICK_AXIS_MAX / 2);

    const auto secondInstanceId = AttachVirtualGamepad(0x0003);
    ASSERT_NE(secondInstanceId, 0u) << SDL_GetError();
    mControlDeck->GetConnectedPhysicalDeviceManager()->RefreshConnectedSDLGamepads();
    IgnoreUnexpectedGamepads();
    auto* secondJoystick = SDL_GetJoystickFromID(secondInstanceId);
    ASSERT_NE(secondJoystick, nullptr) << SDL_GetError();
    SetAxisValue(secondJoystick, SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MAX);

    EXPECT_EQ(mapping.GetAnalogValue(), 255);
}

TEST_F(SDLAxisDirectionToButtonMappingTest, ReportsTravelForBothTriggerAxes) {
    SDLAxisDirectionToButtonMapping leftTrigger(0, 1, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, POSITIVE, mControlDeck,
                                                std::make_shared<ConsoleVariable>());
    SDLAxisDirectionToButtonMapping rightTrigger(0, 1, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, POSITIVE, mControlDeck,
                                                 std::make_shared<ConsoleVariable>());

    SetAxisValue(mJoystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    SetAxisValue(mJoystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);

    EXPECT_EQ(leftTrigger.GetAnalogValue(), 0);
    EXPECT_EQ(rightTrigger.GetAnalogValue(), 0);

    SetAxisValue(mJoystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);
    SetAxisValue(mJoystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);

    EXPECT_NEAR(leftTrigger.GetAnalogValue(), 128, 1);
    EXPECT_EQ(rightTrigger.GetAnalogValue(), 255);
}

TEST_F(SDLAxisDirectionToButtonMappingTest, IgnoresBlockedOrIgnoredDevices) {
    SDLAxisDirectionToButtonMapping mapping(0, 1, SDL_GAMEPAD_AXIS_LEFTX, POSITIVE, mControlDeck,
                                            std::make_shared<ConsoleVariable>());
    SetAxisValue(SDL_JOYSTICK_AXIS_MAX);

    mControlDeck->GetConnectedPhysicalDeviceManager()->IgnoreInstanceIdForPort(0, mInstanceId);
    EXPECT_EQ(mapping.GetAnalogValue(), 0);

    mControlDeck->GetConnectedPhysicalDeviceManager()->UnignoreInstanceIdForPort(0, mInstanceId);
    mControlDeck->BlockGameInput(1);
    EXPECT_EQ(mapping.GetAnalogValue(), 0);
}

} // namespace
} // namespace Ship
