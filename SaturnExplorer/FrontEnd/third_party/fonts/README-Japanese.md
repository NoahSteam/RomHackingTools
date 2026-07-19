# Japanese font (IpaGothicJP.h)

`IpaGothicJP.h` is IPAGothic (`ipag.ttf`, IPA Font License — see
`LICENSE-IPAfont.txt`) **subset** to Dear ImGui's Japanese glyph set and embedded
as base85-compressed TTF, so the web single-file build renders Japanese with no
external fetch. It is merged into the UI font in `Theme.cpp` and used by the Hex
Editor's Shift-JIS text pane.

Regenerate (only if the glyph set or source font changes):

```sh
# 1. Dump ImGui's Japanese codepoints (ImFontAtlas::GetGlyphRangesJapanese) -> jp_unicodes.txt
# 2. Subset the source font to just those glyphs:
python3 -m fontTools.subset /usr/share/fonts/opentype/ipafont-gothic/ipag.ttf \
    --unicodes-file=jp_unicodes.txt --output-file=ipag_jp.ttf \
    --no-hinting --desubroutinize --drop-tables+=DSIG
# 3. Compress to a base85 C header (imgui misc/fonts/binary_to_compressed_c):
./binary_to_compressed_c -base85 ipag_jp.ttf IpaGothicJP > IpaGothicJP.h
```

`../../src/Debug/ShiftJisTable.h` (the Shift-JIS -> Unicode map) is generated from
Python's `shift_jis` codec — see the header comment.
