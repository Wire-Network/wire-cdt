#!/bin/sh
# S5 gate: static lint of the packaging verifier suite.
set -e
root=$(cd "$(dirname "$0")" && pwd)
fail() { echo "S5 FAIL: $1"; exit 1; }
for s in verify-tgz.sh verify-deb.sh verify-rpm.sh verify-scripts.sh; do
    sh -n "$root/$s" || fail "sh -n $s"
done
# verify-all.sh is the one bash member of the suite ([[ ]] / arrays), so it is
# parsed by bash: `sh -n` would accept it while checking a grammar it is not
# written in.
bash -n "$root/verify-all.sh" || fail "bash -n verify-all.sh"
if command -v shellcheck >/dev/null 2>&1; then
    shellcheck "$root"/verify-*.sh || echo "S5 note: shellcheck findings above are advisory"
fi
echo "S5 PASS"
