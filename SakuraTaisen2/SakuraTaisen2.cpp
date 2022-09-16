/*******************************************************************************
SakuraTaisen2.cpp - Tools for modifying Sakura Taisen 2

(c) Copyright 2021 NoahSteam

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*******************************************************************************/

#include <stdio.h>
#include <windows.h>
#include <vector>
#include <string>
#include <list>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <utility>
#include <assert.h>

#include "..\Utils\Utils.h"
#include "..\Utils\decompress_rtns.c"

using std::vector;
using std::string;
using std::list;
using std::map;
using std::unordered_map;

#include "Source/SakuraConstants.h"
#include "Source/SakuraString.h"
#include "Source/SakuraFontSheet.h"
#include "Source/CreateTranslatedFontSheet.h"
#include "Source/SakuraTextFile.h"
#include "Source/ImageExtraction.h"
#include "Source/FaceWinAll.h"
#include "Source/CreateStorySpreadhseets.h"
#include "Source/MNameCG.h"
#include "Source/BattleMenuExtraction.h"
#include "Source/MainMenuExtraction.h"
#include "Source/InfoNameExtraction.h"
#include "Source/SysFileExtraction.h"
#include "Source/CreateSysSpreadsheets.h"
#include "Source/CreateIndexFile.h"
#include "Source/MiniGameExtraction.h"
#include "Source/CreateMiniGameSpreadsheets.h"
#include "Source/BatchedImageExtractor.h"
#include "Source/InsertText.h"
#include "Source/PatchGame.h"

void PrintHelp()
{
	printf("usage: SakuraTaisen [command]\n");
	printf("Commands:\n");
	printf("ExtractText rootSakuraTaisenDirectory paletteFileName outDirectory\n");
	printf("CreateTranslatedFontSheets translatedFontSheet outDirectory\n");
	printf("ConvertTranslatedText translatedTextDir outDirectory\n");
	printf("PatchGameKNJ rootSakuraTaisenDirectory newKNJ outDirectory\n");
	printf("PatchPalettes rootSakuraTaisenDirectory originalPalette newPalette outDirectory\n");
	printf("InsertText rootSakuraTaisenDirectory translatedText outDirectory\n");
	printf("FindDuplicateText dialogDirectory outFile\n");
	printf("FindDialogOrder rootSakuraTaisenDirectory outDirectory\n");
	printf("ParseEVTFiles rootSakuraTaisenDirectory\n");
	printf("CreateTBLSpreadsheets dialogImageDirectory duplicatesFile sakura1Directory translatedTextDirectory forRelease\n");
	printf("CreateMesSpreadsheets dialogImageDirectory rootSakuraTaisenDirectory\n");
	printf("CreateWKLSpreadsheets dialogImageDirectory duplicatesFile rootSakuraTaisenDirectory\n");
	printf("CreateSysSpreadsheets dialogImageDirectory duplicatesFile sakura2Directory\n");
	printf("CreateNameCG1Spreadsheet imageDirectory\n");
	printf("ExtractImages fileName paletteFile width height outDirectory\n");
	printf("ExtractPalettedImage fileName paletteFile offset width height numTilesPerRow numColors[256, 128] pngOrBmp[0 png, 1 bmp] outDirectory\n");
	printf("ExtractFCEFiles rootSakuraTaisenDirectory paletteFile outDirectory\n");
	printf("ExtractFACEFiles rootSakuraTaisenDirectory outDirectory\n");
	printf("ExtractWKLFiles rootSakuraTaisenDirectory outDirectory\n");
	printf("ExtractTMapSP rootSakuraTaisenDirectory paletteFile outDirectory\n");
	printf("PatchTMapSP sakuraDirectory patchDataPath\n");
	printf("PrintPaletteColors fileName\n");
	printf("CompressFile inFilePath outFilePath\n");
	printf("FindCompressedData compressedFile uncompressedFile outDirectory\n");
	printf("FindCompressedDataInDir inDirectory uncompressedFile outDirectory\n");
	printf("ExtractMiniGameData rootSakuraDirectory patchedDataDirectory outDirectory\n");
	printf("ExtractStatusScreen rootSakuraDirectory patchedDataDirectory outDirectory\n");
	printf("ExtractTitleScreens rootSakuraDir discNumber outDir\n");
	printf("YabauseToMednafen yabauseFilePath outFile\n");
	printf("DumpBitmap inputFilePath outDirectory\n");
	printf("ExtractMiniGames rootSakuraDirectory isBmp outDir\n");
	printf("CreateMiniGameSpreadsheets miniGameImageDirectory\n");
	printf("BatchImageExtractor filePath outputDirectory\n");
	printf("PatchGame sourceGameDirectory translationDataDirectory patchedGameDirectory\n");
}

