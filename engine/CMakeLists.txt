cmake_minimum_required(VERSION 3.24)
# The port's own release version (docs/ap/release_checklist.md section 1), two-digit minor
# and patch fields so either can go past 9 without ambiguity. CMake keeps the literal string
# here in PROJECT_VERSION (checked: "0.01.00", not normalized to "0.1.0"), so this one line is
# the single source: it reaches the code as SS_VERSION (below) and the release scripts read
# it back out of this file (tools/build_linux.sh, tools/build_windows.sh).
project(ShatteredStars LANGUAGES C CXX VERSION 0.02.00)

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

# MinGW (the Windows build): the C / C++ runtimes are linked in, so the game ships as
# shattered-stars.exe plus SDL3.dll (copied next to each program after the build).
if(MINGW)
  add_link_options(-static-libgcc -static-libstdc++
                   -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
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
  src/runtime/debug_overlay.cpp
  src/runtime/libgpu.cpp
  src/runtime/libgte.cpp
  src/runtime/libspu.cpp
  src/runtime/libapi.cpp
  src/runtime/boot_exe.cpp
  src/runtime/region.cpp
  src/game/common/text_rules.cpp
  src/runtime/libcard.cpp
  src/util/png.cpp)
target_include_directories(ss_core PUBLIC src)
target_compile_options(ss_core PRIVATE ${SS_WARNINGS})
# SS_VERSION: "0.02.00" (PROJECT_VERSION above), a plain string literal everywhere that
# links ss_core (the game, setup, both test binaries): --help / --version, setup's header.
target_compile_definitions(ss_core PUBLIC SS_VERSION="${PROJECT_VERSION}")

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
  src/game/common/save_icon.cpp
  src/game/title/load_screen.cpp
  src/game/field/menu_camp.cpp
  src/game/field/menu_voice.cpp
  src/game/field/menu_shop.cpp
  src/game/field/item_effect.cpp
  src/game/field/menu_items.cpp
  src/game/field/menu_equip.cpp
  src/game/field/menu_status.cpp
  src/game/field/menu_skills.cpp
  src/game/field/menu_specialty.cpp
  src/game/field/menu_options.cpp
  src/game/field/menu_combat.cpp
  src/game/field/menu_creation.cpp
  src/game/field/menu_picture.cpp
  src/game/field/menu_rename.cpp
  src/game/field/event_sweep.cpp
  src/game/common/menu_viewer.cpp
  src/game/title/intro.cpp
  src/game/title/movie.cpp
  src/game/credits/credits_program.cpp
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
  src/game/field/field_section6.cpp
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
  src/game/field/leader.cpp
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
  src/game/battle/stage_objects.cpp
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
  src/game/battle/party_talk.cpp
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
  src/game/battle/pc_casters.cpp
  src/game/battle/pc_claude.cpp
  src/game/battle/party_ai.cpp
  src/game/battle/pc_bowman.cpp
  src/game/battle/pc_dias.cpp
  src/game/battle/pc_ernest.cpp
  src/game/battle/pc_chisato.cpp
  src/game/battle/pc_module_util.cpp
  src/game/battle/pc_precis.cpp
  src/game/battle/pc_opera.cpp
  src/game/battle/pc_ashton.cpp
  src/game/battle/enemy_support.cpp
  src/game/battle/enemy_shots.cpp
  src/game/battle/bload.cpp
  src/game/battle/voice_player.cpp
  src/game/battle/party_voice.cpp
  src/game/battle/enemy_families.cpp
  src/game/battle/enemy_modules.cpp
  src/game/battle/enemy_mod_guardian.cpp
  src/game/battle/enemy_mod_carlaeagle.cpp
  src/game/battle/enemy_mod_ericodus.cpp
  src/game/battle/enemy_mod_gelatin.cpp
  src/game/battle/enemy_mod_marsilio.cpp
  src/game/battle/enemy_mod_slime.cpp
  src/game/battle/enemy_mod_visseyer.cpp
  src/game/battle/enemy_mod_xine.cpp
  src/game/battle/enemy_mod_arcmene.cpp
  src/game/battle/enemy_mod_hounddog.cpp
  src/game/battle/enemy_mod_salamander.cpp
  src/game/battle/enemy_mod_kitty.cpp
  src/game/battle/enemy_mod_ricki.cpp
  src/game/battle/enemy_mod_ogre.cpp
  src/game/battle/enemy_mod_snowman.cpp
  src/game/battle/enemy_mod_bunny.cpp
  src/game/battle/enemy_mod_gargoyle.cpp
  src/game/battle/enemy_mod_stingray.cpp
  src/game/battle/enemy_mod_gerel.cpp
  src/game/battle/enemy_mod_controller.cpp
  src/game/battle/enemy_mod_niquia.cpp
  src/game/battle/enemy_mod_azamgil.cpp
  src/game/battle/enemy_mod_guardbox.cpp
  src/game/battle/enemy_mod_lover.cpp
  src/game/battle/enemy_mod_miel.cpp
  src/game/battle/enemy_mod_shin.cpp
  src/game/battle/enemy_mod_magichand.cpp
  src/game/battle/enemy_mod_succubus.cpp
  src/game/battle/enemy_mod_ghost.cpp
  src/game/battle/enemy_mod_berle.cpp
  src/game/battle/enemy_mod_cyril.cpp
  src/game/battle/enemy_mod_dias.cpp
  src/game/battle/enemy_mod_zand.cpp
  src/game/battle/enemy_mod_bark.cpp
  src/game/battle/enemy_mod_synard.cpp
  src/game/battle/enemy_mod_celesta.cpp
  src/game/battle/enemy_mod_shigeo.cpp
  src/game/battle/enemy_mod_ruprecht.cpp
  src/game/battle/enemy_mod_vesper.cpp
  src/game/battle/enemy_mod_decus.cpp
  src/game/battle/enemy_mod_nicolus.cpp
  src/game/battle/enemy_mod_jibril.cpp
  src/game/battle/enemy_mod_indalecio.cpp
  src/game/battle/spells.cpp
  src/game/battle/spell_shots.cpp
  src/game/battle/spell_units.cpp
  src/game/battle/spell_fx.cpp
  src/game/battle/spell_windows.cpp
  src/game/battle/spellcast.cpp
  src/game/battle/spell_modules.cpp
  src/game/battle/command_menu.cpp
  src/game/battle/items.cpp
  src/game/ap/json.cpp
  src/game/ap/seed.cpp
  src/game/ap/ap.cpp
  src/game/ap/hub.cpp
  src/game/battle/shatter.cpp
  src/game/battle/spell_mod_deep_freeze.cpp
  src/game/battle/spell_mod_noah.cpp
  src/game/battle/spell_mod_mind_blast.cpp
  src/game/battle/spell_mod_spicule.cpp
  src/game/battle/spell_mod_wind_of_destruction.cpp
  src/game/battle/spell_mod_star_flare.cpp
  src/game/battle/spell_mod_daemons_gate.cpp
  src/game/battle/spell_mod_foehn.cpp
  src/game/battle/spell_mod_starlight.cpp
  src/game/battle/spell_mod_magnum_tornado.cpp
  src/game/battle/spell_mod_cure_all.cpp
  src/game/battle/spell_mod_black_saber.cpp
  src/game/battle/spell_mod_earthquake.cpp
  src/game/battle/spell_mod_explode.cpp
  src/game/battle/spell_mod_fairy_light.cpp
  src/game/battle/spell_mod_angel_feather.cpp
  src/game/battle/spell_mod_dark_circle.cpp
  src/game/battle/spell_mod_energy_arrow.cpp
  src/game/battle/spell_mod_dispel.cpp
  src/game/battle/spell_mod_earth_grave.cpp
  src/game/battle/spell_mod_tetanus_wind.cpp
  src/game/battle/spell_mod_growth.cpp
  src/game/battle/spell_mod_shadow_bolt.cpp
  src/game/battle/spell_mod_protection.cpp
  src/game/battle/spell_mod_silence.cpp
  src/game/battle/spell_mod_divine_comedy.cpp
  src/game/battle/spell_mod_gremlin_lair.cpp
  src/game/battle/spell_mod_tractor_beam.cpp
  src/game/battle/spell_mod_anti.cpp
  src/game/battle/spell_mod_raise_dead.cpp
  src/game/battle/spell_mod_lunar_light.cpp
  src/game/battle/spell_mod_extinction.cpp
  src/game/battle/spell_mod_press.cpp
  src/game/battle/spell_mod_energy_net.cpp
  src/game/battle/spell_mod_neutral.cpp
  src/game/battle/spell_mod_deep_mist.cpp
  src/game/battle/spell_mod_bless.cpp
  src/game/battle/spell_mod_forget.cpp
  src/game/battle/spell_mod_sonic_saber.cpp
  src/game/battle/spell_mod_meteor_swarm.cpp
  src/game/battle/spell_mod_eruption.cpp
  src/game/battle/spell_mod_southern_cross.cpp
  src/game/battle/spell_mod_gravity_press.cpp
  src/game/battle/spell_mod_blood_sucker.cpp
  src/game/battle/spell_mod_fairy_heal.cpp
  src/game/battle/spell_mod_shadow_flare.cpp
  src/game/battle/spell_mod_reflection.cpp
  src/game/battle/spell_mod_delay.cpp
  src/game/battle/spell_mod_thunderstorm.cpp
  src/game/battle/spell_mod_haste.cpp
  src/game/battle/spell_mod_light_cross.cpp
  src/game/battle/spell_mod_spell40.cpp
  src/game/battle/spell_mod_acid_rain.cpp
  src/game/battle/spell_mod_curse.cpp
  src/game/battle/spell_mod_thundercloud.cpp)
