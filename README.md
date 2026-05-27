# Delve

A turn-based roguelike for the Sega Dreamcast. Ten floors of procedurally generated dungeon, a handful of enemy types, items, and a carry weight system. Written in C targeting KallistiOS.

---

## Requirements

- [KallistiOS](https://github.com/KallistiOS/KallistiOS) SDK installed at `/opt/toolchains/dc/kos` (or set `$KOS_BASE`)
- `mkdcdisc` on `PATH` if you want to build a CDI image

---

## Building

```bash
./build.sh          # build roguelike.elf (run in emulator)
./build.sh dist     # strip and produce 1ST_READ.BIN for real hardware
./build.sh cdi      # build a burnable CDI image
./build.sh clean    # remove build artifacts
```

To burn a CDI to disc:

```bash
cd ~/dcdib && sudo ./dcdib /path/to/roguelike.cdi
```

---

## Controls

| Button | Play | Inventory | Targeting |
|--------|------|-----------|-----------|
| D-pad | Move / attack | Scroll | Move cursor |
| A | Pick up item | Use / equip | Confirm |
| B | Open inventory | Close | Cancel |
| X | | Drop item | |
| R trigger | Use stairs | | |
| Start | Quit | Quit | Quit |

---

## Gameplay

Descend through ten floors using the `>` stairs. Enemies move whenever you do. Reach floor 10 and take the stairs to win.

**Combat** is bump-to-attack. Damage is `ATK - DEF + 1d5 - 2`, minimum 1.

**Items** found on the ground can be picked up (A) and managed from the inventory (B). Weapons, shields, and armour are equipped from inventory. Scrolls and wands require targeting.

**Carry weight** limits how much you can hold. Heavier armour offers more protection but fills your pack faster.

**XP and levelling** — killing enemies grants XP. Each level raises max HP and base ATK.

---

## Asset generation

The font atlas and hit sound are pre-built and committed. If you need to regenerate them:

```bash
python3 tools/gen_font.py       # writes assets/font.raw
```

WAV files must be mono, 16-bit PCM, 22050 Hz to load correctly on KOS.

---

## License

Do whatever you want with it. Creddit me though.
