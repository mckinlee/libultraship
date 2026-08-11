#include <gtest/gtest.h>

#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldeck/ControlPort.h"
#include "ship/controller/controldevice/controller/Controller.h"
#include "ship/core/Context.h"

namespace Ship {
namespace {

class TestController final : public Controller {
  public:
    explicit TestController(std::shared_ptr<ConsoleVariable> consoleVariable)
        : Controller(0, {}, std::move(consoleVariable)) {
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
    }

    void AddControllerBackReference(const std::shared_ptr<ControlDeck>& self) {
        auto controller = std::make_shared<TestController>(mTestConsoleVariable);
        controller->SetControlDeck(self);
        mPorts.push_back(std::make_shared<ControlPort>(0, std::move(controller)));
    }

  private:
    std::shared_ptr<ConsoleVariable> mTestConsoleVariable;
};

TEST(ControlDeckTest, PreservesGameSpecificButtonNames) {
    constexpr CONTROLLERBUTTONS_T testButton = 0x4000;
    auto consoleVariables = std::make_shared<ConsoleVariable>();
    TestControlDeck controlDeck({ { testButton, "Test" } }, consoleVariables);

    EXPECT_EQ(controlDeck.GetAllButtonNames().at(testButton), "Test");
    EXPECT_EQ(controlDeck.GetButtonNameForBitmask(testButton), "Test");
}

TEST(ControlDeckTest, ReleasesControllerBackReferencesWhenRemoved) {
    auto context = Context::CreateInstance("ControlDeck lifecycle test", "control-deck-lifecycle-test");
    auto deck = std::make_shared<TestControlDeck>(std::unordered_map<CONTROLLERBUTTONS_T, std::string>{},
                                                  std::make_shared<ConsoleVariable>());
    deck->AddControllerBackReference(deck);
    context->GetChildren().Add(deck);
    std::weak_ptr<ControlDeck> releasedDeck = deck;

    EXPECT_GE(static_cast<int32_t>(context->GetChildren().Remove(deck, true)), 0);
    deck.reset();

    EXPECT_TRUE(releasedDeck.expired());
}

} // namespace
} // namespace Ship