target_include_directories(ss_game PUBLIC src)
target_link_libraries(ss_game PUBLIC ss_core ss_platform ss_mods)
target_compile_options(ss_game PRIVATE ${SS_WARNINGS})
target_include_directories(ss_platform PUBLIC src)
target_link_libraries(ss_platform PUBLIC SDL3::SDL3)
target_compile_options(ss_platform PRIVATE ${SS_WARNINGS})

# The launcher library (port feature): config.ini, finding and identifying the player's
# discs, and starting one program from the other. The game and the setup program share it.
add_library(ss_launcher STATIC
  src/launcher/ap_seed.cpp
  src/launcher/config.cpp
  src/launcher/discs.cpp
  src/launcher/process.cpp
  src/launcher/report.cpp
  src/launcher/settings.cpp
  src/launcher/title_art.cpp)
target_include_directories(ss_launcher PUBLIC src)
# It uses ss_core's disc reader, but doesn't declare it: that would move ss_core after
# ss_platform on link lines, and ss_core's runtime needs the platform window. Programs
# list ss_launcher before ss_core (ss_game brings ss_core).
target_link_libraries(ss_launcher PUBLIC SDL3::SDL3 ss_mods)  # ss_mods: the .zip reader (launcher/ap_seed.h)
target_compile_options(ss_launcher PRIVATE ${SS_WARNINGS})

