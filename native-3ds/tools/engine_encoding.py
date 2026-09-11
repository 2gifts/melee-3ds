"""Preserve Melee's Shift-JIS execution bytes with Clang's UTF-8 frontend.

Apply after preprocessing so literals in headers/macros are included. Escape
only narrow C string/character tokens, never source comments or identifiers.
Three-digit octal escapes cannot absorb following hexadecimal/digit text.
"""
import re

TOKENS=re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|(?:u8|[LuU])?(?:"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\')')

def shift_jis_literals(source):
    def convert(match):
        token=match[0]
        if token[0] not in ('"',"'") or token.isascii():return token
        # Windows Shift-JIS maps the full-width hyphen used by this source
        # (U+FF0D) to 81 7C, as in the original DOL.
        return ''.join(c if ord(c)<128 else ''.join('\\%03o'%b for b in c.encode('cp932')) for c in token)
    return TOKENS.sub(convert,source)
