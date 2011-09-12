SOURCES=netv-recovery.c \
    keyboard.c sdl-keyboard.c \
    sdl-picker.c picker.c  \
    textbox.c sdl-textbox.c \
    wpa-controller.c
OBJECTS=$(SOURCES:.c=.o)
EXEC=netv-recovery
CFLAGS += `pkg-config sdl --cflags` -Wall -g
LIBS += `pkg-config sdl --libs` -lSDL_ttf

all: $(OBJECTS)
	$(CC) $(LIBS) $(OBJECTS) -o $(EXEC)

clean:
	rm -f $(EXEC) $(OBJECTS)

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

