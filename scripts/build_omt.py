#!/usr/bin/env python3
"""Build the Open Media Transport runtime (libomt + libvmx) from source, at pinned commits, for the packages.

Usage: build_omt.py OUT_DIR

ISFLab loads libomt at run time and looks for it next to the application first (src/engine/output/OutputOmt.cpp), so the
packages ship it there. All three projects are MIT licensed (github.com/openmediatransport); their licence texts are
written to OUT_DIR/OMT-LICENSES.txt.

Needs git, the .NET 8 SDK (libomt is C# compiled to a native library by Native AOT) and a C++ compiler: clang++ on
Linux and Windows (with the MSVC environment), Xcode's on macOS.
"""
import ctypes
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

# libomtnet (the protocol, C#), libomt (its C API, Native AOT) and libvmx (the VMX codec, C++)
REPOS = {
    "libomtnet": "7d29031ff3f355ffa1f0cd08ee24d02e258445dd",
    "libomt": "bb4b67be8e5964f1017592b9fc5406495e704e5d",
    "libvmx": "31724bb96530325a989bc157c8e07538d6f1c4db",
}


def run(cmd, cwd, env=None):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=cwd, check=True, env=env)


def dotnet(args, cwd):
    # The MSVC environment (Windows) sets Platform=x64, which MSBuild takes for the project's platform: the outputs
    # would land in bin/x64/..., not where libomt looks for libomtnet
    env = {k: v for k, v in os.environ.items() if k.lower() != "platform"}
    run(["dotnet", *args], cwd, env)


def fetch(root: Path):
    for name, sha in REPOS.items():
        d = root / name
        if (d / ".git").exists():
            continue
        d.mkdir(parents=True)
        run(["git", "init", "-q"], d)
        run(["git", "fetch", "-q", "--depth", "1", f"https://github.com/openmediatransport/{name}", sha], d)
        run(["git", "checkout", "-q", "FETCH_HEAD"], d)


def build_vmx(src: Path, out: Path):
    s = src / "src"
    system = platform.system()
    if system == "Darwin":
        # Universal. Apple silicon starts at macOS 11; a 10.15 target triggers newer libc++'s unsupported-platform warning.
        # Suppress only SSE2NEON's opt-in optimization reminder, not compiler diagnostics. The codec stays at -O3.
        common = ["-O3", "-std=c++17", "-fdeclspec", "-fPIC", "-mmacosx-version-min=11.0", "-dynamiclib",
                  "-install_name", "@rpath/libvmx.dylib"]
        arm, x86 = out / "vmx-arm64.dylib", out / "vmx-x86_64.dylib"
        run(["clang++", *common, "-target", "arm64-apple-darwin", "-DSSE2NEON_SUPPRESS_WARNINGS=1", s / "vmxcodec_arm.cpp", s / "vmxcodec.cpp", "-o", arm], src)
        run(["clang++", *common, "-target", "x86_64-apple-darwin", "-mlzcnt", "-mavx2", "-mbmi",
             s / "vmxcodec_x86.cpp", s / "vmxcodec_avx2.cpp", s / "vmxcodec.cpp", "-o", x86], src)
        run(["lipo", "-create", "-output", out / "libvmx.dylib", x86, arm], src)
        arm.unlink()
        x86.unlink()
    elif system == "Windows":
        # The runner's MSVC STL asks for a newer Clang than the one shipped with it; libvmx only uses the C++
        # standard library headers, so the documented escape hatch is enough
        run(["clang++", "-O3", "-std=c++17", "-fdeclspec", "-mlzcnt", "-mavx2", "-mbmi", "-shared",
             "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH",
             f"-Wl,/DEF:{src / 'exports.def'}", s / "vmxcodec_x86.cpp", s / "vmxcodec_avx2.cpp", s / "vmxcodec.cpp",
             "-o", out / "libvmx.dll"], src)
    else:
        # A soname: libomt asks for "libvmx.so", the copy ISFLab loads first from the same folder
        run(["clang++", "-O3", "-std=c++17", "-fdeclspec", "-fPIC", "-mlzcnt", "-mavx2", "-mbmi", "-shared",
             "-Wl,-soname,libvmx.so", "-Wl,-rpath,$ORIGIN", s / "vmxcodec_x86.cpp", s / "vmxcodec_avx2.cpp",
             s / "vmxcodec.cpp", "-o", out / "libvmx.so"], src)


def build_omt(root: Path, out: Path):
    # libomt references ../libomtnet/bin/Release/netstandard2.0/libomtnet.dll
    dotnet(["build", root / "libomtnet" / "libomtnet.csproj", "-c", "Release", "-f", "netstandard2.0"], root)
    omt = root / "libomt"
    system = platform.system()
    rids = {"Darwin": ["osx-arm64", "osx-x64"], "Windows": ["win-x64"]}.get(system, ["linux-x64"])
    natives = []
    for rid in rids:
        dotnet(["publish", omt / "libomt.csproj", "-r", rid, "-c", "Release"], omt)
        lib = [p for p in (omt / "bin").rglob("libomt.*")
               if p.parent.name == "native" and rid in p.parts and p.suffix in (".dylib", ".dll", ".so")]
        if len(lib) != 1:
            sys.exit(f"libomt for {rid}: expected one native library, found {lib}")
        natives.append(lib[0])
    if system == "Darwin":
        target = out / "libomt.dylib"
        run(["lipo", "-create", "-output", target, *natives], root)
        run(["install_name_tool", "-id", "@rpath/libomt.dylib", target], root)
        run(["install_name_tool", "-add_rpath", "@loader_path", target], root)
    else:
        shutil.copy2(natives[0], out / natives[0].name)


def licences(root: Path, out: Path):
    parts = ["Open Media Transport runtime bundled with ISFLab (libomt, libvmx), built from source:\n"]
    for name, sha in REPOS.items():
        parts.append(f"\n===== {name} — https://github.com/openmediatransport/{name} @ {sha}\n\n")
        parts.append((root / name / "LICENSE.txt").read_text(encoding="utf-8-sig"))
    (out / "OMT-LICENSES.txt").write_text("".join(parts), encoding="utf-8")


def check(out: Path):
    """The libraries load, as ISFLab loads them (libvmx first, globally), and libomt exports its sender."""
    ext = {"Darwin": ".dylib", "Windows": ".dll"}.get(platform.system(), ".so")
    mode = getattr(ctypes, "RTLD_GLOBAL", 0)
    ctypes.CDLL(str(out / ("libvmx" + ext)), mode=mode)
    omt = ctypes.CDLL(str(out / ("libomt" + ext)), mode=mode)
    for symbol in ("omt_send_create", "omt_send", "omt_send_destroy", "omt_receive_create"):
        getattr(omt, symbol)
    print("libomt and libvmx load; libomt exports its sender and receiver")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = Path(sys.argv[1]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    root = Path(os.environ.get("RUNNER_TEMP", out.parent)) / "omt-src"
    root.mkdir(parents=True, exist_ok=True)
    fetch(root)
    build_vmx(root / "libvmx", out)
    build_omt(root, out)
    licences(root, out)
    check(out)
    for p in sorted(out.iterdir()):
        print(f"{p.name}: {p.stat().st_size} bytes")


if __name__ == "__main__":
    main()
