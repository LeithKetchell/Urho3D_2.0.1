#!/bin/bash
# Generate 4096-byte PAKE key from cosmic sources XOR-masked with UTC
# Sources: NOAA SWPC (no auth, no rate limit)
#   1. Interplanetary Magnetic Field
#   2. GOES Proton Flux
#   3. Solar Wind Plasma
OUT=/etc/urho3d/pake.key
UTC=$(date -u +%Y%m%d%H%M%S%N)
echo "UTC seed: $UTC"

echo "Fetching cosmic data from NOAA SWPC..."
echo "  1/3: Interplanetary Magnetic Field..."
curl -s --max-time 30 "https://services.swpc.noaa.gov/products/solar-wind/mag-1-day.json" > /tmp/pake_src1
if [ ! -s /tmp/pake_src1 ]; then echo "ERROR: IMF fetch failed"; exit 1; fi
echo "    OK"

echo "  2/3: GOES Proton Flux..."
curl -s --max-time 30 "https://services.swpc.noaa.gov/json/goes/primary/integral-protons-1-day.json" > /tmp/pake_src2
if [ ! -s /tmp/pake_src2 ]; then echo "ERROR: Proton fetch failed"; exit 1; fi
echo "    OK"

echo "  3/3: Solar Wind Plasma..."
curl -s --max-time 30 "https://services.swpc.noaa.gov/products/solar-wind/plasma-1-day.json" > /tmp/pake_src3
if [ ! -s /tmp/pake_src3 ]; then echo "ERROR: Plasma fetch failed"; exit 1; fi
echo "    OK"

echo "Generating key (UTC XOR cosmic)..."
# Concatenate all raw bytes, hash with UTC as XOR mask
cat /tmp/pake_src1 /tmp/pake_src2 /tmp/pake_src3 > /tmp/pake_raw

python3 - "$UTC" << 'EOF' > "$OUT"
import sys, hashlib

utc = sys.argv[1].encode()
with open("/tmp/pake_raw", "rb") as f:
    raw = f.read()

# Hash cosmic bytes into 4096 bytes
cosmic = b""
seed = raw
while len(cosmic) < 4096:
    seed = hashlib.sha512(seed).digest()
    cosmic += seed
cosmic = cosmic[:4096]

# Hash UTC into 4096-byte mask
mask = b""
seed = utc
while len(mask) < 4096:
    seed = hashlib.sha512(seed).digest()
    mask += seed
mask = mask[:4096]

# XOR — single operation on 4096-byte integers
c = int.from_bytes(cosmic, 'big')
m = int.from_bytes(mask, 'big')
sys.stdout.buffer.write((c ^ m).to_bytes(4096, 'big'))
EOF

rm -f /tmp/pake_src1 /tmp/pake_src2 /tmp/pake_src3 /tmp/pake_raw
chmod 640 "$OUT"
SIZE=$(wc -c < "$OUT")
echo "Generated $SIZE bytes at $OUT"
if [ "$SIZE" -ne 4096 ]; then
    echo "WARNING: Expected 4096 bytes, got $SIZE"; exit 1
fi
echo "Done. Sources: IMF + Proton + Plasma, XOR masked with UTC $UTC"
