#include "libultraship/controller/controldeck/ControlDeck.h"

#include "ship/core/Context.h"
#include "libultraship/controller/controldevice/controller/Controller.h"
#include "libultraship/controller/controldevice/controller/mapping/ControllerDefaultMappings.h"
#include "ship/utils/StringHelper.h"
#include "ship/controller/controldevice/controller/mapping/mouse/WheelHandler.h"
#include "ship/window/gui/IconsFontAwesome4.h"
#include <imgui.h>

#include <algorithm>

namespace LUS {
namespace {

Ship::InputEditorSchema DefaultInputEditorSchema() {
    return {
        {
            { "Buttons",
              {
                  { BTN_A, "A", Ship::InputEditorButtonColor::Blue },
                  { BTN_B, "B", Ship::InputEditorButtonColor::Green },
                  { BTN_START, "Start", Ship::InputEditorButtonColor::Red },
                  { BTN_L, "L" },
                  { BTN_R, "R" },
                  { BTN_Z, "Z" },
                  { BTN_CUP, StringHelper::Sprintf("C %s", ICON_FA_ARROW_UP),
                    Ship::InputEditorButtonColor::Yellow },
                  { BTN_CDOWN, StringHelper::Sprintf("C %s", ICON_FA_ARROW_DOWN),
                    Ship::InputEditorButtonColor::Yellow },
                  { BTN_CLEFT, StringHelper::Sprintf("C %s", ICON_FA_ARROW_LEFT),
                    Ship::InputEditorButtonColor::Yellow },
                  { BTN_CRIGHT, StringHelper::Sprintf("C %s", ICON_FA_ARROW_RIGHT),
                    Ship::InputEditorButtonColor::Yellow },
              } },
            { "D-Pad",
              {
                  { BTN_DUP, ICON_FA_ARROW_UP },
                  { BTN_DDOWN, ICON_FA_ARROW_DOWN },
                  { BTN_DLEFT, ICON_FA_ARROW_LEFT },
                  { BTN_DRIGHT, ICON_FA_ARROW_RIGHT },
              } },
        },
        {
            { "Analog Stick", Ship::LEFT_STICK, true },
            { "Additional (\"Right\") Stick", Ship::RIGHT_STICK, false },
        },
        { true, true, true },
    };
}

Ship::InputEditorSchema InputEditorSchemaFromButtonNames(
    const std::unordered_map<CONTROLLERBUTTONS_T, std::string>& buttonNames) {
    std::vector<Ship::InputEditorButtonRow> rows;
    rows.reserve(buttonNames.size());
    for (const auto& [bitmask, label] : buttonNames) {
        rows.push_back({ bitmask, label });
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& left, const auto& right) { return left.bitmask < right.bitmask; });
    return {
        { { "Buttons", std::move(rows), true } },
        {
            { "Analog Stick", Ship::LEFT_STICK, true },
            { "Additional (\"Right\") Stick", Ship::RIGHT_STICK, false },
        },
        { true, true, true },
    };
}

} // namespace

ControlDeck::ControlDeck(std::vector<CONTROLLERBUTTONS_T> additionalBitmasks,
                         std::shared_ptr<Ship::ControllerDefaultMappings> controllerDefaultMappings,
                         std::unordered_map<CONTROLLERBUTTONS_T, std::string> buttonNames,
                         std::shared_ptr<Ship::Window> window, std::shared_ptr<Ship::ConsoleVariable> consoleVariable)
    : ControlDeck(std::move(additionalBitmasks), std::move(controllerDefaultMappings), buttonNames,
                  InputEditorSchemaFromButtonNames(buttonNames), std::move(window), std::move(consoleVariable)) {
}

ControlDeck::ControlDeck(std::vector<CONTROLLERBUTTONS_T> additionalBitmasks,
                         std::shared_ptr<Ship::ControllerDefaultMappings> controllerDefaultMappings,
                         std::unordered_map<CONTROLLERBUTTONS_T, std::string> buttonNames,
                         Ship::InputEditorSchema inputEditorSchema, std::shared_ptr<Ship::Window> window,
                         std::shared_ptr<Ship::ConsoleVariable> consoleVariable)
    : Ship::ControlDeck(additionalBitmasks, controllerDefaultMappings, buttonNames, window, consoleVariable,
                        std::move(inputEditorSchema)),
      mPads(nullptr) {
    std::vector<CONTROLLERBUTTONS_T> bitmasks;
    for (auto [bitmask, name] : buttonNames) {
        bitmasks.push_back(bitmask);
    }
    bitmasks.insert(bitmasks.end(), additionalBitmasks.begin(), additionalBitmasks.end());
    for (int32_t i = 0; i < MAXCONTROLLERS; i++) {
        mPorts.push_back(std::make_shared<Ship::ControlPort>(
            i, std::make_shared<Controller>(i, bitmasks, consoleVariable, nullptr, window)));
    }
}

ControlDeck::ControlDeck(std::vector<CONTROLLERBUTTONS_T> additionalBitmasks, std::shared_ptr<Ship::Window> window,
                         std::shared_ptr<Ship::ConsoleVariable> consoleVariable)
    : ControlDeck(additionalBitmasks, std::make_shared<LUS::ControllerDefaultMappings>(),
                  std::unordered_map<CONTROLLERBUTTONS_T, std::string>({
                      { BTN_A, "A" },
                      { BTN_B, "B" },
                      { BTN_L, "L" },
                      { BTN_R, "R" },
                      { BTN_Z, "Z" },
                      { BTN_START, "Start" },
                      { BTN_CLEFT, "CLeft" },
                      { BTN_CRIGHT, "CRight" },
                      { BTN_CUP, "CUp" },
                      { BTN_CDOWN, "CDown" },
                      { BTN_DLEFT, "DLeft" },
                      { BTN_DRIGHT, "DRight" },
                      { BTN_DUP, "DUp" },
                      { BTN_DDOWN, "DDown" },
                  }),
                  DefaultInputEditorSchema(),
                  std::move(window), std::move(consoleVariable)) {
}

ControlDeck::ControlDeck(std::shared_ptr<Ship::Window> window, std::shared_ptr<Ship::ConsoleVariable> consoleVariable)
    : ControlDeck(std::vector<CONTROLLERBUTTONS_T>(), std::move(window), std::move(consoleVariable)) {
}

OSContPad* ControlDeck::GetPads() {
    return mPads;
}

void ControlDeck::WriteToPad(void* pad) {
    WriteToOSContPad((OSContPad*)pad);
}

void ControlDeck::WriteToOSContPad(OSContPad* pad) {
    SDL_PumpEvents();
    GetWheelHandler()->Update();

    if (AllGameInputBlocked()) {
        return;
    }

    mPads = pad;

    for (size_t i = 0; i < mPorts.size(); i++) {
        const std::shared_ptr<Ship::Controller> controller = mPorts[i]->GetConnectedController();

        if (controller != nullptr) {
            controller->ReadToPad(&pad[i]);
        }
    }
}
} // namespace LUS
