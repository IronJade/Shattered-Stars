#pragma once

#include <memory>
#include <string>

#include "ap_net/link.h"

// The real Archipelago link: apclientpp (websocketpp + asio, nlohmann/json; OpenSSL for
// wss, zlib for compression) on a thread of its own. CMake target ss_ap_net.
//
// - It connects to the server, and on its room info connects the slot (ConnectSlot with
//   the game, slot, password, items_handling and the DeathLink tag when asked for); a lost
//   connection is retried by itself, and the slot connected again.
// - "host:port" tries wss:// first, then ws:// (apclientpp); "ws://" / "wss://" choose.
// - The data package (item, location and player names) is kept in memory for the run.
// - Our own DeathLink bounces come back from the server; they are dropped here, so a
//   kDeathLink event always means someone else died.
namespace ss::ap_net {

class Client final : public Link {
 public:
  // 'cert_store': a PEM file of certificate authorities for wss; empty for the system's
  // (Windows: its certificate store; Linux: the distribution's CA bundle, wherever it is).
  explicit Client(ConnectInfo info, std::string cert_store = {});
  ~Client() override;
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  void check(const std::vector<int64_t>& locations) override;
  void goal() override;
  void deathlink(const std::string& cause) override;
  void say(const std::string& text) override;
  std::vector<Event> poll() override;
  State state() const override;
  std::string player_name(int slot) const override;
  std::string item_name(int64_t item, int player) const override;
  std::string location_name(int64_t location, int player) const override;

  // Whether this build can use wss:// (OpenSSL linked in).
  static bool has_ssl();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ss::ap_net
