#include <gtest/gtest.h>

#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldeck/ControlPort.h"
#include "ship/controller/controldevice/controller/Controller.h"
#include "ship/core/Context.h"
#include "libultraship/bridge/controllerbridge.h"
#include "libultraship/libultra/os.h"

namespace Ship {
namespace {

constexpr CONTROLLERBUTTONS_T kTestButton = 0x4000;
constexpr char kTestButtonMappingIds[] = "Controllers.Port1.Buttons.TestButtonMappingIds";

class TestController final : public Controller {
  public:
    explicit TestController(std::shared_ptr<ConsoleVariable> consoleVariable)
        : Controller(0, { kTestButton }, std::move(consoleVariable)) {
    }

    void ReadToPad(void*) override {
    }
};

class TestControlDeck final : public ControlDeck {
  public:
    TestControlDeck(std::unordered_map<CONTROLLERBUTTONS_T, std::string> buttonNames,
                    std::shared_ptr<ConsoleVariable> consoleVariable)
        : ControlDeck({}, nullptr, std::move(buttonNames), nullptr, consoleVariable),
          mTestConsoleVariable(std::move(consoleVariable)) {
    }

    void WriteToPad(void*) override {
        mWroteToPad = true;
    }

    bool WroteToPad() const {
        return mWroteToPad;
    }

    void AddControllerBackReference(const std::shared_ptr<ControlDeck>& self) {
        auto controller = std::make_shared<TestController>(mTestConsoleVariable);
        controller->SetControlDeck(self);
        mPorts.push_back(std::make_shared<ControlPort>(0, std::move(controller)));
    }

  private:
    std::shared_ptr<ConsoleVariable> mTestConsoleVariable;
    bool mWroteToPad = false;
};

TEST(ControlDeckTest, PreservesGameSpecificButtonNames) {
    auto consoleVariables = std::make_shared<ConsoleVariable>();
    TestControlDeck controlDeck({ { kTestButton, "Test" } }, consoleVariables);

    EXPECT_EQ(controlDeck.GetAllButtonNames().at(kTestButton), "Test");
    EXPECT_EQ(controlDeck.GetButtonNameForBitmask(kTestButton), "Test");
}

TEST(ControlDeckTest, ReleasesControllerBackReferencesWithoutDeletingConfiguration) {
    auto context = Context::CreateInstance("ControlDeck lifecycle test", "control-deck-lifecycle-test");
    auto consoleVariables = std::make_shared<ConsoleVariable>();
    consoleVariables->SetString(kTestButtonMappingIds, "persistent-mapping");
    auto deck = std::make_shared<TestControlDeck>(
        std::unordered_map<CONTROLLERBUTTONS_T, std::string>{ { kTestButton, "Test" } }, consoleVariables);
    deck->AddControllerBackReference(deck);
    context->GetChildren().Add(deck);
    std::weak_ptr<ControlDeck> releasedDeck = deck;

    EXPECT_GE(static_cast<int32_t>(context->GetChildren().Remove(deck, true)), 0);
    deck.reset();

    EXPECT_TRUE(releasedDeck.expired());
    EXPECT_STREQ(consoleVariables->GetString(kTestButtonMappingIds, ""), "persistent-mapping");
}

TEST(ControlDeckTest, LibultraDoesNotRetainTheControlDeckAfterBridgeTeardown) {
    auto context = Context::CreateInstance("Libultra controller lifecycle test", "libultra-controller-test");
    auto consoleVariables = std::make_shared<ConsoleVariable>();
    auto deck = std::make_shared<TestControlDeck>(
        std::unordered_map<CONTROLLERBUTTONS_T, std::string>{ { kTestButton, "Test" } }, consoleVariables);
    context->GetChildren().Add(deck);
    ControllerSetControlDeck(deck);

    OSContPad pads[MAXCONTROLLERS]{};
    osContGetReadData(pads);
    EXPECT_TRUE(deck->WroteToPad());

    std::weak_ptr<ControlDeck> releasedDeck = deck;
    ControllerSetControlDeck(nullptr);
    EXPECT_GE(static_cast<int32_t>(context->GetChildren().Remove(deck, true)), 0);
    deck.reset();

    EXPECT_TRUE(releasedDeck.expired());
    context.reset();
}

} // namespace
} // namespace Ship
