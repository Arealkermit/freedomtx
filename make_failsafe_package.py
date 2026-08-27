from pathlib import Path
import hashlib
import struct

RAW = Path(r"artifacts\trainer-failsafe\firmware.bin")
OUT = Path(r"tango2-ghst-usbtrainer-failsafe-local.bin")

APP_OFFSET = 0xC000

CONFIG = 0x00
HARDWARE_ID = 0x00040001
FIRMWARE_ID = 0x0128

raw = RAW.read_bytes()
app = raw[APP_OFFSET:]

initial_sp = int.from_bytes(app[0:4], "little")
reset_vector = int.from_bytes(app[4:8], "little")

if initial_sp != 0x10010000:
    raise SystemExit(f"ERROR: unexpected SP 0x{initial_sp:08X}")

if not (0x0800C000 <= reset_vector < 0x080C0000):
    raise SystemExit(f"ERROR: suspicious reset vector 0x{reset_vector:08X}")

header = (
    bytes([CONFIG]) +
    struct.pack(">I", HARDWARE_ID) +
    struct.pack(">H", FIRMWARE_ID) +
    struct.pack(">I", len(app))
)

data = header + app

crc = 0
for byte in data:
    crc ^= byte
    for _ in range(8):
        if crc & 0x80:
            crc = ((crc << 1) ^ 0xD5) & 0xFF
        else:
            crc = (crc << 1) & 0xFF

package = data + bytes([crc])
OUT.write_bytes(package)

print("Created:", OUT)
print("Raw size:", len(raw))
print("App size:", len(app), "= 0x%X" % len(app))
print("Initial SP: 0x%08X" % initial_sp)
print("Reset vector: 0x%08X" % reset_vector)
print("Header:", header.hex(" "))
print("CRC8: 0x%02X" % crc)
print("Package size:", len(package))
print("SHA256:", hashlib.sha256(package).hexdigest().upper())
