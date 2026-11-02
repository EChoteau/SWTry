#pragma once

#include "swcore/reference_db.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace swcore {

enum class BonusSource {
    Decoration,
    WizardSkill,
    GuildSkill,
};

struct AccountBonusDefinition {
    std::string stat;
    double flat = 0.0;
    double percent = 0.0;
};

struct AccountBonus {
    BonusSource source = BonusSource::Decoration;
    int id = 0;
    int level = 0;
    int raw_value = 0;
    std::optional<AccountBonusDefinition> definition;
};

class AccountBonuses {
public:
    static AccountBonuses load(const std::filesystem::path& export_path,
                               const std::filesystem::path& mapping_path,
                               WarningSink warnings = {});

    const std::vector<AccountBonus>& values() const noexcept;

private:
    std::vector<AccountBonus> values_;
};

std::string bonus_source_name(BonusSource source);

}  // namespace swcore
