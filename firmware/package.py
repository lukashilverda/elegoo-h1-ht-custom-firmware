"""Create a verified microSD update package."""

import hashlib
import struct
import sys
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

APP_BASE = 0x08008000
JOURNAL_START = 0x08047000
# Match the padded application length of the v1.2.5 microSD update.
UPDATE_SIZE = 262692
APP_NAME = "ELEGOO_DryerBox.bin"


def package(binary, output):
    image = binary.read_bytes()
    stack, reset = struct.unpack_from("<II", image)
    if not 0x20000000 < stack <= 0x20010000 or stack % 8:
        raise ValueError("Stack pointer outside SRAM or misaligned")
    if not reset & 1 or not APP_BASE <= (reset & ~1) < APP_BASE + len(image):
        raise ValueError("Reset vector outside the application image")
    if len(image) > JOURNAL_START - APP_BASE:
        raise ValueError("Application overlaps the settings journal")

    padded = image.ljust(UPDATE_SIZE, b"\xff")
    temp = output.with_suffix(".tmp")
    if binary.resolve() in (output.resolve(), temp.resolve()):
        raise ValueError("Use a separate package output path")
    with ZipFile(temp, "w") as archive:
        archive.writestr(ZipInfo(APP_NAME), padded, compress_type=ZIP_DEFLATED)
    with ZipFile(temp) as archive:
        if archive.namelist() != [APP_NAME] or archive.read(APP_NAME) != padded:
            raise ValueError("Invalid microSD ZIP")
    temp.replace(output)

    print(f"{output}: {len(image)} application bytes, {len(padded)} update bytes")
    print(f"SHA-256 ZIP {hashlib.sha256(output.read_bytes()).hexdigest()}")
    print(f"SHA-256 BIN {hashlib.sha256(padded).hexdigest()}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("Usage: package.py <firmware.bin> <update.zip>")
    package(*map(Path, sys.argv[1:]))
