#!/usr/bin/env python3
"""Builds retro_affine_test.c with retro_affine.c on the host and runs it: shaders that are a scale and an offset of
the diffuse color are found, shaders that read the specular, the fog or mix the components are not."""
import subprocess, sys, tempfile, os
here = os.path.dirname(os.path.abspath(__file__))
with tempfile.TemporaryDirectory() as tmp:
    exe = os.path.join(tmp, "t")
    subprocess.check_call(["gcc", "-O1", "-Wall", os.path.join(here, "retro_affine_test.c"),
                           os.path.join(here, "..", "src", "retro_affine.c"), "-lm", "-o", exe])
    run = subprocess.run([exe], capture_output=True, text=True)
    sys.stdout.write(run.stdout)
    sys.exit(run.returncode)
