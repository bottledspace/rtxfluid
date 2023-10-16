#include <stdint.h>

struct D_Table;

// Hard upper-limits
#define D_TABLES_MAX 512       // Must be a power of two (see D_impl_find)
#define D_STRING_MAX 16
#define D_COLUMNS_MAX 32

#define D_TYPE_STRING 1
#define D_TYPE_INT    2

struct D_ColumnDesc {
    const char *name;
    size_t size;
};
struct D_TableDesc {
    const char *name;
    size_t rowStep;
};

#define D_TABLE(structName) \
    const static struct D_TableDesc table_##structName = { #structName, sizeof(struct structName) }; \
    const static struct D_ColumnDesc columns_##structName[] =

#define D_COLUMN(columnName, type) \
    { #columnName, sizeof(type) }

#define D_TABLE_DESC(structName) \
    &table_##structName, \
    ARRAYSIZE(columns_##structName), \
    columns_##structName

// Initialize the database, allocating shared memory if required.
int D_connect(void);

// Release memory.
void D_disconnect(void);



uint32_t D_hash(const char *str);

// Search for a table with the given name. Returns non-zero upon failure.
// A pointer to the table reference is provided if it is found.
//struct D_Table *D_find_table(const struct D_TableDesc *table);

// Create a table, if it does not already exist. Tables can only be created,
// never deleted.
void D_create_table(struct D_Table **table,
                    const struct D_TableDesc *tableDesc,
                    size_t columnCount,
                    const struct D_ColumnDesc *columns);

void D_insert_rows(struct D_Table *table,
                   size_t numRows,
                   void **rows);

size_t D_blob_alloc(size_t length);
void D_blob_free(size_t blobOffset);
void *D_blob_pointer(size_t blobOffset);
