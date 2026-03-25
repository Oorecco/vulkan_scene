// main.cpp — Entry point. All roads lead here.
#include "Game.h"
#include <memory>
#include <stdexcept>

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // Seed RNG
    srand((unsigned)time(nullptr));

    try {
        auto game = std::make_unique<Game>();
        if (!game->init(hInst)) {
            MessageBoxA(nullptr,
                "Initialisation failed. Check the debug output for details.",
                "VkScene: Fatal Error", MB_OK | MB_ICONERROR);
            return 1;
        }
        return game->run();
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(),
            "VkScene: Unhandled Exception", MB_OK | MB_ICONERROR);
        return 1;
    }
}
