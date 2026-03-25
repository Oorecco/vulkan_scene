cmake_minimum_required(VERSION 3.20)
project(VkScene VERSION 0.7.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Visual Studio version note ─────────────────────────────────────────────
# VS2026 uses the "Visual Studio 17 2022" generator (same toolset, new IDE).
# If you have VS2026, cmake -G "Visual Studio 17 2022" -A x64 still works.
# Microsoft kept the version numbering sane exactly once, and this was it.

# ── Dependencies ──────────────────────────────────────────────────────────
find_package(Vulkan REQUIRED)
if(NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR "VULKAN_SDK not set. Install from lunarg.com then restart.\n"
                        "This is not optional. Vulkan does not come from vibes.")
endif()
set(VK_SDK $ENV{VULKAN_SDK})

find_library(SHADERC_LIB NAMES shaderc_combined shaderc_combined.lib
    HINTS "${VK_SDK}/Lib" "${VK_SDK}/lib" REQUIRED)

find_path(STB_INCLUDE_DIR stb_image.h
    HINTS "${CMAKE_SOURCE_DIR}/extern/stb"
          "${VK_SDK}/Include"
          "C:/vcpkg/installed/x64-windows/include")
if(NOT STB_INCLUDE_DIR)
    message(FATAL_ERROR "stb_image.h not found. See BUILD.md for options.\n"
                        "It's a single header file. The bar is literally that low.")
endif()

find_path(GLM_INCLUDE_DIR glm/glm.hpp
    HINTS "${CMAKE_SOURCE_DIR}/extern/glm"
          "${VK_SDK}/Include"
          "C:/vcpkg/installed/x64-windows/include")
if(NOT GLM_INCLUDE_DIR)
    message(FATAL_ERROR "GLM not found. See BUILD.md.\n"
                        "Math library missing. Not great for a 3D application.")
endif()

set(FMOD_DIR "" CACHE PATH "FMOD SDK root (optional — audio will fall back to MessageBeep without it)")
if(FMOD_DIR)
    find_library(FMOD_LIB NAMES fmod_vc HINTS "${FMOD_DIR}/api/core/lib/x64" REQUIRED)
    add_definitions(-DHAVE_FMOD)
    message(STATUS "FMOD enabled: ${FMOD_DIR}")
endif()

# ── Source files with VS Solution Explorer filters ─────────────────────────
# Each set gets a source_group so VS shows proper folders, not a flat soup.

set(SRC_ROOT
    src/main.cpp
    src/Game.cpp
    src/Settings.cpp
    src/AudioManager.cpp
    src/SaveSystem.cpp
    src/DevConsole.cpp
)
set(HDR_ROOT
    src/Common.h
    src/Game.h
    src/Settings.h
    src/AudioManager.h
    src/SaveSystem.h
    src/DevConsole.h
)
set(SRC_RENDERER
    src/renderer/VulkanContext.cpp
    src/renderer/VulkanBuffer.cpp
    src/renderer/VulkanImage.cpp
    src/renderer/VulkanSwapchain.cpp
    src/renderer/VulkanPipeline.cpp
    src/renderer/VulkanRenderer.cpp
    src/renderer/AssetManager.cpp
)
set(HDR_RENDERER
    src/renderer/VulkanContext.h
    src/renderer/VulkanBuffer.h
    src/renderer/VulkanImage.h
    src/renderer/VulkanSwapchain.h
    src/renderer/VulkanPipeline.h
    src/renderer/VulkanRenderer.h
    src/renderer/AssetManager.h
)
set(SRC_GAME
    src/game/Player.cpp
    src/game/PhysicsCube.cpp
    src/game/Freecam.cpp
    src/game/ItemSystem.cpp
    src/game/CrateSpawner.cpp
    src/game/CursedObjects.cpp
    src/game/NPCShop.cpp
    src/game/TitleCamera.cpp
)
set(HDR_GAME
    src/game/Player.h
    src/game/PhysicsCube.h
    src/game/Freecam.h
    src/game/ItemSystem.h
    src/game/CrateSpawner.h
    src/game/CursedObjects.h
    src/game/NPCShop.h
    src/game/TitleCamera.h
    src/game/ActiveEffects.h
    src/game/DayNight.h
)
set(SRC_SCENE
    src/scene/Scene.cpp
    src/scene/WorldDecorations.cpp
)
set(HDR_SCENE
    src/scene/Scene.h
    src/scene/WorldDecorations.h
)
set(HDR_UI
    src/ui/Font.h
    src/ui/UIBatch.h
)
# Windows resource file — gives us the app icon and version info in Explorer
set(RC_FILES
    src/resource.rc
)

# ── Target ────────────────────────────────────────────────────────────────
add_executable(VkScene WIN32
    ${SRC_ROOT}   ${HDR_ROOT}
    ${SRC_RENDERER} ${HDR_RENDERER}
    ${SRC_GAME}   ${HDR_GAME}
    ${SRC_SCENE}  ${HDR_SCENE}
    ${HDR_UI}
    ${RC_FILES}
)

# ── Solution Explorer filters ─────────────────────────────────────────────
# These make VS show actual folders instead of alphabetical chaos.
# If your Solution Explorer shows folders: you're welcome.
# If it doesn't: delete build/ and regenerate.
source_group("Source"            FILES ${SRC_ROOT} ${HDR_ROOT})
source_group("Source\\renderer"  FILES ${SRC_RENDERER} ${HDR_RENDERER})
source_group("Source\\game"      FILES ${SRC_GAME} ${HDR_GAME})
source_group("Source\\scene"     FILES ${SRC_SCENE} ${HDR_SCENE})
source_group("Source\\ui"        FILES ${HDR_UI})
source_group("Resources"         FILES ${RC_FILES})

# ── Include directories ───────────────────────────────────────────────────
target_include_directories(VkScene PRIVATE
    src
    ${Vulkan_INCLUDE_DIRS}
    ${VK_SDK}/Include
    ${GLM_INCLUDE_DIR}
    ${STB_INCLUDE_DIR}
)
if(FMOD_DIR)
    target_include_directories(VkScene PRIVATE "${FMOD_DIR}/api/core/inc")
    target_link_libraries(VkScene PRIVATE ${FMOD_LIB})
endif()

# ── Link ──────────────────────────────────────────────────────────────────
target_link_libraries(VkScene PRIVATE
    ${Vulkan_LIBRARIES}
    ${SHADERC_LIB}
    Psapi Shell32 Shlwapi
)

# ── MSVC compiler + linker options ───────────────────────────────────────
if(MSVC)
    target_compile_options(VkScene PRIVATE /W3 /MP /permissive-)
    target_compile_options(VkScene PRIVATE $<$<CONFIG:Release>:/O2 /DNDEBUG>)
    target_compile_options(VkScene PRIVATE $<$<CONFIG:Debug>:/Od /Zi>)

    # ── THE SHADERC CRT FIX ───────────────────────────────────────────────
    # shaderc_combined.lib from the Vulkan SDK ships compiled with the
    # RELEASE CRT (/MD, MD_DynamicRelease, _ITERATOR_DEBUG_LEVEL=0).
    # MSVC Debug defaults to /MDd (MDd_DynamicDebug, _ITERATOR_DEBUG_LEVEL=2).
    # Mixing them = LNK2038 "mismatch detected for RuntimeLibrary" x100+.
    #
    # Fix: force /MD in Debug too, overriding the default /MDd.
    # Both your code and shaderc then agree on the same CRT.
    # Side effect: no iterator-debug checking in Debug. Accepted trade-off —
    # nobody enjoys waiting for debug iterators when hunting Vulkan bugs anyway.
    foreach(flag CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG)
        string(REPLACE "/MDd" "/MD" ${flag} "${${flag}}")
        set(${flag} "${${flag}}" CACHE STRING "" FORCE)
    endforeach()

    # Belt AND suspenders: also suppress static-CRT default lib noise.
    target_link_options(VkScene PRIVATE
        $<$<CONFIG:Debug>:/NODEFAULTLIB:libcmtd>
        $<$<CONFIG:Release>:/NODEFAULTLIB:libcmt>
        /ignore:4099   # "PDB not found" from shaderc — acknowledged, moving on
    )
endif()

# ── Post-build: copy runtime assets ──────────────────────────────────────
add_custom_command(TARGET VkScene POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:VkScene>/shaders"
    COMMENT "Copying shaders to output directory")
add_custom_command(TARGET VkScene POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:VkScene>/assets"
    COMMENT "Copying assets to output directory")
if(FMOD_DIR)
    add_custom_command(TARGET VkScene POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FMOD_DIR}/api/core/lib/x64/fmod.dll"
            "$<TARGET_FILE_DIR:VkScene>/fmod.dll"
        COMMENT "Copying fmod.dll")
endif()

message(STATUS "VkScene v0.07-alpha | SDK=${VK_SDK} | GLM=${GLM_INCLUDE_DIR} | STB=${STB_INCLUDE_DIR}")
