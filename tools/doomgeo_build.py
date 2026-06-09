#!/usr/bin/env python3
"""DoomGeo-AES build helper.

This script is intentionally dependency-light so GitHub Actions can package it
as a single standalone binary with PyInstaller for Linux and Windows.
"""

from __future__ import annotations

import argparse
import binascii
import hashlib
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import TypeAlias


REPO_MARKERS = ("Makefile", "config.mk", "rom.mk")
DEFAULT_LOCAL_PREFIX = Path(".tools") / "ngdevkit-local" / "usr"
PROJECT_NAME = "DoomGeo-AES"
PACKAGE_ROM_ZIP = "doomgeo-aes.zip"
PACKAGE_ASM_ROM_ZIP = "doomgeo-aes-asm.zip"
FBNEO_COMPAT_ROM_ZIP = "puzzledp.zip"
ROM_ZIP = Path("build") / "rom" / FBNEO_COMPAT_ROM_ZIP
ROM_ELF = Path("build") / "rom.elf"
BIOS_ZIP = Path("build") / "rom" / "neogeo.zip"
ASM_ROM_ZIP = Path("build") / "asm-rom" / FBNEO_COMPAT_ROM_ZIP
ASM_ROM_ELF = Path("build") / "asm" / "doomgeo_asm.elf"
ASM_BIOS_ZIP = Path("build") / "asm-rom" / "neogeo.zip"
FBNEO_PUZZLEDP_CRC = {
    "202-p1.p1": 0x2B61415B,
    "202-s1.s1": 0xCD19264F,
    "202-c1.c1": 0xCC0095EF,
    "202-c2.c2": 0x42371307,
    "202-m1.m1": 0x9C0291EA,
    "202-v1.v1": 0xDEBEB8FB,
}
FBNEO_PUZZLEDP_SIZE = {
    "202-p1.p1": 0x80000,
    "202-s1.s1": 0x20000,
    "202-c1.c1": 0x200000,
    "202-c2.c2": 0x200000,
    "202-m1.m1": 0x20000,
    "202-v1.v1": 0x80000,
}
FBNEO_NEOGEO_CRC = {
    "sp-s3.sp1": 0x91B64BE3,
    "sm1.sm1": 0x94416D67,
    "sfix.sfix": 0xC2EA0CFD,
    "000-lo.lo": 0x5A86CFF2,
}
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
    if variant == "asm":
        artifacts = (
            (ASM_ROM_ZIP, PACKAGE_ASM_ROM_ZIP),
            (ASM_ROM_ELF, ASM_ROM_ELF.name),
            (ASM_BIOS_ZIP, ASM_BIOS_ZIP.name),
        )
    else:
        artifacts = (
            (ROM_ZIP, PACKAGE_ROM_ZIP),
            (ROM_ELF, ROM_ELF.name),
            (BIOS_ZIP, BIOS_ZIP.name),
        )
    copied = 0
    for artifact, packaged_name in artifacts:
        source = root / artifact
        if source.exists():
            destination = out_dir / packaged_name
            shutil.copy2(source, destination)
            print_step(f"copied {artifact} -> {destination}")
            copied += 1
    if copied == 0:
        raise BuildError("no build artifacts found; run build first")


