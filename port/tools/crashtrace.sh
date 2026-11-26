#!/bin/zsh
# Run the native build under lldb; on a crash print the stub log and backtrace.
#   port/tools/crashtrace.sh [seconds]   (needs NFSMW_DATA=<game dir>)
cd "${0:A:h}/../.."
export NFSMW_DATA=${NFSMW_DATA:-$HOME/Games/NFSMW}
APP=build/port/NFSMW.app/Contents/MacOS/NFSMW
out=$(timeout ${1:-30} lldb --batch -o run -k "bt 30" -k quit $APP 2>&1)
echo "--- stubs hit (in order) ---"
print -r -- "$out" | grep '^\[stub\]'
echo "--- result ---"
print -r -- "$out" | grep -E 'stop reason|exited with status|^\s+(\* )?frame #' | cut -c1-200
