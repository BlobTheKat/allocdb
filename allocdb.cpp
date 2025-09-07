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

	// Up to 256, allows allocations up to 2^(MAX_BUCKETS/4) * SMALLEST_BUCKET
	static constexpr int MAX_BUCKETS = 160;

	// BUCKET0_OFFSET==8 => SMALLEST_BUCKET==1024
	// We encode the bucket # on the lowest 8 bits of block pointers so don't set this to lower than 8 unless you change how the bucket # is encoded
	static constexpr int BUCKET0_OFFSET = 8;
	// minimum allocation size, i.e size of blocks in bucket 0
	static constexpr int SMALLEST_BUCKET = 4 << BUCKET0_OFFSET;

	struct Bucket: std::mutex{
		file_t fd = X_FILE_T_INVALID;
		uint64_t end = 0;
		std::vector<uint64_t> free;
		bool check_init(std::string& prefix, int bucket){
			if(fd == X_FILE_T_INVALID){
				std::string name = prefix + "/" + std::to_string(bucket);
				if((fd = x_open(name.c_str())) == X_FILE_T_INVALID)
					return false;
				end = x_getsize(fd);
			}
			return true;
		}
	};
	// 1024 -> ...
	// 1280 -> ...
	// 1536 -> ...
	// 1792 -> ...
	// 2048 -> ...
	// 2560 -> ...
	// 3072 -> ...
	// ...

	static constexpr int TOP_ARRAY_LEN = (MAX_BUCKETS+7) / 8;
	std::atomic<Bucket*> fds[TOP_ARRAY_LEN] = {0};
	std::string prefix;
	std::mutex master_lock;
	std::atomic<uint64_t> a_root = 0;
