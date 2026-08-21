"""Rewrite non-ASCII characters in a C++ source file as \\uXXXX escapes.

MSVC, without an explicit /utf-8 flag or a BOM, reinterprets source files
through the local ANSI code page, which turns literal Turkish characters in
wide-string literals into mojibake at compile time. Running this over a file
keeps the sources pure ASCII so the compiled UTF-16 strings are always right.

Usage: python tools/escape_nonascii.py <file> [<file> ...]
"""

import sys

BS = chr(92)  # backslash, spelled out to keep this file's own escapes simple


def escape_file(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()

    out = []
    changed = 0
    for ch in text:
        cp = ord(ch)
        if cp > 127:
            out.append(BS + "u" + format(cp, "04X"))
            changed += 1
        else:
            out.append(ch)

    if changed:
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write("".join(out))
    return changed


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    for path in sys.argv[1:]:
        n = escape_file(path)
        print("{}: escaped {} character(s)".format(path, n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
