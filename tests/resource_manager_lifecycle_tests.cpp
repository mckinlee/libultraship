#include <gtest/gtest.h>

#include "ship/core/Context.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/archive/ArchiveManager.h"
#include "ship/resource/factory/BlobFactory.h"
#include "ship/resource/ResourceFactoryBinary.h"
#include "ship/thread/ThreadPool.h"
#include "ship/resource/ResourceType.h"
#include "ship/utils/binarytools/endianness.h"
#include "archive_resource_fixtures.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace Ship {
namespace {

std::string HeaderedBlob(const std::string& payload) {
    std::string header(OTR_HEADER_SIZE, '\0');
    header[0] = static_cast<char>(Endianness::Native);
    const uint32_t type = static_cast<uint32_t>(ResourceType::Blob);
    std::memcpy(header.data() + 4, &type, sizeof(type));
    const uint32_t size = static_cast<uint32_t>(payload.size());
    std::string body(sizeof(size), '\0');
    std::memcpy(body.data(), &size, sizeof(size));
    return header + body + payload;
}

class ReentrantResource final : public Resource<void> {
  public:
    ReentrantResource(std::shared_ptr<ResourceInitData> initData, std::function<void()> onDestroy)
        : Resource(std::move(initData)), mOnDestroy(std::move(onDestroy)) {
    }

    ~ReentrantResource() override {
        if (mOnDestroy) {
            mOnDestroy();
        }
    }

    void* GetPointer() override {
        return &mData;
    }

    size_t GetPointerSize() override {
        return sizeof(mData);
    }

  private:
    uint8_t mData = 0;
    std::function<void()> mOnDestroy;
};

class ReentrantResourceFactory final : public ResourceFactoryBinary {
  public:
    explicit ReentrantResourceFactory(std::function<void()> onDestroy) : mOnDestroy(std::move(onDestroy)) {
    }

    std::shared_ptr<IResource> ReadResource(std::shared_ptr<File> file,
                                            std::shared_ptr<ResourceInitData> initData) override {
        if (!FileHasValidFormatAndReader(file, initData)) {
            return nullptr;
        }
        return std::make_shared<ReentrantResource>(std::move(initData), mOnDestroy);
    }

