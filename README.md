# Delve

A turn-based roguelike for the Sega Dreamcast. Ten floors of procedurally generated dungeon, a handful of enemy types, items, and a carry weight system. Written in C targeting KallistiOS.

---

## Requirements

- [KallistiOS](https://github.com/KallistiOS/KallistiOS) SDK installed at `/opt/toolchains/dc/kos` (or set `$KOS_BASE`)
- `mkdcdisc` on `PATH` if you want to build a CDI image

---

## Building

I haven't tried on other computers or Windows so don't expect the build.sh to just work. 

```bash
./build.sh          # build roguelike.elf (run in emulator)
./build.sh dist     # strip and produce 1ST_READ.BIN
./build.sh cdi      # build a burnable CDI image
./build.sh clean    # remove build artifacts
```

To make it work on real hardware, burn the CDI file to a CD-R disk.

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

The font atlas is pre-built and committed. If you need to regenerate them:

```bash
python3 tools/gen_font.py       # writes assets/font.raw
```

WAV files must be mono, 16-bit PCM, 22050 Hz to load correctly on KOS.

---

## Disclaimer

This is my first real Dreamcast project so expect some bugs.

---


## License

Do whatever you want with it. Creddit me though.

<img width="2050" height="1166" alt="1" src="https://github.com/user-attachments/assets/c3da09e7-8601-429c-af0b-0ccbd21b5e84" />
<img width="2050" height="1166" alt="2" src="https://github.com/user-attachments/assets/01f0d4ba-93fc-4a4e-a553-df12e56967c2" />
<img width="2050" height="1166" alt="3" src="https://github.com/user-attachments/assets/53008bfb-10c2-4350-b050-5b08128f945d" />
