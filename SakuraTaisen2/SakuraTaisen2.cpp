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
#include <set>
#include <assert.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "..\Utils\Utils.h"
#include "..\Utils\decompress_rtns.c"

using std::vector;
using std::string;
using std::list;
using std::map;
using std::unordered_map;
using std::unordered_set;

#include "Source/SakuraConstants.h"
#include "Source/SakuraString.h"
#include "Source/SakuraFontSheet.h"
#include "Source/SakuraCommon.h"
#include "Source/SyncFiles.h"
#include "Source/ImageColoringFunctions.h"
#include "Source/TextInImage.h"
#include "Source/TextInImageUsingFontSheet.h"
#include "Source/ExportTranslationToFiles.h"
#include "Source/CreateTranslatedFontSheet.h"
#include "Source/ImageExtraction.h"
#include "Source/SakuraTextFile.h"
#include "Source/FaceWinAll.h"
#include "Source/CreateStorySpreadhseets.h"
#include "Source/MNameCG.h"
#include "Source/BattleMenu.h"
#include "Source/MainMenu.h"
#include "Source/InfoNameExtraction.h"
#include "Source/SysFileExtraction.h"
#include "Source/CreateSysSpreadsheets.h"
#include "Source/CreateIndexFile.h"
#include "Source/MiniGameExtraction.h"
#include "Source/CreateMiniGameSpreadsheets.h"
#include "Source/BatchedImageExtractor.h"
#include "Source/InsertText.h"
#include "Source/SysFilePatcher.h"
#include "Source/TitleScreens.h"
#include "Source/SplashScreen.h"
#include "Source/ExportTranslationData.h"
#include "Source/IntroScreenExtractor.h"
#include "Source/SakuraWars2Utils.h"
#include "Source/BGDatFiles.h"
#include "Source/LoadScreens.h"
#include "Source/NBGFile.h"
#include "Source/NCDatFile.h"
#include "Source/EyeCatch.h"
#include "Source/CreateInfoNameImages.h"
#include "Source/VDP2Images.h"
#include "Source/StatusScreen.h"
#include "Source/Kinematron.h"
#include "Source/WarningScreens.h"
#include "Source/TycoonGame.h"
#include "Source/LongDay.h"
#include "Source/BattleAnimViewer.h"
#include "Source/BattleSimulatorInLongDay.h"
#include "Source/Credits.h"
#include "Source/SyncFilePatching.h"
#include "Source/IntroSubtitles.h"
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

