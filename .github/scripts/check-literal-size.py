#!/usr/bin/env python3
"""Fail when a C++ raw string literal is too long for MSVC.

MSVC rejects a single string literal longer than 16,380 bytes (error C2026), while gcc and
clang accept any length, so a long raw string builds on Linux and breaks the Windows build.
Adjacent literals are joined by the compiler, so a long text must be split into pieces.
This checks every raw string (R"delim( ... )delim") in the given directories.

A directory that does not exist is an error (A12-46: the vendored-tree job checked out only part of the tree, the
directories it was told to scan were not there, and the step printed "ok" having read nothing), and so is a run that
reads no C++ file at all.

Usage: check-literal-size.py DIR [DIR ...]
"""
import os
import sys

LIMIT = 16000   # a margin under MSVC's 16,380 (CRLF checkouts lengthen a piece by its line count)


def raw_strings(text):
    """Yield (line, byte length) for each raw string literal, scanning linearly."""
    i = 0
    n = len(text)
    while True:
        i = text.find('R"', i)
        if i < 0:
            return
        # an identifier character before R makes it something else (e.g. uR" is still raw; FOOR" is not)
        prev = text[i - 1] if i > 0 else ' '
        if prev.isalnum() and prev not in 'uUL8' or prev == '_':
            i += 2
            continue
        open_paren = text.find('(', i + 2, i + 2 + 17)
        if open_paren < 0:
            i += 2
            continue
        delim = text[i + 2:open_paren]
        if any(c in delim for c in ' \\)\t\n"'):
            i += 2
            continue
        close = text.find(')' + delim + '"', open_paren + 1)
        if close < 0:
            return
        body = text[open_paren + 1:close]
        yield text.count('\n', 0, i) + 1, len(body.encode('utf-8')) + body.count('\n')
        i = close + len(delim) + 2


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    bad = 0
    files = 0
    strings = 0
    for root in argv[1:]:
        if not os.path.isdir(root):
            print(f'error: {root} is not a directory (is it missing from the checkout?)')
            return 2
        root_files = 0
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d != 'third_party']
            for name in filenames:
                if not name.endswith(('.cpp', '.cc', '.h', '.hpp')):
                    continue
                path = os.path.join(dirpath, name)
                # A file that is not valid UTF-8 (a Latin-1 or Shift-JIS comment) is still read, with the bad bytes replaced:
                # skipping it would let a long raw string in it pass (A12-54). A file that cannot be opened is an error.
                try:
                    with open(path, encoding='utf-8', errors='replace') as f:
                        text = f.read()
                except OSError as e:
                    print(f'error: cannot read {path}: {e}')
                    return 2
                files += 1
                root_files += 1
                for line, size in raw_strings(text):
                    strings += 1
                    if size > LIMIT:
                        print(f'{path}:{line}: raw string of {size} bytes (with CRLF) exceeds {LIMIT}; '
                              'split it into adjacent raw strings (MSVC C2026 limit is 16,380)')
                        bad += 1
        # Each directory must hold a C++ file: one that exists but is empty (a partial checkout) read nothing (A12-54).
        if root_files == 0:
            print(f'error: no C++ file was read under {root}')
            return 2
    if files == 0:
        print('error: no C++ file was read')
        return 2
    print(f'raw string sizes: {"ok" if not bad else f"{bad} too long"} ({strings} raw strings in {files} files)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
