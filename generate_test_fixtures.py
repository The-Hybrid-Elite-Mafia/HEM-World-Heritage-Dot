"""Synthetic decoder validation signals ONLY. Never an instrument sample library."""
from pathlib import Path
import math, struct, hashlib

root = Path(__file__).parent / 'test-fixtures'
root.mkdir(exist_ok=True)
root.joinpath('README.txt').write_text('SYNTHETIC TEST FIXTURES ONLY. These simple mathematical signals validate decoders. They are not recorded instruments and must not be shipped as an instrument library.\n')
pcm = [round(math.sin(i * 2 * math.pi / 64) * 12000) for i in range(4096)]
raw = struct.pack('<4096h', *pcm)
fmt = struct.pack('<HHIIHH', 1, 1, 44100, 88200, 2, 16)
body = b'WAVEfmt ' + struct.pack('<I',len(fmt)) + fmt + b'data' + struct.pack('<I',len(raw)) + raw
root.joinpath('fixture_C4_v1-80_rr2.wav').write_bytes(b'RIFF' + struct.pack('<I',len(body)) + body)
long_raw = struct.pack('<220500h', *[pcm[i % len(pcm)] for i in range(220500)])
long_body = b'WAVEfmt ' + struct.pack('<I',len(fmt)) + fmt + b'data' + struct.pack('<I',len(long_raw)) + long_raw
root.joinpath('fixture_streaming.wav').write_bytes(b'RIFF' + struct.pack('<I',len(long_body)) + long_body)
comm = struct.pack('>HIH', 1, len(pcm), 16) + bytes.fromhex('400eac44000000000000')
ssnd = b'\0'*8 + struct.pack('>4096h',*pcm)
body = b'AIFFCOMM'+struct.pack('>I',len(comm))+comm+b'SSND'+struct.pack('>I',len(ssnd))+ssnd
root.joinpath('fixture.aiff').write_bytes(b'FORM'+struct.pack('>I',len(body))+body)
# Minimal standards-conforming FLAC: mono signed16, verbatim subframes.
def crc(data, width, poly):
    value=0
    for byte in data:
        value ^= byte << (width-8)
        for _ in range(8): value = ((value << 1) ^ (poly if value & (1 << (width-1)) else 0)) & ((1<<width)-1)
    return value
stream = struct.pack('>HH',256,256) + b'\0'*6 + ((44100<<44)|(15<<36)|4096).to_bytes(8,'big') + hashlib.md5(raw).digest()
flac = bytearray(b'fLaC\x80\x00\x00\x22'+stream)
for index in range(16):
    head = bytes([0xff,0xf8,0x89,0x08,index])
    frame = head+bytes([crc(head,8,7)])+b'\x02'+struct.pack('>256h',*pcm[index*256:(index+1)*256])
    flac += frame+crc(frame,16,0x8005).to_bytes(2,'big')
root.joinpath('fixture.flac').write_bytes(flac)
root.joinpath('truncated.wav').write_bytes(b'RIFF\xff\xff\xff\xffWAVE')
root.joinpath('not_audio.wav').write_text('not audio')
root.joinpath('malformed.whic').write_text('WHIC_PRESET 1\n"test fixture"\n1\n"missing.wav" 60 127 0 1 127 0\n')
print('Created synthetic decoder test fixtures in',root)
