TARGET = roguelike.elf
OBJS   = src/main.o src/console.o src/dungeon.o src/fov.o \
         src/entity.o src/game.o assets/font.o assets/hit.o

KOS_CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -Isrc

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

rm-elf:
	-rm -f $(TARGET)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

assets/font.raw:
	python3 tools/gen_font.py

assets/font.o: assets/font.raw
	$(KOS_BASE)/utils/bin2c/bin2c $< font_tmp.c font_atlas
	kos-cc -o $@ -c font_tmp.c
	rm -f font_tmp.c

assets/hit.o: assets/hit.wav
	$(KOS_BASE)/utils/bin2c/bin2c $< hit_tmp.c hit_wav
	kos-cc -o $@ -c hit_tmp.c
	rm -f hit_tmp.c

clean:
	-rm -f $(OBJS) $(TARGET)
	-rm -f assets/font.raw assets/font.o font_tmp.c
	-rm -f assets/hit.o hit_tmp.c

dist: $(TARGET)
	$(KOS_STRIP) $(TARGET)
	kos-objcopy -O binary $(TARGET) roguelike.bin
	$(KOS_BASE)/utils/scramble/scramble roguelike.bin 1ST_READ.BIN

# Build a burnable CDI image for real hardware.
# Requires mkdcdisc (gitlab.com/simulant/mkdcdisc) on PATH.
cdi: $(TARGET)
	mkdcdisc -e $(TARGET) -i IP.BIN -o roguelike.cdi -n "DUNGEON CRAWLER DC"
	@echo ""
	@echo "CDI ready: roguelike.cdi"
	@echo "Burn with: sudo dcdib -s 4 -b /dev/sr0 roguelike.cdi"

.PHONY: all rm-elf clean dist cdi
