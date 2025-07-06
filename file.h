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
static inline file_t x_open(const char* name){
	return (file_t) CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 3, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_POSIX_SEMANTICS | FILE_FLAG_RANDOM_ACCESS, NULL);
}
#define X_OPEN_FAILED ((file_t) INVALID_HANDLE_VALUE)
static inline size_t x_sizeof(file_t fd){
	struct BY_HANDLE_FILE_INFORMATION st;
	return GetFileInformationByHandle(fd, &st) ?
	#if SIZE_T_MAX > 0xFFFFFFFF
	((size_t)st.nFileSizeHigh)<<32 |
	#endif
	((size_t)st.nFileSizeLow) : 0;
}
static inline size_t x_read(file_t fd, void* buf, size_t start, size_t count){
	DWORD bytesRead;
	OVERLAPPED overlapped = {0};
	overlapped.Offset = (DWORD) start;
	#if SIZE_T_MAX > 0xFFFFFFFF
	overlapped.OffsetHigh = (DWORD) (start >> 32);
	if(count > 0xFFFFFFFF){
		do{
			DWORD tot = ReadFile(fd, buf, 0xFFFFFFFF, &bytesRead, &overlapped) ? bytesRead : 0;
			if(~tot){
				size_t a = (DWORD)start - overlapped.Offset;
				return (a<<32)-a + tot;
			}
			overlapped.Offset--; overlapped.OffsetHigh++;
			buf += 0xFFFFFFFF; count -= 0xFFFFFFFF;
		}while(count > 0xFFFFFFFF);
		size_t a = (DWORD)start - overlapped.Offset;
		return (a<<32)-a + (ReadFile(fd, buf, (DWORD) count, &bytesRead, &overlapped) ? bytesRead) : 0;
	}
	#endif
	return ReadFile(fd, buf, (DWORD) count, &bytesRead, &overlapped) ? bytesRead : 0;
}
static inline size_t x_write(file_t fd, const void* buf, size_t start, size_t count){
	DWORD bytesWritten;
	OVERLAPPED overlapped = {0};
	overlapped.Offset = (DWORD) start;
	#if SIZE_T_MAX > 0xFFFFFFFF
	overlapped.OffsetHigh = (DWORD) (start >> 32);
	if(count > 0xFFFFFFFF){
		do{
			DWORD tot = WriteFile(fd, buf, 0xFFFFFFFF, &bytesWritten, &overlapped) ? bytesWritten : 0;
			if(~tot){
				size_t a = (DWORD)start - overlapped.Offset;
				return (a<<32)-a + tot;
			}
			overlapped.Offset--; overlapped.OffsetHigh++;
			buf += 0xFFFFFFFF; count -= 0xFFFFFFFF;
		}while(count > 0xFFFFFFFF);
		size_t a = (DWORD)start - overlapped.Offset;
		return (a<<32)-a + (WriteFile(fd, buf, (DWORD) count, &bytesWritten, &overlapped) ? bytesWritten : 0);
	}
	#endif
	return WriteFile(fd, buf, (DWORD) count, &bytesWritten, &overlapped) ? bytesWritten : 0;
}
static inline void x_close(file_t fd){ CloseHandle(fd); }
#else
typedef int file_t;
#define _FILE_OFFSET_BITS 64
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
static inline file_t x_open(const char* name){
	return (file_t) open(name, O_RDWR | O_CREAT, 384);
}
#define X_OPEN_FAILED ((file_t) -1)
static inline size_t x_sizeof(file_t fd){
	struct stat st;
	return fstat(fd, &st) ? 0 : st.st_size;
}
static inline size_t x_read(file_t fd, void* buf, size_t start, size_t count){
	return pread(fd, buf, count, start);
}
static inline size_t x_write(file_t fd, const void* buf, size_t start, size_t count){
	return pwrite(fd, buf, count, start);
}
static inline void x_close(file_t fd){ close(fd); }
#endif