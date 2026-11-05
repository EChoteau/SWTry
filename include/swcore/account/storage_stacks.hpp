#pragma once

#include "swcore/reference_db.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace swcore {

struct StoredStack {
    int unit_master_id = 0;
    int stars = 0;
    int quantity = 0;

    friend bool operator==(const StoredStack&, const StoredStack&) = default;
};

class StorageStacks {
public:
    static StorageStacks load(const std::filesystem::path& path,
                              WarningSink warnings = {});

    const std::vector<StoredStack>& stacks() const noexcept;
    std::size_t totalQuantity() const noexcept;
    std::size_t distinctEntryCount() const noexcept;

private:
    std::vector<StoredStack> stacks_;
};

}  // namespace swcore
