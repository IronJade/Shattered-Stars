#!/bin/bash
# A local Archipelago server for the network client's tests. It listens on 127.0.0.1 only;
# nothing here talks to archipelago.gg or any other public server.
#
#   apworld/tools/ap_test_server.sh start [PORT]   set up (the first time), start; prints the address
#   apworld/tools/ap_test_server.sh start-wss [PORT]  a second one with TLS (a self-signed certificate
#                                          for localhost, scratchpad/ap/cert.pem), default 38282
#   apworld/tools/ap_test_server.sh stop           stop the servers started here (by their PIDs)
#   apworld/tools/ap_test_server.sh address        the address of the running server
#
# Then: SS_AP_SERVER=ws://localhost:38281 port/build/ss_game_tests   (the ap_net cases),
# and for wss also SS_AP_SERVER_WSS=wss://localhost:38282 SS_AP_CERT=scratchpad/ap/cert.pem
#
# Everything lives in scratchpad/ap/ (git-ignored): Python 3.13 (uv), Archipelago 0.6.8, a
# venv with its requirements, and a two-player ChecksFinder multiworld (slots Alice and
# Bob, seed 12345); until there is a Star Ocean APWorld, ChecksFinder stands in to test the
# protocol. The server starts fresh each time (--disable_save).
set -eu
cd "$(dirname "$0")/../.."
ap=$PWD/scratchpad/ap
mkdir -p "$ap"
export UV_PYTHON_INSTALL_DIR=$ap/python UV_CACHE_DIR=$ap/uv-cache SKIP_REQUIREMENTS_UPDATE=1

setup() {
  if [ ! -x "$ap/tools-venv/bin/uv" ]; then
    python3 -m venv "$ap/tools-venv"
    "$ap/tools-venv/bin/pip" install -q uv
  fi
  [ -x "$ap/python/cpython-3.13"*/bin/python3.13 ] 2>/dev/null || "$ap/tools-venv/bin/uv" python install 3.13 > /dev/null
  [ -d "$ap/Archipelago" ] || git clone -q --depth 1 --branch 0.6.8 https://github.com/ArchipelagoMW/Archipelago.git "$ap/Archipelago"
  if [ ! -x "$ap/ap-venv/bin/python" ]; then
    "$ap/tools-venv/bin/uv" venv -q --python 3.13 "$ap/ap-venv"
    "$ap/tools-venv/bin/uv" pip install -q --python "$ap/ap-venv/bin/python" pip -r "$ap/Archipelago/requirements.txt"
    (cd "$ap/Archipelago" && SKIP_REQUIREMENTS_UPDATE= "$ap/ap-venv/bin/python" ModuleUpdate.py --yes > /dev/null)
  fi
  if ! ls "$ap"/out/AP_*.zip > /dev/null 2>&1; then
    mkdir -p "$ap/players" "$ap/out"
    printf 'name: Alice\ngame: ChecksFinder\nChecksFinder: {}\n' > "$ap/players/Alice.yaml"
    printf 'name: Bob\ngame: ChecksFinder\nChecksFinder: {}\n' > "$ap/players/Bob.yaml"
    (cd "$ap/Archipelago" && "$ap/ap-venv/bin/python" Generate.py --player_files_path "$ap/players" \
       --outputpath "$ap/out" --seed 12345 > "$ap/generate.log" 2>&1)
  fi
}

serve() {  # NAME PORT [MultiServer options...]: one server in its own process group
  local name=$1 port=$2
  shift 2
  if [ -f "$ap/$name.pid" ] && kill -0 "$(cat "$ap/$name.pid")" 2>/dev/null; then
    echo "already running (PID $(cat "$ap/$name.pid")): $(cat "$ap/$name.address")"
    return 0
  fi
  if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
    echo "port $port is already in use" >&2
    return 1
  fi
  setup
  local zip
  zip=$(ls "$ap"/out/AP_*.zip | head -1)
  # Its own session and process group (MultiServer starts a child process): stop ends both.
  # The subshell becomes the server (exec), so $! is its PID and process group.
  (cd "$ap/Archipelago" && exec setsid "$ap/ap-venv/bin/python" MultiServer.py "$zip" --host 127.0.0.1 \
     --port "$port" --disable_save --loglevel info "$@") > "$ap/$name.log" 2>&1 < /dev/null &
  echo $! > "$ap/$name.pid"
  for _ in $(seq 1 300); do  # until it listens (up to 30 s)
    if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
      echo "$(cat "$ap/$name.address") (PID $(cat "$ap/$name.pid"))"
      return 0
    fi
    sleep 0.1
  done
  echo "the server did not start: see $ap/$name.log" >&2
  return 1
}

case "${1:-}" in
  start)
    port=${2:-38281}
    echo "ws://localhost:$port" > "$ap/server.address"
    serve server "$port"
    ;;
  start-wss)
    port=${2:-38282}
    if [ ! -f "$ap/cert.pem" ]; then
      openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -subj "/CN=localhost" \
        -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" -keyout "$ap/key.pem" -out "$ap/cert.pem" 2> /dev/null
    fi
    echo "wss://localhost:$port" > "$ap/server-wss.address"
    serve server-wss "$port" --cert "$ap/cert.pem" --cert_key "$ap/key.pem"
    ;;
  stop)
    for name in server server-wss; do
      if [ -f "$ap/$name.pid" ]; then
        pid=$(cat "$ap/$name.pid")
        kill -- "-$pid" 2>/dev/null || kill "$pid" 2>/dev/null || true  # the server's process group
        rm -f "$ap/$name.pid" "$ap/$name.address"
      fi
    done
    ;;
  address)
    [ -f "$ap/server.address" ] && cat "$ap/server.address"
    ;;
  *)
    echo "usage: $0 start [PORT] | start-wss [PORT] | stop | address" >&2
    exit 2
    ;;
esac
