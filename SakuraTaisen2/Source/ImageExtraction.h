#pragma once

bool ExtractImageFromData(const char* pInColorData, const unsigned int inColorDataSize, const string& outFileName, const char* pInPaletteData, const unsigned int inPaletteDataSize, const int inTextureDimX, const int inTextureDimY,
	const int inNumTexturesPerRow, const int inNumColors = 256, const int inDataOffset = 0, bool bInFillEmptyData = true, bool bForceBitmapFormat = false)
{
	const int divisor = inPaletteDataSize == 32 ? 2 : 1; //4bit images only have half the pixels
	const int tileDimX = inTextureDimX;
	const int tileDimY = inTextureDimY;
	const int tileBytes = (tileDimX * tileDimY) / divisor; //4bits per pixel, so only half the amount of bytes as pixels
	const int tilesPerRow = inNumTexturesPerRow;
	const int bytesPerTile = tileBytes;
	const int bytesPerTileRow = bytesPerTile * tilesPerRow;
	const unsigned long dataSize = bInFillEmptyData ? inColorDataSize - inDataOffset : (inTextureDimX * inTextureDimY * inNumTexturesPerRow) / divisor;
	const int numRows = (int)ceil(dataSize / (float)bytesPerTileRow);
	const int numColumns = numRows > 0 ? tilesPerRow : dataSize / bytesPerTileRow;
	const int imageHeight = numRows * tileDimY;
	const int imageWidth = numColumns * tileDimX;

	//Create 32bit palette from the 16 bit(5:5:5 bgr) palette in SakuraTaisen
	PaletteData paletteData;
	paletteData.CreateFrom15BitData(pInPaletteData, inPaletteDataSize);

	//Allocate space for tiled data
	int numTiles = dataSize / tileBytes;
	int currTileRow = 0;
	int currTileCol = 0;
	const int bytesInEachTilesWidth = tileDimX / divisor;
	const int numTiledBytes = (numRows * bytesPerTileRow);// + numColumns*bytesInEachTilesWidth;
	char* pOutTiledData = new char[numTiledBytes];
	const int bytesPerHorizontalLine = bytesInEachTilesWidth * numColumns;

	memset(pOutTiledData, 0, numTiledBytes);

	//In Mode 3, the image only has 128 colors so only the 7 lower bits are used. 0x7f = 01111111
	const unsigned char bitMask = inNumColors == 128 ? 0x7f : 0xff;

	//Fill in data
	for (int tileIndex = 0; tileIndex < numTiles; ++tileIndex)
	{
		const char* pTile = pInColorData + inDataOffset + tileIndex * tileBytes;
		const int outTileOffset = currTileRow * bytesPerHorizontalLine * tileDimY + currTileCol * bytesInEachTilesWidth;
		char* pOutTile = pOutTiledData + outTileOffset;
		int tilePixel = 0;
		for (int r = 0; r < tileDimY; ++r)
		{
			for (int c = 0; c < bytesInEachTilesWidth; ++c)
			{
				assert(outTileOffset + r * bytesPerHorizontalLine + c < numTiledBytes);
				pOutTile[r * bytesPerHorizontalLine + c] = pTile[tilePixel++] & bitMask;
			}
		}

		if (++currTileCol >= numColumns)
		{
			currTileCol = 0;
			++currTileRow;
		}
	}

	BitmapWriter fontBitmap;
	fontBitmap.CreateBitmap(outFileName, imageWidth, -imageHeight, inPaletteDataSize == 32 ? 4 : 8, pOutTiledData, numTiledBytes, paletteData.GetData(), paletteData.GetSize(), bForceBitmapFormat);

	delete[] pOutTiledData;

	return true;
}

bool ExtractImage(const FileNameContainer& inFileNameContainer, const string& outFileName, const FileData& inPaletteFile, const int inTextureDimX, const int inTextureDimY, const int inNumTexturesPerRow, const int inNumColors = 256,
	const int inDataOffset = 0, bool bInFillEmptyData = true, bool bForceBitmap = false)
{
	FileData fontSheet;
	if (!fontSheet.InitializeFileData(inFileNameContainer))
	{
		return false;
	}

	printf("Extracting: %s\n", inFileNameContainer.mFileName.c_str());

	return ExtractImageFromData(fontSheet.GetData(), fontSheet.GetDataSize(), outFileName, inPaletteFile.GetData(), inPaletteFile.GetDataSize(), inTextureDimX, inTextureDimY, inNumTexturesPerRow,
		inNumColors, inDataOffset, bInFillEmptyData, bForceBitmap);
}

bool ExtractFontSheetAsBitmap(const FileNameContainer& inFileNameContainer, const string& outFileName, const FileData& inPaletteFile)
{
	//	return ExtractImage(inFileNameContainer, outFileName, inPaletteFile, 8, 12, 255, 16, 0, false, true);
	return ExtractImage(inFileNameContainer, outFileName, inPaletteFile, 16, 16, 255);
}
