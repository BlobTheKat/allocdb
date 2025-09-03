#include "allocdb.cpp"
#include <vector>

// Rudimentary, and frankly quite garbage fuzz tester for AllocDB
// Just to assert basic functionality works as intended

AllocDB db("example");
std::vector<uint64_t> ptrs;
void test_write(uint64_t sz){
	sz *= 4;
	uint64_t ptr = db.alloc(sz);
	int* a = (int*) malloc(sz);
	for(size_t i = sz>>2; i > 0;){ i--; a[i] = i; }
	if(!db.write(ptr, a)){
		puts("write(): failure");
		abort();
	}
	ptrs.push_back(ptr);
}
void test_read(size_t idx){
	uint64_t ptr = ptrs[idx];
	ptrs.erase(ptrs.begin()+idx);
	size_t sz = db.size_of(ptr);
	int* a = (int*) malloc(sz);
	if(!db.read(ptr, a)){
		puts("read(): failure");
		abort();
	}
	db.free(ptrs[idx]);
	for(size_t i = sz>>2; i > 0;){
		i--;
		if(a[i] != i){
			puts("data corrupted");
			abort();
		}
	}
}

// llvm fuzz test
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	while(size >= 1){
		uint8_t cmd = *data;
		data++; size--;
		int sz = (cmd&7) << ((cmd>>3&15)+6);
		if(cmd&128){ test_write(sz); continue; }
		int idx = ptrs.size()-cmd-1;
		if(idx < 0) continue;
		test_read(idx);
	}
	std::cout << ptrs.size() << " blocks allocated currently\n";
	db.flush();
	return 0;
}