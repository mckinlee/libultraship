#include "ship/controller/controldevice/controller/mapping/sdl/SDLAxisDirectionToButtonMapping.h"
#include <spdlog/spdlog.h>
#include "ship/utils/StringHelper.h"
#include "ship/window/gui/IconsFontAwesome4.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/Context.h"
#include "ship/controller/controldeck/ControlDeck.h"

#include <algorithm>

namespace Ship {
SDLAxisDirectionToButtonMapping::SDLAxisDirectionToButtonMapping(uint8_t portIndex, CONTROLLERBUTTONS_T bitmask,
                                                                 int32_t sdlControllerAxis, int32_t axisDirection)
    : ControllerInputMapping(PhysicalDeviceType::SDLGamepad),
      ControllerButtonMapping(PhysicalDeviceType::SDLGamepad, portIndex, bitmask),
      SDLAxisDirectionToAnyMapping(sdlControllerAxis, axisDirection) {
}

void SDLAxisDirectionToButtonMapping::UpdatePad(CONTROLLERBUTTONS_T& padButtons) {
    if (Context::GetRawInstance()->GetControlDeck()->GamepadGameInputBlocked()) {
        return;
    }

    int32_t axisThresholdPercentage = 25;
    if (AxisIsStick()) {
        axisThresholdPercentage = Ship::Context::GetRawInstance()
                                      ->GetControlDeck()
                                      ->GetGlobalSDLDeviceSettings()
                                      ->GetStickAxisThresholdPercentage();
    } else if (AxisIsTrigger()) {
        axisThresholdPercentage = Ship::Context::GetRawInstance()
                                      ->GetControlDeck()
                                      ->GetGlobalSDLDeviceSettings()
                                      ->GetTriggerAxisThresholdPercentage();
    }

    for (const auto& [instanceId, gamepad] : Context::GetRawInstance()
                                                 ->GetControlDeck()
                                                 ->GetConnectedPhysicalDeviceManager()
                                                 ->GetConnectedSDLGamepadsForPort(mPortIndex)) {
        const auto axisValue = SDL_GameControllerGetAxis(gamepad, mControllerAxis);

        auto axisMinValue = SDL_JOYSTICK_AXIS_MAX * (axisThresholdPercentage / 100.0f);
        if ((mAxisDirection == POSITIVE && axisValue > axisMinValue) ||
            (mAxisDirection == NEGATIVE && axisValue < -axisMinValue)) {
            padButtons |= mBitmask;
        }
    }
}

int8_t SDLAxisDirectionToButtonMapping::GetMappingType() {
    return MAPPING_TYPE_GAMEPAD;
}

std::string SDLAxisDirectionToButtonMapping::GetButtonMappingId() {
    return StringHelper::Sprintf("P%d-B%d-SDLA%d-AD%s", mPortIndex, mBitmask, mControllerAxis,
                                 mAxisDirection == 1 ? "P" : "N");
}

uint8_t SDLAxisDirectionToButtonMapping::GetAnalogValue() {
    if (Context::GetRawInstance()->GetControlDeck()->GamepadGameInputBlocked()) {
        return 0;
    }

    uint8_t strongestValue = 0;
    for (const auto& [instanceId, gamepad] : Context::GetRawInstance()
                                                   ->GetControlDeck()
                                                   ->GetConnectedPhysicalDeviceManager()
                                                   ->GetConnectedSDLGamepadsForPort(mPortIndex)) {
        (void)instanceId;
        const int32_t axisValue = SDL_GameControllerGetAxis(gamepad, mControllerAxis);
        const int32_t magnitude =
            mAxisDirection == POSITIVE ? std::max(axisValue, 0)
                                       : std::max(-axisValue, 0);
        const int32_t maximum =
            mAxisDirection == POSITIVE ? SDL_JOYSTICK_AXIS_MAX
                                       : -SDL_JOYSTICK_AXIS_MIN;
        const auto normalized = static_cast<uint8_t>(
            std::clamp((magnitude * 255 + maximum / 2) / maximum, 0, 255));
        strongestValue = std::max(strongestValue, normalized);
    }
    return strongestValue;
}

void SDLAxisDirectionToButtonMapping::SaveToConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".ButtonMappings." + GetButtonMappingId();
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetString(
        StringHelper::Sprintf("%s.ButtonMappingClass", mappingCvarKey.c_str()).c_str(),
        "SDLAxisDirectionToButtonMapping");
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.Bitmask", mappingCvarKey.c_str()).c_str(), mBitmask);
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.SDLControllerAxis", mappingCvarKey.c_str()).c_str(), mControllerAxis);
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.AxisDirection", mappingCvarKey.c_str()).c_str(), mAxisDirection);
    Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
}

void SDLAxisDirectionToButtonMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".ButtonMappings." + GetButtonMappingId();
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.ButtonMappingClass", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.Bitmask", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.SDLControllerAxis", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.AxisDirection", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
}

std::string SDLAxisDirectionToButtonMapping::GetPhysicalDeviceName() {
    return SDLAxisDirectionToAnyMapping::GetPhysicalDeviceName();
}

std::string SDLAxisDirectionToButtonMapping::GetPhysicalInputName() {
    return SDLAxisDirectionToAnyMapping::GetPhysicalInputName();
}
} // namespace Ship
