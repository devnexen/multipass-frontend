#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#if LLVM_VERSION_MAJOR <= 5
#include "llvm/CodeGen/CommandFlags.h"
#elif LLVM_VERSION_MAJOR >= 7
#include "llvm/CodeGen/CommandFlags.inc"
#else
#include "llvm/CodeGen/CommandFlags.def"
#endif
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <bsd/stdlib.h>
#include <bsd/string.h>
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

using namespace llvm;
using namespace std;

static string targetTriple = sys::getDefaultTargetTriple();
static string targetCpu = "generic";
static string targetFeatures = "";
static TargetOptions targetOptions;
static bool hasMallocUsableSize = false;
static bool hasClockGettime = false;
static bool hasArc4random = false;
static bool hasGetentropy = false;
static bool hasGetrandom = false;
static bool hasPledge = false;
static bool hasUnveil = false;
static bool hasCapsicum = false;
static bool hasPrctl = false;
static bool hasExplicitBzero = false;
static bool hasExplicitMemset = false;
static bool hasMremapLinux = false;
static bool hasMremapBSD = false;
static bool hasGetauxval = false;
static bool hasElfauxinfo = false;
static bool hasPthreadNameLinux = false;
static bool hasPthreadNameBSD = false;
static bool hasPthreadGetNameBSD = false;
static bool hasMapConceal = false;
static bool hasMapSuperpg = false;
static bool hasTimingsafeCmp = false;
static bool hasConsttimeMemequal = false;
static bool reportSep = false;
static vector<Function *> TestFunctions;
static Module *Mod;
static StructType *TimespecType;
static StructType *TimevalType;
static StructType *PthreadType;
static StructType *PthreadAttrType;
static StructType *PProcMapType;
static LoadInst *StartFMbr;
static LoadInst *StartSMbr;
static LoadInst *EndFMbr;
static LoadInst *EndSMbr;
static Value *Lim;
static Value *ABufferSize;
static Value *StartFAccess;
static Value *StartSAccess;
static Value *EndFAccess;
static Value *EndSAccess;
static Value *SecSettings;
static GlobalVariable *TotalUsableSize;
static GlobalVariable *TotalAllocated;
static GlobalVariable *RealSize;
static GlobalVariable *PtrSize;
static GlobalVariable *PtrDblSize;
static GlobalVariable *LastRandomValue;
static GlobalVariable *LastBuffer;
static GlobalVariable *GZero;
static GlobalVariable *SyscallGetrandomId;
static GlobalVariable *SyscallGetrandomMod;
static GlobalVariable *SecRetCall;
static GlobalVariable *ClockMonotonic;
static GlobalVariable *PrctlSetSeccomp;
static GlobalVariable *ProtReadWrite;
static GlobalVariable *MapSharedAnon;
static GlobalVariable *MapPrivate;
static GlobalVariable *MapFixed;
static GlobalVariable *MapConceal;
static GlobalVariable *MapSuperPg;
static GlobalVariable *MadvNoDump;
static GlobalVariable *PageSize;
static GlobalVariable *PthreadRetCall;
static GlobalVariable *PthreadSetnameRetCall;
static GlobalVariable *SzStrlcpy;
static GlobalVariable *SzStrlcat;
static GlobalVariable *MemcmpRet;
static GlobalVariable *BcmpRet;
static GlobalVariable *AtHwcap;
static GlobalVariable *AtHwcap2;
static GlobalVariable *MAtHwcap;
static GlobalVariable *MAtHwcap2;
static GlobalVariable *AuxVec;
static GlobalVariable *AuxVec2;
static GlobalVariable *GThreadName;
static GlobalVariable *OrigThreadName;
static GlobalVariable *Errno;
static AllocaInst *AStart;
static AllocaInst *AEnd;
static CallInst *CStartInst;
static CallInst *CEndInst;
static Function *PrintfFnc;
static Function *StrerrorFnc;

static int32_t randomStrLen;
static std::unique_ptr<char[]> randomStr;

static cl::opt<string>
    NumIterations("iterations", cl::init("1024"),
                  cl::desc("Number of iterations for the running tests"));

static cl::opt<string> RandomBufferSize("random-buffer-size", cl::init("32"),
                                        cl::desc("Size of the random buffer"));

static cl::opt<bool> DisplayMod("display-module", cl::init(false),
                                cl::desc("Display the IR bytecode in stdout"));

static cl::opt<bool> NoCpuFeatures("no-cpu-features", cl::init(false),
                                   cl::desc("No cpu features flags"));

static cl::opt<bool> Verbose("verbose", cl::init(false), cl::desc("Verbose"));

static cl::opt<string> TargetTriple("mtriple",
                                    cl::init(sys::getDefaultTargetTriple()),
                                    cl::desc("Target triple"));

static cl::opt<string> SizeToAllocate("sizetoallocate", cl::init("64"),
                                      cl::desc("Size to allocate"));

static cl::opt<string> PledgePermissions("pledge-perms",
                                         cl::init("stdio rpath wpath"),
                                         cl::desc("pledge call permissions"));

static cl::opt<string> UnveilPermissions("unveil-perms", cl::init("rwx"),
                                         cl::desc("unveil call permissions"));

static cl::opt<string> OptLevel("opt-level", cl::init("2"),
                                cl::desc("Compilation optimisation level"));

static cl::opt<string> CodeLvl("code-level", cl::init("2"),
                               cl::desc("Code size level"));

static cl::opt<bool> ForkMod("fork-mod", cl::init(false),
                             cl::desc("Launch in fork mode"));

void printReport(IRBuilder<> Builder, Function *Fnc) {
  char format[128];

  static size_t progressIndex = 0, totalTests = 7;
  progressIndex++;

  ::snprintf(format, sizeof(format), "[%s %zu / %zu] in progress",
             Fnc->getName().data(), progressIndex, totalTests);
  Value *ReportFormat =
      Builder.CreateGlobalStringPtr("%s%s%s\n", "Printreportfmt");
  Value *Format = Builder.CreateGlobalStringPtr(format);
  Value *ReportSep = Builder.CreateGlobalStringPtr("**", "Reportsep");
  Value *EmptySep = Builder.CreateGlobalStringPtr("", "Emptysep");

  vector<Value *> PrintfCallArgs(4);
  PrintfCallArgs[0] = ReportFormat;
  PrintfCallArgs[1] = reportSep ? ReportSep : EmptySep;
  PrintfCallArgs[2] = Format;
  PrintfCallArgs[3] = reportSep ? ReportSep : EmptySep;
  Builder.CreateCall(PrintfFnc, PrintfCallArgs);
}

Function *addBasicFunction(IRBuilder<> Builder, string Name) {
  vector<string> FuncArgs(1);
  FuncArgs[0] = "Num";
  size_t Idx = 0;
  vector<Type *> Integers(FuncArgs.size(),
                          Type::getInt64Ty(Builder.getContext()));
  FunctionType *Ft = FunctionType::get(Builder.getInt64Ty(), Integers, false);
  Function *Fnc = Function::Create(Ft, Function::InternalLinkage, Name, Mod);

  for (Function::arg_iterator AI = Fnc->arg_begin(); Idx != FuncArgs.size();
       ++AI, ++Idx)
    AI->setName(FuncArgs[Idx]);

  return Fnc;
}

void addBasicBlock(IRBuilder<> Builder, string Name, Instruction::BinaryOps op,
                   Function *Fnc) {
  static Value *One = Builder.getInt64(1);
  Value *Num = cast<Value>(Fnc->arg_begin());
  Value *Res;
  BasicBlock *Bb = BasicBlock::Create(Builder.getContext(), Name, Fnc);
  Builder.SetInsertPoint(Bb);
  IRBuilder<> Bbuilder(Bb);
  Res = Bbuilder.CreateBinOp(op, Num, One);
  Bbuilder.CreateRet(Res);
}

Function *addMTFunction(IRBuilder<> Builder, string Name) {
  vector<Type *> MTArgs(1);
  MTArgs[0] = Builder.getInt8PtrTy();
  FunctionType *MTFt = FunctionType::get(Builder.getInt8PtrTy(), MTArgs, false);
  Function *MTFn = Function::Create(MTFt, Function::InternalLinkage, Name, Mod);

  return MTFn;
}

void addMTBlock(IRBuilder<> Builder, string Name) {
  Function *MTFn = Mod->getFunction(Name);
  Function *RandomnessFnc = Mod->getFunction("test_randomness");
  Function *MemFnc = Mod->getFunction("test_memory");

  reportSep = true;

  BasicBlock *Entry = BasicBlock::Create(Builder.getContext(), "entry", MTFn);

  IRBuilder<> EntryBuilder(Entry);

  EntryBuilder.CreateCall(RandomnessFnc);
  EntryBuilder.CreateCall(MemFnc);

  ReturnInst::Create(Builder.getContext(),
                     Constant::getNullValue(Builder.getInt8PtrTy()), Entry);
}

Function *addTestFunction(IRBuilder<> Builder, const char *FName) {
  FunctionType *Ft = FunctionType::get(Builder.getVoidTy(), false);
  Function *TestFnc =
      Function::Create(Ft, Function::InternalLinkage, FName, Mod);

  return TestFnc;
}

void addTestBlock(IRBuilder<> Builder, const char *FName, Function *TestFnc) {
  Function *FFnc = Mod->getFunction(FName);
  BasicBlock *Entry =
      BasicBlock::Create(Builder.getContext(), "entry", TestFnc);
  IRBuilder<> EntryBuilder(Entry);
  BasicBlock *EntryHeader = EntryBuilder.GetInsertBlock();
  Builder.SetInsertPoint(Entry);
  Value *Beg = EntryBuilder.getInt64(1);
  Value *Inc = EntryBuilder.getInt64(1);

  if (Verbose)
    printReport(Builder, TestFnc);

  BasicBlock *Loop = BasicBlock::Create(Builder.getContext(), "loop", TestFnc);
  Builder.SetInsertPoint(Loop);
  IRBuilder<> LoopBuilder(Loop);
  BasicBlock *LoopHeader = LoopBuilder.GetInsertBlock();
  PHINode *I = LoopBuilder.CreatePHI(Builder.getInt64Ty(), 8, "I");
  I->addIncoming(Beg, EntryHeader);
  Value *Nxt = LoopBuilder.CreateAdd(I, Inc, "Nxt");

  vector<Value *> Args(1);
  Args[0] = Nxt;
  LoopBuilder.CreateCall(FFnc, Args, FName);
  Value *EndLoop = LoopBuilder.CreateICmpULT(I, Lim, "EndLoop");
  EndLoop = LoopBuilder.CreateICmpNE(EndLoop, Builder.getInt1(0), "LoopCond");
  EntryBuilder.CreateBr(Loop);

  BasicBlock *End = BasicBlock::Create(Builder.getContext(), "end", TestFnc);
  LoopBuilder.CreateCondBr(EndLoop, Loop, End);
  IRBuilder<> EndBuilder(End);
  Builder.SetInsertPoint(End);
  I->addIncoming(Nxt, LoopHeader);

  TestFunctions.push_back(TestFnc);

  ReturnInst::Create(Builder.getContext(), nullptr, End);
}

Function *addMemoryTestFunction(IRBuilder<> Builder) {
  FunctionType *Ft = FunctionType::get(Builder.getVoidTy(), false);
  Function *TestFnc =
      Function::Create(Ft, Function::InternalLinkage, "func_calls_memory", Mod);

  return TestFnc;
}

