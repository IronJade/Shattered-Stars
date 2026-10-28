// The quit prompt (runtime/quit_prompt.h) [escquit]: Esc asks before the game closes.

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "launcher/config.h"
#include "launcher/process.h"
#include "platform/window.h"
#include "runtime/quit_prompt.h"
#include "test.h"

using namespace ss;
using platform::HostKey;
using runtime::QuitPrompt;
using Result = QuitPrompt::Result;

namespace {

Result press_all(QuitPrompt& prompt, const std::vector<std::pair<HostKey, u16>>& presses) {
  Result r = Result::kOpen;
  for (const auto& [host, bits] : presses) {
    r = prompt.press(host, bits);
    if (r != Result::kOpen) break;
  }
  return r;
}

}  // namespace

// The keys as the coordinator listed them: Esc -> prompt, Esc -> back; Esc, Right, Enter ->
// quit; Esc, Enter on Keep playing -> back. Then the bound buttons and a gamepad's.
TEST(quit_prompt_keys) {
  {
    QuitPrompt prompt;  // Esc opened it: Keep playing is highlighted
    CHECK_EQ(prompt.choice(), QuitPrompt::kKeepPlaying);
    CHECK(prompt.press(HostKey::kEscape, 0) == Result::kKeepPlaying);  // Esc again: back to the game
  }
  {
    QuitPrompt prompt;
    CHECK(prompt.press(HostKey::kRight, platform::kPadRight) == Result::kOpen);
    CHECK_EQ(prompt.choice(), QuitPrompt::kQuit);
    CHECK(prompt.press(HostKey::kEnter, platform::kPadStart) == Result::kQuit);
  }
  {
    QuitPrompt prompt;
    CHECK(prompt.press(HostKey::kEnter, platform::kPadStart) == Result::kKeepPlaying);
  }
  {
    QuitPrompt prompt;  // Quit highlighted, then Esc: still back to the game, never a quit
    CHECK(press_all(prompt, {{HostKey::kRight, 0}, {HostKey::kEscape, 0}}) == Result::kKeepPlaying);
  }
  {
    QuitPrompt prompt;  // Right, Left: Keep playing again; Right twice stays on Quit
    CHECK(press_all(prompt, {{HostKey::kRight, 0}, {HostKey::kLeft, 0}}) == Result::kOpen);
    CHECK_EQ(prompt.choice(), QuitPrompt::kKeepPlaying);
    CHECK(press_all(prompt, {{HostKey::kRight, 0}, {HostKey::kRight, 0}}) == Result::kOpen);
    CHECK_EQ(prompt.choice(), QuitPrompt::kQuit);
    CHECK(prompt.press(HostKey::kNone, platform::kPadTriangle) == Result::kOpen);  // other buttons: nothing
    CHECK(prompt.press(HostKey::kNone, platform::kPadUp) == Result::kOpen);
    CHECK_EQ(prompt.choice(), QuitPrompt::kQuit);
  }
  {
    QuitPrompt prompt;  // a gamepad: d-pad Right, then the game's confirm (Cross) as bound
    CHECK(press_all(prompt, {{HostKey::kRight, platform::kPadRight}, {HostKey::kNone, platform::kPadCross}}) ==
          Result::kQuit);
  }
  {
    QuitPrompt prompt;  // Start confirms as well; on Keep playing it resumes
    CHECK(prompt.press(HostKey::kNone, platform::kPadStart) == Result::kKeepPlaying);
  }
  {
    QuitPrompt prompt;  // the game's cancel (Circle) closes it, even with Quit highlighted
    CHECK(press_all(prompt, {{HostKey::kNone, platform::kPadRight}, {HostKey::kNone, platform::kPadCircle}}) ==
          Result::kKeepPlaying);
  }
  {
    QuitPrompt prompt;  // keys rebound to the d-pad move it (D for Right, A for Left)
    CHECK(prompt.press(HostKey::kNone, platform::kPadRight) == Result::kOpen);
    CHECK_EQ(prompt.choice(), QuitPrompt::kQuit);
    CHECK(prompt.press(HostKey::kNone, platform::kPadLeft) == Result::kOpen);
    CHECK_EQ(prompt.choice(), QuitPrompt::kKeepPlaying);
  }
  {
    QuitPrompt prompt;  // the port's keys mean what they say whatever they are bound to
    CHECK(prompt.press(HostKey::kRight, platform::kPadCross) == Result::kOpen);  // the arrow bound to Cross
    CHECK_EQ(prompt.choice(), QuitPrompt::kQuit);
    CHECK(prompt.press(HostKey::kEnter, platform::kPadCircle) == Result::kQuit);  // Enter bound to Circle
  }
}

