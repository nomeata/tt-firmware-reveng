#!/usr/bin/env python3
"""gen_sounds.py -- generate the dumper's own start/done cue sounds as IMA-ADPCM WAV.

The firmware decodes Ogg/Vorbis and IMA-ADPCM (RIFF/WAV) (see Firmware-Findings.md). IMA-ADPCM WAV
is trivially generatable in pure Python (no encoder library), so we ship our own short beeps rather
than borrowing samples from a content GME. Two distinct tones so start and finish are audibly
different. NOTE: in-emulator we can only verify the play_sound CALL (correct media index/offset);
actual audio decode is a hardware behaviour, so the WAV *acceptance* is unverified here (worst case:
a silent cue -- it cannot fault, play_sound just decodes bytes).
"""
import math, struct, os

_STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,
         130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,
         1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
         5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
         27086,29794,32767]
_IDX = [-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8]


def _enc_ima(pcm, predictor=0, index=0):
    """Encode int16 PCM samples to IMA-ADPCM nibbles (returns bytes, 2 samples/byte)."""
    out, cur, have = bytearray(), 0, False
    for s in pcm:
        step = _STEP[index]
        d = s - predictor
        code = 0
        if d < 0:
            code = 8; d = -d
        if d >= step: code |= 4; d -= step
        if d >= step >> 1: code |= 2; d -= step >> 1
        if d >= step >> 2: code |= 1
        diff = step >> 3
        if code & 4: diff += step
        if code & 2: diff += step >> 1
        if code & 1: diff += step >> 2
        predictor += -diff if code & 8 else diff
        predictor = max(-32768, min(32767, predictor))
        index = max(0, min(88, index + _IDX[code]))
        if not have:
            cur = code; have = True
        else:
            out.append(cur | (code << 4)); have = False
    if have:
        out.append(cur)
    return bytes(out)


def ima_adpcm_wav(freq_hz, ms=180, rate=8000):
    n = int(rate * ms / 1000)
    pcm = [int(9000 * math.sin(2 * math.pi * freq_hz * i / rate) *
               min(1.0, min(i, n - i) / (rate * 0.02))) for i in range(n)]     # short fade in/out
    # one block, block-header (predictor int16, index u8, reserved 0) + nibbles
    body = _enc_ima(pcm, 0, 0)
    block = struct.pack("<hBB", 0, 0, 0) + body
    block_align = len(block)
    samples_per_block = 1 + (len(body) * 2)
    fmt = struct.pack("<HHIIHHHH", 0x11, 1, rate, rate * block_align // samples_per_block,
                      block_align, 4, 2, samples_per_block)
    fact = struct.pack("<I", n)
    def chunk(tag, data): return tag + struct.pack("<I", len(data)) + data
    riff = b"WAVE" + chunk(b"fmt ", fmt) + chunk(b"fact", fact) + chunk(b"data", block)
    return b"RIFF" + struct.pack("<I", len(riff)) + riff


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "sounds")
    os.makedirs(out, exist_ok=True)
    for name, freq in (("start", 1200), ("done", 700)):
        w = ima_adpcm_wav(freq)
        open(os.path.join(out, name + ".wav"), "wb").write(w)
        assert w[:4] == b"RIFF" and w[8:12] == b"WAVE"
        print("wrote sounds/%s.wav (%d bytes, %d Hz IMA-ADPCM)" % (name, len(w), freq))


if __name__ == "__main__":
    main()
