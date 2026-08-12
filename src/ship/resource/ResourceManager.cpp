#include "ship/resource/ResourceManager.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "ship/resource/File.h"
#include "ship/resource/archive/Archive.h"
#include <algorithm>
#include <thread>
#include <stdexcept>
#include "ship/utils/StringHelper.h"
#include "ship/utils/Utils.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/security/Keystore.h"
#include "ship/thread/ThreadPool.h"

namespace Ship {
namespace {

template <typename T> std::shared_future<T> MakeReadySharedFuture(T value) {
    std::promise<T> promise;
    promise.set_value(std::move(value));
    return promise.get_future().share();
}

} // namespace

ResourceFilter::ResourceFilter(const std::list<std::string>& includeMasks, const std::list<std::string>& excludeMasks,
                               const uintptr_t owner, const std::shared_ptr<Archive> parent)
    : IncludeMasks(includeMasks), ExcludeMasks(excludeMasks), Owner(owner), Parent(parent) {
}

ResourceManager::ResourceManager(std::shared_ptr<ThreadPool> threadPool, std::shared_ptr<Keystore> keystore)
    : Component("ResourceManager"), mThreadPool(std::move(threadPool)), mKeystore(std::move(keystore)) {
}

void ResourceManager::OnInit(const nlohmann::json& initArgs) {
    auto archivePaths = initArgs.value("archivePaths", std::vector<std::string>{});
    auto hashesVec = initArgs.value("validHashes", std::vector<uint32_t>{});
    std::unordered_set<uint32_t> validHashes(hashesVec.begin(), hashesVec.end());

    mResourceLoader =
        std::make_shared<ResourceLoader>(std::dynamic_pointer_cast<ResourceManager>(GetSharedComponent()));
    mArchiveManager =
        std::make_shared<ArchiveManager>(std::dynamic_pointer_cast<ResourceManager>(GetSharedComponent()), mKeystore);
    GetArchiveManager()->Init(archivePaths, validHashes);

    if (!mArchiveManager->IsInitialized()) {
        // Nothing ever unpauses the thread pool since nothing will ever try to load the archive again.
        auto tpc = GetThreadPool();
        if (tpc) {
            tpc->Pause();
        }
        throw std::runtime_error("Failed to initialize ArchiveManager");
    }
}

void ResourceManager::OnRemoved(bool forced) {
    Component::OnRemoved(forced);
    if (GetParents().GetCount() != 0) {
        return;
    }

    {
        std::unique_lock<std::mutex> lock(mAsyncTaskMutex);
        mAcceptingAsyncTasks = false;
        mAsyncTaskFinished.wait(lock, [this]() { return mAsyncTaskCount == 0; });
    }
    mResourceLoader.reset();
    mArchiveManager.reset();
}

bool ResourceManager::BeginAsyncTask() {
    const std::lock_guard<std::mutex> lock(mAsyncTaskMutex);
    if (!mAcceptingAsyncTasks) {
        return false;
    }
    ++mAsyncTaskCount;
    return true;
}

void ResourceManager::FinishAsyncTask() {
    const std::lock_guard<std::mutex> lock(mAsyncTaskMutex);
    --mAsyncTaskCount;
    if (mAsyncTaskCount == 0) {
        mAsyncTaskFinished.notify_all();
    }
}

ResourceManager::~ResourceManager() {
    // Guard against logging after the Logger component (and spdlog) has shut down
    // during Context teardown.
    if (spdlog::default_logger()) {
        SPDLOG_INFO("destruct ResourceManager");
    }
}

std::shared_ptr<File> ResourceManager::LoadFileProcess(const std::string& filePath) {
    auto file = mArchiveManager->LoadFile(filePath);
    if (file != nullptr) {
        SPDLOG_TRACE("Loaded File {} on ResourceManager", filePath);
    } else {
        SPDLOG_TRACE("Could not load File {} in ResourceManager", filePath);
    }
    return file;
}

std::shared_ptr<File> ResourceManager::LoadFileProcess(const ResourceIdentifier& identifier) {
    if (identifier.GetParent() == nullptr) {
        if (identifier.IsPath()) {
            return LoadFileProcess(identifier.GetPath());
        }

        return LoadFileProcess(identifier.GetPathHash());
    }

    auto archive = identifier.GetParent();
    std::shared_ptr<File> file =
        identifier.IsPath() ? archive->LoadFile(identifier.GetPath()) : archive->LoadFile(identifier.GetPathHash());

    if (file != nullptr) {
        if (identifier.IsPath()) {
            SPDLOG_TRACE("Loaded File {} on ResourceManager", identifier.GetPath());
        } else {
            SPDLOG_TRACE("Loaded File {} on ResourceManager", identifier.GetPathHash());
        }
    } else {
        if (identifier.IsPath()) {
            SPDLOG_TRACE("Could not load File {} in ResourceManager", identifier.GetPath());
        } else {
            SPDLOG_TRACE("Could not load File {} in ResourceManager", identifier.GetPathHash());
        }
    }
    return file;
}

std::shared_ptr<File> ResourceManager::LoadFileProcess(uint64_t hash) {
    auto file = mArchiveManager->LoadFile(hash);
    if (file != nullptr) {
        SPDLOG_TRACE("Loaded File {} on ResourceManager", hash);
    } else {
        SPDLOG_TRACE("Could not load File {} in ResourceManager", hash);
    }
    return file;
}

std::shared_ptr<ResourceInitData> ResourceManager::ResolveMetaAlias(const ResourceIdentifier& identifier) {
    const std::string* basePath =
        identifier.IsPath() ? &identifier.GetPath() : mArchiveManager->HashToString(identifier.GetPathHash());
    if (basePath == nullptr || basePath->empty()) {
        return nullptr;
    }

    auto metaFile = LoadFileProcess({ *basePath + ".meta", identifier.GetOwner(), identifier.GetParent() });
    if (metaFile == nullptr) {
        return nullptr;
    }

    auto metaInitData = GetResourceLoader()->ReadResourceInitData(*basePath, metaFile);

    auto targetIdentifier = metaInitData->Identifier;
    targetIdentifier.SetOwner(identifier.GetOwner());
    metaInitData->Identifier = targetIdentifier;

    // Both candidates are ranked by the archive holding them, and the higher one loads. A tie
    // means one archive supplies both, and there its `.meta` is the more specific instruction.
    // Nothing at the requested path reports -1, so any reachable target outranks it.
    const int32_t targetPriority = mArchiveManager->GetFilePriority(targetIdentifier);
    const int32_t basePriority = mArchiveManager->GetFilePriority(identifier);
    if (targetPriority < basePriority) {
        SPDLOG_TRACE("Alias {} -> {} not taken: target ranks {} against {} at the requested path", *basePath,
                     targetIdentifier.GetPath(), targetPriority, basePriority);
        return nullptr;
    }

    return metaInitData;
}

std::shared_ptr<IResource> ResourceManager::LoadResourceProcess(const ResourceIdentifier& identifier, bool loadExact,
                                                                std::shared_ptr<ResourceInitData> initData) {
    if (initData != nullptr) {
        initData->Identifier = identifier;
    }

    // Check for and remove the OTR signature
    if (identifier.IsPath() && OtrSignatureCheck(identifier.GetPath().c_str())) {
        const auto newFilePath = identifier.GetPath().substr(7);
        return LoadResourceProcess({ newFilePath, identifier.GetOwner(), identifier.GetParent() }, false, initData);
    }

    // Cache the starts_with check to avoid repeated string comparisons
    const bool isAltPath = identifier.IsPath() && identifier.GetPath().starts_with(IResource::gAltAssetPrefix);
    const bool shouldCheckAlt = identifier.IsPath() && !loadExact && mAltAssetsEnabled && !isAltPath;

    // Attempt to load the alternate version of the asset, if we fail then we continue trying to load the standard
    // asset.
    if (shouldCheckAlt) {
        std::string altPath = IResource::gAltAssetPrefix;
        altPath += identifier.GetPath();
        auto altResource = LoadResourceProcess({ std::move(altPath), identifier.GetOwner(), identifier.GetParent() },
                                               loadExact, initData);

        if (altResource != nullptr) {
            return altResource;
        }
    }

    // While waiting in the queue, another thread could have loaded the resource.
    // In a last attempt to avoid doing work that will be discarded, let's check if the cached version exists.
    auto cacheLine = CheckCache(identifier, loadExact);
    auto cachedResource = GetCachedResource(cacheLine);
    if (cachedResource != nullptr) {
        return cachedResource;
    }

    // Check for resource load errors which can indicate an alternate asset.
    // If we are attempting to load an alternate asset, we can return null
    if (!loadExact && mAltAssetsEnabled && isAltPath) {
        if (std::holds_alternative<ResourceLoadError>(cacheLine)) {
            try {
                // If we have attempted to cache an alternate asset, but failed, we return nullptr and rely on the
                // calling function to return a regular asset. If we have NOT attempted load already, attempt the load.
                auto loadError = std::get<ResourceLoadError>(cacheLine);
                if (loadError != ResourceLoadError::NotCached) {
                    return nullptr;
                }
            } catch (std::bad_variant_access const& e) {
                // This should never happen. The holds_alternative check above should prevent it.
                SPDLOG_ERROR("Unexpected bad_variant_access in LoadResourceProcess: {}", e.what());
            }
        }
    }

    // A `.meta` beside this path can name a different file to load in its place
    std::shared_ptr<File> file = nullptr;
    if (initData == nullptr) {
        if (auto metaInitData = ResolveMetaAlias(identifier)) {
            file = LoadFileProcess(metaInitData->Identifier);
            if (file != nullptr) {
                initData = metaInitData;
            }
        }
    }

    // Get the file from the OTR
    if (file == nullptr) {
        file = LoadFileProcess(identifier);
    }

    if (file == nullptr) {
        if (identifier.IsPath()) {
            SPDLOG_TRACE("Failed to load resource file at path {}", identifier.GetPath());
        } else {
            SPDLOG_TRACE("Failed to load resource file at hash {}", identifier.GetPathHash());
        }
        std::shared_ptr<IResource> displacedResource;
        {
            const std::lock_guard<std::mutex> lock(mMutex);
            auto cacheIt = mResourceCache.find(identifier);
            if (cacheIt == mResourceCache.end()) {
                mResourceCache.emplace(identifier, ResourceLoadError::NotFound);
            } else {
                cachedResource = GetCachedResource(cacheIt->second);
                if (!cachedResource) {
                    if (std::holds_alternative<std::shared_ptr<IResource>>(cacheIt->second)) {
                        displacedResource = std::move(std::get<std::shared_ptr<IResource>>(cacheIt->second));
                    }
                    cacheIt->second = ResourceLoadError::NotFound;
                }
            }
        }
        return cachedResource;
    }

    // Transform the raw data into a resource
    auto resource = GetResourceLoader()->LoadResource(identifier, file, initData);

    // Keep replaced values alive until after the cache lock is released. Resource destructors
    // can load other resources and therefore must not run while mMutex is held.
    std::shared_ptr<IResource> discardedResource;
    std::shared_ptr<IResource> displacedResource;
    {
        const std::lock_guard<std::mutex> lock(mMutex);

        auto cacheIt = mResourceCache.find(identifier);
        if (cacheIt != mResourceCache.end()) {
            cachedResource = GetCachedResource(cacheIt->second);
        }
        if (cachedResource) {
            // If another thread has already loaded this resource, discard the work we already did and return from
            // cache.
            discardedResource = std::move(resource);
            resource = cachedResource;
        } else {
            if (cacheIt != mResourceCache.end() &&
                std::holds_alternative<std::shared_ptr<IResource>>(cacheIt->second)) {
                displacedResource = std::move(std::get<std::shared_ptr<IResource>>(cacheIt->second));
            }

            const auto cacheValue = resource != nullptr
                                        ? std::variant<ResourceLoadError, std::shared_ptr<IResource>>(resource)
                                        : std::variant<ResourceLoadError, std::shared_ptr<IResource>>(
                                              ResourceLoadError::NotFound);
            if (cacheIt == mResourceCache.end()) {
                mResourceCache.emplace(identifier, cacheValue);
            } else {
                cacheIt->second = cacheValue;
            }
        }
    }

    if (resource != nullptr) {
        if (identifier.IsPath()) {
            SPDLOG_TRACE("Loaded Resource {} on ResourceManager", identifier.GetPath());
        } else {
            SPDLOG_TRACE("Loaded Resource {} on ResourceManager", identifier.GetPathHash());
        }
    } else {
        if (identifier.IsPath()) {
            SPDLOG_TRACE("Resource load FAILED {} on ResourceManager", identifier.GetPath());
        } else {
            SPDLOG_TRACE("Resource load FAILED {} on ResourceManager", identifier.GetPathHash());
        }
    }

    return resource;
}

std::shared_ptr<IResource> ResourceManager::LoadResourceProcess(const std::string& filePath, bool loadExact,
                                                                std::shared_ptr<ResourceInitData> initData) {
    return LoadResourceProcess({ filePath, mDefaultCacheOwner, mDefaultCacheArchive }, loadExact, initData);
}

std::shared_future<std::shared_ptr<IResource>>
ResourceManager::LoadResourceAsync(const ResourceIdentifier& identifier, bool loadExact, BS::priority_t priority,
                                   std::shared_ptr<ResourceInitData> initData) {
    // Check for and remove the OTR signature
    if (identifier.IsPath() && OtrSignatureCheck(identifier.GetPath().c_str())) {
        auto newFilePath = identifier.GetPath().substr(7);
        return LoadResourceAsync({ newFilePath, identifier.GetOwner(), identifier.GetParent() }, loadExact, priority);
    }

    // Check the cache before queueing the job.
    auto cacheCheck = GetCachedResource(identifier, loadExact);
    if (cacheCheck) {
        auto promise = std::make_shared<std::promise<std::shared_ptr<IResource>>>();
        promise->set_value(cacheCheck);
        return promise->get_future().share();
    }

    if (!BeginAsyncTask()) {
        return MakeReadySharedFuture<std::shared_ptr<IResource>>(nullptr);
    }
    try {
        return GetThreadPool()->Get()->submit_task(
            [this, identifier, loadExact, initData]() -> std::shared_ptr<IResource> {
                try {
                    auto resource = LoadResourceProcess(identifier, loadExact, initData);
                    FinishAsyncTask();
                    return resource;
                } catch (...) {
                    FinishAsyncTask();
                    throw;
                }
            },
            priority);
    } catch (...) {
        FinishAsyncTask();
        throw;
    }
}

std::shared_future<std::shared_ptr<IResource>>
ResourceManager::LoadResourceAsync(const std::string& filePath, bool loadExact, BS::priority_t priority,
                                   std::shared_ptr<ResourceInitData> initData) {
    return LoadResourceAsync({ filePath, mDefaultCacheOwner, mDefaultCacheArchive }, loadExact, priority, initData);
}

std::shared_ptr<IResource> ResourceManager::LoadResource(const ResourceIdentifier& identifier, bool loadExact,
                                                         std::shared_ptr<ResourceInitData> initData) {
    auto resource = LoadResourceAsync(identifier, loadExact, BS::pr::highest, initData).get();
    if (resource == nullptr) {
        if (identifier.IsPath()) {
            SPDLOG_TRACE("Failed to load resource file at path {}", identifier.GetPath());
        } else {
            SPDLOG_TRACE("Failed to load resource file at hash {}", identifier.GetPathHash());
        }
    }
    return resource;
}

std::shared_ptr<IResource> ResourceManager::LoadResource(const std::string& filePath, bool loadExact,
                                                         std::shared_ptr<ResourceInitData> initData) {
    return LoadResource({ filePath, mDefaultCacheOwner, mDefaultCacheArchive }, loadExact, initData);
}

std::shared_ptr<IResource> ResourceManager::LoadResource(uint64_t crc, bool loadExact,
                                                         std::shared_ptr<ResourceInitData> initData) {
    return LoadResource({ crc, mDefaultCacheOwner, mDefaultCacheArchive }, loadExact, initData);
}

std::variant<ResourceManager::ResourceLoadError, std::shared_ptr<IResource>>
ResourceManager::CheckCache(const ResourceIdentifier& identifier, bool loadExact) {
    if (!loadExact && mAltAssetsEnabled && identifier.IsPath() &&
        !identifier.GetPath().starts_with(IResource::gAltAssetPrefix)) {
        const auto altPath = IResource::gAltAssetPrefix + identifier.GetPath();
        auto altCacheResult = CheckCache({ altPath, identifier.GetOwner(), identifier.GetParent() }, loadExact);

        // If the type held at this cache index is a resource, then we return it.
        // Else we attempt to load standard definition assets.
        if (std::holds_alternative<std::shared_ptr<IResource>>(altCacheResult)) {
            return altCacheResult;
        }
    }

    const std::lock_guard<std::mutex> lock(mMutex);

    auto cacheFind = mResourceCache.find(identifier);
    if (cacheFind == mResourceCache.end()) {
        return ResourceLoadError::NotCached;
    }

    return cacheFind->second;
}

std::variant<ResourceManager::ResourceLoadError, std::shared_ptr<IResource>>
ResourceManager::CheckCache(const std::string& filePath, bool loadExact) {
    return CheckCache({ filePath, mDefaultCacheOwner, mDefaultCacheArchive }, loadExact);
}

std::shared_ptr<IResource> ResourceManager::GetCachedResource(const ResourceIdentifier& identifier, bool loadExact) {
    // Gets the cached resource based on filePath.
    return GetCachedResource(CheckCache(identifier, loadExact));
}

std::shared_ptr<IResource> ResourceManager::GetCachedResource(const std::string& filePath, bool loadExact) {
    // Gets the cached resource based on filePath.
    return GetCachedResource({ filePath, mDefaultCacheOwner, mDefaultCacheArchive }, loadExact);
}

std::shared_ptr<IResource>
ResourceManager::GetCachedResource(std::variant<ResourceLoadError, std::shared_ptr<IResource>> cacheLine) {
    // Gets the cached resource based on a cache line std::variant from the cache map.
    if (std::holds_alternative<std::shared_ptr<IResource>>(cacheLine)) {
        try {
            auto resource = std::get<std::shared_ptr<IResource>>(cacheLine);

            if (resource.use_count() <= 0) {
                return nullptr;
            }

            if (resource->IsDirty()) {
                return nullptr;
            }

            return resource;
        } catch (std::bad_variant_access const& e) {
            // This should never happen. The holds_alternative check above should prevent it.
            SPDLOG_ERROR("Unexpected bad_variant_access in GetCachedResource: {}", e.what());
        }
    }

    return nullptr;
}

std::shared_ptr<std::vector<std::shared_ptr<IResource>>>
ResourceManager::LoadResourcesProcess(const ResourceFilter& filter) {
    auto loadedList = std::make_shared<std::vector<std::shared_ptr<IResource>>>();
    auto fileList = GetArchiveManager()->ListFiles(filter.IncludeMasks, filter.ExcludeMasks);
    loadedList->reserve(fileList->size());

    for (size_t i = 0; i < fileList->size(); i++) {
        auto fileName = std::string(fileList->operator[](i));
        auto resource = LoadResourceProcess({ fileName, filter.Owner, filter.Parent }, false, nullptr);
        loadedList->push_back(resource);
    }

    return loadedList;
}

std::shared_future<std::shared_ptr<std::vector<std::shared_ptr<IResource>>>>
ResourceManager::LoadResourcesAsync(const ResourceFilter& filter, BS::priority_t priority) {
    if (!BeginAsyncTask()) {
        return MakeReadySharedFuture(std::make_shared<std::vector<std::shared_ptr<IResource>>>());
    }
    try {
        return GetThreadPool()->Get()->submit_task(
            [this, filter]() -> std::shared_ptr<std::vector<std::shared_ptr<IResource>>> {
                try {
                    auto resources = LoadResourcesProcess(filter);
                    FinishAsyncTask();
                    return resources;
                } catch (...) {
                    FinishAsyncTask();
                    throw;
                }
            },
            priority);
    } catch (...) {
        FinishAsyncTask();
        throw;
    }
}

std::shared_future<std::shared_ptr<std::vector<std::shared_ptr<IResource>>>>
ResourceManager::LoadResourcesAsync(const std::string& searchMask, BS::priority_t priority) {
    return LoadResourcesAsync({ { searchMask }, {}, mDefaultCacheOwner, mDefaultCacheArchive }, priority);
}

std::shared_ptr<std::vector<std::shared_ptr<IResource>>> ResourceManager::LoadResources(const std::string& searchMask) {
    return LoadResources({ { searchMask }, {}, mDefaultCacheOwner, mDefaultCacheArchive });
}

std::shared_ptr<std::vector<std::shared_ptr<IResource>>> ResourceManager::LoadResources(const ResourceFilter& filter) {
    return LoadResourcesAsync(filter, BS::pr::highest).get();
}

void ResourceManager::DirtyResources(const ResourceFilter& filter) {
    if (!BeginAsyncTask()) {
        return;
    }
    try {
        GetThreadPool()->Get()->submit_task([this, filter]() -> void {
            try {
                auto list = GetArchiveManager()->ListFiles(filter.IncludeMasks, filter.ExcludeMasks);

                for (const auto& key : *list.get()) {
                    auto resource = GetCachedResource({ key, filter.Owner, filter.Parent });
                    // If it's a resource, we will set the dirty flag, else we will just unload it.
                    if (resource != nullptr) {
                        resource->Dirty();
                    } else {
                        UnloadResource({ key, filter.Owner, filter.Parent });
                    }
                }
                FinishAsyncTask();
            } catch (...) {
                FinishAsyncTask();
                throw;
            }
        });
    } catch (...) {
        FinishAsyncTask();
        throw;
    }
}

void ResourceManager::DirtyResources(const std::string& searchMask) {
    DirtyResources({ { searchMask }, {}, mDefaultCacheOwner, mDefaultCacheArchive });
}

void ResourceManager::UnloadResourcesAsync(const std::string& searchMask, BS::priority_t priority) {
    UnloadResourcesAsync({ { searchMask }, {}, mDefaultCacheOwner, mDefaultCacheArchive }, priority);
}

void ResourceManager::UnloadResourcesAsync(const ResourceFilter& filter, BS::priority_t priority) {
    if (!BeginAsyncTask()) {
        return;
    }
    try {
        GetThreadPool()->Get()->submit_task(
            [this, filter]() -> void {
                try {
                    UnloadResourcesProcess(filter);
                    FinishAsyncTask();
                } catch (...) {
                    FinishAsyncTask();
                    throw;
                }
            },
            priority);
    } catch (...) {
        FinishAsyncTask();
        throw;
    }
}

void ResourceManager::UnloadResources(const std::string& searchMask) {
    UnloadResources({ { searchMask }, {}, mDefaultCacheOwner, mDefaultCacheArchive });
}

void ResourceManager::UnloadResources(const ResourceFilter& filter) {
    UnloadResourcesProcess(filter);
}

void ResourceManager::UnloadResourcesProcess(const ResourceFilter& filter) {
    auto list = GetArchiveManager()->ListFiles(filter.IncludeMasks, filter.ExcludeMasks);

    for (const auto& key : *list.get()) {
        UnloadResource({ key, mDefaultCacheOwner, mDefaultCacheArchive });
    }
}

std::shared_ptr<ArchiveManager> ResourceManager::GetArchiveManager() {
    return mArchiveManager;
}

std::shared_ptr<ResourceLoader> ResourceManager::GetResourceLoader() {
    return mResourceLoader;
}

size_t ResourceManager::UnloadResource(const ResourceIdentifier& identifier) {
    // Store a shared pointer here so that erase doesn't destruct the resource.
    // The resource will attempt to load other resources on the destructor, and this will fail because we already hold
    // the mutex.
    std::variant<ResourceLoadError, std::shared_ptr<IResource>> value = nullptr;
    size_t ret = 0;
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        auto it = mResourceCache.find(identifier);
        if (it != mResourceCache.end()) {
            value = std::move(it->second);
            mResourceCache.erase(it);
            ret = 1;
        }
    }