void addMemoryTestBlock(IRBuilder<> Builder) {
  Function *TestFnc = Mod->getFunction("func_calls_memory");
  BasicBlock *Entry =
      BasicBlock::Create(Builder.getContext(), "entry", TestFnc);
  IRBuilder<> EntryBuilder = IRBuilder<>(Entry);

  vector<Type *> MemsetArgs(3);
  MemsetArgs[0] = Builder.getInt8PtrTy();
  MemsetArgs[1] = Builder.getInt8Ty();
  MemsetArgs[2] = Builder.getInt64Ty();
  FunctionType *MemsetFt =
      FunctionType::get(Builder.getInt8PtrTy(), MemsetArgs, false);
  Function *MemsetFnc =
      Function::Create(MemsetFt, Function::ExternalLinkage, "memset", Mod);
  MemsetFnc->setCallingConv(CallingConv::C);

  vector<Type *> StrlcpyArgs(3);
  StrlcpyArgs[0] = Builder.getInt8PtrTy();
  StrlcpyArgs[1] = Builder.getInt8PtrTy();
  StrlcpyArgs[2] = Builder.getInt64Ty();
  FunctionType *StrlcpyFt =
      FunctionType::get(Builder.getInt64Ty(), StrlcpyArgs, false);
  Function *StrlcpyFnc =
      Function::Create(StrlcpyFt, Function::ExternalLinkage, "strlcpy", Mod);
  Function *StrlcatFnc =
      Function::Create(StrlcpyFt, Function::ExternalLinkage, "strlcat", Mod);

  if (hasExplicitBzero) {
    vector<Type *> ExplicitBzeroArgs(2);
    ExplicitBzeroArgs[0] = MemsetArgs[0];
    ExplicitBzeroArgs[1] = MemsetArgs[2];
    FunctionType *ExplicitBzeroFt =
        FunctionType::get(Builder.getVoidTy(), ExplicitBzeroArgs, false);
    Function *ExplicitBzeroFnc = Function::Create(
        ExplicitBzeroFt, Function::ExternalLinkage, "explicit_bzero", Mod);
    ExplicitBzeroFnc->setCallingConv(CallingConv::C);
  } else if (hasExplicitMemset) {
    Function *ExplicitMemsetFnc = Function::Create(
        MemsetFt, Function::ExternalLinkage, "explicit_memset", Mod);
    ExplicitMemsetFnc->setCallingConv(CallingConv::C);
  }

  vector<Type *> SafeBzeroArgs(2);
  SafeBzeroArgs[0] = MemsetArgs[0];
  SafeBzeroArgs[1] = MemsetArgs[2];
  FunctionType *SafeBzeroFt =
      FunctionType::get(Builder.getVoidTy(), SafeBzeroArgs, false);
  Function *SafeBzeroFnc = Function::Create(
      SafeBzeroFt, Function::ExternalLinkage, "safe_bzero", Mod);
  SafeBzeroFnc->setCallingConv(CallingConv::C);

  vector<Type *> MemcmpArgs(3);
  MemcmpArgs[0] = Builder.getInt8PtrTy();
  MemcmpArgs[1] = Builder.getInt8PtrTy();
  MemcmpArgs[2] = Builder.getInt64Ty();
  FunctionType *MemcmpFt =
      FunctionType::get(Builder.getInt32Ty(), MemcmpArgs, false);
  Function *MemcmpFnc = nullptr;
  Function *BcmpFnc = nullptr;
  Function *SafeBcmpFnc = nullptr;
  Function *SafeProcmapsFnc = nullptr;

  if (!hasTimingsafeCmp && !hasConsttimeMemequal) {
    MemcmpFnc =
        Function::Create(MemcmpFt, Function::ExternalLinkage, "memcmp", Mod);
    BcmpFnc =
        Function::Create(MemcmpFt, Function::ExternalLinkage, "bcmp", Mod);
  } else if (hasTimingsafeCmp) {
    MemcmpFnc = Function::Create(MemcmpFt, Function::ExternalLinkage,
                                 "timingsafe_memcmp", Mod);
    BcmpFnc = Function::Create(MemcmpFt, Function::ExternalLinkage,
                               "timingsafe_bcmp", Mod);
  } else if (hasConsttimeMemequal) {
    MemcmpFnc = Function::Create(MemcmpFt, Function::ExternalLinkage,
                                 "consttime_memequal", Mod);
    BcmpFnc = MemcmpFnc;
  }

  MemcmpFnc->setCallingConv(CallingConv::C);
  BcmpFnc->setCallingConv(CallingConv::C);
  SafeBcmpFnc =
      Function::Create(MemcmpFt, Function::ExternalLinkage, "safe_bcmp", Mod);

  SafeBcmpFnc->setCallingConv(CallingConv::C);

  vector<Type *> SafeProcmapsArgs(1);
  SafeProcmapsArgs[0] = Builder.getInt32Ty();
  FunctionType *SafeProcmapsFt =
      FunctionType::get(Builder.getInt32Ty(), SafeProcmapsArgs, false);
  SafeProcmapsFnc = Function::Create(SafeProcmapsFt, Function::ExternalLinkage,
                                     "safe_proc_maps", Mod);
  SafeProcmapsFnc->setCallingConv(CallingConv::C);

  vector<Type *> MallocArgs(1);
  MallocArgs[0] = Builder.getInt64Ty();
  FunctionType *MallocFt =
      FunctionType::get(Builder.getInt8PtrTy(), MallocArgs, false);
  Function *MallocFnc =
      Function::Create(MallocFt, Function::ExternalLinkage, "malloc", Mod);
  MallocFnc->setCallingConv(CallingConv::C);

  vector<Type *> CallocArgs(2);
  CallocArgs[0] = Builder.getInt64Ty();
  CallocArgs[1] = Builder.getInt64Ty();
  FunctionType *CallocFt =
      FunctionType::get(Builder.getInt8PtrTy(), CallocArgs, false);
  Function *CallocFnc =
      Function::Create(CallocFt, Function::ExternalLinkage, "calloc", Mod);
  CallocFnc->setCallingConv(CallingConv::C);

  vector<Type *> ReallocArgs(2);
  ReallocArgs[0] = Builder.getInt8PtrTy();
  ReallocArgs[1] = Builder.getInt64Ty();
  FunctionType *ReallocFt =
      FunctionType::get(Builder.getInt8PtrTy(), ReallocArgs, false);
  Function *ReallocFnc =
      Function::Create(ReallocFt, Function::ExternalLinkage, "realloc", Mod);
  ReallocFnc->setCallingConv(CallingConv::C);

  FunctionType *GetpagesizeFt = FunctionType::get(Builder.getInt64Ty(), false);
  Function *GetpagesizeFnc = Function::Create(
      GetpagesizeFt, Function::ExternalLinkage, "getpagesize", Mod);

  vector<Type *> PosixMemalignArgs(3);
  PosixMemalignArgs[0] = PointerType::get(Builder.getInt8PtrTy(), 0);
  PosixMemalignArgs[1] = Builder.getInt64Ty();
  PosixMemalignArgs[2] = Builder.getInt64Ty();
  FunctionType *PosixMemalignFt =
      FunctionType::get(Builder.getInt32Ty(), PosixMemalignArgs, false);
  Function *PosixMemalignFnc = Function::Create(
      PosixMemalignFt, Function::ExternalLinkage, "posix_memalign", Mod);
  PosixMemalignFnc->setCallingConv(CallingConv::C);

  vector<Type *> MmapArgs(6);
  MmapArgs[0] = Builder.getInt8PtrTy();
  MmapArgs[1] = Builder.getInt64Ty();
  MmapArgs[2] = Builder.getInt32Ty();
  MmapArgs[3] = Builder.getInt32Ty();
  MmapArgs[4] = Builder.getInt32Ty();
  MmapArgs[5] = Builder.getInt64Ty();
  FunctionType *MmapFt =
      FunctionType::get(Builder.getInt8PtrTy(), MmapArgs, false);
  Function *MmapFnc =
      Function::Create(MmapFt, Function::ExternalLinkage, "mmap", Mod);
  MmapFnc->setCallingConv(CallingConv::C);

  vector<Type *> MadviseArgs(3);
  MadviseArgs[0] = Builder.getInt8PtrTy();
  MadviseArgs[1] = Builder.getInt64Ty();
  MadviseArgs[2] = Builder.getInt32Ty();
  FunctionType *MadviseFt =
      FunctionType::get(Builder.getInt32Ty(), MadviseArgs, false);
  Function *MadviseFnc =
      Function::Create(MadviseFt, Function::ExternalLinkage, "madvise", Mod);
  MadviseFnc->setCallingConv(CallingConv::C);

  Function *MremapFnc = nullptr;

  if (hasMremapLinux) {
    vector<Type *> MremapArgs(3);
    MremapArgs[0] = Builder.getInt8PtrTy();
    MremapArgs[1] = Builder.getInt64Ty();
    MremapArgs[2] = Builder.getInt64Ty();
    FunctionType *MremapFt =
        FunctionType::get(Builder.getInt8PtrTy(), MremapArgs, true);
    MremapFnc =
        Function::Create(MremapFt, Function::ExternalLinkage, "mremap", Mod);
    MremapFnc->setCallingConv(CallingConv::C);
  } else if (hasMremapBSD) {
    vector<Type *> MremapArgs(4);
    MremapArgs[0] = Builder.getInt8PtrTy();
    MremapArgs[1] = Builder.getInt64Ty();
    MremapArgs[2] = Builder.getInt8PtrTy();
    MremapArgs[3] = Builder.getInt64Ty();
    FunctionType *MremapFt =
        FunctionType::get(Builder.getInt8PtrTy(), MremapArgs, true);
    MremapFnc =
        Function::Create(MremapFt, Function::ExternalLinkage, "mremap", Mod);
  }

  vector<Type *> FreeArgs(1);
  FreeArgs[0] = Builder.getInt8PtrTy();
  FunctionType *FreeFt =
      FunctionType::get(Builder.getVoidTy(), FreeArgs, false);
  Function *FreeFnc =
      Function::Create(FreeFt, Function::ExternalLinkage, "free", Mod);
  FreeFnc->setCallingConv(CallingConv::C);

  Value *Dst = EntryBuilder.getInt8(0);
  Dst->setName("Dst");
  Value *Zero = EntryBuilder.getInt8(0);
  Zero->setName("Zero");
  Value *DstSize = ConstantExpr::getSizeOf(EntryBuilder.getInt8Ty());
  DstSize->setName("DstSize");

  Constant *CurrentAllocated = TotalAllocated->getInitializer();

  AllocaInst *ADst = EntryBuilder.CreateAlloca(
      Type::getInt8Ty(EntryBuilder.getContext()), nullptr, "Adst");
  EntryBuilder.CreateStore(Dst, ADst);

  vector<Value *> MemsetCallArgs(3);
  MemsetCallArgs[0] = ADst;
  MemsetCallArgs[1] = Zero;
  MemsetCallArgs[2] = DstSize;

  EntryBuilder.CreateCall(MemsetFnc, MemsetCallArgs);

  AllocaInst *DstBuf = EntryBuilder.CreateAlloca(
      Type::getInt8Ty(EntryBuilder.getContext()),
      EntryBuilder.getInt64((randomStrLen * 2) + 2), "Dstbuf");
  Value *SrcBuf =
      EntryBuilder.CreateGlobalStringPtr(StringRef(randomStr.get()), "Srcbuf");

  vector<Value *> StrlcpyCallArgs(3);
  StrlcpyCallArgs[0] = DstBuf;
  StrlcpyCallArgs[1] = SrcBuf;
  StrlcpyCallArgs[2] = EntryBuilder.getInt64(10);

  Value *VSzStrlcpy = EntryBuilder.CreateCall(StrlcpyFnc, StrlcpyCallArgs);
  Value *VSzStrlcat = EntryBuilder.CreateCall(StrlcatFnc, StrlcpyCallArgs);

  EntryBuilder.CreateStore(VSzStrlcpy, SzStrlcpy);
  EntryBuilder.CreateStore(VSzStrlcat, SzStrlcat);

  Value *Ptr = nullptr;
  Value *NPtr = nullptr;
  Value *PtrLen = PtrSize->getInitializer();
  Value *PtrDblLen = PtrDblSize->getInitializer();
  Value *DPtrLen = EntryBuilder.CreateMul(PtrLen, Builder.getInt64(2));

  EntryBuilder.CreateAlloca(EntryBuilder.getInt8Ty(), PtrLen, "Allocaptr");
  EntryBuilder.CreateAlloca(
      EntryBuilder.getInt8Ty(),
      EntryBuilder.CreateMul(PtrLen, EntryBuilder.getInt64(2)), "Allocbptr");

  Value *TotalPtrLen = Builder.CreateAdd(PtrLen, DPtrLen);
  Value *TotalAllocatedA = Builder.CreateAdd(CurrentAllocated, TotalPtrLen);

  vector<Value *> MallocCallArgs(1);
  MallocCallArgs[0] = PtrLen;

  Ptr = EntryBuilder.CreateCall(MallocFnc, MallocCallArgs);

  vector<Value *> ReallocCallArgs(2);
  ReallocCallArgs[0] = Ptr;
  ReallocCallArgs[1] = DPtrLen;

  Value *TotalAllocatedB = Builder.CreateAdd(TotalAllocatedA, TotalPtrLen);

  NPtr = EntryBuilder.CreateCall(ReallocFnc, ReallocCallArgs);

  if (hasMallocUsableSize) {
    vector<Type *> MallocUsableSizeArgs(1);
    MallocUsableSizeArgs[0] = Builder.getInt8PtrTy();
    FunctionType *MallocUsableSizeFt =
        FunctionType::get(Builder.getInt64Ty(), MallocUsableSizeArgs, false);
    Function *MallocUsableSizeFnc =
        Function::Create(MallocUsableSizeFt, Function::ExternalLinkage,
                         "malloc_usable_size", Mod);
    vector<Value *> MallocUsableSizeCallArgs(1);
    MallocUsableSizeCallArgs[0] = NPtr;

    Value *UsableSize =
        EntryBuilder.CreateCall(MallocUsableSizeFnc, MallocUsableSizeCallArgs);
    Constant *CurrentUsableSize = TotalUsableSize->getInitializer();
    Value *AddUsableSize = EntryBuilder.CreateAdd(CurrentUsableSize, UsableSize,
                                                  "Currentusablesize");
    EntryBuilder.CreateStore(AddUsableSize, TotalUsableSize);
  }

  EntryBuilder.CreateStore(DPtrLen, RealSize);

  vector<Value *> FreeCallArgs(1);

  vector<Value *> CallocCallArgs(2);
  CallocCallArgs[0] = Builder.getInt64(4);
  CallocCallArgs[1] = DPtrLen;

  Value *TotalAllocatedC = Builder.CreateAdd(TotalAllocatedB, TotalPtrLen);

  Ptr = EntryBuilder.CreateCall(CallocFnc, CallocCallArgs);

  vector<Value *> MemcmpCallArgs(3);
  MemcmpCallArgs[0] = NPtr;
  MemcmpCallArgs[1] = Ptr;
  MemcmpCallArgs[2] = DPtrLen;

  Value *MemcmpPtrs = EntryBuilder.CreateCall(MemcmpFnc, MemcmpCallArgs);
  Value *BcmpPtrs = EntryBuilder.CreateCall(BcmpFnc, MemcmpCallArgs);

  EntryBuilder.CreateStore(MemcmpPtrs, MemcmpRet);
  EntryBuilder.CreateStore(BcmpPtrs, BcmpRet);

  EntryBuilder.CreateCall(SafeBcmpFnc, MemcmpCallArgs);

  vector<Value *> SafeProcmapsCallArgs(1);
  SafeProcmapsCallArgs[0] = Builder.getInt32(-1);

  EntryBuilder.CreateCall(SafeProcmapsFnc, SafeProcmapsCallArgs);

  AllocaInst *ProcMaps =
      EntryBuilder.CreateAlloca(PProcMapType, nullptr, "Procmaps");

  FreeCallArgs[0] = NPtr;
  EntryBuilder.CreateCall(FreeFnc, FreeCallArgs);
  FreeCallArgs[0] = Ptr;
  EntryBuilder.CreateCall(FreeFnc, FreeCallArgs);

  Value *VPageSize = EntryBuilder.CreateCall(GetpagesizeFnc);

  EntryBuilder.CreateStore(VPageSize, PageSize);

  AllocaInst *DPPtr = EntryBuilder.CreateAlloca(
      Builder.getInt8PtrTy(), Constant::getNullValue(Builder.getInt8Ty()),
      "Dpptr");

  vector<Value *> PosixMemalignCallArgs(3);
  PosixMemalignCallArgs[0] = DPPtr;
  PosixMemalignCallArgs[1] = VPageSize;
  PosixMemalignCallArgs[2] = PtrLen;

  EntryBuilder.CreateCall(PosixMemalignFnc, PosixMemalignCallArgs);

  Value *PPtr = EntryBuilder.CreateLoad(DPPtr);

  Value *TotalAllocatedD = Builder.CreateAdd(TotalAllocatedC, PtrLen);

  FreeCallArgs[0] = PPtr;

  EntryBuilder.CreateCall(FreeFnc, FreeCallArgs);

  Value *Superpg = Builder.getInt64(2 * 1024 * 1024);

  vector<Value *> MmapCallArgs(6);
  MmapCallArgs[0] = Constant::getNullValue(Builder.getInt8PtrTy());
  MmapCallArgs[1] = PtrLen;
  MmapCallArgs[2] = ProtReadWrite->getInitializer();
  MmapCallArgs[3] = MapSharedAnon->getInitializer();
  MmapCallArgs[4] = Builder.getInt32(-1);
  MmapCallArgs[5] = Builder.getInt64(0);

  Value *LPtr = EntryBuilder.CreateCall(MmapFnc, MmapCallArgs);
  Value *TotalAllocatedE = nullptr;
  APInt MinusOne(64, -1, true);
  Constant *MinusOnePtr =
      Constant::getIntegerValue(Builder.getInt8PtrTy(), MinusOne);

  if (hasMapSuperpg) {
    MmapCallArgs[1] = Superpg;
    MmapCallArgs[3] = MapSuperPg->getInitializer();
    EntryBuilder.CreateCall(MmapFnc, MmapCallArgs);
    MmapCallArgs[1] = PtrLen;
    MmapCallArgs[3] = MapSharedAnon->getInitializer();
  }

  if (!hasMapConceal) {
    MmapCallArgs[3] = MapPrivate->getInitializer();
    Value *HPtr = EntryBuilder.CreateCall(MmapFnc, MmapCallArgs);

    vector<Value *> MadviseCallArgs(3);
    MadviseCallArgs[0] = HPtr;
    MadviseCallArgs[1] = PtrLen;
    MadviseCallArgs[2] = EntryBuilder.CreateLoad(MadvNoDump);

    EntryBuilder.CreateCall(MadviseFnc, MadviseCallArgs);
  } else {
    MmapCallArgs[3] = MapConceal->getInitializer();
    EntryBuilder.CreateCall(MmapFnc, MmapCallArgs);
  }

  if (LPtr != MinusOnePtr)
    TotalAllocatedE = Builder.CreateAdd(TotalAllocatedD, PtrLen);
  else
    TotalAllocatedE = TotalAllocatedD;

  EntryBuilder.CreateStore(TotalAllocatedE, TotalAllocated);

  MmapCallArgs[0] = LPtr;
  MmapCallArgs[3] = MapFixed->getInitializer();

  EntryBuilder.CreateCall(MmapFnc, MmapCallArgs);

  if (hasMremapLinux) {
    vector<Value *> MremapCallArgs(3);
    MremapCallArgs[0] = LPtr;
    MremapCallArgs[1] = PtrLen;
    MremapCallArgs[2] = PtrDblLen;

    EntryBuilder.CreateCall(MremapFnc, MremapCallArgs);
  } else if (hasMremapBSD) {
    vector<Value *> MremapCallArgs(4);
    MremapCallArgs[0] = LPtr;
    MremapCallArgs[1] = PtrLen;
    MremapCallArgs[2] = Constant::getNullValue(Builder.getInt8PtrTy());
    MremapCallArgs[3] = PtrDblLen;

    EntryBuilder.CreateCall(MremapFnc, MremapCallArgs);
  }

  ReturnInst::Create(Builder.getContext(), nullptr, Entry);
}

