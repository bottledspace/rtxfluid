.PHONY : clean all

CC := x86_64-w64-mingw32-gcc
CFLAGS := -g -Wall -Wpedantic -Werror -Wno-unused-function

all : rtxfluid.exe flip.dll
clean :
	-rm rtxfluid.exe flip.dll rtxfluid.obj database.obj plugin.obj flip.obj

rtxfluid.exe : rtxfluid.obj database.obj plugin.obj
	$(CC) $(CFLAGS) -o rtxfluid.exe rtxfluid.obj plugin.obj database.obj
database.obj : database.c
	$(CC) $(CFLAGS) -o database.obj -c database.c
plugin.obj : plugin.c
	$(CC) $(CFLAGS) -o plugin.obj -c plugin.c
rtxfluid.obj : rtxfluid.c
	$(CC) $(CFLAGS) -o rtxfluid.obj -c rtxfluid.c
flip.dll : flip.obj database.obj
	$(CC) $(CFLAGS) -shared -o flip.dll flip.obj database.obj
flip.obj : flip.c
	$(CC) $(CFLAGS) -o flip.obj -c flip.c
