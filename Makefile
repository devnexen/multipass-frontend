PLATFORM=$(shell uname -s)

LLVMCFG=llvm-config
CXXFLAGS=`$(LLVMCFG) --cxxflags`
LDFLAGS=`$(LLVMCFG) --ldflags --libs`
CC=`$(LLVMCFG) --bindir`/clang
CXX=`$(LLVMCFG) --bindir`/clang++
AR=ar
OLEVEL=0
CMODEL=2
OFLAGS=-g -O$(OLEVEL)
OLIBS=
MAPLDFLAGS=-pthread
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
ifneq (,$(findstring $(PLATFORM), FreeBSD))
MAPLDFLAGS+=-ldl
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
MAPLDFLAGS+=-ldl
endif
endif

.PHONY: clean

testsLib: exec
	$(CXX) $(OFLAGS) -Wall -fPIE -I Src -o bins/testsLib Tests/testsLib.cpp $(ILIBS)

exec: operands.o
	$(CXX) $(OFLAGS) -Wall -fPIC -I Src -o objs/libs.o -c Src/libs.cpp
	$(CXX) $(OFLAGS) -DUSE_MMAP=1 -Wall -fPIC -I Src -o objs/libsmmap.o -c Src/libs.cpp
	$(CXX) $(OFLAGS) -shared -Wl,-soname,liblibs.so -o objs/liblibs.so objs/libs.o $(MAPLDFLAGS)
	$(CXX) $(OFLAGS) -shared -Wl,-soname,liblibsmmap.so -o objs/liblibsmmap.so objs/libsmmap.o
	$(AR) rcs objs/liblibs.a objs/libs.o
	$(AR) rcs objs/liblibsmmap.a objs/libsmmap.o
	$(CC) $(OFLAGS) -o bins/operands objs/operands.o -pthread $(OLIBS) $(ILIBS)
	$(CC) $(OFLAGS) -Wall -fPIC -I Src -shared -Wl,-soname,libwrapper.so -o objs/libwrapper.so Src/wrapper.cpp -pthread $(OLIBS) $(ILIBS)
	$(CC) $(OFLAGS) -Wall -fPIC -I Src -shared -Wl,-soname,libwrappermmap.so -o objs/libwrappermmap.so Src/wrapper.cpp -pthread $(OLIBS) $(ILIBS)mmap
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
