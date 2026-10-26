#pragma once

#include <vector>

#include "core/types.h"
#include "platform/window.h"

// The quit prompt (port feature) [escquit]: Esc while playing no longer closes the game at
// once. "Quit Shattered Stars?" / "Unsaved progress will be lost." comes up over the paused
// picture with two choices, Keep playing (highlighted first) and Quit.
//
// - Left / Right (the arrows, or what the controls bind to the d-pad's Left / Right; a
//   gamepad's d-pad or left stick) choose; Enter, or the game's confirm as bound (Cross,
//   Start) picks.
// - Esc again, or the game's cancel as bound (Circle), closes it: back to the game. A
//   double tap of Esc never quits.
// - The window's close button and Alt+F4 still close the game at once, prompt or not.
//
// While it is up the game is paused exactly where it was: Runtime::vblank stops before the
// field ends (no VSync callback, no sound produced, no network events taken, the vblank count
// held), the sound device holds its queue, the gamepad's motors stop, and the keys pressed for
// the prompt never reach the game (platform::Window::set_modal). Runtime::ask_quit runs it.
namespace ss::runtime {

class QuitPrompt {
 public:
  enum Choice : int { kKeepPlaying = 0, kQuit = 1 };
  enum class Result { kOpen, kKeepPlaying, kQuit };

  // A key or gamepad button press: the port key it is (Esc, Enter, the arrows), and the
  // PS1 buttons it presses under the player's controls.
  Result press(platform::HostKey host, u16 bits);
  int choice() const { return choice_; }

 private:
  int choice_ = kKeepPlaying;
};

// The canvas the prompt is drawn on for a picture of 'width' x 'height' window pixels (the
// 4:3 area Window::present fills): 320 x 240 times the largest whole scale that fits (at
// least 1), so its lines are as sharp as the game's own pixels.
int quit_prompt_scale(int width, int height);

// The prompt's picture: 'frame' (the game's picture as the window shows it, frame_w x
// frame_h: any of the PS1's display sizes) scaled to the canvas (320 x 240 times 'scale'),
// dimmed, and the prompt's window over it in the style of the game's own windows, 'choice'
// highlighted.
void draw_quit_prompt(const u32* frame, int frame_w, int frame_h, int choice, int scale, std::vector<u32>& canvas);

}  // namespace ss::runtime
