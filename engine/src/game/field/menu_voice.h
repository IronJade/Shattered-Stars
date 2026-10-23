#pragma once

#include "core/types.h"
#include "game/common/menu_core.h"

// The field's part of the menus' background loader (field 0x80038bcc / 0x80038c70 /
// 0x80039418 / 0x80038dfc / 0x80038e44). While a menu runs over the field, the field's voice
// streaming (VoiceStream, load_queue.h) goes on whenever the menu has nothing of its own to
// read: the menu copies the streaming state in (bgload_init), steps it once a frame after
// 60 idle frames (bgload_update) and writes it back when it closes (bgload_shutdown). A
// killer-move or party change in a menu reloads that member's voice bank
// (voice_reload_member, the g_voice_reload_member hook of common/party.h).
namespace ss::game::field {

// The hooks FieldMenuHost installs for the menu's lifetime.
const menu::BgLoadHooks& menu_voice_hooks();

// 0x80038dfc: the voice queue stops, party slot 'slot's bank must be loaded again, and the
// menu's streaming starts over (state 0) after 60 idle frames.
void voice_reload_member(int slot);
// 0x80038e44: voice_queue_abort.
void voice_preload_abort();

}  // namespace ss::game::field
