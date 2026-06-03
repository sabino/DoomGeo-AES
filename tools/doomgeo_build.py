#!/usr/bin/env python3
"""DoomGeo-MVS build helper.

This script is intentionally dependency-light so GitHub Actions can package it
as a single standalone binary with PyInstaller for Linux and Windows.
"""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import TypeAlias


REPO_MARKERS = ("Makefile", "config.mk", "rom.mk")
DEFAULT_LOCAL_PREFIX = Path(".tools") / "ngdevkit-local" / "usr"
ROM_ZIP = Path("build") / "rom" / "puzzledp.zip"
ROM_ELF = Path("build") / "rom.elf"
BIOS_ZIP = Path("build") / "rom" / "neogeo.zip"
ASM_ROM_ZIP = Path("build") / "asm-rom" / "puzzledp.zip"
ASM_ROM_ELF = Path("build") / "asm" / "doomgeo_asm.elf"
ASM_BIOS_ZIP = Path("build") / "asm-rom" / "neogeo.zip"
REQUIRED_TOOLS = (
    "m68k-neogeo-elf-gcc",
    "m68k-neogeo-elf-objcopy",
)
DEB_PACKAGES = ("ngdevkit-toolchain", "ngdevkit", "ngdevkit-gngeo")
MSYS2_NGDEVKIT_REPO = "https://dciabrin.net/msys2-ngdevkit/$arch"
ToolPrefix: TypeAlias = Path | str


class BuildError(RuntimeError):
    pass


def repo_root() -> Path:
    here = Path(__file__).resolve()
    for candidate in (here.parent, *here.parents):
        if all((candidate / marker).exists() for marker in REPO_MARKERS):
            return candidate
    return Path.cwd().resolve()


def print_step(message: str) -> None:
    print(f"[doomgeo-build] {message}", flush=True)


def run(args: list[str], cwd: Path, env: dict[str, str] | None = None) -> None:
    print_step("$ " + " ".join(args))
    try:
        subprocess.run(args, cwd=cwd, env=env, check=True)
    except FileNotFoundError as exc:
        raise BuildError(f"missing executable: {args[0]}") from exc
    except subprocess.CalledProcessError as exc:
        raise BuildError(f"command failed with exit code {exc.returncode}: {' '.join(args)}") from exc


def command_exists(name: str) -> bool:
    return shutil.which(name) is not None


def local_prefix(root: Path) -> Path:
    return root / DEFAULT_LOCAL_PREFIX


def tool_path(prefix: Path, tool: str) -> Path:
    suffix = ".exe" if platform.system() == "Windows" else ""
    return prefix / "bin" / f"{tool}{suffix}"


def has_local_toolchain(root: Path) -> bool:
    prefix = local_prefix(root)
    return all(tool_path(prefix, tool).exists() for tool in REQUIRED_TOOLS)


def make_env(prefix: ToolPrefix | None = None) -> dict[str, str]:
    env = os.environ.copy()
    if prefix is not None:
        if isinstance(prefix, Path):
            bindir = str(prefix / "bin")
            env["PATH"] = bindir + os.pathsep + env.get("PATH", "")
        elif not is_msys2():
            bindir = prefix.rstrip("/") + "/bin"
            env["PATH"] = bindir + os.pathsep + env.get("PATH", "")
        env["TOOLS_PREFIX"] = str(prefix)
    return env


def is_msys2() -> bool:
    return bool(os.environ.get("MSYSTEM"))


def msys2_prefix() -> str | None:
    prefix = os.environ.get("MINGW_PREFIX")
    if prefix:
        return prefix
    if os.environ.get("MSYSTEM", "").upper() == "UCRT64":
        return "/ucrt64"
    return None


