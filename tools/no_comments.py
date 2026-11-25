#!/usr/bin/env python3
import io
import os
import re
import subprocess
import sys
import tokenize

C_LIKE = (".c", ".h", ".ts", ".js", ".mjs", ".cjs", ".json", ".jsonc")
HASH_NAMES = ("Makefile", "Dockerfile", ".gitignore", ".dockerignore", "_headers")
HASH_EXT = (".sh", ".yml", ".yaml", ".toml")
PRAGMA = re.compile(r"^(///\s*<|//\s*@ts-|/\*\s*eslint-|//\s*eslint-)")


def c_like(src, slash_slash=True):
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "\\":
            i += 2
        elif c in "\"'`":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            i = j + 1
        elif src.startswith("/*", i) or (slash_slash and src.startswith("//", i)):
            if not PRAGMA.match(src[i:i + 40]):
                yield src.count("\n", 0, i) + 1
            i = src.find("\n", i) if src.startswith("//", i) else src.find("*/", i + 2) + 2
            if i <= 0:
                return
        else:
            i += 1


def python(src):
    for t in tokenize.generate_tokens(io.StringIO(src).readline):
        if t.type == tokenize.COMMENT and not (t.start[0] == 1 and t.string.startswith("#!")):
            yield t.start[0]


def hashes(src):
    for k, line in enumerate(src.split("\n"), 1):
        s = line.strip()
        if s.startswith("#") and not (k == 1 and s.startswith("#!")):
            yield k
            continue
        m = re.search(r"\S(\s+#\s)", line)
        if m:
            prefix = line[:m.start(1)]
            if prefix.count('"') % 2 == 0 and prefix.count("'") % 2 == 0:
                yield k


def html(src):
    for m in re.finditer(r"<!--", src):
        yield src.count("\n", 0, m.start()) + 1


def scan(path):
    name = os.path.basename(path)
    try:
        src = open(path, encoding="utf-8").read()
    except (UnicodeDecodeError, FileNotFoundError, IsADirectoryError):
        return []
    if name.endswith(C_LIKE):
        return list(c_like(src))
    if name.endswith(".css"):
        return list(c_like(src, slash_slash=False))
    if name.endswith((".py", ".fam")):
        return list(python(src))
    if name.endswith(".html"):
        return list(html(src))
    if name in HASH_NAMES or name.endswith(HASH_EXT):
        return list(hashes(src))
    return []


def tracked(roots):
    out = subprocess.run(["git", "ls-files", "--", *roots], capture_output=True, text=True, check=True)
    return [p for p in out.stdout.splitlines() if os.path.isfile(p)]


def main(argv):
    bad = 0
    for path in tracked(argv or ["."]):
        for line in scan(path):
            print(f"{path}:{line}: comment")
            bad += 1
    if bad:
        print(f"no_comments: {bad} comment(s); this repo has a no-comment rule", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
