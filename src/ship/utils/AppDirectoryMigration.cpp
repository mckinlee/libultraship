#include "ship/utils/AppDirectoryMigration.h"

#include <fstream>

namespace Ship {

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

    const auto markerPath = destination / ".lus-app-data-migrated";
    if (std::filesystem::exists(markerPath, error)) {
        return !error;
    }

    const auto finishMigration = [&]() {
        std::ofstream marker(markerPath, std::ios::binary | std::ios::trunc);
        marker << "1\n";
        marker.close();
        if (!marker) {
            error = std::make_error_code(std::errc::io_error);
            return false;
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

    for (std::filesystem::recursive_directory_iterator entry(source, error), end; entry != end;
         entry.increment(error)) {
        if (error) {
            return false;
        }

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
            if (!error) {
                std::filesystem::copy_file(entry->path(), targetPath, std::filesystem::copy_options::skip_existing,
                                           error);
            }
        }
        if (error) {
            return false;
        }
    }

    return finishMigration();
}

} // namespace Ship
