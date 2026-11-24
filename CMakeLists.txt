cmake_minimum_required(VERSION 3.15)

# Realistic NPCs fuer HITMAN World of Assassination (Plugin fuer das ZHMModSDK):
# Wachen warnen und lassen mit sich reden, bevor sie schiessen. Angeschossene gehen zu Boden, zucken und verbluten.

# ---------------------------------------------------------------------------------------------------------------------
# Kern: reine Logik ohne Spiel-Abhaengigkeiten – wird auch von den Tests genutzt.
# ---------------------------------------------------------------------------------------------------------------------
file(GLOB_RECURSE RN_CORE_SOURCES CONFIGURE_DEPENDS
        Src/Core/*.cpp
        Src/Core/*.h
)

add_library(RealisticNPCsCore STATIC ${RN_CORE_SOURCES})

target_include_directories(RealisticNPCsCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core
)

target_compile_options(RealisticNPCsCore PRIVATE /utf-8)

# ---------------------------------------------------------------------------------------------------------------------
# Die Mod selbst (DLL fuer HITMAN 3\Retail\mods).
# ---------------------------------------------------------------------------------------------------------------------
file(GLOB_RECURSE RN_PLUGIN_SOURCES CONFIGURE_DEPENDS
        Src/Plugin/*.cpp
        Src/Plugin/*.h
)

add_library(RealisticNPCs SHARED ${RN_PLUGIN_SOURCES})

target_include_directories(RealisticNPCs PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Src/Plugin
)

target_link_libraries(RealisticNPCs PRIVATE
        ZHMModSDK
        RealisticNPCsCore
)

target_compile_options(RealisticNPCs PRIVATE /utf-8)

# ---------------------------------------------------------------------------------------------------------------------
# Tests fuer den Kern (laufen ohne Spiel): _build\x64-Release\Mods\RealisticNPCs\RealisticNPCsTests.exe
# ---------------------------------------------------------------------------------------------------------------------
add_executable(RealisticNPCsTests Tests/CoreTests.cpp)
target_link_libraries(RealisticNPCsTests PRIVATE RealisticNPCsCore)
target_compile_options(RealisticNPCsTests PRIVATE /utf-8)