def crc32(data: bytes | bytearray) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def force_crc32(data: bytes, desired: int, patch_offset: int | None = None) -> bytes:
    """Return data with 4 bytes adjusted so the whole blob has desired CRC32.

    The generated Neo Geo ROMs are padded, so the Pages build can safely use the
    final four bytes as a CRC correction field for FBNeo's arcade romset gate.
    """
    if len(data) < 4:
        raise BuildError("cannot CRC-patch blobs smaller than four bytes")
    if patch_offset is None:
        patch_offset = len(data) - 4
    if patch_offset < 0 or patch_offset + 4 > len(data):
        raise BuildError(f"invalid CRC patch offset {patch_offset} for blob size {len(data)}")

    base = bytearray(data)
    base[patch_offset : patch_offset + 4] = b"\0\0\0\0"
    base_crc = crc32(base)
    delta = desired ^ base_crc

    columns: list[int] = []
    probe = bytearray(base)
    for bit in range(32):
        probe[patch_offset : patch_offset + 4] = b"\0\0\0\0"
        probe[patch_offset + bit // 8] = 1 << (bit % 8)
        columns.append(crc32(probe) ^ base_crc)

    rows: list[tuple[int, int]] = []
    for bit in range(32):
        mask = 0
        for col, value in enumerate(columns):
            if (value >> bit) & 1:
                mask |= 1 << col
        rows.append((mask, (delta >> bit) & 1))

    rank = 0
    for col in range(32):
        pivot = next((row for row in range(rank, 32) if (rows[row][0] >> col) & 1), None)
        if pivot is None:
            continue
        rows[rank], rows[pivot] = rows[pivot], rows[rank]
        pivot_mask, pivot_rhs = rows[rank]
        for row in range(32):
            if row != rank and ((rows[row][0] >> col) & 1):
                rows[row] = (rows[row][0] ^ pivot_mask, rows[row][1] ^ pivot_rhs)
        rank += 1

    if rank != 32:
        raise BuildError("CRC patch matrix is singular")

    patch_value = 0
    for mask, rhs in rows:
        if mask and rhs:
            patch_value |= mask & -mask

    patched = bytearray(base)
    patched[patch_offset : patch_offset + 4] = patch_value.to_bytes(4, "little")
    final_crc = crc32(patched)
    if final_crc != desired:
        raise BuildError(f"CRC patch failed: wanted {desired:08x}, got {final_crc:08x}")
    return bytes(patched)


def write_zip_entries(path: Path, entries: dict[str, bytes]) -> None:
    import zipfile

    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in entries.items():
            archive.writestr(name, data)


def web_asset_version(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in paths:
        digest.update(path.name.encode("utf-8"))
        digest.update(path.read_bytes())
    return digest.hexdigest()[:12]


def build_fbneo_rom_zip(source_zip: Path, out_zip: Path) -> None:
    import zipfile

    if not source_zip.exists():
        raise BuildError(f"ROM not found: {source_zip}")
    entries: dict[str, bytes] = {}
    with zipfile.ZipFile(source_zip) as archive:
        for name, desired_crc in FBNEO_PUZZLEDP_CRC.items():
            try:
                data = archive.read(name)
            except KeyError as exc:
                raise BuildError(f"ROM entry missing for FBNeo package: {name}") from exc
            expected_size = FBNEO_PUZZLEDP_SIZE[name]
            if len(data) != expected_size:
                raise BuildError(
                    f"ROM entry {name} is {len(data)} bytes, but the FBNeo puzzledp "
                    f"web driver expects {expected_size} bytes; rebuild the Pages ROM "
                    "with DOOM_CROM_FILE_BYTES=2097152"
                )
            entries[name] = force_crc32(data, desired_crc)
    write_zip_entries(out_zip, entries)
    print_step(f"wrote FBNeo-compatible ROM package to {out_zip}")


def build_fbneo_bios_zip(source_zip: Path, out_zip: Path) -> None:
    import zipfile

    if not source_zip.exists():
        raise BuildError(f"BIOS not found: {source_zip}")
    with zipfile.ZipFile(source_zip) as archive:
        source_names = set(archive.namelist())

        def read_first(*names: str) -> bytes:
            for name in names:
                if name in source_names:
                    return archive.read(name)
            raise BuildError(f"BIOS entry missing for FBNeo package: {' or '.join(names)}")

        entries = {
            "sp-s3.sp1": force_crc32(read_first("sp-s3.sp1", "sp-s2.sp1", "neo-epo.bin", "aes-bios.bin"), FBNEO_NEOGEO_CRC["sp-s3.sp1"]),
            "sm1.sm1": force_crc32(read_first("sm1.sm1"), FBNEO_NEOGEO_CRC["sm1.sm1"]),
            "sfix.sfix": force_crc32(read_first("sfix.sfix"), FBNEO_NEOGEO_CRC["sfix.sfix"]),
            "000-lo.lo": force_crc32(read_first("000-lo.lo"), FBNEO_NEOGEO_CRC["000-lo.lo"]),
        }
    write_zip_entries(out_zip, entries)
    print_step(f"wrote FBNeo-compatible BIOS package to {out_zip}")


def html_page(
    game_name: str,
    subtitle: str,
    game_url: str,
    download_url: str,
    bios_url: str,
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
      <a class="button" href="__BIOS_URL__">Download web BIOS</a>
      __EXTRA_ACTION__
    </div>
  </main>
  <script>
    window.EJS_player = "#game";
    window.EJS_core = "fbneo";
    window.EJS_gameName = "__GAME_NAME__";
    window.EJS_gameUrl = "__GAME_URL__";
    window.EJS_biosUrl = "__BIOS_URL__";
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
        .replace("__BIOS_URL__", bios_url)
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
    version_inputs = [rom, bios]
    if asm_rom_source:
        version_inputs.append((root / asm_rom_source).resolve())
    version = web_asset_version(version_inputs)
    rom_out = out_dir / "rom" / f"web-{version}"
    rom_out.mkdir(parents=True, exist_ok=True)
    main_rom_url = f"rom/web-{version}/{PACKAGE_ROM_ZIP}"
    bios_url = f"rom/web-{version}/neogeo.zip"
    build_fbneo_rom_zip(rom, rom_out / PACKAGE_ROM_ZIP)
    build_fbneo_bios_zip(bios, rom_out / "neogeo.zip")
    # FBNeo still matches the internal Neo Geo chip filenames and CRCs against
    # the Puzzle De Pon driver. The public zip name can be project-specific.
    (out_dir / "index.html").write_text(
        html_page(
            PROJECT_NAME,
            "Neo Geo AES Doom prototype running in a browser through the EmulatorJS FBNeo WebAssembly core.",
            main_rom_url,
            main_rom_url,
            bios_url,
            "asm.html" if asm_rom_source else None,
        ),
        encoding="utf-8",
    )
    if asm_rom_source:
        asm_rom = (root / asm_rom_source).resolve()
        if not asm_rom.exists():
            raise BuildError(f"ASM ROM not found: {asm_rom}")
        asm_out = out_dir / "rom" / "asm" / f"web-{version}"
        asm_out.mkdir(parents=True, exist_ok=True)
        asm_rom_url = f"rom/asm/web-{version}/{PACKAGE_ASM_ROM_ZIP}"
        build_fbneo_rom_zip(asm_rom, asm_out / PACKAGE_ASM_ROM_ZIP)
        (out_dir / "asm.html").write_text(
            html_page(
                f"{PROJECT_NAME} ASM",
                "A separate 68000 assembly cartridge build with a controller-driven Neo Geo sprite scene.",
                asm_rom_url,
                asm_rom_url,
                bios_url,
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
    parser = argparse.ArgumentParser(description=f"Build and manage {PROJECT_NAME} locally.")
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