// The canvas follows the window's whole scale; the box sits in the middle of the dimmed
// picture and the highlight moves with the choice. Any of the PS1's display sizes.
TEST(quit_prompt_draws_over_the_dimmed_picture) {
  CHECK_EQ(runtime::quit_prompt_scale(960, 720), 3);
  CHECK_EQ(runtime::quit_prompt_scale(1280, 960), 4);
  CHECK_EQ(runtime::quit_prompt_scale(1440, 1080), 4);  // 1920 x 1080 full screen
  CHECK_EQ(runtime::quit_prompt_scale(2880, 2160), 9);
  CHECK_EQ(runtime::quit_prompt_scale(300, 225), 1);
  const u32 grey = 0xFF808080u, dimmed = 0xFF404040u;
  for (const auto& [fw, fh] : std::vector<std::pair<int, int>>{{320, 240}, {512, 240}, {640, 480}, {368, 480}}) {
    const std::vector<u32> frame(static_cast<size_t>(fw) * fh, grey);
    for (int scale : {1, 2, 3, 4}) {
      std::vector<u32> keep, quit;
      runtime::draw_quit_prompt(frame.data(), fw, fh, QuitPrompt::kKeepPlaying, scale, keep);
      runtime::draw_quit_prompt(frame.data(), fw, fh, QuitPrompt::kQuit, scale, quit);
      const int cw = 320 * scale, ch = 240 * scale;
      CHECK_EQ(keep.size(), static_cast<size_t>(cw) * ch);
      CHECK_EQ(keep[0], dimmed);  // the corners: the paused picture at half brightness
      CHECK_EQ(keep.back(), dimmed);
      CHECK(keep[static_cast<size_t>(ch / 2) * cw + cw / 2] != dimmed);  // the box
      CHECK(keep[static_cast<size_t>(ch / 8) * cw + cw / 2] == dimmed);  // above it
      // The two pictures differ only where the buttons and the cursor are (the lower part).
      int first_row = ch, changed = 0;
      for (size_t i = 0; i < keep.size(); ++i)
        if (keep[i] != quit[i]) {
          ++changed;
          first_row = std::min(first_row, static_cast<int>(i / static_cast<size_t>(cw)));
        }
      CHECK(changed > 0);
      CHECK(first_row > ch / 2);
    }
  }
}

// The window: Esc closes nothing and presses no PS1 button (even bound to one), it comes to
// the game as the port's key; what is pressed while the prompt is up stays the prompt's.
TEST(window_escape_asks_and_the_prompt_keeps_its_keys) {
  using namespace ss::platform;
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);
  {
    Window window;
    std::string error;
    if (!window.create("quit prompt test", 320, 240, &error)) {
      std::printf("  (skipped: no offscreen OpenGL window here: %s)\n", error.c_str());
    } else {
      Controls controls = Controls::defaults();
      controls.keys[std::countr_zero(static_cast<unsigned>(kPadCircle))] = SDLK_ESCAPE;  // a hand-edited config.ini
      window.set_controls(controls);
      std::vector<KeyEvent> seen;
      auto pump = [&] { return window.pump([&](const KeyEvent& k) { seen.push_back(k); }); };
      auto key = [&](SDL_Keycode k, bool down) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.key = k;
        e.key.down = down;
        SDL_PushEvent(&e);
        return pump();
      };
      auto button = [&](SDL_GamepadButton b, bool down) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
        e.gbutton.button = static_cast<Uint8>(b);
        e.gbutton.down = down;
        SDL_PushEvent(&e);
        return pump();
      };
      seen.clear();
      CHECK(key(SDLK_ESCAPE, true));  // still open
      CHECK_EQ(window.pad(), 0);       // not Circle
      CHECK(seen.size() == 1 && seen[0].host == HostKey::kEscape && seen[0].down && seen[0].bits == 0);
      CHECK(key(SDLK_ESCAPE, false));
      seen.clear();
      key(SDLK_RIGHT, true);
      CHECK(seen.size() == 1 && seen[0].host == HostKey::kRight && seen[0].bits == kPadRight);
      key(SDLK_RIGHT, false);
      seen.clear();
      key(SDLK_KP_ENTER, true);
      CHECK(seen.size() == 1 && seen[0].host == HostKey::kEnter);
      key(SDLK_KP_ENTER, false);

      // Held before the prompt: keeps counting. Pressed during it: no button until released.
      key(SDLK_UP, true);
      CHECK_EQ(window.pad(), kPadUp);
      window.set_modal(true);
      key(SDLK_Z, true);  // Cross
      CHECK_EQ(window.pad(), kPadUp);
      seen.clear();
      button(SDL_GAMEPAD_BUTTON_SOUTH, true);  // a gamepad's Cross: the prompt sees it...
      CHECK(seen.size() == 1 && seen[0].key == 0 && seen[0].down && seen[0].bits == kPadCross);
      CHECK_EQ(window.pad(), kPadUp);  // ...the game doesn't
      button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
      CHECK(seen.size() == 2 && seen[1].host == HostKey::kRight);
      window.set_modal(false);
      CHECK_EQ(window.pad(), kPadUp);  // still held from the prompt: nothing
      key(SDLK_Z, false);
      button(SDL_GAMEPAD_BUTTON_SOUTH, false);
      button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false);
      CHECK_EQ(window.pad(), kPadUp);
      key(SDLK_Z, true);  // pressed again after the prompt: the game's
      CHECK_EQ(window.pad(), kPadUp | kPadCross);
      button(SDL_GAMEPAD_BUTTON_EAST, true);
      CHECK_EQ(window.pad(), kPadUp | kPadCross | kPadCircle);
      button(SDL_GAMEPAD_BUTTON_EAST, false);
      key(SDLK_Z, false);
      key(SDLK_UP, false);
      CHECK_EQ(window.pad(), 0);

      // The window's close button closes at once, with the prompt up or not.
      window.set_modal(true);
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
      CHECK(!pump());
      window.set_modal(false);
      SDL_PushEvent(&quit);
      CHECK(!pump());
    }
  }
  SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
}

