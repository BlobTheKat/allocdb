#!/usr/bin/env bash

# Fuzz testing
# clang++ -O3 -std=c++20 test.cpp -fsanitize=fuzzer,undefined -o .test && ./.test

OPTIONAL_FLAGS="-flto -fno-exceptions"

# build C lib
clang++ -c -fPIC -O3 $OPTIONAL_FLAGS ffi.cpp -o .o
	&& ar rcs liballocdb-c.a .o && rm .o

# build C++ lib
clang++ -c -fPIC -O3 $OPTIONAL_FLAGS allocdb.cpp -o .o
	&& ar rcs liballocdb.a .o && rm .o


# MSVC
# clang-cl /O2 /c /EHs- /EHc- ffi.cpp /Fo.o && lib /OUT:liballocdb.lib .o && del .o