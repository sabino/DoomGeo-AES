# DoomGeo-AES

DoomGeo-AES is a Neo Geo AES research prototype that pushes Doom-like gameplay
through the hardware the way the machine wants to draw: sprite strips, pre-baked
graphics, fix-layer UI, and very small runtime data.

It is not a framebuffer Doom source port. The build tools read Doom-format WAD
data offline, convert the map and assets into Neo Geo-friendly structures, and
the 68000 runtime drives the scene by updating sprite control blocks rather than
drawing pixels.

![Current DoomGeo-AES gameplay](docs/screenshots/doomgeo-aes-current-gameplay.png)

## Current State

| Area | Status |
| --- | --- |
| WAD conversion | Converts E1M1 map lumps, player start, doors, exits, secrets, damaging sectors, texture classes, and runtime things into a higher-resolution Neo Geo grid. |
| Rendering | Fallback 20-column wall raycaster, Doom wall/door atlases, depth palettes, sprite-backed floor/ceiling approximation, and eight visible world-thing slots kept under the Neo Geo scanline limit. |
| HUD | Doom `STBAR`, face frames, key/weapon indicators, large red status digits, and compact ammo counters. |
| Weapons | Fist, pistol, shotgun, chaingun, rocket launcher, plasma rifle, BFG, and chainsaw have playable runtime paths. Shareware builds use placeholder psprite frames for plasma/BFG because those Doom lumps are not present in `doom1.wad`; a registered/commercial WAD can supply the real art. |
| Gameplay | Pickups, keys, doors, exits, secrets, hurt/bonus/muzzle feedback, monsters, barrels, corpses, drops, projectiles, and compact AI are present. |
| Map | Higher-resolution internal grid with a downsampled fix-layer minimap for player, walls, pickups, threats, doors, and exits. It needs an instant/faster redraw pass. |
| Audio | Null sound path only. YM2610/Z80 sound and music conversion are not implemented yet. |
| Browser build | GitHub Pages package runs the ROM through EmulatorJS/FBNeo, plus a separate 68000 ASM demo build. |

## Screenshots

| Current gameplay | Minimap overlay |
| --- | --- |
| ![Current wall, floor, weapon, and HUD state](docs/screenshots/doomgeo-aes-current-gameplay.png) | ![Fix-layer minimap overlay with map markers](docs/screenshots/doomgeo-aes-current-minimap.png) |

| HUD work-in-progress | Native Doom comparison |
| --- | --- |
| ![Current HUD status bar and known number placement issue](docs/screenshots/doomgeo-aes-current-hud.png) | ![Native Doom beside the Neo Geo prototype](docs/screenshots/doomgeo-aes-native-comparison.png) |

## Controls

| Input | Action |
| --- | --- |
| D-pad Up / Down | Move forward/back |
| D-pad Left / Right | Turn |
| Hold A + Left/Right | Strafe |
| B | Fire weapon |
| C | Toggle weapon |
| D | Use facing door |
| Hold A + C | Toggle minimap |
| D after DEAD/EXIT | Restart level |

The default GnGeo keyboard mapping is:

| Neo Geo | Keyboard |
| --- | --- |
| A / B / C / D | `Z` / `X` / `A` / `S` |
| Start / Coin | `1` / `3` |
| D-pad | Arrow keys |
| Menu | `Esc` |

## Build And Run

Tools and downloaded WAD data are kept under `.tools/`, which is ignored by git.

```sh
python3 tools/doomgeo_build.py install
python3 tools/doomgeo_build.py doctor
python3 tools/doomgeo_build.py build
SDL_VIDEODRIVER=x11 make gngeo
```

Useful variants:

```sh
make key-test-rom
make key-test-gngeo
make DOOM_MAP=E1M2
make DOOM_IWAD=/path/to/DOOM.WAD DOOM_MAP=E1M1
make DOOM_MAP=E1M1 DOOM_MAP_WIDTH=38 DOOM_MAP_HEIGHT=27
python3 tools/doomgeo_build.py build --target asm-rom
python3 tools/doomgeo_build.py pages --out dist/pages
DOOM_MAP=E1M1 tools/capture_compare.sh
```

You must provide your own Neo Geo BIOS for local emulation. The browser package
uses an FBNeo-compatible packaging path, but that does not rename the project:
`DoomGeo-AES` is the game name; `puzzledp` is only the private FBNeo driver/chip
identity used so the arcade core accepts the generated homebrew ROM zip.

## Documentation

- [Feature Inventory](docs/features.md)
- [Architecture Notes](docs/architecture.md)
- [Roadmap](docs/roadmap.md)
- [Reference Port Notes](docs/reference-ports.md)
- [Build and Packaging](docs/build-packaging.md)
- [Release Plan](docs/release-plan.md)
- [Video Reference Transcript](VIDEO-REF.md)

## Reference Ports

These projects are useful references, but code is not copied into this branch:

