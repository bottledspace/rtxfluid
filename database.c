#include "database.h"
#include <windows.h>
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#define D_SHMEM_KEY  "0fcc37cb-9dd8-4aae-ab0a-0748d788311f"
#define D_SHMEM_MAX  (1024ull*1024ull*1024ull*4ull)  // 4 gigs
#define D_SHMEM_PAGE 4096

// No pointers may be stored in the state, since the address space used by each
// process might be different. 
struct D_Block {
    int isOccupied;
    size_t nextBlockOffset;
};
struct D_Table {
    struct D_TableDescriptor desc;
    uint32_t key;              // Hash of the name for searching
    size_t blockOffset;
};
struct D_State {
    HANDLE arenaFile;
    size_t arenaLength;
    size_t lastBlockOffset;
    struct D_Table tables[D_TABLES_MAX];
    struct D_Block blocks[];
};
static struct D_State *state;

#define D_BLOCK_AT(offset) \
    (struct D_Block *)((char *)state + (offset))



static void D_impl_free_block(size_t offset)
{
    struct D_Block *block = D_BLOCK_AT(offset);
    block->isOccupied = 0;
}


static size_t D_impl_alloc_block(size_t length)
{
    size_t actualLength = D_SHMEM_PAGE;
    while (actualLength < length+sizeof(struct D_Block)) {
        actualLength *= 2;
    }

    size_t offset = offsetof(struct D_State, blocks);
    for (;;) {
        struct D_Block *block = D_BLOCK_AT(offset);
        
        if (!block->isOccupied && (block->nextBlockOffset - offset) == actualLength) {
            block->isOccupied = 1;
            return offset;
        
        } else if (state->lastBlockOffset == offset) {
            state->arenaLength = state->lastBlockOffset + actualLength + D_SHMEM_PAGE;
            state->lastBlockOffset += actualLength;

            block->isOccupied = 1;
            block->nextBlockOffset = state->lastBlockOffset;

            (void)VirtualAlloc(state, state->arenaLength, MEM_COMMIT, PAGE_READWRITE);

            struct D_Block *lastBlock = D_BLOCK_AT(state->lastBlockOffset);
            lastBlock->nextBlockOffset = state->lastBlockOffset + D_SHMEM_PAGE;
            lastBlock->isOccupied = 0;
            return offset;
        }
        offset = block->nextBlockOffset;
    }
}


int D_connect(void)
{
    assert(sizeof(struct D_Block) < D_SHMEM_PAGE);

    // Reserve (but do not allocate) the maximum possible size.
    HANDLE file = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_RESERVE,
                                     D_SHMEM_MAX >> 32, (DWORD)D_SHMEM_MAX, D_SHMEM_KEY);
    if (!file) {
        return EXIT_FAILURE;
    }
    
    // Now we allocate address space for the memory.
    state = MapViewOfFile(file, FILE_MAP_WRITE, 0, 0, 0);
    if (!state) {
        return EXIT_FAILURE;
    }

    // Finally we commit some of the arena to fit the header and initial requested size.
    (void)VirtualAlloc(state, sizeof(struct D_State) + D_SHMEM_PAGE, MEM_COMMIT, PAGE_READWRITE);
    
    // If we are the first process to create the arena, fill its header.
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        state->arenaFile = file;
        state->arenaLength = sizeof(struct D_State) + D_SHMEM_PAGE;
        state->lastBlockOffset = offsetof(struct D_State, blocks);
        state->blocks[0].isOccupied = 0;
        state->blocks[0].nextBlockOffset = state->lastBlockOffset + D_SHMEM_PAGE;
    } else {

        D_impl_alloc_block(4100);
        D_impl_alloc_block(100);
        D_impl_free_block(D_impl_alloc_block(100));
    }
    return EXIT_SUCCESS;
}


void D_disconnect()
{
    HANDLE file = state->arenaFile;

    (void)UnmapViewOfFile(state);
    (void)CloseHandle(file);
}


static uint32_t D_impl_hash(const char *str)
{
    // Use the SDBM algorithm (see http://www.cse.yorku.ca/~oz/hash.html)

    uint32_t hash = 0;
    while (*str) {
        hash = (uint32_t)*str + (hash << 6) + (hash << 16) - hash;
        str++;
    }
    return hash;
}


static int D_impl_find(const char *name, uint64_t key, size_t *index)
{
    // Here we use the triangular numbers to probe the hash-table. Since the
    // size of the table is a power of two, this will eventually search every
    // slot. As long as we never overfill the table, we can be sure that this
    // will return.

    size_t i = key % D_TABLES_MAX;
    size_t k = 1;
    do {
        if (state->tables[i].key == key
         && strcmp(state->tables[i].desc.name, name) == 0) {
            *index = i;
            return EXIT_SUCCESS;
        }
        i = (i + k) % D_TABLES_MAX;
        k++;
    } while (state->tables[i].key);
    
    *index = i;
    return EXIT_FAILURE;
}


struct D_Table *D_find_table(const char *name)
{
    uint32_t key = D_impl_hash(name);
    size_t h;
    if (!D_impl_find(name, key, &h)) {
        return &state->tables[h];
    }
    return NULL;
}


struct D_Table *D_create_table(struct D_TableDescriptor *tableDesc)
{
    uint32_t key = D_impl_hash(tableDesc->name);
    size_t h;
    if (D_impl_find(tableDesc->name, key, &h)) {
        assert(!state->tables[h].key);
        state->tables[h].key = key;
        memcpy(&state->tables[h].desc, tableDesc, sizeof(struct D_TableDescriptor));
    }
    return &state->tables[h];
}
