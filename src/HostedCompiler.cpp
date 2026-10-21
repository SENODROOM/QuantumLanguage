// Runs the self-hosted compiler (selfhost/compiler.sa) from the bytecode
// embedded at build time. One VM holds the loaded compiler for the life of
// the process; each compile calls one of its entry points, which return a
// result dict ({"ok", "bytes", ...} or the error's kind/message/line/col).
#include "HostedCompiler.h"
#include "Error.h"
#include "Serializer.h"
#include "Vm.h"

#include <memory>
#include <stdexcept>
#include <unordered_map>

// Generated from selfhost/compiler.qbc (cmake/EmbedBytecode.cmake).
extern const unsigned char kHostedCompilerBytecode[];
extern const size_t kHostedCompilerBytecodeSize;

namespace hosted
{
namespace
{
// Lives until the process exits (never destroyed: no teardown ordering
// against other statics at exit).
VM *g_vm = nullptr;

VM &compilerVm()
{
    if (!g_vm)
    {
        auto vm = std::make_unique<VM>();
        vm->unlimitedSteps();
        std::vector<uint8_t> bytes(kHostedCompilerBytecode, kHostedCompilerBytecode + kHostedCompilerBytecodeSize);
        vm->run(Serializer::deserialize(bytes)); // defines the compiler's functions
        g_vm = vm.release();
    }
    return *g_vm;
}

QuantumValue callCompiler(const std::string &entry, const std::vector<QuantumValue> &args)
{
    try
    {
        return compilerVm().callGlobal(entry, args);
    }
    catch (const QuantumError &e)
    {
        // a fault inside the compiler leaves its VM mid-call: start afresh
        delete g_vm;
        g_vm = nullptr;
        throw std::runtime_error("internal compiler error: " + std::string(e.what()));
    }
}

const QuantumValue &field(const QuantumValue &d, const char *key)
{
    static const QuantumValue nil;
    const auto &dict = *d.asDict();
    auto it = dict.find(key);
    return it == dict.end() ? nil : it->second;
}

// Throws the error a failed result describes; returns its bytes otherwise.
std::vector<uint8_t> unpack(const QuantumValue &r, std::string *compiledOut)
{
    if (!r.isDict())
        throw std::runtime_error("internal compiler error: unexpected result");
    if (compiledOut && field(r, "compiled").isString())
        *compiledOut = field(r, "compiled").asString();
    if (field(r, "ok").isTruthy())
    {
        const QuantumValue &b = field(r, "bytes");
        if (!b.isString())
            return {};
        const std::string &s = b.asString();
        return std::vector<uint8_t>(s.begin(), s.end());
    }
    const std::string kind = field(r, "kind").toString();
    const std::string message = field(r, "message").toString();
    const QuantumValue &lineV = field(r, "line");
    const QuantumValue &colV = field(r, "col");
    const int line = lineV.isNumber() ? (int)lineV.asNumber() : 0;
    const int col = colV.isNumber() ? (int)colV.asNumber() : 0;
    if (kind == "ParseError")
        throw ParseError(message, line, col);
    if (kind == "LexError")
        throw QuantumError("LexError", message, line);
    if (kind == "Fatal")
        throw std::runtime_error(message);
    throw std::runtime_error("internal compiler error: " + message);
}

// ── The C++ AST as ast.sa nodes ─────────────────────────────────────────────

QuantumValue str(const std::string &s) { return QuantumValue(s); }

QuantumValue strings(const std::vector<std::string> &v)
{
    auto a = std::make_shared<Array>();
    for (const auto &s : v)
        a->push_back(QuantumValue(s));
    return QuantumValue(a);
}

QuantumValue bools(const std::vector<bool> &v)
{
    auto a = std::make_shared<Array>();
    for (bool b : v)
        a->push_back(QuantumValue(b));
    return QuantumValue(a);
}

class AstConverter
{
public:
    QuantumValue node(const ASTNode *n)
    {
        if (!n)
            return QuantumValue();
        // a shared subtree (shared_ptr fields) stays one node, as in C++
        auto memo = seen_.find(n);
        if (memo != seen_.end())
            return memo->second;
        auto d = std::make_shared<Dict>();
        QuantumValue v(d);
        seen_.emplace(n, v);
        fill(*d, *n);
        (*d)["line"] = QuantumValue((double)n->line);
        return v;
    }

private:
    std::unordered_map<const ASTNode *, QuantumValue> seen_;

