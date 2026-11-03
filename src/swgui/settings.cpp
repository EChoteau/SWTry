#include "settings.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <system_error>

namespace swgui {

using json = nlohmann::json;

Settings load_settings(const std::filesystem::path& user_directory) {
    Settings settings;
    std::ifstream input(user_directory / "settings.json");
    if (!input) return settings;
    try {
        json root;
        input >> root;
        settings.last_export = root.value("last_export", settings.last_export);
        settings.last_storage = root.value("last_storage", settings.last_storage);
        settings.window_width = root.value("window_width", settings.window_width);
        settings.window_height = root.value("window_height", settings.window_height);
    } catch (const json::exception&) {
        // Réglages illisibles : on repart des valeurs par défaut.
    }
    return settings;
}

bool save_settings(const std::filesystem::path& user_directory,
                   const Settings& settings) {
    std::error_code error;
    std::filesystem::create_directories(user_directory, error);
    std::ofstream output(user_directory / "settings.json", std::ios::binary);
    if (!output) return false;
    const json root{{"last_export", settings.last_export},
                    {"last_storage", settings.last_storage},
                    {"window_width", settings.window_width},
                    {"window_height", settings.window_height}};
    output << root.dump(2) << '\n';
    return static_cast<bool>(output);
}

}  // namespace swgui
