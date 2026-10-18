"""Lossless standard-library capture compaction and incomplete-write safety."""
import pathlib, subprocess, tempfile, struct, zlib, sys
script=pathlib.Path(__file__).with_name('compact_gui_captures.py')
with tempfile.TemporaryDirectory(prefix='tangos-capture-') as directory:
    root=pathlib.Path(directory);(root/'sse-control').mkdir()
    width,height=320,240
    pixels=bytes([56,34,12,0])*width*height
    data=struct.pack('<2sIHHI',b'BM',54+len(pixels),0,0,54)+struct.pack('<IiiHHIIiiII',40,width,height,1,32,0,len(pixels),0,0,0,0)+pixels
    (root/'window.bmp').write_bytes(data);(root/'helper-floating-tips.bmp').write_bytes(data)
    (root/'incomplete.bmp').write_bytes(b'BMpartial')
    (root/'capture-compactor.stop').write_text('complete')
    run=subprocess.run([sys.executable,str(script),'--directory',str(root)],capture_output=True,timeout=20)
    assert run.returncode==0,(run.stdout,run.stderr)
    assert not (root/'window.bmp').exists()
    png=(root/'window.png').read_bytes()
    assert png[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack_from('>II',png,16)==(width,height)
    compressed=b'';offset=8
    while offset<len(png):
        size=struct.unpack_from('>I',png,offset)[0];kind=png[offset+4:offset+8];payload=png[offset+8:offset+8+size]
        if kind==b'IDAT':compressed+=payload
        assert zlib.crc32(kind+payload)&0xffffffff==struct.unpack_from('>I',png,offset+8+size)[0]
        offset+=12+size
    assert zlib.decompress(compressed)==(b'\0'+bytes([12,34,56])*width)*height
    assert (root/'helper-floating-tips.bmp').read_bytes()==data
    assert (root/'incomplete.bmp').read_bytes()==b'BMpartial'
print('PASS capture compactor: lossless PNG/CRC, incomplete-write retention, pixel-test BMP preserved')