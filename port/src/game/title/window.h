#pragma once

#include "core/types.h"
#include "game/common/screen.h"

// The New Game option windows (title program 0x80046f8c-0x800495xx). Each window owns
// a render context (an arena and a one-entry ordering table) that the sequence draws
// before its own; the windows are C++ objects with a vtable at +4 on the console.
namespace ss::game {

// Base class (vtable 0x8004d548).
class Window {
 public:
  explicit Window(int prims);   // 0x800493cc: arena of prims * 0x28 bytes, one-entry OT
  virtual ~Window();            // 0x80049458
  virtual void draw();          // vtable +0x0c; the base draws the frame (0x8004950c)
  virtual void set_rect(int x, int y, int w, int h);  // vtable +0x14 (0x800494d0)
  virtual void draw_contents() {}  // vtable +0x24 (0x8004ae1c, empty in every class)

  s16 x = 0, y = 0, w = 0, h = 0;  // +0x08
  u8 unknown18 = 0x0C;              // +0x18
  RenderContext* ctx = nullptr;     // +0x1c
  s32 selection = 0;                // +0x20
};

// 0x8004950c: the skinned frame (texture page 0xf, CLUT 0x7f81) around a translucent
// gradient in the configured window colours (config + 0x30..0x3c).
void window_draw_frame(Window* w);
// 0x80047228: frame of the character and keyboard windows: a grey centre and four
// edges fading out, subtracted from the background.
void window_draw_shade_frame(Window* w);

// "MEMORY CARD not inserted" and friends (vtable 0x8004d4e8, draw 0x80048c40);
// selection = notice - 1.
class MemcardNoticeWindow : public Window {
 public:
  MemcardNoticeWindow() : Window(0x80) {}
  void draw() override;
};

// Sound Select: Surround / Stereo / Monaural (ctor 0x8004711c, draw 0x8004716c).
class SoundWindow : public Window {
 public:
  SoundWindow();
  void draw() override;
};

// Vibration ON / OFF (ctor 0x80047788, draw 0x800477dc).
class VibrationWindow : public Window {
 public:
  VibrationWindow();
  void draw() override;
};

// Claude Kenni / Rena Lanford profile (ctor 0x80047690, draw 0x800476d0).
class CharacterWindow : public Window {
 public:
  CharacterWindow();
  void draw() override;
};

class NameBoxWindow;

// Name entry keyboard (ctor 0x800478e8, draw 0x80047b28). Row 0 holds Capital /
// Lower / OK / Delete / Return; rows 1.. the characters, ten per row.
class NameKeyboardWindow : public Window {
 public:
  NameKeyboardWindow();
  void draw() override;
  // selection (+0x20) is the character set: 0 capitals, 1 lower case.
  s32 column = 2;       // +0x24
  s32 row = 0;          // +0x28
  s32 next_column = 2;  // +0x2c
  s32 next_row = 0;     // +0x30
  s32 moving = 0;       // +0x34: frames left in the cursor's slide
};
void keyboard_move(NameKeyboardWindow* kb, NameBoxWindow* box, int dx, int dy);  // 0x80047948
// 0x80047f48: Cross on the keyboard; returns 1 for OK, 2 for Return, else 0.
int keyboard_confirm(NameKeyboardWindow* kb, NameBoxWindow* box);

// The name being typed (ctor 0x800480ac, draw 0x80048298); selection is the character.
class NameBoxWindow : public Window {
 public:
  explicit NameBoxWindow(int character);
  void draw() override;
  s32 length = 0;      // +0x24: characters entered
  s32 blink = 0;       // +0x28: cursor glow phase
  s32 unknown2c = 0;   // +0x2c
  char chars[8][6]{};  // +0x30: one string per character slot
};
void name_box_reset(NameBoxWindow* box);                     // 0x80048100: the default name
bool name_box_append(NameBoxWindow* box, const char* text);  // 0x800481e4: true once full
void name_box_backspace(NameBoxWindow* box);                 // 0x80048240
// 0x80048800: writes the name to 'out'; false when it is empty. Flags a changed name in
// the config (0x42 Claude, 0x43 Rena).
bool name_box_commit(NameBoxWindow* box, char* out);

// Combat mode: Standard / Semi-active / Full Active (ctor 0x800489dc, draw 0x80048a1c).
class CombatModeWindow : public Window {
 public:
  CombatModeWindow();
  void draw() override;
};

// Battle Rank: Earth / Galaxy / Universe (ctor 0x80048cec, draw 0x80048d2c).
class BattleRankWindow : public Window {
 public:
  BattleRankWindow();
  void draw() override;
};

}  // namespace ss::game
