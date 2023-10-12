.PHONY : clean all

CC := x86_64-w64-mingw32-gcc
CFLAGS := -g -Wall -Wpedantic -Werror

all : rtxfluid.exe flip.dll
clean :
	-rm rtxfluid.exe flip.dll rtxfluid.obj database.obj plugin.obj flip.obj shmem.obj

rtxfluid.exe : rtxfluid.obj database.obj plugin.obj shmem.obj
	$(CC) $(CFLAGS) -o rtxfluid.exe rtxfluid.obj plugin.obj database.obj shmem.obj
database.obj : database.c
	$(CC) $(CFLAGS) -o database.obj -c database.c
plugin.obj : plugin.c
	$(CC) $(CFLAGS) -o plugin.obj -c plugin.c
rtxfluid.obj : rtxfluid.c
	$(CC) $(CFLAGS) -o rtxfluid.obj -c rtxfluid.c
flip.dll : flip.obj database.obj shmem.obj
	$(CC) $(CFLAGS) -shared -o flip.dll flip.obj database.obj shmem.obj
flip.obj : flip.c
	$(CC) $(CFLAGS) -o flip.obj -c flip.c
shmem.obj : shmem.c
	$(CC) $(CFLAGS) -o shmem.obj -c shmem.c