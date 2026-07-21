#!/usr/bin/env bash
# Build the mask-ROM dumper end-to-end: cue sounds -> media header -> per-pen binaries -> dual-pen dumper.gme.
# Fully self-contained: our own api.h / sdk.c / *.ld (NO tt-homebrew, no external SDK), and make_gme.py
# synthesizes the GME header itself (NO reference .gme needed). The media header is regenerated from the
# same media list that builds the GME, so MEDIA_* names and indices can never drift.
# Requires: arm-none-eabi-gcc + python3 on PATH.
set -e
cd "$(dirname "$0")"
PRODUCT="${1:-42}"     # default 42 = the "taschenrechner" printout's product id (reuse that OID sheet)

# Our own start/done cue sounds: synthetic IMA-ADPCM WAV, generated here -- NO vendor audio is bundled.
python3 gen_sounds.py
python3 -c "import make_gme; open('gme_media.h','w').write(make_gme.media_header_text(['sounds/start.wav','sounds/done.wav']))"
mkdir -p build

# per-pen binaries -- one main.c, api.h picks the layout via -Dbuild_for_<pen>
arm-none-eabi-gcc -Dbuild_for_2N     -I . -T 2N.ld     -nostdlib -fno-builtin -fshort-wchar startup.s main.c -o build/2N.out
arm-none-eabi-objcopy -O binary build/2N.out build/2N.bin
arm-none-eabi-gcc -Dbuild_for_ZC3201 -I . -T ZC3201.ld -nostdlib -fno-builtin -fshort-wchar startup.s main.c -o build/ZC3201.out
arm-none-eabi-objcopy -O binary build/ZC3201.out build/ZC3201.bin

# dual-pen dumper GME with the two shipped sounds (no reference gme: header synthesized from constants)
python3 make_gme.py -o dumper.gme --product-id "$PRODUCT" \
    -b 2N=build/2N.bin -b ZC3201=build/ZC3201.bin \
    -m sounds/start.wav -m sounds/done.wav --media-header gme_media.h --verify
echo "OK -> dumper.gme (product_id=$PRODUCT)"
