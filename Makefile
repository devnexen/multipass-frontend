PLATFORM=$(shell uname -s)

CXXFLAGS=`$(LLVMCFG) --cxxflags`
LDFLAGS=`$(LLVMCFG) --ldflags --libs`
OLEVEL=0
CMODEL=2
OFLAGS=-g -O$(OLEVEL)
OLIBS=
MPASSFLAGS=-opt-level=$(OLEVEL) -code-level=$(CMODEL)
ILIBS = -L objs -Wl,-rpath,objs -llibs

ifneq (,$(findstring $(PLATFORM), Darwin))
BREWBASE=/usr/local/Cellar/llvm
VERSION=6.0.0
CC=${BREWBASE}/${VERSION}/bin/clang
CXX=${BREWBASE}/${VERSION}/bin/clang++
LLVMCFG=${BREWBASE}/${VERSION}/bin/llvm-config
LIBS=-lncurses
else
CC=clang
CXX=clang++
LLVMCFG=llvm-config
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

testsLib: exec
	$(CXX) $(OFLAGS) -Wall -fPIE -I Src -o bins/testsLib Tests/testsLib.cpp $(ILIBS)

exec: operands.o
	$(CXX) $(OFLAGS) -Wall  -fPIC -I Src -shared -Wl,-soname,liblibs.so -o objs/liblibs.so Src/libs.cpp
	$(CC) $(OFLAGS) -o bins/operands objs/operands.o -pthread $(OLIBS) $(ILIBS)
operands.o: mpass
	bins/mpass $(MPASSFLAGS)
mpass:  dirs
	$(CXX) $(CXXFLAGS) -lz -pthread $(OFLAGS) -o bins/mpass Src/frontend.cpp $(LDFLAGS) $(LIBS)

dirs:
	mkdir -p bins
	mkdir -p objs
clean:
	rm -rf bins
	rm -rf objs
