/*******************************************************************************
DragonForce.cpp - Tools for modifying Dragon Force on PS2

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

#include "..\..\Utils\Utils.h"

using std::vector;
using std::string;
using std::unordered_map;
using std::map;

class SaturnFontToPS2Font
{
public:
	bool ConvertSaturnFontSheetToPS2(const string& inSaturnFontPath, const string& inOutputFontPath, uint32 inSaturnFontWidth, uint32 inSaturnFontHeight, 
									 uint32 inPs2FontWidth, uint32 inPs2FontHeight)
	{
		BmpToSaturnConverter imageConverter;
		if( !imageConverter.ConvertBmpToSakuraFormat(inSaturnFontPath, false, 0, &inSaturnFontWidth, &inSaturnFontHeight) )
		{
			return false;
		}

		const int numTiles = (int)imageConverter.mTileExtractor.mTiles.size();
		const uint32 colorDataSize = inPs2FontWidth * inPs2FontHeight * numTiles;
		char* pColorData = new char[colorDataSize];
		memset(pColorData, 0, colorDataSize);

		const uint32 numBytesInDestTile = inPs2FontWidth * inPs2FontHeight;
		const uint32 numBytesInDestRow  = inPs2FontWidth;

		//Copy tiles over
		const uint32 numBytesInSaturnRow = inSaturnFontWidth / 2;
		for( int tileIndex = 0; tileIndex < numTiles; ++tileIndex )
		{
			for( int y = 0; y < (int)inSaturnFontHeight; ++y )
			{
				char* pDest = pColorData + (numTiles - tileIndex - 1) * numBytesInDestTile + (inPs2FontHeight - y - 1)*numBytesInDestRow;

				for( uint32 x = 0; x < numBytesInSaturnRow; ++x )
				{
					const uint32 tilePixelIndex = x + y*numBytesInSaturnRow;
					const char src = imageConverter.mTileExtractor.mTiles[tileIndex].mpTile[tilePixelIndex];

					char firstPixel  = (src & 0xf0) >> 4;
					char secondPixel = src & 0x0f;

					if( firstPixel != 0 )
					{
						*pDest = 0x33;
					}
					++pDest;

					if( secondPixel != 0 )
					{
						*pDest = 0x33;
					}
					++pDest;
				}
			}
		}

		const int paletteSize = 256 * 4;
		unsigned char* pPaletteData = new unsigned char[paletteSize];
		for( int colorIndex = 0; colorIndex < 256; ++colorIndex )
		{
			const int paletteIndex = colorIndex * 4;
			pPaletteData[paletteIndex + 0] = (unsigned char)colorIndex;
			pPaletteData[paletteIndex + 1] = (unsigned char)colorIndex;
			pPaletteData[paletteIndex + 2] = (unsigned char)colorIndex;
			pPaletteData[paletteIndex + 3] = 0;//(unsigned char)colorIndex;
		}

		BitmapWriter writer;
		writer.CreateBitmap(inOutputFontPath.c_str(), inPs2FontWidth, (int)inPs2FontHeight * numTiles, 8, pColorData, (int)colorDataSize, (char*)pPaletteData, paletteSize, true);

		delete[] pColorData;
		delete[] pPaletteData;

		return true;
	}
};

//Fontsheets in Dragon Force on PS2 are stored as 8bpp images where each tile contains two characters.
//The first character is in the lower 4 bits and the second is in the upper 4 bits.
class BmpToDragonForcePS2FontSheet
{
public:

	struct Tile
	{
		char* pBuffer{nullptr};
	};
	vector<Tile> mTiles;

	uint32 mTileWidth;
	uint32 mTileHeight;

public:

	~BmpToDragonForcePS2FontSheet()
	{
		for( Tile& tile : mTiles )
		{
			delete[] tile.pBuffer;
			tile.pBuffer = nullptr;
		}

		mTiles.clear();
	}

	uint32 GetTileSize() const
	{
		return mTileWidth * mTileHeight;
	}

	bool ConvertBmpToFontSheet(const char* pBitmapPath, uint32 inWidth, uint32 inHeight)
	{
		mTileWidth = inWidth;
		mTileHeight = inHeight;

		BmpToSaturnConverter imageConverter;
		if( !imageConverter.ConvertBmpToSakuraFormat(pBitmapPath, false, 0, &inWidth, &inHeight) )
		{
			return false;
		}

		imageConverter.PackTiles();

		//Create tiles buffer
		mTiles.resize(imageConverter.mTileExtractor.mTiles.size());

		const uint32 bufferSize = inWidth * inHeight;
		const size_t numSourceTiles = imageConverter.mTileExtractor.mTiles.size();
		for( size_t i = 0, destTileIndex = 0; i < numSourceTiles; i += 2, destTileIndex++ )
		{
			const TileExtractor::Tile* pTile     = &imageConverter.mTileExtractor.mTiles[i];
			const TileExtractor::Tile* pNextTile = i + 1 < numSourceTiles ? &imageConverter.mTileExtractor.mTiles[i + 1] : nullptr;

			mTiles[destTileIndex].pBuffer = new char[bufferSize];
			char* pTileBuffer = mTiles[destTileIndex].pBuffer;
	
			assert(pTile->mTileSize == bufferSize);
			for( uint32 t = 0; t < pTile->mTileSize; ++t )
			{
				pTileBuffer[t] = (pTile->mpTile[t]);
			}

			if( pNextTile )
			{
				assert(pNextTile->mTileSize == bufferSize);
				for( uint32 t = 0; t < pNextTile->mTileSize; ++t )
				{
					if(pNextTile->mpTile[t] > 0)
					{
						pTileBuffer[t] += (pNextTile->mpTile[t] + 153);
					}
				}
			}
		}

		return true;
	}

	bool OutputToFile(const char* pFilePath)
	{
		FileWriter writer;
		if( !writer.OpenFileForWrite(pFilePath) )
		{
			return false;
		}

		for( const Tile& tile : mTiles )
		{
			if( !tile.pBuffer )
			{
				break;
			}

			writer.WriteData(tile.pBuffer, mTileWidth*mTileHeight);
		}

		return true;
	}
};

bool PatchFontSheets(const string& inDiscDirectory, const string& inDataDirectory, const string& inOutputDirectory)
{
	FileReadWriter f;
	if( !f.OpenFile((inDiscDirectory + "DATA.0").c_str()) )
	{
		return false;
	}

	BmpToDragonForcePS2FontSheet fontSheetConverter;
	if( !fontSheetConverter.ConvertBmpToFontSheet( (inDataDirectory + "PS2EnglishFontSheet.bmp").c_str(), 16, 32) )
	{
		return false;
	}

	fontSheetConverter.OutputToFile( (inOutputDirectory + "PS2EnglishFontSheet.bin").c_str() );
	
	uint32 offset = 0;
	int numWritten = 0;
	for( const BmpToDragonForcePS2FontSheet::Tile& tile : fontSheetConverter.mTiles )
	{
		if( !tile.pBuffer )
		{
			break;
		}

		f.WriteData(0x1BCBE00 + offset, tile.pBuffer, fontSheetConverter.GetTileSize());

		offset += fontSheetConverter.GetTileSize();

		++numWritten;
	}

	numWritten = numWritten + 1;

	return true;
}

bool PatchTextSpacing(const string& inPatchedDiscDirectory)
{
	FileReadWriter patchedDataFile;
	FileReadWriter patchedReadyFile;
	if( !patchedDataFile.OpenFile((inPatchedDiscDirectory + "DATA.0")) )
	{
		return false;
	}

	if( !patchedReadyFile.OpenFile((inPatchedDiscDirectory + "READY.BIN")) )
	{
		return false;
	}
	
	//Code exists here for spacing text:
	//DATA.0 @0x002b4c44
	//READY.BIN @0x0003cc44
	const char textSpacing = 6;
	patchedDataFile.WriteData(0x002b4c44, &textSpacing, 1);
	patchedReadyFile.WriteData(0x0003cc44, &textSpacing, 1);

	//Code for spacing blank spaces
	//DATA.0 @0x002b3f28
	//READY.BIN @0x0003bf28
	const char spaceSpacing = 4;
	patchedDataFile.WriteData(0x002b3f28, &spaceSpacing, 1);
	patchedReadyFile.WriteData(0x0003bf28, &spaceSpacing, 1);

	return true;
}

bool PatchMenus(const string& inPatchedDiscDirectory, const string& inDataDirectory)
{
	FileReadWriter patchedDataFile;
	if( !patchedDataFile.OpenFile((inPatchedDiscDirectory + "DATA.0")) )
	{
		return false;
	}

	FileData patchedMenuFile;
	if( !patchedMenuFile.InitializeFileData("Menus1.Bin", (inDataDirectory + "Menus1.BIN").c_str()) )
	{
		return false;
	}
	
	patchedDataFile.WriteData(0x07ec22c0, patchedMenuFile.GetData(), patchedMenuFile.GetDataSize());
	patchedDataFile.WriteData(0x07f11d78, patchedMenuFile.GetData(), patchedMenuFile.GetDataSize());
	patchedDataFile.WriteData(0x07f62b54, patchedMenuFile.GetData(), patchedMenuFile.GetDataSize());
	patchedDataFile.WriteData(0x07fb3ce4, patchedMenuFile.GetData(), patchedMenuFile.GetDataSize());
	patchedDataFile.WriteData(0x08004a5c, patchedMenuFile.GetData(), patchedMenuFile.GetDataSize());
	patchedDataFile.WriteData(0x08055c0c, patchedMenuFile.GetData(), patchedMenuFile.GetDataSize());
	patchedDataFile.WriteData(0x080a69ac, patchedMenuFile.GetData(), patchedMenuFile.GetDataSize());
	patchedDataFile.WriteData(0x080f7df4, patchedMenuFile.GetData(), patchedMenuFile.GetDataSize());

	//0x080fa6bc
	FileData patchedMenuFile2;
	if( !patchedMenuFile2.InitializeFileData("Menus2.Bin", (inDataDirectory + "Menus2.BIN").c_str()) )
	{
		return false;
	}
	patchedDataFile.WriteData(0x080f7df4, patchedMenuFile2.GetData(), patchedMenuFile2.GetDataSize());

	FileData fieldMenu1;
	if( !fieldMenu1.InitializeFileData("FieldMenu1.Bin", (inDataDirectory + "FieldMenu1.BIN").c_str()) )
	{
		return false;
	}
	patchedDataFile.WriteData(0x06e3d330, fieldMenu1.GetData(), fieldMenu1.GetDataSize());
	patchedDataFile.WriteData(0x06ef7b70, fieldMenu1.GetData(), fieldMenu1.GetDataSize());
	patchedDataFile.WriteData(0x06fb614c, fieldMenu1.GetData(), fieldMenu1.GetDataSize());
	patchedDataFile.WriteData(0x07073918, fieldMenu1.GetData(), fieldMenu1.GetDataSize());
	patchedDataFile.WriteData(0x07130e18, fieldMenu1.GetData(), fieldMenu1.GetDataSize());
	patchedDataFile.WriteData(0x071edb64, fieldMenu1.GetData(), fieldMenu1.GetDataSize());
	patchedDataFile.WriteData(0x072ac1a8, fieldMenu1.GetData(), fieldMenu1.GetDataSize());
	patchedDataFile.WriteData(0x0736aefc, fieldMenu1.GetData(), fieldMenu1.GetDataSize());

	FileData fieldMenu2;
	if( !fieldMenu2.InitializeFileData("FieldMenu2.Bin", (inDataDirectory + "FieldMenu2.BIN").c_str()) )
	{
		return false;
	}
	patchedDataFile.WriteData(0x001e00f4, fieldMenu2.GetData(), fieldMenu2.GetDataSize());
	patchedDataFile.WriteData(0x06e3f660, fieldMenu2.GetData(), fieldMenu2.GetDataSize());
	patchedDataFile.WriteData(0x06ef9ea0, fieldMenu2.GetData(), fieldMenu2.GetDataSize());
	patchedDataFile.WriteData(0x06fb847c, fieldMenu2.GetData(), fieldMenu2.GetDataSize());
	patchedDataFile.WriteData(0x07075c48, fieldMenu2.GetData(), fieldMenu2.GetDataSize());
	patchedDataFile.WriteData(0x07133148, fieldMenu2.GetData(), fieldMenu2.GetDataSize());
	patchedDataFile.WriteData(0x071efe94, fieldMenu2.GetData(), fieldMenu2.GetDataSize());
	patchedDataFile.WriteData(0x072ae4d8, fieldMenu2.GetData(), fieldMenu2.GetDataSize());
	patchedDataFile.WriteData(0x0736d22c, fieldMenu2.GetData(), fieldMenu2.GetDataSize());

	return true;
}

bool PatchData(const string& inPatchedDiscDirectory, const string& inDataDirectory, const string& inOutputDirectory)
{
	if( !PatchFontSheets(inPatchedDiscDirectory, inDataDirectory, inOutputDirectory) )
	{
		return false;
	}

	PatchTextSpacing(inPatchedDiscDirectory);

	if( !PatchMenus(inPatchedDiscDirectory, inDataDirectory) )
	{
		printf("PatchMenus failed\n");
		return false;
	}

	printf("PatchData Succeeded\n");
	return true;
}

//Extract text from Ready.bin
bool ExtractFileText(const string& inFilePath, uint32 inFileOffset, const string& inOutputFilePath)
{
	FileNameContainer inFileName(inFilePath.c_str());
	FileData readyFile;
	if( !readyFile.InitializeFileData(inFileName) )
	{
		return false;
	}

	FileWriter outFile;
	if( !outFile.OpenFileForWrite(inOutputFilePath) )
	{
		return false;
	}

	uint32 fileOffset = inFileOffset;//0x4bac0;//0x7a728;
	const char* pData = readyFile.GetData();
	while( fileOffset < readyFile.GetDataSize() )
	{
		if( pData[fileOffset] == 0 || (uint8)pData[fileOffset] == 0xff )
		{
			++fileOffset;
			continue;
		}

		outFile.WriteData(&pData[fileOffset], 1);
		++fileOffset;

		if( pData[fileOffset] == 0 )
		{
			char newLine = '\n';
			outFile.WriteData(&newLine, 1);
		}
	}

	return true;
}

void ExtractTextFromStream(const char* pInData, uint32 inOffset, uint32 inDataSize, unordered_map<string, uint32>& outData, 
						   map<uint32, string>& outLineToText)
{
	char buffer[1024];
	memset(buffer, 0, sizeof(buffer));

	uint32 bufferIndex = 0;
	uint32 fileOffset  = inOffset;
	uint32 entryNumber = 0;
	while( fileOffset < inDataSize )
	{
		if( pInData[fileOffset] == 0 )
		{
			++fileOffset;
			continue;
		}

		buffer[bufferIndex++] = pInData[fileOffset++];
		if( bufferIndex > 1024 )
		{
			printf("Buffer too small\n");
			return;
		}

		if( pInData[fileOffset] == 0 )
		{
			string newString(buffer);
			outData[newString]         = entryNumber;
			outLineToText[entryNumber] = newString;

			memset(buffer, 0, sizeof(buffer));

			++entryNumber;
			bufferIndex = 0;
		}
	}
}

bool MatchUpTextFromReadyFiles(const string& inPs2ReadyFilePath, const string& inSaturnEngReadyFilePath, const string& inSaturnJpReadyFilePath,
							   const string& inOutputDirectory)
{
	FileData ps2ReadyFile;
	if( !ps2ReadyFile.InitializeFileData("Ready.bin", inPs2ReadyFilePath.c_str()) )
	{
		return false;
	}

	FileData engReadyFile;
	if( !engReadyFile.InitializeFileData("Ready0.bin", inSaturnEngReadyFilePath.c_str()) )
	{
		return false;
	}

	FileData jpnReadyFile;
	if( !jpnReadyFile.InitializeFileData("Ready0.bin", inSaturnJpReadyFilePath.c_str()) )
	{
		return false;
	}

	TextFileWriter outFile;
	if( !outFile.OpenFileForWrite((inOutputDirectory + "ReadyMatches.txt")) )
	{
		return false;
	}

	unordered_map<string, uint32> ps2Text, engText, jpText;
	map<uint32, string> ps2TextLineToText, engTextLineToText, jpTextLineToText;
	ExtractTextFromStream(ps2ReadyFile.GetData(), 0x7a728, ps2ReadyFile.GetDataSize(), ps2Text, ps2TextLineToText);
	ExtractTextFromStream(engReadyFile.GetData(), 0x4c30c, engReadyFile.GetDataSize(), engText, engTextLineToText);
	ExtractTextFromStream(jpnReadyFile.GetData(), 0x4bd64, jpnReadyFile.GetDataSize(), jpText,  jpTextLineToText);

	for( map<uint32, string>::iterator iter = jpTextLineToText.begin(); iter != jpTextLineToText.end(); ++iter )
	{
		//if( engTextLineToText.find(iter->first) != engTextLineToText.end() )
		{
		//	outFile.Printf("%s, %s\n", iter->second.c_str(), engTextLineToText[iter->first].c_str());
		}
	//	else
		{
		//	outFile.Printf("%s, NotFound\n", iter->second.c_str());
		}

		outFile.Printf("%s\n", iter->second.c_str());
	}

	return true;
}

bool CopyOriginalFiles(const string& inDiscDirectory, const string& inPatchedDiscDirectory)
{
	#define CopyOriginalFile(fileName)\
	{\
		const string originalFile = inDiscDirectory + fileName;\
		const string patchedFile  = inPatchedDiscDirectory + fileName;\
		if( !CopyFile(originalFile.c_str(), patchedFile.c_str(), FALSE) )\
		{\
			return false;\
		}\
	}

	CopyOriginalFile("DATA.O");
	CopyOriginalFile("READY.BIN");

	return true;
}

int main(int argc, char* argv[])
{
	if (argc == 1)
	{
//		PrintHelp();
		return 1;
	}

	const string command(argv[1]);

	if (command == string("PatchData") && argc == 5)
	{
		const string patchedDiscDirectory = string(argv[2]) + Seperators;
		const string dataDirectory        = string(argv[3]) + Seperators;
		const string outDirectory         = string(argv[4]) + Seperators;

		PatchData(patchedDiscDirectory, dataDirectory, outDirectory);
	}
	else if(command == string("FontFromSaturnToPS2") && argc == 4)
	{
		const string saturnFontPath = string(argv[2]);
		const string outputPath = string(argv[3]);

		SaturnFontToPS2Font c;
		if( c.ConvertSaturnFontSheetToPS2(saturnFontPath, outputPath, 8, 32, 16, 32) )
		{
			printf("Success\n");
		}
	}
	else if( command == string("ExtractFileText") && argc == 5 )
	{
		const string discDirectory = string(argv[2]);
		const uint32 offset = (uint32)strtol(argv[3], nullptr, 16);
		const string outputDirectory = string(argv[4]);

		if( ExtractFileText(discDirectory, offset, outputDirectory) )
		{
			printf("Success\n");
		}
		else
		{
			printf("Failed\n");
		}
	}
	else if( command == string("MatchUpTextFromReadyFiles") && argc == 6 )
	{
		const string ps2Ready = string(argv[2]);
		const string engReady = string(argv[3]);
		const string jpnReady = string(argv[4]);
		const string outDirectory = string(argv[5]) + Seperators;

		MatchUpTextFromReadyFiles(ps2Ready, engReady, jpnReady, outDirectory);
	}
}
