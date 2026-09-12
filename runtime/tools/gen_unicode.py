#!/usr/bin/env python3
"""Generates runtime/src/rune_unicode_data.c from Python's Unicode database.

Three tables, each sorted so the runtime can binary-search them:

    combining classes   the non-zero ones only; everything else is 0
    decompositions      canonical only, at most two code points
    compositions        the reverse, minus the composition exclusions
    case folds          simple (one code point to one) only

Hangul is not in any of them: its composition and decomposition are
arithmetic, and the runtime does that directly.

    python3 runtime/tools/gen_unicode.py > runtime/src/rune_unicode_data.c
"""

import sys
import unicodedata

HANGUL_BASE, HANGUL_COUNT = 0xAC00, 11172


def canonical(cp):
    d = unicodedata.decomposition(chr(cp))
    if not d or d.startswith("<"):
        return None
    parts = [int(x, 16) for x in d.split()]
    return parts if len(parts) <= 2 else None


def main():
    ccc = [(cp, unicodedata.combining(chr(cp)))
           for cp in range(0x110000) if unicodedata.combining(chr(cp))]

    decomp = []
    for cp in range(0x110000):
        if HANGUL_BASE <= cp < HANGUL_BASE + HANGUL_COUNT:
            continue
        parts = canonical(cp)
        if parts:
            decomp.append((cp, parts[0], parts[1] if len(parts) > 1 else 0))

    # A pair composes only if the decomposition is a pair, the first element
    # has combining class 0, and the character is not a composition exclusion.
    # Python has no exclusion list, but the singletons and non-starters it
    # would exclude are exactly the cases filtered here; the remaining
    # script-specific exclusions are listed below.
    EXCLUSIONS = {
        0x0958, 0x0959, 0x095A, 0x095B, 0x095C, 0x095D, 0x095E, 0x095F,
        0x09DC, 0x09DD, 0x09DF, 0x0A33, 0x0A36, 0x0A59, 0x0A5A, 0x0A5B,
        0x0A5E, 0x0B5C, 0x0B5D, 0x0F43, 0x0F4D, 0x0F52, 0x0F57, 0x0F5C,
        0x0F69, 0x0F76, 0x0F78, 0x0F93, 0x0F9D, 0x0FA2, 0x0FA7, 0x0FAC,
        0x0FB9, 0x2ADC, 0xFB1D, 0xFB1F, 0xFB2A, 0xFB2B, 0xFB2C, 0xFB2D,
        0xFB2E, 0xFB2F, 0xFB30, 0xFB31, 0xFB32, 0xFB33, 0xFB34, 0xFB35,
        0xFB36, 0xFB38, 0xFB39, 0xFB3A, 0xFB3B, 0xFB3C, 0xFB3E, 0xFB40,
        0xFB41, 0xFB43, 0xFB44, 0xFB46, 0xFB47, 0xFB48, 0xFB49, 0xFB4A,
        0xFB4B, 0xFB4C, 0xFB4D, 0xFB4E, 0x1D15E, 0x1D15F, 0x1D160, 0x1D161,
        0x1D162, 0x1D163, 0x1D164, 0x1D1BB, 0x1D1BC, 0x1D1BD, 0x1D1BE,
        0x1D1BF, 0x1D1C0,
    }
    comp = []
    for cp, a, b in decomp:
        if b == 0:                       # a singleton never composes
            continue
        if cp in EXCLUSIONS:
            continue
        if unicodedata.combining(chr(a)):  # the first must be a starter
            continue
        comp.append((a, b, cp))
    comp.sort()

    folds = []
    for cp in range(0x110000):
        low = chr(cp).lower()
        if len(low) == 1 and low != chr(cp):
            folds.append((cp, ord(low)))

    out = sys.stdout
    out.write("/*===- rune_unicode_data.c - generated, do not edit -------===*\\\n")
    out.write("|*\n")
    out.write("|* Produced by runtime/tools/gen_unicode.py from Unicode %s.\n"
              % unicodedata.unidata_version)
    out.write("|* Regenerate it rather than editing it by hand.\n")
    out.write("\\*========================================================*/\n\n")
    out.write('#include "rune_runtime.h"\n\n')

    out.write("const uint32_t rune_ucd_ccc[][2] = {\n")
    for cp, c in ccc:
        out.write("  {0x%04X, %d},\n" % (cp, c))
    out.write("};\nconst uint32_t rune_ucd_ccc_count = %d;\n\n" % len(ccc))

    out.write("const uint32_t rune_ucd_decomp[][3] = {\n")
    for cp, a, b in decomp:
        out.write("  {0x%04X, 0x%04X, 0x%04X},\n" % (cp, a, b))
    out.write("};\nconst uint32_t rune_ucd_decomp_count = %d;\n\n" % len(decomp))

    out.write("const uint32_t rune_ucd_comp[][3] = {\n")
    for a, b, cp in comp:
        out.write("  {0x%04X, 0x%04X, 0x%04X},\n" % (a, b, cp))
    out.write("};\nconst uint32_t rune_ucd_comp_count = %d;\n\n" % len(comp))

    out.write("const uint32_t rune_ucd_fold[][2] = {\n")
    for cp, low in folds:
        out.write("  {0x%04X, 0x%04X},\n" % (cp, low))
    out.write("};\nconst uint32_t rune_ucd_fold_count = %d;\n" % len(folds))


if __name__ == "__main__":
    main()
