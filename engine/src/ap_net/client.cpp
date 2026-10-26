// The Archipelago client on apclientpp (see client.h). Compiled as C++17: websocketpp does
// not build as C++20. Every use of the APClient object happens under one mutex: the worker
// thread holds it while it polls and sends, the game thread only for name lookups.

#include "ap_net/client.h"

#include <apclient.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <thread>

#ifndef SS_AP_VERSION_MAJOR  // CMakeLists.txt: archipelago.json's minimum_ap_version
#define SS_AP_VERSION_MAJOR 0
#define SS_AP_VERSION_MINOR 6
#define SS_AP_VERSION_BUILD 8
#endif

namespace ss::ap_net {
namespace {

const APClient::Version kAnnounced{SS_AP_VERSION_MAJOR, SS_AP_VERSION_MINOR, SS_AP_VERSION_BUILD};

using json = nlohmann::json;

// The data package, kept in memory for the run (apclientpp's default store would write
// into ~/.cache/Archipelago, outside the game's own folders).
class MemoryStore final : public APDataPackageStore {
 public:
  bool load(const std::string& game, const std::string& checksum, json& data) override {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = games_.find(game);
    if (it == games_.end()) return false;
    if (!checksum.empty() && it->second.value("checksum", std::string()) != checksum) return false;
    data = it->second;
    return true;
  }
  bool save(const std::string& game, const json& data) override {
    std::lock_guard<std::mutex> lock(mutex_);
    games_[game] = data;
    return true;
  }

 private:
  std::mutex mutex_;
  std::map<std::string, json> games_;
};

MemoryStore& memory_store() {
  static MemoryStore store;
  return store;
}

double now_seconds() {
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string random_uuid() {
  std::random_device rd;
  std::mt19937_64 rng((static_cast<uint64_t>(rd()) << 32) ^ rd());
  static const char hex[] = "0123456789abcdef";
  std::string out;
  for (int i = 0; i < 32; ++i) out += hex[rng() & 15];
  return out;
}

// Linux: an OpenSSL linked into the release knows the certificate paths of the system it
// was built on (Debian's); the player's distribution may keep its CA bundle elsewhere.
std::string system_ca_bundle() {
#if !defined(_WIN32) && !defined(__APPLE__)
  static const char* const candidates[] = {
      "/etc/ssl/certs/ca-certificates.crt",                 // Debian, Ubuntu, Arch, Gentoo
      "/etc/pki/tls/certs/ca-bundle.crt",                   // Fedora, RHEL
      "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",  // Fedora, RHEL (newer)
      "/etc/ssl/ca-bundle.pem",                             // openSUSE
      "/etc/pki/tls/cacert.pem",                            // OpenELEC
      "/etc/ssl/cert.pem",                                  // Alpine
  };
  for (const char* path : candidates) {
    if (std::FILE* f = std::fopen(path, "rb")) {
      std::fclose(f);
      return path;
    }
  }
#endif
  return {};
}

State from_client(APClient::State s) {
  switch (s) {
    case APClient::State::DISCONNECTED: return State::kDisconnected;
    case APClient::State::SOCKET_CONNECTING: return State::kConnecting;
    case APClient::State::SOCKET_CONNECTED:
    case APClient::State::ROOM_INFO: return State::kSocketOpen;
    case APClient::State::SLOT_CONNECTED: return State::kSlotConnected;
  }
  return State::kDisconnected;
}

}  // namespace

struct Client::Impl {
  ConnectInfo info;
  std::string cert_store;

  mutable std::mutex client_mutex;  // every use of 'client'
  std::unique_ptr<APClient> client;
  bool goal_reached = false;        // sent again whenever the slot connects
  std::set<double> own_deaths;      // our bounces' times, to drop their echo
  std::string last_error;           // not to repeat the same socket error while retrying

  std::mutex queue_mutex;  // the events and the requests from the game thread
  std::condition_variable wake;
  std::vector<Event> events;
  std::vector<int64_t> checks;
  bool goal = false;
  std::vector<std::string> deaths, says;

