# compare_sound_events.py REDUX_LOG PORT_LOG [play#]: compares the title music's driver events (redux_sound.lua events.log vs the port's SS_SOUND_TRACE log)
import sys, re, collections
def parse(path, redux):
    ev=[]; plays=[]
    for line in open(path):
        m=re.search(r'tick (-?\d+) (.*)', line)
        if not m: continue
        t=int(m.group(1)); rest=m.group(2)
        if rest.startswith('play'): plays.append((len(ev), t)); continue
        ev.append((t, rest))
    return ev, plays
rx, rplays = parse(sys.argv[1], True)
px, pplays = parse(sys.argv[2], False)
which = int(sys.argv[3]) if len(sys.argv) > 3 else 2
def section(ev, plays, k):
    start = plays[k-1][0]
    end = plays[k][0] if k < len(plays) else len(ev)
    seg = ev[start:end]
    # first key-on after the play
    t0 = next(t for t, r in seg if r.startswith('key 1'))
    out = collections.defaultdict(list)
    for t, r in seg:
        if t < t0 - 2: continue
        if r.startswith('key'):
            on, mask = r.split()[1], int(r.split()[2], 16)
            out[t - t0].append(('key', on, bin(mask).count('1')))
        elif r.startswith('attr'):
            f = dict(kv.split('=') for kv in r.split()[2:])
            out[t - t0].append(('attr', f['vol'], f['pitch'], f['addr'], f['adsr']))
    return out, t0
r, rt0 = section(rx, rplays, which)
p, pt0 = section(px, pplays, which)
ticks = sorted(set(r) | set(p))
same = diff = 0
shown = 0
for t in ticks:
    a = sorted(r.get(t, [])); b = sorted(p.get(t, []))
    if a == b: same += 1
    else:
        diff += 1
        if shown < 15:
            print('tick', t, '\n  redux', a, '\n  port ', b); shown += 1
nr = sum(len(v) for v in r.values()); np_ = sum(len(v) for v in p.values())
print(f'redux events {nr}, port events {np_}; ticks identical {same}, different {diff}; t0 redux {rt0} port {pt0}')
# Order-only comparison (voice numbers included) and the tick offset of each event.
def flat(ev, plays, k):
    start = plays[k-1][0]; end = plays[k][0] if k < len(plays) else len(ev)
    seg = [(t, r) for t, r in ev[start:end] if r.startswith('key') or r.startswith('attr')]
    t0 = next(t for t, r in seg if r.startswith('key 1'))
    return [(t - t0, r) for t, r in seg]
fr = flat(rx, rplays, which); fp = flat(px, pplays, which)
n = min(len(fr), len(fp))
same_order = sum(1 for i in range(n) if fr[i][1] == fp[i][1])
offs = collections.Counter(fr[i][0] - fp[i][0] for i in range(n) if fr[i][1] == fp[i][1])
first_bad = next((i for i in range(n) if fr[i][1] != fp[i][1]), None)
print(f'events in order: {same_order}/{n} identical (voices, pitch, volume, ADSR, address); tick offsets redux-port: {dict(offs)}; first mismatch at {first_bad}')
if first_bad is not None: print(' redux', fr[first_bad], '\n port ', fp[first_bad])
