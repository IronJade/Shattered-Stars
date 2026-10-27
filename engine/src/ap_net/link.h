#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The Archipelago network link (port feature; docs/archipelago_plan.md, phase 2). Plain
// types only: game/ap talks to a Link, and only the program that makes the real Client
// (ap_net/client.h: apclientpp, OpenSSL, zlib) links those. Tests use a fake Link.
//
// Threading: a Link may run its own thread; every member function is safe to call from the
// game thread. Events wait until poll() takes them (game/ap calls it once per vblank).
namespace ss::ap_net {

struct ConnectInfo {
  std::string server;  // "localhost:38281", "ws://host:port" or "wss://host:port"
  std::string slot;    // the slot (player) name
  std::string password;
  std::string game = "Star Ocean: The Second Story";
  std::string uuid;    // this install's id (config key ap_uuid)
  // Which items the server sends (ReceivedItems): 0b001 items from other worlds only. Our
  // own items are given offline (the seed's patches, boss hooks, starting inventory), so
  // the server mustn't send them again.
  int items_handling = 0b001;
  bool deathlink = false;  // tags ["DeathLink"]: send and receive DeathLink bounces
};

enum class State {
  kDisconnected,   // not connected (yet, or the server went away: it retries by itself)
  kConnecting,     // opening the socket
  kSocketOpen,     // connected to the server, the slot not yet (or refused, retrying later)
  kSlotConnected,  // in the game: checks and items flow
};

struct NetItem {
  int64_t item = 0;      // Archipelago item id
  int64_t location = 0;  // where it was found (in the sender's world)
  int player = 0;        // the slot that found it (the sender)
  int flags = 0;         // 1 progression, 2 useful, 4 trap
  int64_t index = 0;     // its position in the server's list of items for us
};

struct Event {
  enum Kind {
    kStatus,     // text: the connection changed (connecting, connected, lost, retrying)
    kConnected,  // the slot is connected: slot, team, seed_name, checked, missing, slot_data_json
    kRefused,    // text: the server's reasons (InvalidSlot, InvalidPassword, ...)
    kItems,      // items: received items, each with its index
    kDeathLink,  // someone else died: source (their name), text (the cause; may be empty)
    kMessage,    // text: a server message as plain text (PrintJSON); item_send: it is an
                 // ItemSend line that concerns us (our item found, or we found one)
  };
  Kind kind = kStatus;
  std::string text;
  std::string source;
  bool item_send = false;
  std::vector<NetItem> items;
  std::vector<int64_t> checked;  // kConnected: locations the server has as checked
  std::vector<int64_t> missing;  // kConnected: locations still to check
  std::string seed_name;         // kConnected: the room's seed (compare with Seed::seed)
  std::string slot_data_json;    // kConnected: the slot's slot_data, as JSON text
  int slot = 0, team = 0;        // kConnected
};

class Link {
 public:
  virtual ~Link() = default;
  // Queued until the slot is connected; sent again after a reconnect where needed.
  virtual void check(const std::vector<int64_t>& locations) = 0;  // LocationChecks
  virtual void goal() = 0;                                        // StatusUpdate CLIENT_GOAL
  virtual void deathlink(const std::string& cause) = 0;           // Bounce, tags [DeathLink]
  virtual void say(const std::string& text) = 0;                  // a chat message
  // [dskeys agent] Data storage (trackers; docs/ap/data_storage.md): Set 'key' to 'value', a
  // JSON text (operation "replace", the value also as the default, no reply asked for). Fire
  // and forget: it goes out only while the slot is connected; one made while it is not is
  // dropped (game/ap writes its keys again after every kConnected). A key set again before it
  // went out sends the later value only.
  virtual void set(const std::string& key, const std::string& value) = 0;
  virtual std::vector<Event> poll() = 0;  // the events since the last call (game thread)
  virtual State state() const = 0;
  // Names from the server's data package ("" until known).
  virtual std::string player_name(int slot) const = 0;
  virtual std::string item_name(int64_t item, int player) const = 0;
  virtual std::string location_name(int64_t location, int player) const = 0;
};

}  // namespace ss::ap_net
