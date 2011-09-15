SOURCES=netv-recovery.c \
    keyboard.c sdl-keyboard.c \
    sdl-picker.c picker.c  \
    textbox.c sdl-textbox.c \
    wpa-controller.c ap-scan.c ufdisk.c myifup.c dhcpc.c wget.c \
    udev.c gunzip.c
OBJECTS=$(SOURCES:.c=.o)
EXEC=netv-recovery
MY_CFLAGS += `pkg-config sdl --cflags` -Wall -Werror -Os -DDANGEROUS
MY_LIBS += `pkg-config sdl --libs` -lSDL_ttf

all: $(OBJECTS)
	$(CC) $(MY_LIBS) $(LIBS) $(LDFLAGS) $(OBJECTS) -o $(EXEC)

clean:
	rm -f $(EXEC) $(OBJECTS)

.c.o:
	$(CC) -c $(CFLAGS) $(MY_CFLAGS) $< -o $@

