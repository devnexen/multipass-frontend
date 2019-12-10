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
    Function *SafefreeFnc;
    FunctionType *SafefreeFt;
    IntegerType *Int32Ty;
    IntegerType *Int64Ty;
    PointerType *VoidTy;
    Type *NoretTy;
    bool updateInst(Function *, CallInst *CI, SymbolTableList<Instruction> &,
                    SymbolTableList<Instruction>::iterator &, Function *);

  public:
    static char ID;
    CustomLibModPass()
        : ModulePass(ID), chg(0), SafebcmpFnc(nullptr), SafebcmpFt(nullptr),
          SafebzeroFnc(nullptr), SafebzeroFt(nullptr), SafememFnc(nullptr),
          SafememFt(nullptr), SaferandlFnc(nullptr), SaferandlFt(nullptr),
          SaferandiFnc(nullptr), SaferandiFt(nullptr), SafemallocFnc(nullptr),
          SafemallocFt(nullptr), SafefreeFnc(nullptr), SafefreeFt(nullptr) {
        auto verbose = ::getenv("VERBOSE");
        vb = verbose && *verbose == '1';
    }
    bool runOnModule(Module &) override;
    StringRef getPassName() const override { return "custom lib module pass"; }
};
} // namespace

char CustomLibModPass::ID = 0;

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
    const char *mallocfns[] = {"malloc"};
    const char *freefns[] = {"free"};

    LLVMContext &C = M.getContext();
    Int32Ty = IntegerType::getInt32Ty(C);
    Int64Ty = IntegerType::getInt64Ty(C);
    VoidTy = PointerType::getInt8PtrTy(C);
    NoretTy = IntegerType::getVoidTy(C);
    vector<Type *> SafebcmpArgs(3);
    vector<Type *> SafebzeroArgs(2);
    vector<Type *> SafememArgs(4);
    vector<Type *> SaferandlArgs(0);
    vector<Type *> SaferandiArgs(0);
    vector<Type *> SafemallocArgs(1);
    vector<Type *> SafefreeArgs(1);
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
    SafefreeArgs[0] = VoidTy;

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
    SafefreeFt = FunctionType::get(NoretTy, SafefreeArgs, false);
    SafefreeFnc =
        Function::Create(SafefreeFt, Function::ExternalLinkage, "safe_free", M);

    for (const auto &cmpfn : cmpfns) {
        Function *Fn = M.getFunction(cmpfn);
        if (Fn)
            fm[SafebcmpFnc].push_back(Fn);
    }

    for (const auto &zerofn : zerofns) {
        Function *Fn = M.getFunction(zerofn);
        if (Fn)
            fm[SafebzeroFnc].push_back(Fn);
    }

    for (const auto &memfn : memfns) {
        Function *Fn = M.getFunction(memfn);
        if (Fn)
            fm[SafememFnc].push_back(Fn);
    }

    for (const auto &randomfn : randomfns) {
        Function *Fn = M.getFunction(randomfn);
        if (Fn)
            fm[SaferandlFnc].push_back(Fn);
    }

    for (const auto &randfn : randfns) {
        Function *Fn = M.getFunction(randfn);
        if (Fn)
            fm[SaferandiFnc].push_back(Fn);
    }

    for (const auto &mallocfn : mallocfns) {
        Function *Fn = M.getFunction(mallocfn);
        if (Fn)
            fm[SafemallocFnc].push_back(Fn);
    }

    for (const auto &freefn : freefns) {
        Function *Fn = M.getFunction(freefn);
        if (Fn)
            fm[SafefreeFnc].push_back(Fn);
    }

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
                    if (updateInst(FCI, CI, BBlst, bbbeg, SafefreeFnc))
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