  private:
    std::function<void()> mOnDestroy;
};

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

TEST(ResourceManagerLifecycleTest, WaitsForQueuedResourceWorkBeforeReleasingDependencies) {
    const auto fixture =
        std::filesystem::temp_directory_path() /
        ("lus-resource-manager-queued-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ASSERT_TRUE(std::filesystem::create_directories(fixture));
    std::ofstream(fixture / "resource.bin", std::ios::binary) << "resource";

    auto context = Context::CreateInstance("Queued resource test", "queued-resource-test");
    auto threadPool = std::make_shared<ThreadPool>(1);
    auto resourceManager = std::make_shared<ResourceManager>(threadPool);
    context->GetChildren().Add(resourceManager);
    resourceManager->Init({ { "archivePaths", std::vector<std::string>{ fixture.string() } },
                            { "validHashes", std::vector<uint32_t>{} } });

    std::promise<void> releaseWorker;
    auto releaseSignal = releaseWorker.get_future().share();
    std::promise<void> workerStarted;
    auto blocker = threadPool->Get()->submit_task([&workerStarted, releaseSignal]() {
        workerStarted.set_value();
        releaseSignal.wait();
    });
    workerStarted.get_future().wait();
    auto resource = resourceManager->LoadResourceAsync("resource.bin");

    auto removal = std::async(std::launch::async, [&]() { context->GetChildren().Remove(resourceManager, true); });
    EXPECT_EQ(removal.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
    releaseWorker.set_value();
    blocker.get();
    resource.wait();
    EXPECT_EQ(removal.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(resourceManager->GetArchiveManager(), nullptr);

    resourceManager.reset();
    context.reset();
    std::error_code cleanupError;
    std::filesystem::remove_all(fixture, cleanupError);
    EXPECT_FALSE(cleanupError);
}

TEST(ResourceManagerLifecycleTest, ConcurrentFailedLoadsAndUnloadsKeepTheCacheConsistent) {
    const auto fixture =
        std::filesystem::temp_directory_path() /
        ("lus-resource-manager-concurrent-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ASSERT_TRUE(std::filesystem::create_directories(fixture));

    auto context = Context::CreateInstance("Concurrent resource test", "concurrent-resource-test");
    auto threadPool = std::make_shared<ThreadPool>(4);
    auto resourceManager = std::make_shared<ResourceManager>(threadPool);
    context->GetChildren().Add(resourceManager);
    resourceManager->Init({ { "archivePaths", std::vector<std::string>{ fixture.string() } },
                            { "validHashes", std::vector<uint32_t>{} } });

    std::vector<std::shared_future<std::shared_ptr<IResource>>> loads;
    loads.reserve(64);
    for (size_t i = 0; i < 64; ++i) {
        loads.push_back(resourceManager->LoadResourceAsync("missing-" + std::to_string(i) + ".bin"));
    }
    std::thread unloader([&]() {
        for (size_t i = 0; i < 64; ++i) {
            resourceManager->UnloadResource("missing-" + std::to_string(i) + ".bin");
        }
    });
    for (auto& load : loads) {
        EXPECT_EQ(load.get(), nullptr);
    }
    unloader.join();

    context->GetChildren().Remove(resourceManager, true);
    resourceManager.reset();
    context.reset();
    std::error_code cleanupError;
    std::filesystem::remove_all(fixture, cleanupError);
    EXPECT_FALSE(cleanupError);
}

TEST(ResourceManagerLifecycleTest, ConcurrentSameKeyLoadsPublishOneResourceInstance) {
    LusTest::TempDirectoryArchive base;
    auto threadPool = std::make_shared<ThreadPool>(4);
    auto resourceManager = std::make_shared<ResourceManager>(threadPool);
    resourceManager->Init({ { "archivePaths", std::vector<std::string>{ base.GetPath().string() } },
                            { "validHashes", std::vector<uint32_t>{} } });
    resourceManager->GetResourceLoader()->RegisterResourceFactory(
        std::make_shared<ResourceFactoryBinaryBlobV0>(), RESOURCE_FORMAT_BINARY, "Blob",
        static_cast<uint32_t>(ResourceType::Blob), 0);
    resourceManager->GetArchiveManager()->AddArchive(
        LusTest::LoadedArchive("ram://same-key", { { "same.bin", HeaderedBlob("payload") } }));

    std::vector<std::shared_future<std::shared_ptr<IResource>>> loads;
    loads.reserve(32);
    for (size_t i = 0; i < 32; ++i) {
        loads.push_back(resourceManager->LoadResourceAsync("same.bin"));
    }
    const auto first = loads.front().get();
    ASSERT_NE(first, nullptr);
    for (auto& load : loads) {
        EXPECT_EQ(load.get(), first);
    }
}

TEST(ResourceManagerLifecycleTest, ReplacingDirtyResourceDestroysItAfterUnlockingCache) {
    LusTest::TempDirectoryArchive base;
    auto threadPool = std::make_shared<ThreadPool>(1);
    auto resourceManager = std::make_shared<ResourceManager>(threadPool);
    resourceManager->Init({ { "archivePaths", std::vector<std::string>{ base.GetPath().string() } },
                            { "validHashes", std::vector<uint32_t>{} } });
    const std::weak_ptr<ResourceManager> weakResourceManager = resourceManager;
    resourceManager->GetResourceLoader()->RegisterResourceFactory(
        std::make_shared<ReentrantResourceFactory>([weakResourceManager]() {
            if (auto manager = weakResourceManager.lock()) {
                manager->GetCachedResource("same.bin");
            }
        }),
        RESOURCE_FORMAT_BINARY, "Reentrant", static_cast<uint32_t>(ResourceType::Blob), 0);
    resourceManager->GetArchiveManager()->AddArchive(
        LusTest::LoadedArchive("ram://dirty-resource", { { "same.bin", HeaderedBlob("payload") } }));

    auto first = resourceManager->LoadResource("same.bin");
    ASSERT_NE(first, nullptr);
    first->Dirty();
    first.reset();

    auto replacement = resourceManager->LoadResourceAsync("same.bin");
    ASSERT_EQ(replacement.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_NE(replacement.get(), nullptr);
}

TEST(ResourceManagerLifecycleTest, FailedDirtyReloadDoesNotReturnStaleResource) {
    LusTest::TempDirectoryArchive base;
    auto threadPool = std::make_shared<ThreadPool>(1);
    auto resourceManager = std::make_shared<ResourceManager>(threadPool);
    resourceManager->Init({ { "archivePaths", std::vector<std::string>{ base.GetPath().string() } },
                            { "validHashes", std::vector<uint32_t>{} } });
    resourceManager->GetResourceLoader()->RegisterResourceFactory(
        std::make_shared<ResourceFactoryBinaryBlobV0>(), RESOURCE_FORMAT_BINARY, "Blob",
        static_cast<uint32_t>(ResourceType::Blob), 0);
    auto archive = LusTest::LoadedArchive("ram://removed-resource", { { "same.bin", HeaderedBlob("payload") } });
    resourceManager->GetArchiveManager()->AddArchive(archive);

    auto first = resourceManager->LoadResource("same.bin");
    ASSERT_NE(first, nullptr);
    first->Dirty();
    resourceManager->GetArchiveManager()->RemoveArchive(archive);

    EXPECT_EQ(resourceManager->LoadResource("same.bin"), nullptr);
    EXPECT_EQ(resourceManager->LoadResource("same.bin"), nullptr);
}

} // namespace
} // namespace Ship
