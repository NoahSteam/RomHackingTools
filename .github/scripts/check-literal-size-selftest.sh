#!/bin/bash
# Self-test of check-literal-size.py: each case is a small tree with the exit code the checker must give.
#   0 = passes, 1 = a raw string too long, 2 = the scan could not be trusted (missing or empty directory,
#   unterminated raw string).
# Run from the repository root: .github/scripts/check-literal-size-selftest.sh
set -u
here=$(cd "$(dirname "$0")" && pwd)
checker="$here/check-literal-size.py"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
long=$(python3 -c "print('a' * 20000)")

mk() { mkdir -p "$work/$1"; cat > "$work/$1/a.cpp"; }

# Comments and ordinary literals that spell an opener are not raw strings (valid C++; PR #74's review).
mk comment <<'EOF'
// Example raw-string opener: R"demo(
int main() { return 0; }
EOF
mk block <<'EOF'
/* see R"x( in the docs */
int x;
EOF
mk instring <<'EOF'
const char* s = "R\"x(";
char c = '"';
EOF
mk short <<'EOF'
const char* a = R"(short)";
const wchar_t* b = LR"z(x)z";
EOF
# A real opener with no closing delimiter stops the scan, so it is an error (A12-57).
mk unterm <<'EOF'
const char* s = R"x(never closed
EOF
printf 'const char* s = R"x(%s)x";\n' "$long" > "$work/long.cpp"; mkdir -p "$work/long"; mv "$work/long.cpp" "$work/long/a.cpp"
# Digit separators must not be read as character literals that hide what follows.
mkdir -p "$work/digits"; printf "int n = 1'000'000;\nconst char* s = R\"x(%s)x\";\n" "$long" > "$work/digits/a.cpp"
mkdir -p "$work/u8"; printf 'const char* s = u8R"q(%s)q";\n' "$long" > "$work/u8/a.cpp"
# A file that is not valid UTF-8 is still read (A12-54).
mkdir -p "$work/latin"; printf '// caf\xe9\nconst char* s = R"x(%s)x";\n' "$long" > "$work/latin/a.cpp"
mkdir -p "$work/empty"

fail=0
expect() { # case, expected exit code
    python3 -I "$checker" "$work/$1" > "$work/$1.out" 2>&1
    local rc=$?
    if [ "$rc" -ne "$2" ]; then
        echo "FAIL $1: exit $rc, expected $2"; sed 's/^/  /' "$work/$1.out"; fail=1
    else
        echo "ok   $1 (exit $rc)"
    fi
}
expect comment 0
expect block 0
expect instring 0
expect short 0
expect unterm 2
expect long 1
expect digits 1
expect u8 1
expect latin 1
expect empty 2
expect missing 2
exit $fail
