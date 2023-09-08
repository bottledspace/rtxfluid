#include <windows.h>
#include <memory.h>
#include <stdint.h>
#include <stdio.h>

#define SHMEMSIZE 4096

// We need a unique name for the shared file, so we simply use a randomized
// string long enough to make it unlikely to ever have a collision.
#define MAP_NAME "pnAVV53ttICRsZCVA4kz48wsVLyjUKQf"

struct Node {
    char name[64];
};


static HANDLE mapFile = NULL;
static uint8_t *memBuffer = NULL;

int S_init(void)
{
    mapFile = CreateFileMappingA(
            INVALID_HANDLE_VALUE,   // use paging file
            NULL,                   // default security attributes
            PAGE_READWRITE,         // read/write access
            0,                      // size: high 32-bits
            SHMEMSIZE,              // size: low 32-bits
            MAP_NAME); // name of map object
    if (!mapFile) {
        fprintf(stderr, "ERROR: Failed to create mapping file.");
        return EXIT_FAILURE;
    }
    memBuffer = MapViewOfFile(
            mapFile,        // object to map view of
            FILE_MAP_WRITE, // read/write access
            0,              // high offset:  map from
            0,              // low offset:   beginning
            0);             // default: map entire file
    if (!memBuffer) {
        fprintf(stderr, "ERROR: Failed map buffer.");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

void S_deinit()
{
    UnmapViewOfFile(memBuffer);
    CloseHandle(mapFile);
}

struct Node *S_find(const char *name)
{
    return NULL;
}

struct Node *S_find_or_create(const char *name)
{
    return NULL;
}
