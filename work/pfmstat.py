import struct, statistics, sys

def readpfm(p):
    with open(p, 'rb') as f:
        assert f.readline().strip() == b'PF'
        w, h = map(int, f.readline().split())
        f.readline()
        data = struct.unpack('<%df' % (w * h * 3), f.read(w * h * 3 * 4))
    return w, h, data

for name in sys.argv[1:]:
    w, h, d = readpfm(name)
    g = d[1::3]
    var = statistics.pvariance(g)
    # horizontal derivative energy, normalised
    hf = sum(abs(g[y*w+x+1]-g[y*w+x]) for y in range(h) for x in range(w-1)) / (h*(w-1))
    vf = sum(abs(g[(y+1)*w+x]-g[y*w+x]) for y in range(h-1) for x in range(w)) / ((h-1)*w)
    print(f"{name.split('/')[-1]:34s} var={var:.6f}  |dx|={hf:.6f}  |dy|={vf:.6f}  max={max(g):.4f}")