    QuantumValue list(const std::vector<ASTNodePtr> &v)
    {
        auto a = std::make_shared<Array>();
        for (const auto &x : v)
            a->push_back(node(x.get()));
        return QuantumValue(a);
    }

    void fill(Dict &d, const ASTNode &n)
    {
        std::visit(
            [&](const auto &x) {
                using T = std::decay_t<decltype(x)>;
                auto kind = [&](const char *k) { d["k"] = str(k); };
                if constexpr (std::is_same_v<T, NumberLiteral>)
                {
                    kind("Number");
                    d["value"] = QuantumValue(x.value);
                }
                else if constexpr (std::is_same_v<T, StringLiteral>)
                {
                    kind("String");
                    d["value"] = str(x.value);
                }
                else if constexpr (std::is_same_v<T, BoolLiteral>)
                {
                    kind("Bool");
                    d["value"] = QuantumValue(x.value);
                }
                else if constexpr (std::is_same_v<T, NilLiteral>)
                    kind("Nil");
                else if constexpr (std::is_same_v<T, Identifier>)
                {
                    kind("Ident");
                    d["name"] = str(x.name);
                }
                else if constexpr (std::is_same_v<T, BinaryExpr>)
                {
                    kind("Binary");
                    d["op"] = str(x.op);
                    d["left"] = node(x.left.get());
                    d["right"] = node(x.right.get());
                }
                else if constexpr (std::is_same_v<T, UnaryExpr>)
                {
                    kind("Unary");
                    d["op"] = str(x.op);
                    d["operand"] = node(x.operand.get());
                }
                else if constexpr (std::is_same_v<T, AssignExpr>)
                {
                    kind("Assign");
                    d["op"] = str(x.op);
                    d["target"] = node(x.target.get());
                    d["value"] = node(x.value.get());
                }
                else if constexpr (std::is_same_v<T, CallExpr>)
                {
                    kind("Call");
                    d["callee"] = node(x.callee.get());
                    d["args"] = list(x.args);
                }
                else if constexpr (std::is_same_v<T, IndexExpr>)
                {
                    kind("Index");
                    d["object"] = node(x.object.get());
                    d["index"] = node(x.index.get());
                }
                else if constexpr (std::is_same_v<T, SliceExpr>)
                {
                    kind("Slice");
                    d["object"] = node(x.object.get());
                    d["start"] = node(x.start.get());
                    d["stop"] = node(x.stop.get());
                    d["step"] = node(x.step.get());
                }
                else if constexpr (std::is_same_v<T, MemberExpr>)
                {
                    kind("Member");
                    d["object"] = node(x.object.get());
                    d["member"] = str(x.member);
                }
                else if constexpr (std::is_same_v<T, ArrayLiteral>)
                {
                    kind("Array");
                    d["elements"] = list(x.elements);
                }
                else if constexpr (std::is_same_v<T, DictLiteral>)
                {
                    kind("Dict");
                    auto pairs = std::make_shared<Array>();
                    for (const auto &kv : x.pairs)
                    {
                        auto p = std::make_shared<Array>();
                        p->push_back(node(kv.first.get()));
                        p->push_back(node(kv.second.get()));
                        pairs->push_back(QuantumValue(p));
                    }
                    d["pairs"] = QuantumValue(pairs);
                }
                else if constexpr (std::is_same_v<T, LambdaExpr>)
                {
                    kind("Lambda");
                    d["params"] = strings(x.params);
                    d["paramTypes"] = strings(x.paramTypes);
                    d["defaultArgs"] = list(x.defaultArgs);
                    d["returnType"] = str(x.returnType);
                    d["body"] = node(x.body.get());
                }
                else if constexpr (std::is_same_v<T, ListComp>)
                {
                    kind("ListComp");
                    d["expr"] = node(x.expr.get());
                    d["vars"] = strings(x.vars);
                    d["iterable"] = node(x.iterable.get());
                    d["condition"] = node(x.condition.get());
                }
                else if constexpr (std::is_same_v<T, TupleLiteral>)
                {
                    kind("Tuple");
                    d["elements"] = list(x.elements);
                }
                else if constexpr (std::is_same_v<T, VarDecl>)
                {
                    kind("VarDecl");
                    d["isConst"] = QuantumValue(x.isConst);
                    d["name"] = str(x.name);
                    d["initializer"] = node(x.initializer.get());
                    d["typeHint"] = str(x.typeHint);
                    d["isPointer"] = QuantumValue(x.isPointer);
                    d["isExported"] = QuantumValue(x.isExported);
                    d["cTyped"] = QuantumValue(x.cTyped);
                    d["perInstance"] = QuantumValue(x.perInstance);
                    d["isStatic"] = QuantumValue(x.isStatic);
                }
                else if constexpr (std::is_same_v<T, FunctionDecl>)
                {
                    kind("FunctionDecl");
                    d["name"] = str(x.name);
                    d["params"] = strings(x.params);
                    d["paramTypes"] = strings(x.paramTypes);
                    d["paramIsRef"] = bools(x.paramIsRef);
                    d["defaultArgs"] = list(x.defaultArgs);
                    d["returnType"] = str(x.returnType);
                    d["body"] = node(x.body.get());
                    d["isExported"] = QuantumValue(x.isExported);
                    d["cppParamTypes"] = strings(x.cppParamTypes);
                    d["cReturnType"] = str(x.cReturnType);
                    d["pythonScope"] = QuantumValue(x.pythonScope);
                }
                else if constexpr (std::is_same_v<T, GlobalStmt>)
                {
                    kind("Global");
                    d["names"] = strings(x.names);
                    d["isNonlocal"] = QuantumValue(x.isNonlocal);
                }
                else if constexpr (std::is_same_v<T, ReturnStmt>)
                {
                    kind("Return");
                    d["value"] = node(x.value.get());
                }
                else if constexpr (std::is_same_v<T, IfStmt>)
                {
                    kind("If");
                    d["condition"] = node(x.condition.get());
                    d["thenBranch"] = node(x.thenBranch.get());
                    d["elseBranch"] = node(x.elseBranch.get());
                }
                else if constexpr (std::is_same_v<T, WhileStmt>)
                {
                    kind("While");
                    d["condition"] = node(x.condition.get());
                    d["body"] = node(x.body.get());
                    d["post"] = node(x.post.get());
                }
                else if constexpr (std::is_same_v<T, ForStmt>)
                {
                    kind("For");
                    d["var"] = str(x.var);
                    d["var2"] = str(x.var2);
                    d["iterable"] = node(x.iterable.get());
                    d["body"] = node(x.body.get());
                }
                else if constexpr (std::is_same_v<T, BlockStmt>)
                {
                    kind("Block");
                    d["statements"] = list(x.statements);
                }
                else if constexpr (std::is_same_v<T, ExprStmt>)
                {
                    kind("ExprStmt");
                    d["expr"] = node(x.expr.get());
                }
                else if constexpr (std::is_same_v<T, PrintStmt>)
                {
                    kind("Print");
                    d["args"] = list(x.args);
                    d["newline"] = QuantumValue(x.newline);
                    d["sep"] = str(x.sep);
                    d["end"] = str(x.end);
                }
                else if constexpr (std::is_same_v<T, InputStmt>)
                {
                    kind("Input");
                    d["target"] = str(x.target);
                    d["prompt"] = node(x.prompt.get());
                    d["lvalueTarget"] = node(x.lvalueTarget.get());
                }
                else if constexpr (std::is_same_v<T, BreakStmt>)
                    kind("Break");
                else if constexpr (std::is_same_v<T, ContinueStmt>)
                    kind("Continue");
                else if constexpr (std::is_same_v<T, SuperExpr>)
                {
                    kind("Super");
                    d["method"] = str(x.method);
                }
                else if constexpr (std::is_same_v<T, RaiseStmt>)
                {
                    kind("Raise");
                    d["value"] = node(x.value.get());
                }
                else if constexpr (std::is_same_v<T, TryStmt>)
                {
                    kind("Try");
                    d["body"] = node(x.body.get());
                    auto handlers = std::make_shared<Array>();
                    for (const auto &h : x.handlers)
                    {
                        auto hd = std::make_shared<Dict>();
                        (*hd)["errorType"] = str(h.errorType);
                        (*hd)["alias"] = str(h.alias);
                        (*hd)["body"] = node(h.body.get());
                        handlers->push_back(QuantumValue(hd));
                    }
                    d["handlers"] = QuantumValue(handlers);
                    d["finallyBody"] = node(x.finallyBody.get());
                }
                else if constexpr (std::is_same_v<T, ImportStmt>)
                {
                    kind("Import");
                    d["module"] = str(x.module);
                    auto items = std::make_shared<Array>();
                    for (const auto &it : x.imports)
                    {
                        auto id = std::make_shared<Dict>();
                        (*id)["name"] = str(it.name);
                        (*id)["alias"] = str(it.alias);
                        items->push_back(QuantumValue(id));
                    }
                    d["imports"] = QuantumValue(items);
                }
                else if constexpr (std::is_same_v<T, ClassDecl>)
                {
                    kind("Class");
                    d["name"] = str(x.name);
                    d["base"] = str(x.base);
                    d["methods"] = list(x.methods);
                    d["staticMethods"] = list(x.staticMethods);
                    d["fields"] = list(x.fields);
                }
                else if constexpr (std::is_same_v<T, TernaryExpr>)
                {
                    kind("Ternary");
                    d["condition"] = node(x.condition.get());
                    d["thenExpr"] = node(x.thenExpr.get());
                    d["elseExpr"] = node(x.elseExpr.get());
                }
                else if constexpr (std::is_same_v<T, AddressOfExpr>)
                {
                    kind("AddressOf");
                    d["operand"] = node(x.operand.get());
                }
                else if constexpr (std::is_same_v<T, DerefExpr>)
                {
                    kind("Deref");
                    d["operand"] = node(x.operand.get());
                }
                else if constexpr (std::is_same_v<T, ArrowExpr>)
                {
                    kind("Arrow");
                    d["object"] = node(x.object.get());
                    d["member"] = str(x.member);
                }
                else if constexpr (std::is_same_v<T, NewExpr>)
                {
                    kind("New");
                    d["typeName"] = str(x.typeName);
                    d["args"] = list(x.args);
                    d["isArray"] = QuantumValue(x.isArray);
                    d["sizeExpr"] = node(x.sizeExpr.get());
                }
            },
            n.node);
    }
};
} // namespace

std::vector<uint8_t> compileSource(const std::string &source, const std::string &path, bool dialect,
                                   std::string *compiledOut)
{
    return unpack(callCompiler("hostedCompile", {str(source), str(path), QuantumValue(dialect)}), compiledOut);
}

std::vector<uint8_t> compileAst(const ASTNode &program)
{
    AstConverter conv;
    QuantumValue ast = conv.node(&program);
    return unpack(callCompiler("hostedCompileAst", {ast}), nullptr);
}

void checkSource(const std::string &source, const std::string &path, std::string *compiledOut)
{
    unpack(callCompiler("hostedCheck", {str(source), str(path)}), compiledOut);
}
} // namespace hosted