- [doomgeneric](https://github.com/ozkl/doomgeneric) shows a compact platform
  API around Doom's tick/frame/input model.
- [PureDOOM](https://github.com/Daivuk/PureDOOM) shows a single-header,
  callback-oriented Doom source packaging style and a clear license section.
- [id Software DOOM](https://github.com/id-Software/DOOM) is the historical
  source release used for license and architecture context.

Any future code import from these projects must be deliberate, attributed, and
license-compatible. Current work adapts ideas and terminology only.

## License And Assets

DoomGeo-AES is research/homebrew work derived from this repo's Neo Geo raycaster
code plus original conversion/runtime code written here. It does not include a
commercial Doom IWAD, a Neo Geo BIOS, or proprietary id Software game data. The
default build downloads shareware/Freedoom-compatible WAD data under `.tools/`
for local conversion.

The official id Software DOOM source release is distributed under GPL-2.0. The
historical DOOM Source Code License text, included below because several Doom
ports keep it visible for provenance, is not a grant for commercial Doom asset
redistribution.

<details>
<summary>Historical DOOM Source Code License text</summary>

```text
      LIMITED USE SOFTWARE LICENSE AGREEMENT

        This Limited Use Software License Agreement (the "Agreement")
is a legal agreement between you, the end-user, and Id Software, Inc.
("ID").  By downloading or purchasing the software material, which
includes source code (the "Source Code"), artwork data, music and
software tools (collectively, the "Software"), you are agreeing to
be bound by the terms of this Agreement.  If you do not agree to the
terms of this Agreement, promptly destroy the Software you may have
downloaded or copied.

ID SOFTWARE LICENSE

1.      Grant of License.  ID grants to you the right to use the
Software.  You have no ownership or proprietary rights in or to the
Software, or the Trademark. For purposes of this section, "use" means
loading the Software into RAM, as well as installation on a hard disk
or other storage device. The Software, together with any archive copy
thereof, shall be destroyed when no longer used in accordance with
this Agreement, or when the right to use the Software is terminated.
You agree that the Software will not be shipped, transferred or
exported into any country in violation of the U.S. Export
Administration Act (or any other law governing such matters) and that
you will not utilize, in any other manner, the Software in violation
of any applicable law.

2.      Permitted Uses.  For educational purposes only, you, the
end-user, may use portions of the Source Code, such as particular
routines, to develop your own software, but may not duplicate the
Source Code, except as noted in paragraph 4.  The limited right
referenced in the preceding sentence is hereinafter referred to as
"Educational Use."  By so exercising the Educational Use right you
shall not obtain any ownership, copyright, proprietary or other
interest in or to the Source Code, or any portion of the Source
Code.  You may dispose of your own software in your sole discretion.
With the exception of the Educational Use right, you may not
otherwise use the Software, or an portion of the Software, which
includes the Source Code, for commercial gain.

3.      Prohibited Uses:  Under no circumstances shall you, the
end-user, be permitted, allowed or authorized to commercially exploit
the Software. Neither you nor anyone at your direction shall do any
of the following acts with regard to the Software, or any portion
thereof:

        Rent;

        Sell;

        Lease;

        Offer on a pay-per-play basis;

        Distribute for money or any other consideration; or

        In any other manner and through any medium whatsoever
commercially exploit or use for any commercial purpose.

Notwithstanding the foregoing prohibitions, you may commercially
exploit the software you develop by exercising the Educational Use
right, referenced in paragraph 2. hereinabove.

4.      Copyright.  The Software and all copyrights related thereto
(including all characters and other images generated by the Software
or depicted in the Software) are owned by ID and is protected by
United States  copyright laws and international treaty provisions.
Id shall retain exclusive ownership and copyright in and to the
Software and all portions of the Software and you shall have no
ownership or other proprietary interest in such materials. You must
treat the Software like any other copyrighted material. You may not
otherwise reproduce, copy or disclose to others, in whole or in any
part, the Software.  You may not copy the written materials
accompanying the Software.  You agree to use your best efforts to
see that any user of the Software licensed hereunder complies with
this Agreement.

5.      NO WARRANTIES.  ID DISCLAIMS ALL WARRANTIES, BOTH EXPRESS
IMPLIED, INCLUDING BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE WITH RESPECT
TO THE SOFTWARE.  THIS LIMITED WARRANTY GIVES YOU SPECIFIC LEGAL
RIGHTS.  YOU MAY HAVE OTHER RIGHTS WHICH VARY FROM JURISDICTION TO
JURISDICTION.  ID DOES NOT WARRANT THAT THE OPERATION OF THE SOFTWARE
WILL BE UNINTERRUPTED, ERROR FREE OR MEET YOUR SPECIFIC REQUIREMENTS.
THE WARRANTY SET FORTH ABOVE IS IN LIEU OF ALL OTHER EXPRESS
WARRANTIES WHETHER ORAL OR WRITTEN.  THE AGENTS, EMPLOYEES,
DISTRIBUTORS, AND DEALERS OF ID ARE NOT AUTHORIZED TO MAKE
MODIFICATIONS TO THIS WARRANTY, OR ADDITIONAL WARRANTIES ON BEHALF
OF ID.

        Exclusive Remedies.  The Software is being offered to you
free of any charge.  You agree that you have no remedy against ID, its
affiliates, contractors, suppliers, and agents for loss or damage
caused by any defect or failure in the Software regardless of the form
of action, whether in contract, tort, includinegligence, strict
liability or otherwise, with regard to the Software.  This Agreement
shall be construed in accordance with and governed by the laws of the
State of Texas.  Copyright and other proprietary matters will be
governed by United States laws and international treaties.  IN ANY
CASE, ID SHALL NOT BE LIABLE FOR LOSS OF DATA, LOSS OF PROFITS, LOST
SAVINGS, SPECIAL, INCIDENTAL, CONSEQUENTIAL, INDIRECT OR OTHER
SIMILAR DAMAGES ARISING FROM BREACH OF WARRANTY, BREACH OF CONTRACT,
NEGLIGENCE, OR OTHER LEGAL THEORY EVEN IF ID OR ITS AGENT HAS BEEN
ADVISED OF THE POSSIBILITY OF SUCH DAMAGES, OR FOR ANY CLAIM BY ANY
OTHER PARTY. Some jurisdictions do not allow the exclusion or
limitation of incidental or consequential damages, so the above
limitation or exclusion may not apply to you.
```

</details>

## Acknowledgements

Built against [ngdevkit](https://github.com/dciabrin/ngdevkit). Hardware details
were cross-referenced with the [Neo Geo Development Wiki](https://wiki.neogeodev.org).
