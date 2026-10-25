#pragma once

// Archipelago (port feature) [charselect agent]: the character select's short descriptions of the
// ten members who have no profile in the game (Claude and Rena keep their original bios, title
// strings 0x20-0x2d). Our own words (the user's choice 2026-10-25: "Both": the name, a facts line
// and the favourite food read from the disc, then these four lines); never copied from a manual
// or guide. English in every release. Each line must fit the window (0xe8 px less the margins:
// about 34 characters of the title font); the screen logs a line that does not fit. Seven lines in
// all: when the disc's race and occupation do not fit one line (Celine's in the French and German
// releases) they take two and the last line here is left out, so Celine's is a sentence of its own.
// Review and reword freely: nothing else depends on the wording.
namespace ss::game::member_profiles {

struct Profile {
  const char* name;      // the full name (line 0)
  const char* lines[4];  // the description
};

// By party id - 1 (1 Claude ... 12 Chisato); Claude and Rena's entries hold only the name.
inline constexpr Profile kProfiles[12] = {
    {"Claude Kenni", {}},
    {"Rena Lanford", {}},
    {"Celine Jules",
     {"A heraldic sorceress and treasure", "hunter from Cross. Proud and quick", "of tongue, she loves fine things.",
      "No old ruin escapes her notice."}},
    {"Bowman Jean",
     {"A pharmacist from Linga who mixes", "his own medicines and fights with", "his fists. Easygoing, fond of his",
      "pipe, and devoted to his wife."}},
    {"Dias Flac",
     {"A swordsman of rare skill who has", "wandered alone since bandits took", "his family. He says little, trusts",
      "few, and never lowers his guard."}},
    {"Precis Neumann",
     {"A cheerful young inventor from", "Linga who builds her own robots.", "Tireless and talkative, she is",
      "never without a gadget in hand."}},
    {"Ashton Anchors",
     {"A swordsman who fought a two-", "headed dragon and was left with", "its heads on his back. Gloomy",
      "about his luck, kind at heart."}},
    {"Leon D. S. Geeste",
     {"A Fellpool prodigy, already a", "heraldry scientist at Lacour.", "Brilliant and proud, he hides a",
      "lonely heart behind sharp words."}},
    {"Opera Vectra",
     {"A Tetragene who left her world to", "find the man she loves. Bold and", "warm, she handles her heraldic gun",
      "as surely as she trusts her heart."}},
    {"Ernest Raviede",
     {"A Tetragene archaeologist who", "travels the stars in search of", "lost civilizations. Calm, aloof,",
      "and deadly with his whip."}},
    {"Noel Chandler",
     {"A Nedian zoologist who looks after", "a nature reserve. Gentle and a", "little dreamy, he understands",
      "animals better than people."}},
    {"Chisato Madison",
     {"A Nedian newspaper reporter always", "on the trail of the next scoop.", "Energetic and nosy, she never lets",
      "a good story get away."}},
};

}  // namespace ss::game::member_profiles