Function *addMemoryFunction(IRBuilder<> Builder) {
  FunctionType *Ft = FunctionType::get(Builder.getVoidTy(), false);
  Function *MemFnc =
      Function::Create(Ft, Function::InternalLinkage, "test_memory", Mod);

  return MemFnc;
}

void addMemoryBlock(IRBuilder<> Builder) {
  Function *TestFnc = Mod->getFunction("test_memory");
  Function *MemFnc = Mod->getFunction("func_calls_memory");
  BasicBlock *Entry =
      BasicBlock::Create(Builder.getContext(), "entry", TestFnc);
  Builder.SetInsertPoint(Entry);
  IRBuilder<> EntryBuilder = IRBuilder<>(Entry);

  if (Verbose)
    printReport(EntryBuilder, TestFnc);

  BasicBlock *EntryHeader = EntryBuilder.GetInsertBlock();
  Builder.SetInsertPoint(Entry);
  Value *Beg = EntryBuilder.getInt64(1);
  Value *Inc = EntryBuilder.getInt64(1);

  BasicBlock *Loop = BasicBlock::Create(Builder.getContext(), "loop", TestFnc);
  Builder.SetInsertPoint(Loop);
  IRBuilder<> LoopBuilder(Loop);
  BasicBlock *LoopHeader = LoopBuilder.GetInsertBlock();
  PHINode *I = LoopBuilder.CreatePHI(Builder.getInt64Ty(), 8, "I");
  I->addIncoming(Beg, EntryHeader);
  Value *Nxt = LoopBuilder.CreateAdd(I, Inc, "Nxt");

  LoopBuilder.CreateCall(MemFnc);

  Value *EndLoop = LoopBuilder.CreateICmpULT(I, Lim, "EndLoop");
  EndLoop = LoopBuilder.CreateICmpNE(EndLoop, Builder.getInt1(0), "LoopCond");
  EntryBuilder.CreateBr(Loop);

  BasicBlock *End = BasicBlock::Create(Builder.getContext(), "end", TestFnc);
  LoopBuilder.CreateCondBr(EndLoop, Loop, End);
  IRBuilder<> EndBuilder(End);
  Builder.SetInsertPoint(End);
  I->addIncoming(Nxt, LoopHeader);

  TestFunctions.push_back(TestFnc);

  ReturnInst::Create(Builder.getContext(), nullptr, End);
}

Function *addRandomnessFunction(IRBuilder<> Builder) {
  vector<Type *> RandomnessArgs(1);
  RandomnessArgs[0] = Builder.getInt64Ty();
  FunctionType *RandomFt =
      FunctionType::get(Builder.getInt64Ty(), RandomnessArgs, false);
  Function *RandomFnc =
      Function::Create(RandomFt, Function::InternalLinkage, "randomness", Mod);

  return RandomFnc;
}

