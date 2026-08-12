#include <gtest/gtest.h>

#include "ship/core/Context.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/archive/ArchiveManager.h"
#include "ship/thread/ThreadPool.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace Ship {
namespace {

TEST(ResourceManagerLifecycleTest, ReleasesOwnedComponentsWhenRemoved) {
    const auto fixture =
        std::filesystem::temp_directory_path() /
        ("lus-resource-manager-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ASSERT_TRUE(std::filesystem::create_directories(fixture));
    std::ofstream(fixture / "resource.bin", std::ios::binary) << "resource";

    auto context = Context::CreateInstance("LUS resource manager test", "lus-resource-manager-test");
    context->Init();
    auto threadPool = std::make_shared<ThreadPool>(1);
    context->GetChildren().Add(threadPool);
    threadPool->Init();
    auto resourceManager = std::make_shared<ResourceManager>(threadPool);
    context->GetChildren().Add(resourceManager);
    resourceManager->Init({ { "archivePaths", std::vector<std::string>{ fixture.string() } },
                            { "validHashes", std::vector<uint32_t>{} } });

    std::weak_ptr<ResourceManager> weakResourceManager = resourceManager;
    std::weak_ptr<ArchiveManager> weakArchiveManager = resourceManager->GetArchiveManager();
    context->GetChildren().Remove(resourceManager, true);
    resourceManager.reset();

    EXPECT_TRUE(weakResourceManager.expired());
    EXPECT_TRUE(weakArchiveManager.expired());

    context.reset();
    std::error_code cleanupError;
    std::filesystem::remove_all(fixture, cleanupError);
    EXPECT_FALSE(cleanupError);
    EXPECT_FALSE(std::filesystem::exists(fixture));
}

TEST(ResourceManagerLifecycleTest, KeepsOwnedComponentsUntilRemovedFromEveryParent) {
    const auto fixture =
        std::filesystem::temp_directory_path() /
        ("lus-shared-resource-manager-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ASSERT_TRUE(std::filesystem::create_directories(fixture));
    std::ofstream(fixture / "resource.bin", std::ios::binary) << "resource";

    auto first = Context::CreateInstance("First resource parent", "first-resource-parent");
    auto second = Context::CreateInstance("Second resource parent", "second-resource-parent");
    auto threadPool = std::make_shared<ThreadPool>(1);
    auto resourceManager = std::make_shared<ResourceManager>(threadPool);
    first->GetChildren().Add(resourceManager);
    second->GetChildren().Add(resourceManager);
    resourceManager->Init({ { "archivePaths", std::vector<std::string>{ fixture.string() } },
                            { "validHashes", std::vector<uint32_t>{} } });

    first->GetChildren().Remove(resourceManager, true);
    EXPECT_EQ(resourceManager->GetParents().GetCount(), 1U);
    EXPECT_NE(resourceManager->GetArchiveManager(), nullptr);
    EXPECT_NE(resourceManager->GetResourceLoader(), nullptr);

    second->GetChildren().Remove(resourceManager, true);
    EXPECT_EQ(resourceManager->GetParents().GetCount(), 0U);
    EXPECT_EQ(resourceManager->GetArchiveManager(), nullptr);
    EXPECT_EQ(resourceManager->GetResourceLoader(), nullptr);

    first.reset();
    second.reset();
    resourceManager.reset();
    threadPool.reset();
    std::error_code cleanupError;
    std::filesystem::remove_all(fixture, cleanupError);
    EXPECT_FALSE(cleanupError);
}

} // namespace
} // namespace Ship
