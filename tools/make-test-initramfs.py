#!/usr/bin/env python3
"""Pack the handoff test's static /init and device nodes as a newc archive."""
import gzip
import stat
import sys
from pathlib import Path

if len(sys.argv) != 3:
    raise SystemExit(f"Usage: {sys.argv[0]} INIT OUTPUT.cpio.gz")

archive = bytearray()


def entry(name, mode, data=b"", device=(0, 0)):
    encoded = name.encode() + b"\0"
    fields = [1, mode, 0, 0, 1, 0, len(data), 0, 0, *device, len(encoded), 0]
    archive.extend(b"070701" + b"".join(f"{v:08x}".encode() for v in fields))
    archive.extend(encoded)
    archive.extend(bytes(-len(archive) % 4))
    archive.extend(data)
    archive.extend(bytes(-len(archive) % 4))


entry("dev", stat.S_IFDIR | 0o755)
entry("dev/console", stat.S_IFCHR | 0o600, device=(5, 1))
entry("dev/kmsg", stat.S_IFCHR | 0o600, device=(1, 11))
entry("init", stat.S_IFREG | 0o755, Path(sys.argv[1]).read_bytes())
entry("TRAILER!!!", 0)
Path(sys.argv[2]).write_bytes(gzip.compress(archive, mtime=0))