    return ret;
}

size_t ResourceManager::UnloadResource(const std::string& filePath) {
    return UnloadResource({ filePath, mDefaultCacheOwner, mDefaultCacheArchive });
}

bool ResourceManager::WriteResource(const ResourceIdentifier& identifier, const std::vector<uint8_t>& data,
                                    bool unloadFile) {
    std::string path;
    if (identifier.IsPath()) {
        path = identifier.GetPath();
    } else {
        const std::string* resolvedPath = mArchiveManager->HashToString(identifier.GetPathHash());
        if (resolvedPath == nullptr) {
            return false;
        }
        path = *resolvedPath;
    }

    std::shared_ptr<Archive> archive = identifier.GetParent();

    if (!archive) {
        archive = mArchiveManager->GetArchiveFromFile(path);
    }

    if (!archive) {
        return false;
    }

    if (!mArchiveManager->WriteFile(archive, path, data)) {
        return false;
    }

    if (unloadFile) {
        UnloadResource(identifier);
    }

    return true;
}

bool ResourceManager::WriteResource(uint64_t hash, const std::vector<uint8_t>& data, bool unloadFile) {
    const std::string* resolvedPath = mArchiveManager->HashToString(hash);
    if (resolvedPath == nullptr) {
        return false;
    }

    ResourceIdentifier identifier(hash, mDefaultCacheOwner, mDefaultCacheArchive);

    auto archive = mArchiveManager->GetArchiveFromFile(*resolvedPath);
    if (archive == nullptr) {
        return false;
    }

    identifier.SetParent(archive);
    return WriteResource(identifier, data, unloadFile);
}

