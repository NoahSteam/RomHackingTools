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
			//		pTileBuffer[t*2 + 1]     |= (pNextTile->mpTile[t] & 0x0f);
			//		pTileBuffer[t*2 + 0] |= (pNextTile->mpTile[t] & 0xf0) >> 4;
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

void PatchFontSheets()
{
	FileReadWriter f;
	if( !f.OpenFile("C:\\Users\\16306\\Desktop\\Rizwan\\DragonForce\\Game\\Disc_Patched\\Data.0") )
	{
		return;
	}

	BmpToDragonForcePS2FontSheet fontSheetConverter;
	if( !fontSheetConverter.ConvertBmpToFontSheet("C:\\Users\\16306\\Desktop\\Rizwan\\RomHackingTools\\DragonForce\\Data\\PS2EnglishFontSheet.bmp", 16, 32) )
	{
		return;
	}

	fontSheetConverter.OutputToFile("C:\\Users\\16306\\Desktop\\Rizwan\\RomHackingTools\\DragonForce\\Data\\PS2EnglishFontSheet.bin");
	
	uint32 offset = 0;
	int numWritten = 0;
	for( const BmpToDragonForcePS2FontSheet::Tile& tile : fontSheetConverter.mTiles )
	{
		if( !tile.pBuffer )
		{
			break;
		}

		f.WriteData(0x1BCBE51 + offset, tile.pBuffer, fontSheetConverter.GetTileSize());

		offset += fontSheetConverter.GetTileSize();

		++numWritten;
	}

	numWritten = numWritten + 1;
}

void main()
{
	PatchFontSheets();
}