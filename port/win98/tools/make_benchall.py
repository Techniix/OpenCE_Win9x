#!/usr/bin/env python3
"""Writes BENCHALL.BAT: runs the Windows 9x build through many settings, one after
another with nobody at the machine, each run a named line in BENCH.TXT.

    python3 port/win98/tools/make_benchall.py OUT/BENCHALL.BAT [--seconds 40]

Each run sets the settings as environment variables (port_config.c's names: they
are read when the game starts and never touch config.toml), asks for a frame
budget report every 10 seconds (debug.frame_budget) and a name (debug.bench_label),
and ends itself after the run's seconds (debug.exit_after). Copy BENCH.TXT and
HALO.log back. The values of the four presets are those of
port/win98/launcher/win9x_launcher_logic.c's launcher_preset (the governor is
off in them, so a run is one setting from start to end; one run has it on).
These runs are in the menu's backdrop: they measure the renderer, the clock and
the sound; the game's own simulation needs a run in a level (see the README).
"""
import argparse

def env_names():
    """The environment variable of each key, read from port_config.c, so a name cannot be misspelled here."""
    import re, os
    path = os.path.join(os.path.dirname(__file__), "..", "..", "linux", "src", "port_config.c")
    return dict(re.findall(r'\{ "([a-z_.]+)", _config_\w+, [^\n]*?"(HALO_[A-Z_0-9]+)", _environment_value', open(path).read()))

# halo_render_stages.h's lists, read so the batch cannot drift
def skip_lists():
    import re, os
    path = os.path.join(os.path.dirname(__file__), "..", "..", "linux", "include", "halo_render_stages.h")
    text = open(path).read().replace("\\\n", " ")
    shiny = re.search(r'#define HALO_SKIP_SHINY\s+"([^"]*)"', text).group(1)
    some = re.search(r'#define HALO_SKIP_SOME HALO_SKIP_SHINY\s+"([^"]*)"', text).group(1)
    most = re.search(r'#define HALO_SKIP_MOST HALO_SKIP_SOME\s+"([^"]*)"', text).group(1)
    return {"": "", "shiny": shiny, "some": shiny + some, "most": shiny + some + most}

SOUND = {  # launcher_logic's sound stops: rate, resampler, sinc voices, buffer frames
    "low":    (22050, "linear", 0, 1024),
    "medium": (32000, "auto", 3, 1024),
    "high":   (44100, "auto", 8, 768),
    "best":   (48000, "sinc", 6, 512),
}

def runs():
    skip = skip_lists()
    base = dict(scale="native", lod="1.00", skip="", vis="portals", tex=0, keep=0, fps=-1, sound="best", reverb="true")
    presets = {
        "fastest":  dict(scale="50%", lod="0.25", skip=skip["most"], vis="pvs", tex=2, fps=30, sound="low", reverb="false"),
        "fast":     dict(scale="75%", lod="0.50", skip=skip["some"], tex=1, fps=30, sound="medium", reverb="false"),
        "balanced": dict(scale="native", lod="0.75", skip=skip["shiny"], fps=60, sound="high"),
        "best":     dict(),
    }
    out = []
    for name, over in presets.items():
        out.append(("preset-" + name, dict(base, **over)))
    # one thing at a time, from the Best settings
    for tex in (1, 2, 3):
        out.append(("texture-%d" % tex, dict(base, tex=tex)))
    for scale in ("25%", "50%", "75%"):
        out.append(("scale-" + scale, dict(base, scale=scale)))
    for lod in ("0.25", "0.50"):
        out.append(("lod-" + lod, dict(base, lod=lod)))
    for key in ("shiny", "some", "most"):
        out.append(("skip-" + key, dict(base, skip=skip[key])))
    out.append(("visibility-pvs", dict(base, vis="pvs")))
    for stop in SOUND:
        out.append(("sound-" + stop, dict(base, sound=stop)))
    out.append(("sound-best-noreverb", dict(base, reverb="false")))
    out.append(("governor-25", dict(base, keep=25, lod="1.00")))
    return out

def write(path, seconds):
    lines = [
        "@echo off",
        "rem Written by make_benchall.py: runs many settings one after another, each a line in BENCH.TXT.",
        "rem About %d minutes. Leave the machine alone; copy BENCH.TXT and HALO.log when it says done." % 0,
        "echo Benchmark batch: leave the machine alone until it says done.",
        "if exist BENCH.TXT del BENCH.TXT",
    ]
    names = env_names()
    lines += [
        "set %s=true" % names["launcher.configured"],
        "set %s=10" % names["debug.frame_budget"],
        "set %s=%d" % (names["debug.exit_after"], seconds),
    ]
    names = env_names()
    all_runs = runs()
    for label, r in all_runs:
        rate, resampler, voices, buffer = SOUND[r["sound"]]
        values = [
            ("debug.bench_label", label),
            ("display.resolution_scaling", r["scale"].replace("%", "%%")),
            ("render.lod_scale", r["lod"]),
            ("render.skip_passes", r["skip"]),
            ("render.visibility", r["vis"]),
            ("render.texture_detail", r["tex"]),
            ("render.adaptive_fps", r["keep"]),
            ("display.max_fps", r["fps"]),
            ("audio.sample_rate", rate),
            ("audio.resampler", resampler),
            ("audio.sinc_voices", voices),
            ("audio.buffer_frames", buffer),
            ("audio.reverb", r["reverb"]),
        ]
        lines += ["", "echo %s" % label]
        lines += ["set %s=%s" % (names[k], v) for k, v in values]
        lines.append("halo.exe --no-setup")
    lines += ["", "echo done. Copy BENCH.TXT and HALO.log.", "pause", ""]
    minutes = len(all_runs) * (seconds + 15) / 60.0
    lines[2] = "rem About %d minutes (%d runs of %d seconds, and the game's start each). Leave the machine alone; copy BENCH.TXT and HALO.log when it says done." % (
        round(minutes), len(all_runs), seconds)
    with open(path, "w", newline="") as f:
        f.write("\r\n".join(lines))
    return len(all_runs)

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--seconds", type=int, default=40)
    a = ap.parse_args()
    print("%d runs written to %s" % (write(a.out, a.seconds), a.out))
