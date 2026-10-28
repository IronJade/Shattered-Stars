# compare_audio.py REDUX_RAW(f32 stereo 44.1k) REDUX_START_S PORT_WAV PORT_GUESS_S DURATION_S: envelope, level, balance, pitch and spectrum agreement of a Redux capture (SDL disk driver) and a port WAV (--audio-wav)
import sys, wave, numpy as np
sr = 44100
r = np.fromfile(sys.argv[1], dtype='<f4').reshape(-1, 2)
r_start = float(sys.argv[2])
w = wave.open(sys.argv[3]); p = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, 2) / 32768.0
p_guess = float(sys.argv[4]); dur = float(sys.argv[5])
def env(x, hop=441, win=2205):
    m = x.mean(axis=1)
    n = (len(m) - win) // hop
    return np.array([np.sqrt(np.mean(m[i*hop:i*hop+win]**2)) for i in range(n)])
rs = r[int(r_start*sr):int((r_start+dur+4)*sr)]
re_ = env(rs)
# search the port's start around the guess (+-4 s) by envelope correlation
best = None
ps_all = p[int(max(0, p_guess-4)*sr):int((p_guess+dur+8)*sr)]
pe_all = env(ps_all)
L = int(dur*100)
for lag in range(0, len(pe_all) - L):
    a = re_[:L]; b = pe_all[lag:lag+L]
    c = np.corrcoef(a, b)[0, 1]
    if best is None or c > best[0]: best = (c, lag)
c, lag = best
p_start = max(0, p_guess-4) + lag/100
ps = p[int(p_start*sr):int((p_start+dur)*sr)]
rs = rs[:len(ps)]
re_, pe = env(rs), env(ps)
print(f'aligned: port start {p_start:.3f} s, redux start {r_start:.3f} s')
print(f'RMS envelope (50 ms windows, 10 ms hop) correlation: {np.corrcoef(re_, pe)[0,1]:.4f}')
rr, pr = np.sqrt(np.mean(rs**2)), np.sqrt(np.mean(ps**2))
print(f'overall RMS: redux {rr:.4f}  port {pr:.4f}  ({20*np.log10(pr/rr):+.2f} dB)')
print(f'L/R balance: redux {np.sqrt(np.mean(rs[:,0]**2))/np.sqrt(np.mean(rs[:,1]**2)):.3f} port {np.sqrt(np.mean(ps[:,0]**2))/np.sqrt(np.mean(ps[:,1]**2)):.3f}')
# dominant pitch per 100 ms (FFT 8192 on mono), and spectral similarity
N = 8192; hop = 4410
win = np.hanning(N)
agree = total = 0; cos = []
def peak(x):
    s = np.abs(np.fft.rfft(x*win)); s[:int(60*N/sr)] = 0
    return s, np.argmax(s)*sr/N
for i in range(0, len(ps)-N, hop):
    a = rs[i:i+N].mean(axis=1); b = ps[i:i+N].mean(axis=1)
    if np.sqrt(np.mean(a**2)) < 0.01: continue
    sa, fa = peak(a); sb, fb = peak(b)
    total += 1
    if abs(12*np.log2(fb/fa)) < 0.5: agree += 1
    cos.append(np.dot(sa, sb) / (np.linalg.norm(sa)*np.linalg.norm(sb)))
print(f'dominant pitch per 100 ms window: {agree}/{total} agree within half a semitone ({100*agree/total:.1f}%)')
print(f'magnitude spectrum cosine similarity per window: mean {np.mean(cos):.4f}, median {np.median(cos):.4f}, min {np.min(cos):.4f}')
# envelope at 1 s resolution for the record
print('RMS per second (redux / port):', ' '.join(f'{np.sqrt(np.mean(rs[s*sr:(s+1)*sr]**2)):.3f}/{np.sqrt(np.mean(ps[s*sr:(s+1)*sr]**2)):.3f}' for s in range(int(dur))))
