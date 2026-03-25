# VkScene v0.07-alpha — Build Guide

Vulkan 1.1+ renderer, C++20, Windows x64, Visual Studio 2022/2026.

---

## Quick Start

```bat
cd vulkan_scene
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Debug
Debug\VkScene.exe
```

---

## Prerequisites

| Dependency | Source | Notes |
|---|---|---|
| **Visual Studio 2022 or 2026** | [visualstudio.microsoft.com](https://visualstudio.microsoft.com) | "Desktop development with C++" workload |
| **CMake 3.20+** | [cmake.org](https://cmake.org/download/) | Or use VS's bundled CMake |
| **Vulkan SDK** | [lunarg.com/vulkan-sdk](https://www.lunarg.com/vulkan-sdk/) | Sets `VULKAN_SDK` env var. Latest version. |
| **GLM** | `extern/glm/` or vcpkg | `glm/glm.hpp` header-only math |
| **stb_image** | `extern/stb/` or vcpkg | Single-header image loader |
| **FMOD Studio API** *(optional)* | [fmod.com/download](https://www.fmod.com/download) | Music + SFX. Silent fallback without it. |

---

## VS2026 Note

VS2026 uses the **same generator string** as VS2022:
```bat
cmake .. -G "Visual Studio 17 2022" -A x64
```
Microsoft kept the numbering stable for once.

---

## Step-by-Step

### 1 — Vulkan SDK
Install from LunarG. Open a **new** terminal and verify: `echo %VULKAN_SDK%`

### 2 — GLM (pick one)
```bat
vcpkg install glm:x64-windows
:: OR manually:
:: Drop https://github.com/g-truc/glm releases into extern\glm\
```

### 3 — stb_image (pick one)
```bat
vcpkg install stb:x64-windows
:: OR manually:
curl -o extern\stb\stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
```

### 4 — Build
```bat
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Debug
```

Release build:
```bat
cmake --build . --config Release
```

### 5 — FMOD (optional)
```bat
cmake .. -G "Visual Studio 18 2026" -A x64 ^
  -DFMOD_DIR="C:\Program Files (x86)\FMOD SoundSystem\FMOD Studio API Windows"
```
Without FMOD: music and SFX are silently disabled. The game runs fully.

---

## CMake Variables

| Variable | Default | Effect |
|---|---|---|
| `FMOD_DIR` | *(empty)* | FMOD SDK root. Leave empty for no-audio build. |

---

## Common Errors

| Error | Fix |
|---|---|
| `vulkan/vulkan.h` not found | Re-install Vulkan SDK, open **new** terminal |
| `glm/glm.hpp` not found | `extern/glm/` or vcpkg |
| `stb_image.h` not found | `extern/stb/stb_image.h` or vcpkg |
| 100+ `LNK2038` mismatch errors | shaderc CRT conflict — already fixed in CMakeLists via `/NODEFAULTLIB` + `/MD` force. Delete `build/` and regenerate. |
| App crashes on launch | Run from `build/Debug/` — `assets/` and `shaders/` must be adjacent to the exe. CMake copies them automatically post-build. |
| `VK_ERROR_INCOMPATIBLE_DRIVER` | GPU/driver too old for Vulkan 1.3. Open Settings → Vulkan Version → 1.1 |

---

## Controls

| Key | Action |
|---|---|
| WASD | Move |
| Mouse | Look |
| Space | Jump |
| L.Ctrl | Sprint |
| **E (hold)** | Charge push — swing meter fills, release to push cube |
| **E (tap)** | Interact (crate / shop / cursed object) |
| F | Grab / release cube |
| ESC | Pause menu |
| Shift+P | Toggle freecam |
| Shift+R | *(freecam)* Teleport player here |
| **Shift+F1** | General debug overlay |
| **Shift+F2** | Render stats overlay (draw counts, lighting, FOV) |
| **Shift+F3** | World data overlay (player, cube, time, NPC, effects) |
| **Shift+F4** | Developer console |
| Left click | Use held item |

All gameplay keys are rebindable via Pause → Controls.

---

## World

- **750×750m** playable area
- VOID_Y = −85m (fall below → teleport back to ground with XZ preserved)
- 5 cursed standing-stone objects hidden 200–330m from spawn
  - 3 appear at night only, 2 during day
  - Finding all 5 unlocks diamonds and triggers *IT'S NOT YOUR FAULT*
- NPC merchant wanders between locations on a 6h-stay / 2h-away schedule

---

## Asset Layout

```
assets/
  sounds/sfx/
    push_sfx.mp3       — cube push sound
    buy_sfx.mp3        — shop purchase (ka-ching)
    cursedobj_sfx.mp3  — cursed object interaction
  sounds/music/
    title_screen.mp3
    game_scene.mp3
  textures/
    ground/ground_grass.png
    skybox/skybox_*.png  (6 faces)
    props/               (bark, leaves, rock, crate, fence, mushroom, cursed_geometry, …)
    ui/items/            (9 item icons)
  app_icon.ico
```

---

## Save / Settings

| File | Location |
|---|---|
| Save | `%USERPROFILE%\Documents\VkScene\save.vks` |
| Settings | `%USERPROFILE%\Documents\VkScene\settings.xml` |

Settings are human-readable XML. Save is XOR-shuffled (not encrypted).
Use `give_money` / `give_diamond` in the dev console to cheat legitimately.

---

*Creator: Oorecco — Developer: Claude AI — claude.ai*
