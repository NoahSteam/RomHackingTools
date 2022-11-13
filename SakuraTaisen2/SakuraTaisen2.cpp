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
#include "Source/ImageExtraction.h"
#include "Source/SakuraTextFile.h"
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
#include "Source/TitleScreens.h"
#include "Source/PatchGame.h"
#include "Source/ExportTranslationData.h"
#include "Source/IntroScreenExtractor.h"

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

//FUN_06009df8
int SW2PRSDecompressor(const unsigned char* param_1, unsigned char* param_2)

{
	char cVar1;
	uint32 iVar2;
	const unsigned char* pcVar3;
	const unsigned char* pcVar4;
	unsigned char* pcVar5;
	uint32 uVar6;
	uint32 uVar7;
	uint32 uVar8;
	uint32 iVar9;
	uint32 uVar10;
	uint32 uVar11;

	uVar11 = 0xffffff00;
	uVar10 = 0xffffe000;
	uVar6 = (uint8)*param_1;
	pcVar3 = param_1 + 1;
	iVar9 = 9;
	pcVar5 = param_2;
	int outIndex = 0;
	do 
	{
		while (true) 
		{
			iVar9 = iVar9 + -1;
			pcVar4 = pcVar3;
			uVar7 = uVar6;
			if (iVar9 == 0) 
			{
				uVar7 = (uint8)*pcVar3;
				pcVar4 = pcVar3 + 1;
				iVar9 = 8;
			}
			uVar6 = uVar7 >> 1;
			if ((uVar7 & 1) != 1) 
			{
				break;
			}
			pcVar3 = pcVar4 + 1;
			*pcVar5 = *pcVar4;
			pcVar5 = pcVar5 + 1;
			outIndex++;
		}
		iVar9 = iVar9 + -1;
		uVar7 = uVar7 >> 2;
		uVar8 = uVar6;
		if (iVar9 == 0) 
		{
			uVar8 = (uint8)*pcVar4;
			pcVar4 = pcVar4 + 1;
			iVar9 = 8;
			uVar7 = uVar8 >> 1;
		}
		uVar6 = uVar7;
		if ((uVar8 & 1) == 1)
		{
			pcVar3 = pcVar4 + 2;
			uVar7 = (uint8)*pcVar4 & 0xffU | (uint8)pcVar4[1] << 8;
			if (uVar7 == 0) 
			{
				return (int)pcVar5 - (int)param_2;
			}
			uVar8 = (uint8)*pcVar4 & 7;
			uVar7 = uVar7 >> 3 | uVar10;
			if (uVar8 != 0)
			{
				goto LAB_06009e50;
			}
			cVar1 = *pcVar3;
			pcVar3 = pcVar4 + 3;
			pcVar4 = pcVar5 + uVar7;
			iVar2 = ((uint8)cVar1 & 0xffU) + 1;
		}
		else 
		{
			iVar9 = iVar9 + -1;
			if (iVar9 == 0) 
			{
				uVar6 = (uint8)*pcVar4;
				pcVar4 = pcVar4 + 1;
				iVar9 = 8;
			}
			uVar7 = uVar6 & 1;
			uVar8 = uVar6 >> 1;
			iVar9 = iVar9 + -1;
			uVar6 = uVar6 >> 2;
			if (iVar9 == 0)
			{
				uVar8 = (uint8)*pcVar4;
				pcVar4 = pcVar4 + 1;
				iVar9 = 8;
				uVar6 = uVar8 >> 1;
			}
			pcVar3 = pcVar4 + 1;
			uVar8 = (uint8)(uVar7 == 1) << 1 | (uint8)((uVar8 & 1) == 1);
			uVar7 = (uint8)*pcVar4 | uVar11;
		LAB_06009e50:
			iVar2 = uVar8 + 2;
			pcVar4 = pcVar5 + uVar7;
		}
		do 
		{
			cVar1 = *pcVar4;
			pcVar4 = pcVar4 + 1;
			iVar2 = iVar2 + -1;
			*pcVar5 = cVar1;
			pcVar5 = pcVar5 + 1;
			++outIndex;
		} while (iVar2 != 0);
	} while (true);
}

