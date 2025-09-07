# AllocDB

Database with similar semantics to a memory allocator

```cpp
class AllocDB{
	// Construct an AllocDB from path pointing to folder.
	// The folder will be created if it does not exist
	AllocDB(std::string folder);

	// Destruct and flush an AllocDB
	~AllocDB();



	// Allocate a block of at least `size` bytes.
	// The actual size allocated is written back to `size`
	// Returns an ID pointing to the allocated block, or -1 on failure
	uint64_t alloc(uint64_t& size);


	// Free a block previously allocated with alloc().
	// If ptr is obviously invalid (e.g -1), the function does nothing
	void free(uint64_t ptr);


	// Read the entire contents of the block pointed to by ptr into buf.
	// The size of the block is determined by size_of(ptr),
	// which is equal to the size allocated by alloc()
	// Returns true on success, false on failure
	// (e.g, ptr is obviously invalid / the underlying read operation fails)
	bool read(uint64_t ptr, void* buf);


	// Write the entire contents of buf to the block pointed to by ptr.
	// The size of the block is determined by size_of(ptr),
	// which is equal to the size allocated by alloc()
	// Returns true on success, false on failure
	// (e.g, ptr is obviously invalid / the underlying write operation fails)
	bool write(uint64_t ptr, const void* buf);


	// Calculate the size of a block pointed to by ptr.
	// This would be equal to the size allocated by alloc()
	// The block does not have to be currently allocated for the size
	// to be calculatable. If ptr is obviously invalid, 0 is returned
	static uint64_t size_of(uint64_t ptr);


	// Flush all internal state to disk.
	// This is automatically called when the AllocDB is destroyed,
	// but can be called manually to ensure all previous writes have completed
	// This function is atomic with respect to other write and flush operations,
	// except when it is called by the destructor
	// You should not destroy the object until all other operations complete
	void flush();

	// Get/set the root pointer. The root pointer is a uint64_t that is not interpreted
	// by AllocDB, but is guaranteed to be persistent across restarts of the database.
	// It can be used by the user to point to some important structure in the database,
	// such as an index or tree root node. Default value is -1
	uint64_t root();
	void root(uint64_t r);
	
};
```

C headers and docs can be found in `ffi.h`

# Building

As a C++ library, you need only include `allocdb.cpp` and no build step is required. If you want to build anyway (e.g incremental builds), `allocdb.hpp` is provided as a header you can include instead. It is recommended you build with `-flto` and `-fno-exceptions` for maximum performance

```sh
# Example
clang++ -c -fPIC -O3 -flto -fno-exceptions allocdb.cpp -o tmp.o
ar rcs liballocdb.a tmp.o
rm tmp.o
```

The C library can be built similarly (a build step is required if your project is in C as you can't include .cpp files from C). Build `ffi.cpp` with a C++ compiler

```sh
# Example
clang++ -c -fPIC -O3 -flto -fno-exceptions ffi.cpp -o tmp.o
ar rcs liballocdb-c.a tmp.o
rm tmp.o
```

Look at `script.sh` for more info

# License

This project is made available under the [CC-BY-NC-SA 4.0 license](https://creativecommons.org/licenses/by-nc-sa/4.0/deed.en), attribute your work to `BlobTheKat` or `Matthew Reiner`