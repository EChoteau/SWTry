// swgui : interface graphique de l'optimiseur (Dear ImGui + GLFW + OpenGL3).
//   swgui                      fenêtre normale
//   swgui --selftest           sans fenêtre : solveur Raoq via l'API asynchrone
//   swgui --exit-after-frames N  ouvre la fenêtre, rend N images, quitte

#include "app.hpp"
#include "data_store.hpp"
#include "settings.hpp"
#include "text_util.hpp"

#include "swcore/solve_async.hpp"
#include "swcore/solver_config.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;

// Sous-système fenêtre : on se rattache à la console parente pour les modes
// en ligne de commande (sinon les sorties héritées, ex. terminal MSYS2).
void attach_console() {
#ifdef _WIN32
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
#endif
}

// Charge les données, prend la config ATQ Fatal + Blade de Raoq et lance le
// solveur via l'API asynchrone (comme le fera la page Optimiseur).
int run_selftest() {
    constexpr double kExpectedScore = 3084;
    swgui::DataStore data(swgui::find_data_root());
    const fs::path tests = data.root() / "tests" / "data";
    if (!data.load_export(tests / "Qui-Gon-21196058-2026-11-02_001212.json")) {
        std::printf("selftest: export illisible : %s\n", data.export_file.error.c_str());
        return 2;
    }
    const auto config = swcore::SolverConfig::load(tests / "solver_raoq_atk_test.json",
                                                   data.mapping_directory());
    if (!config.pool.target_unit_id) {
        std::printf("selftest: config sans monstre cible\n");
        return 2;
    }
    const auto target = std::find_if(
        data.monsters.begin(), data.monsters.end(),
        [&](const swcore::AccountMonster& monster) {
            return monster.unit_id == *config.pool.target_unit_id;
        });
    if (target == data.monsters.end()) {
        std::printf("selftest: monstre cible introuvable\n");
        return 2;
    }
    swcore::AsyncSolver solver;
    solver.start(*target, data.runes, data.effects, config, data.mapping_directory());
    while (solver.snapshot().running)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto report = solver.wait();
    double best = -1;
    for (const auto& scenario : report.scenarios)
        if (!scenario.builds.empty()) best = std::max(best, scenario.builds.front().score);
    bool ok = best == kExpectedScore;
    std::printf("selftest: score attendu %.0f, obtenu %.0f : %s\n", kExpectedScore, best,
                ok ? "OK" : "ECHEC");

    // Fiches "Monstre" rendues sans fenêtre pour chaque monstre possédé : mêmes
    // stats que le tableau, et pas de plantage sans rune ni artefact.
    swgui::Settings settings;
    swgui::App app(data, settings);
    const auto errors = app.selftest_monster_sheets();
    for (const auto& error : errors) std::printf("selftest: %s\n", error.c_str());
    std::printf("selftest: fiches de %d monstres : %s\n",
                static_cast<int>(data.monsters.size()), errors.empty() ? "OK" : "ECHEC");
    ok = ok && errors.empty();
    std::fflush(stdout);
    return ok ? 0 : 1;
}

void apply_style(float scale) {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    ImVec4* colors = style.Colors;
    const ImVec4 background{0.105f, 0.11f, 0.125f, 1.0f};
    const ImVec4 panel{0.135f, 0.14f, 0.16f, 1.0f};
    const ImVec4 accent{0.27f, 0.62f, 0.72f, 1.0f};
    const ImVec4 accent_soft{0.27f, 0.62f, 0.72f, 0.45f};
    colors[ImGuiCol_WindowBg] = background;
    colors[ImGuiCol_ChildBg] = panel;
    colors[ImGuiCol_PopupBg] = panel;
    colors[ImGuiCol_Border] = ImVec4(0.22f, 0.23f, 0.26f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.19f, 0.21f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.23f, 0.26f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.27f, 0.30f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.25f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = accent_soft;
    colors[ImGuiCol_ButtonActive] = accent;
    colors[ImGuiCol_Header] = accent_soft;
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.27f, 0.62f, 0.72f, 0.30f);
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent;
    colors[ImGuiCol_Tab] = panel;
    colors[ImGuiCol_TabHovered] = accent_soft;
    colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.30f, 0.34f, 1.0f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.17f, 0.18f, 0.20f, 1.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.025f);
    colors[ImGuiCol_SeparatorHovered] = accent_soft;
    colors[ImGuiCol_SeparatorActive] = accent;
    style.ScaleAllSizes(scale);
}

// Segoe UI (accents corrects) si présente, sinon la police par défaut d'ImGui.
void load_fonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {"C:/Windows/Fonts/segoeui.ttf"};
    for (const char* path : candidates) {
        std::error_code error;
        if (!fs::exists(path, error)) continue;
        if (io.Fonts->AddFontFromFileTTF(path, 17.0f * scale, nullptr,
                                         io.Fonts->GetGlyphRangesDefault()))
            return;
    }
    ImFontConfig config;
    config.SizePixels = 13.0f * scale;
    io.Fonts->AddFontDefault(&config);
}

int run_gui(int exit_after_frames) {
    glfwSetErrorCallback([](int code, const char* description) {
        std::fprintf(stderr, "GLFW %d : %s\n", code, description);
    });
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    swgui::DataStore data(swgui::find_data_root());
    swgui::Settings settings = swgui::load_settings(data.user_directory());
    GLFWwindow* window =
        glfwCreateWindow(std::max(640, settings.window_width),
                         std::max(480, settings.window_height),
                         "Optimiseur de runes", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    float scale_x = 1.0f;
    float scale_y = 1.0f;
    glfwGetWindowContentScale(window, &scale_x, &scale_y);
    const float scale = scale_x > 0.0f ? scale_x : 1.0f;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // rien d'autre que data/user/ n'est écrit
    io.LogFilename = nullptr;
    apply_style(scale);
    load_fonts(scale);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    swgui::App app(data, settings);
    app.load_last_files();
    glfwSetWindowUserPointer(window, &app);
    glfwSetDropCallback(window, [](GLFWwindow* source, int count, const char** paths) {
        auto* target = static_cast<swgui::App*>(glfwGetWindowUserPointer(source));
        if (target) target->on_drop(std::vector<std::string>(paths, paths + count));
    });

    int frames = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) && exit_after_frames <= 0) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app.render();
        ImGui::Render();
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.105f, 0.11f, 0.125f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        if (exit_after_frames > 0 && ++frames >= exit_after_frames)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    if (exit_after_frames <= 0) {  // le test de fumée ne touche pas aux réglages
        glfwGetWindowSize(window, &settings.window_width, &settings.window_height);
        swgui::save_settings(data.user_directory(), settings);
        app.save_state();
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

}  // namespace

int app_main(int argc, char** argv) {
    int exit_after_frames = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--selftest") {
            attach_console();
            return run_selftest();
        }
        if (argument == "--exit-after-frames" && i + 1 < argc)
            exit_after_frames = std::max(1, std::atoi(argv[++i]));
    }
    return run_gui(exit_after_frames);
}

#ifdef _WIN32
// Point d'entrée du sous-système fenêtre (MinGW) : délègue à main.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return app_main(__argc, __argv); }
#endif

int main(int argc, char** argv) { return app_main(argc, argv); }