  std::atomic<State> state{State::kDisconnected};
  std::atomic<bool> stop{false};
  std::thread thread;

  void push(Event e) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    events.push_back(std::move(e));
  }
  void status(const std::string& text) {
    Event e;
    e.kind = Event::kStatus;
    e.text = text;
    push(std::move(e));
  }

  void handlers();
  void flush();
  void run();
};

void Client::Impl::handlers() {
  APClient& c = *client;
  c.set_socket_connected_handler([this] {
    last_error.clear();
    status("Connected to " + info.server);
  });
  c.set_socket_error_handler([this](const std::string& error) {
    if (error != last_error) status("Connection error: " + error);
    last_error = error;
  });
  c.set_socket_disconnected_handler([this] { status("Disconnected from " + info.server + "; trying again"); });
  c.set_room_info_handler([this] {
    std::list<std::string> tags;
    if (info.deathlink) tags.push_back("DeathLink");
    client->ConnectSlot(info.slot, info.password, info.items_handling, tags, kAnnounced);
  });
  c.set_slot_connected_handler([this](const json& slot_data) {
    Event e;
    e.kind = Event::kConnected;
    e.slot = client->get_player_number();
    e.team = client->get_team_number();
    e.seed_name = client->get_seed();
    for (int64_t id : client->get_checked_locations()) e.checked.push_back(id);
    for (int64_t id : client->get_missing_locations()) e.missing.push_back(id);
    e.slot_data_json = slot_data.dump();
    push(std::move(e));
    if (goal_reached) client->StatusUpdate(APClient::ClientStatus::GOAL);
  });
  c.set_slot_refused_handler([this](const std::list<std::string>& reasons) {
    Event e;
    e.kind = Event::kRefused;
    for (const std::string& r : reasons) e.text += (e.text.empty() ? "" : ", ") + r;
    push(std::move(e));
  });
  c.set_items_received_handler([this](const std::list<APClient::NetworkItem>& items) {
    Event e;
    e.kind = Event::kItems;
    for (const auto& item : items)
      e.items.push_back(NetItem{item.item, item.location, item.player, static_cast<int>(item.flags), item.index});
    push(std::move(e));
  });
  c.set_bounced_handler([this](const json& command) {
    const json tags = command.value("tags", json::array());
    if (!tags.is_array() || std::find(tags.begin(), tags.end(), "DeathLink") == tags.end()) return;
    const json data = command.value("data", json::object());
    Event e;
    e.kind = Event::kDeathLink;
    e.source = data.value("source", std::string());
    e.text = data.value("cause", std::string());
    const double time = data.value("time", 0.0);
    if (e.source == info.slot && own_deaths.count(time)) return;  // our own, come back
    push(std::move(e));
  });
  c.set_print_json_handler([this](const APClient::PrintJSONArgs& args) {
    Event e;
    e.kind = Event::kMessage;
    e.text = client->render_json(args.data, APClient::RenderFormat::TEXT);
    e.item_send = args.type == "ItemSend" && args.item != nullptr &&
                  ((args.receiving && client->slot_concerns_self(*args.receiving)) ||
                   client->slot_concerns_self(args.item->player));
    push(std::move(e));
  });
}

void Client::Impl::flush() {
  std::vector<int64_t> c;
  std::vector<std::string> d, s;
  bool g = false;
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    c.swap(checks);
    g = goal;
    goal = false;
    d.swap(deaths);
    if (client->get_state() == APClient::State::SLOT_CONNECTED) s.swap(says);
  }
  const bool connected = client->get_state() == APClient::State::SLOT_CONNECTED;
  if (!c.empty()) client->LocationChecks(std::list<int64_t>(c.begin(), c.end()));  // queued until connected
  if (g) {
    goal_reached = true;
    if (connected) client->StatusUpdate(APClient::ClientStatus::GOAL);  // else when the slot connects
  }
  for (const std::string& cause : d) {
    if (!connected) {  // a death is news only now: not sent later
      status("DeathLink not sent: not connected");
      continue;
    }
    const double time = now_seconds();
    own_deaths.insert(time);
    json data = {{"time", time}, {"source", info.slot}};
    if (!cause.empty()) data["cause"] = cause;
    client->Bounce(data, {}, {}, {"DeathLink"});
  }
  for (const std::string& text : s) client->Say(text);
}

