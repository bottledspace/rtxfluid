.PHONY : clean all

CC := x86_64-w64-mingw32-gcc
CFLAGS := -g -Wall -Wpedantic -Werror
OBJS := rtxfluid.obj scene.obj plugin.obj flip.obj
all : rtxfluid.exe flip.dll
clean :
	-rm rtxfluid.exe flip.dll $(OBJS)

rtxfluid.exe : $(OBJS)
	$(CC) $(CFLAGS) -o rtxfluid.exe rtxfluid.obj plugin.obj scene.obj
scene.obj : scene.c
	$(CC) $(CFLAGS) -o scene.obj -c scene.c
plugin.obj : plugin.c
	$(CC) $(CFLAGS) -o plugin.obj -c plugin.c
rtxfluid.obj : rtxfluid.c
	$(CC) $(CFLAGS) -o rtxfluid.obj -c rtxfluid.c
flip.dll : flip.obj scene.obj
	$(CC) $(CFLAGS) -shared -o flip.dll flip.obj scene.obj
flip.obj : flip.c
	$(CC) $(CFLAGS) -o flip.obj -c flip.c