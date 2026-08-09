#include <gtest/gtest.h>

#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldeck/ControlPort.h"

namespace Ship {
namespace {

InputEditorSchema ValidSchema() {
    return {
        {
            { "Face", { { 0x1, "A" }, { 0x2, "B" } }, true },
            { "Shoulders", { { 0x20, "Z" } }, false },
        },
        {
            { "Main Stick", LEFT_STICK, true },
            { "C-Stick", RIGHT_STICK, false },
        },
        { true, false, false },
    };
}

class SchemaControlDeck final : public ControlDeck {
  public:
    SchemaControlDeck(std::unordered_map<CONTROLLERBUTTONS_T, std::string> buttonNames, InputEditorSchema schema,
                      size_t portCount = 1)
        : ControlDeck({}, nullptr, std::move(buttonNames), nullptr, std::make_shared<ConsoleVariable>(),
                      std::move(schema)) {
        for (size_t i = 0; i < portCount; ++i) {
            mPorts.push_back(std::make_shared<ControlPort>(static_cast<uint8_t>(i)));
        }
    }

    void WriteToPad(void*) override {
    }
};

SchemaControlDeck MakeValidDeck() {
    return SchemaControlDeck({ { 0x1, "A" }, { 0x2, "B" }, { 0x20, "Z" } }, ValidSchema());
}

TEST(InputEditorSchemaTest, PreservesGameSpecificOrderingGroupsSticksAndCapabilities) {
    auto controlDeck = MakeValidDeck();
    ASSERT_TRUE(controlDeck.ValidateInputEditorSchema().valid);

    const auto& schema = controlDeck.GetInputEditorSchema();
    ASSERT_EQ(schema.buttonGroups.size(), 2U);
    EXPECT_EQ(schema.buttonGroups[0].label, "Face");
    EXPECT_EQ(schema.buttonGroups[0].buttons[0].label, "A");
    EXPECT_EQ(schema.buttonGroups[0].buttons[1].label, "B");
    EXPECT_EQ(schema.buttonGroups[1].label, "Shoulders");
    EXPECT_EQ(schema.buttonGroups[1].buttons[0].label, "Z");
    ASSERT_EQ(schema.sticks.size(), 2U);
    EXPECT_EQ(schema.sticks[0].label, "Main Stick");
    EXPECT_EQ(schema.sticks[1].label, "C-Stick");
    EXPECT_TRUE(schema.capabilities.rumble);
    EXPECT_FALSE(schema.capabilities.gyro);
    EXPECT_FALSE(schema.capabilities.led);
    EXPECT_EQ(controlDeck.GetPortCount(), 1U);
    EXPECT_EQ(controlDeck.GetControllerByPort(1), nullptr);
}

TEST(InputEditorSchemaTest, RejectsEveryMalformedSchemaClass) {
    const auto names =
        std::unordered_map<CONTROLLERBUTTONS_T, std::string>{ { 0x1, "A" }, { 0x2, "B" }, { 0x20, "Z" } };
    const auto invalid = [&](InputEditorSchema schema, size_t ports = 1) {
        return !SchemaControlDeck(names, std::move(schema), ports).ValidateInputEditorSchema().valid;
    };

    auto schema = ValidSchema();
    schema.buttonGroups.clear();
    EXPECT_TRUE(invalid(schema));
    EXPECT_TRUE(invalid(ValidSchema(), 0));

    schema = ValidSchema();
    schema.buttonGroups[0].label = " ";
    EXPECT_TRUE(invalid(schema));
    schema = ValidSchema();
    schema.buttonGroups[1].label = schema.buttonGroups[0].label;
    EXPECT_TRUE(invalid(schema));
    schema = ValidSchema();
    schema.buttonGroups[0].buttons.clear();
    EXPECT_TRUE(invalid(schema));
    schema = ValidSchema();
    schema.buttonGroups[0].buttons[0].label = "";
    EXPECT_TRUE(invalid(schema));
    schema = ValidSchema();
    schema.buttonGroups[0].buttons[1].label = schema.buttonGroups[0].buttons[0].label;
    EXPECT_TRUE(invalid(schema));
    schema = ValidSchema();
    schema.buttonGroups[0].buttons[0].bitmask = 0;
    EXPECT_TRUE(invalid(schema));
    schema = ValidSchema();
    schema.buttonGroups[0].buttons[0].bitmask = 0x3;
    EXPECT_TRUE(invalid(schema));
    schema = ValidSchema();
    schema.buttonGroups[0].buttons[1].bitmask = schema.buttonGroups[0].buttons[0].bitmask;
    EXPECT_TRUE(invalid(schema));
    schema = ValidSchema();
    schema.buttonGroups[0].buttons[0].label = "Wrong";
    EXPECT_TRUE(invalid(schema));
    schema = ValidSchema();
    schema.buttonGroups.pop_back();
    EXPECT_TRUE(invalid(schema));
    schema = ValidSchema();
    schema.sticks[0].label = " ";
    EXPECT_TRUE(invalid(schema));
    schema = ValidSchema();
    schema.sticks[1].label = schema.sticks[0].label;
    EXPECT_TRUE(invalid(schema));
    schema = ValidSchema();
    schema.sticks[1].index = schema.sticks[0].index;
    EXPECT_TRUE(invalid(schema));
    schema = ValidSchema();
    schema.sticks[1].index = 2;
    EXPECT_TRUE(invalid(schema));
}

} // namespace
} // namespace Ship
