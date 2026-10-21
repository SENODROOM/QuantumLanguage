# Embeds the self-hosted compiler's bytecode (selfhost/compiler.qbc) into a
# C++ source file defining kHostedCompilerBytecode / kHostedCompilerBytecodeSize
# (read by src/HostedCompiler.cpp).
#   cmake -DIN=<compiler.qbc> -DOUT=<generated .cpp> -P EmbedBytecode.cmake
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" digits)
math(EXPR size "${digits} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){24})" "\\1\n" bytes "${bytes}")
set(out "// Generated from selfhost/compiler.qbc by cmake/EmbedBytecode.cmake. Do not edit.\n")
string(APPEND out "#include <cstddef>\n\n")
string(APPEND out "extern const unsigned char kHostedCompilerBytecode[] = {\n${bytes}\n};\n")
string(APPEND out "extern const size_t kHostedCompilerBytecodeSize = ${size};\n")
# rewrite only on change, so an unchanged compiler does not trigger a rebuild
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL out)
    file(WRITE "${OUT}" "${out}")
endif()