void addRandomnessBlock(IRBuilder<> Builder) {
  Function *RandomFnc = Mod->getFunction("randomness");
  Function *MemsetFnc;

  if (hasExplicitBzero)
    MemsetFnc = Mod->getFunction("explicit_bzero");
  else if (hasExplicitMemset)
    MemsetFnc = Mod->getFunction("explicit_memset");
  else
    MemsetFnc = Mod->getFunction("memset");

  vector<Type *> SafeRandomArgs(2);
  SafeRandomArgs[0] = Builder.getInt8PtrTy();
  SafeRandomArgs[1] = Builder.getInt64Ty();
  FunctionType *SafeRandomFt =
      FunctionType::get(Builder.getVoidTy(), SafeRandomArgs, false);
  Function *SafeRandomFnc = Function::Create(
      SafeRandomFt, Function::ExternalLinkage, "safe_random", Mod);
  SafeRandomFnc->setCallingConv(CallingConv::C);

  BasicBlock *Entry =
      BasicBlock::Create(Builder.getContext(), "entry", RandomFnc);
  Builder.SetInsertPoint(Entry);
  IRBuilder<> EntryBuilder(Entry);
  static Value *One = Builder.getInt64(1);
  Value *RandomRng = nullptr;

  Value *Zero = EntryBuilder.getInt8(0);
  Zero->setName("Zero");

  Value *ABufferLim = EntryBuilder.CreateSub(ABufferSize, One);
  AllocaInst *ABuffer =
      Builder.CreateAlloca(Builder.getInt8Ty(), ABufferSize, "Abuffer");

  vector<Value *> MemsetCallArgs;

  if (hasExplicitBzero) {
    MemsetCallArgs = vector<Value *>(2);
    MemsetCallArgs[0] = ABuffer;
    MemsetCallArgs[1] = ABufferSize;
  } else {
    MemsetCallArgs = vector<Value *>(3);
    MemsetCallArgs[0] = ABuffer;
    MemsetCallArgs[1] = Zero;
    MemsetCallArgs[2] = ABufferSize;
  }

  EntryBuilder.CreateCall(MemsetFnc, MemsetCallArgs);

  MemsetFnc = Mod->getFunction("safe_bzero");
  vector<Value *> SafeBzeroCallArgs(2);
  SafeBzeroCallArgs[0] = ABuffer;
  SafeBzeroCallArgs[1] = ABufferSize;

  EntryBuilder.CreateCall(MemsetFnc, SafeBzeroCallArgs);

  if (hasArc4random) {
    FunctionType *Arc4randomFt =
        FunctionType::get(EntryBuilder.getInt32Ty(), false);
    Function *Arc4randomFnc = Function::Create(
        Arc4randomFt, Function::ExternalLinkage, "arc4random", Mod);
    Arc4randomFnc->setCallingConv(CallingConv::C);

    vector<Value *> Arc4randomCallArgs;
    RandomRng = EntryBuilder.CreateCall(Arc4randomFnc, Arc4randomCallArgs);

    if (hasGetentropy) {
      vector<Type *> GetentropyArgs(2);
      GetentropyArgs[0] = PointerType::get(EntryBuilder.getInt8Ty(), 0);
      GetentropyArgs[1] = EntryBuilder.getInt64Ty();

      FunctionType *GetentropyFt =
          FunctionType::get(EntryBuilder.getInt32Ty(), GetentropyArgs, false);
      Function *GetentropyFnc = Function::Create(
          GetentropyFt, Function::ExternalLinkage, "getentropy", Mod);
      GetentropyFnc->setCallingConv(CallingConv::C);

      vector<Value *> GetentropyCallArgs(2);
      GetentropyCallArgs[0] = ABuffer;
      GetentropyCallArgs[1] = ABufferLim;

      EntryBuilder.CreateCall(GetentropyFnc, GetentropyCallArgs);
    } else {
      ABuffer->setMetadata(Mod->getMDKindID("nosanitize"),
                           MDNode::get(EntryBuilder.getContext(), None));
      vector<Type *> Arc4randombufArgs(2);
      Arc4randombufArgs[0] = PointerType::get(Builder.getInt8Ty(), 0);
      Arc4randombufArgs[1] = Builder.getInt64Ty();

      FunctionType *Arc4randombufFt =
          FunctionType::get(EntryBuilder.getVoidTy(), Arc4randombufArgs, false);
      Function *Arc4randombufFnc = Function::Create(
          Arc4randombufFt, Function::ExternalLinkage, "arc4random_buf", Mod);
      Arc4randombufFnc->setCallingConv(CallingConv::C);

      vector<Value *> Arc4randombufCallArgs(2);
      Arc4randombufCallArgs[0] = ABuffer;
      Arc4randombufCallArgs[1] = ABufferLim;

      EntryBuilder.CreateCall(Arc4randombufFnc, Arc4randombufCallArgs);
    }
  } else {
    FunctionType *RandomFt =
        FunctionType::get(EntryBuilder.getInt32Ty(), false);
    Function *RandomFnc =
        Function::Create(RandomFt, Function::ExternalLinkage, "random", Mod);
    RandomFnc->setCallingConv(CallingConv::C);

    vector<Value *> RandomCallArgs;
    RandomRng = EntryBuilder.CreateCall(RandomFnc, RandomCallArgs);
    CallInst *RetGet = nullptr;

    if (hasGetentropy) {
      vector<Type *> GetentropyArgs(2);
      GetentropyArgs[0] = PointerType::get(EntryBuilder.getInt8Ty(), 0);
      GetentropyArgs[1] = EntryBuilder.getInt64Ty();

      FunctionType *GetentropyFt =
          FunctionType::get(EntryBuilder.getInt32Ty(), GetentropyArgs, false);
      Function *GetentropyFnc = Function::Create(
          GetentropyFt, Function::ExternalLinkage, "getentropy", Mod);
      GetentropyFnc->setCallingConv(CallingConv::C);

      vector<Value *> GetentropyCallArgs(2);
      GetentropyCallArgs[0] = ABuffer;
      GetentropyCallArgs[1] = ABufferLim;

      RetGet = EntryBuilder.CreateCall(GetentropyFnc, GetentropyCallArgs);
    }

    if (!RetGet && hasGetrandom) {
      vector<Type *> SyscallArgs(1);
      SyscallArgs[0] = Builder.getInt32Ty();
      FunctionType *SyscallFt =
          FunctionType::get(EntryBuilder.getInt64Ty(), SyscallArgs, true);
      Function *SyscallFnc = Function::Create(
          SyscallFt, Function::ExternalLinkage, "syscall", Mod);
      SyscallFnc->setCallingConv(CallingConv::C);

      vector<Value *> SyscallCallArgs(4);
      SyscallCallArgs[0] = Builder.CreateLoad(SyscallGetrandomId);
      SyscallCallArgs[1] = ABuffer;
      SyscallCallArgs[2] = ABufferLim;
      SyscallCallArgs[3] = Builder.CreateLoad(SyscallGetrandomMod);

      RetGet = EntryBuilder.CreateCall(SyscallFnc, SyscallCallArgs);
    }

    if (RetGet)
      RetGet->setMetadata(Mod->getMDKindID("nosanitize"),
                          MDNode::get(EntryBuilder.getContext(), None));
  }

  vector<Value *> SafeRandomCallArgs(2);
  SafeRandomCallArgs[0] = ABuffer;
  SafeRandomCallArgs[1] = ABufferLim;
  EntryBuilder.CreateCall(SafeRandomFnc, SafeRandomCallArgs);

  EntryBuilder.CreateStore(ABuffer, LastBuffer);
  EntryBuilder.CreateStore(RandomRng, LastRandomValue);

  ReturnInst::Create(Builder.getContext(), Builder.getInt64(0), Entry);
}

Function *addMTTest(IRBuilder<> Builder, string FName) {
  FunctionType *Ft = FunctionType::get(Builder.getVoidTy(), false);
  Function *TestFnc =
      Function::Create(Ft, Function::InternalLinkage, FName, Mod);

  return TestFnc;
}

void addMTTestBlock(IRBuilder<> Builder, const char *FName, Function *TestFnc) {
  Function *FFnc = Mod->getFunction(FName);
  BasicBlock *Entry =
      BasicBlock::Create(Builder.getContext(), "entry", TestFnc);
  IRBuilder<> EntryBuilder(Entry);
  Constant *ZeroPtr = Constant::getNullValue(Builder.getInt8PtrTy());

  if (Verbose)
    printReport(EntryBuilder, TestFnc);

  Value *NameStackLim = EntryBuilder.getInt64(16);
  AllocaInst *NameStack = EntryBuilder.CreateAlloca(EntryBuilder.getInt8PtrTy(),
                                                    NameStackLim, "Namestack");
  Value *NameStackPtr = EntryBuilder.CreateLoad(NameStack);
  vector<Type *> PthreadCreateArgs(4);
  PthreadCreateArgs[0] =
      PointerType::get(PointerType::getUnqual(PthreadType), 0);
  PthreadCreateArgs[1] =
      PointerType::get(PointerType::getUnqual(PthreadAttrType), 0);
  PthreadCreateArgs[2] = PointerType::get(FFnc->getFunctionType(), 0);
  PthreadCreateArgs[3] = Builder.getInt8PtrTy();
  FunctionType *PthreadCreateFt =
      FunctionType::get(Builder.getInt32Ty(), PthreadCreateArgs, false);
  Function *PthreadCreateFnc = Function::Create(
      PthreadCreateFt, Function::ExternalLinkage, "pthread_create", Mod);
  PthreadCreateFnc->setCallingConv(CallingConv::C);
  vector<Type *> emptyArgs(0);
  vector<Value *> emptyCallArgs(0);
  FunctionType *PthreadSelfFt =
      FunctionType::get(PthreadType, emptyArgs, false);
  Function *PthreadSelfFnc = Function::Create(
      PthreadSelfFt, Function::ExternalLinkage, "pthread_self", Mod);
  PthreadSelfFnc->setCallingConv(CallingConv::C);

  Value *Self = EntryBuilder.CreateCall(PthreadSelfFnc, emptyCallArgs);

  vector<Type *> PthreadJoinArgs(2);
  PthreadJoinArgs[0] = PointerType::getUnqual(PthreadType);
  PthreadJoinArgs[1] = PointerType::get(Builder.getInt8PtrTy(), 0);
  FunctionType *PthreadJoinFt =
      FunctionType::get(Builder.getInt32Ty(), PthreadJoinArgs, false);
  Function *PthreadJoinFnc = Function::Create(
      PthreadJoinFt, Function::ExternalLinkage, "pthread_join", Mod);
  PthreadJoinFnc->setCallingConv(CallingConv::C);

  AllocaInst *APtd = EntryBuilder.CreateAlloca(
      PointerType::getUnqual(PthreadType), nullptr, "Aptd");

  vector<Value *> PthreadCreateCallArgs(4);
  PthreadCreateCallArgs[0] = APtd;
  PthreadCreateCallArgs[1] = Constant::getNullValue(
      PointerType::get(PointerType::getUnqual(PthreadAttrType), 0));
  PthreadCreateCallArgs[2] = FFnc;
  PthreadCreateCallArgs[3] = ZeroPtr;

  Value *Ret = EntryBuilder.CreateCall(PthreadCreateFnc, PthreadCreateCallArgs);
  EntryBuilder.CreateStore(Ret, PthreadRetCall);

  Value *Ptd = EntryBuilder.CreateLoad(APtd);
  Value *ThreadName =
      EntryBuilder.CreateGlobalStringPtr("MP-frontend", "Threadname");
  EntryBuilder.CreateStore(ThreadName, OrigThreadName);

  if (hasPthreadNameLinux) {
    vector<Type *> PthreadSetnameNpArgs(2);
    PthreadSetnameNpArgs[0] = PointerType::getUnqual(PthreadType);
    PthreadSetnameNpArgs[1] = Builder.getInt8PtrTy();

    FunctionType *PthreadSetnameNpFt =
        FunctionType::get(Builder.getInt32Ty(), PthreadSetnameNpArgs, false);
    Function *PthreadSetnameNpFnc =
        Function::Create(PthreadSetnameNpFt, Function::ExternalLinkage,
                         "pthread_setname_np", Mod);
    PthreadSetnameNpFnc->setCallingConv(CallingConv::C);
    vector<Type *> PthreadGetnameNpArgs(3);
    PthreadGetnameNpArgs[0] = PointerType::getUnqual(PthreadType);
    PthreadGetnameNpArgs[1] = Builder.getInt8PtrTy();
    PthreadGetnameNpArgs[2] = Builder.getInt64Ty();

    FunctionType *PthreadGetnameNpFt =
        FunctionType::get(Builder.getInt32Ty(), PthreadGetnameNpArgs, false);
    Function *PthreadGetnameNpFnc =
        Function::Create(PthreadGetnameNpFt, Function::ExternalLinkage,
                         "pthread_getname_np", Mod);
    PthreadGetnameNpFnc->setCallingConv(CallingConv::C);

    vector<Value *> PthreadSetnameNpCallArgs(2);
    PthreadSetnameNpCallArgs[0] = Ptd;
    PthreadSetnameNpCallArgs[1] = ThreadName;

    Ret =
        EntryBuilder.CreateCall(PthreadSetnameNpFnc, PthreadSetnameNpCallArgs);
    EntryBuilder.CreateStore(Ret, PthreadSetnameRetCall);

    vector<Value *> PthreadGetnameNpCallArgs(3);
    PthreadGetnameNpCallArgs[0] = Ptd;
    PthreadGetnameNpCallArgs[1] = NameStack;
    PthreadGetnameNpCallArgs[2] = NameStackLim;

    EntryBuilder.CreateCall(PthreadGetnameNpFnc, PthreadGetnameNpCallArgs);
    EntryBuilder.CreateStore(NameStackPtr, GThreadName);
  } else if (hasPthreadNameBSD) {
    vector<Type *> PthreadSetnameNpArgs(2);
    PthreadSetnameNpArgs[0] = PointerType::getUnqual(PthreadType);
    PthreadSetnameNpArgs[1] = Builder.getInt8PtrTy();

    FunctionType *PthreadSetnameNpFt =
        FunctionType::get(Builder.getVoidTy(), PthreadSetnameNpArgs, false);
    Function *PthreadSetnameNpFnc =
        Function::Create(PthreadSetnameNpFt, Function::ExternalLinkage,
                         "pthread_set_name_np", Mod);
    PthreadSetnameNpFnc->setCallingConv(CallingConv::C);

    vector<Type *> PthreadGetnameNpArgs(3);
    PthreadGetnameNpArgs[0] = PointerType::getUnqual(PthreadType);
    PthreadGetnameNpArgs[1] = Builder.getInt8PtrTy();
    PthreadGetnameNpArgs[2] = Builder.getInt64Ty();

    vector<Value *> PthreadSetnameNpCallArgs(2);
    PthreadSetnameNpCallArgs[0] = Ptd;
    PthreadSetnameNpCallArgs[1] = ThreadName;

    EntryBuilder.CreateCall(PthreadSetnameNpFnc, PthreadSetnameNpCallArgs);
    EntryBuilder.CreateStore(Ret, PthreadSetnameRetCall);

    if (hasPthreadGetNameBSD) {
      FunctionType *PthreadGetnameNpFt =
          FunctionType::get(Builder.getVoidTy(), PthreadGetnameNpArgs, false);
      Function *PthreadGetnameNpFnc =
          Function::Create(PthreadGetnameNpFt, Function::ExternalLinkage,
                           "pthread_get_name_np", Mod);
      PthreadGetnameNpFnc->setCallingConv(CallingConv::C);

      vector<Value *> PthreadGetnameNpCallArgs(3);
      PthreadGetnameNpCallArgs[0] = Ptd;
      PthreadGetnameNpCallArgs[1] = NameStack;
      PthreadGetnameNpCallArgs[2] = NameStackLim;

      EntryBuilder.CreateCall(PthreadGetnameNpFnc, PthreadGetnameNpCallArgs);
      EntryBuilder.CreateStore(NameStackPtr, GThreadName);
    }
  } else {
    EntryBuilder.CreateStore(ThreadName, GThreadName);
  }

  vector<Value *> PthreadJoinCallArgs(2);
  PthreadJoinCallArgs[0] = Ptd;
  PthreadJoinCallArgs[1] =
      Constant::getNullValue(PointerType::get(Builder.getInt8PtrTy(), 0));

  EntryBuilder.CreateCall(PthreadJoinFnc, PthreadJoinCallArgs);

  TestFunctions.push_back(TestFnc);

  ReturnInst::Create(Builder.getContext(), nullptr, Entry);
}