# Mods (port feature; docs/notes/mods.md): the patch-line grammar, mod packages and their
# load order, conflict analysis, and installing packages from .zip archives. Used by the
# game (game/field/mods.cpp) and the setup program; no dependencies beyond the standard
# library.
add_library(ss_mods STATIC
  src/mods/catalog.cpp
  src/mods/patch.cpp
  src/mods/zip.cpp)
target_include_directories(ss_mods PUBLIC src)
target_compile_options(ss_mods PRIVATE ${SS_WARNINGS})

# WIN32: a GUI program on Windows (no console window when double-clicked); it attaches
# to the terminal it was started from (launcher/report.h). No effect elsewhere.
add_executable(shattered-stars WIN32 src/main.cpp)
target_link_libraries(shattered-stars PRIVATE ss_game ss_launcher)
target_compile_options(shattered-stars PRIVATE ${SS_WARNINGS})
if(WIN32)
  add_custom_command(TARGET shattered-stars POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:shattered-stars>
            $<TARGET_FILE_DIR:shattered-stars>
    COMMAND_EXPAND_LISTS)
endif()

# The setup program (port feature): the players' settings window (discs, display, sound,
# memory cards, mods), built on Dear ImGui 1.92.9b (MIT licence), fetched and pinned by
# SHA-256 like SDL. shattered-stars-setup; setup.exe on Windows, a GUI program (no console).
option(SS_BUILD_SETUP "Build the setup program (fetches Dear ImGui)" ON)
if(SS_BUILD_SETUP)
  include(FetchContent)
  FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
    URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(imgui)
  add_library(ss_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
  target_include_directories(ss_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends
                                              ${imgui_SOURCE_DIR}/misc/cpp)
  target_compile_definitions(ss_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
  target_link_libraries(ss_imgui PUBLIC SDL3::SDL3 ${CMAKE_DL_LIBS})

  # game/ap/seed.cpp + json.cpp: the game's own seed reader, for the Archipelago page (self-contained).
  add_executable(shattered-stars-setup WIN32 src/setup/setup_main.cpp src/setup/setup_ui.cpp
                 src/game/ap/seed.cpp src/game/ap/json.cpp)
  if(WIN32)
    set_target_properties(shattered-stars-setup PROPERTIES OUTPUT_NAME setup)
  endif()
  target_link_libraries(shattered-stars-setup PRIVATE ss_launcher ss_mods ss_core ss_platform ss_imgui)
  target_compile_options(shattered-stars-setup PRIVATE ${SS_WARNINGS})
  if(WIN32)
    add_custom_command(TARGET shattered-stars-setup POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:shattered-stars-setup>
              $<TARGET_FILE_DIR:shattered-stars-setup>
      COMMAND_EXPAND_LISTS)
  endif()
endif()

# The Archipelago network client (port feature; src/ap_net/client.h): apclientpp and its
# header-only dependencies, fetched and pinned by SHA-256 like Dear ImGui. Compiled as
# C++17 (websocketpp does not build as C++20). OpenSSL gives wss://, zlib compression;
# without them (MinGW here: install mingw64-openssl mingw64-zlib) it speaks ws:// only.
# game/ap uses only the plain interface (ap_net/link.h); the game program links this.
option(SS_BUILD_AP_NET "Build the Archipelago network client (fetches apclientpp)" ON)
if(SS_BUILD_AP_NET)
  include(FetchContent)
  # SOURCE_SUBDIR none: header-only, nothing to add_subdirectory.
  FetchContent_Declare(apclientpp
    URL https://github.com/black-sliver/apclientpp/archive/refs/tags/v0.6.6.tar.gz
    URL_HASH SHA256=bf5390051c078c9ed43a2e135a315e589ca6e2427b4623a2ad391ae21b5ba1c0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR none)
  FetchContent_Declare(wswrap
    URL https://github.com/black-sliver/wswrap/archive/aeba7ac428028723fb26ce92488f260660f786b1.tar.gz
    URL_HASH SHA256=3276e3fb143e38189c5bf5eee2b43a34e821c3fa8f7323da965056e73856e2cf
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR none)
  FetchContent_Declare(websocketpp
    URL https://github.com/zaphoyd/websocketpp/archive/refs/tags/0.8.2.tar.gz
    URL_HASH SHA256=6ce889d85ecdc2d8fa07408d6787e7352510750daa66b5ad44aacb47bea76755
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR none)
  FetchContent_Declare(asio
    URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-28-2.tar.gz
    URL_HASH SHA256=5705a0e403017eba276625107160498518838064a6dd7fd8b00b2e30c0ffbdee
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR none)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
    URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR none)
  FetchContent_MakeAvailable(apclientpp wswrap websocketpp asio nlohmann_json)
  add_library(ss_ap_net STATIC src/ap_net/client.cpp)
  set_target_properties(ss_ap_net PROPERTIES CXX_STANDARD 17)
  target_include_directories(ss_ap_net PUBLIC src)
  target_include_directories(ss_ap_net SYSTEM PRIVATE ${apclientpp_SOURCE_DIR} ${wswrap_SOURCE_DIR}/include
                             ${websocketpp_SOURCE_DIR} ${asio_SOURCE_DIR}/asio/include ${nlohmann_json_SOURCE_DIR}/include)
  target_compile_definitions(ss_ap_net PRIVATE ASIO_STANDALONE AP_NO_SCHEMA AP_NO_DEFAULT_DATA_PACKAGE_STORE
                             _WEBSOCKETPP_CPP11_STL_ _WEBSOCKETPP_CPP11_THREAD_)
  find_package(Threads REQUIRED)
  target_link_libraries(ss_ap_net PRIVATE Threads::Threads)
  find_package(OpenSSL QUIET)
  if(OpenSSL_FOUND)
    target_link_libraries(ss_ap_net PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    # Static OpenSSL 3's default provider (provider_core.c) calls PathCchRemoveFileSpec to
    # find its module directory relative to the DLL/exe at run time; on MinGW that symbol
    # lives in a separate import library, not pulled in automatically.
    if(WIN32)
      target_link_libraries(ss_ap_net PRIVATE pathcch)
    endif()
  else()
    message(STATUS "OpenSSL not found: the Archipelago client speaks ws:// only")
    target_compile_definitions(ss_ap_net PRIVATE WSWRAP_NO_SSL)
  endif()
  find_package(ZLIB QUIET)
  if(ZLIB_FOUND)
    target_link_libraries(ss_ap_net PRIVATE ZLIB::ZLIB)
  else()
    target_compile_definitions(ss_ap_net PRIVATE WSWRAP_NO_COMPRESSION)
  endif()
  if(WIN32)
    target_compile_definitions(ss_ap_net PRIVATE _WIN32_WINNT=0x0601)
    target_link_libraries(ss_ap_net PRIVATE ws2_32 mswsock crypt32)
  endif()
  # The game builds the Client (main.cpp) and hands it to game/ap.
  target_link_libraries(shattered-stars PRIVATE ss_ap_net)
  target_compile_definitions(shattered-stars PRIVATE SS_HAVE_AP_NET)
endif()

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
  tests/test_game.cpp
  tests/test_region.cpp
  tests/test_text_rules.cpp)
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
target_sources(ss_game_tests PRIVATE tests/test_save.cpp)
target_sources(ss_game_tests PRIVATE tests/test_launcher.cpp)
target_sources(ss_game_tests PRIVATE tests/test_mods.cpp)
target_sources(ss_game_tests PRIVATE tests/test_text_rules_game.cpp)
if(TARGET ss_ap_net)  # against a local Archipelago server (SS_AP_SERVER), skipped without one
  target_sources(ss_game_tests PRIVATE tests/test_ap_net.cpp)
  target_link_libraries(ss_game_tests PRIVATE ss_ap_net)
endif()
target_sources(ss_game_tests PRIVATE tests/test_ap.cpp)
target_sources(ss_game_tests PRIVATE tests/test_leader.cpp)
target_link_libraries(ss_game_tests PRIVATE ss_game ss_launcher)
target_compile_options(ss_game_tests PRIVATE ${SS_WARNINGS})
add_test(NAME ss_game_tests COMMAND ss_game_tests)

# Dumps decoded STR movie frames for comparison with jPSXdec.
add_executable(mdec_probe tests/mdec_probe.cpp)
target_link_libraries(mdec_probe PRIVATE ss_core)
target_compile_options(mdec_probe PRIVATE ${SS_WARNINGS})
