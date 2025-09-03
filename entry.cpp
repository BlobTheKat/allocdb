#include "x.h"
#include <stdexcept>
#include <iostream>
#include <vector>
#include <utility>
#include <bit>
#include <mutex>
#include <memory>
using memory_order = std::memory_order;

// throw/std::runtime_error is used for catchable errors
// perror/abort is used for fatal errors
// new/delete is used for higher-level types
// malloc/realloc/free is used for dirty buffers

class MallocDB{
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
public:
	MallocDB(std::string folder) : prefix(std::move(folder)){
		auto info = x_stat(prefix.c_str());
		if(info.type == X_FILE_NOT_FOUND)
			x_mkdir(prefix.c_str());
	}
	~MallocDB(){
		for(int i = 0; i < 20; i++){
			Heap* f = fds[i];
			if(!f) break;
			for(int j = 0; j < 8; j++){
				Heap& f2 = f[j];
				if(f2.fd == X_FILE_T_INVALID) break;
				x_close(f2.fd);
			}
			delete[] f;
		}
	}
	uint64_t alloc(uint64_t& size){
		int bucket = 0;
		if(size > 1024){
			int a = std::max(53 - std::countl_zero(size-1), 0);
			bucket = (a<<2|((size-1)>>(a+8)&3))+1;
		}
		if(bucket >= 160) return -1;
		size = uint64_t(1024 | (bucket&3)<<8) << (bucket>>2);
		Heap* harr = fds[bucket>>3].load(memory_order::acquire);
		if(!harr){
			Heap* harr2 = new Heap[20]();
			if(fds[bucket>>3].compare_exchange_strong(harr, harr2)) harr = harr2;
			else delete[] harr2;
		}
		Heap& heap = harr[bucket&7];
		heap.lock();
		if(heap.fd == X_FILE_T_INVALID){
			std::string name = prefix + "/" + std::to_string(bucket) + ".bin";
			if((heap.fd = x_open(name.c_str())) == X_FILE_T_INVALID){
				heap.unlock();
				return -1;
			}
			heap.end = x_getsize(heap.fd);
		}
		uint64_t ret;
		if(heap.free.size()){
			ret = heap.free.back();
			heap.free.pop_back();
		}else{
			ret = heap.end;
			heap.end += size;
		}
		heap.unlock();
		return ret | bucket;
	}
	void free(uint64_t ptr){
		int bucket = ptr & 0xFF;
		ptr &= ~uint64_t(0xFF);
		Heap* harr = fds[bucket>>3].load(memory_order::acquire);
		if(!harr) return;
		Heap& heap = harr[bucket&7];
		std::lock_guard _(heap);
		if(heap.fd == X_FILE_T_INVALID) return;
		heap.free.push_back(ptr);
	}
	bool write(uint64_t ptr, const void* buf){
		int bucket = ptr & 0xFF;
		ptr &= ~uint64_t(0xFF);
		uint64_t size = uint64_t(1024 | (bucket&3)<<8) << (bucket>>2);
		Heap* harr = fds[bucket>>3].load(memory_order::acquire);
		if(!harr) return false;
		Heap& heap = harr[bucket&7];
		std::lock_guard _(heap);
		if(heap.fd == X_FILE_T_INVALID) return false;
		return x_write(heap.fd, buf, ptr, size) >= size;
	}
	uint64_t size_of(uint64_t ptr){
		int bucket = ptr & 0xFF;
		return uint64_t(1024 | (bucket&3)<<8) << (bucket>>2);
	}
	bool read(uint64_t ptr, void* buf){
		int bucket = ptr & 0xFF;
		ptr &= ~uint64_t(0xFF);
		uint64_t size = uint64_t(1024 | (bucket&3)<<8) << (bucket>>2);
		Heap* harr = fds[bucket>>3].load(memory_order::acquire);
		if(!harr) return false;
		Heap& heap = harr[bucket&7];
		std::lock_guard _(heap);
		if(heap.fd == X_FILE_T_INVALID) return false;
		return x_read(heap.fd, buf, ptr, size) >= size;
	}
};