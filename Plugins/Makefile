LLVMCFG=llvm-config
CXXFLAGS=`$(LLVMCFG) --cxxflags`
LDFLAGS=`$(LLVMCFG) --ldflags --libs`
CC=`$(LLVMCFG) --bindir`/clang
OLEVEL=0
OFLAGS=-g -O$(OLEVEL)

.PHONY: clean

libcustom-lib-pass.so: custom-lib-pass.cc
	$(CXX) $(CXXFLAGS) $(OFLAGS) -std=c++14 -Wall -fPIC -I ../Src -shared -Wl,-soname,$@ -o $@ $< $(PLDFLAGS)
	$(CXX) $(OFLAGS) -Wall -fPIC -I Src -o objslibs.o -c ../Src/libs.cpp

clean:
	rm -f lib*.so
	rm -f objslibs.o
