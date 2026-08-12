#include "ship/utils/AppDirectoryMigration.h"

#include <fstream>

namespace Ship {
namespace {

constexpr char kMigrationMarker[] = ".lus-app-data-migrated";
constexpr char kMigrationPendingMarker[] = ".lus-app-data-migration-pending";

} // namespace

bool AppDataMigrationNeedsLegacyFallback(const std::filesystem::path& destination, std::error_code& error) {
    error.clear();
    if (std::filesystem::exists(destination / kMigrationMarker, error)) {
        return false;
    }
    if (error) {
        return false;
    }
    const bool pending = std::filesystem::exists(destination / kMigrationPendingMarker, error);
    if (error || pending) {
        return pending;
    }
    return std::filesystem::is_empty(destination, error) && !error;
}

bool MigrateAppData(const std::filesystem::path& source, const std::filesystem::path& destination,
                    std::error_code& error) {
    error.clear();
    if (source.empty() || destination.empty() || source == destination) {
        return true;
    }

    std::filesystem::create_directories(destination, error);
    if (error) {
        return false;
    }

    const auto markerPath = destination / kMigrationMarker;
    const auto pendingMarkerPath = destination / kMigrationPendingMarker;
    if (std::filesystem::exists(markerPath, error)) {
        return !error;
    }

    bool pendingMigration = std::filesystem::exists(pendingMarkerPath, error);
    if (error) {
        return false;
    }
    if (!pendingMigration && std::filesystem::is_empty(destination, error)) {
        if (error) {
            return false;
        }
        std::ofstream pendingMarker(pendingMarkerPath, std::ios::binary | std::ios::trunc);
        pendingMarker << "1\n";
        pendingMarker.close();
        if (!pendingMarker) {
            error = std::make_error_code(std::errc::io_error);
            return false;
        }
        pendingMigration = true;
    }

    const auto finishMigration = [&]() {
        std::ofstream marker(markerPath, std::ios::binary | std::ios::trunc);
        marker << "1\n";
        marker.close();
        if (!marker) {
            error = std::make_error_code(std::errc::io_error);
            return false;
        }
        if (pendingMigration) {
            std::error_code cleanupError;
            std::filesystem::remove(pendingMarkerPath, cleanupError);
            if (cleanupError) {
                error = cleanupError;
                return false;
            }
        }
        return true;
    };

    if (!std::filesystem::exists(source, error)) {
        return error ? false : finishMigration();
    }
    if (!std::filesystem::is_directory(source, error)) {
        if (!error) {
            error = std::make_error_code(std::errc::not_a_directory);
        }
        return false;
    }

    std::filesystem::recursive_directory_iterator entry(source, error), end;
    if (error) {
        return false;
    }
    while (entry != end) {

        const auto relativePath = std::filesystem::relative(entry->path(), source, error);
        if (error) {
            return false;
        }
        const auto targetPath = destination / relativePath;
        const auto status = entry->symlink_status(error);
        if (error) {
            return false;
        }

        if (std::filesystem::is_directory(status)) {
            std::filesystem::create_directories(targetPath, error);
        } else if (std::filesystem::is_regular_file(status)) {
            std::filesystem::create_directories(targetPath.parent_path(), error);
            if (!error && !std::filesystem::exists(targetPath, error)) {
                auto stagingPath = targetPath;
                stagingPath += ".lus-migration-copy";
                std::filesystem::remove(stagingPath, error);
                if (!error) {
                    std::filesystem::copy_file(entry->path(), stagingPath,
                                               std::filesystem::copy_options::overwrite_existing, error);
                }
                if (!error) {
                    std::filesystem::rename(stagingPath, targetPath, error);
                }
            }
        }
        if (error) {
            return false;
        }
        entry.increment(error);
        if (error) {
            return false;
        }
    }

    return finishMigration();
}

} // namespace Ship
