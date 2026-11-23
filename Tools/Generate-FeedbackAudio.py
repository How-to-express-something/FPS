"""Original deterministic DSP placeholders; no samples or neural audio model used."""
from pathlib import Path
import math
import random
import struct
import wave

root = Path(__file__).resolve().parent.parent
out = root / 'SourceAssets' / 'Audio'
out.mkdir(parents=True, exist_ok=True)
rate = 48000
for name, length, notes in [
    ('S_HitBody', .085, [(0., 650., .07)]),
    ('S_HitHead', .13, [(0., 1450., .08), (.035, 2050., .085)]),
    ('S_HitKill', .24, [(0., 900., .12), (.055, 1350., .14), (.105, 1800., .13)]),
]:
    rng = random.Random(1729)
    samples = []
    for i in range(int(rate * length)):
        t = i / rate
        value = 0.
        for start, frequency, duration in notes:
            u = t - start
            if 0 <= u < duration:
                envelope = min(1., u / .0025) * (1. - u / duration) ** 3
                value += envelope * (math.sin(2 * math.pi * frequency * u) + .2 * math.sin(2 * math.pi * frequency * 2.01 * u))
        if t < .012:
            value += rng.uniform(-1, 1) * .25 * (1 - t / .012) ** 2
        samples.append(value)
    peak = max(abs(x) for x in samples)
    pcm = b''.join(struct.pack('<h', round(x / peak * .25 * 32767)) for x in samples)
    with wave.open(str(out / (name + '.wav')), 'wb') as stream:
        stream.setnchannels(1)
        stream.setsampwidth(2)
        stream.setframerate(rate)
        stream.writeframes(pcm)
    print(f'{name}: {length:.3f}s, 48 kHz mono PCM16, peak -12 dBFS')
