# AllocDB

Database with similar semantics to a memory allocator

```cpp
class AllocDB{
	// Construct an AllocDB from path pointing to folder. The folder will be created if it does not exist.
	AllocDB(std::string folder);

	// Destruct and flush an AllocDB
	~AllocDB();


	// Calculate the size of a block pointed to by ptr. This would be equal to the size allocated by alloc(). The block does not have to be currently allocated for the size to be calculatable. If ptr is obviously invalid, 0 is returned.
	static uint64_t size_of(uint64_t ptr);


	// Flush all internal state to disk. This is automatically called when the AllocDB is destroyed, but can be called manually to ensure data is on disk at a specific point in time. This function is atomic with respect to other write and flush operations, except when it is called by the destructor. You should not destroy the database until all other operations have returned.
	void flush();



	// Allocate a block of at least 'size' bytes. The actual size allocated is written back to 'size'. Returns an ID pointing to the allocated block, or -1 on failure.
	uint64_t alloc(uint64_t& size);


	// Free a block previously allocated with alloc(). If ptr is obviously invalid, the function does nothing.
	void free(uint64_t ptr);


	// Read the entire contents of the block pointed to by ptr into buf. The size of the block is determined by size_of(ptr), which is equal to the size allocated by alloc(). Returns true on success, false on failure (for example, if ptr is obviously invalid, or if the underlying read operation fails)
	bool read(uint64_t ptr, void* buf);


	// Write the entire contents of buf to the block pointed to by ptr. The size of the block is determined by size_of(ptr), which is equal to the size allocated by alloc(). Returns true on success, false on failure (for example, if ptr is obviously invalid, or if the underlying write operation fails)
	bool write(uint64_t ptr, const void* buf);

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