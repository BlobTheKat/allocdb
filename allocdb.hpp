#include <cstdint>
#include <string>

class AllocDB{
	public:
	// Construct an AllocDB from path pointing to folder. The folder will be created if it does not exist.
	AllocDB(std::string folder);
	// Destruct and flush an AllocDB
	~AllocDB();
	// Calculate the size of a block pointed to by ptr. This would be equal to the size allocated by alloc(). The block does not have to be currently allocated for the size to be calculatable. If ptr is obviously invalid, 0 is returned.
	static uint64_t size_of(uint64_t ptr);
	// Flush all internal state to disk. This is automatically called when the AllocDB is destroyed, but can be called manually to ensure data is on disk at a specific point in time. This function is atomic with respect to other write and flush operations, except when it is called by the destructor. You should not destroy the database until all other operations have returned.
	void flush();

	// Get the root pointer. The root pointer is a 64-bit value that is not interpreted by AllocDB, but is guaranteed to be persistent across restarts of the database. It can be used by the user to point to some important structure in the database, such as an index or tree root node. Default value is -1
	uint64_t root();
	// Set the root pointer. See the other overload, `root()`.
	void root(uint64_t r);

	// Allocate a block of at least `size` bytes. The actual size allocated is written back to `size`. Returns an ID pointing to the allocated block, or -1 on failure.
	uint64_t alloc(uint64_t& size);
	// Free a block previously allocated with alloc(). If ptr is obviously invalid, the function does nothing.
	void free(uint64_t ptr);
	// Read the entire contents of the block pointed to by ptr into buf. The size of the block is determined by size_of(ptr), which is equal to the size allocated by alloc(). Returns true on success, false on failure (for example, if ptr is obviously invalid, or if the underlying read operation fails)
	bool read(uint64_t ptr, void* buf);
	// Write the entire contents of buf to the block pointed to by ptr. The size of the block is determined by size_of(ptr), which is equal to the size allocated by alloc(). Returns true on success, false on failure (for example, if ptr is obviously invalid, or if the underlying write operation fails)
	bool write(uint64_t ptr, const void* buf);
};
