#pragma once

#include <filesystem>
#include <string>

namespace swgui {

// Chemins <-> UTF-8 (ImGui, GLFW et JSON travaillent en UTF-8).
std::string to_utf8(const std::filesystem::path& path);
std::filesystem::path from_utf8(const std::string& text);

// Date de modification d'un fichier, heure locale "AAAA-MM-JJ HH:MM" ("" si absent).
std::string file_date(const std::filesystem::path& path);

// Minuscules sans accents (é -> e...), pour les recherches.
std::string fold(const std::string& text);

// Nombre avec séparateur de milliers (espace), comme la CLI.
std::string group_digits(long long value);

}  // namespace swgui