// A gamepad (SDL's virtual one): its d-pad and left stick move the prompt's choice (the stick
// only while the prompt is up), its buttons stay the prompt's, and the motors stop while the
// prompt is up and get the game's levels back after it.
TEST(window_gamepad_and_rumble_with_the_prompt) {
  using namespace ss::platform;
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);
  {
    Window window;
    std::string error;
    if (!window.create("quit prompt pad test", 320, 240, &error)) {
      std::printf("  (skipped: no offscreen OpenGL window here: %s)\n", error.c_str());
      SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
      return;
    }
    std::vector<std::pair<int, int>> motors;  // (low, high) as the pad got them
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "quit prompt test pad";
    desc.userdata = &motors;
    desc.Rumble = [](void* user, Uint16 low, Uint16 high) {
      static_cast<std::vector<std::pair<int, int>>*>(user)->emplace_back(low, high);
      return true;
    };
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    SDL_Joystick* joystick = id != 0 ? SDL_OpenJoystick(id) : nullptr;
    if (joystick == nullptr) {
      std::printf("  (skipped: no virtual gamepad here: %s)\n", SDL_GetError());
    } else {
      std::vector<KeyEvent> seen;
      auto pump = [&] {
        seen.clear();
        CHECK(window.pump([&](const KeyEvent& k) { seen.push_back(k); }));
      };
      pump();  // the window opens the pad
      auto stick = [&](int x) {
        SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, static_cast<Sint16>(x));
        pump();
      };
      auto press = [&](SDL_GamepadButton b, bool down) {
        SDL_SetJoystickVirtualButton(joystick, b, down);
        pump();
      };
      window.rumble(1, 200);  // the game's levels
      CHECK(!motors.empty() && motors.back() == std::make_pair(200 * 257, 0xFFFF));
      stick(-30000);  // the game's stick, no prompt: no Left
      CHECK(seen.empty());
      stick(0);
      press(SDL_GAMEPAD_BUTTON_SOUTH, true);
      CHECK_EQ(window.pad(), kPadCross);
      press(SDL_GAMEPAD_BUTTON_SOUTH, false);

      window.set_modal(true);
      CHECK(!motors.empty() && motors.back() == std::make_pair(0, 0));  // stopped
      stick(-30000);
      CHECK(seen.size() == 1 && seen[0].host == HostKey::kLeft && seen[0].down);
      stick(-12000);  // still to the left (hysteresis): nothing more
      CHECK(seen.empty());
      stick(30000);
      CHECK(seen.size() == 1 && seen[0].host == HostKey::kRight);
      stick(0);
      press(SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
      CHECK(seen.size() == 1 && seen[0].host == HostKey::kLeft && seen[0].bits == kPadLeft);
      CHECK_EQ(window.pad(), 0);  // the prompt's, not the game's
      press(SDL_GAMEPAD_BUTTON_SOUTH, true);
      CHECK(seen.size() == 1 && seen[0].bits == kPadCross && seen[0].down);
      CHECK_EQ(window.pad(), 0);
      window.set_modal(false);
      CHECK(!motors.empty() && motors.back() == std::make_pair(200 * 257, 0xFFFF));  // the game's again
      CHECK_EQ(window.pad(), 0);  // held from the prompt
      press(SDL_GAMEPAD_BUTTON_SOUTH, false);
      press(SDL_GAMEPAD_BUTTON_DPAD_LEFT, false);
      press(SDL_GAMEPAD_BUTTON_SOUTH, true);
      CHECK_EQ(window.pad(), kPadCross);
      press(SDL_GAMEPAD_BUTTON_SOUTH, false);
      window.rumble(0, 0);
      SDL_CloseJoystick(joystick);
    }
    if (id != 0) SDL_DetachVirtualJoystick(id);
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
    }
  }
  SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
}

