#include <gtest/gtest.h>

#include "ship/utils/AppDirectoryMigration.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace Ship {
namespace {

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

TEST(AppDirectoryMigrationTest, CopiesMissingLegacyDataOnceWithoutReplacingNewerFiles) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lus-app-directory-migration-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto legacy = root / "libultraship";
    const auto application = root / "township";
    ASSERT_TRUE(std::filesystem::create_directories(legacy / "controllers"));
    ASSERT_TRUE(std::filesystem::create_directories(application));
    std::ofstream(legacy / "settings.json") << "legacy settings";
    std::ofstream(legacy / "default.sav", std::ios::binary) << "legacy save";
    std::ofstream(legacy / "controllers" / "gamepad.json") << "legacy controller";
    std::ofstream(application / "settings.json") << "current settings";

    std::error_code error;
    ASSERT_TRUE(MigrateAppData(legacy, application, error));
    EXPECT_FALSE(error);
    EXPECT_EQ(ReadText(application / "settings.json"), "current settings");
    EXPECT_EQ(ReadText(application / "default.sav"), "legacy save");
    EXPECT_EQ(ReadText(application / "controllers" / "gamepad.json"), "legacy controller");
    EXPECT_EQ(ReadText(legacy / "default.sav"), "legacy save");
    EXPECT_TRUE(std::filesystem::is_regular_file(application / ".lus-app-data-migrated"));

    ASSERT_TRUE(std::filesystem::remove(application / "default.sav"));
    ASSERT_TRUE(MigrateAppData(legacy, application, error));
    EXPECT_FALSE(error);
    EXPECT_EQ(ReadText(application / "settings.json"), "current settings");
    EXPECT_FALSE(std::filesystem::exists(application / "default.sav"));

    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error);
}

TEST(AppDirectoryMigrationTest, ReportsCopyFailureWithoutChangingLegacyData) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lus-app-directory-migration-failure-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto legacy = root / "libultraship";
    const auto occupiedDestination = root / "township";
    ASSERT_TRUE(std::filesystem::create_directories(legacy));
    std::ofstream(legacy / "default.sav", std::ios::binary) << "legacy save";
    std::ofstream(occupiedDestination) << "not a directory";

    std::error_code error;
    EXPECT_FALSE(MigrateAppData(legacy, occupiedDestination, error));
    EXPECT_TRUE(error);
    EXPECT_EQ(ReadText(legacy / "default.sav"), "legacy save");

    error.clear();
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error);
}

TEST(AppDirectoryMigrationTest, DoesNotImportLegacyDataCreatedAfterFirstRun) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lus-app-directory-no-legacy-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto legacy = root / "libultraship";
    const auto application = root / "township";

    std::error_code error;
    ASSERT_TRUE(MigrateAppData(legacy, application, error));
    ASSERT_TRUE(std::filesystem::is_regular_file(application / ".lus-app-data-migrated"));

    ASSERT_TRUE(std::filesystem::create_directories(legacy));
    std::ofstream(legacy / "default.sav", std::ios::binary) << "other application save";
    ASSERT_TRUE(MigrateAppData(legacy, application, error));
    EXPECT_FALSE(std::filesystem::exists(application / "default.sav"));

    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error);
}

} // namespace
} // namespace Ship
