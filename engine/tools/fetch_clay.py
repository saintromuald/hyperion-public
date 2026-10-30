"""Fetch Clay's single header into engine/third_party.

The front end (docs/front-end.md) lays its elements out with Clay, a layout library in one
C header: rows, columns, padding, gaps, alignment and sizing, much as CSS's flexbox does,
handing back rectangles that the front end draws itself. The header lands in
engine/third_party/clay-<version>/, which is gitignored: the repository carries the version
and the checksum, not the file. Clay is under the zlib licence.

    python tools/fetch_clay.py

The download is the header at the release tag, checked against the SHA-256 it had when the
version was pinned. A file that is already there is left alone, but for Hyperion's one change,
which patch() makes and each build asks for: a line of Chinese or Japanese, which has no
spaces, may break after any of its characters, where Clay breaks only at a space.
"""

import hashlib
import sys
import urllib.request
from pathlib import Path

NAME = "Clay"
VERSION = "0.14"
URL = f"https://raw.githubusercontent.com/nicbarker/clay/v{VERSION}/clay.h"
SHA256 = "c97241cc423af3fa11267978adce9cbb46274a2ad0709a5d4b2b1092dc27599d"

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"clay-{VERSION}"
CHECK = DEST / "clay.h"

# Clay's word splitting (Clay__MeasureTextCached): an ideograph, kana or full-width mark, three
# bytes of UTF-8 from U+3000 to U+9FFF or U+FF00 to U+FFEF, is a word of its own
PATCH_MARK = "Hyperion: CJK"
PATCH_AT = """        char current = text->chars[end];
        if (current == ' ' || current == '\\n') {"""
PATCH_NEW = """        char current = text->chars[end];
        // Hyperion: CJK, a line may break after an ideograph, kana or full-width mark
        unsigned char lead = (unsigned char)current;
        if (((lead >= 0xE3 && lead <= 0xE9) || (lead == 0xEF && end + 1 < text->length && (unsigned char)text->chars[end + 1] >= 0xBC)) && end + 3 <= text->length) {
            int32_t length = end + 3 - start;
            Clay_Dimensions dimensions = Clay__MeasureText(CLAY__INIT(Clay_StringSlice) { .length = length, .chars = &text->chars[start], .baseChars = text->chars }, config, context->measureTextUserData);
            measured->minWidth = CLAY__MAX(dimensions.width, measured->minWidth);
            measuredHeight = CLAY__MAX(measuredHeight, dimensions.height);
            previousWord = Clay__AddMeasuredWord(CLAY__INIT(Clay__MeasuredWord) { .startOffset = start, .length = length, .width = dimensions.width, .next = -1 }, previousWord);
            lineWidth += dimensions.width;
            end += 3;
            start = end;
            continue;
        }
        if (current == ' ' || current == '\\n') {"""


def patch() -> None:
    """Hyperion's change to clay.h, made once."""
    text = CHECK.read_text(encoding="utf-8")
    if PATCH_MARK in text:
        return
    if text.count(PATCH_AT) != 1:
        sys.exit(f"{CHECK}: not the Clay {VERSION} this patch is for")
    CHECK.write_text(text.replace(PATCH_AT, PATCH_NEW), encoding="utf-8", newline="")
    print(f"{NAME} {VERSION}: CJK line breaks added")


def main() -> int:
    if CHECK.exists():
        print(f"{NAME} {VERSION} already in {DEST}")
        patch()
        return 0
    print(f"downloading {URL}")
    with urllib.request.urlopen(URL) as r:
        data = r.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        print(f"checksum mismatch: {digest}, want {SHA256}")
        return 1
    DEST.mkdir(parents=True, exist_ok=True)
    CHECK.write_bytes(data)
    if not CHECK.exists():
        print(f"unexpected layout: no {CHECK}")
        return 1
    patch()
    print(f"{NAME} {VERSION} -> {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
