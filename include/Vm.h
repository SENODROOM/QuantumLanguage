#pragma once
#include "Opcode.h"
#include "Value.h"
#include "Error.h"
#include <vector>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <string>

// ─── Upvalue (heap cell for captured variables) ───────────────────────────────
// While the captured variable is still live, the upvalue refers to its stack
// slot by index — never by address: the stack is a std::deque, and inserting
// into its middle (a bound method call slots `self` in under the arguments)
// invalidates every element reference. Once the variable leaves the stack
// the upvalue is closed and owns the value.
struct Upvalue
{
    std::deque<QuantumValue> *stack = nullptr; // non-null while open
    size_t index = 0;                           // stack slot while open
    QuantumValue closed;                        // the value once closed

    Upvalue(std::deque<QuantumValue> *s, size_t i) : stack(s), index(i) {}

    QuantumValue get() const
    {
        if (!stack)
            return closed;
        return index < stack->size() ? (*stack)[index] : QuantumValue();
    }
    void set(QuantumValue v)
    {
        if (!stack)
            closed = std::move(v);
        else if (index < stack->size())
            (*stack)[index] = std::move(v);
    }
    void close()
    {
        if (!stack)
            return;
        if (index < stack->size())
            closed = (*stack)[index];
        stack = nullptr;
    }
};

// ─── Closure ──────────────────────────────────────────────────────────────────
struct Closure
{
    std::shared_ptr<Chunk> chunk;
    std::vector<std::shared_ptr<Upvalue>> upvalues;
    std::string name;

    explicit Closure(std::shared_ptr<Chunk> c)
        : chunk(std::move(c)), name(chunk->name) {}
};

// ─── CallFrame ────────────────────────────────────────────────────────────────
struct CallFrame
{
    std::shared_ptr<Closure> closure;
    size_t ip;        // instruction pointer
    size_t stackBase; // where locals start on the value stack
    int argCount = 0; // arguments the caller actually supplied (incl. self)
    // Per parameter slot, whether the caller supplied it — set when keyword
    // arguments leave gaps (`f(a, c=3)` skips `b`); empty means "the first
    // argCount slots".
    std::vector<bool> passed;
};

// ─── ExceptionHandler ─────────────────────────────────────────────────────────
struct ExceptionHandler
{
    int32_t catchIp;   // IP to jump to on exception
    size_t frameDepth; // call-frame depth to unwind to
    size_t stackDepth; // value stack depth to restore
};

// ─── STL-style iterators (VmStl.cpp) ──────────────────────────────────────────
// `v.begin() + n` into an array is a Dict {"__it_arr": array, "__it_pos": n};
// iterator arithmetic, comparison and `*it` are handled in the VM, and the
// <algorithm> natives (sort, find, max_element, ...) take such ranges.
bool isStlIterator(const QuantumValue &v);
QuantumValue makeStlIterator(std::shared_ptr<Array> arr, long pos);
std::shared_ptr<Array> stlIteratorArray(const QuantumValue &v);
long stlIteratorPos(const QuantumValue &v);
QuantumValue stlIteratorDeref(const QuantumValue &v);

// Keyword arguments reach a keyword-aware native (print, sorted, list.sort,
// min, max) as a trailing dict tagged "__kwargs__" (see Op::CALL). Removes
// and returns it, or nullptr when the call had none.
std::shared_ptr<Dict> takeKwargs(std::vector<QuantumValue> &args);

// ─── JavaScript runtime objects (VmJsRuntime.cpp) ─────────────────────────────
// Date / URL / URLSearchParams are dicts tagged by a hidden key; their methods
// dispatch through callJsObjectMethod (from VM::callDictMethod).
bool isJsDate(const QuantumValue &v);
double jsDateMs(const QuantumValue &v);
QuantumValue makeJsDateModule(std::shared_ptr<double> simulatedMs);
bool callJsObjectMethod(const std::shared_ptr<Dict> &d, const std::string &method,
                        const std::vector<QuantumValue> &args, QuantumValue &out);

// ─── VM ───────────────────────────────────────────────────────────────────────
class VM
{
public:
    VM();

    // Execute a compiled chunk (top-level script).
    void run(std::shared_ptr<Chunk> chunk);

    // Expose globals so the REPL can persist state across calls.
    std::shared_ptr<Environment> globals;

private:
    // Value stack
    std::deque<QuantumValue> stack_;
    std::vector<CallFrame> frames_;
    std::vector<ExceptionHandler> handlers_;

    // Open upvalues linked list (for closing)
    std::vector<std::shared_ptr<Upvalue>> openUpvalues_;

