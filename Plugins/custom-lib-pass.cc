#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/TypeFinder.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

using namespace llvm;
using namespace std;

namespace {
typedef vector<Function *> flst;
typedef map<Function *, flst> flmap;
class CustomLibModPass : public ModulePass {
    size_t chg;
    bool vb;
    flmap fm;
    ConstantInt *True;
    Function *SafebcmpFnc;
    FunctionType *SafebcmpFt;
    Function *SafebzeroFnc;
    FunctionType *SafebzeroFt;
    Function *SafememFnc;
    FunctionType *SafememFt;
    Function *SaferandlFnc;
    FunctionType *SaferandlFt;
    Function *SaferandiFnc;
    FunctionType *SaferandiFt;
    Function *SafemallocFnc;
    FunctionType *SafemallocFt;
    Function *SafecallocFnc;
    FunctionType *SafecallocFt;
    Function *SafereallocFnc;
    FunctionType *SafereallocFt;
    Function *SafefreeFnc;
    FunctionType *SafefreeFt;
    Function *SafememsetFnc;
    FunctionType *SafememsetFt;
    IntegerType *Int1Ty;
    IntegerType *Int32Ty;
    IntegerType *Int64Ty;
    PointerType *VoidTy;
    Type *NoretTy;
    bool updateMemsetInst(Function *, CallInst *,
                          SymbolTableList<Instruction> &,
                          SymbolTableList<Instruction>::iterator &);
    bool updateInst(Function *, CallInst *, SymbolTableList<Instruction> &,
                    SymbolTableList<Instruction>::iterator &, Function *);

  public:
    static char ID;
    CustomLibModPass() : ModulePass(ID), chg(0) {
        auto verbose = ::getenv("VERBOSE");
        vb = verbose && *verbose == '1';
    }
    bool runOnModule(Module &) override;
    StringRef getPassName() const override { return "custom lib module pass"; }
};
} // namespace

char CustomLibModPass::ID = 0;

bool CustomLibModPass::updateMemsetInst(
    Function *FCI, CallInst *CI, SymbolTableList<Instruction> &BBlst,
    SymbolTableList<Instruction>::iterator &bbbeg) {
    auto oname = FCI->getName().data();

    if (strncmp(oname, "llvm.memset", sizeof("llvm.memset") - 1))
        return false;

    FunctionType *FCT = FCI->getFunctionType();
    auto nArgs = CI->getNumArgOperands();
    vector<Value *> fnCallArgs(nArgs);

    for (auto i = 0ul; i < nArgs - 1; i++)
        fnCallArgs[i] = dyn_cast<Value>(CI->getArgOperand(i));
    fnCallArgs[nArgs - 1] = True;
    CallInst *cInst = CallInst::Create(FCT, FCI, fnCallArgs);
    auto &refit = bbbeg;
    BBlst.insert(bbbeg, cInst);
    BBlst.remove(refit);
    if (vb)
        outs() << *FCI << " intrinsic updated\n";
    return true;
}

bool CustomLibModPass::updateInst(Function *FCI, CallInst *CI,
                                  SymbolTableList<Instruction> &BBlst,
                                  SymbolTableList<Instruction>::iterator &bbbeg,
                                  Function *ToFnc) {
    flst::const_iterator fit;

    auto flm = fm.find(ToFnc);

    if (flm == fm.end())
        return false;

    auto fl = flm->second;

    FunctionType *ToFt = ToFnc->getFunctionType();

    if ((fit = find(fl.begin(), fl.end(), FCI)) != fl.end()) {
        vector<Value *> fnCallArgs;

        for (auto &Arg : CI->arg_operands()) {
            Value *Val = dyn_cast<Value>(Arg);
            fnCallArgs.push_back(Val);
        }

        CallInst *cInst = CallInst::Create(ToFt, ToFnc, fnCallArgs);
        auto &refit = bbbeg;
        BBlst.insert(bbbeg, cInst);
        BBlst.remove(refit);
        Instruction *SI = &(*bbbeg);
        Value *Ret = dyn_cast<Value>(cInst);

        if (Ret->getType() != NoretTy) {
            SI->setOperand(0, Ret);
            if (vb)
                outs() << *FCI << " function updated to " << *ToFnc << '\n';
            return true;
        }
    }

    auto nArgs = CI->getNumArgOperands();
    for (auto i = 0ul; i < nArgs; i++) {
        Function *FCN = dyn_cast<Function>(CI->getArgOperand(i));
        if (FCN) {
            flst::const_iterator fit;

            if ((fit = find(fl.begin(), fl.end(), FCN)) != fl.end()) {
                CI->setArgOperand(i, ToFnc);
                if (vb)
                    outs() << *FCN << " argument updated to " << *ToFnc << '\n';
                return true;
            }
        }
    }

    return false;
}

