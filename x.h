#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#undef NOMINMAX
// Top tier windows trolling
#undef near
#undef far
#undef pascal
#undef cdecl

typedef SSIZE_T ssize_t;
typedef SIZE_T size_t;
typedef HANDLE file_t;
static const file_t X_FILE_T_INVALID = (file_t) INVALID_HANDLE_VALUE;
typedef struct{
	HANDLE hFind;
	char* first;
} *folder_list_t;
#else
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <dirent.h>
typedef int file_t;
typedef DIR* folder_list_t;
static const file_t X_FILE_T_INVALID = (file_t) -1;
#endif

const uint64_t X_FILE_NOT_FOUND = 0, X_FILE_UNKNOWN = 0,
	X_FILE_TYPE_FILE = 1,
	X_FILE_TYPE_FOLDER = 2,
	X_FILE_TYPE_SPECIAL = 3;
typedef struct{
	uint64_t type:8;
	uint64_t modified:56;
	uint64_t size;
} x_stat_t;

// Create a directory
static inline bool x_mkdir(const char* name);

// Delete a file or empty directory
static inline bool x_remove(const char* name);

// Open a directory for listing files
static inline folder_list_t x_opendir(const char* name);
// Get the next file in a directory listing, or 0 (NULL) if there are no more files. The returned string is valid until the next call to x_next() or x_closedir()
static inline char* x_next(folder_list_t dir);
// Close a directory listing
static inline void x_closedir(folder_list_t dir);

// Open a file from a null-terminated string specifying the pathname
static inline file_t x_open(const char* name);

// Get the size of a file in bytes
static inline size_t x_getsize(file_t fd);

// Read `count` bytes from fd, starting at offset `start`. Data is written to `buf`, which is expected to be valid, writable memory for at least `count` bytes. The number of bytes actually read is returned, which may be less than the number of bytes requested if the end of the file was found, or 0 if the file could not be read from
static inline size_t x_read(file_t fd, void* buf, size_t start, size_t count);

// Write `count` bytes to fd, starting at offset `start`. Data is read from `buf`, which is expected to be valid, readable memory for at least `count` bytes. The number of bytes actually written is returned, which may be less than the number of bytes requested under special circumstances (old systems, disk full), or 0 if the file could not be written to
static inline size_t x_write(file_t fd, const void* buf, size_t start, size_t count);

// Set the size of a file in bytes. If the size is smaller than the current size, the file is truncated, otherwise it is expanded and the additional bytes are all set to 0
static inline bool x_setsize(file_t fd, size_t sz);

// Close a file. The file_t becomes invalid before the function returns and new calls to x_open may create file_t handles that compare == to this one. It is best practice to completely forget the old file handle and never assume anything about it after it has been closed, much like you would with a pointer that has been free()'d
static inline void x_close(file_t fd);

// Maps a region of a file_t to memory, from page `off` (byte `off * X_PAGE_SIZE`) to, and excluding, page `off+sz` (byte `(off+sz) * X_PAGE_SIZE`)
// If copy == false, the memory region returned will be a live view into the file. Anything written to this memory will be written back to the file and visible to any other process that has also mapped the same region. Note that mixing writes via both x_read()/x_write() and accessing the mapped memory results in unpredictable behavior. Writes may fail, be torn, or not be visible immediately.
// If copy == true, the memory region will be populated with a copy of the file's contents, and any modifications will not be written back to the file, and effectively becomes private memory much like x_pagealloc(). It is then safe to x_read()/x_write() the region, as it will not affect the mapped memory in any way
// In either case, the returned pointer should be freed with x_pagefree()
// On failure, 0 (NULL) is returned
static inline void* x_mapfile(file_t fd, size_t off, size_t sz, bool copy);

// Allocate `sz` pages (`sz * X_PAGE_SIZE` bytes) of memory, with all bytes initially set to 0
static inline void* x_pagealloc(size_t sz);

// Free memory returned by x_pagealloc() or x_mapfile(). `sz` must exactly match the size of the allocation/mapping. Freeing only part of an allocation/mapping is not allowed.
static inline void x_pagefree(void* ptr, size_t sz);

// Check if a file exists and get metadata
static inline x_stat_t x_stat(const char* name);

// Page size used by X (always 65536). Specifically, x_mapfile()/x_pagealloc()/x_pagefree() have all their size/offset arguments measured in pages of 65536 bytes. Converting from pages to bytes or vice versa is as simple a shifting right (>>) or left (<<) 16 bits
static const size_t X_PAGE_SIZE = 65536;


#ifdef _WIN32
#include <malloc.h>

