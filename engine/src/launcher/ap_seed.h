#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Archipelago seeds as the generator hands them out (port feature): the generator writes
// one archive per multiworld, AP_<seed>.zip, holding every player's file (for Star Ocean
// AP_<seed>_P<n>_<slot>.apso2, next to other games' patches, the spoiler log and the
// server's .archipelago file). The setup and the game take that archive as it is, or a
// loose .apso2.
//
// While a seed is chosen the game plays on that seed's own memory card (seed_memcard), never the
// player's own cards (config.ini's memcard1 / memcard2), unless a card is named for the run
// (--memcardN, SS_MEMCARDN).
//
// config.ini keys (shared by the game and the setup program):
//   ap_seed         the seed the player chose: an .apso2, or an archive holding one
//   ap_seed_entry   in an archive with several Star Ocean seeds, the one to play (its
//                   name in the archive); empty when the archive holds one
//   ap_enabled      0: setup's Archipelago switch is off; the game plays as the original and
//                   the seed and server keys are kept (default 1)
//
// A windowed run with a seed allows no debugging (main.cpp): --debug, the debug starts and
// the SS_* hooks are ignored; headless runs (the tests) keep them.
namespace ss::launcher {

// Archipelago's own folder, where its generator, Players/, worlds/ and output/ live, as
// Archipelago finds it (Utils.user_path / home_path): $XDG_DATA_HOME/Archipelago (default
// ~/.local/share/Archipelago, or the older ~/Archipelago) on Linux, %ProgramData%\Archipelago
// (the installer's default) on Windows, ~/Library/Application Support/Archipelago on macOS.
// Empty when none of them exists.
std::filesystem::path archipelago_dir();
// Its output folder (host.yaml's general_options.output_path, default "output"): where the
// generator writes AP_<seed>.zip. Empty when it doesn't exist.
std::filesystem::path archipelago_output_dir();
// The same from a given Archipelago folder (tests).
std::filesystem::path archipelago_output_dir(const std::filesystem::path& ap_dir);

// Whether 'path' names an archive (.zip) rather than a seed file.
bool is_seed_archive(const std::filesystem::path& path);

// The Star Ocean seeds (.apso2) inside an archive: their names in it, in archive order.
bool list_archive_seeds(const std::filesystem::path& zip, std::vector<std::string>* names, std::string* error);

// A seed's text. 'path' is an .apso2 ('entry' ignored) or an archive, where 'entry' picks the
// seed (its name in the archive, or just its file name); empty takes the archive's only one.
// '*chosen' (optional) gets the entry read, empty for a loose file.
bool read_seed_text(const std::filesystem::path& path, const std::string& entry, std::string* text,
                    std::string* chosen, std::string* error);

// The seed file the game loads: 'path' itself, or the seed copied out of the archive into
// 'cache' (default config_dir() / "archipelago"), rewritten only when it changed.
bool seed_file(const std::filesystem::path& path, const std::string& entry, std::filesystem::path* out,
               std::string* error, const std::filesystem::path& cache = {});

// The memory card a seed plays on: its own, so a seed's saves never mix with ordinary ones (a
// finished game can't be loaded into a seed, and a seed never writes the player's own cards):
// 'base' (default config_dir() / "archipelago") / "<seed>_<slot>" / "memcard1.mcd". The game
// creates it formatted on first use; the save sidecars sit beside it.
std::filesystem::path seed_memcard(const std::string& seed, const std::string& slot_name,
                                   const std::filesystem::path& base = {});

// Saves an earlier version made for a seed on the player's own cards (their sidecars,
// "<card>.slotNN.ap.json", name this seed and slot). Only when 'seed_card' doesn't exist yet:
// the first of 'own_cards' holding such saves is copied to 'seed_card' with every other file on
// it erased, and those saves' sidecars are copied beside it. The player's cards and sidecars are
// never changed. Returns how many saves were taken over (0: none, or the seed card exists);
// -1 and '*error' when the copy failed (the game then starts the seed on a new card).
int adopt_seed_saves(const std::filesystem::path& seed_card, const std::vector<std::filesystem::path>& own_cards,
                     const std::string& seed, const std::string& slot_name, std::string* error);

}  // namespace ss::launcher
