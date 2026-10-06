CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -Iraylib/include
LDFLAGS = -Lraylib/lib
LDLIBS  = -lraylib -lopengl32 -lgdi32 -lwinmm

game.exe: main.c
	$(CC) main.c -o $@ $(CFLAGS) $(LDFLAGS) $(LDLIBS)

run: game.exe
	./game.exe

clean:
	rm -f game.exe

.PHONY: run clean
