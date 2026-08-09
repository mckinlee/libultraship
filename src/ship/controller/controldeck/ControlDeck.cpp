#include "ship/controller/controldeck/ControlDeck.h"

#include "ship/core/Context.h"
#include "ship/window/gui/Gui.h"
#include "ship/controller/controldevice/controller/Controller.h"
#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include <imgui.h>
#include <stdexcept>
#include <unordered_set>
#include "ship/controller/controldevice/controller/mapping/mouse/WheelHandler.h"

namespace Ship {

ControlDeck::ControlDeck(std::vector<CONTROLLERBUTTONS_T> additionalBitmasks,
                         std::shared_ptr<ControllerDefaultMappings> controllerDefaultMappings,
                         std::unordered_map<CONTROLLERBUTTONS_T, std::string> buttonNames,
                         std::shared_ptr<Window> window, std::shared_ptr<ConsoleVariable> consoleVariable,
                         InputEditorSchema inputEditorSchema)
    : Component("ControlDeck"), mButtonNames(std::move(buttonNames)), mInputEditorSchema(std::move(inputEditorSchema)),
      mWindow(std::move(window)), mConsoleVariables(std::move(consoleVariable)) {
    mConnectedPhysicalDeviceManager = std::make_shared<ConnectedPhysicalDeviceManager>();
    mGlobalSDLDeviceSettings = std::make_shared<GlobalSDLDeviceSettings>(mConsoleVariables);
    mControllerDefaultMappings = controllerDefaultMappings == nullptr ? std::make_shared<ControllerDefaultMappings>()
                                                                      : controllerDefaultMappings;
}

ControlDeck::~ControlDeck() {
    SPDLOG_TRACE("destruct control deck");
}

void ControlDeck::Init(uint8_t* controllerBits) {
    mControllerBits = controllerBits;
    *mControllerBits |= 1 << 0;

    mWheelHandler = std::make_shared<WheelHandler>(GetWindow());

    auto self = std::dynamic_pointer_cast<ControlDeck>(GetSharedComponent());
    for (auto port : mPorts) {
        if (port->GetConnectedController() != nullptr) {
            port->GetConnectedController()->SetControlDeck(self);
        }
    }

    for (auto port : mPorts) {
        if (port->GetConnectedController()->HasConfig()) {
            port->GetConnectedController()->ReloadAllMappingsFromConfig();
        }
    }

    // if we don't have a config for controller 1, set default bindings
    if (!mPorts[0]->GetConnectedController()->HasConfig()) {
        mPorts[0]->GetConnectedController()->AddDefaultMappings(PhysicalDeviceType::Keyboard);
        mPorts[0]->GetConnectedController()->AddDefaultMappings(PhysicalDeviceType::Mouse);
        mPorts[0]->GetConnectedController()->AddDefaultMappings(PhysicalDeviceType::SDLGamepad);
    }

    MarkInitialized();
}

bool ControlDeck::ProcessKeyboardEvent(KbEventType eventType, KbScancode scancode) {
    bool result = false;
    for (auto port : mPorts) {
        auto controller = port->GetConnectedController();

        if (controller != nullptr) {
            result = controller->ProcessKeyboardEvent(eventType, scancode) || result;
        }
    }

    return result;
}

bool ControlDeck::ProcessMouseButtonEvent(bool isPressed, MouseBtn button) {
    bool result = false;
    for (auto port : mPorts) {
        auto controller = port->GetConnectedController();

        if (controller != nullptr) {
            result = controller->ProcessMouseButtonEvent(isPressed, button) || result;
        }
    }

    return result;
}

bool ControlDeck::AllGameInputBlocked() {
    return !mGameInputBlockers.empty();
}

bool ControlDeck::GamepadGameInputBlocked() {
    // block controller input when using the controller to navigate imgui menus
    return AllGameInputBlocked() || GetWindow()->GetGui()->GetMenuOrMenubarVisible() &&
                                        GetConsoleVariables()->GetInteger(CVAR_IMGUI_CONTROLLER_NAV, 0);
}

bool ControlDeck::KeyboardGameInputBlocked() {
    // block keyboard input when typing in imgui
    ImGuiWindow* activeIDWindow = ImGui::GetCurrentContext()->ActiveIdWindow;
    return AllGameInputBlocked() ||
           (activeIDWindow != NULL && activeIDWindow->ID != GetWindow()->GetGui()->GetMainGameWindowID()) ||
           ImGui::GetTopMostPopupModal() != NULL; // ImGui::GetIO().WantCaptureKeyboard, but ActiveId check altered
}

bool ControlDeck::MouseGameInputBlocked() {
    // block mouse input when user interacting with gui
    ImGuiWindow* window = ImGui::GetCurrentContext()->HoveredWindow;
    if (window == NULL) {
        return true;
    }
    return AllGameInputBlocked() || (window->ID != GetWindow()->GetGui()->GetMainGameWindowID());
}

std::shared_ptr<Controller> ControlDeck::GetControllerByPort(uint8_t port) {
    if (port >= mPorts.size()) {
        return nullptr;
    }
    return mPorts[port]->GetConnectedController();
}

size_t ControlDeck::GetPortCount() const {
    return mPorts.size();
}

void ControlDeck::BlockGameInput(int32_t blockId) {
    mGameInputBlockers[blockId] = true;
}

void ControlDeck::UnblockGameInput(int32_t blockId) {
    mGameInputBlockers.erase(blockId);
}

std::shared_ptr<ConnectedPhysicalDeviceManager> ControlDeck::GetConnectedPhysicalDeviceManager() {
    return mConnectedPhysicalDeviceManager;
}

std::shared_ptr<GlobalSDLDeviceSettings> ControlDeck::GetGlobalSDLDeviceSettings() {
    return mGlobalSDLDeviceSettings;
}

std::shared_ptr<ControllerDefaultMappings> ControlDeck::GetControllerDefaultMappings() {
    return mControllerDefaultMappings;
}

std::shared_ptr<WheelHandler> ControlDeck::GetWheelHandler() const {
    if (!mWheelHandler) {
        throw std::runtime_error("ControlDeck requires WheelHandler to be initialized");
    }
    return mWheelHandler;
}

const std::unordered_map<CONTROLLERBUTTONS_T, std::string>& ControlDeck::GetAllButtonNames() const {
    return mButtonNames;
}

const InputEditorSchema& ControlDeck::GetInputEditorSchema() const {
    return mInputEditorSchema;
}

InputEditorSchemaValidation ControlDeck::ValidateInputEditorSchema() const {
    const auto fail = [](std::string error) { return InputEditorSchemaValidation{ false, std::move(error) }; };
    const auto blank = [](const std::string& value) { return value.find_first_not_of(" \t\r\n") == std::string::npos; };

    if (mPorts.empty()) {
        return fail("The controller deck has no ports.");
    }
    if (mInputEditorSchema.buttonGroups.empty()) {
        return fail("The input schema has no button groups.");
    }

    std::unordered_set<std::string> groupLabels;
    std::unordered_set<std::string> rowLabels;
    std::unordered_set<CONTROLLERBUTTONS_T> bitmasks;
    for (const auto& group : mInputEditorSchema.buttonGroups) {
        if (blank(group.label) || !groupLabels.insert(group.label).second) {
            return fail("Button group labels must be nonempty and unique.");
        }
        if (group.buttons.empty()) {
            return fail("Every button group must contain at least one button.");
        }
        for (const auto& row : group.buttons) {
            if (blank(row.label) || !rowLabels.insert(row.label).second) {
                return fail("Button labels must be nonempty and unique.");
            }
            if (row.bitmask == 0 || (row.bitmask & (row.bitmask - 1)) != 0 || !bitmasks.insert(row.bitmask).second) {
                return fail("Button masks must be unique nonzero single bits.");
            }
            const auto known = mButtonNames.find(row.bitmask);
            if (known == mButtonNames.end() || known->second != row.label) {
                return fail("The input schema must exactly match the ControlDeck button map.");
            }
        }
    }
    if (bitmasks.size() != mButtonNames.size()) {
        return fail("The input schema must cover every ControlDeck button.");
    }

    std::unordered_set<std::string> stickLabels;
    std::unordered_set<uint8_t> stickIndices;
    for (const auto& stick : mInputEditorSchema.sticks) {
        if (blank(stick.label) || !stickLabels.insert(stick.label).second) {
            return fail("Stick labels must be nonempty and unique.");
        }
        if (stick.index > static_cast<uint8_t>(RIGHT_STICK) || !stickIndices.insert(stick.index).second) {
            return fail("Stick indices must be unique supported indices.");
        }
    }
    return { true, {} };
}

std::string ControlDeck::GetButtonNameForBitmask(CONTROLLERBUTTONS_T bitmask) {
    // if we don't have a name for this bitmask,
    // return the stringified bitmask
    if (!mButtonNames.contains(bitmask)) {
        return std::to_string(bitmask);
    }

    return mButtonNames[bitmask];
}

std::shared_ptr<Window> ControlDeck::GetWindow() const {
    if (!mWindow) {
        throw std::runtime_error("ControlDeck requires Window dependency");
    }
    if (!mWindow->IsInitialized()) {
        throw std::runtime_error("ControlDeck requires Window to be initialized");
    }
    return mWindow;
}

std::shared_ptr<ConsoleVariable> ControlDeck::GetConsoleVariables() const {
    if (!mConsoleVariables) {
        throw std::runtime_error("ControlDeck requires ConsoleVariable dependency");
    }
    if (!mConsoleVariables->IsInitialized()) {
        throw std::runtime_error("ControlDeck requires ConsoleVariable to be initialized");
    }
    return mConsoleVariables;
}
} // namespace Ship
