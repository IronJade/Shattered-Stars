# escquit: Esc asks before quitting (2026-10-25)

Agent "escquit". User request: "can we add a safety to the ESC button? Right now it closes
directly the port without asking if it was intended or not."

## Rules (given)
- Headless only; never a window on the desktop (SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy).
- Build dir engine/build-escquit (configured like engine/build-hubcore), -j8.
- SS_DISC1 = the NA Disc 1 cue. Every game process under the 4 GB cap; at most 2 at once.
- A scratch XDG_CONFIG_HOME for every game run; never a flag that saves the user's config.ini.
- Scratch in scratchpad/escquit/, not /tmp. Cap output. No commits, no pushes.
- Small hunks in shared files (main.cpp, platform/window.cpp, runtime); dated notes in
  winfix's / appicon's logs when touching the same file.
- The coordinator runs the RC rerun; report when ready. Permission refusal: stop, report.
- Past ~400k tokens: update this log and end the turn.
- Enhance, never degrade: no play-state change.

## Design (coordinator's, refined here)
- Esc in the game opens a port overlay over the paused game: "Quit Shattered Stars?" /
  "Unsaved progress will be lost." / [Keep playing] [Quit], Keep playing highlighted.
- Left / Right (arrows, the keys / buttons bound to the d-pad) move; Enter or the bound
  confirm (Cross, Start) picks; Esc or the bound cancel (Circle) closes it.
- Paused = Runtime::vblank stops before the next field: no VSync callback, no SPU, no net
  poll, no vblank count; the sound device is paused (its queue kept); rumble stopped.
- Window X / Alt+F4 still close at once (also while the prompt is up).
- Keys pressed while the prompt is up never reach the game, even held after it closes.
- Esc is a port key like F11: never a game button (setup can't bind it; a hand-edited
  key_X=Escape is ignored).

## Status (2026-10-26 01:20): DONE, ready for the coordinator's RC rerun
Code (builds clean in engine/build-escquit; MinGW -fsyntax-only clean for every changed file):
- platform/window.{h,cpp}: HostKey + KeyEvent.bits/host; Esc no longer closes and is never a
  PS1 button (like F11); gamepad button events reach on_key; the left stick gives Left / Right
  while modal (hysteresis 16000 / 8000); set_modal (keys / buttons / triggers pressed while
  modal press nothing until released; rumble stopped and restored); picture_size.
- platform/audio.{h,cpp}: audio_pause (SDL_Pause/ResumeAudioStreamDevice; the queue is kept).
- runtime/quit_prompt.{h,cpp} (new, ss_core): QuitPrompt state machine, quit_prompt_scale,
  draw_quit_prompt (canvas 320x240 x the window's whole scale; the overlay's 5x7 font,
  Scale2x-smoothed, g j p q y with descenders; a game-style blue window, rim + inner line,
  highlighted button + cursor).
- runtime/runtime.{h,cpp}: present() split into screen() + frame_border(); host_events()
  (pump + SS_KEYS) before the pacing; ask_quit() modal loop; SS_KEYS test input; SS_SHOT
  writes NAME-promptN.png for each prompt frame at a listed vblank.
- runtime/debug_overlay.{h,cpp}: overlay_font_column. tools/viewer.cpp: Esc closes the viewer.
- main.cpp: rt.audio_pause (1 line) + the header comment. CMakeLists: 2 lines.
Tests: engine/tests/test_quit_prompt.cpp, 5 cases (state machine; drawing at scales 1-4 and
display sizes 320x240 / 512x240 / 640x480 / 368x480; window: Esc / keypad Enter / arrows,
modal swallowing, close button with and without the prompt; SDL virtual gamepad: d-pad,
stick, buttons swallowed, rumble stop / restore; the game headless via SS_KEYS: Esc,Esc ->
runs to the limit; Esc,Right,Enter -> quits at vblank 100, exit 0; close while up -> quits).
Proofs (scratchpad/escquit/):
- state/: Arlia walk + camp menu, 5 prompts (nav + Esc, Enter on Keep, Esc Esc, Circle,
  30 frames then Cross). A (no prompt) vs B: 7 state dumps (400 before, 401 after, ...),
  WAV, 11 raw captures, VRAM at exit, card: byte-identical; RNG 2851 draws with identical
  call sites (setarch -R). C (the pre-change binary) = A for every file and draw count.
- battle/: --battle 2:3, prompts at 300/700/1100 (mid-fight) and 1500: every battle frame's
  [bunits] line (rng index + FNV table hash + draws + unit positions / HP; 679 lines),
  battle exit dump, WAV, VRAM, captures identical to the run without prompts.
- windowed/: the player's path (offscreen window, real pacing, dummy audio device, config +
  log in scratch): W1 Esc,Right,Enter at 300 -> rc 0 after 5.4 s, log has open / quit lines;
  W2 a 2 s prompt -> 2.1 s longer than W3, both 0 audio restarts / 0 drops.
- captures/: field keep / quit, battle, German camp menu, scale renders (320x240 .. 4K).
Suites (final binary, md5 checked before and after): ss_tests 103/0, ss_game_tests 193/0 (5 mine),
smoke 282/0 (SS_DISC1 set; a first rerun without it in the environment failed every case: my mistake, not the code).
Docs: README, engine/README, docs/HANDOFF.md (SS_KEYS), release notes 0.02.50 (What's new),
wiki scratchpad/wiki/Playing.md (window keys; not committed). Notes left in appicon / winfix.

## Note from winfix (2026-10-26): my hunks in files you may touch
Uncommitted in the working tree (details docs/handoff/winfix.md):
- engine/src/main.cpp: `int main(int argc, char** argv) try {` + `util::Arguments` (3 lines) at the
  top; the `catch (const std::filesystem::filesystem_error&)` handler after main's last `return 0;`;
  `env_set`/`env_or` near the top use util::env; path arguments via util::absolute_utf8 /
  util::utf8_path; the SS_* reads (SS_DISC1/2, SS_MEMCARD1/2, SS_AP_SERVER/SLOT/GAME, SS_MODS_DIR,
  SS_SHOT, SS_VRAM_PNG, SS_AUDIO_WAV) via util::env; the "Archipelago: connecting" line names the
  client version. Nothing near the game loop or the window.
- engine/src/runtime/runtime.cpp: Runtime::log_once prints through util::log_line (one line).
- Not touched: platform/window.cpp, runtime.h.
