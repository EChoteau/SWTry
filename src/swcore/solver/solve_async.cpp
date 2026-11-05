#include "swcore/solver/solve_async.hpp"

#include <utility>

namespace swcore {

AsyncSolver::~AsyncSolver() {
    cancel();
    if (worker_.joinable()) worker_.join();
}

void AsyncSolver::start(AccountMonster target, std::vector<Rune> pool,
                        SetEffects effects, SolverConfig config,
                        std::filesystem::path mapping_directory, WarningSink live) {
    cancel();
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard lock(mutex_);
        result_.reset();
        warnings_.clear();
    }
    progress_.elapsed_ms.store(0);
    progress_.builds_found.store(0);
    progress_.incomplete.store(false);
    running_.store(true);
    worker_ = std::jthread(
        [this, target = std::move(target), pool = std::move(pool),
         effects = std::move(effects), config = std::move(config),
         mapping_directory = std::move(mapping_directory),
         live = std::move(live)](std::stop_token stop) {
            const WarningSink sink =
                live ? live : WarningSink{[this](const std::string& message) {
                    std::lock_guard lock(mutex_);
                    warnings_.push_back(message);
                }};
            auto report = solve_runes(target, pool, effects, config,
                                      mapping_directory, sink,
                                      SolveControl{stop, &progress_});
            for (const auto& scenario : report.scenarios)
                if (scenario.incomplete) progress_.incomplete.store(true);
            {
                std::lock_guard lock(mutex_);
                result_ = std::move(report);
            }
            running_.store(false);
        });
}

void AsyncSolver::cancel() {
    if (worker_.joinable()) worker_.request_stop();
}

SolveSnapshot AsyncSolver::snapshot() const {
    SolveSnapshot snapshot;
    snapshot.running = running_.load();
    snapshot.elapsed_ms = progress_.elapsed_ms.load();
    snapshot.builds_found = progress_.builds_found.load();
    snapshot.incomplete = progress_.incomplete.load();
    std::lock_guard lock(mutex_);
    snapshot.finished = result_.has_value();
    return snapshot;
}

std::optional<SolverReport> AsyncSolver::take_result() {
    std::lock_guard lock(mutex_);
    auto result = std::move(result_);
    result_.reset();
    return result;
}

std::vector<std::string> AsyncSolver::take_warnings() {
    std::lock_guard lock(mutex_);
    return std::exchange(warnings_, {});
}

SolverReport AsyncSolver::wait() {
    if (worker_.joinable()) worker_.join();
    auto result = take_result();
    return result ? std::move(*result) : SolverReport{};
}

}  // namespace swcore
