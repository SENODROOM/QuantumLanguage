#!/usr/bin/env bash
# Rebuilds the compiler qrun embeds (selfhost/compiler.qbc) from the sources
# in selfhost/, with the compiler qrun currently embeds, until a rebuild
# reproduces itself byte for byte (the bootstrap fixed point).
#
#   selfhost/bootstrap.sh [rounds]        (run from the compiler/ folder)
#
# One round compiles selfhost/compiler.sa with ./qrun.exe and rebuilds qrun
# around the result. A change to the code generator needs two rounds before
# it shows in the compiler's own bytecode; the script stops as soon as a
# round changes nothing.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="/c/msys64/ucrt64/bin:$PATH"
rounds="${1:-4}"
for ((i = 1; i <= rounds; i++)); do
    ./qrun.exe --emit-qbc selfhost/compiler.sa selfhost/compiler.qbc.next
    if cmp -s selfhost/compiler.qbc selfhost/compiler.qbc.next; then
        rm -f selfhost/compiler.qbc.next
        echo "round $i: fixed point (selfhost/compiler.qbc reproduces itself)"
        exit 0
    fi
    mv -f selfhost/compiler.qbc.next selfhost/compiler.qbc
    (cd build && mingw32-make qrun -j8 > /dev/null)
    cp -f build/qrun.exe qrun.exe
    echo "round $i: rebuilt qrun with the new compiler"
done
echo "no fixed point after $rounds rounds" >&2
exit 1
