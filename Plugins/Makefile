PLATFORM=$(shell uname -s)

LLVMCFG=llvm-config
CXXFLAGS=`$(LLVMCFG) --cxxflags`
LDFLAGS=`$(LLVMCFG) --ldflags --libs` -Wl,-znodelete
CC=`$(LLVMCFG) --bindir`/clang
CXX=`$(LLVMCFG) --bindir`/clang++
OLEVEL=0
OFLAGS=-g -O$(OLEVEL)

.PHONY: clean

clean:

	rm -f lib*.so
	rm -f lib*.a
	rm -f objslibs*.o

ifneq (,$(findstring $(PLATFORM), Darwin))
	$(warning Darwin is unsupported at the moment)
	$(shell exit 1)
else

libcustom-lib-pass.so: custom-lib-pass.cc
	$(CXX) $(CXXFLAGS) $(OFLAGS) -std=c++14 -Wall -fPIC -I ../Src -shared -Wl,-soname,$@ -o $@ $< $(LDFLAGS)
	$(CXX) $(OFLAGS) -Wall -fPIC -I Src -o objslibs.o -c ../Src/libs.cpp
	$(CXX) $(OFLAGS) -DUSE_MMAP=1 -Wall -fPIC -I Src -o objslibsmmap.o -c ../Src/libs.cpp
endif
