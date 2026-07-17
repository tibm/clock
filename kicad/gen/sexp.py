"""Minimal, faithful S-expression reader/writer for KiCad files.

Preserves the distinction between bare symbols/atoms and quoted strings so we
can round-trip KiCad symbol libraries and emit valid .kicad_sch files.
"""
from __future__ import annotations


class Atom(str):
    """A bare token: a symbol tag or an unquoted number/keyword."""
    __slots__ = ()


class QStr(str):
    """A double-quoted string value."""
    __slots__ = ()


class Node(list):
    """An S-expression list. node[0] is the tag (Atom)."""
    __slots__ = ()

    @property
    def tag(self):
        return self[0] if self else None

    def find(self, tag):
        for c in self[1:]:
            if isinstance(c, Node) and c.tag == tag:
                return c
        return None

    def findall(self, tag):
        return [c for c in self[1:] if isinstance(c, Node) and c.tag == tag]


def parse(text: str) -> Node:
    """Parse the first top-level S-expression in text."""
    pos = 0
    n = len(text)

    def skip_ws():
        nonlocal pos
        while pos < n and text[pos] in " \t\r\n":
            pos += 1

    def read_node():
        nonlocal pos
        assert text[pos] == "("
        pos += 1
        node = Node()
        while True:
            skip_ws()
            if pos >= n:
                raise ValueError("unexpected EOF in list")
            c = text[pos]
            if c == ")":
                pos += 1
                return node
            elif c == "(":
                node.append(read_node())
            elif c == '"':
                node.append(read_string())
            else:
                node.append(read_atom())

    def read_string():
        nonlocal pos
        assert text[pos] == '"'
        pos += 1
        out = []
        while pos < n:
            c = text[pos]
            if c == "\\":
                nxt = text[pos + 1]
                mapping = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "r": "\r"}
                out.append(mapping.get(nxt, nxt))
                pos += 2
            elif c == '"':
                pos += 1
                return QStr("".join(out))
            else:
                out.append(c)
                pos += 1
        raise ValueError("unterminated string")

    def read_atom():
        nonlocal pos
        start = pos
        while pos < n and text[pos] not in " \t\r\n()\"":
            pos += 1
        return Atom(text[start:pos])

    skip_ws()
    return read_node()


def _esc(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def dumps(node, indent=0) -> str:
    """Serialize a Node to KiCad-style pretty S-expression text."""
    pad = "\t" * indent
    if isinstance(node, Node):
        # Decide inline vs multiline: inline if no child is a Node.
        has_child_node = any(isinstance(c, Node) for c in node)
        if not has_child_node:
            parts = [dumps(c) for c in node]
            return pad + "(" + " ".join(parts) + ")"
        out = [pad + "(" + (str(node[0]) if node else "")]
        for c in node[1:]:
            if isinstance(c, Node):
                out.append("\n" + dumps(c, indent + 1))
            else:
                out.append(" " + dumps(c))
        out.append("\n" + pad + ")")
        return "".join(out)
    elif isinstance(node, QStr):
        return '"' + _esc(node) + '"'
    else:  # Atom or plain
        s = str(node)
        return s if s != "" else '""'
