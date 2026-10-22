cmake_minimum_required(VERSION 3.24)
project(ShatteredStars LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# SDL3: use an installed copy when there is one, otherwise build the pinned release.
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
  message(STATUS "SDL3 not installed; building SDL 3.4.18 from source")
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  # Optional X11 extension whose headers are often missing; SDL works without it.
  set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(SDL3)
endif()

if(MSVC)
  set(SS_WARNINGS /W4 /permissive-)
else()
  set(SS_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
endif()

# Everything except the platform layer, so tests can link it without a window.
add_library(ss_core STATIC
  src/disc/disc_image.cpp
  src/disc/archive.cpp
  src/disc/slz.cpp
  src/disc/str.cpp
  src/psx/gpu.cpp
  src/psx/gte.cpp
  src/psx/mdec.cpp
  src/psx/spu.cpp
  src/psx/memory_card.cpp
  src/game/common/texture.cpp
  src/game/common/disc_screen.cpp
  src/game/common/math.cpp
  src/game/boot/loader.cpp
  src/game/boot/pad.cpp
  src/game/boot/rng.cpp
  src/game/boot/sound.cpp
  src/game/boot/stubs.cpp
  src/runtime/runtime.cpp
  src/runtime/libgpu.cpp
  src/runtime/libgte.cpp
  src/runtime/libspu.cpp
  src/runtime/libapi.cpp
  src/runtime/boot_exe.cpp
  src/runtime/libcard.cpp
  src/util/png.cpp)
target_include_directories(ss_core PUBLIC src)
target_compile_options(ss_core PRIVATE ${SS_WARNINGS})

add_library(ss_platform STATIC
  src/platform/gl.cpp
  src/platform/audio.cpp
  src/platform/window.cpp)

# Ported game programs; they present through the platform window.
add_library(ss_game STATIC
  src/game/boot/system.cpp
  src/game/common/program_image.cpp
  src/game/common/memcard.cpp
  src/game/common/screen.cpp
  src/game/common/party.cpp
  src/game/common/menu_core.cpp
  src/game/common/menu_scene.cpp
  src/game/common/menu_gfx.cpp
  src/game/common/menu_widget.cpp
  src/game/common/menu_stats.cpp
  src/game/common/menu_save.cpp
  src/game/title/load_screen.cpp
  src/game/field/menu_camp.cpp
  src/game/field/menu_shop.cpp
  src/game/field/item_effect.cpp
  src/game/field/menu_items.cpp
  src/game/field/menu_equip.cpp
  src/game/field/menu_status.cpp
  src/game/field/menu_skills.cpp
  src/game/field/menu_options.cpp
  src/game/common/menu_viewer.cpp
  src/game/title/intro.cpp
  src/game/title/movie.cpp
  src/game/title/title_program.cpp
  src/game/title/title_screen.cpp
  src/game/title/voice_collection.cpp
  src/game/title/load_queue.cpp
  src/game/title/new_game.cpp
  src/game/title/text.cpp
  src/game/title/window.cpp
  src/game/field/actors.cpp
  src/game/field/afterimage.cpp
  src/game/field/actor_base.cpp
  src/game/field/actor_field.cpp
  src/game/field/sprite_pack.cpp
  src/game/field/sprite_render.cpp
  src/game/field/collision.cpp
  src/game/field/field_input.cpp
  src/game/field/callways.cpp
  src/game/field/camera.cpp
  src/game/field/effects.cpp
  src/game/field/effects_draw.cpp
  src/game/field/effects_camera.cpp
  src/game/field/scene_renderer.cpp
  src/game/field/field_movie.cpp
  src/game/field/field_sub_overlays.cpp
  src/game/field/bg.cpp
  src/game/field/field_program.cpp
  src/game/field/map.cpp
  src/game/field/screen.cpp
  src/game/field/tasks.cpp
  src/game/field/script_vm.cpp
  src/game/field/script_ext.cpp
  src/game/field/script_table_a.cpp
  src/game/field/script_table_b.cpp
  src/game/field/script_triggers.cpp
  src/game/field/script_party.cpp
  src/game/field/script_gte.cpp
  src/game/field/vm_services.cpp
  src/game/field/message.cpp
  src/game/field/message_text.cpp
  src/game/field/message_draw.cpp
  src/game/field/fade.cpp
  src/game/field/notice.cpp
  src/game/field/debug_console.cpp
  src/game/field/mods.cpp
  src/game/field/post_battle.cpp
  src/game/field/voice_queue.cpp
  src/game/field/text_panel.cpp
  src/game/field/load_queue.cpp
  src/game/field/music.cpp
  src/game/field/stash.cpp
  src/game/field/encounter.cpp
  src/game/field/worldmap_bg.cpp
  src/game/field/worldmap_camera.cpp
  src/game/field/worldmap_render.cpp
  src/game/field/worldmap_actor.cpp
  src/game/field/worldmap_vehicle.cpp
  src/game/field/field_world.cpp
  src/game/field/characters.cpp
  src/game/field/special.cpp
  src/tools/viewer.cpp)
# The battle program (#2129), native (docs/notes/battle.md).
target_sources(ss_game PRIVATE
  src/game/battle/battle_program.cpp
  src/game/battle/battle_debug.cpp
  src/game/battle/loaders.cpp
  src/game/battle/stage.cpp
  src/game/battle/render.cpp
  src/game/battle/camera.cpp
  src/game/battle/bobj.cpp
  src/game/battle/unit.cpp
  src/game/battle/encounter.cpp
  src/game/battle/party.cpp
  src/game/battle/vram.cpp
  src/game/battle/sprite.cpp
  src/game/battle/unit_move.cpp
  src/game/battle/combat.cpp
  src/game/battle/bsfx.cpp
  src/game/battle/party_states.cpp
  src/game/battle/enemy_ai.cpp
  src/game/battle/input.cpp
  src/game/battle/director.cpp
  src/game/battle/window.cpp
  src/game/battle/hud.cpp
  src/game/battle/result.cpp
  src/game/battle/voice.cpp
  src/game/battle/shot.cpp
  src/game/battle/fx_draw.cpp
  src/game/battle/killer.cpp
  src/game/battle/particle.cpp
  src/game/battle/pc_modules.cpp
  src/game/battle/pc_claude.cpp
  src/game/battle/enemy_support.cpp
  src/game/battle/enemy_families.cpp
  src/game/battle/enemy_modules.cpp
  src/game/battle/enemy_mod_guardian.cpp)
target_include_directories(ss_game PUBLIC src)
target_link_libraries(ss_game PUBLIC ss_core ss_platform)
target_compile_options(ss_game PRIVATE ${SS_WARNINGS})
target_include_directories(ss_platform PUBLIC src)
target_link_libraries(ss_platform PUBLIC SDL3::SDL3)
target_compile_options(ss_platform PRIVATE ${SS_WARNINGS})

add_executable(shattered-stars src/main.cpp)
target_link_libraries(shattered-stars PRIVATE ss_game)
target_compile_options(shattered-stars PRIVATE ${SS_WARNINGS})

enable_testing()
add_executable(ss_tests
  tests/test_main.cpp
  tests/test_slz.cpp
  tests/test_gpu.cpp
  tests/test_gte.cpp
  tests/test_disc.cpp
  tests/test_mdec.cpp
  tests/test_memcard.cpp
  tests/test_spu.cpp
  tests/test_sound.cpp
  tests/test_game.cpp)
target_link_libraries(ss_tests PRIVATE ss_core)
target_compile_options(ss_tests PRIVATE ${SS_WARNINGS})
add_test(NAME ss_tests COMMAND ss_tests)

# Tests of ported game code. They link the game programs, and with them SDL, but
# never open a window.
add_executable(ss_game_tests
  tests/test_main.cpp
  tests/test_movie.cpp
  tests/test_script_vm.cpp
  tests/test_field_message.cpp
  tests/test_menu.cpp
  tests/test_field_actors.cpp
  tests/test_field_party.cpp)
target_sources(ss_game_tests PRIVATE tests/test_battle.cpp)
target_sources(ss_game_tests PRIVATE tests/test_field_effects.cpp)
target_sources(ss_game_tests PRIVATE tests/test_worldmap.cpp)
target_sources(ss_game_tests PRIVATE tests/test_pad.cpp)
target_link_libraries(ss_game_tests PRIVATE ss_game)
target_compile_options(ss_game_tests PRIVATE ${SS_WARNINGS})
add_test(NAME ss_game_tests COMMAND ss_game_tests)

# Dumps decoded STR movie frames for comparison with jPSXdec.
add_executable(mdec_probe tests/mdec_probe.cpp)
target_link_libraries(mdec_probe PRIVATE ss_core)
target_compile_options(mdec_probe PRIVATE ${SS_WARNINGS})
