#include <gtest/gtest.h>

#include "libultraship/window/gui/InputEditorWindow.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldevice/controller/ControllerButton.h"
#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardKeyToButtonMapping.h"

namespace LUS {
namespace {

class TestControlDeck final : public Ship::ControlDeck {
  public:
    explicit TestControlDeck(
        std::unordered_map<CONTROLLERBUTTONS_T, std::string> buttonNames,
        std::vector<CONTROLLERBUTTONS_T> additionalBitmasks = {},
        std::shared_ptr<Ship::ConsoleVariable> consoleVariable = std::make_shared<Ship::ConsoleVariable>())
        : ControlDeck(std::move(additionalBitmasks), nullptr, std::move(buttonNames), nullptr,
                      std::move(consoleVariable)) {
    }

    void WriteToPad(void*) override {
    }
};

InputEditorLayout MakeLayout() {
    return {
        .buttonGroups = {
            {
                .label = "Buttons",
                .buttons = {
                    { .bitmask = 1, .label = "Confirm", .color = InputEditorChipColor::Green },
                    { .bitmask = 2, .label = "Up", .color = InputEditorChipColor::Gray },
                },
            },
        },
        .sticks = {
            { .label = "Control Stick", .index = Ship::LEFT_STICK, .color = InputEditorChipColor::Purple },
        },
        .capabilities = { .rumble = true },
    };
}

TEST(InputEditorLayoutTest, ControlDeckUsesCallerProvidedButtonNames) {
    auto controlDeck = std::make_shared<TestControlDeck>(
        std::unordered_map<CONTROLLERBUTTONS_T, std::string>{ { 1, "A" }, { 2, "DUp" } });

    EXPECT_EQ(controlDeck->GetButtonNameForBitmask(1), "A");
    EXPECT_EQ(controlDeck->GetButtonNameForBitmask(2), "DUp");
    EXPECT_EQ(controlDeck->GetButtonNameForBitmask(4), "4");
}

TEST(InputEditorLayoutTest, ExistingConstructorRemainsSourceCompatible) {
    EXPECT_NO_THROW({ InputEditorWindow inputEditor("InputEditor", "Input Editor", nullptr, nullptr); });
}

TEST(InputEditorLayoutTest, CustomLayoutAcceptsLabelsThatDifferFromButtonNames) {
    auto controlDeck = std::make_shared<TestControlDeck>(
        std::unordered_map<CONTROLLERBUTTONS_T, std::string>{ { 1, "A" }, { 2, "DUp" } });

    EXPECT_NO_THROW(
        { InputEditorWindow inputEditor("InputEditor", "Input Editor", controlDeck, nullptr, MakeLayout()); });
}

TEST(InputEditorLayoutTest, AdditionalBitmasksArePartOfTheLayoutRegistry) {
    auto controlDeck = std::make_shared<TestControlDeck>(
        std::unordered_map<CONTROLLERBUTTONS_T, std::string>{ { 1, "A" }, { 2, "DUp" } },
        std::vector<CONTROLLERBUTTONS_T>{ 4 });

    EXPECT_EQ(controlDeck->GetButtonNameForBitmask(4), "4");

    auto completeLayout = MakeLayout();
    completeLayout.buttonGroups.front().buttons.push_back({ .bitmask = 4, .label = "Extra" });
    EXPECT_NO_THROW(
        { InputEditorWindow inputEditor("InputEditor", "Input Editor", controlDeck, nullptr, completeLayout); });

    EXPECT_THROW(InputEditorWindow("InputEditor", "Input Editor", controlDeck, nullptr, MakeLayout()),
                 std::invalid_argument);
}

TEST(InputEditorLayoutTest, ButtonNamesDoNotRenameExistingConfigKeys) {
    auto consoleVariable = std::make_shared<Ship::ConsoleVariable>();
    auto controlDeck =
        std::make_shared<TestControlDeck>(std::unordered_map<CONTROLLERBUTTONS_T, std::string>{ { 1, "A" } },
                                          std::vector<CONTROLLERBUTTONS_T>{}, consoleVariable);
    Ship::ControllerButton button(0, 1, consoleVariable, controlDeck);
    auto mapping = std::make_shared<Ship::KeyboardKeyToButtonMapping>(0, 1, Ship::LUS_KB_A, controlDeck, nullptr,
                                                                      consoleVariable);
    button.AddButtonMapping(mapping);

    constexpr auto numericKey = CVAR_PREFIX_CONTROLLERS ".Port1.Buttons.1ButtonMappingIds";
    constexpr auto namedKey = CVAR_PREFIX_CONTROLLERS ".Port1.Buttons.AButtonMappingIds";
    consoleVariable->SetString(numericKey, "numeric");
    consoleVariable->SetString(namedKey, "named");

    button.SaveButtonMappingIdsToConfig();

    EXPECT_EQ(consoleVariable->GetString(numericKey, ""), mapping->GetButtonMappingId() + ",");
    EXPECT_STREQ(consoleVariable->GetString(namedKey, ""), "named");
}

TEST(InputEditorLayoutTest, CustomLayoutRequiresEveryRegisteredButtonExactlyOnce) {
    auto controlDeck = std::make_shared<TestControlDeck>(
        std::unordered_map<CONTROLLERBUTTONS_T, std::string>{ { 1, "A" }, { 2, "DUp" } });

    auto missingButton = MakeLayout();
    missingButton.buttonGroups.front().buttons.pop_back();
    EXPECT_THROW(InputEditorWindow("InputEditor", "Input Editor", controlDeck, nullptr, missingButton),
                 std::invalid_argument);

    auto duplicateButton = MakeLayout();
    duplicateButton.buttonGroups.front().buttons.back().bitmask = 1;
    EXPECT_THROW(InputEditorWindow("InputEditor", "Input Editor", controlDeck, nullptr, duplicateButton),
                 std::invalid_argument);

    auto unknownButton = MakeLayout();
    unknownButton.buttonGroups.front().buttons.back().bitmask = 4;
    EXPECT_THROW(InputEditorWindow("InputEditor", "Input Editor", controlDeck, nullptr, unknownButton),
                 std::invalid_argument);

    auto duplicateSection = MakeLayout();
    duplicateSection.buttonGroups.front().label = "Rumble";
    EXPECT_THROW(InputEditorWindow("InputEditor", "Input Editor", controlDeck, nullptr, duplicateSection),
                 std::invalid_argument);
}

} // namespace
} // namespace LUS
