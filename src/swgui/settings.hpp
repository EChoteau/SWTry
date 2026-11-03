#pragma once

#include <filesystem>
#include <string>

namespace swgui {

// data/user/settings.json : derniers fichiers chargés et taille de fenêtre.
struct Settings {
    std::string last_export;   // chemin UTF-8
    std::string last_storage;  // chemin UTF-8
    int window_width = 1440;
    int window_height = 900;
};

Settings load_settings(const std::filesystem::path& user_directory);
bool save_settings(const std::filesystem::path& user_directory,
                   const Settings& settings);

}  // namespace swgui