// The game itself, headless, with SS_KEYS pressing the keys (no window): Esc, Esc goes back
// and the run reaches its frame limit; Esc, Right, Enter quits there, exit code 0; a
// double Esc then Esc, Enter (Keep playing) never quits.
TEST(game_escape_asks_before_quitting) {
  const char* d1 = std::getenv("SS_DISC1");
  const std::filesystem::path game = launcher::sibling_program(launcher::kGameProgram);
  if (d1 == nullptr || *d1 == 0 || game.empty()) {
    std::printf("  (skipped: SS_DISC1 not set, or the game not built next to the tests)\n");
    return;
  }
  const char* base = SDL_GetBasePath();
  const std::filesystem::path home = std::filesystem::path(base ? base : ".") / "test_quit_prompt_scratch";
  std::error_code ec;
  std::filesystem::remove_all(home, ec);
  std::filesystem::create_directories(home, ec);
  auto run = [&](const std::string& keys, int* code) {
    const std::string game_utf8 = launcher::path_utf8(game), home_utf8 = launcher::path_utf8(home);
    const char* argv[] = {game_utf8.c_str(), "--headless", "--frames", "160", nullptr};
    SDL_Environment* env = SDL_CreateEnvironment(true);
    SDL_SetEnvironmentVariable(env, "XDG_CONFIG_HOME", home_utf8.c_str(), true);
    SDL_SetEnvironmentVariable(env, "APPDATA", home_utf8.c_str(), true);
    SDL_SetEnvironmentVariable(env, "SDL_VIDEO_DRIVER", "offscreen", true);
    SDL_SetEnvironmentVariable(env, "SDL_AUDIO_DRIVER", "dummy", true);
    SDL_SetEnvironmentVariable(env, "SS_KEYS", keys.c_str(), true);
    SDL_UnsetEnvironmentVariable(env, "SS_SHOT");
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(argv));
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    std::string out;
    *code = -1;
    if (process == nullptr) return out;
    size_t size = 0;
    if (void* data = SDL_ReadProcess(process, &size, code)) {
      out.assign(static_cast<const char*>(data), size);
      SDL_free(data);
    }
    SDL_DestroyProcess(process);
    return out;
  };
  auto has = [](const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; };
  int code = -1;
  std::string out = run("100:escape,escape", &code);
  CHECK_EQ(code, 0);
  CHECK(has(out, "[port] quit prompt: open at vblank 100\n"));
  CHECK(has(out, "[port] quit prompt: keep playing\n"));
  CHECK(has(out, "stopped after 160 vblanks"));
  out = run("100:escape,right,enter", &code);
  CHECK_EQ(code, 0);
  CHECK(has(out, "[port] quit prompt: quit\n"));
  CHECK(has(out, "stopped after 100 vblanks"));
  out = run("100:escape,escape,escape,enter;120:right,enter", &code);  // keys outside the prompt: nothing
  CHECK_EQ(code, 0);
  CHECK(!has(out, "[port] quit prompt: quit"));
  CHECK(has(out, "stopped after 160 vblanks"));
  out = run("100:escape,wait,wait,close", &code);  // the window's close button while it is up
  CHECK_EQ(code, 0);
  CHECK(has(out, "[port] quit prompt: quit\n"));
  CHECK(has(out, "stopped after 100 vblanks"));
  std::filesystem::remove_all(home, ec);
}
