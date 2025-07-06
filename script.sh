# test
clang++ -O1 -std=c++20 test.cpp -o .test && ./.test

# build C lib
# clang++ -c -fPIC -O3 ffi.cpp -o .o && ar rcs libtera.a .o && rm .o