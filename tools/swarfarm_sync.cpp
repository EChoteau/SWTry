// Télécharge / met à jour les données SWARFARM dans data/swarfarm/.
// Usage (depuis la racine du dépôt) : swsync [--only monsters] [--dry-run]
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr const char* kBase = "https://swarfarm.com/api/v2";
constexpr auto kDelay = std::chrono::seconds(1);  // respecte les limites de débit
const std::vector<std::string> kEndpoints = {"monsters", "skills"};
const fs::path kOut = "data/swarfarm";

json get_page(const std::string& url) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        cpr::Response r = cpr::Get(
            cpr::Url{url},
            cpr::Header{{"User-Agent", "sw-rune-optimizer-sync/0.1 (projet perso)"}},
            cpr::Timeout{30000});
        if (r.status_code == 429) {  // trop de requêtes : pause puis nouvel essai
            int wait = 10 * (attempt + 1);
            if (auto it = r.header.find("Retry-After"); it != r.header.end())
                wait = std::stoi(it->second);
            std::cout << "  429, pause de " << wait << "s\n";
            std::this_thread::sleep_for(std::chrono::seconds(wait));
            continue;
        }
        if (r.error || r.status_code != 200)
            throw std::runtime_error("HTTP " + std::to_string(r.status_code) + " sur " + url +
                                     " : " + r.error.message);
        return json::parse(r.text);
    }
    throw std::runtime_error("Trop de 429 sur " + url);
}

// Parcourt toutes les pages ; "next" contient déjà les paramètres de la page suivante.
std::map<long long, json> fetch_all(const std::string& endpoint) {
    std::map<long long, json> items;
    std::string url = std::string(kBase) + "/" + endpoint + "/?format=json";
    while (!url.empty()) {
        json page = get_page(url);
        for (auto& o : page.at("results")) items[o.at("id").get<long long>()] = o;
        std::cout << "  " << endpoint << ": " << items.size() << "/"
                  << page.value("count", 0) << "\n";
        url = page["next"].is_string() ? page["next"].get<std::string>() : "";
        std::this_thread::sleep_for(kDelay);
    }
    return items;
}

std::map<long long, json> load(const fs::path& p) {
    std::map<long long, json> m;
    if (!fs::exists(p)) return m;
    std::ifstream f(p);
    for (auto& o : json::parse(f)) m[o.at("id").get<long long>()] = o;
    return m;
}

void sync(const std::string& endpoint, bool dry_run) {
    const fs::path path = kOut / (endpoint + ".json");
    auto old_items = load(path);
    auto new_items = fetch_all(endpoint);

    size_t added = 0, removed = 0, changed = 0;
    for (auto& [id, o] : new_items) {
        auto it = old_items.find(id);
        if (it == old_items.end()) {
            ++added;
            if (added <= 20) std::cout << "  + " << id << " " << o.value("name", "") << "\n";
        } else if (it->second != o) ++changed;
    }
    for (auto& [id, o] : old_items) if (!new_items.count(id)) ++removed;
    std::cout << "[" << endpoint << "] total=" << new_items.size() << " +" << added
              << " -" << removed << " modifies=" << changed << "\n";
    if (dry_run) return;

    json arr = json::array();
    for (auto& [id, o] : new_items) arr.push_back(o);
    fs::create_directories(kOut);
    const fs::path tmp = path.string() + ".tmp";
    { std::ofstream f(tmp); f << arr.dump(1); }
    fs::rename(tmp, path);  // écriture sûre : on remplace le fichier une fois complet
}

int main(int argc, char** argv) {
    std::string only;
    bool dry_run = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dry-run") dry_run = true;
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
    }
    try {
        for (auto& ep : kEndpoints)
            if (only.empty() || only == ep) sync(ep, dry_run);
    } catch (const std::exception& e) {
        std::cerr << "Erreur : " << e.what() << "\n";
        return 1;
    }
}