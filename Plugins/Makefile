PLATFORM=$(shell uname -s)

LLVMCFG=llvm-config
CXXFLAGS=`$(LLVMCFG) --cxxflags`
LDFLAGS=`$(LLVMCFG) --ldflags --libs` -Wl,-znodelete
CC=`$(LLVMCFG) --bindir`/clang
CXX=`$(LLVMCFG) --bindir`/clang++
OLEVEL=0
OFLAGS=-g -O$(OLEVEL)

.PHONY: clean

ifneq (,$(findstring $(PLATFORM), Darwin))
	$(warning Darwin is unsupported at the moment)
	$(shell exit 1)


clean:
	$(shell exit 1)

all:

else

all: libcustom-lib-pass.so
	$(CC) $(OFLAGS) -Wall -fPIC -o ../bins/testsPlugin ../Tests/testsPlugin.c -Xclang -load -Xclang ./$< objslibsmmap.o
	$(CC) $(OFLAGS) -Wall -fPIC -o ../bins/testsPlugin-wo ../Tests/testsPlugin.c

libcustom-lib-pass.so: custom-lib-pass.cc
	$(CXX) $(CXXFLAGS) $(OFLAGS) -std=c++14 -Wall -fPIC -I ../Src -shared -Wl,-soname,$@ -o $@ $< $(LDFLAGS)
	$(CXX) $(OFLAGS) -Wall -fPIC -I Src -o objslibs.o -c ../Src/libs.cpp
	$(CXX) $(OFLAGS) -DUSE_MMAP=1 -Wall -fPIC -I Src -o objslibsmmap.o -c ../Src/libs.cpp

clean:

	rm -f lib*.so
	rm -f lib*.a
	rm -f objslibs*.o
endif
