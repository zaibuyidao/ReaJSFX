"""Test Windows x64 VST2 resource lookup through audio output.

Each case loads a temporary DLL copy in a separate process with process-local,
read-only registry redirection.
Usage: python scripts/test-vst2-paths.py path/to/reajsfx.dll [other.dll ...]
"""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def probe(dll):
    import winreg
    registry = C.WinDLL("advapi32", use_last_error=True)
    override = registry.RegOverridePredefKey
    override.argtypes = [C.c_void_p, C.c_void_p]
    override.restype = C.c_long
    key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment", 0, winreg.KEY_READ)
    try:
        unexpected = winreg.OpenKey(key, "Software\\REAPER")
    except FileNotFoundError:
        pass
    else:
        unexpected.Close()
        key.Close()
        raise RuntimeError("Cannot isolate registry lookup: Environment has Software/REAPER")
    redirected = []
    try:
        for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
            result = override(hive, int(key))
            if result:
                raise OSError(result, "RegOverridePredefKey failed")
            redirected.append(hive)

        ptr, i32, iptr = C.c_void_p, C.c_int32, C.c_ssize_t
        callback_type = C.CFUNCTYPE(iptr, ptr, i32, i32, iptr, ptr, C.c_float)
        dispatcher_type = callback_type
        floats = C.POINTER(C.c_float)
        process_type = C.CFUNCTYPE(None, ptr, C.POINTER(floats), C.POINTER(floats), i32)

        class Effect(C.Structure):
            _fields_ = [("magic", i32), ("dispatcher", dispatcher_type),
                        ("process", ptr), ("setParameter", ptr), ("getParameter", ptr),
                        ("numPrograms", i32), ("numParams", i32),
                        ("numInputs", i32), ("numOutputs", i32), ("flags", i32),
                        ("reserved1", ptr), ("reserved2", ptr), ("initialDelay", i32),
                        ("realQualities", i32), ("offQualities", i32), ("ioRatio", C.c_float),
                        ("object", ptr), ("user", ptr), ("uniqueID", i32), ("version", i32),
                        ("processReplacing", process_type)]

        @callback_type
        def host(effect, opcode, index, value, pointer, opt):
            return {1: 2400, 16: 48000, 17: 64, 22: 1, 23: 1}.get(opcode, 0)

        library = C.CDLL(str(dll))
        entry = getattr(library, "VSTPluginMain", None) or getattr(library, "main")
        entry.argtypes = [callback_type]
        entry.restype = C.POINTER(Effect)
        effect = entry(host)
        if not effect or effect.contents.magic != 0x56737450:
            raise RuntimeError("Invalid VST2 entry result")
        dispatch = effect.contents.dispatcher
        try:
            for op, value, opt in ((0, 0, 0), (10, 0, 48000), (11, 64, 0), (12, 1, 0)):
                dispatch(effect, op, 0, value, None, opt)
            if (effect.contents.numInputs, effect.contents.numOutputs) != (2, 2):
                raise RuntimeError("Expected stereo test instance")
            inputs = [(C.c_float * 64)() for _ in range(2)]
            outputs = [(C.c_float * 64)() for _ in range(2)]
            in_ptrs = (floats * 2)(*inputs)
            out_ptrs = (floats * 2)(*outputs)
            effect.contents.processReplacing(effect, in_ptrs, out_ptrs, 64)
            actual = [float(outputs[c][0]) for c in range(2)]
            # Left proves Effects/default-effect lookup; right proves Data lookup.
            passed = all(abs(outputs[c][i] - target) < 1e-6
                         for c, target in enumerate((0.25, 0.375)) for i in range(64))
            print(json.dumps({"pass": passed, "output": actual, "expected": [0.25, 0.375]}))
            return 0 if passed else 1
        finally:
            dispatch(effect, 12, 0, 0, None, 0)
            dispatch(effect, 1, 0, 0, None, 0)
    finally:
        for hive in reversed(redirected):
            override(hive, None)
        key.Close()


def run(dlls):
    failures = 0
    scratch = Path(__file__).resolve().parents[1] / "build" / "path-tests"
    scratch.mkdir(parents=True, exist_ok=True)
    for dll in dlls:
        dll = dll.resolve(strict=True)
        for case in ("default", "relative", "relative-dot", "absolute", "renamed"):
            with tempfile.TemporaryDirectory(prefix="reajs-path-", dir=scratch) as tmp:
                base = Path(tmp)
                plugin_dir = base / "plugin"
                plugin_dir.mkdir()
                copied = plugin_dir / ("RenamedHost.dll" if case == "renamed" else "reajs.dll")
                shutil.copy2(dll, copied)
                root = plugin_dir / ("JS" if case == "default" else "custom-content")
                (root / "Effects").mkdir(parents=True)
                (root / "Data").mkdir()
                (root / "Effects" / "path_probe").write_text(
                    "desc:Resource lookup regression\nfilename:0,path_probe.txt\n"
                    "@init\nv=-0.5; f=file_open(0); f>=0 ? (file_var(f,v); file_close(f););\n"
                    "@sample\nspl0=0.25; spl1=v;\n", encoding="ascii")
                (root / "Data" / "path_probe.txt").write_text("0.375\n", encoding="ascii")
                setting = "" if case == "default" else "rootpath=" + (
                    str(root) if case in ("absolute", "renamed") else
                    ".\\custom-content" if case == "relative-dot" else "custom-content") + "\n"
                copied.with_suffix(".ini").write_text(
                    "[ReaJS]\ndefaulteffect=path_probe\ninputs=2\noutputs=2\n" + setting,
                    encoding="ascii")
                # Neither cwd nor APPDATA can accidentally contain our Effects.
                env = dict(os.environ, APPDATA=str(base / "empty-appdata"))
                completed = subprocess.run([sys.executable, str(Path(__file__).resolve()),
                                            "--probe", str(copied)], cwd=base, env=env,
                                           capture_output=True, text=True, timeout=30)
                ok = completed.returncode == 0
                failures += not ok
                print(f"{'PASS' if ok else 'FAIL'} {dll} [{case}] "
                      f"{completed.stdout.strip()} {completed.stderr.strip()}", flush=True)
    return int(failures != 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dlls", nargs="*", type=Path)
    parser.add_argument("--probe", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if sys.platform != "win32" or C.sizeof(C.c_void_p) != 8:
        parser.error("Requires Windows x64 Python")
    if not args.probe and not args.dlls:
        parser.error("Supply at least one DLL path")
    sys.exit(probe(args.probe) if args.probe else run(args.dlls))
