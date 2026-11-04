#!/usr/bin/env python3
"""Builds p2p_selftest.c with p2p_crypto.c on the host and runs it: the tunnel's SHA-256, HMAC, ChaCha20-Poly1305,
X25519 and Ed25519 against the RFCs' vectors (the same code halo.exe --selftest runs on the target)."""
import subprocess, sys, tempfile, os
here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, "..", "src")
mono = os.path.join(here, "..", "..", "third_party", "monocypher")
with tempfile.TemporaryDirectory() as tmp:
    exe = os.path.join(tmp, "t")
    cc = ["gcc", "-O1", "-I" + src, "-I" + mono, os.path.join(here, "p2p_selftest_test.c"),
          os.path.join(src, "p2p_selftest.c"), os.path.join(src, "p2p_crypto.c"),
          os.path.join(mono, "monocypher.c"), os.path.join(mono, "monocypher-ed25519.c"), "-o", exe]
    subprocess.check_call(cc)
    run = subprocess.run([exe], capture_output=True, text=True)
    sys.stdout.write(run.stdout)
    sys.exit(run.returncode)
