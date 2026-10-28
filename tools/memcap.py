"""memcap.py: a memory cap for every headless game process a tool starts.

The machine is shared with the user's desktop, so no game run may take more than a few GB
(docs/CONVENTIONS.md, machine budget). A run that misreads a table can loop and allocate
without bound; the cap turns that into a failure of that run instead of a full machine.

    from memcap import capped
    cmd, preexec = capped(cmd, binary)          # then Popen(cmd, preexec_fn=preexec, ...)
    watch = Watch(p.pid)                         # poll watch.poll() while it runs
    ... hit_cap(rc, output[, killed], watch) -> True when the run died at the cap
    rc, timed_out, at_cap = run_capped(cmd, binary, timeout, **popen_kwargs)   # all in one

- A normal build: RLIMIT_AS (address space) of SS_MEM_CAP_GB (default 4) GB, set in the
  child before exec. An allocation past it fails (std::bad_alloc, an abort).
- An AddressSanitizer build reserves terabytes of shadow address space, so RLIMIT_AS can't
  apply; it runs in a systemd user scope with MemoryMax (SS_MEM_CAP_ASAN_GB, default 6)
  instead, where the kernel kills it at the cap. Without systemd-run it runs uncapped, with
  a warning.
- macOS has no RLIMIT_AS (setrlimit refuses it), so there the watch below is the cap: the
  runner kills a process whose physical footprint (Activity Monitor's "Memory") passes it.
- SS_MEM_CAP_GB=0 turns the cap off (never on this machine).

A process at the cap does not always die of std::bad_alloc: a failed malloc deep in a
library can also end in SIGSEGV. So the runner also watches the process's address space
(/proc/PID/status VmPeak) and calls a crash whose peak came within 5% of the cap a cap hit.

As a command, GNU timeout under the cap (port/tools/smoke_field_events.sh's `timeout` on macOS):
    tools/memcap.py DURATION COMMAND [ARG...]   # 124 when the time runs out, 137 at the cap
"""

import ctypes
import ctypes.util
import os
import resource
import shutil
import subprocess
import sys
import time

_asan = {}
_darwin = sys.platform == "darwin"
_libproc = None


def cap_bytes():
    return int(float(os.environ.get("SS_MEM_CAP_GB", "4")) * (1 << 30))


def is_asan(binary):
    if binary not in _asan:
        try:
            with open(binary, "rb") as f:
                _asan[binary] = b"__asan_init" in f.read()
        except OSError:
            _asan[binary] = False
    return _asan[binary]


def _limit(n):
    if _darwin:
        return None  # no RLIMIT_AS there: the runner's watch kills at the cap
    def preexec():
        resource.setrlimit(resource.RLIMIT_AS, (n, n))
    return preexec


def limit_preexec():
    """A preexec_fn that caps a child (and everything it starts: each process gets its own
    limit) by RLIMIT_AS; for shell commands of non-ASan programs. None when the cap is off,
    and on macOS (only a Watch can cap there)."""
    n = cap_bytes()
    return _limit(n) if n > 0 else None


def capped(cmd, binary=None):
    """(command, preexec_fn) running 'cmd' (a list) under the cap."""
    n = cap_bytes()
    if n <= 0:
        return list(cmd), None
    if is_asan(binary or cmd[0]):
        if shutil.which("systemd-run"):
            gb = os.environ.get("SS_MEM_CAP_ASAN_GB", "6")
            return ["systemd-run", "--user", "--scope", "--quiet", "-p", f"MemoryMax={gb}G",
                    "-p", "MemorySwapMax=0", "--"] + list(cmd), None
        print("memcap: an ASan build and no systemd-run: running without a memory cap", file=sys.stderr)
        return list(cmd), None
    return list(cmd), _limit(n)


def _darwin_footprint_kb(pid):
    """macOS: a process's physical footprint (resident plus compressed), from proc_pid_rusage;
    0 when it can't be read (the process is gone)."""
    global _libproc
    if _libproc is None:
        _libproc = ctypes.CDLL(ctypes.util.find_library("proc"))
    info = (ctypes.c_uint64 * 12)()  # rusage_info_v0: a 16-byte uuid, then 10 counters
    if _libproc.proc_pid_rusage(int(pid), 0, ctypes.byref(info)) != 0:
        return 0
    return info[9] // 1024  # ri_phys_footprint


class Watch:
    """The peak address space (VmPeak) of a running process, from /proc (Linux); on macOS its
    peak physical footprint."""

    def __init__(self, pid):
        self.pid = pid
        self.peak_kb = 0

    def poll(self):
        if _darwin:
            self.peak_kb = max(self.peak_kb, _darwin_footprint_kb(self.pid))
            return self.peak_kb
        try:
            with open(f"/proc/{self.pid}/status") as f:
                for line in f:
                    if line.startswith("VmPeak:"):
                        self.peak_kb = max(self.peak_kb, int(line.split()[1]))
                        break
        except (OSError, ValueError):
            pass
        return self.peak_kb

    def near_cap(self):
        n = cap_bytes()
        return n > 0 and self.peak_kb * 1024 >= n * 0.95


def hit_cap(rc, output, killed=False, watch=None):
    """True when a finished run died at the cap: an allocation failure (RLIMIT_AS), a crash
    whose address space had reached the cap (watch), or a SIGKILL the tool did not send
    itself ('killed': its own hang kill), which is the scope's MemoryMax (or the kernel's OOM
    killer)."""
    if rc == 0 or rc is None:
        return False
    text = output if isinstance(output, str) else "\n".join(output)
    if ("std::bad_alloc" in text or "Cannot allocate memory" in text or "out of memory" in text.lower()
            or "at the memory cap" in text):
        return True
    if watch is not None and watch.near_cap():
        return True
    return rc == -9 and not killed


def run_capped(cmd, binary=None, timeout=None, **kwargs):
    """subprocess.run under the cap: (returncode, timed_out, at_cap); output goes where
    kwargs say (stdout=...). A timed-out run is killed (this process only)."""
    run_cmd, preexec = capped(cmd, binary)
    p = subprocess.Popen(run_cmd, preexec_fn=preexec, **kwargs)
    watch = Watch(p.pid)
    t0 = time.time()
    timed_out = False
    while p.poll() is None:
        watch.poll()
        if timeout is not None and time.time() - t0 > timeout:
            timed_out = True
            p.kill()
            break
        if _darwin and cap_bytes() > 0 and watch.peak_kb * 1024 >= cap_bytes():
            p.kill()  # macOS: the watch is the cap
            break
        time.sleep(0.25)
    rc = p.wait()
    at_cap = not timed_out and rc != 0 and (watch.near_cap() or (rc == -9))
    return rc, timed_out, at_cap


def main(argv):
    """GNU timeout under the cap: tools/memcap.py DURATION COMMAND [ARG...]. The command's own
    exit status; 124 when the time runs out; 137 when it dies at the cap (with a line on stderr
    that hit_cap() knows)."""
    if len(argv) < 3:
        print("usage: memcap.py DURATION COMMAND [ARG...]", file=sys.stderr)
        return 125
    rc, timed_out, at_cap = run_capped(argv[2:], timeout=float(argv[1]))
    if timed_out:
        return 124
    if at_cap:
        print(f"memcap: {argv[2]} stopped at the memory cap ({cap_bytes() >> 30} GB)", file=sys.stderr)
        return 137
    return 128 - rc if rc < 0 else rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
