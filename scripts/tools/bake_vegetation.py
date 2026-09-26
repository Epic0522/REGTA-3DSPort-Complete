#!/usr/bin/env python3
"""Bake optional far-canopy caches from the owner's PC-format game assets.

Requires Pillow. Source IMG/TXD/DFF files are read-only. Missing/unsupported
assets are reported and remain on the original runtime rendering path.
"""
import argparse
import math
import pathlib
import struct
from PIL import Image

LEAVES = {'newtreeleaves128', 'newtreeleavesb128', 'oakleaf1', 'oakleaf2',
          'planta256', 'kbtree4_test', 'fuzzyplant256', 'yuka256', 'plantc256'}
SIZE = 128

def chunks(data):
    p = 0
    while p + 12 <= len(data):
        kind, size, version = struct.unpack_from('<III', data, p)
        if p + 12 + size > len(data):
            raise ValueError('truncated RenderWare chunk')
        yield kind, data[p+12:p+12+size], version
        p += 12 + size

def child(data, kind):
    return next(d for k, d, _ in chunks(data) if k == kind)

def cstr(data):
    return data.split(b'\0', 1)[0].decode('ascii').lower()

class Assets:
    def __init__(self, root):
        self.files = {str(p.relative_to(root)).lower(): p for p in root.rglob('*') if p.is_file()}
        self.entries = {}
        data = self.files['models/gta3.dir'].read_bytes()
        for p in range(0, len(data), 32):
            offset, size, name = struct.unpack_from('<II24s', data, p)
            self.entries[cstr(name)] = (offset * 2048, size * 2048)
        self.img = self.files['models/gta3.img']
        self.textures = {}

    def read(self, name):
        if name == 'generic.txd' and 'models/' + name in self.files:
            return self.files['models/' + name].read_bytes()
        if name in self.entries:
            offset, size = self.entries[name]
            with self.img.open('rb') as f:
                f.seek(offset)
                return f.read(size)
        return self.files['models/' + name].read_bytes()

    def txd(self, name):
        if name in self.textures:
            return self.textures[name]
        result = {}
        for kind, data, _ in chunks(child(self.read(name + '.txd'), 22)):
            if kind != 21:
                continue
            s = child(data, 1)
            platform = struct.unpack_from('<I', s)[0]
            texname = cstr(s[8:40])
            if texname not in LEAVES:
                continue
            if platform != 8:
                raise ValueError('requires PC D3D8 textures: ' + texname)
            fmt, alpha, width, height, depth, levels, rastertype, compression = struct.unpack_from('<IIHHBBBB', s, 72)
            p = 88
            palette = None
            if fmt & 0x2000:
                palette = s[p:p+1024]
                p += 1024
            size = struct.unpack_from('<I', s, p)[0]
            raw = s[p+4:p+4+size]
            if compression:
                decoder = {1: (1, 'DXT1'), 3: (2, 'DXT3'), 5: (3, 'DXT5')}[compression]
                image = Image.frombytes('RGBA', (width, height), raw, 'bcn', decoder)
            elif palette:
                image = Image.frombytes('RGBA', (width, height), b''.join(palette[i*4:i*4+4] for i in raw[:width*height]))
            elif depth == 32:
                image = Image.frombytes('RGBA', (width, height), raw[:width*height*4], 'raw', 'BGRA')
            else:
                raise ValueError('unsupported foliage texture format: ' + texname)
            result[texname] = image
        self.textures[name] = result
        return result

def geometries(data):
    for kind, body, version in chunks(data):
        if kind in (16, 26):
            yield from geometries(body)
        elif kind == 15:
            yield body, version

def parse_geometry(body, version):
    s = child(body, 1)
    flags, nt, nv, nm = struct.unpack_from('<IIII', s)
    ver = ((((version >> 14) & 0x3ff00) + 0x30000) | ((version >> 16) & 0x3f)) if version & 0xffff0000 else version << 8
    if nm != 1 or flags & 0x01000000:
        raise ValueError('native/morph geometry is not supported')
    p = 16 + (12 if ver < 0x34000 else 0)
    colors = [tuple(s[p+i*4:p+i*4+4]) for i in range(nv)] if flags & 8 else [(255,)*4]*nv
    if flags & 8:
        p += nv * 4
    sets = (flags >> 16) & 255
    sets = sets or (2 if flags & 128 else (1 if flags & 4 else 0))
    if not sets:
        raise ValueError('no UV coordinates')
    uv = [struct.unpack_from('<2f', s, p+i*8) for i in range(nv)]
    p += nv * sets * 8
    tris = []
    for i in range(nt):
        b, a, material, c = struct.unpack_from('<4H', s, p+i*8)
        tris.append((a, b, c, material))
    p += nt * 8
    if struct.unpack_from('<I', s, p+16)[0] != 1:
        raise ValueError('no vertices')
    positions = s[p+24:p+24+nv*12]
    verts = list(struct.iter_unpack('<3f', positions))
    matlist = child(body, 8)
    links = child(matlist, 1)
    count = struct.unpack_from('<I', links)[0]
    materials = iter(d for k, d, _ in chunks(matlist) if k == 7)
    mats = []
    for i in range(count):
        reference = struct.unpack_from('<i', links, 4+i*4)[0]
        if reference >= 0:
            if reference >= i:
                raise ValueError('invalid material reference')
            mats.append(mats[reference])
            continue
        data = next(materials)
        textures = [d for k, d, _ in chunks(data) if k == 6]
        mats.append(cstr(child(textures[0], 2)) if textures else '')
    mask = sum(1 << i for i, name in enumerate(mats) if name in LEAVES)
    if not mask or len(mats) > 32:
        raise ValueError('no whitelisted canopy')
    h = 2166136261
    signature = struct.pack('<II', nv, nt) + positions + b''.join(struct.pack('<2f',*v) for v in uv)
    signature += b''.join(n.encode('ascii')+b'\0' for n in mats)
    for byte in signature:
        h = ((h ^ byte) * 16777619) & 0xffffffff
    return h, nv, nt, mask, verts, uv, colors, tris, mats

