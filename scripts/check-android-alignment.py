"""Check every packaged ELF and uncompressed library's APK offset for 16 KB."""
import struct
import sys
import zipfile
from pathlib import Path

apk = Path(sys.argv[1])
page = 16384
with zipfile.ZipFile(apk) as archive, apk.open('rb') as raw:
    libraries = [entry for entry in archive.infolist() if entry.filename.endswith('.so')]
    if not libraries:
        raise SystemExit('No native libraries found')
    for entry in libraries:
        data = archive.read(entry)
        if data[:6] != b'\x7fELF\x02\x01':
            raise SystemExit(f'Unsupported ELF format: {entry.filename}')
        phoff = struct.unpack_from('<Q', data, 32)[0]
        phsize, phcount = struct.unpack_from('<HH', data, 54)
        alignments = []
        for i in range(phcount):
            position = phoff + i * phsize
            if struct.unpack_from('<I', data, position)[0] != 1:
                continue
            offset, virtual = struct.unpack_from('<QQ', data, position + 8)
            alignment = struct.unpack_from('<Q', data, position + 48)[0]
            if alignment < page or (virtual - offset) % page:
                raise SystemExit(f'ELF not 16 KB aligned: {entry.filename}')
            alignments.append(alignment)
        if not alignments:
            raise SystemExit(f'No load segments: {entry.filename}')
        raw.seek(entry.header_offset + 26)
        name_size, extra_size = struct.unpack('<HH', raw.read(4))
        offset = entry.header_offset + 30 + name_size + extra_size
        if entry.compress_type == zipfile.ZIP_STORED and offset % page:
            raise SystemExit(f'APK entry not 16 KB aligned: {entry.filename}')
        print(f'{entry.filename}: ELF {min(alignments)} bytes; APK offset {offset}: OK')
print('16 KB alignment: PASS')
