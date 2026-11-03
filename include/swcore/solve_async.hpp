#pragma once

#include "swcore/solver.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace swcore {

// Instantané de progression, lisible sans bloquer depuis le thread UI.
struct SolveSnapshot {
    bool running = false;
    bool finished = false;  // un résultat est disponible (take_result)
    std::int64_t elapsed_ms = 0;
    std::uint64_t builds_found = 0;
    bool incomplete = false;
};

// Recherche lancée dans un std::jthread, annulable (std::stop_token).
// Les entrées sont copiées : l'appelant peut modifier les siennes ensuite.
// Une annulation garde les résultats partiels (marqués incomplets).
class AsyncSolver {
public:
    AsyncSolver() = default;
    ~AsyncSolver();
    AsyncSolver(const AsyncSolver&) = delete;
    AsyncSolver& operator=(const AsyncSolver&) = delete;

    // Démarre une recherche (annule et attend la précédente s'il y en a une).
    // `live` reçoit les messages du solveur depuis le thread de recherche ;
    // vide : les messages sont conservés pour take_warnings().
    void start(AccountMonster target, std::vector<Rune> pool, SetEffects effects,
               SolverConfig config, std::filesystem::path mapping_directory,
               WarningSink live = {});

    void cancel();                 // demande l'arrêt, ne bloque pas
    SolveSnapshot snapshot() const;
    std::optional<SolverReport> take_result();  // non bloquant
    std::vector<std::string> take_warnings();
    SolverReport wait();           // bloque jusqu'à la fin (CLI, tests)

private:
    SolveProgress progress_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::optional<SolverReport> result_;
    std::vector<std::string> warnings_;
    std::jthread worker_;
};

}  // namespace swcore
