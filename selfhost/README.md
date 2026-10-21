# The Quantum compiler, written in Quantum

This folder is the compiler for Quantum's multi-syntax family — `.sa`, and
`.py` / `.js` / `.rb`, plus C/C++ outside the typed front-end's subset — and
the code generator for every language qrun runs. qrun carries it as bytecode
(`compiler.qbc`, embedded at build time by `cmake/EmbedBytecode.cmake`) and
calls it through `src/HostedCompiler.cpp`; the C++ side keeps the VM (the
runtime that executes bytecode) and the C/C++ (`src/cpp/`) and Java
(`src/java/`) front-ends, whose syntax trees it hands to `codegen.sa`.

## The pipeline

| file | stage |
|---|---|
| `dialect.sa` | routes a source through the passes its extension calls for |
| `dialect_cpp.sa` | C++ project structure: `#include "x.h"` spliced in, namespaces lowered, out-of-class members moved into their class |
| `dialect_ruby.sa`, `dialect_ruby_main.sa` | Ruby syntax → brace syntax (strict for `.rb`; for `.sa` only where unambiguous) |
| `dialect_scan.sa` | string searching, `<regex>` access and the bracket/quote-aware scanners the passes share |
| `lexer.sa` | tokens (Python INDENT/DEDENT included) |
| `parser.sa`, `parser_stmt2.sa`, `parser_expr.sa` | the syntax tree, as the dicts `ast.sa` builds |
| `modules.sa` | `# @use` modules inlined ahead of the program, on-disk `import`s resolved |
| `typecheck.sa` | type-mismatch warnings (stderr only) |
| `codegen.sa` | bytecode chunks |
| `serializer.sa` | the bytecode file format (`src/Serializer.cpp` reads it) |
| `compiler.sa` | the library: `hostedCompile`, `hostedCompileAst`, `hostedCheck` — the entry points qrun calls |
| `qc.sa` | a command-line front end: `qrun selfhost/qc.sa [--raw] <source> <out.qbc>` |

Errors are raised as `{"kind", "message", "line", "col"}` (`ParseError`,
`LexError`, or `Fatal`); `src/HostedCompiler.cpp` turns them back into the
exceptions the CLI reports.

## Changing the compiler

`compiler.qbc` is the compiler compiled by itself. After editing any `.sa`
file here, rebuild it with the compiler qrun currently embeds:

```
selfhost/bootstrap.sh        # from compiler/: recompile, rebuild qrun, repeat until it reproduces itself
```

A change to the code generator takes two rounds to reach the compiler's own
bytecode (the first round's compiler was itself compiled by the old one); the
script stops at the first round that changes nothing — the fixed point.
Keep a copy of a working `compiler.qbc` (and its qrun) before a risky
change: a compiler that miscompiles itself has to be rolled back by hand.

**These sources go through the dialect passes too** (qrun compiles them as
`.sa` files), so they must be text the passes leave alone. Several Ruby
rewrites are plain regular expressions over the whole line, string literals
included: write `"." + "split"`, not `".split"`, and `"Array.new" + "("`, not
`"Array.new("`; avoid `"#{"` in strings, `.split()` with empty parentheses,
a variable named `input`, `?:` ternaries, and lines ending in `do`. Avoid
calling a method on a dict whose keys come from the program
(`d.keys()`, `d.has(k)`): a key of that name shadows the method — use
`Object.keys(d)` and `k in d`.

## Tools

| tool | prints |
|---|---|
| `tools/lexdump.sa <file>` | the tokens, one per line (type, line, col, value) |
| `tools/astdump.sa <file>` | the syntax tree, one node per line |
| `tools/dialectdump.sa <file>` | the source after the dialect passes |

Run one with `qrun selfhost/tools/<tool>.sa <file>`, or compile it once
(`qrun --emit-qbc …`) and run the bytecode with `qrun --exec`, which also
lifts the instruction budget scripts otherwise run under.