Function *addMain(IRBuilder<> Builder) {
  FunctionType *Ft = FunctionType::get(Builder.getInt32Ty(), false);
  Function *MainFnc =
      Function::Create(Ft, Function::ExternalLinkage, "main", Mod);

  return MainFnc;
}

void addMainBlock(IRBuilder<> Builder, Function *MainFnc) {
  BasicBlock *MainEntry =
      BasicBlock::Create(Builder.getContext(), "entry", MainFnc);
  Builder.SetInsertPoint(MainEntry);
  Value *SecRet = Builder.getInt32(-2);

  reportSep = false;

  if (!hasArc4random) {
    vector<Type *> TimeArgs(1);
    TimeArgs[0] = PointerType::get(Builder.getInt64Ty(), 0);
    FunctionType *TimeFt =
        FunctionType::get(Builder.getInt64Ty(), TimeArgs, false);
    Function *TimeFnc =
        Function::Create(TimeFt, Function::ExternalLinkage, "time", Mod);
    TimeFnc->setCallingConv(CallingConv::C);

    vector<Value *> TimeCallArgs(1);
    TimeCallArgs[0] = GZero;
    Value *Now = Builder.CreateCall(TimeFnc, TimeCallArgs);

    vector<Type *> SRandomArgs(1);
    SRandomArgs[0] = Builder.getInt64Ty();
    FunctionType *SRandomFt =
        FunctionType::get(Builder.getVoidTy(), SRandomArgs, false);
    Function *SRandomFnc =
        Function::Create(SRandomFt, Function::ExternalLinkage, "srandom", Mod);
    SRandomFnc->setCallingConv(CallingConv::C);

    vector<Value *> SRandomCallArgs(1);
    SRandomCallArgs[0] = Now;
    Builder.CreateCall(SRandomFnc, SRandomCallArgs);
  }

  if (hasUnveil) {
    vector<Type *> UnveilArgs(2);
    UnveilArgs[0] = Builder.getInt8PtrTy();
    UnveilArgs[1] = Builder.getInt8PtrTy();
    FunctionType *UnveilFt =
        FunctionType::get(Builder.getInt32Ty(), UnveilArgs, false);
    Function *UnveilFnc =
        Function::Create(UnveilFt, Function::ExternalLinkage, "unveil", Mod);

    Value *CurrentDir = Builder.CreateGlobalStringPtr(".", "Pwd");
    Value *UnveilPerms =
        Builder.CreateGlobalStringPtr(UnveilPermissions, "Unveilpermissions");

    vector<Value *> UnveilCallArgs(2);
    UnveilCallArgs[0] = CurrentDir;
    UnveilCallArgs[1] = UnveilPerms;

    Builder.CreateCall(UnveilFnc, UnveilCallArgs);

    UnveilCallArgs[0] = Constant::getNullValue(Builder.getInt8PtrTy());
    UnveilCallArgs[1] = Constant::getNullValue(Builder.getInt8PtrTy());

    Builder.CreateCall(UnveilFnc, UnveilCallArgs);
  }

  if (hasGetauxval) {
    vector<Type *> GetauxvalArgs(1);
    GetauxvalArgs[0] = Builder.getInt64Ty();
    vector<Value *> GetauxvalCallArgs(1);
    GetauxvalCallArgs[0] = Builder.CreateLoad(AtHwcap);

    FunctionType *GetauxvalFt =
        FunctionType::get(Builder.getInt64Ty(), GetauxvalArgs, false);
    Function *GetauxvalFnc = Function::Create(
        GetauxvalFt, Function::ExternalLinkage, "getauxval", Mod);
    GetauxvalFnc->setCallingConv(CallingConv::C);

    Value *TAuxVec = Builder.CreateCall(GetauxvalFnc, GetauxvalCallArgs);

    Builder.CreateStore(TAuxVec, AuxVec);

    GetauxvalCallArgs[0] = Builder.CreateLoad(AtHwcap2);

    Value *TAuxVec2 = Builder.CreateCall(GetauxvalFnc, GetauxvalCallArgs);

    Builder.CreateStore(TAuxVec2, AuxVec2);
  } else if (hasElfauxinfo) {
    ConstantInt *LongSz =
        ConstantInt::get(Builder.getInt64Ty(), sizeof(unsigned long));
    AllocaInst *ALong =
        Builder.CreateAlloca(Builder.getInt8Ty(), nullptr, "Along");

    vector<Type *> ElfauxinfoArgs(3);
    ElfauxinfoArgs[0] = Builder.getInt32Ty();
    ElfauxinfoArgs[1] = Builder.getInt8PtrTy();
    ElfauxinfoArgs[2] = Builder.getInt64Ty();

    FunctionType *ElfauxinfoFt =
        FunctionType::get(Builder.getInt32Ty(), ElfauxinfoArgs, false);
    Function *ElfauxinfoFnc = Function::Create(
        ElfauxinfoFt, Function::ExternalLinkage, "elf_aux_info", Mod);
    ElfauxinfoFnc->setCallingConv(CallingConv::C);

    vector<Value *> ElfauxinfoCallArgs(3);
    ElfauxinfoCallArgs[0] = Builder.CreateLoad(MAtHwcap);
    ElfauxinfoCallArgs[1] = ALong;
    ElfauxinfoCallArgs[2] = LongSz;

    Value *TAuxVec = Builder.getInt8(0);
    Value *TAuxVec2 = Builder.getInt8(0);
    Builder.CreateCall(ElfauxinfoFnc, ElfauxinfoCallArgs);
    Builder.CreateStore(TAuxVec, ALong);
    Builder.CreateAdd(TAuxVec, AuxVec);

    ElfauxinfoCallArgs[0] = Builder.CreateLoad(MAtHwcap2);
    ElfauxinfoCallArgs[1] = ALong;

    Builder.CreateCall(ElfauxinfoFnc, ElfauxinfoCallArgs);
    Builder.CreateStore(TAuxVec2, ALong);
    Builder.CreateAdd(TAuxVec2, AuxVec2);
  } else {
    Builder.CreateStore(Builder.getInt64(0), AuxVec);
    Builder.CreateStore(Builder.getInt64(0), AuxVec2);
  }

  vector<Type *> ChgPathArgs(1);
  ChgPathArgs[0] = Builder.getInt8PtrTy();
  vector<Value *> ChgPathCallArgs(1);
  ChgPathCallArgs[0] = Builder.CreateGlobalStringPtr("HOME", "Homestr");

  FunctionType *GetenvFt =
      FunctionType::get(Builder.getInt8PtrTy(), ChgPathArgs, false);

  Function *GetenvFnc =
      Function::Create(GetenvFt, Function::ExternalLinkage, "getenv", Mod);

  Value *CurrentPath = Builder.CreateCall(GetenvFnc, ChgPathCallArgs);

  FunctionType *ChgPathFt =
      FunctionType::get(Builder.getInt32Ty(), ChgPathArgs, false);

  Function *ChrootFnc =
      Function::Create(ChgPathFt, Function::ExternalLinkage, "chroot", Mod);

  Function *ChdirFnc =
      Function::Create(ChgPathFt, Function::ExternalLinkage, "chdir", Mod);

  Value *RootPath = Builder.CreateGlobalStringPtr("/", "Rootpath");

  ChgPathCallArgs[0] = CurrentPath;

  Value *ChgPath = Builder.CreateCall(ChrootFnc, ChgPathCallArgs);
  if (Builder.CreateICmpEQ(ChgPath, Builder.getInt32(0))) {
    ChgPathCallArgs[0] = RootPath;
    Builder.CreateCall(ChdirFnc, ChgPathCallArgs);
  }

  if (hasPledge) {
    vector<Type *> PledgeArgs(2);
    PledgeArgs[0] = Builder.getInt8PtrTy();
    PledgeArgs[1] = Builder.getInt8PtrTy();

    FunctionType *PledgeFt =
        FunctionType::get(Builder.getInt32Ty(), PledgeArgs, false);
    Function *PledgeFnc =
        Function::Create(PledgeFt, Function::ExternalLinkage, "pledge", Mod);

    Value *PledgePerms =
        Builder.CreateGlobalStringPtr(PledgePermissions, "Pledgepermissions");

    vector<Value *> PledgeCallArgs(2);
    PledgeCallArgs[0] = PledgePerms;
    PledgeCallArgs[1] = Constant::getNullValue(Builder.getInt8PtrTy());

    SecRet = Builder.CreateCall(PledgeFnc, PledgeCallArgs);
    if (Builder.CreateICmpEQ(SecRet, Builder.getInt32(0)))
      SecSettings =
          Builder.CreateGlobalStringPtr(PledgePermissions, "Secsettings");
  } else if (hasCapsicum) {
    FunctionType *CapEnterFt = FunctionType::get(Builder.getInt32Ty(), false);
    Function *CapEnterFnc = Function::Create(
        CapEnterFt, Function::ExternalLinkage, "cap_enter", Mod);

    if (CapEnterFnc) {
      SecRet = Builder.CreateCall(CapEnterFnc);
      if (Builder.CreateICmpEQ(SecRet, Builder.getInt32(0)))
        SecSettings = Builder.CreateGlobalStringPtr("simple cap_enter call",
                                                    "Secsettings");
    }
  } else if (hasPrctl) {
    vector<Type *> PrctlArgs(1);
    PrctlArgs[0] = Builder.getInt32Ty();

    FunctionType *PrctlFt =
        FunctionType::get(Builder.getInt32Ty(), PrctlArgs, true);
    Function *PrctlFnc =
        Function::Create(PrctlFt, Function::ExternalLinkage, "prctl", Mod);

    if (PrctlFnc) {
      vector<Value *> PrctlCallArgs(2);
      PrctlCallArgs[0] = Builder.CreateLoad(PrctlSetSeccomp);
      PrctlCallArgs[1] = Builder.getInt32(0);

      SecRet = Builder.CreateCall(PrctlFnc, PrctlCallArgs);
      if (Builder.CreateICmpEQ(SecRet, Builder.getInt32(0)))
        SecSettings =
            Builder.CreateGlobalStringPtr("simple prctl call", "Secsettings");
    }
  }

  Builder.CreateStore(SecRet, SecRetCall);

  if (!SecSettings)
    SecSettings = Builder.CreateGlobalStringPtr("no sec features supported",
                                                "Secsettings");

  vector<Value *> TimeArgs(2);

  if (hasClockGettime) {
    vector<Type *> ClockTypes(2);
    ClockTypes[0] = Type::getInt32Ty(Builder.getContext());
    ClockTypes[1] = PointerType::getUnqual(TimespecType);

    FunctionType *ClockFt = FunctionType::get(
        Type::getInt32Ty(Builder.getContext()), ClockTypes, false);
    Function *ClockGettime = Function::Create(
        ClockFt, Function::ExternalLinkage, "clock_gettime", Mod);
    ClockGettime->setCallingConv(CallingConv::C);

    AStart = Builder.CreateAlloca(TimespecType, nullptr, "Astart");
    AStart->setMetadata(Mod->getMDKindID("nosanitize"),
                        MDNode::get(Builder.getContext(), None));
    AStart->setAlignment(8);
    AEnd = Builder.CreateAlloca(TimespecType, nullptr, "Aend");
    AEnd->setMetadata(Mod->getMDKindID("nosanitize"),
                      MDNode::get(Builder.getContext(), None));
    AEnd->setAlignment(8);

    TimeArgs[0] = Builder.CreateLoad(ClockMonotonic);
    TimeArgs[1] = AStart;

    CStartInst = Builder.CreateCall(ClockGettime, TimeArgs);
  } else {
    vector<Type *> GettimeTypes(2);
    GettimeTypes[0] = PointerType::getUnqual(TimevalType);
    GettimeTypes[1] = Builder.getInt8PtrTy();

    FunctionType *GettimeFt = FunctionType::get(
        Type::getInt32Ty(Builder.getContext()), GettimeTypes, false);
    Function *Gettimeofday = Function::Create(
        GettimeFt, Function::ExternalLinkage, "gettimeofday", Mod);
    Gettimeofday->setCallingConv(CallingConv::C);

    AStart = Builder.CreateAlloca(TimevalType, nullptr, "Astart");
    AStart->setAlignment(8);
    AEnd = Builder.CreateAlloca(TimevalType, nullptr, "Aend");
    AEnd->setAlignment(8);

    TimeArgs[0] = AStart;
    TimeArgs[1] = Constant::getNullValue(Builder.getInt8PtrTy());

    CStartInst = Builder.CreateCall(Gettimeofday, TimeArgs);
  }

  CStartInst->setMetadata(Mod->getMDKindID("nosanitize"),
                          MDNode::get(Builder.getContext(), None));
  ArrayRef<Value *> Args;

  for (const auto Fnc : TestFunctions)
    Builder.CreateCall(Fnc, Args);

  if (hasClockGettime) {
    Function *ClockGettime = Mod->getFunction("clock_gettime");
    TimeArgs[1] = AEnd;

    CEndInst = Builder.CreateCall(ClockGettime, TimeArgs);

    Value *TVsec = ConstantInt::get(Builder.getContext(), APInt(32, 0, true));
    Value *TVNsec = ConstantInt::get(Builder.getContext(), APInt(32, 1, true));
    vector<Value *> TimespecIndexes(2);

    TimespecIndexes[0] =
        ConstantInt::get(Builder.getContext(), APInt(32, 0, true));
    TimespecIndexes[1] = TVsec;
    StartFAccess = Builder.CreateInBoundsGEP(TimespecType, AStart,
                                             TimespecIndexes, "Startfaccess");
    StartFMbr = Builder.CreateLoad(StartFAccess, "Startfmbr");
    StartFMbr->setMetadata(Mod->getMDKindID("nosanitize"),
                           MDNode::get(Builder.getContext(), None));
    StartFMbr->setAlignment(8);
    TimespecIndexes[1] = TVNsec;
    StartSAccess = Builder.CreateInBoundsGEP(TimespecType, AStart,
                                             TimespecIndexes, "Startsaccess");
    StartSMbr = Builder.CreateLoad(StartSAccess, "Startsmbr");
    StartSMbr->setMetadata(Mod->getMDKindID("nosanitize"),
                           MDNode::get(Builder.getContext(), None));
    StartSMbr->setAlignment(8);

    TimespecIndexes[1] = TVsec;
    EndFAccess = Builder.CreateInBoundsGEP(TimespecType, AEnd, TimespecIndexes,
                                           "Endfaccess");
    EndFMbr = Builder.CreateLoad(EndFAccess, "Endfmbr");
    EndFMbr->setMetadata(Mod->getMDKindID("nosanitize"),
                         MDNode::get(Builder.getContext(), None));
    EndFMbr->setAlignment(8);
    TimespecIndexes[1] = TVNsec;
    EndSAccess = Builder.CreateInBoundsGEP(TimespecType, AEnd, TimespecIndexes,
                                           "Endsaccess");
    EndSMbr = Builder.CreateLoad(EndSAccess, "Endsmbr");
    EndSMbr->setMetadata(Mod->getMDKindID("nosanitize"),
                         MDNode::get(Builder.getContext(), None));
    EndSMbr->setAlignment(8);
  } else {
    Function *Gettimeofday = Mod->getFunction("gettimeofday");
    TimeArgs[0] = AEnd;

    CEndInst = Builder.CreateCall(Gettimeofday, TimeArgs);

    Value *TVsec = ConstantInt::get(Builder.getContext(), APInt(32, 0, true));
    Value *TVNsec = ConstantInt::get(Builder.getContext(), APInt(32, 1, true));
    vector<Value *> TimevalIndexes(2);

    TimevalIndexes[0] =
        ConstantInt::get(Builder.getContext(), APInt(32, 0, true));
    TimevalIndexes[1] = TVsec;
    StartFAccess = Builder.CreateInBoundsGEP(TimevalType, AStart,
                                             TimevalIndexes, "Startfaccess");
    StartFMbr = Builder.CreateLoad(StartFAccess, "Startfmbr");
    StartFMbr->setAlignment(8);
    TimevalIndexes[1] = TVNsec;
    StartSAccess = Builder.CreateInBoundsGEP(TimevalType, AStart,
                                             TimevalIndexes, "Startsaccess");
    StartSMbr = Builder.CreateLoad(StartSAccess, "Startsmbr");
    StartSMbr->setAlignment(8);

    TimevalIndexes[1] = TVsec;
    EndFAccess = Builder.CreateInBoundsGEP(TimevalType, AEnd, TimevalIndexes,
                                           "Endfaccess");
    EndFMbr = Builder.CreateLoad(EndFAccess, "Endfmbr");
    EndFMbr->setAlignment(8);
    TimevalIndexes[1] = TVNsec;
    EndSAccess = Builder.CreateInBoundsGEP(TimevalType, AEnd, TimevalIndexes,
                                           "Endsaccess");
  }

  CEndInst->setMetadata(Mod->getMDKindID("nosanitize"),
                        MDNode::get(Builder.getContext(), None));

  char buffer[1024];
  ::strlcpy(buffer, "has", sizeof(buffer));

  if (hasMallocUsableSize)
    ::strlcat(buffer, " malloc_usable_size", sizeof(buffer));

  if (hasClockGettime)
    ::strlcat(buffer, " clock_gettime", sizeof(buffer));

  if (hasArc4random)
    ::strlcat(buffer, " arc4random", sizeof(buffer));

  if (hasGetentropy)
    ::strlcat(buffer, " getentropy", sizeof(buffer));

  if (hasGetrandom)
    ::strlcat(buffer, " getrandom", sizeof(buffer));

  if (hasPledge)
    ::strlcat(buffer, " pledge", sizeof(buffer));

  if (hasUnveil)
    ::strlcat(buffer, " unveil", sizeof(buffer));

  if (hasCapsicum)
    ::strlcat(buffer, " capsicium", sizeof(buffer));

  if (hasPrctl)
    ::strlcat(buffer, " prctl", sizeof(buffer));

  if (hasExplicitBzero)
    ::strlcat(buffer, " explicit_bzero", sizeof(buffer));

  if (hasExplicitMemset)
    ::strlcat(buffer, " explicit_memset", sizeof(buffer));

  if (hasMremapLinux || hasMremapBSD)
    ::strlcat(buffer, " mremap", sizeof(buffer));

  if (hasMapConceal)
    ::strlcat(buffer, " map_conceal", sizeof(buffer));

  if (hasMapSuperpg)
    ::strlcat(buffer, " map_superpg", sizeof(buffer));

  if (hasTimingsafeCmp)
    ::strlcat(buffer, " timingsafe_mem/bcmp", sizeof(buffer));

  if (hasConsttimeMemequal)
    ::strlcat(buffer, " consttime_memequal", sizeof(buffer));

  if (hasGetauxval)
    ::strlcat(buffer, " getauxval", sizeof(buffer));

  if (hasElfauxinfo)
    ::strlcat(buffer, " elf_aux_info", sizeof(buffer));

  if (ForkMod)
    ::strlcat(buffer, " fork", sizeof(buffer));

  Value *ResultFmt = Builder.CreateGlobalStringPtr(
      "{\"%s\":{\"cpufeatures\":\"%s\",\"auxvec\":\"%d "
      "%d\",\"numtests\":%lld,\"iterations\":%lld,"
      "\"time\":%lld,\"total_allocated\":%lld,\"real_size\":%lld,\"usable_"
      "size\":%lld,\"string_buffer\":\"%s\",\"strlcpy_bytes_copied\":%lld,"
      "\"strlcat_bytes_copied\":%lld,"
      "\"buffer\":\"%s\",\"buffer_size\":%lld,\"page_size\":%lld,"
      "\"random_value\":%ld,\"sec_return\":%d,\"sec_settings\":\"%s\","
      "\"thread_return\":%d,\"thread_setname_return\":%d,\"memcmp_ret\":%d,"
      "\"bcmp_ret\":%d,\"thread_name\":\"%"
      "s\",\"orig_thread_name\":\"%s\",\"last_error\":%d,\"str_last_error\":"
      "\"%s\","
      "\"pointer_size\":%lld,"
      "\"pointer_double_size\":%lld,\"features_supported\":\"%s\"}}",
      "Resultfmt");

  Value *TargetTripleRef =
      Builder.CreateGlobalStringPtr(targetTriple, "Targettriple");
  Value *TargetFeaturesRef =
      Builder.CreateGlobalStringPtr(targetFeatures, "Targetfeatures");
  Value *OsFeaturesRef = Builder.CreateGlobalStringPtr(buffer, "Osfeatures");
  Value *SrcBufRef =
      Builder.CreateGlobalStringPtr(StringRef(randomStr.get()), "Srcbufref");

  vector<Value *> PrintfCallArgs;
  Value *Res = Builder.CreateSub(EndFMbr, StartFMbr);
  Value *RandomValue = Builder.CreateLoad(LastRandomValue);
  RandomValue = Builder.CreateSRem(RandomValue, Builder.getInt32(1024));

  vector<Value *> StrerrorCallArgs(1);
  StrerrorCallArgs[0] = Builder.CreateLoad(Errno);

  Value *StrErrno = Builder.CreateCall(StrerrorFnc, StrerrorCallArgs);

  PrintfCallArgs.push_back(ResultFmt);
  PrintfCallArgs.push_back(TargetTripleRef);
  PrintfCallArgs.push_back(TargetFeaturesRef);
  PrintfCallArgs.push_back(Builder.CreateLoad(AuxVec));
  PrintfCallArgs.push_back(Builder.CreateLoad(AuxVec2));
  PrintfCallArgs.push_back(Builder.getInt64(TestFunctions.size()));
  PrintfCallArgs.push_back(Lim);
  PrintfCallArgs.push_back(Res);
  PrintfCallArgs.push_back(Builder.CreateLoad(TotalAllocated));
  PrintfCallArgs.push_back(Builder.CreateLoad(RealSize));
  PrintfCallArgs.push_back(Builder.CreateLoad(TotalUsableSize));
  PrintfCallArgs.push_back(SrcBufRef);
  PrintfCallArgs.push_back(Builder.CreateLoad(SzStrlcpy));
  PrintfCallArgs.push_back(Builder.CreateLoad(SzStrlcat));
  PrintfCallArgs.push_back(Builder.CreateLoad(LastBuffer));
  PrintfCallArgs.push_back(ABufferSize);
  PrintfCallArgs.push_back(Builder.CreateLoad(PageSize));
  PrintfCallArgs.push_back(RandomValue);
  PrintfCallArgs.push_back(Builder.CreateLoad(SecRetCall));
  PrintfCallArgs.push_back(SecSettings);
  PrintfCallArgs.push_back(Builder.CreateLoad(PthreadRetCall));
  PrintfCallArgs.push_back(Builder.CreateLoad(PthreadSetnameRetCall));
  PrintfCallArgs.push_back(Builder.CreateLoad(MemcmpRet));
  PrintfCallArgs.push_back(Builder.CreateLoad(BcmpRet));
  PrintfCallArgs.push_back(Builder.CreateLoad(GThreadName));
  PrintfCallArgs.push_back(Builder.CreateLoad(OrigThreadName));
  PrintfCallArgs.push_back(Builder.CreateLoad(Errno));
  PrintfCallArgs.push_back(StrErrno);
  PrintfCallArgs.push_back(Builder.CreateLoad(PtrSize));
  PrintfCallArgs.push_back(Builder.CreateLoad(PtrDblSize));
  PrintfCallArgs.push_back(OsFeaturesRef);

  Builder.CreateCall(PrintfFnc, PrintfCallArgs);

  ReturnInst::Create(Builder.getContext(), Builder.getInt32(0), MainEntry);
}

