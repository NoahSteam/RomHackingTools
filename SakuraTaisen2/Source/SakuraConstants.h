#pragma once

#define OPTIMIZE_INSERTION_DEBUGGING 0
#define USE_SINGLE_BYTE_LOOKUPS 1
#define USE_TREKKIS_MINIGAME_DATA 1
#define FIX_TIMING_DATA 0
#define FIX_EVT_FILES 0
#define USE_LARGE_BATTLE_MENU 0
#define FIX_SLG_FONT_DRAWING_SIZE 0

const unsigned char MaxCharsPerLine = 27;
const unsigned char OutTileSpacingX = 8;
const unsigned char OutTileSpacingY = 12;
const unsigned long MaxTBLFileSize = 0x3f7f7;
const unsigned long MaxMESFileSize = 0x1A800;
const unsigned long MaxWKLFileSize = 0x82E00;
const char          MaxLines = 4;
const unsigned char CharTimingStartID = 0x2E;
bool GIsDisc2 = false;

const string PatchedPaletteName("PatchedPalette.BIN");
const string PatchedKNJName("PatchedKNJ.BIN");
const string Disc1("\\Disc1");
const string Disc2("\\Disc2");
const string NewLineWord("<br>");
const string LipsWord("<LIPS>");
const string SpaceWord("<sp>");
const string BMPExtension(".bmp");
const string SlgFontFileSuffix("_SLG");
const unsigned short SpecialDialogIndicator = 0xC351;