    // Keyword-argument binding (Op::KW_NAMES → the next CALL).
    std::vector<std::string> pendingKwNames_;
    // Set by bindKeywordArgs: the args on the stack are already laid out one
    // per parameter, so callClosure must not re-collect varargs.
    bool kwArranged_ = false;
    std::vector<bool> kwPassed_;
    // Reorders the `argCount` values on top of the stack, named by `names`,
    // into `closure`'s parameter order: keywords bind by name, unmatched
    // ones fill a **kwargs dict (or remaining slots), extras a *args array.
    // `hasSelf`: the closure's first parameter is a receiver not yet on the
    // stack. Returns the new argument count.
    int bindKeywordArgs(const std::shared_ptr<Closure> &closure, int argCount,
                        const std::vector<std::string> &names, bool hasSelf);

    long long stepCount_ = 0;
    static constexpr long long MAX_STEPS = 200'000'000;
    std::vector<std::pair<QuantumValue, size_t>> pendingInstances_;

    // ── Native registration ───────────────────────────────────────────────────
    void registerNatives();
    void registerStlNatives(); // <algorithm>-style free functions (VmStl.cpp)
    void registerJsRuntimeNatives(); // URL, window, encodeURIComponent (VmJsRuntime.cpp)
    // Globals that yield to a same-named member inside a method: common
    // identifiers like `next`/`count`/`find` registered as STL algorithms
    // must not shadow a C++ class's own `next` field or `find()` method under
    // implicit `this`. A user definition of the name removes it from the set.
    std::unordered_set<std::string> weakGlobals_;
    // Ordering used by sort/max_element/... without a comparator: numbers,
    // strings, arrays (pairs) lexicographically, instances via __lt__.
    bool stlLess(const QuantumValue &a, const QuantumValue &b);
    // Stable sort shared by sorted() and list.sort(): `key` (Python key=)
    // maps each element to its sort key; otherwise `cmp` is a JS/C++/Ruby
    // comparator (a number < 0 or `true` means a sorts first). `reverse`
    // keeps equal elements in order, as Python's does.
    void sortValues(Array &arr, const QuantumValue &key, const QuantumValue &cmp, bool reverse);

    // ── Execution ────────────────────────────────────────────────────────────
    void runFrame(size_t stopDepth = 0);

    // ── Stack helpers ─────────────────────────────────────────────────────────
    void push(QuantumValue v);
    QuantumValue pop();
    QuantumValue &peek(int offset = 0);

    // ── Call helpers ──────────────────────────────────────────────────────────
    void callValue(QuantumValue callee, int argCount, int line);
    void callClosure(std::shared_ptr<Closure> closure, int argCount, int line);
    void callNativeFn(std::shared_ptr<QuantumNative> fn, int argCount, int line);
    void callClass(std::shared_ptr<QuantumClass> klass, int argCount, int line);
    // Calls any callable (closure, bound method, native) to completion from
    // native code — e.g. a JS replace() callback — and returns its result.
    QuantumValue invokeCallable(const QuantumValue &fn, const std::vector<QuantumValue> &args);
    // Calls instance method `name` (e.g. __getitem__) if the instance's
    // class defines it; returns false otherwise.
    bool invokeMagic(const QuantumValue &inst, const char *name,
                     const std::vector<QuantumValue> &args, QuantumValue &out);
    QuantumValue callBuiltinMethod(QuantumValue &obj,
                                   const std::string &method,
                                   std::vector<QuantumValue> args,
                                   int line);
    QuantumValue callArrayMethod(std::shared_ptr<Array> arr,
                                 const std::string &method,
                                 std::vector<QuantumValue> args);
    QuantumValue callStringMethod(const std::string &s,
                                  const std::string &method,
                                  std::vector<QuantumValue> args);
    QuantumValue callDictMethod(std::shared_ptr<Dict> d,
                                const std::string &method,
                                std::vector<QuantumValue> args);

    // ── Upvalue helpers ───────────────────────────────────────────────────────
    std::shared_ptr<Upvalue> captureUpvalue(size_t stackIdx);
    void closeUpvalues(size_t fromIdx);
    // Inserts below the top of the stack, keeping open upvalues on the
    // shifted slots attached to their variables.
    void insertOnStack(size_t pos, QuantumValue v);

    // ── Binary / unary ops ────────────────────────────────────────────────────
    QuantumValue execBinary(Op op, const QuantumValue &left, const QuantumValue &right, int line);
    QuantumValue execUnary(Op op, const QuantumValue &val, int line);

    // Iterator state is stored inside each iterator native's fn closure

    // ── Misc helpers ──────────────────────────────────────────────────────────
    static std::string valueEq(const QuantumValue &a, const QuantumValue &b);
    static bool valuesEqual(const QuantumValue &a, const QuantumValue &b);
    double toNumber(const QuantumValue &v, const std::string &ctx, int line);
    void runtimeError(const std::string &msg, int line);
};