void compileObjectFile(TargetMachine *targetMachine, string FileName) {
  legacy::PassManager pass;
  error_code EC;
  auto FileType = TargetMachine::CGFT_ObjectFile;

  raw_fd_ostream obj(FileName, EC, sys::fs::F_None);

#if LLVM_VERSION_MAJOR >= 7
  if (targetMachine->addPassesToEmitFile(pass, obj, nullptr, FileType)) {
#else
  if (targetMachine->addPassesToEmitFile(pass, obj, FileType)) {
#endif
    errs() << "TargetMachine can't emit\n";
    return;
  }

  pass.run(*Mod);
  obj.flush();
}

int tasks(void *oBuilder) {
  outs() << __func__ << " start\n";
  IRBuilder<> *tBuilder = reinterpret_cast<IRBuilder<> *>(oBuilder);
  IRBuilder<> Builder = *tBuilder;
  vector<Type *> PrintfArgs(1);
  PrintfArgs[0] = Builder.getInt8PtrTy();
  FunctionType *PrintfFt =
      FunctionType::get(Builder.getInt32Ty(), PrintfArgs, true);
  PrintfFnc =
      Function::Create(PrintfFt, Function::ExternalLinkage, "printf", Mod);
  PrintfFnc->setCallingConv(CallingConv::C);

  vector<Type *> StrerrorArgs(1);
  StrerrorArgs[0] = Builder.getInt32Ty();

  FunctionType *StrerrorFt =
      FunctionType::get(Builder.getInt8PtrTy(), StrerrorArgs, false);
  StrerrorFnc =
      Function::Create(StrerrorFt, Function::ExternalLinkage, "strerror", Mod);

  Function *Fnc = addBasicFunction(Builder, "increment");
  addBasicBlock(Builder, "entry", Instruction::BinaryOps::Add, Fnc);
  verifyFunction(*Fnc);

  Function *TestFnc = addTestFunction(Builder, "test_increment");
  addTestBlock(Builder, "increment", TestFnc);
  verifyFunction(*TestFnc);

  Fnc = addBasicFunction(Builder, "decrement");
  addBasicBlock(Builder, "entry", Instruction::BinaryOps::Sub, Fnc);
  verifyFunction(*Fnc);

  TestFnc = addTestFunction(Builder, "test_decrement");
  addTestBlock(Builder, "decrement", TestFnc);
  verifyFunction(*TestFnc);

  Fnc = addBasicFunction(Builder, "multiply");
  addBasicBlock(Builder, "entry", Instruction::BinaryOps::Mul, Fnc);
  verifyFunction(*Fnc);

  TestFnc = addTestFunction(Builder, "test_multiply");
  addTestBlock(Builder, "multiply", TestFnc);
  verifyFunction(*TestFnc);

  Fnc = addBasicFunction(Builder, "divide");
  addBasicBlock(Builder, "entry", Instruction::BinaryOps::SDiv, Fnc);
  verifyFunction(*Fnc);

  TestFnc = addTestFunction(Builder, "test_divide");
  addTestBlock(Builder, "divide", TestFnc);
  verifyFunction(*TestFnc);

  TestFnc = addMemoryTestFunction(Builder);
  addMemoryTestBlock(Builder);
  verifyFunction(*TestFnc);

  TestFnc = addMemoryFunction(Builder);
  addMemoryBlock(Builder);
  verifyFunction(*TestFnc);

  TestFnc = addRandomnessFunction(Builder);
  addRandomnessBlock(Builder);
  verifyFunction(*TestFnc);

  TestFnc = addTestFunction(Builder, "test_randomness");
  addTestBlock(Builder, "randomness", TestFnc);
  verifyFunction(*TestFnc);

  Fnc = addMTFunction(Builder, "mthread");
  addMTBlock(Builder, "mthread");
  verifyFunction(*Fnc);

  TestFnc = addMTTest(Builder, "test_mthread");
  addMTTestBlock(Builder, "mthread", TestFnc);
  verifyFunction(*TestFnc);

  Function *MainFnc = addMain(Builder);
  addMainBlock(Builder, MainFnc);
  verifyFunction(*MainFnc);

  setFunctionAttributes(targetCpu, targetFeatures, *Mod);
  outs() << __func__ << " end\n";

  return 0;
}

int main(int argc, char **argv) {
  llvm_shutdown_obj Exit;
  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  cl::ParseCommandLineOptions(argc, argv, "FrontEnd multipass");
  cl::PrintOptionValues();

  LLVMContext lctx;
  IRBuilder<> Builder(lctx);
  Mod = new Module("mpass", lctx);

  string Error;
  TargetOptions opt;

  if (TargetTriple != "")
    targetTriple = TargetTriple;

  auto currentTarget = TargetRegistry::lookupTarget(targetTriple, Error);

  if (!currentTarget) {
    errs() << "Invalid target\n";
    return 0;
  }

  const char *targetTripleStr = targetTriple.c_str();
  const char *osNameStr;

  bool isLinux, isFreeBSD, isOpenBSD, isNetBSD, isDragonFly, isDarwin,
      isAndroid;
  float osVersion = 0.0f;

  isLinux = isFreeBSD = isOpenBSD = isNetBSD = isDragonFly = isDarwin =
      isAndroid = false;

  if ((osNameStr = ::strstr(targetTripleStr, "linux")))
    isLinux = true;
  else if ((osNameStr = ::strstr(targetTripleStr, "freebsd")))
    isFreeBSD = true;
  else if ((osNameStr = ::strstr(targetTripleStr, "openbsd")))
    isOpenBSD = true;
  else if ((osNameStr = ::strstr(targetTripleStr, "netbsd")))
    isNetBSD = true;
  else if ((osNameStr = ::strstr(targetTripleStr, "dragonfly")))
    isDragonFly = true;
  else if ((osNameStr = ::strstr(targetTripleStr, "darwin")))
    isDarwin = true;
  else if ((osNameStr = ::strstr(targetTripleStr, "android")))
    isAndroid = true;

  if (isFreeBSD || isOpenBSD || isNetBSD || isDragonFly) {
    const char *versionStr = osNameStr;
    while (versionStr && !isdigit(*versionStr))
      ++versionStr;
    osVersion = ::strtof(versionStr, nullptr);
  }

  Optional<Reloc::Model> relocModel =
      Optional<Reloc::Model>(Reloc::Model::PIC_);
  int16_t optlevel = static_cast<int16_t>(::strtol(OptLevel.c_str(), 0, 10));
  int16_t codemodel = static_cast<int16_t>(::strtol(CodeLvl.c_str(), 0, 10));
  if (optlevel < 0)
    optlevel = 0;
  if (optlevel > 3)
    optlevel = 3;

  if (codemodel < 0)
    codemodel = 0;
  if (codemodel > 3)
    codemodel = 3;

  enum CodeGenOpt::Level optLevel;
  switch (optlevel) {
  case 0:
    optLevel = CodeGenOpt::None;
    break;
  case 1:
    optLevel = CodeGenOpt::Less;
    break;
  case 2:
    optLevel = CodeGenOpt::Default;
    break;
  case 3:
    optLevel = CodeGenOpt::Aggressive;
    break;
  }

  enum CodeModel::Model codeModel;
  switch (codemodel) {
  case 0:
    codeModel = CodeModel::Model::Small;
    break;
  case 1:
    codeModel = CodeModel::Model::Kernel;
    break;
  case 2:
    codeModel = CodeModel::Model::Medium;
    break;
  case 3:
    codeModel = CodeModel::Model::Large;
    break;
  }

  if (getCPUStr() != "")
    targetCpu = getCPUStr();

  if (!NoCpuFeatures) {
    if (getFeaturesStr() != "") {
      targetFeatures = getFeaturesStr();
    } else {
      StringMap<bool> HostFeatures;
      SubtargetFeatures Sb;

      if (!sys::getHostCPUFeatures(HostFeatures)) {
        errs() << "Could not get CPU features\n";
        return 0;
      }

      for (auto &Feature : HostFeatures)
        Sb.AddFeature(Feature.first(), Feature.second);

      targetFeatures = Sb.getString();
    }
  }

  Mod->setPIELevel(PIELevel::Small);
  auto targetMachine =
      unique_ptr<llvm::TargetMachine>(currentTarget->createTargetMachine(
          targetTriple, targetCpu, targetFeatures, opt, relocModel, codeModel,
          optLevel));

  hasMallocUsableSize = isLinux || isFreeBSD;
  hasClockGettime = !isDarwin;
  hasArc4random = isFreeBSD || isOpenBSD || isNetBSD || isDragonFly ||
                  isDarwin || isAndroid;
  hasGetentropy = isOpenBSD || isLinux || (isFreeBSD && osVersion > 11.9);
  hasGetrandom = isLinux || (isFreeBSD && osVersion > 11.9);
  hasPledge = isOpenBSD;
  hasUnveil = (isOpenBSD && osVersion > 6.3);
  hasCapsicum = isFreeBSD;
  hasPrctl = isLinux;
  hasExplicitBzero = isLinux || (isFreeBSD && osVersion > 10.9) || isOpenBSD;
  hasExplicitMemset = isNetBSD;
  hasMremapLinux = isLinux;
  hasMremapBSD = isNetBSD;
  hasGetauxval = isLinux;
  hasElfauxinfo = isFreeBSD;
  hasPthreadNameLinux = isLinux;
  hasPthreadNameBSD = isFreeBSD || isOpenBSD;
  hasPthreadGetNameBSD = ((isFreeBSD && osVersion >= 12) || isOpenBSD);
  hasMapConceal = isOpenBSD;
  hasMapSuperpg = isFreeBSD;
  hasTimingsafeCmp = isFreeBSD | isOpenBSD;
  hasConsttimeMemequal = isNetBSD;

  randomStrLen = ::random() % 64;
  randomStr = std::make_unique<char[]>(randomStrLen + 1);
  ::memset(randomStr.get(), 0, randomStrLen);
  for (auto x = 0; x < randomStrLen; x++)
    randomStr.get()[x] = (arc4random() % 68) + 48;

  vector<Type *> TimespecMembers(2);
  TimespecMembers[0] = IntegerType::getInt64Ty(Builder.getContext());
  TimespecMembers[1] = IntegerType::getInt64Ty(Builder.getContext());
  TimespecType = StructType::create(Builder.getContext(), "struct.timespec");
  TimespecType->setBody(TimespecMembers);

  vector<Type *> TimevalMembers(2);
  TimevalMembers[0] = IntegerType::getInt64Ty(Builder.getContext());
  TimevalMembers[1] = IntegerType::getInt32Ty(Builder.getContext());
  TimevalType = StructType::create(Builder.getContext(), "struct.timeval");
  TimevalType->setBody(TimevalMembers);

  PthreadType = StructType::create(Builder.getContext(), "struct.pthread");
  PthreadAttrType =
      StructType::create(Builder.getContext(), "struct.pthread_attr");

  vector<Type *> PProcMapMembers(7);
  PProcMapMembers[0] = PointerType::getInt8Ty(Builder.getContext());
  PProcMapMembers[1] = PointerType::getInt8Ty(Builder.getContext());
  PProcMapMembers[2] = IntegerType::getInt64Ty(Builder.getContext());
  PProcMapMembers[3] = IntegerType::getInt32Ty(Builder.getContext());
  PProcMapMembers[4] = IntegerType::getInt32Ty(Builder.getContext());
  PProcMapMembers[5] = IntegerType::getInt64Ty(Builder.getContext());
  PProcMapMembers[6] = PointerType::getInt8PtrTy(Builder.getContext());
  PProcMapType = StructType::create(Builder.getContext(), "struct.p_proc_map");
  PProcMapType->setBody(PProcMapMembers);

  Mod->setDataLayout(targetMachine->createDataLayout());
  Mod->setTargetTriple(targetTriple);

  int64_t lim = ::strtoll(NumIterations.c_str(), 0, 10);
  if (lim < 10000)
    lim = 10000;

  Lim = Builder.getInt64(lim);

  int64_t bufferSize = ::strtoll(RandomBufferSize.c_str(), 0, 10);
  if (bufferSize >= 8 && bufferSize <= 256)
    ABufferSize = Builder.getInt64(bufferSize);
  else
    ABufferSize = Builder.getInt64(32);

  int32_t SYS_getrandom = 278;
  int32_t GRND_NONBLOCK = 0x01;
  int32_t prctlSetSeccomp = 22;
  int32_t clockMonotonic = isLinux ? 1 : isFreeBSD ? 4 : 3;
  int32_t protRead = 1;
  int32_t protWrite = 2;
  int32_t mapShared = 1;
  int32_t mapPrivate = 2;
  int32_t mapFixed = 10;
  int32_t mapAnon = isLinux ? 1000 : 20;
  int32_t madvNodump = isLinux ? 16 : 10;
  int32_t mapConceal = 8000;
  int32_t mapAlignedSuper = 1 << 24;
  uint64_t sizeToAllocate = ::strtoull(SizeToAllocate.c_str(), 0, 10);
  if (sizeToAllocate < 4 || sizeToAllocate > INT_MAX)
    sizeToAllocate = 32UL;
  else if (sizeToAllocate % sizeof(void *))
    sizeToAllocate += (sizeToAllocate % sizeof(void *));

  Constant *SizeConst = ConstantInt::get(Builder.getInt64Ty(), sizeToAllocate);
  Constant *SizeDblConst =
      ConstantInt::get(Builder.getInt64Ty(), sizeToAllocate * 2);
  Constant *Zero = ConstantInt::get(Builder.getInt64Ty(), 0);
  Constant *Zero32 = ConstantInt::get(Builder.getInt32Ty(), 0);

  Constant *ZeroPtr = Constant::getNullValue(Builder.getInt8PtrTy());
  Constant *MinusOne = ConstantInt::get(Builder.getInt32Ty(), -1);
  Constant *ConstClockMonotonic =
      ConstantInt::get(Builder.getInt32Ty(), clockMonotonic);
  Constant *ConstSetSeccomp =
      ConstantInt::get(Builder.getInt32Ty(), prctlSetSeccomp);

  Constant *ConstProtReadWrite =
      ConstantInt::get(Builder.getInt32Ty(), protRead | protWrite);
  Constant *ConstMapSharedAnon =
      ConstantInt::get(Builder.getInt32Ty(), mapShared | mapAnon);
  Constant *ConstMapFixed =
      ConstantInt::get(Builder.getInt32Ty(), mapShared | mapAnon | mapFixed);
  Constant *ConstMapPrivate =
      ConstantInt::get(Builder.getInt32Ty(), mapPrivate);

  Constant *ConstMapConceal =
      ConstantInt::get(Builder.getInt32Ty(), mapPrivate | mapConceal);

  Constant *ConstMapSuperPg = ConstantInt::get(
      Builder.getInt32Ty(), mapPrivate | mapAnon | mapAlignedSuper);

  Constant *ConstMadvNoDump =
      ConstantInt::get(Builder.getInt32Ty(), madvNodump);

  SecRetCall = new GlobalVariable(*Mod, Builder.getInt32Ty(), false,
                                  GlobalVariable::PrivateLinkage, MinusOne,
                                  "Secretcall");

  SecSettings = nullptr;

  PtrSize =
      new GlobalVariable(*Mod, Builder.getInt64Ty(), false,
                         GlobalVariable::PrivateLinkage, SizeConst, "Ptrsize");

  PtrDblSize = new GlobalVariable(*Mod, Builder.getInt64Ty(), false,
                                  GlobalVariable::PrivateLinkage, SizeDblConst,
                                  "Ptrdblsize");

  TotalUsableSize = new GlobalVariable(*Mod, Builder.getInt64Ty(), false,
                                       GlobalVariable::PrivateLinkage, Zero,
                                       "Totalusablesize");

  TotalAllocated = new GlobalVariable(*Mod, Builder.getInt64Ty(), false,
                                      GlobalVariable::ExternalLinkage, Zero,
                                      "Totalallocated");

  AuxVec = new GlobalVariable(*Mod, Builder.getInt64Ty(), false,
                              GlobalVariable::ExternalLinkage, Zero, "AuxVec");
  AuxVec2 =
      new GlobalVariable(*Mod, Builder.getInt64Ty(), false,
                         GlobalVariable::ExternalLinkage, Zero, "AuxVec2");

  ClockMonotonic = new GlobalVariable(*Mod, Builder.getInt32Ty(), true,
                                      GlobalVariable::ExternalLinkage,
                                      ConstClockMonotonic, "Clockmonotonic");

  PrctlSetSeccomp = new GlobalVariable(*Mod, Builder.getInt32Ty(), true,
                                       GlobalVariable::ExternalLinkage,
                                       ConstSetSeccomp, "Prctlsetseccomp");

  ProtReadWrite = new GlobalVariable(*Mod, Builder.getInt32Ty(), true,
                                     GlobalVariable::ExternalLinkage,
                                     ConstProtReadWrite, "Protreadwrite");

  MapSharedAnon = new GlobalVariable(*Mod, Builder.getInt32Ty(), true,
                                     GlobalVariable::ExternalLinkage,
                                     ConstMapSharedAnon, "Mapsharedanon");

  MapPrivate = new GlobalVariable(*Mod, Builder.getInt32Ty(), true,
                                  GlobalVariable::ExternalLinkage,
                                  ConstMapPrivate, "Mapprivate");

  MapFixed = new GlobalVariable(*Mod, Builder.getInt32Ty(), true,
                                GlobalVariable::ExternalLinkage, ConstMapFixed,
                                "Mapfixed");

  MapConceal = new GlobalVariable(*Mod, Builder.getInt32Ty(), true,
                                  GlobalVariable::ExternalLinkage,
                                  ConstMapConceal, "MapConceal");

  MapSuperPg = new GlobalVariable(*Mod, Builder.getInt32Ty(), true,
                                  GlobalVariable::ExternalLinkage,
                                  ConstMapSuperPg, "MapSuperpage");

  MadvNoDump = new GlobalVariable(*Mod, Builder.getInt32Ty(), true,
                                  GlobalVariable::ExternalLinkage,
                                  ConstMadvNoDump, "Madvnodump");

  RealSize =
      new GlobalVariable(*Mod, Builder.getInt64Ty(), false,
                         GlobalVariable::PrivateLinkage, Zero, "Realsize");

  LastRandomValue =
      new GlobalVariable(*Mod, Builder.getInt32Ty(), false,
                         GlobalVariable::PrivateLinkage, Zero32, "Randomvalue");

  LastBuffer =
      new GlobalVariable(*Mod, Builder.getInt8PtrTy(), false,
                         GlobalVariable::PrivateLinkage, ZeroPtr, "Lastbuffer");

  GZero = new GlobalVariable(*Mod, Builder.getInt64Ty(), false,
                             GlobalVariable::PrivateLinkage, Zero, "Gzero");

  SzStrlcpy =
      new GlobalVariable(*Mod, Builder.getInt64Ty(), false,
                         GlobalVariable::PrivateLinkage, Zero, "Szstrlcpy");

  SzStrlcat =
      new GlobalVariable(*Mod, Builder.getInt64Ty(), false,
                         GlobalVariable::PrivateLinkage, Zero, "Szstrlcat");

  SyscallGetrandomId = new GlobalVariable(
      *Mod, Builder.getInt32Ty(), true, GlobalVariable::PrivateLinkage,
      Builder.getInt32(SYS_getrandom), "SYS_getrandom");

  SyscallGetrandomMod = new GlobalVariable(
      *Mod, Builder.getInt32Ty(), true, GlobalVariable::PrivateLinkage,
      Builder.getInt32(GRND_NONBLOCK), "GRND_NONBLOCK");

  AtHwcap = new GlobalVariable(*Mod, Builder.getInt64Ty(), true,
                               GlobalVariable::PrivateLinkage,
                               Builder.getInt64(16), "AtHwcap");
  AtHwcap2 = new GlobalVariable(*Mod, Builder.getInt64Ty(), true,
                                GlobalVariable::PrivateLinkage,
                                Builder.getInt64(26), "AtHwcap2");

  MAtHwcap = new GlobalVariable(*Mod, Builder.getInt32Ty(), true,
                                GlobalVariable::PrivateLinkage,
                                Builder.getInt64(25), "MAtHwcap");
  MAtHwcap2 = new GlobalVariable(*Mod, Builder.getInt32Ty(), true,
                                 GlobalVariable::PrivateLinkage,
                                 Builder.getInt64(26), "MAtHwcap2");

  GThreadName = new GlobalVariable(*Mod, Builder.getInt8PtrTy(), false,
                                   GlobalVariable::PrivateLinkage, ZeroPtr,
                                   "GThreadname");

  OrigThreadName = new GlobalVariable(*Mod, Builder.getInt8PtrTy(), false,
                                      GlobalVariable::PrivateLinkage, ZeroPtr,
                                      "Origthreadname");

  PageSize =
      new GlobalVariable(*Mod, Builder.getInt64Ty(), false,
                         GlobalVariable::PrivateLinkage, Zero, "Pagesize");

  MemcmpRet =
      new GlobalVariable(*Mod, Builder.getInt32Ty(), false,
                         GlobalVariable::PrivateLinkage, Zero, "Memcmpret");

  BcmpRet = new GlobalVariable(*Mod, Builder.getInt32Ty(), false,
                               GlobalVariable::PrivateLinkage, Zero, "Bcmpret");

  PthreadRetCall = new GlobalVariable(*Mod, Builder.getInt32Ty(), false,
                                      GlobalVariable::PrivateLinkage, MinusOne,
                                      "Pthreadretcall");

  PthreadSetnameRetCall = new GlobalVariable(*Mod, Builder.getInt32Ty(), false,
                                             GlobalVariable::PrivateLinkage,
                                             MinusOne, "PthreadSetnameretcall");

  Errno = new GlobalVariable(*Mod, Builder.getInt32Ty(), false,
                             GlobalVariable::ExternalLinkage, Zero, "errno");

  if (ForkMod) {
#if defined(__FreeBSD__)
    auto fpid = rfork(RFMEM | RFCFDG);
#else
    auto fpid = fork();
#endif
    if (fpid == -1) {
      errs() << "Error creating sub process\n";
    } else if (fpid > 0) {
      if (Verbose)
        outs() << "As forked task\n";
      tasks(reinterpret_cast<void *>(&Builder));
    } else {
      waitpid(fpid, nullptr, 0);
    }
  } else {
    tasks(reinterpret_cast<void *>(&Builder));
  }

  FILE *fp = ::fopen("objs/operands.ll", "wb");

  if (fp) {
    auto ofp = fileno(fp);
    raw_fd_ostream F(ofp, false, false);
    F << *Mod;
    ::fclose(fp);
  } else {
    errs() << "Could not write the IR file\n";
  }

  compileObjectFile(targetMachine.get(), "objs/operands.o");

  if (DisplayMod) {
    for (auto it = Mod->getFunctionList().begin();
         it != Mod->getFunctionList().end(); ++it) {
      outs() << *it << '\n';
    }
  }

  delete Mod;

  return 0;
}