void Client::Impl::run() {
  {
    std::lock_guard<std::mutex> lock(client_mutex);
    try {
      client = std::make_unique<APClient>(info.uuid, info.game, info.server, cert_store, &memory_store());
    } catch (const std::exception& e) {
      status(std::string("Cannot connect to ") + info.server + ": " + e.what());
      return;
    }
    handlers();
  }
  status("Connecting to " + info.server);
  while (!stop) {
    {
      std::lock_guard<std::mutex> lock(client_mutex);
      client->poll();
      flush();
      state = from_client(client->get_state());
    }
    std::unique_lock<std::mutex> lock(queue_mutex);
    wake.wait_for(lock, std::chrono::milliseconds(20), [this] {
      return stop.load() || !checks.empty() || goal || !deaths.empty();
    });
  }
  std::lock_guard<std::mutex> lock(client_mutex);
  client.reset();  // closes the socket
  state = State::kDisconnected;
}

Client::Client(ConnectInfo info, std::string cert_store) : impl_(std::make_unique<Impl>()) {
  impl_->info = std::move(info);
  if (impl_->info.uuid.empty()) impl_->info.uuid = random_uuid();
  impl_->cert_store = cert_store.empty() ? system_ca_bundle() : std::move(cert_store);
  impl_->state = State::kConnecting;
  impl_->thread = std::thread([impl = impl_.get()] { impl->run(); });
}

Client::~Client() {
  impl_->stop = true;
  impl_->wake.notify_all();
  if (impl_->thread.joinable()) impl_->thread.join();
}

void Client::check(const std::vector<int64_t>& locations) {
  {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    impl_->checks.insert(impl_->checks.end(), locations.begin(), locations.end());
  }
  impl_->wake.notify_all();
}

void Client::goal() {
  {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    impl_->goal = true;
  }
  impl_->wake.notify_all();
}

void Client::deathlink(const std::string& cause) {
  {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    impl_->deaths.push_back(cause);
  }
  impl_->wake.notify_all();
}

void Client::say(const std::string& text) {
  std::lock_guard<std::mutex> lock(impl_->queue_mutex);
  impl_->says.push_back(text);
}

std::vector<Event> Client::poll() {
  std::vector<Event> out;
  std::lock_guard<std::mutex> lock(impl_->queue_mutex);
  out.swap(impl_->events);
  return out;
}

State Client::state() const { return impl_->state.load(); }

std::string Client::player_name(int slot) const {
  std::lock_guard<std::mutex> lock(impl_->client_mutex);
  return impl_->client ? impl_->client->get_player_alias(slot) : std::string();
}

std::string Client::item_name(int64_t item, int player) const {
  std::lock_guard<std::mutex> lock(impl_->client_mutex);
  if (!impl_->client) return {};
  return impl_->client->get_item_name(item, impl_->client->get_player_game(player));
}

std::string Client::location_name(int64_t location, int player) const {
  std::lock_guard<std::mutex> lock(impl_->client_mutex);
  if (!impl_->client) return {};
  return impl_->client->get_location_name(location, impl_->client->get_player_game(player));
}

std::string Client::announced_version() {
  return std::to_string(kAnnounced.ma) + "." + std::to_string(kAnnounced.mi) + "." + std::to_string(kAnnounced.build);
}

bool Client::has_ssl() {
#ifdef WSWRAP_NO_SSL
  return false;
#else
  return true;
#endif
}

}  // namespace ss::ap_net
