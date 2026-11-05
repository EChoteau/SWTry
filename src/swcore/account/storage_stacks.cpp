#include "swcore/account/storage_stacks.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <string>

namespace swcore {
namespace {

using json = nlohmann::json;

void warn(const WarningSink& warnings, const std::string& message) {
    if (warnings) warnings(message);
}

template <typename T>
bool read_required(const json& object, const char* field, T& result,
                   const WarningSink& warnings) {
    const auto it = object.find(field);
    if (it == object.end()) {
        warn(warnings, std::string("StorageStacks: missing field '") + field + "'");
        return false;
    }
    try {
        result = it->get<T>();
        return true;
    } catch (const json::exception& error) {
        warn(warnings, std::string("StorageStacks: invalid field '") + field +
                          "': " + error.what());
        return false;
    }
}

}  // namespace

StorageStacks StorageStacks::load(const std::filesystem::path& path,
                                  WarningSink warnings) {
    StorageStacks storage;
    std::ifstream input(path);
    if (!input) {
        warn(warnings, "StorageStacks: unable to open '" + path.string() + "'");
        return storage;
    }

    json root;
    try {
        input >> root;
    } catch (const json::exception& error) {
        warn(warnings, std::string("StorageStacks: invalid JSON: ") + error.what());
        return storage;
    }
    if (!root.is_object()) {
        warn(warnings, "StorageStacks: root must be an object");
        return storage;
    }

    const auto list = root.find("unit_storage_list");
    if (list == root.end()) {
        warn(warnings, "StorageStacks: missing field 'unit_storage_list'");
        return storage;
    }
    if (!list->is_array()) {
        warn(warnings, "StorageStacks: 'unit_storage_list' must be an array");
        return storage;
    }

    for (const auto& value : *list) {
        if (!value.is_object()) {
            warn(warnings, "StorageStacks: ignored non-object entry");
            continue;
        }
        StoredStack stack;
        const bool valid_master =
            read_required(value, "unit_master_id", stack.unit_master_id, warnings);
        const bool valid_stars = read_required(value, "class", stack.stars, warnings);
        const bool valid_quantity =
            read_required(value, "quantity", stack.quantity, warnings);
        if (!valid_master || !valid_stars || !valid_quantity) continue;
        if (stack.unit_master_id <= 0 || stack.stars <= 0 || stack.quantity < 0) {
            warn(warnings, "StorageStacks: ignored entry with invalid value");
            continue;
        }
        storage.stacks_.push_back(stack);
    }
    return storage;
}

const std::vector<StoredStack>& StorageStacks::stacks() const noexcept {
    return stacks_;
}

std::size_t StorageStacks::totalQuantity() const noexcept {
    std::size_t total = 0;
    for (const auto& stack : stacks_) {
        total += static_cast<std::size_t>(stack.quantity);
    }
    return total;
}

std::size_t StorageStacks::distinctEntryCount() const noexcept {
    std::set<std::pair<int, int>> entries;
    for (const auto& stack : stacks_) {
        entries.emplace(stack.unit_master_id, stack.stars);
    }
    return entries.size();
}

}  // namespace swcore