bool CustomLibModPass::runOnModule(Module &M) {
    const char *cmpfns[] = {"memcmp", "bcmp"};
    const char *randomfns[] = {"random"};
    const char *randfns[] = {"rand"};
    const char *zerofns[] = {"bzero"};
    const char *memfns[] = {"memmem"};
    const char *memsetfns[] = {"memset"};
    const char *mallocfns[] = {"malloc"};
    const char *callocfns[] = {"calloc"};
    const char *reallocfns[] = {"realloc"};
    const char *freefns[] = {"free"};

    LLVMContext &C = M.getContext();
    Int1Ty = IntegerType::getInt1Ty(C);
    Int32Ty = IntegerType::getInt32Ty(C);
    Int64Ty = IntegerType::getInt64Ty(C);
    VoidTy = PointerType::getInt8PtrTy(C);
    NoretTy = IntegerType::getVoidTy(C);
    True = ConstantInt::get(Int1Ty, true);
    vector<Type *> SafebcmpArgs(3);
    vector<Type *> SafebzeroArgs(2);
    vector<Type *> SafememArgs(4);
    vector<Type *> SaferandlArgs(0);
    vector<Type *> SaferandiArgs(0);
    vector<Type *> SafemallocArgs(1);
    vector<Type *> SafecallocArgs(2);
    vector<Type *> SafereallocArgs(2);
    vector<Type *> SafefreeArgs(1);
    vector<Type *> SafememsetArgs(3);
    SafebcmpArgs[0] = VoidTy;
    SafebcmpArgs[1] = VoidTy;
    SafebcmpArgs[2] = Int64Ty;
    SafebzeroArgs[0] = VoidTy;
    SafebzeroArgs[1] = Int64Ty;
    SafememArgs[0] = VoidTy;
    SafememArgs[1] = Int64Ty;
    SafememArgs[2] = VoidTy;
    SafememArgs[3] = Int64Ty;
    SafemallocArgs[0] = Int64Ty;
    SafecallocArgs[0] = Int64Ty;
    SafecallocArgs[1] = Int64Ty;
    SafereallocArgs[0] = VoidTy;
    SafereallocArgs[1] = Int64Ty;
    SafefreeArgs[0] = VoidTy;
    SafememsetArgs[0] = VoidTy;
    SafememsetArgs[1] = Int32Ty;
    SafememsetArgs[2] = Int64Ty;

    SafebcmpFt = FunctionType::get(Int32Ty, SafebcmpArgs, false);
    SafebcmpFnc =
        Function::Create(SafebcmpFt, Function::ExternalLinkage, "safe_bcmp", M);
    SafebzeroFt = FunctionType::get(Int32Ty, SafebzeroArgs, false);
    SafebzeroFnc = Function::Create(SafebzeroFt, Function::ExternalLinkage,
                                    "safe_bzero", M);
    SafememFt = FunctionType::get(VoidTy, SafememArgs, false);
    SafememFnc =
        Function::Create(SafememFt, Function::ExternalLinkage, "safe_mem", M);
    SaferandlFt = FunctionType::get(Int64Ty, SaferandlArgs, false);
    SaferandlFnc = Function::Create(SaferandlFt, Function::ExternalLinkage,
                                    "safe_rand_l", M);
    SaferandiFt = FunctionType::get(Int32Ty, SaferandiArgs, false);
    SaferandiFnc = Function::Create(SaferandiFt, Function::ExternalLinkage,
                                    "safe_rand_i", M);
    SafemallocFt = FunctionType::get(VoidTy, SafemallocArgs, false);
    SafemallocFnc = Function::Create(SafemallocFt, Function::ExternalLinkage,
                                     "safe_malloc", M);
    SafecallocFt = FunctionType::get(VoidTy, SafecallocArgs, false);
    SafecallocFnc = Function::Create(SafecallocFt, Function::ExternalLinkage,
                                     "safe_calloc", M);
    SafereallocFt = FunctionType::get(VoidTy, SafereallocArgs, false);
    SafereallocFnc = Function::Create(SafereallocFt, Function::ExternalLinkage,
                                     "safe_realloc", M);
    SafefreeFt = FunctionType::get(NoretTy, SafefreeArgs, false);
    SafefreeFnc =
        Function::Create(SafefreeFt, Function::ExternalLinkage, "safe_free", M);
    SafememsetFt = FunctionType::get(VoidTy, SafememsetArgs, false);
    SafememsetFnc = Function::Create(SafememsetFt, Function::ExternalLinkage,
                                     "safe_memset", M);

#define addOrigFn(KeyFn, fns)                                                  \
    do {                                                                       \
        for (const auto &fn : fns) {                                           \
            Function *Fn = M.getFunction(fn);                                  \
            if (Fn)                                                            \
                fm[KeyFn].push_back(Fn);                                       \
        }                                                                      \
    } while (0)

    addOrigFn(SafebcmpFnc, cmpfns);
    addOrigFn(SafebzeroFnc, zerofns);
    addOrigFn(SafememFnc, memfns);
    addOrigFn(SaferandlFnc, randomfns);
    addOrigFn(SaferandiFnc, randfns);
    addOrigFn(SafemallocFnc, mallocfns);
    addOrigFn(SafecallocFnc, callocfns);
    addOrigFn(SafereallocFnc, reallocfns);
    addOrigFn(SafefreeFnc, freefns);
    addOrigFn(SafememsetFnc, memsetfns);

    for (auto &F : M) {
        for (auto &BB : F) {
            auto &BBlst = BB.getInstList();

            for (auto bbbeg = BBlst.begin(), bbend = BBlst.end();
                 bbbeg != bbend; bbbeg++) {
                CallInst *CI = dyn_cast<CallInst>(&(*bbbeg));

                if (CI) {
                    Function *FCI = CI->getCalledFunction();
                    if (!FCI)
                        continue;

                    if (updateMemsetInst(FCI, CI, BBlst, bbbeg))
                        chg++;
                    if (updateInst(FCI, CI, BBlst, bbbeg, SafebcmpFnc))
                        chg++;
                    if (updateInst(FCI, CI, BBlst, bbbeg, SafememFnc))
                        chg++;
                    if (updateInst(FCI, CI, BBlst, bbbeg, SaferandlFnc))
                        chg++;
                    if (updateInst(FCI, CI, BBlst, bbbeg, SaferandiFnc))
                        chg++;
                    if (updateInst(FCI, CI, BBlst, bbbeg, SafemallocFnc))
                        chg++;
                    if (updateInst(FCI, CI, BBlst, bbbeg, SafecallocFnc))
                        chg++;
                    if (updateInst(FCI, CI, BBlst, bbbeg, SafereallocFnc))
                        chg++;
                    if (updateInst(FCI, CI, BBlst, bbbeg, SafefreeFnc))
                        chg++;
                    if (updateInst(FCI, CI, BBlst, bbbeg, SafememsetFnc))
                        chg++;
                }
            }
        }
    }

    return (chg > 0);
}

static RegisterPass<CustomLibModPass>
    CMP("custom-lib", "Custom library function pass", false, false);
static RegisterStandardPasses X(PassManagerBuilder::EP_EnabledOnOptLevel0,
                                [](const PassManagerBuilder &,
                                   legacy::PassManagerBase &PM) {
                                    PM.add(new CustomLibModPass());
                                });
