"""Original LCS (PS2) loading screens, splash/title images, intro captions, menu background and movies
for the SA front end, built from the user's own copy of the game.

Code only: nothing extracted from the game is stored in the repository. The output is a tree of files that
the modloader overlays on the SA ones (models/txd/*.txd, models/fronten2.txd, movies/*.mpg).

SA dictionary                      <- LCS source (SPLASH/*.CHK, MOVIES/*.PSS)
  models/txd/loadscs.txd           loadsc0..14 <- LOADSC0..3 (cycled); title_pc_EU/US <- SCEALE; nvidia/eax kept
  models/txd/loadsc0..14.txd       same textures, standalone (and LOADSUK.txd / load0uk.txd)
  models/txd/splash1..3.txd        SPLASH1..3 ("Welcome to <island>")
  models/txd/intro1..4.txd         INTRO1..4 (caption cards)
  models/fronten2.txd              back2..back7 = one LCS loading screen across the 3x2 SA menu mosaic, dimmed
                                   like the PS2 front end (a little lighter); every other texture kept
  movies/Logo.mpg                  RSLEEDS.PSS
  movies/GTAtitles.mpg             DM.PSS (intro credits)

CHK textures are 8-bit (INTRO: 4-bit) linear images with a PS2 CLUT (entries 8-15 and 16-23 swapped).
"""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import wave
from array import array
from pathlib import Path

RW_VERSION = 0x1803FFFF   # RenderWare 3.6.0.3, as in SA's own dictionaries
ID_STRUCT, ID_EXTENSION, ID_TEXTURE_NATIVE, ID_TEXDICT = 0x01, 0x03, 0x15, 0x16
D3DFMT_A8R8G8B8 = 21
MENU_DIM = 64 / 128       # PS2 front end tints the splash with 48 of 128 (0.375); half keeps the art readable on PC
MENU_TILES = ('back2', 'back3', 'back4', 'back5', 'back6', 'back7')   # row-major 3x2 mosaic


def image_module():
    try:
        from PIL import Image
    except ImportError as error:
        raise SystemExit('Pillow is required to convert the LCS screens (pip install pillow)') from error
    return Image


def read_chk(path):
    """Returns (width, height, RGB bytes) of a PS2 loading-screen texture chunk."""
    data = Path(path).read_bytes()
    base, body = 0xB8, len(data) - 0xB8
    if data[:4] != b'xet\0':
        raise ValueError('Not a PS2 texture chunk: ' + str(path))
    if body in (65536 + 1024, 131072 + 1024, 262144 + 1024):
        bits, indices, colours = 8, body - 1024, 256
    elif body == 131072 + 64:
        bits, indices, colours = 4, 131072, 16
    else:
        raise ValueError('Unsupported PS2 texture layout (%d bytes): %s' % (len(data), path))
    pixels = data[base:base + indices]
    palette = data[base + indices:base + indices + colours * 4]

    def entry(i):
        if colours == 256:
            group = i & 0x18
            i = i + 8 if group == 8 else i - 8 if group == 16 else i
        return palette[i * 4:i * 4 + 3]
    table = [entry(i) for i in range(colours)]
    if bits == 4:
        pixels = bytes(v for byte in pixels for v in (byte & 15, byte >> 4))
    rgb = b''.join(table[p] for p in pixels)
    return 512, len(pixels) // 512, rgb


def chunk(kind, payload):
    return struct.pack('<III', kind, len(payload), RW_VERSION) + payload


def native_texture(name, width, height, rgb):
    """D3D9 A8R8G8B8 texture native chunk, as the package's own lcs_*.txd."""
    pixels = bytearray(width * height * 4)
    pixels[0::4], pixels[1::4], pixels[2::4], pixels[3::4] = rgb[2::3], rgb[1::3], rgb[0::3], b'\xff' * (width * height)
    header = struct.pack('<II', 9, 0x1102) + name.encode().ljust(32, b'\0') + bytes(32)
    header += struct.pack('<IIHHBBBB', 0x500, D3DFMT_A8R8G8B8, width, height, 32, 1, 4, 1)
    return chunk(ID_TEXTURE_NATIVE, chunk(ID_STRUCT, header + struct.pack('<I', len(pixels)) + bytes(pixels)) + chunk(ID_EXTENSION, b''))


def dictionary(natives):
    return chunk(ID_TEXDICT, chunk(ID_STRUCT, struct.pack('<HH', len(natives), 2)) + b''.join(natives) + chunk(ID_EXTENSION, b''))


def native_name(data, offset):
    # texture chunk header 12 + struct header 12 + platform and filter 8
    return data[offset + 32:offset + 64].split(b'\0')[0].decode('latin1')


