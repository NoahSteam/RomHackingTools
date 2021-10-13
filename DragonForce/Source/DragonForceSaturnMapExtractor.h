#pragma once

struct DragonForceMapTileIndex
{
	uint16 pattern1;
	uint16 pattern2;

	void ToBigEndian()
	{
		static const uint16 mask = 0xffff >> 5;
		pattern1 = SwapByteOrder(pattern1);
		pattern2 = SwapByteOrder(pattern2) & mask;

		if( pattern2 % 2 )
		{
			pattern2--;
		}

		pattern2 = pattern2 >> 1;
	}
};

bool ExtractMap(const string& inTileIndicesPath, const string& inColorDataPath, const string& inPalettePath, const string& outPath)
{
	FileData colorData;
	if( !colorData.InitializeFileData(inColorDataPath) )
	{
		return false;
	}

	FileData paletteRawData;
	if( !paletteRawData.InitializeFileData(inPalettePath) )
	{
		return false;
	}

	//Create 32bit palette data
	PaletteData paletteData;
	paletteData.CreateFrom15BitData(paletteRawData.GetData(), paletteRawData.GetDataSize());

	FileData tileIndiceData;
	if( !tileIndiceData.InitializeFileData(inTileIndicesPath) )
	{
		return false;
	}

	const int numTiles = 0x40000 / 0x40;
	const int tileDim = 8;
	TileMap tiles;
	if (!tiles.CreateFontSheetFromData(colorData.GetData(), tileDim * tileDim * numTiles))
	{
		return false;
	}

	//Read in indices
	const int numIndices = tileIndiceData.GetDataSize() / sizeof(DragonForceMapTileIndex);
	DragonForceMapTileIndex* pIndices = new DragonForceMapTileIndex[numIndices];
	memcpy_s(pIndices, sizeof(DragonForceMapTileIndex)*numIndices, tileIndiceData.GetData(), tileIndiceData.GetDataSize());
	for (int tileIndex = 0; tileIndex < numIndices; ++tileIndex)
	{
		pIndices[tileIndex].ToBigEndian();
	}

	const int numTilesInWidth = 32;
	const int numTilesInHeight = 32;
	BitmapSurface sakuraStringBmp;
	sakuraStringBmp.CreateSurface(numTilesInWidth*8, numTilesInHeight*8, BitmapSurface::EBitsPerPixel::kBPP_8, paletteData.GetData(), paletteData.GetSize());

	int currX = 0;
	int currY = 0;
	const int indexOffset = 0;//0x16;
	for(int indiceIndex = 0; indiceIndex < 32*32; ++indiceIndex)
	{
		const int tileIndex = pIndices[indiceIndex].pattern2 - indexOffset;

		if(tileIndex < 0 || tileIndex > numTiles)
		{
			break;
		}

		const char* pTileData = tiles.GetTileData(tileIndex);
		sakuraStringBmp.AddTile(pTileData, tileDim*tileDim, currX*tileDim, currY*tileDim, tileDim, tileDim);

		if( ++currX >= numTilesInWidth )
		{
			currX = 0;
			++currY;
		}
	}

	sakuraStringBmp.WriteToFile(outPath, true);

	delete[] pIndices;

	return true;
}
