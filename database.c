#define _CRT_SECURE_NO_WARNINGS 1
#include "database.h"
#include <windows.h>
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#define D_SHMEM_KEY  "0fcc37cb-9dd8-4aae-ab0a-0748d788311f"
#define D_SHMEM_MAX  (1024ull*1024ull*1024ull*4ull)  // 4 gigs
#define D_SHMEM_PAGE 4096
#define D_SHMEM_ADDR ((LPVOID)0x000001ad34a70000)

struct D_Block {
    size_t length;
    int isOccupied;
};
struct D_Column {
    char name[D_STRING_MAX];
    int type;     // = D_TYPE_*
};
struct D_Table {
    char name[D_STRING_MAX];
    uint32_t nameHash;
    size_t rowStep;
    size_t rowCount;
    size_t rowCapacity;
    size_t rowBlockOffset;
};
struct D_State {
    HANDLE arenaFile;
    size_t arenaLength;
    struct D_Table tables[D_TABLES_MAX];
};
static struct D_State *state;

#define D_BLOCK_FIRST(state) ((struct D_Block *)((char *)(state) + sizeof(struct D_State)))
#define D_BLOCK_NEXT(block) ((struct D_Block *)((char *)(block) + (block)->length))
#define D_BLOCK_END(state)  ((struct D_Block *)((char *)(state) + (state)->arenaLength))


size_t D_blob_alloc(size_t length)
{
    size_t actualLength = D_SHMEM_PAGE;
    while (actualLength < length+sizeof(struct D_Block)) {
        actualLength *= 2;
    }

    struct D_Block *block = D_BLOCK_FIRST(state);
    for (;;) {
        // Block can be an invalid pointer, we need to check first!
        if (block == D_BLOCK_END(state)) {
            state->arenaLength += actualLength;
            (void)VirtualAlloc(state, state->arenaLength, MEM_COMMIT, PAGE_READWRITE);

            block->length = actualLength;
            break;
        } else if (!block->isOccupied && block->length == actualLength) {
            break;
        }
        block = D_BLOCK_NEXT(block);
    }
    block->isOccupied = 1;

    return (char *)block - (char *)state;
}


void D_blob_free(size_t blobOffset)
{
    struct D_Block *block = (struct D_Block *)((char *)state + blobOffset);
    block->isOccupied = 0;
}


size_t D_blob_capacity(size_t blobOffset)
{
    struct D_Block *block = (struct D_Block *)((char *)state + blobOffset);
    return block->length;
}


void *D_blob_pointer(size_t blobOffset)
{
    return ((char *)state + blobOffset + sizeof(struct D_Block));
}


int D_connect(void)
{
    // Reserve (but do not allocate) the maximum possible size.
    HANDLE file = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_RESERVE,
                                     D_SHMEM_MAX >> 32, (DWORD)D_SHMEM_MAX, D_SHMEM_KEY);
    int isFirstConnection = (GetLastError() != ERROR_ALREADY_EXISTS);
    assert(file);
    
    // Map the shared memory into our address space. The address which is chosen
    // will likely be different for every process.
    state = MapViewOfFile(file, FILE_MAP_WRITE, 0, 0, 0);
    assert(state);

    // Request enough space be allocated for the state. This is safe to do even
    // if the region has been allocated previously.
    (void)VirtualAlloc(state, sizeof(struct D_State) + D_SHMEM_PAGE, MEM_COMMIT, PAGE_READWRITE);

    if (isFirstConnection) {
        state->arenaLength = sizeof(struct D_State) + D_SHMEM_PAGE;

        struct D_Block *block = D_BLOCK_FIRST(state);
        block->isOccupied = 0;
        block->length = D_SHMEM_PAGE;

        assert(D_BLOCK_NEXT(block) == D_BLOCK_END(state) && "Sanity check.");
    }
    return EXIT_SUCCESS;
}


void D_disconnect()
{
    HANDLE file = state->arenaFile;
    // VirtualFree is unncessary in the case of mapped memory.
    (void)UnmapViewOfFile(state);
    (void)CloseHandle(file);
}


uint32_t D_hash(const char *str)
{
    // Use the SDBM algorithm (see http://www.cse.yorku.ca/~oz/hash.html)
    uint32_t hash = 0;
    while (*str) {
        hash = (uint32_t)*str + (hash << 6) + (hash << 16) - hash;
        str++;
    }
    return hash;
}


static int D_impl_find(const char *name, uint64_t nameHash, size_t *index)
{
    // Here we use the triangular numbers to probe the hash-table. Since the
    // size of the table is a power of two, this will eventually search every
    // slot. As long as we never overfill the table, we can be sure that this
    // will return.

    size_t i = nameHash % D_TABLES_MAX;
    size_t k = 1;
    do {
        if (state->tables[i].nameHash == nameHash
         && strcmp(state->tables[i].name, name) == 0) {
            *index = i;
            return EXIT_SUCCESS;
        }
        i = (i + k) % D_TABLES_MAX;
        k++;
    } while (state->tables[i].nameHash);
    
    *index = i;
    return EXIT_FAILURE;
}


#if 0
size_t D_find_table(const struct D_TableDesc *tableDesc)
{
    uint32_t key = D_impl_hash(tableDesc->name);
    size_t h;
    if (!D_impl_find(tableDesc->name, key, &h)) {
        return &state->tables[h];
    }
    return NULL;
}
#endif 


void D_create_table(struct D_Table **table,
                    const struct D_TableDesc *tableDesc,
                    size_t columnCount,
                    const struct D_ColumnDesc *columns)
{
    uint32_t hash = D_hash(tableDesc->name);
    size_t h;
    if (D_impl_find(tableDesc->name, hash, &h)) {
        // Allocate an initial space for the table rows.
        size_t length = tableDesc->rowStep * 128;
        size_t blob = D_blob_alloc(length);
        length = D_blob_capacity(blob);  // Get actual size given to us.

        *table = &state->tables[h];
        strncpy((*table)->name, tableDesc->name, D_STRING_MAX);
        (*table)->nameHash = hash;
        (*table)->rowStep = tableDesc->rowStep;
        (*table)->rowCount = 0;
        (*table)->rowBlockOffset = blob;
        (*table)->rowCapacity = length / tableDesc->rowStep;
    }
}


void D_insert_rows(struct D_Table *table,
                   size_t numRows,
                   void **rows)
{
    // If there isnt enough room for the new rows we need to allocate a larger
    // space for the table and copy over the current rows.
    if (table->rowCount + numRows > table->rowCapacity) {
        size_t length = (table->rowCount + numRows) * table->rowStep;
        size_t blob = D_blob_alloc(length);
        length = D_blob_capacity(blob);  // Get actual size given to us.

        memcpy(D_blob_pointer(blob),
               D_blob_pointer(table->rowBlockOffset),
               sizeof(table->rowCount * table->rowStep));
        
        D_blob_free(table->rowBlockOffset);
        table->rowCapacity = length / table->rowStep;
        table->rowBlockOffset = blob;
    }
    *rows = ((char *)D_blob_pointer(table->rowBlockOffset) + table->rowCount * table->rowStep);
    table->rowCount += numRows;
}