int main(int argc, char *argv[])
{
#if 0
	FindDiff();

	if(argc == 2)
	{
		string command(argv[1]);
		command = "";

		return 1;
	}
#else
	if(argc == 1)
	{
		PrintHelp();
		return 1;
	}

	const string command(argv[1]);

	if (command == string("PatchGame") && argc == 5)
	{
		const string inSourceGameDirectory = string(argv[2]);
		const string inTranslatedDirectory = string(argv[3]);
		const string inPatchedDirectory    = string(argv[4]);

		PatchGame(inSourceGameDirectory, inTranslatedDirectory, inPatchedDirectory);
	}
	else if (command == string("ExtractText") && argc == 5)
	{
		const string searchDirectory = string(argv[2]);
		const string paletteFileName = string(argv[3]);
		const string outDirectory = string(argv[4]) + Seperators;

		ExtractText(searchDirectory, paletteFileName, outDirectory);
	}
	else if(command == "CreateTBLSpreadsheets" && argc == 8 )
	{
		const string dialogImageDirectory = string(argv[2]);
		const string duplicatesFile       = string(argv[3]);
		const string sakura1Directory     = string(argv[4]) + Seperators;
		const int    discNumber           = atoi(argv[5]);
		const string translatedDirectory  = string(argv[6]) + Seperators;
		const bool bForRelease            = atoi(argv[7]) != 0;

		CreateStoryTextSpreadsheets(dialogImageDirectory, duplicatesFile, sakura1Directory, discNumber, translatedDirectory, bForRelease);
	}
	else if( command == "CreateSysSpreadsheets" && argc == 5 )
	{
		const string dialogImageDirectory = string(argv[2]);
		const string duplicatesFile = string(argv[3]);
		const string sakura2Directory = string(argv[4]) + Seperators;

		CreateSysSpreadsheets(dialogImageDirectory, duplicatesFile, sakura2Directory);
	}
	else if(command == "CreateNameCG1Spreadsheet" && argc == 3 )
	{
		const string imageDirectory = string(argv[2]) + Seperators;

		CreateNameCG1Spreadsheet(imageDirectory);
	}
	else if (command == "CreateNameGOVERSpreadsheet" && argc == 3)
	{
		const string imageDirectory = string(argv[2]) + Seperators;

		CreateNameGOVERSpreadsheet(imageDirectory);
	}
	else if (command == "CreateNamePBookFLSpreadsheet" && argc == 3)
	{
		const string imageDirectory = string(argv[2]) + Seperators;

		CreateNamePBookFLSpreadsheet(imageDirectory);
	}
	else if (command == "CreateNameInfoNameSpreadsheet" && argc == 3)
	{
		const string imageDirectory = string(argv[2]) + Seperators;

		CreateNameInfoNameSpreadsheet(imageDirectory);
	}
	else if (command == "CreateMiniGameSpreadsheets" && argc == 3)
	{
		const string miniGameImagesDirectory = string(argv[2]) + Seperators;

		CreateMinigameSpreadsheets(miniGameImagesDirectory);
	}
	else if(command == "ExtractMNameCGFiles" && argc == 5 )
	{
		const string rootSakuraTaisenDirectory = string(argv[2]) + Seperators;
		const string paletteFile               = string(argv[3]);
		const string outDirectory              = string(argv[4]) + Seperators;

		ExtractNameCG(rootSakuraTaisenDirectory, paletteFile, outDirectory);
	}
	else if (command == "ExtractBattleMenu" && argc == 5)
	{
		const string rootSakuraTaisenDirectory = string(argv[2]) + Seperators;
		const bool   bmpFormat                 = atoi(argv[3]);
		const string outDirectory              = string(argv[4]) + Seperators;

		ExtractBattleMenu(rootSakuraTaisenDirectory, bmpFormat, outDirectory);
	}
	else if (command == "ExtractMainMenu" && argc == 5)
	{
		const string rootSakuraTaisenDirectory = string(argv[2]) + Seperators;
		const bool   bmpFormat = atoi(argv[3]);
		const string outDirectory = string(argv[4]) + Seperators;

		ExtractMainMenu(rootSakuraTaisenDirectory, bmpFormat, outDirectory);
	}
	else if (command == "ExtractInfoName" && argc == 6)
	{
		const string rootSakuraTaisenDirectory = string(argv[2]) + Seperators;
		const string paletteFileName           = string(argv[3]);
		const bool   bmpFormat                 = atoi(argv[4]);
		const string outDirectory              = string(argv[5]) + Seperators;

		ExtractInfoName(rootSakuraTaisenDirectory, paletteFileName, bmpFormat, outDirectory);
	}
	else if (command == "ExtractSysFiles" && argc == 5)
	{
		const string rootSakuraTaisenDirectory = string(argv[2]) + Seperators;
		const string paletteFileName           = string(argv[3]);
		const string outDirectory              = string(argv[4]) + Seperators;

		ExtractSysFiles(rootSakuraTaisenDirectory, paletteFileName, outDirectory);
	}
	else if (command == "ExtractMiniGames" && argc == 5)
	{
		const string rootSakuraTaisenDirectory = string(argv[2]) + Seperators;
		const bool   bmpFormat                 = atoi(argv[3]);
		const string outDirectory              = string(argv[4]) + Seperators;

		ExtractMiniGames(rootSakuraTaisenDirectory, bmpFormat, outDirectory);
	}
	else if (command == "CreateIndexFile" && argc == 6)
	{
		const string mesFileDirectory  = string(argv[2]) + Seperators;
		const string skFileDirectory   = string(argv[3]) + Seperators;
		const string miniGameDirectory = string(argv[4]) + Seperators;
		const string outFilePath       = string(argv[5]);

		CreateIndexFile(mesFileDirectory, skFileDirectory, miniGameDirectory, outFilePath);
	}
	else if(command == "BatchImageExtractor" && argc == 5)
	{
		const string filePath = string(argv[2]);
		const string palettePath = string(argv[3]);
		const string outDir   = string(argv[4]) + Seperators;

		BatchExtractImagesFromFile(filePath.c_str(), palettePath.c_str(), outDir);
	}
	else
	{
		PrintHelp();
	}
#endif
	return 1;
}