static inline file_t x_open(const char* name){
	return (file_t) CreateFileA(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_POSIX_SEMANTICS | FILE_FLAG_RANDOM_ACCESS,
	NULL);
}

static inline size_t x_getsize(file_t fd){
	LARGE_INTEGER li;
	if (!GetFileSizeEx(fd, &li)) return 0;
	return (size_t) li.QuadPart;
}

static inline size_t x_read(file_t fd, void* buf, size_t start, size_t count){
	DWORD bytesRead;
	OVERLAPPED overlapped = {0};
	overlapped.Offset = (DWORD) start;
	char* _buf = buf;
	#if SIZE_T_MAX > 0xFFFFFFFF
	overlapped.OffsetHigh = (DWORD) (start >> 32);
	if(count > 0xFFFFFFFF){
		// Do not "fix" what you do not understand
		// Ignorance sees bad code, but fast code is not for the ignorant
		// Learn the -O3 way and you will appreciate the beauty and fragility
		// of what you almost broke by being impatient
		do{
			DWORD tot = ReadFile(fd, _buf, 0xFFFFF000, &bytesRead, &overlapped) ? bytesRead : 0;
			if(tot < 0xFFFFF000)
				return (_buf-buf) + tot;
			_buf += 0xFFFFF000; count -= 0xFFFFF000;
			size_t b = _buf-buf;
			overlapped.Offset = b;
			overlapped.OffsetHigh = b>>32;
		}while(count > 0xFFFFFFFF);
		return (_buf-buf) + (ReadFile(fd, _buf, (DWORD) count, &bytesRead, &overlapped) ? bytesRead) : 0;
	}
	#endif
	return ReadFile(fd, _buf, (DWORD) count, &bytesRead, &overlapped) ? bytesRead : 0;
}

static inline size_t x_write(file_t fd, const void* buf, size_t start, size_t count){
	DWORD bytesWritten;
	OVERLAPPED overlapped = {0};
	overlapped.Offset = (DWORD) start;
	const char* _buf = buf;
	#if SIZE_T_MAX > 0xFFFFFFFF
	overlapped.OffsetHigh = (DWORD) (start >> 32);
	if(count > 0xFFFFFFFF){
		// See the comment in the x_read() implementation
		do{
			DWORD tot = WriteFile(fd, _buf, 0xFFFFF000, &bytesWritten, &overlapped) ? bytesWritten : 0;
			if(tot < 0xFFFFF000)
				return (_buf-buf) + tot;
			_buf += 0xFFFFF000; count -= 0xFFFFF000;
			size_t b = _buf-buf;
			overlapped.Offset = b;
			overlapped.OffsetHigh = b>>32;
		}while(count > 0xFFFFFFFF);
		return (_buf-buf) + (WriteFile(fd, _buf, (DWORD) count, &bytesWritten, &overlapped) ? bytesWritten : 0);
	}
	#endif
	return WriteFile(fd, _buf, (DWORD) count, &bytesWritten, &overlapped) ? bytesWritten : 0;
}

static inline bool x_setsize(file_t fd, size_t sz){
	FILE_END_OF_FILE_INFO eof;
	eof.EndOfFile.QuadPart = sz;
	return SetFileInformationByHandle(fd, FileEndOfFileInfo, &eof, sizeof(eof));
}

static inline void x_close(file_t fd){ CloseHandle(fd); }

static inline void* x_mapfile(file_t fd, size_t off, size_t sz, bool copy){
	HANDLE hMap = CreateFileMapping(fd, NULL, copy ? PAGE_WRITECOPY : PAGE_READWRITE, 0, 0, NULL);
	if (!hMap) return 0;
	void* ptr = MapViewOfFile(hMap, copy ? FILE_MAP_COPY : FILE_MAP_READ|FILE_MAP_WRITE, off >> 16, off << 16, sz << 16);
	CloseHandle(hMap);
	return ptr;
}

