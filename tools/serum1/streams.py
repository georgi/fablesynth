import sys, struct, zlib
def streams(path):
    d = open(path, 'rb').read(); n = struct.unpack_from('>I', d, 56)[0]; b = d[60:60 + n]
    out = []; gaps = []
    while b:
        if b[0] != 0x78: gaps.append(b); break
        try:
            z = zlib.decompressobj(); r = z.decompress(b)
        except zlib.error: gaps.append(b); break
        out.append(r); b = z.unused_data
    return out, gaps
if __name__ == '__main__':
    for p in sys.argv[1:]:
        s, g = streams(p); print(p.split('/')[-1], [len(x) for x in s], [x.hex() for x in g])
