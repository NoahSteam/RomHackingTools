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

void PatchFontSheets(const string& inDiscDirectory, const string& inDataDirectory, const string& inOutputDirectory)
{
	FileReadWriter f;
	if( !f.OpenFile((inDiscDirectory + "DATA.0").c_str()) )
	{
		return;
	}

	BmpToDragonForcePS2FontSheet fontSheetConverter;
	if( !fontSheetConverter.ConvertBmpToFontSheet( (inDataDirectory + "PS2EnglishFontSheet.bmp").c_str(), 16, 32) )
	{
		return;
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
}

bool PatchData(const string& inDiscDirectory, const string& inDataDirectory, const string& inOutputDirectory)
{
	PatchFontSheets(inDiscDirectory, inDataDirectory, inOutputDirectory);

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
}