def replace_textures(source, replacements):
    """Copy of an existing D3D9 dictionary with some textures replaced by name (case-insensitive)."""
    data = source.read_bytes()
    kind, size, _ = struct.unpack_from('<III', data, 0)
    if kind != ID_TEXDICT:
        raise ValueError('Not a texture dictionary: ' + str(source))
    wanted = {name.lower(): native for name, native in replacements.items()}
    seen, body, offset, end, count = set(), [], 12, 12 + size, 0
    while offset + 12 <= end:
        kind, size, _ = struct.unpack_from('<III', data, offset)
        raw = data[offset:offset + 12 + size]
        if kind == ID_STRUCT:
            count = struct.unpack_from('<H', raw, 12)[0]
        elif kind == ID_TEXTURE_NATIVE:
            name = native_name(data, offset).lower()
            if name in wanted:
                raw = wanted[name]
                seen.add(name)
        body.append(raw)
        offset += 12 + size
    missing = set(wanted) - seen
    if missing:
        raise ValueError('%s lacks the textures %s' % (source.name, sorted(missing)))
    return chunk(ID_TEXDICT, b''.join(body))


def pss_audio(path):
    """Interleaved 16-bit stereo PCM and sample rate of a PS2 PSS (private stream 1: 'SShd'/'SSbd' PCM)."""
    data = Path(path).read_bytes()
    position, header, channels = 0, None, []
    pcm = bytearray()
    while True:
        position = data.find(b'\x00\x00\x01', position)
        if position < 0 or position + 6 > len(data):
            break
        stream = data[position + 3]
        if stream == 0xBA:
            position += 14
        elif stream == 0xB9:
            break
        elif stream >= 0xBB:
            length = struct.unpack_from('>H', data, position + 4)[0]
            if stream == 0xBD:
                payload = data[position + 9 + data[position + 8]:position + 6 + length]
                if header is None:
                    at = payload.find(b'SShd')
                    codec, rate, count, interleave = struct.unpack_from('<IIII', payload, at + 8)
                    if codec != 1 or count != 2:
                        raise ValueError('Unsupported PSS audio (codec %d, %d channels)' % (codec, count))
                    header = (rate, interleave)
                    payload = payload[payload.find(b'SSbd', at) + 8:]
                else:
                    payload = payload[4:]   # 0xFF 0xA0 0x00 0x00 sub-stream header
                pcm += payload
            position += 6 + length
        else:
            position += 4
    if header is None:
        raise ValueError('PSS without audio: ' + str(path))
    rate, interleave = header
    frame = interleave * 2
    pcm = bytes(pcm[:len(pcm) // frame * frame])
    left, right = array('h'), array('h')
    for block in range(0, len(pcm), frame):
        left.frombytes(pcm[block:block + interleave])
        right.frombytes(pcm[block + interleave:block + frame])
    if sys.byteorder == 'big':
        left.byteswap()
        right.byteswap()
    out = array('h', bytes(len(pcm)))
    out[0::2], out[1::2] = left, right
    if sys.byteorder == 'big':
        out.byteswap()
    return out.tobytes(), rate


def find_ffmpeg(hint):
    if hint:
        return str(hint)
    found = shutil.which('ffmpeg')
    if found:
        return found
    base = Path.home()/'AppData/Local/Microsoft/WinGet/Packages'
    for candidate in sorted(base.glob('Gyan.FFmpeg*/**/bin/ffmpeg.exe')) if base.is_dir() else []:
        return str(candidate)
    raise SystemExit('ffmpeg is required to convert the LCS movies (pass --ffmpeg)')


def convert_movie(ffmpeg, pss, target, work):
    """MPEG-1/MP2 program stream (what SA's DirectShow player takes), 4:3, deinterlaced."""
    pcm, rate = pss_audio(pss)
    wav_path = work/(Path(pss).stem + '.wav')
    with wave.open(str(wav_path), 'wb') as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(pcm)
    target.parent.mkdir(parents=True, exist_ok=True)
    command = [ffmpeg, '-v', 'error', '-y', '-i', str(pss), '-i', str(wav_path),
               '-map', '0:v:0', '-map', '1:a:0', '-vf', 'yadif,scale=640:480:flags=lanczos,setsar=1',
               '-c:v', 'mpeg1video', '-b:v', '3500k', '-maxrate', '3500k', '-bufsize', '1835k', '-g', '15',
               '-c:a', 'mp2', '-b:a', '224k', '-ar', '44100', '-f', 'mpeg', str(target)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError('ffmpeg failed for %s: %s' % (pss, result.stderr))


def expected_files():
    """Relative paths build() writes."""
    names = ['models/txd/loadscs.txd', 'models/txd/load0uk.txd', 'models/txd/LOADSUK.txd', 'models/fronten2.txd',
             'movies/Logo.mpg', 'movies/GTAtitles.mpg']
    names += ['models/txd/loadsc%d.txd' % i for i in range(15)] + ['models/txd/splash%d.txd' % i for i in (1, 2, 3)]
    names += ['models/txd/%s.txd' % ('INTRO3' if i == 3 else 'intro%d' % i) for i in (1, 2, 3, 4)]
    return sorted(names)


def sha256(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def build(rom, game, output, ffmpeg=None):
    rom, game, output = Path(rom), Path(game), Path(output)
    Image = image_module()
    splash = rom/'SPLASH'
    loaded = {}

    def screen(name, size=None):
        if (name, size) not in loaded:
            width, height, rgb = read_chk(splash/(name + '.CHK'))
            if size and size != (width, height):
                rgb = Image.frombytes('RGB', (width, height), rgb).resize(size, Image.LANCZOS).tobytes()
                width, height = size
            loaded[name, size] = (width, height, rgb)
        return loaded[name, size]

    def native(texture, name, size=None):
        width, height, rgb = screen(name, size)
        return native_texture(texture, width, height, rgb)

    files = {}
    models = output/'models'
    # Loading screens: SA picks one of loadsc0..14, the LCS has four.
    art = ('LOADSC0', 'LOADSC1', 'LOADSC2', 'LOADSC3')
    loading = {'loadsc%d' % i: native('loadsc%d' % i, art[i % 4]) for i in range(15)}
    title = {'title_pc_EU': native('title_pc_EU', 'SCEALE', (1024, 1024)), 'title_pc_US': native('title_pc_US', 'SCEALE', (1024, 1024))}
    txd = game/'models/txd'
    files['models/txd/loadscs.txd'] = replace_textures(txd/'loadscs.txd', {**loading, **title})
    for i in range(15):
        files['models/txd/loadsc%d.txd' % i] = dictionary([loading['loadsc%d' % i]])
    files['models/txd/load0uk.txd'] = dictionary([native('load0uk', 'LOADSC0')])
    files['models/txd/LOADSUK.txd'] = replace_textures(txd/'LOADSUK.txd', {**{k: v for k, v in loading.items() if k != 'loadsc0'},
                                                       'loadscuk': native('loadscuk', 'LOADSC0')})
    for i in (1, 2, 3):
        files['models/txd/splash%d.txd' % i] = dictionary([native('splash%d' % i, 'SPLASH%d' % i)])
    for i in (1, 2, 3, 4):
        files['models/txd/%s.txd' % ('INTRO3' if i == 3 else 'intro%d' % i)] = dictionary([native('intro%d' % i, 'INTRO%d' % i)])
    # Menu: one picture across the 3x2 SA mosaic (six 512x512 tiles), dimmed like the PS2 front end.
    width, height, rgb = screen('LOADSC1')
    canvas = Image.frombytes('RGB', (width, height), rgb).resize((1536, 1024), Image.LANCZOS)
    canvas = canvas.point(lambda value: int(value * MENU_DIM + 0.5))
    tiles = {}
    for index, name in enumerate(MENU_TILES):
        left, top = index % 3 * 512, index // 3 * 512
        tiles[name] = native_texture(name, 512, 512, canvas.crop((left, top, left + 512, top + 512)).tobytes())
    files['models/fronten2.txd'] = replace_textures(game/'models/fronten2.txd', tiles)
    for relative, content in files.items():
        target = output/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
    work = output/'.work'
    work.mkdir(parents=True, exist_ok=True)
    try:
        encoder = find_ffmpeg(ffmpeg)
        for source, name in (('RSLEEDS', 'Logo'), ('DM', 'GTAtitles')):
            target = output/'movies'/(name + '.mpg')
            convert_movie(encoder, rom/'MOVIES'/(source + '.PSS'), target, work)
            files['movies/%s.mpg' % name] = None
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if sorted(files) != expected_files():
        raise RuntimeError('Screen output list out of sync with expected_files()')
    return [(relative, sha256(output/relative)) for relative in expected_files()]


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--rom', type=Path, required=True, help="the user's LCS PS2 disc contents (SPLASH, MOVIES)")
    parser.add_argument('--game', type=Path, required=True, help="the user's SA directory (dictionaries to patch)")
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ffmpeg', type=Path)
    args = parser.parse_args(argv)
    rows = build(args.rom, args.game, args.output, args.ffmpeg)
    print(json.dumps([dict(relative=relative, sha256=digest) for relative, digest in rows], indent=1))


if __name__ == '__main__':
    main(sys.argv[1:])
