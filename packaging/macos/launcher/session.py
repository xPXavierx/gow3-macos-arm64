#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Relocatable desktop adapter. Startup policy remains exclusively in run.py."""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys

RESOLUTIONS = {"720": (1280, 720), "1080": (1920, 1080),
               "1440": (2560, 1440), "2160": (3840, 2160)}
SCALES = {"native": 1, "quality": 1.5, "balanced": 1.7, "performance": 2}


def dimensions(resolution, upscaling):
    w, h = RESOLUTIONS[resolution]
    # Even, 16:9 dimensions keep all game half-resolution targets integral.
    units = max(1, round(h / SCALES[upscaling] / 18))
    return (w, h) if upscaling == "native" else (units * 32, units * 18)


def plan(resolution, upscaling, cap):
    w, h = dimensions(resolution, upscaling)
    engine = 60 if cap == "60" else 240
    return {"profile": f"{w}x{h}-{engine}", "input": [w, h],
            "output": list(RESOLUTIONS[resolution]), "engine": engine,
            "vsync": True, "frame_generation": False, "retina": True}


def stage(resources, support, selection):
    source = resources / "Profiles" / selection["profile"]
    manifest = json.loads((source / "profile.json").read_text())
    if manifest["input"] != selection["input"] or manifest["engine"] != selection["engine"]:
        raise ValueError("Profile manifest mismatch")
    destination = support / "Profiles" / selection["profile"]
    destination.mkdir(parents=True, exist_ok=True)
    # Copy just immutable loader inputs; runtime writes never enter the app bundle.
    for name in ("boot-linked.bin", "analysis.json", "link.json", "native-inputs.json", "aot-patches.json"):
        src, dst = source / name, destination / name
        if not dst.exists() or dst.stat().st_size != src.stat().st_size or dst.stat().st_mtime_ns != src.stat().st_mtime_ns:
            temporary = dst.with_suffix(dst.suffix + ".tmp")
            shutil.copy2(src, temporary)
            os.replace(temporary, dst)
    return destination, source, manifest


