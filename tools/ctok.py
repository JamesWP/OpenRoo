"""Shared C/C++ lexing for the comment tools: split source into code,
comments and literals without being fooled by `//` inside a string."""
import re

TOKEN = re.compile(r'''
    (?P<block>/\*.*?\*/)
  | (?P<line>//(?:\\\n|[^\n])*)
  | (?P<str>"(?:\\.|[^"\\\n])*")
  | (?P<chr>'(?:\\.|[^'\\\n])*')
''', re.S | re.X)


def comments(src):
    """[(start, end, text)] for every comment in src."""
    return [(m.start(), m.end(), m.group(0)) for m in TOKEN.finditer(src)
            if m.lastgroup in ("block", "line")]


def strip(src):
    """src with every comment replaced by one space (newlines kept)."""
    out, pos = [], 0
    for a, b, text in comments(src):
        out.append(src[pos:a])
        out.append(" " + "\n" * text.count("\n"))
        pos = b
    out.append(src[pos:])
    return "".join(out)


CODE_TOKEN = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|\w+|\S')


def code_tokens(src):
    """The token stream of src with comments removed; whitespace and line
    breaks do not count, except that a preprocessor line ends at its
    newline, so line ends inside directives are kept as a token."""
    toks = []
    for line in strip(src).split("\n"):
        t = CODE_TOKEN.findall(line)
        toks += t
        if t and t[0] == "#" or (toks and toks[-1] == "\\"):
            toks.append("<eol>")
    return toks
