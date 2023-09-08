// Initialize the scene, allocating shared memory if required.
int S_init(void);

// Detach from host, freeing the shared memory file.
void S_deinit(void);


struct Node *S_find(const char *name);

struct Node *S_find_or_create(const char *name);