from pathlib import Path
import hashlib
import struct

RAW = Path(r".\override-build\tango2-ghst-firmware\firmware.bin")
OUT = Path(r".\override-build\tango2-ghst-usbtrainer-override-local.bin")

APP_OFFSET = 0xC000

CONFIG = 0x00
HARDWARE_ID = 0x00040001
FIRMWARE_ID = 0x0128

def crc8_d5(data):
    crc = 0

    for byte in data:
        crc ^= byte

        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0xD5) & 0xFF
            else:
                crc = (crc << 1) & 0xFF

    return crc

raw = RAW.read_bytes()

if len(raw) <= APP_OFFSET:
    raise SystemExit("ERROR: firmware.bin is too small.")

app = raw[APP_OFFSET:]

initial_sp = struct.unpack_from("<I", app, 0)[0]
reset_vector = struct.unpack_from("<I", app, 4)[0]

print(f"Raw size:        {len(raw)}")
print(f"App size:        {len(app)} / 0x{len(app):X}")
print(f"Initial SP:      0x{initial_sp:08X}")
print(f"Reset vector:    0x{reset_vector:08X}")

if not (
    0x10000000 <= initial_sp <= 0x10020000
    or 0x20000000 <= initial_sp <= 0x20040000
):
    raise SystemExit("ERROR: Initial stack pointer looks wrong.")

if not (0x0800C001 <= reset_vector <= 0x08100000):
    raise SystemExit("ERROR: Reset vector is outside expected flash.")

if not (reset_vector & 1):
    raise SystemExit("ERROR: Reset vector is not a Thumb address.")

header = (
    bytes([CONFIG])
    + HARDWARE_ID.to_bytes(4, "big")
    + FIRMWARE_ID.to_bytes(2, "big")
    + len(app).to_bytes(4, "big")
)

package_without_crc = header + app
crc = crc8_d5(package_without_crc)

package = package_without_crc + bytes([crc])

OUT.write_bytes(package)

print()
print("Header:          " + header.hex(" ").upper())
print(f"CRC8-D5:         0x{crc:02X}")
print(f"Package size:    {len(package)}")
print("Package SHA256:  " + hashlib.sha256(package).hexdigest().upper())
print()
print("Created:")
print(OUT.resolve())
