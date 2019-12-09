PLATFORM=$(shell uname -s)

LLVMCFG=llvm-config
CXXFLAGS=`$(LLVMCFG) --cxxflags`
LDFLAGS=`$(LLVMCFG) --ldflags --libs`
CC=`$(LLVMCFG) --bindir`/clang
CXX=`$(LLVMCFG) --bindir`/clang++
OLEVEL=0
CMODEL=2
OFLAGS=-g -O$(OLEVEL)
OLIBS=
MAPFLAGS=
MPASSFLAGS=-opt-level=$(OLEVEL) -code-level=$(CMODEL)
ILIBS = -L objs -Wl,-rpath,objs -llibs
PLDFLAGS=$(LDFLAGS) -Wl,-znodelete

ifneq (,$(findstring $(PLATFORM), Darwin))
VERSION=6.0.0
LIBS=-lncurses
else
ifeq (,$(findstring $(PLATFORM), NetBSD))
ifeq (,$(findstring $(PLATFORM), DragonFly))
LIBS=-lncurses
endif
endif
ifneq (,$(findstring $(PLATFORM), OpenBSD))
OLIBS=-Wl,-z,notext
endif
ifneq (,$(findstring $(PLATFORM), NetBSD))
OLIBS=-Wl,-z,notext
endif
ifneq (,$(findstring $(PLATFORM), Linux))
OLIBS= -lbsd
LIBS= -lbsd
endif
endif

.PHONY: clean libcustom-lib-pass.so

testsLib: exec
	$(CXX) $(OFLAGS) -Wall -fPIE -I Src -o bins/testsLib Tests/testsLib.cpp $(ILIBS)

libcustom-lib-pass.so: Plugins/custom-lib-pass.cc
	$(CXX) $(CXXFLAGS) $(OFLAGS) -std=c++14 -Wall -fPIC -I Src -shared -Wl,-soname,libcustom-lib-pass.so -o Plugins/$@ $< $(PLDFLAGS)
	$(CXX) $(OFLAGS) -Wall -fPIC -I Src -o Plugins/libs.o -c Src/libs.cpp

exec: operands.o
	$(CXX) $(OFLAGS) $(MAPFLAGS) -Wall  -fPIC -I Src -shared -Wl,-soname,liblibs.so -o objs/liblibs.so Src/libs.cpp
	$(CC) $(OFLAGS) -o bins/operands objs/operands.o -pthread $(OLIBS) $(ILIBS)
operands.o: mpass
	bins/mpass $(MPASSFLAGS)
mpass:  dirs
	$(CXX) $(CXXFLAGS) -std=c++14 -lz -pthread $(OFLAGS) -o bins/mpass Src/frontend.cpp $(LDFLAGS) $(LIBS)

dirs:
	mkdir -p bins
	mkdir -p objs
clean:
	rm -rf bins
	rm -rf objs