//Swaps requested battle number with the first battle
void SwapBattleFiles(const string& InBattleNumber, const string& InPatchedDir)
{
	const string prefix = "M";

	const string colFileOrig = InPatchedDir + Seperators + "M00COL.BIN";
	const string lowFileOrig = InPatchedDir + Seperators + "M00LOW.BIN";
	const string prgFileOrig = InPatchedDir + Seperators + "M00PRG.BIN";
	const string vdp1FileOrig = InPatchedDir + Seperators + "M00VDP1.BIN";
	const string vdp2FileOrig = InPatchedDir + Seperators + "M00VDP2.BIN";

	const string colFileOrigModified = InPatchedDir + Seperators + "M00OrigCOL.BIN";
	const string lowFileOrigModified = InPatchedDir + Seperators + "M00OrigLOW.BIN";
	const string prgFileOrigModified = InPatchedDir + Seperators + "M00OrigPRG.BIN";
	const string vdp1FileOrigModified = InPatchedDir + Seperators + "M00OrigVDP1.BIN";
	const string vdp2FileOrigModified = InPatchedDir + Seperators + "M00OrigVDP2.BIN";

	const string colFile = InPatchedDir + Seperators + prefix + InBattleNumber + "COL.BIN";
	const string lowFile = InPatchedDir + Seperators + prefix + InBattleNumber + "LOW.BIN";
	const string prgFile = InPatchedDir + Seperators + prefix + InBattleNumber + "PRG.BIN";
	const string vdp1File = InPatchedDir + Seperators + prefix + InBattleNumber + "VDP1.BIN";
	const string vdp2File = InPatchedDir + Seperators + prefix + InBattleNumber + "VDP2.BIN";

	//Rename 0 to to orig
	rename(colFileOrig.c_str(), colFileOrigModified.c_str());
	rename(lowFileOrig.c_str(), lowFileOrigModified.c_str());
	rename(prgFileOrig.c_str(), prgFileOrigModified.c_str());
	rename(vdp1FileOrig.c_str(), vdp1FileOrigModified.c_str());
	rename(vdp2FileOrig.c_str(), vdp2FileOrigModified.c_str());

	//Rename number to 0
	rename(colFile.c_str(), colFileOrig.c_str());
	rename(lowFile.c_str(), lowFileOrig.c_str());
	rename(prgFile.c_str(), prgFileOrig.c_str());
	rename(vdp1File.c_str(), vdp1FileOrig.c_str());
	rename(vdp2File.c_str(), vdp2FileOrig.c_str());
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
				return reinterpret_cast<int>(pcVar5) - reinterpret_cast<int>(param_2);
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
//	const string currentDirectory = "A:\\SakuraWars2\\TextVerification\\";
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

void FixupStellarAssultLoadScreens()
{
	char exePath[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, exePath, MAX_PATH);
	std::string::size_type pos = std::string(exePath).find_last_of("\\/");

	//const string currentDirectory = "A:\\SakuraWars2\\BuiltTools\\StellarAssault\\FixupLoadScreens\\";//std::string(exePath).substr(0, pos) + Seperators;
	const string currentDirectory = std::string(exePath).substr(0, pos) + Seperators;

	vector<FileNameContainer> allFiles;
	vector<FileNameContainer> bmpFiles;
	FindAllFilesWithinDirectory(currentDirectory, allFiles);
	GetAllFilesOfType(allFiles, ".bmp", bmpFiles);

	const char header[8] = {0, 0, 0, 1, 0, (char)0xd0, 0, (char)0x57};

	for(const FileNameContainer& bmpFileName : bmpFiles)
	{
		BitmapReader bmpReader;
		if(!bmpReader.ReadBitmap(bmpFileName.mFullPath))
		{
			continue;
		}

		BitmapFormatConverter convertedData;
		convertedData.ConvertFrom32BitTo15Bit(bmpReader.GetColorData(), bmpReader.GetColorDataSize());

		const std::string outFileName = currentDirectory + bmpFileName.mNoExtension + std::string(".SP");
		FileWriter outFile;
		if(!outFile.OpenFileForWrite(outFileName))
		{
			continue;
		}
		outFile.WriteData(&header[0], sizeof(header));
		outFile.WriteData(convertedData.GetConvertedData(), convertedData.GetConvertedDataSize());
	}

	/*
	vector<FileNameContainer> spFiles;
	GetAllFilesOfType(allFiles, ".SP", spFiles);
	for (const FileNameContainer& spFileName : spFiles)
	{
		FileData spFile;
		spFile.InitializeFileData(spFileName);

		BitmapFormatConverter convertedData;
		convertedData.ConvertFrom15BitTo32Bit(spFile.GetData() + 8, spFile.GetDataSize() - 8);

		const std::string outFileName = currentDirectory + spFileName.mNoExtension + std::string("_.bmp");
		BitmapWriter outBmp;
		outBmp.CreateBitmap(outFileName, 208, -87, 32, convertedData.GetConvertedData(), convertedData.GetConvertedDataSize(), nullptr, 0, true);
	}*/
}

void TileCounter(const string& inImagePath)
{
	char exePath[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, exePath, MAX_PATH);
	std::string::size_type pos = std::string(exePath).find_last_of("\\/");
	const string currentDirectory = std::string(exePath).substr(0, pos) + Seperators;

	TextFileWriter outFile;
	if(!outFile.OpenFileForWrite((currentDirectory + "Result.txt")))
	{
		printf("Couldn't open outfile\n");
		return;
	}

	const uint32 tileDim = 8;
	BmpToSaturnConverter patchedImage;
	if (!patchedImage.ConvertBmpToSakuraFormat(inImagePath, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim))
	{
		outFile.Printf("Failed to convert file\n");
		return;
	}

	TileSetOptimizer optimizedTileSet;
	optimizedTileSet.OptimizeTileSet(patchedImage);
	optimizedTileSet.PackTiles();
	
	outFile.Printf("TileCount: %i  (Must be less than 704 tiles)\n", optimizedTileSet.GetPackedTileSize()/64);
}

int main(int argc, char *argv[])
{
#if 0
	{
	//	EncodeRadio();
		
	//	TestRadio();
	
	//	VerifyText();

		//FixupStellarAssultLoadScreens();

		if (argc == 2)
		{
			const string imagePath = string(argv[1]);
			TileCounter(imagePath);
		}
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

		PatchGame(inSourceGameDirectory, inTranslatedDirectory, inPatchedDirectory, 1);
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
	else if (command == "CreateNamePBookBTSpreadsheet" && argc == 3)
	{
		const string imageDirectory = string(argv[2]) + Seperators;

		CreateNamePBookBTSpreadsheet(imageDirectory);
	}
	else if (command == "CreateNamePBookECSpreadsheet" && argc == 3)
	{
		const string imageDirectory = string(argv[2]) + Seperators;

		CreateNamePBookECSpreadsheet(imageDirectory);
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
	else if (command == "CreateNameKinematronSpreadsheet" && argc == 3)
	{
		const string imagesDirectory = string(argv[2]) + Seperators;

		CreateNameKinematronSpreadsheet(imagesDirectory);
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

		ExtractPBookFL(rootSakuraTaisenDirectory, bmpFormat, outDirectory);
		ExtractPBookEC(rootSakuraTaisenDirectory, bmpFormat, outDirectory);
		ExtractBattlePauseMenu(rootSakuraTaisenDirectory, bmpFormat, outDirectory);
		ExtractPBEye(rootSakuraTaisenDirectory, bmpFormat, outDirectory);
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
	else if(command == "ExtractSysTextCode" && argc == 4)
	{
		const string searchDirectory = string(argv[2]) + Seperators;
		const string outDirectory = string(argv[3]) + Seperators;

		ExtractSysTextCode(searchDirectory, outDirectory);
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
	else if(command == "FixupWKLImageColors" && argc == 5)
	{
		const string inWklDir = string(argv[2]) + Seperators;
		const string inOutputDir = string(argv[3]) + Seperators;
		const string inNewPaletteImage = string(argv[4]);

		FixupWKLImageColors(inWklDir, inNewPaletteImage, inOutputDir);
	}
	else if( command == "ExtractDemoWPALL" && argc == 4 )
	{
		const string inSrcDir = string(argv[2]) + Seperators;
		const string inOutputDir = string(argv[3]) + Seperators;

		ExtractDemoWPALL(inSrcDir, inOutputDir);
	}
	else if (command == "ExtractLoadScreens" && argc == 4)
	{
		const string inSrcDir = string(argv[2]) + Seperators;
		const string inOutputDir = string(argv[3]) + Seperators;

		ExtractLoadScreens(inSrcDir, inOutputDir);
	}
	else if(command == "ExtractBattleLoadScreens" && argc == 4)
	{
		const string inSrcDir = string(argv[2]) + Seperators;
		const string inOutputDir = string(argv[3]) + Seperators;

		ExtractBattleLoadScreens(inSrcDir, inOutputDir);
	}
	else if(command == "WriteTextIntoImage" && argc == 6)
	{
		const string inTranslatedTextFile = string(argv[2]);
		const string inImageSizesFile = string(argv[3]);
		const string inOriginalImagesDirectory = string(argv[4]) + Seperators;
		const string inOutputPath = string(argv[5]) + Seperators;

		WriteTextIntoImage(inTranslatedTextFile, inImageSizesFile, inOriginalImagesDirectory, inOutputPath);
	}
	else if(command == "CreateTranslatedInfoNameImages" && argc == 5)
	{
		const string inTranslationDirectory = string(argv[2]) + Seperators;
		const string inFontSheet            = string(argv[3]);
		const string inFontSheetSmall       = string(argv[4]);

		CreateTranslatedInfoNameImages(inTranslationDirectory, inFontSheet, inFontSheetSmall);
	}
	else if (command == "CreateTranslatedStatusScreenImages" && argc == 5)
	{
		const string inTranslationDirectory = string(argv[2]) + Seperators;
		const string inFontSheet = string(argv[3]);
		const string inFontSheetSmall = string(argv[4]);

		CreateTranslatedStatusScreenImages(inTranslationDirectory, inFontSheet, inFontSheetSmall);
	}
	else if (command == "CreateTranslatedMNameImages" && argc == 5)
	{
		const string inTranslationDirectory = string(argv[2]) + Seperators;
		const string inFontSheet = string(argv[3]);
		const string inFontSheetSmall = string(argv[4]);

		CreateTranslatedMNameImages(inTranslationDirectory, inFontSheet, inFontSheetSmall);
	}
	else if(command == "WriteTextIntoImageUsingFontSheet" && argc == 7 )
	{
		const string inTextFilePath = string(argv[2]);
		const string inFontSheetPath = string(argv[3]);
		const string inBgndImageDirectory = string(argv[4]) + Seperators;
		const string inOriginalImagesDirectory = string(argv[5]) + Seperators;
		const string inOutputPath = string(argv[6]) + Seperators;
		
		WriteTextIntoImagesUsingFontSheet(inTextFilePath, inFontSheetPath, inBgndImageDirectory, 
										 inOriginalImagesDirectory, inOutputPath);
	}
	else if(command == "SwapColorsInImage" && argc == 5)
	{
		const string inDirectory = string(argv[2]);
		const int inSearchColor = atoi(argv[3]);
		const int newColor = atoi(argv[4]);

		SwapColors(inDirectory, (char)inSearchColor, (char)newColor);
	}
	else if(command == "SwapBattleFiles" && argc == 4)
	{
		const string inBattleNum = string(argv[2]);
		const string inPatchedDir = string(argv[3]) + Seperators;

		SwapBattleFiles(inBattleNum, inPatchedDir);
	}
	else if (command == "ExtractBGDatFiles" && argc == 4)
	{
		const string inSourceGameDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;

		ExtractBGDatFiles(inSourceGameDirectory, inOutputDirectory);
	}
	else if(command == "ExtractVDP2Files" && argc == 4)
	{
		const string inDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;

		ExtractVDP2Files(inDirectory, inOutputDirectory);
	}
	else if(command == "ExtractNBGFile" && argc == 5)
	{
		const string inFilePath = string(argv[2]);
		const string inOutputDirectory = string(argv[3]) + Seperators;
		const bool   bmpFormat = atoi(argv[4]);

		ExtractNBGFile(inFilePath, inOutputDirectory, bmpFormat);
	}
	else if (command == "ExtractNCDatFile" && argc == 5)
	{
		const string inFilePath = string(argv[2]);
		const string inOutputDirectory = string(argv[3]) + Seperators;
		const bool   bmpFormat = atoi(argv[4]);

		ExtractNCDatFile(inFilePath, inOutputDirectory, bmpFormat);
	}
	else if (command == "ExtractEyeCatch" && argc == 5)
	{
		const string inDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;
		const bool   bmpFormat = atoi(argv[4]);

		ExtractEyeCatch(inDirectory, inOutputDirectory, bmpFormat);
	}
	else if (command == "ExtractStatusScreen" && argc == 5)
	{
		const string inDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;
		const bool   bmpFormat = atoi(argv[4]);

		ExtractStatusScreen(inDirectory, inOutputDirectory, bmpFormat);
	}
	else if (command == "ExtractKinematronImages" && argc == 5)
	{
		const string inDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;
		const bool   bmpFormat = atoi(argv[4]);

		ExtractKinematronImages(inDirectory, inOutputDirectory, bmpFormat);
	}
	else if(command == "ExtractWarningScreens" && argc == 5)
	{
		const string inDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;
		const bool   bmpFormat = atoi(argv[4]);

		ExtractWarningScreens(inDirectory, inOutputDirectory, bmpFormat);
	}
	else if(command == "SetColorInPalette" && argc == 6)
	{
		const string inDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;
		const int    inPaletteIndex = atoi(argv[4]);
		const uint32 inPaletteColor = ConvertFromHexString<uint32>(argv[5]);

		SetColorInPalette(inDirectory, inOutputDirectory, inPaletteIndex, inPaletteColor);
	}
	else if(command == "ExtractTycoonRulesScreen" && argc == 5)
	{
		const string inDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;
		const bool   bmpFormat = atoi(argv[4]);

		ExtractTycoonRulesScreen(inDirectory, inOutputDirectory, bmpFormat);
	}
	else if(command == "ExtractLongDayData" && argc == 5)
	{
		const string inDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;
		const bool   bmpFormat = atoi(argv[4]);

		ExtractLongDayData(inDirectory, inOutputDirectory, bmpFormat);
	}
	else if (command == "ExtractBattleAnimViewer" && argc == 5)
	{
		const string inDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;
		const bool   bmpFormat = atoi(argv[4]);

		ExtractBattleAnimViewer(inDirectory, inOutputDirectory, bmpFormat);
	}
	else if (command == "ExtractBattleSimulator" && argc == 5)
	{
		const string inDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;
		const bool   bmpFormat = atoi(argv[4]);

		ExtractBattleSimulator(inDirectory, inOutputDirectory, bmpFormat);
	}
	else if(command == "ExtractCreditsData" && argc == 5)
	{
		const string inDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;
		const bool   bmpFormat = atoi(argv[4]);

		ExtractCreditsData(inDirectory, inOutputDirectory, bmpFormat);
	}
	else if(command == "ExtractIntroSubtitles" && argc == 4)
	{
		const string inDirectory = string(argv[2]) + Seperators;
		const string inOutputDirectory = string(argv[3]) + Seperators;

		ExtractIntroSubtitles(inDirectory, inOutputDirectory);
	}
	else if (command == "CreateTranslatedBattleAnimViewerImages" && argc == 5)
	{
		const string inTranslationDirectory = string(argv[2]) + Seperators;
		const string inFontSheet = string(argv[3]);
		const string inFontSheetSmall = string(argv[4]);

		CreateTranslatedBattleAnimViewerImages(inTranslationDirectory, inFontSheet, inFontSheetSmall);
	}
	else if (command == "CreateTranslatedBattleSimulatorImages" && argc == 5)
	{
		const string inTranslationDirectory = string(argv[2]) + Seperators;
		const string inFontSheet = string(argv[3]);
		const string inFontSheetSmall = string(argv[4]);

		CreateTranslatedBattleSimulatorImages(inTranslationDirectory, inFontSheet, inFontSheetSmall);
	}
	else if(command == "ValidateSyncData" && argc == 4)
	{
		const string inSakura1Directory = string(argv[2]) + Seperators;
		const string outputDirectory = string(argv[3]) + Seperators;

		ValidateSyncData(inSakura1Directory, outputDirectory);
	}
	else
	{
		PrintHelp();
	}
#endif
	return 1;
}
