#include <stdint.h>

// Hard upper-limits
#define D_TABLES_MAX 512       // Must be a power of two (see D_impl_find)
#define D_STRING_MAX 32

struct D_TableRows {
    struct D_TableRows *next;  // Pointer to remaining rows (or NULL if last rows)
    uint32_t count;            // How many rows are in this chunk
    uint32_t length;           // How many bytes are in this chunk. This includes empty space at end.
                               // Last row group can have extra room for growth.
    char data[];
};

struct D_Table {
    uint32_t key;              // Hash of the name for searching
    char name[D_STRING_MAX];   // Points to table string pool
    uint32_t rowStride;        // Size of the row in bytes
    struct D_TableRows *firstRows;
    struct D_TableRows *lastRows;
};

// Initialize the database, allocating shared memory if required.
int D_connect(void);

// Release memory.
void D_disconnect(void);

// Search for a table with the given name. Returns non-zero upon failure.
// A pointer to the table reference is provided if it is found.
int D_find_table(const char *name, struct D_Table **table);

// Create a table, if it does not already exist. Tables can only be created,
// never deleted.
void D_find_or_create_table(const char *name, struct D_Table **table);

void D_insert_rows(struct D_Table *table, uint32_t rows, void *row);


#define D_foreach_row(table, row) \
    for (uint32_t i = 0; i < (table)->rowCount; ++i) \
    if (((row) = (void *)&(table)->rows[i]) && 0) ; else

