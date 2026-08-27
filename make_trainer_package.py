from pathlib import Path
import hashlib
import struct

RAW = Path(r"artifacts\trainer-prototype\firmware.bin")
OUT = Path(r"tango2-ghst-usbtrainer-local.bin")

APP_OFFSET = 0xC000

# Same package identity as official Tango 2 FreedomTX v1.40 package
CONFIG = 0x00
HARDWARE_ID = 0x00040001
FIRMWARE_ID = 0x0128

raw = RAW.read_bytes()

if len(raw) <= APP_OFFSET:
    raise SystemExit("ERROR: firmware.bin is smaller than application offset")

app = raw[APP_OFFSET:]

# Verify Cortex-M vector table
initial_sp = int.from_bytes(app[0:4], "little")
reset_vector = int.from_bytes(app[4:8], "little")

if initial_sp != 0x10010000:
    raise SystemExit(
        f"ERROR: unexpected initial SP: 0x{initial_sp:08X}"
    )

if not (0x0800C000 <= reset_vector < 0x080C0000):
    raise SystemExit(
        f"ERROR: suspicious reset vector: 0x{reset_vector:08X}"
    )

# TBS package header:
# config:u8
# hardwareId:u32 big-endian
# firmwareId:u16 big-endian
# payloadLength:u32 big-endian
header = (
    bytes([CONFIG]) +
    struct.pack(">I", HARDWARE_ID) +
    struct.pack(">H", FIRMWARE_ID) +
    struct.pack(">I", len(app))
)

package_without_crc = header + app

# TBS CRC8:
# poly 0xD5
# init 0
# xorout 0
# non-reflected
crc = 0

for byte in package_without_crc:
    crc ^= byte
    for _ in range(8):
        if crc & 0x80:
            crc = ((crc << 1) ^ 0xD5) & 0xFF
        else:
            crc = (crc << 1) & 0xFF

package = package_without_crc + bytes([crc])

OUT.write_bytes(package)

print("Created:", OUT)
print("Raw firmware size:", len(raw))
print("Application offset: 0x%X" % APP_OFFSET)
print("Application size:", len(app), "= 0x%X" % len(app))
print("Initial SP: 0x%08X" % initial_sp)
print("Reset vector: 0x%08X" % reset_vector)
print("Hardware ID: 0x%08X" % HARDWARE_ID)
print("Firmware ID: 0x%04X" % FIRMWARE_ID)
print("Header:", header.hex(" "))
print("CRC8: 0x%02X" % crc)
print("Package size:", len(package))
print("Package SHA256:", hashlib.sha256(package).hexdigest().upper())
