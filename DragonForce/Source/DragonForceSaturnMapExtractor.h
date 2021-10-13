#pragma once

struct DragonForceMapTileIndex
{
	uint16 pattern1;
	uint16 pattern2;
	
	void ToBigEndian()
	{
		static const uint16 mask = 0xffff;// >> 1;
		pattern1 = SwapByteOrder(pattern1);
		uint16 newPattern2 = SwapByteOrder(pattern2) & mask;

		if(newPattern2 % 2 )
		{
			newPattern2--;
		}

		pattern2 = newPattern2 >> 1;
	}

	BitmapSurface::EFlipFlag GetFlipFlag() const
	{
		BitmapSurface::EFlipFlag flipFlag = BitmapSurface::kFlipNone;

		if (pattern1 & (uint16)(1 << 15))
		{
			flipFlag = BitmapSurface::kFlipVert;
		}

		if (pattern1 & (uint16)(1 << 14))
		{
			flipFlag = (BitmapSurface::EFlipFlag)((uint8)flipFlag | BitmapSurface::kFlipHoriz);
		}

		return flipFlag;
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

	const int numCells = 0x40000 / 0x40;
	const int cellDim = 8;
	TileMap cells;
	if (!cells.CreateFontSheetFromData(colorData.GetData(), cellDim * cellDim * numCells))
	{
		return false;
	}

	//Allocate in indices
	const int numIndices = 64 * 64;
	DragonForceMapTileIndex* pIndices = new DragonForceMapTileIndex[numIndices];
	const int dataSize = sizeof(DragonForceMapTileIndex) * numIndices;

	const uint8 NumPlanes = 16;
	const uint32 planeOffsets[NumPlanes] = 
	{
		0x00000, //A
		0x00000, //B
		0x04000, //C
		0x00000, //D
		0x08000, //E
		0x0C000, //F
		0x10000, //G
		0x14000, //H
		0x00000, //I
		0x18000, //J
		0x1C000, //K
		0x00000, //L
		0x00000, //M
		0x00000, //N
		0x00000, //O
		0x00000, //P
	}; 

	const int tileDim = 64;
	const int planeDim = 4;

	BitmapSurface singleImage;

	//To traverse planes
	const int numPlanesPerRow = 4;
	const int planeStride = tileDim*cellDim;
	int currPlaneX = 0;
	int currPlaneY = 0;
	int currPlaneOffsetX = 0;
	int currPlaneOffsetY = 0;

	bool bAsSingleImage = true;
	for(int planeIndex = 0; planeIndex < NumPlanes; ++planeIndex)
	{
		//Read in indices
		memcpy_s(pIndices, dataSize, tileIndiceData.GetData() + planeOffsets[planeIndex], dataSize);
		for (int tileIndex = 0; tileIndex < numIndices; ++tileIndex)
		{
			pIndices[tileIndex].ToBigEndian();
		}

		BitmapSurface mapImage;
		BitmapSurface* pMapImage = bAsSingleImage ? &singleImage : &mapImage;
		if( bAsSingleImage )
		{
			if( planeIndex == 0 )
			{
				singleImage.CreateSurface(planeStride * numPlanesPerRow, planeStride * planeDim, BitmapSurface::EBitsPerPixel::kBPP_8, paletteData.GetData(), paletteData.GetSize());
			}
		}
		else
		{
			mapImage.CreateSurface(cellDim * tileDim, cellDim * tileDim, BitmapSurface::EBitsPerPixel::kBPP_8, paletteData.GetData(), paletteData.GetSize());
		}

		int currX = 0;
		int currY = 0;
		for (int indiceIndex = 0; indiceIndex < numIndices; ++indiceIndex)
		{
			const int cellIndex = pIndices[indiceIndex].pattern2;

			if (cellIndex < 0 || cellIndex > numCells)
			{
				break;
			}

			const char* pTileData = cells.GetTileData(cellIndex);
			pMapImage->AddTile(pTileData, cellDim * cellDim, currX * cellDim + currPlaneOffsetX, currY * cellDim + currPlaneOffsetY, cellDim, cellDim, pIndices[indiceIndex].GetFlipFlag());

			if (++currX >= tileDim)
			{
				currX = 0;
				++currY;
			}
		}

		if( bAsSingleImage )
		{
			if( ++currPlaneX >= 4 )
			{
				currPlaneX = 0;
				++currPlaneY;

				currPlaneOffsetY = planeStride * currPlaneY;
			}

			currPlaneOffsetX = currPlaneX * planeStride;
		}

		//Write plane
		if( !bAsSingleImage )
		{
			const string outFileName = outPath + string("Plane_") + std::to_string(planeIndex) + string(".bmp");
			pMapImage->WriteToFile(outFileName, true);
		}
	}

	//Write plane
	if (bAsSingleImage)
	{
		const string outFileName = outPath + string("Map.bmp");
		singleImage.WriteToFile(outFileName, true);
	}

	delete[] pIndices;

	return true;
}
