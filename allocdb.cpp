#include "x.h"
#include <stdexcept>
#include <iostream>
#include <vector>
#include <utility>
#include <bit>
#include <mutex>
#include <memory>
using memory_order = std::memory_order;

// throw/std::runtime_error was a mistake
// perror/abort is used for fatal errors
// new/delete is used for higher-level types
// malloc/realloc/free is used for dirty buffers

#ifndef ntohll
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#if defined(_MSC_VER)   // MSVC
#define ntohll(x) _byteswap_uint64(x)
#define htonll(x) _byteswap_uint64(x)
#else
#include <arpa/inet.h>
#define ntohll(x) __builtin_bswap64(x)
#define htonll(x) __builtin_bswap64(x)
#endif
#include <string>
#else
#define ntohll(x) (x)
#define htonll(x) (x)
#endif
#endif

class AllocDB{
	struct Heap: std::mutex{
		file_t fd = X_FILE_T_INVALID;
		uint64_t end = 0;
		std::vector<uint64_t> free;
	};
	// 1024 -> ...
	// 1280 -> ...
	// 1536 -> ...
	// 1792 -> ...
	// 2048 -> ...
	// 2560 -> ...
	// 3072 -> ...
	// ...
	std::atomic<Heap*> fds[20] = {0};
	std::string prefix;
	std::mutex master_lock;
public:
	AllocDB(std::string folder) : prefix(std::move(folder)){
		auto info = x_stat(prefix.c_str());
		if(info.type == X_FILE_NOT_FOUND){
			x_mkdir(prefix.c_str());
		}
		file_t f = x_open((prefix+"/frees").c_str());
		size_t sz = x_getsize(f);
		uint64_t* frees = (uint64_t*) malloc(sz);
		size_t from = x_read(f, frees, 0, sz);
		if(from < sz) memset(frees+from, 0, sz-from);
		int last = -1;
		sz >>= 3;
		std::vector<uint64_t>* vec;
		for(size_t i = 0; i < sz; i++){
			uint64_t v = frees[i];
			int bucket = ntohll(v)&0xFF;
			if(bucket >= 160) continue;
			if(last != bucket){
				last = bucket;
				Heap* harr = fds[bucket>>3].load(memory_order::relaxed);
				if(!harr)
					fds[bucket>>3].store(harr = new Heap[20](), memory_order::release);
				vec = &harr[bucket&7].free;
			}
			vec->push_back(v);
		}
		x_close(f);
	}
	private: void flush(bool close){
		std::string tmp = prefix+"/frees.tmp";
		std::lock_guard _(master_lock);
		file_t f = x_open(tmp.c_str());
		size_t off = 0;
		if(!close) for(int i = 0; i < 20; i++){
			Heap* harr = fds[i];
			if(!harr) continue;
			for(int j = 0; j < 8; j++)
				harr[j].lock();
		}
		for(int i = 0; i < 20; i++){
			Heap* harr = fds[i];
			if(!harr) continue;
			for(int j = 0; j < 8; j++){
				Heap& heap = harr[j];
				if(heap.fd == X_FILE_T_INVALID){
					if(!close) heap.unlock();
					continue;
				}
				size_t sz = heap.free.size()*8;
				x_write(f, heap.free.data(), off, sz);
				off += sz;
				if(close) x_close(heap.fd);
				else x_flush(heap.fd), heap.unlock();
			}
			if(close) delete[] harr;
		}
		x_close(f);
		x_move(tmp.c_str(), (prefix+"/frees").c_str());
	}
	public: void flush(){ flush(false); }
	~AllocDB(){ flush(true); }
	static uint64_t size_of(uint64_t ptr){
		int bucket = ptr & 0xFF;
		return bucket >= 160 ? 0 : uint64_t(1024 | (bucket&3)<<8) << (bucket>>2);
	}
	uint64_t alloc(uint64_t& size){
		int bucket = 0;
		if(size > 1024){
			int a = std::max(53 - std::countl_zero(size-1), 0);
			bucket = (a<<2|((size-1)>>(a+8)&3))+1;
		}
		if(bucket >= 160) return -1;
		size = uint64_t(1024 | (bucket&3)<<8) << (bucket>>2);
		auto& atm = fds[bucket>>3];
		Heap* harr = atm.load(memory_order::acquire);
		if(!harr){
			std::lock_guard _(master_lock);
			if(!(harr = atm.load(memory_order::acquire)))
				atm.store(harr = new Heap[20](), memory_order::release);
		}
		Heap& heap = harr[bucket&7];
		std::lock_guard _(heap);
		if(heap.free.size()){
			uint64_t a = ntohll(heap.free.back());
			heap.free.pop_back();
			return a | bucket;
		}
		if(heap.fd == X_FILE_T_INVALID){
			std::string name = prefix + "/" + std::to_string(bucket);
			if((heap.fd = x_open(name.c_str())) == X_FILE_T_INVALID)
				return -1;
			heap.end = x_getsize(heap.fd);
		}
		uint64_t a = heap.end;
		x_setsize(heap.fd, heap.end += size);
		return a | bucket;
	}
	void free(uint64_t ptr){
		int bucket = ptr & 0xFF;
		if(bucket >= 160) return;
		ptr &= ~uint64_t(0xFF);
		Heap* harr = fds[bucket>>3].load(memory_order::acquire);
		if(!harr) return;
		Heap& heap = harr[bucket&7];
		std::lock_guard _(heap);
		heap.free.push_back(htonll(ptr));
	}
	bool read(uint64_t ptr, void* buf){
		int bucket = ptr & 0xFF;
		if(bucket >= 160) return false;
		ptr &= ~uint64_t(0xFF);
		uint64_t size = uint64_t(1024 | (bucket&3)<<8) << (bucket>>2);
		Heap* harr = fds[bucket>>3].load(memory_order::acquire);
		if(!harr) return false;
		Heap& heap = harr[bucket&7];
		std::lock_guard _(heap);
		if(heap.fd == X_FILE_T_INVALID){
			std::string name = prefix + "/" + std::to_string(bucket);
			if((heap.fd = x_open(name.c_str())) == X_FILE_T_INVALID)
				return false;
			heap.end = x_getsize(heap.fd);
		}
		return x_read(heap.fd, buf, ptr, size) >= size;
	}
	bool write(uint64_t ptr, const void* buf){
		int bucket = ptr & 0xFF;
		if(bucket >= 160) return false;
		ptr &= ~uint64_t(0xFF);
		uint64_t size = uint64_t(1024 | (bucket&3)<<8) << (bucket>>2);
		Heap* harr = fds[bucket>>3].load(memory_order::acquire);
		if(!harr) return false;
		Heap& heap = harr[bucket&7];
		std::lock_guard _(heap);
		if(heap.fd == X_FILE_T_INVALID){
			std::string name = prefix + "/" + std::to_string(bucket);
			if((heap.fd = x_open(name.c_str())) == X_FILE_T_INVALID)
				return false;
			heap.end = x_getsize(heap.fd);
		}
		return x_write(heap.fd, buf, ptr, size) >= size;
	}
};