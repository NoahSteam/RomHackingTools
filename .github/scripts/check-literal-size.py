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
    """Yield (line, byte length) for each raw string literal, scanning linearly as a C++ lexer would.

    Comments, ordinary string and character literals, identifiers and numbers are skipped first, so `R"x(` in a
    comment or in an ordinary string is not taken for an opener (a documentation comment that shows one is valid C++).
    A real opener with no closing delimiter yields (line, None): the rest of the file cannot be scanned, and stopping
    silently let any long raw string after it pass (A12-57).
    """
    i = 0
    n = len(text)
    raw_prefixes = ('R', 'uR', 'UR', 'LR', 'u8R')
    plain_prefixes = ('u', 'U', 'L', 'u8')

    def line_of(pos):
        return text.count('\n', 0, pos) + 1

    def skip_quoted(j, quote):
        # j is just past the opening quote; returns the index past the closing one (or the end of the line)
        while j < n:
            c = text[j]
            if c == '\\':
                j += 2
                continue
            if c == quote:
                return j + 1
            if c == '\n':
                return j   # an unterminated ordinary literal does not compile; leave it to the compiler
            j += 1
        return n

    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            # a line comment, continued past a newline by a trailing backslash
            j = i + 2
            while j < n:
                if text[j] == '\n' and text[j - 1] != '\\':
                    break
                j += 1
            i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif c == '"' or c == "'":
            i = skip_quoted(i + 1, c)
        elif c.isdigit() or (c == '.' and i + 1 < n and text[i + 1].isdigit()):
            # a pp-number, digit separators (1'000) and exponent signs included
            j = i + 1
            while j < n:
                d = text[j]
                if d.isalnum() or d in '._':
                    j += 1
                elif d == "'" and j + 1 < n and text[j + 1].isalnum():
                    j += 2
                elif d in '+-' and text[j - 1] in 'eEpP':
                    j += 1
                else:
                    break
            i = j
        elif c.isalpha() or c == '_':
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] == '_'):
                j += 1
            word = text[i:j]
            if j < n and text[j] == '"' and word in raw_prefixes:
                open_paren = text.find('(', j + 1, j + 1 + 17)
                delim = text[j + 1:open_paren] if open_paren >= 0 else None
                if delim is None or any(ch in delim for ch in ' \\)\t\n"'):
                    i = skip_quoted(j + 1, '"')   # not a valid raw string: the compiler will say so
                    continue
                close = text.find(')' + delim + '"', open_paren + 1)
                if close < 0:
                    yield line_of(i), None
                    return
                body = text[open_paren + 1:close]
                yield line_of(i), len(body.encode('utf-8')) + body.count('\n')
                i = close + len(delim) + 2
            elif j < n and text[j] in '"\'' and word in plain_prefixes:
                i = skip_quoted(j + 1, text[j])
            else:
                i = j
        else:
            i += 1


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
                    if size is None:
                        print(f'{path}:{line}: raw string opener with no closing delimiter; the rest of the file was not checked')
                        return 2
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
