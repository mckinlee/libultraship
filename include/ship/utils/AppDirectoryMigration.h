#pragma once

#include <filesystem>
#include <system_error>

namespace Ship {

bool MigrateAppData(const std::filesystem::path& source, const std::filesystem::path& destination,
                    std::error_code& error);
bool AppDataMigrationNeedsLegacyFallback(const std::filesystem::path& destination, std::error_code& error);

} // namespace Ship
