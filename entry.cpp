#include "file.h"
#include <stdexcept>
#include <iostream>
using namespace std;

class TeraDB{
	file_t fd;

public:
	TeraDB(const char* name) : fd(x_open(name)) {
		if(fd == X_OPEN_FAILED) throw std::runtime_error("Failed to open file");
		uint32_t header[2];
		if(x_read(fd, header, 0, sizeof(header)) < sizeof(header)) err: throw std::runtime_error("Not a TeraDB file");
		if(header[0] != '\xFFTDB') goto err;
		uint32_t root_size = ntohl(header[1]);
	}
	~TeraDB(){
		x_close(fd);
	}
};