public:
	uint64_t root(){ return a_root.load(memory_order::relaxed); }
	void root(uint64_t r){ a_root.store(r, memory_order::relaxed); }

	// frees file: network-endian u64 array: [root] [free_blocks...]

	AllocDB(std::string folder) : prefix(std::move(folder)){
		auto info = x_stat(prefix.c_str());
		if(info.type == X_FILE_NOT_FOUND){
			x_mkdir(prefix.c_str());
		}
		file_t f = x_open((prefix+"/frees").c_str());
		size_t f_sz = x_getsize(f);
		uint64_t* frees = (uint64_t*) malloc(f_sz);
		size_t filled = x_read(f, frees, 0, f_sz);
		if(filled < f_sz) memset(frees+filled, 0, f_sz-filled);

		a_root.store(f_sz ? ntohll(frees[0]) : -1, memory_order::relaxed);

		int last = -1;
		f_sz >>= 3;
		std::vector<uint64_t>* vec;
		for(size_t i = 1; i < f_sz; i++){
			uint64_t v = frees[i];
			int bucket = ntohll(v)&0xFF;
			if(bucket >= MAX_BUCKETS) continue;
			if(last != bucket){
				last = bucket;
				Bucket* b_arr = fds[bucket>>3].load(memory_order::relaxed);
				if(!b_arr)
					fds[bucket>>3].store(b_arr = new Bucket[TOP_ARRAY_LEN](), memory_order::release);
				vec = &b_arr[bucket&7].free;
			}
			vec->push_back(v);
		}
		x_close(f);
	}
	private: void flush(bool close){
		std::string tmp = prefix+"/frees.tmp";
		std::lock_guard _(master_lock);
		file_t f = x_open(tmp.c_str());
		uint64_t root = htonll(a_root.load(memory_order::relaxed));
		x_write(f, &root, 0, 8);
		size_t f_off = 8;
		// close -> destroy everything, don't bother locking
		// !close -> lock everything first to ensure a single consistent state to save
		//     it's ok to unlock Bucket&s as soon as we are done with them since we don't read them after that
		if(!close) for(int i = 0; i < TOP_ARRAY_LEN; i++){
			Bucket* b_arr = fds[i];
			if(!b_arr) continue;
			for(int j = 0; j < 8; j++)
				b_arr[j].lock();
		}
		for(int i = 0; i < TOP_ARRAY_LEN; i++){
			Bucket* b_arr = fds[i];
			if(!b_arr) continue;
			for(int j = 0; j < 8; j++){
				Bucket& bk = b_arr[j];
				if(bk.fd == X_FILE_T_INVALID){
					if(!close) bk.unlock();
					continue;
				}
				size_t sz = bk.free.size()*8;
				x_write(f, bk.free.data(), f_off, sz);
				f_off += sz;
				if(close) x_close(bk.fd);
				else{
					x_flush(bk.fd);
					bk.unlock();
				}
			}
			if(close) delete[] b_arr;
		}
		x_close(f);
		x_move(tmp.c_str(), (prefix+"/frees").c_str());
		// master_lock unlock()ed
	}
	public: void flush(){ flush(false); }
	~AllocDB(){ flush(true); }
	static uint64_t size_of(uint64_t ptr){
		int bucket = ptr & 0xFF;
		return bucket >= MAX_BUCKETS ? 0 : uint64_t(SMALLEST_BUCKET | (bucket&3)<<BUCKET0_OFFSET) << bucket*4;
	}
	// 
	uint64_t alloc(uint64_t& size){
		int bucket = 0;
		if(size > SMALLEST_BUCKET){
			int a = std::max(53 - std::countl_zero(size-1), 0);
			bucket = (a<<2|((size-1)>>(a+BUCKET0_OFFSET)&3))+1;
		}
		if(bucket >= MAX_BUCKETS) return -1;
		size = uint64_t(SMALLEST_BUCKET | (bucket&3)<<BUCKET0_OFFSET) << bucket*4;

		auto& atm = fds[bucket>>3];
		Bucket* b_arr = atm.load(memory_order::acquire);
		if(!b_arr){
			std::lock_guard _(master_lock);
			if(!(b_arr = atm.load(memory_order::acquire)))
				atm.store(b_arr = new Bucket[TOP_ARRAY_LEN](), memory_order::release);
		}
		Bucket& bk = b_arr[bucket&7];
		std::lock_guard _(bk);
		if(bk.free.size()){
			uint64_t a = ntohll(bk.free.back());
			bk.free.pop_back();
			return a | bucket;
		}
		if(!bk.check_init(prefix, bucket)) return -1;
		uint64_t a = bk.end;
		x_setsize(bk.fd, bk.end += size);
		return a | bucket;
	}
	void free(uint64_t ptr){
		int bucket = ptr & 0xFF;
		if(bucket >= MAX_BUCKETS) return;
		ptr &= ~uint64_t(0xFF);
		Bucket* b_arr = fds[bucket>>3].load(memory_order::acquire);
		if(!b_arr) return;
		Bucket& bk = b_arr[bucket&7];
		std::lock_guard _(bk);
		bk.free.push_back(htonll(ptr));
	}
	bool read(uint64_t ptr, void* buf){
		int bucket = ptr & 0xFF;
		if(bucket >= MAX_BUCKETS) return false;
		ptr &= ~uint64_t(0xFF);
		uint64_t size = uint64_t(SMALLEST_BUCKET | (bucket&3)<<BUCKET0_OFFSET) << (bucket>>2);

		Bucket* b_arr = fds[bucket>>3].load(memory_order::acquire);
		if(!b_arr) return false;
		Bucket& bk = b_arr[bucket&7];
		std::lock_guard _(bk);
		if(!bk.check_init(prefix, bucket)) return false;
		return x_read(bk.fd, buf, ptr, size) >= size;
	}
	bool write(uint64_t ptr, const void* buf){
		int bucket = ptr & 0xFF;
		if(bucket >= MAX_BUCKETS) return false;
		ptr &= ~uint64_t(0xFF);
		uint64_t size = uint64_t(SMALLEST_BUCKET | (bucket&3)<<BUCKET0_OFFSET) << (bucket>>2);

		Bucket* b_arr = fds[bucket>>3].load(memory_order::acquire);
		if(!b_arr) return false;
		Bucket& bk = b_arr[bucket&7];
		std::lock_guard _(bk);
		if(!bk.check_init(prefix, bucket)) return false;
		return x_write(bk.fd, buf, ptr, size) >= size;
	}
};