#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Relocatable desktop adapter. Startup policy remains exclusively in run.py."""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import signal
import select
import subprocess
import sys

RESOLUTIONS = {"720": (1280, 720), "1080": (1920, 1080),
               "1440": (2560, 1440), "2160": (3840, 2160)}
SCALES = {"native": 1, "quality": 1.5, "balanced": 1.7, "performance": 2}


@contextmanager
def display_refresh(resources, cap, frame_generation=False):
    """The helper's stdin is a lease, closed even when the supervisor dies.

    CoreGraphics app-scoped changes also revert if the helper is killed. This
    never changes the display size/Retina scale or the user's permanent choice.
    """
    helper = resources / "DisplayMode"
    process = None
    try:
        if cap == "60" and helper.is_file():
            process = subprocess.Popen([str(helper)], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
            if select.select([process.stdout], [], [], 10)[0]:
                print("Display refresh: " + process.stdout.readline().decode(errors="replace").strip(), flush=True)
            else:
                print("Display refresh: helper timed out; retaining the session mode", flush=True)
                process.kill()
                process.wait()
        yield
    finally:
        if process is not None:
            process.stdin.close()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            remainder = process.stdout.read().decode(errors="replace").strip()
            if remainder:
                print("Display refresh: " + remainder, flush=True)
            process.stdout.close()


@contextmanager
def dynamic_display_refresh(resources,support,cap,frame_generation=False):
    import threading
    from contextlib import ExitStack
    stop=threading.Event()
    def watch():
        with ExitStack() as stack:
            active=False
            try:
                while not stop.is_set():
                    want=cap=="60"
                    try:
                        state=json.loads((support/"runtime-graphics.json").read_text())
                        want=state["cap"]==60
                    except (OSError,ValueError,KeyError): pass
                    if want!=active:
                        stack.close()
                        if want:stack.enter_context(display_refresh(resources,"60",False))
                        active=want
                    stop.wait(.15)
            except Exception as error:
                print("Live display refresh unavailable: "+str(error),flush=True)
    worker=threading.Thread(target=watch,name="DisplayModeLease",daemon=True);worker.start()
    try:yield
    finally:stop.set();worker.join(timeout=20)


def dimensions(resolution, upscaling):
    w, h = RESOLUTIONS[resolution]
    # Even, 16:9 dimensions keep all game half-resolution targets integral.
    units = max(1, round(h / SCALES[upscaling] / 18))
    return (w, h) if upscaling == "native" else (units * 32, units * 18)


def plan(resolution, upscaling, cap, frame_generation=False, live=False):
    w, h = dimensions(resolution, upscaling)
    engine = 240 if live else 60 if cap == "60" else 240
    render = [w,h]
    # Live changes can increase resolution as well as decrease it. Keep a logical
    # coordinate grid large enough for every output; the renderer allocates and
    # rasterizes physical targets at `render`, including integer reads/feedback.
    # Starting on a smaller logical grid loses texel positions when switching up.
    logical = list(RESOLUTIONS["2160"]) if live else render
    return {"profile": f"{logical[0]}x{logical[1]}-{engine}", "input": logical,
            "output": list(RESOLUTIONS[resolution]), "engine": engine,
            "vsync": True, "frame_generation": False, "retina": True,
            **({"live": True, "render": render, "resolution": resolution} if live else {})}


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


def environment(resources, support, game, selection, upscaling, cap, hud, language="en", prepared=None):
    out, source, manifest = prepared or stage(resources, support, selection)
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
    frame_generation = False
    config.write_text("\n".join(f"{k}={v}" for k, v in {
        "engine_fps": selection["engine"], "fps_limit": cap if cap == "60" else 0,
        "vsync": 1, "upscaler": "off", "preset": 0,
        "render_resolution": manifest["resolution_patch"],
        "live_resolution": 0, "output_res": "x".join(map(str, selection["output"])),
        "spatial_aa": "off", "deferred_readback": 1, "stale_readback": 1,
        "async_shaders": 1, "display_mode": "fullscreen",
        "show_fps": int(hud), "overlay_fps": 0, "overlay_frametime": 0, "overlay_upscaler": 0,
        "overlay_gpu": 0, "overlay_cpu": 0, "overlay_ram": 0, "overlay_vram": 0,
        "overlay_corner": 2, "overlay_scale": 75, "restart_unconfirmed": 0,
    }.items()) + "\n")
    env.update({"BB_JIT": "0", "GOW3_NATIVE_AOT_DIR": str(out),
        "GOW3_DATA_DIR": str(support), "GOW3_GAME_DIR": str(game),
        "GOW3_PROBE": str(runtime / "bin/gow3-probe-arm64"),
        "GOW3_RECOMP_LIB": str(source / "aot/libgow3-recompiled.dylib"),
        "GOW3_USER_DIR": str(user), "GOW3_GPU_USER_DIR": str(user),
        "GOW3_CONFIG": str(config), "GOW3_TIMEOUT": "0", "GOW3_MODS_ENABLED": "0",
        "GOW3_FRAMEGEN_MODE": "0", "GOW3_FULLSCREEN": "1", "GOW3_RETINA": "1", "GOW3_METAL_PACING_HOST": str(frameworks / "libgow3gpu.dylib"), "GOW3_FRAMEGEN": "1" if frame_generation else "0",
        "GOW3_SHADER_POLICY": "complete", "GOW3_SHADER_LOOKAHEAD": "1",
        "GOW3_LOGICAL_RENDER_SIZE": "x".join(map(str, selection["input"])),
        "GOW3_SCENE_DISCARD_RESAMPLE": "1",
        "GOW3_SWAPCHAIN_IMAGES": "3" if frame_generation else "2", "GOW3_METAL_PRESENT_INTERVAL":
        "0.016666666666666667" if cap == "60" and not frame_generation else "0",
        "VK_DRIVER_FILES": str(runtime / "MoltenVK_icd.json"),
        "DYLD_LIBRARY_PATH": str(frameworks), "MTL_HUD_ENABLED": "1" if hud else "0",
        "PYTHONDONTWRITEBYTECODE": "1", "PYTHONIOENCODING": "utf-8"})
    env.update(GOW3_HUD_DISPLAY_INFO="1" if hud else "0", GOW3_METALFX_PRESET=upscaling,
               GOW3_LAUNCHER_LANGUAGE=language)
    if selection.get("live"):
        env.update(GOW3_RUNTIME_CONTROLS="1", GOW3_RUNTIME_RESOLUTION=str(list(RESOLUTIONS).index(selection["resolution"])),
            GOW3_RUNTIME_CAP=cap, GOW3_RUNTIME_HUD="1" if hud else "0",
            GOW3_RUNTIME_STATE=str(support/"runtime-graphics.json"),
            GOW3_RUNTIME_COMMAND=str(support/"runtime-command.txt"),
            GOW3_METAL_PRESENT_INTERVAL="0", MTL_HUD_ENABLED="1",GOW3_SWAPCHAIN_IMAGES="3",
            GOW3_METALFX_MODE="spatial",GOW3_METALFX_OUTPUT="x".join(map(str,selection["output"])))
        (support/"runtime-graphics.json").unlink(missing_ok=True)
        (support/"runtime-command.txt").unlink(missing_ok=True)
    if frame_generation:
        env.update(GOW3_FRAMEGEN_INPUT_DIVISOR="2", GOW3_FRAMEGEN_COLOR_DIVISOR="2")
    if upscaling != "native":
        env.update(GOW3_METALFX_MODE="spatial", GOW3_METALFX_OUTPUT="x".join(map(str, selection["output"])))
    return out, env


def prepare_graphics_catalog(resources, support, game, env):
    """Asset indexing + bundled render descriptions, independent of CPU AOT.

    Failure falls back to complete on-demand rendering, never skipped draws.
    The game backend performs actual Metal compilation on this Mac at startup.
    """
    catalog = resources / "ShaderCatalog"
    seed = resources / "CommunityCatalog.jsonl"
    manifest_path = catalog / "manifest.json"
    if not seed.is_file() and not manifest_path.is_file():
        return None
    try:
        import hashlib
        import time
        sys.path.insert(0, str(resources))
        if seed.is_file():
            from community_catalog import load, materialize, migrate_descriptions, save
            reference = load(seed)
            abi = {r["abi"] for r in reference.values()}
            sources = [seed]
            # Only the signed official feed is loaded as third-party data.
            # Manual imports stay on disk, but are not silently trusted by this build.
            try:
                from catalog_service import verified_official
                official = verified_official(resources, support)
                if official:
                    sources.append(official)
            except (ImportError, OSError, ValueError, subprocess.SubprocessError) as error:
                print("Official catalogue unavailable; bundled/local data retained", flush=True)
            candidates = [support / "Community/contribution.jsonl"]
            candidates += sorted((support / "Community/contributions").glob("*.jsonl"))
            for source in candidates:
                if not source.exists():
                    continue
                try:
                    rows = load(source)
                    if {r["abi"] for r in rows.values()} != abi:
                        rows = migrate_descriptions(rows, next(iter(abi)), reference)
                        upgraded = support / "Community/migrated" / (hashlib.sha256(source.read_bytes()).hexdigest()+".jsonl")
                        save(upgraded, rows)
                        print("Community descriptions retained for local shader rebuild: " + source.name, flush=True)
                        source = upgraded
                    sources.append(source)
                except (OSError, ValueError, KeyError) as error:
                    print("Community catalogue ignored: " + source.name + ": " + str(error), flush=True)
            catalog = support / "User/cache/CUSA01623/CommunityCatalog"
            report = materialize(sources, catalog)
            recording = support / "Community/recording" / os.urandom(12).hex()
            recording.mkdir(parents=True, exist_ok=True)
            env["GOW3_COMMUNITY_RECORD_DIR"] = str(recording)
            env["GOW3_COMMUNITY_RECORD_OUTPUT"] = str(support / "Community/contributions" / (next(iter(abi))+".jsonl"))
            print("Community graphics catalogue: " + json.dumps({k:v for k,v in report.items() if k!="files"}), flush=True)
        else:
            manifest = json.loads(manifest_path.read_text())
            files = manifest["files"]
            if manifest.get("format") != 1 or not files or len(files) > 65536:
                raise ValueError("Shader description manifest")
            if {p.name for p in catalog.iterdir()} != set(files) | {"manifest.json"}:
                raise ValueError("Unlisted shader description")
            total = 0
            for name, expected in files.items():
                if Path(name).name != name or (Path(name).suffix not in (".recipe", ".pso") and name not in ("compatibility.txt", "binary-fingerprints.tsv")):
                    raise ValueError("Shader description filename")
                path = catalog / name
                if path.is_symlink():
                    raise ValueError("Redirected shader description")
                total += path.stat().st_size
                if total > 32 * 1024**2 or path.stat().st_size > 1024**2:
                    raise ValueError("Shader description size")
                if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                    raise ValueError("Shader description checksum: " + name)
        from shader_catalog import build
        last = [0.0]
        def progress(done, total):
            now = time.monotonic()
            if done != total and now-last[0] < 0.25:
                return
            last[0] = now
            temporary = support / "preparation-status.json.tmp"
            temporary.write_text(json.dumps({"phase": "graphics_index", "fraction": done/total,
                                            "completed": done, "total": total, "updated": time.time()}))
            os.replace(temporary, support / "preparation-status.json")
        progress(0, 1)
        output = support / "User/cache/CUSA01623/AssetIndex"
        result = build(game, output, progress)
        env.update(GOW3_SHADER_ASSET_INDEX=str(output / "programs.tsv"),
                   GOW3_SHADER_CATALOG=str(catalog))
        print("Graphics asset preparation: " + json.dumps(result), flush=True)
        return result
    except (ValueError, OSError, KeyError, ImportError) as error:
        print("Graphics catalog unavailable; retaining complete required rendering: " + str(error), flush=True)
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--resources", required=True, type=Path)
    parser.add_argument("--support", required=True, type=Path)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--resolution", choices=RESOLUTIONS, required=True)
    parser.add_argument("--upscaling", choices=SCALES, required=True)
    parser.add_argument("--cap", choices=("60", "0"), required=True)
    parser.add_argument("--shortcuts",choices=("0","1"),default="1")
    parser.add_argument("--framegen",choices=("0","1","2","3"),default="0", help=argparse.SUPPRESS)
    parser.add_argument("--hud", choices=("0", "1"), default="1")
    parser.add_argument("--language", choices=("en", "pt-BR", "es"), default="en")
    parser.add_argument("--textures", choices=("original", "2x"), default="original")
    parser.add_argument("--prepare-only", action="store_true", help="Validate first-run preparation without starting the game")
    a = parser.parse_args()
    resources, support, game = a.resources.resolve(), a.support.resolve(), a.game.resolve()
    sys.path.insert(0, str(resources))
    support.mkdir(parents=True, exist_ok=True)
    selection = plan(a.resolution, a.upscaling, a.cap, a.framegen!="0", live=True)
    print("Desktop launch: " + json.dumps(selection), flush=True)
    sys.path.insert(0, str(resources / "Runtime/scripts"))
    from native_launch import sha256
    # One lock covers all resolutions and app copies, before modifying shared saves.
    import fcntl
    with (support / "session.lock").open("a+") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError("ALREADY_RUNNING") from None
        def interrupted(_number, _frame):
            raise KeyboardInterrupt
        signal.signal(signal.SIGINT, interrupted)
        signal.signal(signal.SIGTERM, interrupted)
        prepared = None
        community = (resources / "supported-game.json").exists()
        try:
            if community:
                from community import ensure_profile, status, PreparationError
                prepared = ensure_profile(resources, support, game, selection)
            else:
                source = resources / "Profiles" / selection["profile"]
                analysis = json.loads((source / "analysis.json").read_text())
                if sha256(game / "eboot.bin") != analysis["source_sha256"]:
                    raise ValueError("DUMP_VERSION: This local build requires the same executable used for its ARM64 preparation (CUSA01623 v1.02).")
        except KeyboardInterrupt:
            if community:
                status(support, "cancelled")
            return 130
        except Exception as error:
            if community:
                code = error.code if isinstance(error, PreparationError) else "prepare_failed"
                status(support, "failed", error_code=code, detail=str(error))
            raise
        if a.prepare_only:
            return 0
        out, env = environment(resources, support, game, selection, a.upscaling, a.cap, a.hud == "1", a.language, prepared)
        env["GOW3_FRAMEGEN_MODE"] = "0"
        env["GOW3_SHORTCUTS"] = a.shortcuts
        try:
            prepare_graphics_catalog(resources, support, game, env)
        except KeyboardInterrupt:
            return 130
        if a.textures == "2x":
            texture_pack = resources / "TexturePack"
            if not texture_pack.is_dir() or not any(texture_pack.glob("*.g3tx")):
                raise ValueError("The local experimental texture pack is missing")
            import hashlib
            manifest = json.loads((texture_pack / "manifest.json").read_text())
            files = manifest["files"]
            if manifest.get("version") not in (1,2) or not files or len(files) > 2048:
                raise ValueError("Invalid texture pack manifest")
            if set(files) != {p.name for p in texture_pack.glob("*.g3tx")}:
                raise ValueError("Texture pack file list changed")
            total = 0
            for name, expected in files.items():
                if Path(name).name != name or not name.endswith(".g3tx"):
                    raise ValueError("Invalid texture pack filename")
                path = texture_pack / name
                total += path.stat().st_size
                if path.stat().st_size > 128 * 1024**2 or total > 4 * 1024**3:
                    raise ValueError("Texture pack exceeds the experimental pack limit")
                with path.open("rb") as stream:
                    actual = hashlib.file_digest(stream, "sha256").hexdigest()
                if actual != expected:
                    raise ValueError("Texture pack checksum mismatch: " + name)
            env["GOW3_TEXTURE_PACK"] = str(texture_pack)
        print("Texture sampling: " + a.textures, flush=True)
        (support / "last-session.json").write_text(json.dumps({**selection, "profile_directory": str(out)}, indent=2))
        # Let the UI stop only the process belonging to this session.
        (support / "active-profile.txt").write_text(str(out))
        try:
            with dynamic_display_refresh(resources,support,a.cap,selection["frame_generation"]):
                child = subprocess.Popen([sys.executable, "-B", str(resources / "Runtime/run.py")],
                                         env=env, start_new_session=True)
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
            recording = env.get("GOW3_COMMUNITY_RECORD_DIR")
            if recording:
                try:
                    from community_catalog import collect
                    report = collect(Path(recording), Path(env["GOW3_COMMUNITY_RECORD_OUTPUT"]))
                    shutil.rmtree(recording)
                    print("Local community discoveries: " + json.dumps(report), flush=True)
                except (OSError, ValueError, KeyError) as error:
                    print("Community contribution unavailable: " + str(error), flush=True)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"LAUNCH_ERROR: {error}", file=sys.stderr, flush=True)
        sys.exit(1)
