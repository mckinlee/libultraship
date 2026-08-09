#include <gtest/gtest.h>

#include "libultraship/bridge/UltraBridge.h"
#include "libultraship/bridge/apppathbridge.h"
#include "ship/config/Config.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/core/Context.h"

#include <filesystem>
#include <fstream>

namespace {

TEST(AppPathBridgeTest, UltraBridgePublishesAndClearsOnlyTheContextShortName) {
    auto context = Ship::Context::CreateInstance("Township", "township");
    auto bridge = std::make_shared<LUS::UltraBridge>();
    context->GetChildren().Add(bridge);

    bridge->UpdateCaches(context);
    EXPECT_EQ(AppPathGetShortName(), "township");
    EXPECT_EQ(context->GetShortName(), "township");

    bridge->ClearCaches();
    EXPECT_TRUE(AppPathGetShortName().empty());
    context.reset();
}

TEST(AppPathBridgeTest, LegacyConsoleVariablesUseTheConfiguredApplicationDirectory) {
    const auto root = std::filesystem::temp_directory_path() / "lus-app-path-bridge-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream legacy(root / "cvars.cfg");
        legacy << "gTownshipLegacyPath = 37\n";
    }

    auto config = std::make_shared<Ship::Config>((root / "township.json").string());
    auto variables = std::make_shared<Ship::ConsoleVariable>(config);

    EXPECT_EQ(variables->GetInteger("gTownshipLegacyPath", 0), 37);
    EXPECT_FALSE(std::filesystem::exists(root / "cvars.cfg"));
    std::filesystem::remove_all(root);
}

} // namespace