def wsl_repo_path(root: Path) -> str:
    result = subprocess.run(
        ["wsl", "wslpath", "-a", str(root)],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return result.stdout.strip()


def run_via_wsl(root: Path, subcommand: list[str]) -> None:
    linux_path = wsl_repo_path(root)
    quoted_args = " ".join(subcommand)
    run(
        [
            "wsl",
            "bash",
            "-lc",
            f"cd {linux_path!r} && python3 tools/doomgeo_build.py {quoted_args}",
        ],
        cwd=root,
    )


def resolve_prefix(root: Path, requested: str | None) -> ToolPrefix | None:
    if requested:
        if requested.startswith("/") and is_msys2():
            return requested
        return Path(requested)
    if has_local_toolchain(root):
        return local_prefix(root)
    if is_msys2() and msys2_prefix():
        return msys2_prefix()
    if all(command_exists(tool) for tool in REQUIRED_TOOLS):
        return None
    return local_prefix(root)


def install_tools_msys2(root: Path) -> None:
    if not is_msys2():
        raise BuildError("MSYS2 install must run from an MSYS2 UCRT64 shell")
    if os.environ.get("MSYSTEM", "").upper() != "UCRT64":
        raise BuildError("MSYS2 install requires the UCRT64 environment")
    pacman_conf = Path("/etc/pacman.conf")
    existing = pacman_conf.read_text(encoding="utf-8", errors="ignore")
    if "[ngdevkit]" not in existing:
        with pacman_conf.open("a", encoding="utf-8") as handle:
            handle.write(f"\n[ngdevkit]\nSigLevel = Optional TrustAll\nServer = {MSYS2_NGDEVKIT_REPO}\n")
        print_step("added ngdevkit MSYS2 package repository")
    run(["pacman", "--noconfirm", "-Sy", "--needed", "pactoys", "make", "zip", "unzip"], cwd=root)
    run(
        [
            "pacboy",
            "--noconfirm",
            "-S",
            "--needed",
            "ngdevkit:u",
            "ngdevkit-gngeo:u",
            "imagemagick:u",
            "sox:u",
            "python-pillow:u",
            "python-ruamel-yaml:u",
        ],
        cwd=root,
    )


def install_tools_from_debs(root: Path, deb_dir: Path) -> None:
    debs: list[Path] = []
    for package in DEB_PACKAGES:
        matches = sorted(deb_dir.glob(f"{package}_*.deb"))
        if not matches:
            raise BuildError(f"missing {package}_*.deb in {deb_dir}")
        debs.append(matches[-1])
    prefix_root = root / ".tools" / "ngdevkit-local"
    prefix_root.mkdir(parents=True, exist_ok=True)
    for deb in debs:
        run(["dpkg-deb", "-x", str(deb), str(prefix_root)], cwd=root)
    print_step(f"installed local ngdevkit files under {prefix_root}")


def install_tools_system(root: Path) -> None:
    if platform.system() == "Windows":
        raise BuildError(
            "native Windows install is handled through MSYS2 UCRT64; see docs/build-packaging.md"
        )
    if not command_exists("sudo"):
        raise BuildError("system install requires sudo")
    run(["sudo", "apt-get", "update"], cwd=root)
    run(
        [
            "sudo",
            "apt-get",
            "install",
            "-y",
            "software-properties-common",
            "curl",
            "zip",
            "imagemagick",
            "sox",
            "libsox-fmt-mp3",
        ],
        cwd=root,
    )
    run(["sudo", "add-apt-repository", "-y", "ppa:dciabrin/ngdevkit"], cwd=root)
    run(["sudo", "apt-get", "update"], cwd=root)
    run(["sudo", "apt-get", "install", "-y", "ngdevkit", "ngdevkit-gngeo"], cwd=root)


def install_tools_auto(root: Path, deb_dir: Path) -> None:
    if is_msys2():
        install_tools_msys2(root)
    elif platform.system() == "Linux" and deb_dir.exists() and any(deb_dir.glob("ngdevkit_*.deb")):
        install_tools_from_debs(root, deb_dir)
    elif platform.system() == "Linux":
        install_tools_system(root)
    else:
        raise BuildError("automatic install supports Linux or MSYS2 UCRT64")


def build(root: Path, prefix_arg: str | None, target: str, run_emulator: bool) -> None:
    if platform.system() == "Windows" and not command_exists("make"):
        if command_exists("wsl"):
            run_via_wsl(root, ["build", "--target", target])
            return
        raise BuildError("Windows builds need MSYS2 UCRT64 make/ngdevkit or WSL")

    prefix = resolve_prefix(root, prefix_arg)
    env = make_env(prefix)
    args = ["make"]
    if prefix is not None:
        args.append(f"TOOLS_PREFIX={prefix}")
    if is_msys2() and msys2_prefix():
        args.append(f"PYTHON={msys2_prefix()}/bin/python3")
    args.append(target)
    run(args, cwd=root, env=env)
    if run_emulator:
        run(["make", "gngeo"], cwd=root, env=env)


def package_artifacts(root: Path, out_dir: Path, variant: str) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    artifacts = (ASM_ROM_ZIP, ASM_ROM_ELF, ASM_BIOS_ZIP) if variant == "asm" else (ROM_ZIP, ROM_ELF, BIOS_ZIP)
    copied = 0
    for artifact in artifacts:
        source = root / artifact
        if source.exists():
            shutil.copy2(source, out_dir / source.name)
            print_step(f"copied {artifact} -> {out_dir / source.name}")
            copied += 1
    if copied == 0:
        raise BuildError("no build artifacts found; run build first")


def html_page(
    game_name: str,
    subtitle: str,
    game_url: str,
    download_url: str,
    extra_link: str | None = None,
) -> str:
    extra_action = f'\n      <a class="button" href="{extra_link}">Open ASM Build</a>' if extra_link else ""
    template = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>__GAME_NAME__</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #111318;
      --panel: #1b1f27;
      --text: #f1f4f8;
      --muted: #a8b0bd;
      --accent: #df3b2f;
    }
    body {
      margin: 0;
      min-height: 100vh;
      background: var(--bg);
      color: var(--text);
      font: 16px/1.45 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }
    main {
      width: min(1080px, calc(100vw - 32px));
      margin: 0 auto;
      padding: 24px 0 32px;
    }
    header {
      display: flex;
      justify-content: space-between;
      gap: 16px;
      align-items: end;
      margin-bottom: 16px;
    }
    h1 {
      margin: 0;
      font-size: clamp(24px, 4vw, 42px);
      line-height: 1;
    }
    p {
      margin: 6px 0 0;
      color: var(--muted);
    }
    a {
      color: var(--text);
      text-decoration-color: var(--accent);
    }
    #game-wrap {
      background: #000;
      border: 1px solid #2b313c;
      width: 100%;
      aspect-ratio: 320 / 224;
      min-height: 320px;
    }
    #game {
      width: 100%;
      height: 100%;
    }
    .actions {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      margin-top: 14px;
    }
    .button {
      background: var(--panel);
      border: 1px solid #343b48;
      border-radius: 6px;
      padding: 8px 12px;
    }
  </style>