static inline void* x_pagealloc(size_t sz){
	return VirtualAlloc(0, sz<<16, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}
static inline void x_pagefree(void* ptr, size_t sz){
	VirtualFree(ptr, 0, MEM_RELEASE) || UnmapViewOfFile(ptr);
}

static inline x_stat_t x_stat(const char* name){
	x_stat_t ret;
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if(!GetFileAttributesExA(name, GetFileExInfoStandard, &fad)){
		ret.type = X_FILE_NOT_FOUND;
		ret.modified = 0;
		ret.size = 0;
		return ret;
	}
	ULARGE_INTEGER ull;
	ull.LowPart = fad.ftLastWriteTime.dwLowDateTime;
	ull.HighPart = fad.ftLastWriteTime.dwHighDateTime;
	// to milliseconds since unix epoch
	ret.type = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? X_FILE_TYPE_FOLDER :
		(fad.dwFileAttributes & (FILE_ATTRIBUTE_DEVICE|FILE_ATTRIBUTE_REPARSE_POINT)) ? X_FILE_TYPE_SPECIAL : X_FILE_TYPE_FILE;
	ret.modified = (ull.QuadPart - 116444736000000000ULL) / 10000;
	ull.LowPart = fad.nFileSizeLow;
	ull.HighPart = fad.nFileSizeHigh;
	ret.size = ull.QuadPart;
	return ret;
}

static inline bool x_mkdir(const char* name){
	return CreateDirectoryA(name, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static inline bool x_remove(const char* name){
	return DeleteFileA(name) || RemoveDirectoryA(name);
}

static inline folder_list_t x_opendir(const char* name){
	int len = strlen(name) + 3;
	char* buf;
	if(len < 256) buf = (char*) alloca(len);
	else buf = (char*) malloc(len);
	memcpy(buf, name, len-3);
	buf[len-3] = '\\'; buf[len-2] = '*'; buf[len-1] = 0;
	LPWIN32_FIND_DATA a;
	HANDLE h = FindFirstFileA(buf, &a);
	if(len >= 256) free(buf);
	folder_list_t ret = (folder_list_t) malloc(sizeof(folder_list_t));
	ret.hFind = h; ret.first = h == INVALID_HANDLE_VALUE ? 0 : a.cFileName;
	return ret;
}
static inline char* x_next(folder_list_t dir){
	if(dir->first){
		char* r = dir->first;
		dir->first = 0;
		return r;
	}
	WIN32_FIND_DATA a;
	return FindNextFileA(dir->hFind, &a) ? a.cFileName : 0;
}
static inline void x_closedir(folder_list_t dir){
	FindClose(dir.hFind);
	free(dir);
}

#else
#define _FILE_OFFSET_BITS 64
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

// Open a file from a null-terminated string specifying the pathname
static inline file_t x_open(const char* name){
	return (file_t) open(name, O_RDWR | O_CREAT, 0666);
}

static inline size_t x_getsize(file_t fd){
	struct stat st;
	return fstat(fd, &st) ? 0 : st.st_size;
}

static inline size_t x_read(file_t fd, void* buf, size_t start, size_t count){
	return pread(fd, buf, count, start);
}

static inline size_t x_write(file_t fd, const void* buf, size_t start, size_t count){
	return pwrite(fd, buf, count, start);
}

static inline bool x_setsize(file_t fd, size_t sz){
	return !ftruncate(fd, sz);
}

static inline void x_close(file_t fd){ close(fd); }

static inline void* x_mapfile(file_t fd, size_t off, size_t sz, bool copy){
	void* ptr = mmap(NULL, sz << 16, PROT_READ | PROT_WRITE, copy ? MAP_PRIVATE : MAP_SHARED, fd, off << 16);
	return ptr == MAP_FAILED ? 0 : ptr;
}

static inline void* x_pagealloc(size_t sz){
	void* ptr = mmap(NULL, sz << 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	return ptr == MAP_FAILED ? 0 : ptr;
}

static inline void x_pagefree(void* ptr, size_t sz){
	munmap(ptr, sz<<16);
}

static inline x_stat_t x_stat(const char* name){
	struct stat st;
	x_stat_t ret;
	if(stat(name, &st) < 0){
		ret.type = X_FILE_NOT_FOUND;
		return ret;
	}
	int a = st.st_mode & S_IFMT;
	ret.type = a == S_IFREG ? X_FILE_TYPE_FILE : a == S_IFDIR ? X_FILE_TYPE_FOLDER : X_FILE_TYPE_SPECIAL;
	int ms = st.st_mtimespec.tv_nsec/1000000;
	ret.modified = st.st_mtimespec.tv_sec*1000+ms;
	ret.size = st.st_size;
	return ret;
}

static inline bool x_mkdir(const char* name){
	return mkdir(name, 0777) == 0;
}

static inline bool x_remove(const char* name){
	return unlink(name) == 0 || (errno != EPERM && rmdir(name) == 0);
}

static inline folder_list_t x_opendir(const char* name){ return opendir(name); }
static inline char* x_next(folder_list_t dir){
	struct dirent* ent;
	ent = readdir(dir);
	return ent ? ent->d_name : 0;
}
static inline void x_closedir(folder_list_t dir){ closedir(dir); }

#endif