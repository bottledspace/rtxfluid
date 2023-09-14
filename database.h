#include <stdint.h>

// Hard upper-limits
#define D_TABLES_MAX 512       // Must be a power of two (see D_impl_find)
#define D_STRING_MAX 32

struct D_Table;
struct D_TableDescriptor {
    char name[D_STRING_MAX];
};

// Initialize the database, allocating shared memory if required.
int D_connect(void);

// Release memory.
void D_disconnect(void);

// Search for a table with the given name. Returns non-zero upon failure.
// A pointer to the table reference is provided if it is found.
struct D_Table *D_find_table(const char *name);

// Create a table, if it does not already exist. Tables can only be created,
// never deleted.
struct D_Table *D_create_table(struct D_TableDescriptor *tableDesc);
