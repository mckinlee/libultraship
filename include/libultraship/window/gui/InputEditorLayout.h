#pragma once

#include <string>
#include <vector>

#include "ship/controller/controldevice/controller/mapping/ControllerAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/ControllerButtonMapping.h"

namespace LUS {

/** Color used for a button or stick chip in the input editor. */
enum class InputEditorChipColor {
    Gray,
    Blue,
    Green,
    Yellow,
    Red,
    Purple,
};

/** One remappable button row. */
struct InputEditorButtonRow {
    CONTROLLERBUTTONS_T bitmask = 0;
    std::string label;
    InputEditorChipColor color = InputEditorChipColor::Gray;
};

/** A collapsible group of button rows. */
struct InputEditorButtonGroup {
    std::string label;
    std::vector<InputEditorButtonRow> buttons;
    bool defaultOpen = true;
};

/** One remappable analog stick section. */
struct InputEditorStickRow {
    std::string label;
    Ship::StickIndex index = Ship::LEFT_STICK;
    InputEditorChipColor color = InputEditorChipColor::Gray;
    bool defaultOpen = true;
};

/** Optional device sections shown below the main controls. */
struct InputEditorCapabilities {
    bool rumble = false;
    bool gyro = false;
    bool led = false;
};

/** Port-owned presentation for the shared input editor. */
struct InputEditorLayout {
    std::vector<InputEditorButtonGroup> buttonGroups;
    std::vector<InputEditorStickRow> sticks;
    InputEditorCapabilities capabilities;
};

} // namespace LUS
