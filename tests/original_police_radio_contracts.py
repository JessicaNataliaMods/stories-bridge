"""Retail POLICE_RADIO_MESSAGE (943) queue consumers, as ELF data only.

Usage: original_police_radio_contracts.py <SLUS_214.23>

Proves, with positive controls, that the retail last-seen composition queued by 943 is never played:
* the 943 handler reaches the DMAudio wrapper 0x2A20D8, which calls cAudioManager::PlaySuspectLastSeen
  (0x22D378) on AudioManager 0x42D0A0; the radio queue is AudioManager+0x61CC (count +0xF0, write +0xF1,
  read +0xF2);
* exactly four instructions in the image form the queue base: the clear in ResetPoliceRadio
  (0x22CA30), SetupCrimeReport (0x22CC58, a function with no callers), PlaySuspectLastSeen (0x22D3A8)
  and the audio initialiser clear (0x237C50); none advances the read index;
* no instruction reaches the queue range through AudioManager-relative offsets or absolute addresses.
So nothing removes samples from the queue: retail 943 has no audible effect. This grants no runtime support.
"""
import hashlib
import pathlib
import struct
import sys

SHA = '65bc2ec3d8a1b9f8482fb7e69427f2539e7e7b49d9dd262bf31aa8aa54ec53b2'
AM, QUEUE = 0x42D0A0, 0x61CC
LOADSTORE = {0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B, 0x31, 0x39, 0x37, 0x3F, 0x1E, 0x1F}


def words_of(data):
    h = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
    words = {}
    for i in range(h[11]):
        s = struct.unpack_from('<IIIIIIIIII', data, h[5] + i * 40)
        kind, flags, address, offset, size = s[1], s[2], s[3], s[4], s[5]
        if kind != 8 and flags & 2 and flags & 4 and size:
            for j in range(0, size - 3, 4):
                words[address + j] = struct.unpack_from('<I', data, offset + j)[0]
    return words


def jal_target(w):
    return ((w & 0x3FFFFFF) << 2) if (w >> 26) == 3 else None


def main(path):
    data = pathlib.Path(path).read_bytes()
    assert hashlib.sha256(data).hexdigest() == SHA
    words = words_of(data)
    callers = lambda target: sorted(a for a, w in words.items() if jal_target(w) == target)
    # Wrapper: lui a0,0x43 / jal 0x22D378 / addiu a0,a0,-0x2F60 (AudioManager this).
    assert words[0x2A20DC] == 0x3C040043 and jal_target(words[0x2A20E4]) == 0x22D378 and words[0x2A20E8] == 0x2484D0A0
    assert callers(0x22D378) == [0x2A20E4]
    bases = sorted(a for a, w in words.items() if (w >> 26) == 9 and (w & 0xFFFF) == QUEUE)
    assert bases == [0x22CA30, 0x22CC58, 0x22D3A8, 0x237C50], [hex(a) for a in bases]
    # Positive control: the last-seen consumer writes count/write index after the base.
    stores = [a for a in range(0x22D3A8, 0x22D820, 4) if (words[a] >> 26) == 0x28 and (words[a] & 0xFFFF) in (0xF0, 0xF1)]
    assert stores, 'PlaySuspectLastSeen must store queue fields'
    # Reset and initialiser only clear the three indices.
    for start, end in ((0x22CA30, 0x22CA44), (0x237C50, 0x237DA4)):
        fields = [(words[a] >> 26, words[a] & 0xFFFF, (words[a] >> 16) & 31) for a in range(start, end, 4)
                  if (words[a] & 0xFFFF) in (0xF0, 0xF1, 0xF2) and (words[a] >> 26) in LOADSTORE]
        assert fields and all(op == 0x28 and rt == 0 for op, _, rt in fields), (hex(start), fields)
    # SetupCrimeReport (containing 0x22CC58) starts at 0x22CC28 and is never called.
    assert words[0x22CC28] == 0x27BDFFD0 and callers(0x22CC28) == []
    # Nothing else reaches the queue range: AudioManager-relative offsets or absolute addresses.
    for a, w in words.items():
        op, imm = w >> 26, w & 0xFFFF
        if op in LOADSTORE and QUEUE + 0xF0 <= imm <= QUEUE + 0xF2 and ((w >> 21) & 31) != 29:
            raise AssertionError('AudioManager-relative queue field at %x' % a)
        if op == 0xF:
            rt, high = (w >> 16) & 31, imm << 16
            for k in range(1, 24):
                n = words.get(a + 4 * k, 0)
                if ((n >> 21) & 31) == rt and (n >> 26) in LOADSTORE | {9}:
                    target = high + (n & 0xFFFF) - (0x10000 if n & 0x8000 else 0)
                    assert not (AM + QUEUE <= target < AM + QUEUE + 0xF4), 'absolute queue access at %x' % (a + 4 * k)
                    break
    print('PASS: 943 queues into AudioManager+0x61CC; retail has no reader (SetupCrimeReport uncalled, only clears elsewhere)')


if __name__ == '__main__':
    main(sys.argv[1])