void TestDecompressor()
{
	FileData fileData;
	FileNameContainer fileNameInfo("A:\\SakuraWars2\\Disc1_Original\\SAKURA1\\TTL2CGB.BIN");
	if(!fileData.InitializeFileData(fileNameInfo))
	{
		return;
	}

	char* outBuffer = new char[1024*1024];
	memset(outBuffer, 0, 1024*1024);
	SW2PRSDecompressor((unsigned char*)(fileData.GetData() + 0x400), (unsigned char*)outBuffer);

	FileWriter outData;
	outData.OpenFileForWrite("a:\\SakuraWars2\\testOutput.bin");
	outData.WriteData(outBuffer, 1024*1024);

	delete[] outBuffer;
}

void VerifyText()
{
	char exePath[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, exePath, MAX_PATH);
	std::string::size_type pos = std::string(exePath).find_last_of("\\/");

	const string currentDirectory = std::string(exePath).substr(0, pos) + Seperators; //"A:\\SakuraWars2\\TextVerification\\"
	const string gameSourceDirectory = currentDirectory + "SourceFiles\\";
	const string translatedTextDirectory = currentDirectory + "TranslationData\\";
	VerifyText(translatedTextDirectory, gameSourceDirectory, currentDirectory);
}

void OutputUncompressedData(const string& InFileName, const string& OutFileName, int InOffset)
{
	const FileNameContainer inFileName(InFileName);
	FileData compressedFile;
	if(!compressedFile.InitializeFileData(inFileName))
	{
		return;
	}

	if((unsigned long)InOffset >= compressedFile.GetDataSize())
	{
		printf("Failed: Offset is larger than the file.\n");
		return;
	}

	FileWriter outFile;
	if(!outFile.OpenFileForWrite(OutFileName))
	{
		return;
	}

	PRSDecompressor decompressed;
	decompressed.UncompressData((void*)(compressedFile.GetData() + InOffset), compressedFile.GetDataSize() - InOffset);

	outFile.WriteData((void*)decompressed.mpUncompressedData, decompressed.mUncompressedDataSize);
}

int main(int argc, char *argv[])
{
#if 0
	{
		VerifyText();
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
		const string inSourceGameDirectory = string(argv[2]) + Seperators;
		const string inTranslatedDirectory = string(argv[3]) + Seperators;
		const string inPatchedDirectory    = string(argv[4]) + Seperators;

		PatchGame(inSourceGameDirectory, inTranslatedDirectory, inPatchedDirectory);
	}
	else if (command == string("ExtractText") && argc == 5)
	{
		const string searchDirectory = string(argv[2]);
		const string paletteFileName = string(argv[3]);
		const string outDirectory = string(argv[4]) + Seperators;

		ExtractText(searchDirectory, paletteFileName, outDirectory);
	}
	else if (command == string("ExtractFontSheets") && argc == 5)
	{
		const string searchDirectory = string(argv[2]);
		const string paletteFileName = string(argv[3]);
		const string outDirectory = string(argv[4]) + Seperators;

		ExtractFontSheets(searchDirectory, paletteFileName, outDirectory);
	}
	else if (command == string("ExtractTextCode") && argc == 4)
	{
		const string searchDirectory = string(argv[2]) + Seperators;
		const string outDirectory = string(argv[3]) + Seperators;

		ExtractTextCode(searchDirectory, outDirectory);
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
	else if(command == "ExportTranslationData" && argc == 4)
	{
		const string filePath = string(argv[2]);
		const string outDir = string(argv[3]) + Seperators;

		ExportTranslationData(filePath, outDir);
	}
	else if(command == "VerifyText" && argc == 5)
	{
		const string inSourceGameDirectory = string(argv[2]);
		const string inTranslatedDirectory = string(argv[3]);
		const string inPatchedDirectory = string(argv[4]);

		VerifyText(inSourceGameDirectory, inTranslatedDirectory, inPatchedDirectory);
	}
	else if(command == "ExtractIntroImages" && argc == 4)
	{
		const string inSourceGameDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;

		ExtractIntrosScreens(inSourceGameDirectory, inOutputDirectory);
	}
	else if(command == "OutputUncompressedData" && argc == 5)
	{
		const string inSourceFile = string(argv[2]);
		const string inOutputFile = string(argv[3]);
		const int inOffset        = atoi(argv[4]);

		OutputUncompressedData(inSourceFile, inOutputFile, inOffset);
	}
	else
	{
		PrintHelp();
	}
#endif
	return 1;
}
