#pragma once

#undef _DLL

#include <string>
#include <stdint.h>
#include <string>

#include "resource/File.h"
#include "resource/Resource.h"
#include "resource/archive/Archive.h"

namespace LUS {
struct File;

class O2rArchive : virtual public Archive {
  public:
    O2rArchive(const std::string& archivePath);
    ~O2rArchive();

    bool LoadRaw();
    bool UnloadRaw();
    std::shared_ptr<File> LoadFileRaw(const std::string& filePath);
    std::shared_ptr<File> LoadFileRaw(uint64_t hash);

  protected:
    std::shared_ptr<ResourceInitData> LoadFileMeta(const std::string& filePath);
    std::shared_ptr<ResourceInitData> LoadFileMeta(uint64_t hash);

  private:
};
} // namespace LUS
