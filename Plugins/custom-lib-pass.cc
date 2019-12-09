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
class CustomLibModPass : public ModulePass {
    size_t chg;
    flst fl;
    Function *SafebcmpFnc;
    FunctionType *SafebcmpFt;
    bool updateInst(Function *, CallInst *CI, SymbolTableList<Instruction> &,
                    SymbolTableList<Instruction>::iterator);

  public:
    static char ID;
    CustomLibModPass()
        : ModulePass(ID), chg(0), SafebcmpFnc(nullptr), SafebcmpFt(nullptr) {}
    bool runOnModule(Module &) override;
    StringRef getPassName() const override { return "custom lib module pass"; }
};
} // namespace

char CustomLibModPass::ID = 0;

bool CustomLibModPass::updateInst(
    Function *FCI, CallInst *CI, SymbolTableList<Instruction> &BBlst,
    SymbolTableList<Instruction>::iterator bbbeg) {
    flst::const_iterator fit;

    if ((fit = find(fl.begin(), fl.end(), FCI)) != fl.end()) {
        vector<Value *> fnCallArgs;

        for (auto &Arg : CI->args()) {
            Value *Val = dyn_cast<Value>(Arg);
            fnCallArgs.push_back(Val);
        }

        CallInst *cInst = CallInst::Create(SafebcmpFt, SafebcmpFnc, fnCallArgs);
        auto refit = bbbeg;
        BBlst.insert(++bbbeg, cInst);
        BBlst.remove(refit);
        StoreInst *SI = dyn_cast<StoreInst>(&(*bbbeg));

        if (SI) {
            SI->setOperand(0, cInst);
            return true;
        }
    }

    return false;
}

bool CustomLibModPass::runOnModule(Module &M) {
    const char *cmpfns[] = {"memcmp", "bcmp"};

    LLVMContext &C = M.getContext();
    IntegerType *Int32Ty = IntegerType::getInt32Ty(C);
    IntegerType *Int64Ty = IntegerType::getInt64Ty(C);
    PointerType *VoidTy = PointerType::getInt8PtrTy(C);
    vector<Type *> SafebcmpArgs(3);
    SafebcmpArgs[0] = VoidTy;
    SafebcmpArgs[1] = VoidTy;
    SafebcmpArgs[2] = Int64Ty;

    SafebcmpFt = FunctionType::get(Int32Ty, SafebcmpArgs, false);
    SafebcmpFnc =
        Function::Create(SafebcmpFt, Function::ExternalLinkage, "safe_bcmp", M);

    for (const auto &cmpfn : cmpfns) {
        Function *Fn = M.getFunction(cmpfn);
        if (Fn)
            fl.push_back(Fn);
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

                    if (!updateInst(FCI, CI, BBlst, bbbeg)) {
                        for (auto &Arg : CI->arg_operands()) {
                            auto *A = &(*Arg);
                            FCI = dyn_cast<Function>(A);
                            if (FCI) {
                                outs() << *FCI << '\n';
                                flst::const_iterator fit;

                                if ((fit = find(fl.begin(), fl.end(), FCI)) !=
                                    fl.end()) {
                                    A = *fit;
                                }
                            }
                        }
                    } else {
                        outs() << FCI->getName() << " updated to "
                               << SafebcmpFnc->getName() << '\n';
                        chg++;
                    }
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