def bake(parsed, textures):
    h, nv, nt, mask, verts, uv, colors, tris, mats = parsed
    leaves = [t for t in tris if mask & (1 << t[3])]
    points = [verts[i] for t in leaves for i in t[:3]]
    lo = [min(v[i] for v in points) for i in range(3)]
    hi = [max(v[i] for v in points) for i in range(3)]
    center = [(lo[i]+hi[i])*.5 for i in range(3)]
    width, height = (hi[0]-lo[0])*1.04, (hi[2]-lo[2])*1.04
    if min(width, height) < .01:
        raise ValueError('degenerate canopy')
    projected = [((v[0]-center[0])/width*SIZE+SIZE/2, SIZE/2-(v[2]-center[2])/height*SIZE, v[1]) for v in verts]
    pixels = [(0,0,0,0)]*(SIZE*SIZE)
    z = [float('inf')]*(SIZE*SIZE)
    for ia, ib, ic, material in leaves:
        texture = textures[mats[material]]
        sample = texture.load()
        tw, th = texture.size
        a,b,c = [projected[i] for i in (ia,ib,ic)]
        denom=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(denom)<1e-6: continue
        for y in range(max(0,int(min(a[1],b[1],c[1]))),min(SIZE,int(max(a[1],b[1],c[1]))+1)):
            for x in range(max(0,int(min(a[0],b[0],c[0]))),min(SIZE,int(max(a[0],b[0],c[0]))+1)):
                wa=((b[1]-c[1])*(x+.5-c[0])+(c[0]-b[0])*(y+.5-c[1]))/denom
                wb=((c[1]-a[1])*(x+.5-c[0])+(a[0]-c[0])*(y+.5-c[1]))/denom
                wc=1-wa-wb
                if min(wa,wb,wc)<-1e-5: continue
                weights=(wa,wb,wc); ids=(ia,ib,ic)
                depth=sum(w*projected[i][2] for w,i in zip(weights,ids))
                dst=y*SIZE+x
                if depth>=z[dst]: continue
                u,v=[sum(w*uv[i][j] for w,i in zip(weights,ids)) for j in range(2)]
                rgba=sample[int((u%1)*tw)%tw,int((v%1)*th)%th]
                # Bake albedo/coverage, not time-of-day lighting. The 3DS applies
                # the current world's ambient to the proxy each rendered frame.
                color=rgba[:3]+(int(sum(w*colors[i][3] for w,i in zip(weights,ids))*rgba[3]/255),)
                if color[3]<128: continue
                z[dst]=depth;pixels[dst]=color[:3]+(255,)
    if sum(p[3]>0 for p in pixels)<SIZE*SIZE//100:
        raise ValueError('empty canopy capture')
    image=Image.new('RGBA',(SIZE,SIZE));image.putdata(pixels)
    return struct.pack('<6I5f',h,nv,nt,mask,SIZE,SIZE,*center,width,height),image

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('game_dir',type=pathlib.Path)
    ap.add_argument('output',type=pathlib.Path)
    ap.add_argument('--previews',type=pathlib.Path)
    args=ap.parse_args();assets=Assets(args.game_dir)
    models={}
    for name,path in assets.files.items():
        if name.endswith('.ide'):
            for line in path.read_text(errors='replace').splitlines():
                cols=[x.strip().lower() for x in line.split(',')]
                if len(cols)>3 and cols[1].startswith(('veg_tree','veg_palm')):
                    models[cols[1]]=cols[2]
    entries=[];seen=set()
    for model,txd in sorted(models.items()):
        try:
            for body,version in geometries(assets.read(model+'.dff')):
                parsed=parse_geometry(body,version)
                if parsed[0] in seen: continue
                header,image=bake(parsed,assets.txd(txd))
                entries.append(header+image.tobytes());seen.add(parsed[0])
                if args.previews:
                    args.previews.mkdir(parents=True,exist_ok=True)
                    image.save(args.previews/(model+'.png'))
                print('baked',model,txd,hex(parsed[0]))
        except (KeyError,ValueError,StopIteration,FileNotFoundError,struct.error) as error:
            print('kept original',model,':',error)
    if not entries: raise SystemExit('No compatible canopies; no output written')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(b'VGI1'+struct.pack('<I',len(entries))+b''.join(entries))
    print('wrote',len(entries),'canopies to',args.output)

if __name__=='__main__':main()
