#include "allocdb.cpp"
extern "C"{
#include "ffi.h"

AllocDB* allocdb_create(const char* folder){ return new AllocDB(folder); }
void allocdb_destroy(AllocDB* db){ delete db; }
uint64_t allocdb_size_of(uint64_t ptr){ return AllocDB::size_of(ptr); }
void allocdb_flush(AllocDB* db){ db->flush(); }

uint64_t allocdb_alloc(AllocDB* db, uint64_t* size){ return db->alloc(*size); }
void allocdb_free(AllocDB* db, uint64_t ptr){ db->free(ptr); }
bool allocdb_write(AllocDB* db, uint64_t ptr, const void* buf){ return db->write(ptr, buf); }
bool allocdb_read(AllocDB* db, uint64_t ptr, void* buf){ return db->read(ptr, buf); }

}