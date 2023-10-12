#include "database.h"
#include "shmem.h"
#include <windows.h>
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>



struct D_TableRegistry {
    struct D_Table refs[D_TABLES_MAX];
    struct D_TableRows *freeRows;  // The last row is always empty
};
struct shmem_MemoryBlock memoryBlock;

#define tableRegistry  ((struct D_TableRegistry *)memoryBlock.view)  

int D_connect(void)
{
    memoryBlock.size = sizeof(struct D_TableRegistry) + 1024*1024;
    memoryBlock.name = "0fcc37cb-9dd8-4aae-ab0a-0748d788311f";
    shmem_alloc(&memoryBlock);

    // Assign the last row chunk to the remaining empty space.
    tableRegistry->freeRows = (struct D_TableRows *)((char *)memoryBlock.view + sizeof(struct D_TableRegistry));
    tableRegistry->freeRows->length = memoryBlock.size - (tableRegistry->freeRows->data - (char *)memoryBlock.view);
    tableRegistry->freeRows->count = 0;
    tableRegistry->freeRows->next = NULL;

    printf("D_connect: There are %u free bytes.\n", tableRegistry->freeRows->length);

    return EXIT_SUCCESS;
}


void D_disconnect()
{
    shmem_free(&memoryBlock);
}


static struct D_TableRows *D_impl_alloc_rows(uint32_t length)
{
    // Not enough free space to create more rows, time to grow the memory.
    while (tableRegistry->freeRows->length < length) {
        printf("D_impl_alloc_rows: Grow memory.\n");

        shmem_grow(&memoryBlock);
        uint32_t offset = tableRegistry->freeRows->data - (char *)memoryBlock.view;
        tableRegistry->freeRows->length = memoryBlock.size - offset;
    }
    printf("D_impl_alloc_rows: There are %u free bytes. We need %u bytes.\n", tableRegistry->freeRows->length, length);

    uint32_t freeBytes = tableRegistry->freeRows->length - length;

    // Carve out some of the empty space for our rows.
    struct D_TableRows *rows = tableRegistry->freeRows;
    rows->length = length;
    rows->next = NULL;
    rows->count = 0;

    // Adjust remaining free space.
    tableRegistry->freeRows = (struct D_TableRows *)(rows->data + length);
    tableRegistry->freeRows->length = freeBytes;
    tableRegistry->freeRows->count = 0;
    tableRegistry->freeRows->next = NULL;

    return rows;
}


void D_insert_rows(struct D_Table *table, uint32_t count, void *row)
{
    uint32_t offset = table->lastRows->count * table->rowStride;
    uint32_t length = count * table->rowStride;
    
    // If there isn't enough room we assume its not worth splitting and create
    // an entirely new chunk for the rows.
    if (offset + length > table->lastRows->length) {
        printf("D_insert_rows: Last chunk has %d bytes, we need %d bytes. Allocating new chunk.\n", table->lastRows->length - offset, length);
        table->lastRows->next = D_impl_alloc_rows(length);
        table->lastRows = table->lastRows->next;
        offset = 0;
    }

    // Copy into the empty space of the last chunk.
    memcpy(table->lastRows->data + offset, row, length);
    table->lastRows->count += count;
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
        if (tableRegistry->refs[i].key == key
         && strcmp(tableRegistry->refs[i].name, name) == 0) {
            *index = i;
            return EXIT_SUCCESS;
        }
        i = (i + k) % D_TABLES_MAX;
        k++;
    } while (tableRegistry->refs[i].key);
    
    *index = i;
    return EXIT_FAILURE;
}


int D_find_table(const char *name, struct D_Table **table)
{
    uint32_t key = D_impl_hash(name);
    size_t h;
    if (D_impl_find(name, key, &h)) {
        return EXIT_FAILURE;
    }
    *table = &tableRegistry->refs[h];
    return EXIT_SUCCESS;
}


void D_find_or_create_table(const char *name, struct D_Table **table)
{
    uint32_t key = D_impl_hash(name);
    size_t h;
    if (D_impl_find(name, key, &h)) {
        assert(!tableRegistry->refs[h].key);
        tableRegistry->refs[h].key = key;
        strncpy(tableRegistry->refs[h].name, name, D_STRING_MAX);
        tableRegistry->refs[h].lastRows = D_impl_alloc_rows(512);
    }
    *table = &tableRegistry->refs[h];
}