</head>
<body>
  <main>
    <header>
      <div>
        <h1>__GAME_NAME__</h1>
        <p>__SUBTITLE__</p>
      </div>
    </header>
    <div id="game-wrap">
      <div id="game"></div>
    </div>
    <div class="actions">
      <a class="button" href="__DOWNLOAD_URL__">Download ROM</a>
      <a class="button" href="rom/neogeo.zip">Download null BIOS</a>
      __EXTRA_ACTION__
    </div>
  </main>
  <script>
    window.EJS_player = "#game";
    window.EJS_core = "fbneo";
    window.EJS_gameName = "__GAME_NAME__";
    window.EJS_gameUrl = "__GAME_URL__";
    window.EJS_biosUrl = "rom/neogeo.zip";
    window.EJS_pathtodata = "https://cdn.emulatorjs.org/stable/data/";
    window.EJS_startOnLoaded = false;
  </script>
  <script src="https://cdn.emulatorjs.org/stable/data/loader.js"></script>
</body>
</html>
"""
    return (
        template.replace("__GAME_NAME__", game_name)
        .replace("__SUBTITLE__", subtitle)
        .replace("__GAME_URL__", game_url)
        .replace("__DOWNLOAD_URL__", download_url)
        .replace("__EXTRA_ACTION__", extra_action)
    )


def build_pages(
    root: Path,
    out_dir: Path,
    rom_source: Path | None,
    bios_source: Path | None,
    asm_rom_source: Path | None,
) -> None:
    rom = (root / rom_source).resolve() if rom_source else root / ROM_ZIP
    bios = (root / bios_source).resolve() if bios_source else root / BIOS_ZIP
    if not rom.exists():
        raise BuildError(f"ROM not found: {rom}")
    if not bios.exists():
        raise BuildError(f"BIOS not found: {bios}")
    rom_out = out_dir / "rom"
    rom_out.mkdir(parents=True, exist_ok=True)
    shutil.copy2(rom, rom_out / "puzzledp.zip")
    shutil.copy2(bios, rom_out / "neogeo.zip")
    (out_dir / "index.html").write_text(
        html_page(
            "DoomGeo-MVS",
            "Neo Geo Doom prototype running in a browser through the EmulatorJS FBNeo WebAssembly core.",
            "rom/puzzledp.zip",
            "rom/puzzledp.zip",
            "asm.html" if asm_rom_source else None,
        ),
        encoding="utf-8",
    )
    if asm_rom_source:
        asm_rom = (root / asm_rom_source).resolve()
        if not asm_rom.exists():
            raise BuildError(f"ASM ROM not found: {asm_rom}")
        asm_out = rom_out / "asm"
        asm_out.mkdir(parents=True, exist_ok=True)
        shutil.copy2(asm_rom, asm_out / "puzzledp.zip")
        (out_dir / "asm.html").write_text(
            html_page(
                "DoomGeo-MVS ASM",
                "A separate 68000 assembly cartridge build with a controller-driven Neo Geo sprite scene.",
                "rom/asm/puzzledp.zip",
                "rom/asm/puzzledp.zip",
            ),
            encoding="utf-8",
        )
    print_step(f"wrote GitHub Pages site to {out_dir}")


def uninstall(root: Path, all_tools: bool, dry_run: bool) -> None:
    targets = [root / ".tools" / "ngdevkit-local"]
    if all_tools:
        targets.extend([root / ".tools" / "assets", root / ".tools" / "downloads"])
    for target in targets:
        if not target.exists():
            print_step(f"already absent: {target}")
            continue
        if dry_run:
            print_step(f"would remove {target}")
        else:
            shutil.rmtree(target)
            print_step(f"removed {target}")


def doctor(root: Path, prefix_arg: str | None) -> int:
    prefix = resolve_prefix(root, prefix_arg)
    print(f"repo: {root}")
    print(f"platform: {platform.platform()}")
    print(f"python: {sys.version.split()[0]}")
    print(f"local toolchain: {'yes' if has_local_toolchain(root) else 'no'}")
    print(f"selected TOOLS_PREFIX: {prefix or '(PATH/system)'}")
    missing = []
    for tool in REQUIRED_TOOLS:
        if prefix is None:
            found = shutil.which(tool)
        elif isinstance(prefix, str):
            found = shutil.which(tool)
        else:
            found = str(tool_path(prefix, tool)) if tool_path(prefix, tool).exists() else None
        print(f"{tool}: {found or 'missing'}")
        if not found:
            missing.append(tool)
    print(f"rom: {root / ROM_ZIP if (root / ROM_ZIP).exists() else 'not built'}")
    return 1 if missing else 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build and manage DoomGeo-MVS locally.")
    parser.add_argument("--repo", type=Path, default=None, help="repository root override")
    sub = parser.add_subparsers(dest="command", required=True)

    doctor_cmd = sub.add_parser("doctor", help="check toolchain/build readiness")
    doctor_cmd.add_argument("--tools-prefix", default=None)

    install = sub.add_parser("install-tools", help="install ngdevkit tools")
    install.add_argument(
        "--method",
        choices=("auto", "debs", "system", "msys2"),
        default="debs",
        help="debs extracts cached .deb packages under .tools; system installs from the ngdevkit PPA",
    )
    install.add_argument("--deb-dir", type=Path, default=Path(".tools") / "downloads")

    installer = sub.add_parser("install", help="one-command installer for the build toolchain")
    installer.add_argument(
        "--method",
        choices=("auto", "debs", "system", "msys2"),
        default="auto",
        help="auto selects MSYS2, cached .debs, or the Ubuntu PPA based on the host",
    )
    installer.add_argument("--deb-dir", type=Path, default=Path(".tools") / "downloads")

    build_cmd = sub.add_parser("build", help="build the Neo Geo ROM")
    build_cmd.add_argument("--target", default="all")
    build_cmd.add_argument("--tools-prefix", default=None)
    build_cmd.add_argument("--run", action="store_true", help="run GnGeo after building")

    package = sub.add_parser("package", help="copy build artifacts into an output directory")
    package.add_argument("--out", type=Path, default=Path("dist") / "rom")
    package.add_argument("--variant", choices=("c", "asm"), default="c")

    pages = sub.add_parser("pages", help="build a GitHub Pages playable web bundle")
    pages.add_argument("--out", type=Path, default=Path("dist") / "pages")
    pages.add_argument("--rom", type=Path, default=None)
    pages.add_argument("--bios", type=Path, default=None)
    pages.add_argument("--asm-rom", type=Path, default=None, help="optional ASM ROM zip to expose at asm.html")

    remove = sub.add_parser("uninstall", help="remove repo-local installed tools")
    remove.add_argument("--all", action="store_true", help="also remove cached WADs/downloaded packages")
    remove.add_argument("--dry-run", action="store_true")

    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    root = args.repo.resolve() if args.repo else repo_root()
    try:
        if args.command == "doctor":
            return doctor(root, args.tools_prefix)
        if args.command in ("install-tools", "install"):
            deb_dir = (root / args.deb_dir).resolve()
            if args.method == "auto":
                install_tools_auto(root, deb_dir)
            elif args.method == "system":
                install_tools_system(root)
            elif args.method == "msys2":
                install_tools_msys2(root)
            else:
                install_tools_from_debs(root, deb_dir)
            return 0
        if args.command == "build":
            build(root, args.tools_prefix, args.target, args.run)
            return 0
        if args.command == "package":
            package_artifacts(root, (root / args.out).resolve(), args.variant)
            return 0
        if args.command == "pages":
            build_pages(root, (root / args.out).resolve(), args.rom, args.bios, args.asm_rom)
            return 0
        if args.command == "uninstall":
            uninstall(root, args.all, args.dry_run)
            return 0
    except BuildError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
