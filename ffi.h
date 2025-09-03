struct AllocDB;

// Init an AllocDB from path pointing to folder. The folder will be created if it does not exist.
AllocDB* allocdb_create(const char* folder);
// Teardown and flush an AllocDB
void allocdb_destroy(AllocDB* db);
// Flush all internal state to disk. This is automatically called when the AllocDB is destroyed, but can be called manually to ensure data is on disk at a specific point in time. This function is atomic with respect to other write and flush operations, except when it is called from the teardown function. You should not teardown the database until all other operations have returned.
void allocdb_flush(AllocDB* db);
// Calculate the size of a block pointed to by ptr. This would be equal to the size allocated by allocdb_alloc(). The block does not have to be currently allocated for the size to be calculatable. If ptr is obviously invalid, 0 is returned.
uint64_t allocdb_size_of(uint64_t ptr);

// Allocate a block of at least *size bytes. The actual size allocated is written back to *size. Returns an ID pointing to the allocated block, or -1 on failure.
uint64_t allocdb_alloc(AllocDB* db, uint64_t* size);
// Free a block previously allocated with allocdb_alloc(). If ptr is obviously invalid, the function does nothing.
void allocdb_free(AllocDB* db, uint64_t ptr);
// Read the entire contents of the block pointed to by ptr into buf. The size of the block is determined by allocdb_size_of(ptr), which is equal to the size allocated by allocdb_alloc(). Returns true on success, false on failure (for example, if ptr is obviously invalid, or if the underlying read operation fails)
bool allocdb_read(AllocDB* db, uint64_t ptr, void* buf);
// Write the entire contents of buf to the block pointed to by ptr. The size of the block is determined by allocdb_size_of(ptr), which is equal to the size allocated by allocdb_alloc(). Returns true on success, false on failure (for example, if ptr is obviously invalid, or if the underlying write operation fails)
bool allocdb_write(AllocDB* db, uint64_t ptr, const void* buf);