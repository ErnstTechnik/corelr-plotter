# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""corelr_env.py -- the machine's tool paths for the Python tools: local.conf (see local.conf.example), the environment, auto-detection.

    import corelr_env                       # (tools put tools/lib on sys.path, see their header)
    corelr_env.avr_tool("avr-nm")           # a path, or the bare name to be found on the PATH
    corelr_env.get("CORELR_PORT", "/dev/ttyUSB0")
Precedence: the environment variable > local.conf > auto-detection > the default given.
"""
import glob, os, shutil

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
_conf = {}

def _load():
    p = os.path.join(ROOT, "local.conf")
    if not os.path.isfile(p): return
    for line in open(p, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line: continue
        k, v = (x.strip() for x in line.split("=", 1))
        if v == "~" or v.startswith("~/"): v = os.path.expanduser(v)
        _conf[k] = v
_load()

def get(key, default=None):
    return os.environ.get(key) or _conf.get(key) or default

def avr_bin():
    b = get("AVR_BIN")
    if b: return b
    if shutil.which("avr-gcc"): return None
    pats = ["/snap/arduino/current/hardware/tools/avr/bin",
            os.path.expanduser("~/snap/arduino/*/.arduino15/packages/arduino/tools/avr-gcc/*/bin"),
            os.path.expanduser("~/.arduino15/packages/arduino/tools/avr-gcc/*/bin")]
    for p in pats:
        for d in sorted(glob.glob(p), reverse=True):
            if os.path.exists(os.path.join(d, "avr-gcc")): return d
    return None

def avr_tool(name):
    d = avr_bin()
    return os.path.join(d, name) if d else name
