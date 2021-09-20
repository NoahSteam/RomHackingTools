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

		const uint32 numTiles = (uint32)imageConverter.mTileExtractor.mTiles.size();
		const uint32 colorDataSize = inPs2FontWidth * inPs2FontHeight * numTiles;
		char* pColorData = new char[colorDataSize];
		memset(pColorData, 0, colorDataSize);

		const uint32 numBytesInDestTile = inPs2FontWidth * inPs2FontHeight;
		const uint32 numBytesInDestRow  = inPs2FontWidth;

		//Copy tiles over
		const uint32 numBytesInSaturnRow = inSaturnFontWidth / 2;
		for( int tileIndex = 0; tileIndex < numTiles; ++tileIndex )
		{
			for( int y = 0; y < inSaturnFontHeight; ++y )
			{
				char* pDest = pColorData + (numTiles - tileIndex - 1) * numBytesInDestTile + (inSaturnFontHeight - y - 1)*numBytesInDestRow;

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

bool PatchData(const string& inDiscDirectory, const string& inDataDirectory, const string& inOutputDirectory)
{
	if( !PatchFontSheets(inDiscDirectory, inDataDirectory, inOutputDirectory) )
	{
		return false;
	}

	printf("PatchData Succeeded\n");
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
		const string discDirectory = string(argv[2]) + Seperators;
		const string dataDirectory = string(argv[3]) + Seperators;
		const string outDirectory  = string(argv[4]) + Seperators;

		PatchData(discDirectory, dataDirectory, outDirectory);
	}
	else if(command == string("FontFromSaturnToPS2") && argc == 4)
	{
		const string saturnFontPath = string(argv[2]);
		const string outputPath = string(argv[3]);

		SaturnFontToPS2Font c;
		c.ConvertSaturnFontSheetToPS2(saturnFontPath, outputPath, 8, 24, 8, 32);
	}
}
