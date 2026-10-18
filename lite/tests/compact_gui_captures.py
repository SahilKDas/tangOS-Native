"""Compact completed synthetic 32-bit BMP captures; Python standard library only."""
import argparse, pathlib, struct, time, zlib
def encode_bmp(data):
    if len(data)<54 or data[:2]!=b'BM': raise ValueError('Incomplete BMP header')
    offset=struct.unpack_from('<I',data,10)[0]
    width,height=struct.unpack_from('<ii',data,18)
    planes,bpp,compression=struct.unpack_from('<HHI',data,26)
    if width<=0 or height==0 or width>16384 or abs(height)>16384 or planes!=1 or bpp!=32 or compression!=0:
        raise ValueError('Unsupported capture geometry')
    height,top_down=abs(height),height<0
    stride=width*4
    if len(data)<offset+stride*height: raise ValueError('Incomplete BMP pixels')
    compressor=zlib.compressobj(3)
    parts=[]
    for y in range(height):
        row=data[offset+(y if top_down else height-1-y)*stride:offset+(y if top_down else height-1-y)*stride+stride]
        rgb=bytearray(width*3)
        rgb[0::3]=row[2::4];rgb[1::3]=row[1::4];rgb[2::3]=row[0::4]
        parts.append(compressor.compress(b'\0'+rgb))
    parts.append(compressor.flush())
    def chunk(kind,payload):
        return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',b''.join(parts))+chunk(b'IEND',b'')
def main():
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True)
    root=pathlib.Path(p.parse_args().directory).resolve(strict=True)
    if not (root/'sse-control').is_dir(): raise RuntimeError('An explicit GUI fixture is required')
    stop=root/'capture-compactor.stop'
    while True:
        stopping=stop.exists()
        for source in root.glob('*.bmp'):
            if source.name=='helper-floating-tips.bmp' or source.is_symlink(): continue
            try:
                before=source.stat()
                if not stopping and time.time()-before.st_mtime<0.8: continue
                encoded=encode_bmp(source.read_bytes())
                after=source.stat()
                if before.st_size!=after.st_size or before.st_mtime_ns!=after.st_mtime_ns: continue
                temporary=source.with_suffix('.png.partial')
                temporary.write_bytes(encoded);temporary.replace(source.with_suffix('.png'))
                source.unlink()
            except (OSError,ValueError):
                continue
        if stopping: break
        time.sleep(.2)
if __name__=='__main__':main()