bool ResourceManager::OtrSignatureCheck(const char* fileName) {
    static const char* sOtrSignature = "__OTR__";
    return strncmp(fileName, sOtrSignature, strlen(sOtrSignature)) == 0;
}

bool ResourceManager::IsAltAssetsEnabled() {
    return mAltAssetsEnabled;
}

void ResourceManager::SetAltAssetsEnabled(bool isEnabled) {
    mAltAssetsEnabled = isEnabled;
}

size_t ResourceManager::GetResourceSize(std::shared_ptr<IResource> resource) {
    if (resource == nullptr) {
        return 0;
    }

    return resource->GetPointerSize();
}

size_t ResourceManager::GetResourceSize(const char* name) {
    auto resource = LoadResource(name);

    return GetResourceSize(resource);
}

size_t ResourceManager::GetResourceSize(uint64_t crc) {
    auto resource = LoadResource(crc);

    return GetResourceSize(resource);
}

bool ResourceManager::GetResourceIsCustom(std::shared_ptr<IResource> resource) {
    if (resource == nullptr) {
        return false;
    }

    return resource->GetInitData()->IsCustom;
}

bool ResourceManager::GetResourceIsCustom(const char* name) {
    auto resource = LoadResource(name);

    return GetResourceIsCustom(resource);
}

bool ResourceManager::GetResourceIsCustom(uint64_t crc) {
    auto resource = LoadResource(crc);

    return GetResourceIsCustom(resource);
}

void* ResourceManager::GetResourceRawPointer(std::shared_ptr<IResource> resource) {
    if (resource == nullptr) {
        return nullptr;
    }

    return resource->GetRawPointer();
}

void* ResourceManager::GetResourceRawPointer(const char* name) {
    auto resource = LoadResource(name);

    return GetResourceRawPointer(resource);
}

void* ResourceManager::GetResourceRawPointer(uint64_t crc) {
    auto resource = LoadResource(crc);

    return GetResourceRawPointer(resource);
}

std::shared_ptr<ThreadPool> ResourceManager::GetThreadPool() {
    return mThreadPool;
}

} // namespace Ship
