# README #

make (LLVMCFG=<llvm-config version>)
./mpass

Additional options :
-display-module
-no-cpu-features
-iterations

# LLVM Plugin

make (LLVMCFG=<llvm-config version>) -C Plugins
clang(-<llvm-config version related>) ... -Xclang -load -Xclang Plugins/libcustom-lib-pass.so Plugins/objslibs.o