def environment(resources, support, game, selection, upscaling, cap, hud, language="pt-BR"):
    out, source, manifest = stage(resources, support, selection)
    # Finder starts with a small environment; command-line launches must not
    # inherit old experiments, injected profilers or another game's settings.
    env = {k: v for k, v in os.environ.items() if not k.startswith(
        ("GOW3_", "BB_", "MVK_", "VK_", "MTL_", "DYLD_", "PYTHON"))}
    runtime = resources / "Runtime"
    sys.path.insert(0, str(runtime / "scripts"))
    from link_modules import module
    # The shared patch compiler needs the original ELF for signature matching.
    # Derive it from the selected, hash-verified dump; keep it in writable data.
    (out / "eboot.elf").write_bytes(module(game / "eboot.bin")["elf"])
    frameworks = resources.parent / "Frameworks"
    user = support / "User"
    user.mkdir(exist_ok=True)
    config = out / "display.ini"
    config.write_text("\n".join(f"{k}={v}" for k, v in {
        "engine_fps": selection["engine"], "fps_limit": cap if cap == "60" else 0,
        "vsync": 1, "upscaler": "off", "preset": 0,
        "render_resolution": manifest["resolution_patch"],
        "live_resolution": 0, "output_res": "x".join(map(str, selection["output"])),
        "spatial_aa": "off", "deferred_readback": 1, "stale_readback": 1,
        "async_shaders": 1, "display_mode": "fullscreen",
        "show_fps": 1, "overlay_fps": 0, "overlay_frametime": 0, "overlay_upscaler": 0,
        "overlay_gpu": 0, "overlay_cpu": 0, "overlay_ram": 0, "overlay_vram": 0,
        "overlay_corner": 2, "overlay_scale": 75, "restart_unconfirmed": 0,
    }.items()) + "\n")
    env.update({"BB_JIT": "0", "GOW3_NATIVE_AOT_DIR": str(out),
        "GOW3_DATA_DIR": str(support), "GOW3_GAME_DIR": str(game),
        "GOW3_PROBE": str(runtime / "bin/gow3-probe-arm64"),
        "GOW3_RECOMP_LIB": str(source / "aot/libgow3-recompiled.dylib"),
        "GOW3_USER_DIR": str(user), "GOW3_GPU_USER_DIR": str(user),
        "GOW3_CONFIG": str(config), "GOW3_TIMEOUT": "0", "GOW3_MODS_ENABLED": "0",
        "GOW3_FULLSCREEN": "1", "GOW3_RETINA": "1", "GOW3_FRAMEGEN": "0",
        "GOW3_SWAPCHAIN_IMAGES": "2", "GOW3_METAL_PRESENT_INTERVAL":
        "0.016666666666666667" if cap == "60" else "0",
        "VK_DRIVER_FILES": str(runtime / "MoltenVK_icd.json"),
        "DYLD_LIBRARY_PATH": str(frameworks), "MTL_HUD_ENABLED": "1" if hud else "0",
        "PYTHONDONTWRITEBYTECODE": "1", "PYTHONIOENCODING": "utf-8"})
    env.update(GOW3_HUD_DISPLAY_INFO="1", GOW3_METALFX_PRESET=upscaling,
               GOW3_LAUNCHER_LANGUAGE=language)
    if upscaling != "native":
        env.update(GOW3_METALFX_MODE="spatial", GOW3_METALFX_OUTPUT="x".join(map(str, selection["output"])))
    return out, env


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--resources", required=True, type=Path)
    parser.add_argument("--support", required=True, type=Path)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--resolution", choices=RESOLUTIONS, required=True)
    parser.add_argument("--upscaling", choices=SCALES, required=True)
    parser.add_argument("--cap", choices=("60", "0"), required=True)
    parser.add_argument("--hud", choices=("0", "1"), default="1")
    parser.add_argument("--language", choices=("en", "pt-BR", "es"), default="pt-BR")
    a = parser.parse_args()
    resources, support, game = a.resources.resolve(), a.support.resolve(), a.game.resolve()
    support.mkdir(parents=True, exist_ok=True)
    selection = plan(a.resolution, a.upscaling, a.cap)
    print("Desktop launch: " + json.dumps(selection), flush=True)
    sys.path.insert(0, str(resources / "Runtime/scripts"))
    from native_launch import sha256
    source = resources / "Profiles" / selection["profile"]
    analysis = json.loads((source / "analysis.json").read_text())
    if sha256(game / "eboot.bin") != analysis["source_sha256"]:
        raise ValueError("DUMP_VERSION: This local build requires the same executable used for its ARM64 preparation (CUSA01623 v1.02).")
    # One lock covers all resolutions and app copies, before modifying shared saves.
    import fcntl
    with (support / "session.lock").open("a+") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError("ALREADY_RUNNING") from None
        out, env = environment(resources, support, game, selection, a.upscaling, a.cap, a.hud == "1", a.language)
        (support / "last-session.json").write_text(json.dumps({**selection, "profile_directory": str(out)}, indent=2))
        # Let the UI stop only the process belonging to this session.
        (support / "active-profile.txt").write_text(str(out))
        try:
            child = subprocess.Popen([sys.executable, "-B", str(resources / "Runtime/run.py")],
                                     env=env, start_new_session=True)
            def interrupted(_number, _frame):
                raise KeyboardInterrupt
            signal.signal(signal.SIGTERM, interrupted)
            try:
                return child.wait()
            except KeyboardInterrupt:
                # Includes Stop during preparation: don't leave an orphaned
                # supervisor/game behind if no game PID has been written yet.
                if child.poll() is None:
                    os.killpg(child.pid, signal.SIGINT)
                return child.wait()
        finally:
            (support / "active-profile.txt").unlink(missing_ok=True)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"LAUNCH_ERROR: {error}", file=sys.stderr, flush=True)
        sys.exit(1)
