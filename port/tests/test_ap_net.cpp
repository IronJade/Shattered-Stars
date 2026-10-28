// The Archipelago network client against a real, local server (apworld/tools/ap_test_server.sh:
// MultiServer on 127.0.0.1 with a two-player ChecksFinder multiworld, slots Alice and Bob).
// Set SS_AP_SERVER (e.g. ws://localhost:38281) to run them; skipped otherwise. They
// connect, check every location, receive items with their indices, reconnect and resume,
// bounce a DeathLink, report a goal and get refused, and pass however often they run
// against the same server.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ap_net/client.h"
#include "test.h"

using namespace ss;

namespace {

std::string server() {
  const char* s = std::getenv("SS_AP_SERVER");
  return s && *s ? s : "";
}

bool skip() {
  if (!server().empty()) return false;
  std::printf("  (skipped: SS_AP_SERVER not set; apworld/tools/ap_test_server.sh start)\n");
  return true;
}

ap_net::ConnectInfo info(const std::string& slot, bool deathlink = false) {
  ap_net::ConnectInfo i;
  i.server = server();
  i.slot = slot;
  i.game = "ChecksFinder";
  i.items_handling = 0b111;  // the test worlds: all items, own ones too
  i.deathlink = deathlink;
  return i;
}

// Polls a client until 'done' says so (up to 'seconds'); every event lands in 'seen'.
bool wait_for(ap_net::Link& link, std::vector<ap_net::Event>& seen,
              const std::function<bool(const std::vector<ap_net::Event>&)>& done, double seconds = 15) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
  while (std::chrono::steady_clock::now() < end) {
    for (auto& e : link.poll()) seen.push_back(std::move(e));
    if (done(seen)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

const ap_net::Event* find(const std::vector<ap_net::Event>& seen, ap_net::Event::Kind kind) {
  for (const auto& e : seen)
    if (e.kind == kind) return &e;
  return nullptr;
}

// Every item received so far, by index (later lists win, as after a reconnect).
std::vector<ap_net::NetItem> items_by_index(const std::vector<ap_net::Event>& seen) {
  std::vector<ap_net::NetItem> out;
  for (const auto& e : seen) {
    if (e.kind != ap_net::Event::kItems) continue;
    for (const auto& item : e.items) {
      if (item.index < 0) continue;
      if (static_cast<size_t>(item.index) >= out.size()) out.resize(static_cast<size_t>(item.index) + 1);
      out[static_cast<size_t>(item.index)] = item;
    }
  }
  return out;
}

bool connected(const std::vector<ap_net::Event>& seen) { return find(seen, ap_net::Event::kConnected) != nullptr; }

}  // namespace

TEST(ap_net_connects_and_names) {
  if (skip()) return;
  ap_net::Client alice(info("Alice"));
  std::vector<ap_net::Event> seen;
  CHECK(wait_for(alice, seen, connected));
  const ap_net::Event* c = find(seen, ap_net::Event::kConnected);
  if (!c) return;
  CHECK(!c->seed_name.empty());
  CHECK(c->slot >= 1);
  CHECK_EQ(c->checked.size() + c->missing.size(), static_cast<size_t>(25));  // ChecksFinder's 25 tiles
  CHECK(alice.state() == ap_net::State::kSlotConnected);
  CHECK(alice.player_name(c->slot) == "Alice");
  const int64_t any = c->missing.empty() ? c->checked.front() : c->missing.front();
  CHECK(alice.location_name(any, c->slot).rfind("Tile", 0) == 0);
  CHECK(!c->slot_data_json.empty());
}

TEST(ap_net_checks_items_and_resume) {
  if (skip()) return;
  // Both players check all their locations; each then holds every item of their world,
  // with indices 0..n-1 and no gaps.
  std::vector<ap_net::Event> a_seen, b_seen;
  auto alice = std::make_unique<ap_net::Client>(info("Alice"));
  ap_net::Client bob(info("Bob"));
  CHECK(wait_for(*alice, a_seen, connected));
  CHECK(wait_for(bob, b_seen, connected));
  const ap_net::Event* ac = find(a_seen, ap_net::Event::kConnected);
  const ap_net::Event* bc = find(b_seen, ap_net::Event::kConnected);
  if (!ac || !bc) return;
  std::vector<int64_t> a_all = ac->missing, b_all = bc->missing;
  a_all.insert(a_all.end(), ac->checked.begin(), ac->checked.end());
  b_all.insert(b_all.end(), bc->checked.begin(), bc->checked.end());
  // Alice checks half now, half after a reconnect.
  const size_t half = a_all.size() / 2;
  alice->check(std::vector<int64_t>(a_all.begin(), a_all.begin() + static_cast<std::ptrdiff_t>(half)));
  bob.check(b_all);
  CHECK(wait_for(*alice, a_seen, [&](const auto&) { return items_by_index(a_seen).size() >= 1; }));
  alice.reset();  // the connection goes away...
  a_seen.clear();
  alice = std::make_unique<ap_net::Client>(info("Alice"));  // ...and comes back
  CHECK(wait_for(*alice, a_seen, connected));
  ac = find(a_seen, ap_net::Event::kConnected);
  if (!ac) return;
  CHECK(ac->checked.size() >= half);  // the server kept the first half
  alice->check(std::vector<int64_t>(a_all.begin() + static_cast<std::ptrdiff_t>(half), a_all.end()));
  CHECK(wait_for(*alice, a_seen, [&](const auto&) {
    const auto items = items_by_index(a_seen);
    return items.size() >= 25;
  }));
  const auto items = items_by_index(a_seen);
  CHECK_EQ(items.size(), static_cast<size_t>(25));  // every item of Alice's world, indices 0..24
  std::set<int> senders;
  for (const auto& item : items) {
    CHECK(item.item != 0);
    senders.insert(item.player);
  }
  CHECK(senders.size() == 2);  // found in both worlds
  CHECK(!alice->item_name(items[0].item, ac->slot).empty());
}

TEST(ap_net_deathlink_goal_and_refusal) {
  if (skip()) return;
  ap_net::Client alice(info("Alice", true));
  ap_net::Client bob(info("Bob", true));
  std::vector<ap_net::Event> a_seen, b_seen;
  CHECK(wait_for(alice, a_seen, connected));
  CHECK(wait_for(bob, b_seen, connected));
  // A DeathLink reaches the others, and not its sender.
  alice.deathlink("a slime, in testing");
  CHECK(wait_for(bob, b_seen, [](const auto& s) { return find(s, ap_net::Event::kDeathLink) != nullptr; }));
  if (const ap_net::Event* d = find(b_seen, ap_net::Event::kDeathLink)) {
    CHECK(d->source == "Alice");
    CHECK(d->text == "a slime, in testing");
  }
  wait_for(alice, a_seen, [](const auto&) { return false; }, 0.5);
  CHECK(find(a_seen, ap_net::Event::kDeathLink) == nullptr);
  // The goal: the server tells everyone, once per slot. (On a server that already saw it, from
  // an earlier run, nothing comes: apworld/tools/ap_test_server.sh stop / start for a fresh one.)
  const bool announced_before = std::any_of(a_seen.begin(), a_seen.end(), [](const ap_net::Event& e) {
    return e.kind == ap_net::Event::kMessage && e.text.find("Bob") != std::string::npos && e.text.find("goal") != std::string::npos;
  });
  bob.goal();
  const bool announced = wait_for(alice, a_seen, [](const auto& s) {
    for (const auto& e : s)
      if (e.kind == ap_net::Event::kMessage && e.text.find("Bob") != std::string::npos &&
          e.text.find("goal") != std::string::npos)
        return true;
    return false;
  }, 5);
  if (!announced && !announced_before)
    std::printf("  (no goal announcement: Bob reached the goal in an earlier run against this server)\n");
  // A slot that isn't there: refused, with the reason.
  ap_net::Client nobody(info("Nobody"));
  std::vector<ap_net::Event> n_seen;
  CHECK(wait_for(nobody, n_seen, [](const auto& s) { return find(s, ap_net::Event::kRefused) != nullptr; }));
  if (const ap_net::Event* r = find(n_seen, ap_net::Event::kRefused)) CHECK(r->text.find("InvalidSlot") != std::string::npos);
  CHECK(nobody.state() != ap_net::State::kSlotConnected);
}

// wss:// (OpenSSL): a local server with a self-signed certificate (apworld/tools/ap_test_server.sh
// start-wss), trusted through the client's cert store argument.
TEST(ap_net_connects_over_tls) {
  const char* wss = std::getenv("SS_AP_SERVER_WSS");
  const char* cert = std::getenv("SS_AP_CERT");
  if (!wss || !*wss || !cert || !*cert) {
    std::printf("  (skipped: SS_AP_SERVER_WSS / SS_AP_CERT not set; apworld/tools/ap_test_server.sh start-wss)\n");
    return;
  }
  if (!ap_net::Client::has_ssl()) {
    std::printf("  (skipped: built without OpenSSL)\n");
    return;
  }
  ap_net::ConnectInfo i = info("Alice");
  i.server = wss;
  ap_net::Client alice(i, cert);
  std::vector<ap_net::Event> seen;
  CHECK(wait_for(alice, seen, connected));
  CHECK(alice.state() == ap_net::State::kSlotConnected);
}

TEST(ap_net_unreachable_server_reports_and_retries) {
  // No server needed: a closed port gives status events, never kConnected, and a clean stop.
  ap_net::ConnectInfo i;
  i.server = "ws://127.0.0.1:9";  // the discard port: nothing listens there
  i.slot = "Alice";
  auto client = std::make_unique<ap_net::Client>(i);
  std::vector<ap_net::Event> seen;
  wait_for(*client, seen, [](const auto&) { return false; }, 1.0);
  CHECK(!seen.empty());
  CHECK(find(seen, ap_net::Event::kConnected) == nullptr);
  CHECK(client->state() != ap_net::State::kSlotConnected);
  client->check({1, 2, 3});  // queued, harmless
  client.reset();            // stops promptly
  CHECK(true);
}
