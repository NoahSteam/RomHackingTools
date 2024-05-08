#pragma once

bool ExtractImageFromData(const char* pInColorData, const unsigned int inColorDataSize, const string& outFileName, const char* pInPaletteData, 
	const unsigned int inPaletteDataSize, bool bOutput4BitImage, const int inTextureDimX, const int inTextureDimY,
	const int inNumTexturesPerRow, const int inNumColors = 256, const int inDataOffset = 0, bool bInFillEmptyData = true, bool bForceBitmapFormat = false,
	PaletteData* pInPreMadePalette = nullptr)
{
	const int divisor = bOutput4BitImage ? 2 : 1; //4bit images only have half the pixels
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
	PaletteData* pPaletteData = &paletteData;
	if (!pInPreMadePalette)
	{
		paletteData.CreateFrom15BitData(pInPaletteData, inPaletteDataSize);
	}
	else
	{
		pPaletteData = pInPreMadePalette;
	}

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
		const char* pTile       = pInColorData + inDataOffset + tileIndex * tileBytes;
		const int outTileOffset = currTileRow * bytesPerHorizontalLine * tileDimY + currTileCol * bytesInEachTilesWidth;
		char* pOutTile          = pOutTiledData + outTileOffset;
		int tilePixel           = 0;
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
	fontBitmap.CreateBitmap(outFileName, imageWidth, -imageHeight, bOutput4BitImage ? 4 : 8, pOutTiledData, numTiledBytes, pPaletteData->GetData(), pPaletteData->GetSize(), bForceBitmapFormat);
	
	delete[] pOutTiledData;

	return true;
}

bool ExtractImage(const FileNameContainer& inFileNameContainer, const string& outFileName, const FileData& inPaletteFile, const int inTextureDimX, 
	const int inTextureDimY, const int inNumTexturesPerRow, const int bInIs4BitImage, const int inNumColors = 256,
	const int inDataOffset = 0, bool bInFillEmptyData = true, bool bForceBitmap = false, PaletteData* pInPreMadePalette = nullptr)
{
	FileData fontSheet;
	if (!fontSheet.InitializeFileData(inFileNameContainer))
	{
		return false;
	}

	printf("Extracting: %s\n", inFileNameContainer.mFileName.c_str());

	return ExtractImageFromData(fontSheet.GetData(), fontSheet.GetDataSize(), outFileName, inPaletteFile.GetData(), inPaletteFile.GetDataSize(), bInIs4BitImage, 
								inTextureDimX, inTextureDimY, inNumTexturesPerRow,
								inNumColors, inDataOffset, bInFillEmptyData, bForceBitmap, pInPreMadePalette);
}

bool ExtractFontSheetAsBitmap(const FileNameContainer& inFileNameContainer, const string& outFileName, const FileData& inPaletteFile)
{
	const bool bOutput4BitImage = inPaletteFile.GetDataSize() == 32;
	//	return ExtractImage(inFileNameContainer, outFileName, inPaletteFile, 8, 12, 255, 16, 0, false, true);
	return ExtractImage(inFileNameContainer, outFileName, inPaletteFile, 16, 16, 255, bOutput4BitImage);
}

bool Extract16BitImageFromData(const string& inFileName, const char* pInColorData, const int inWidth, const int inHeight)
{
	const int bufferSize = inWidth * inHeight;
	uint16* pColorBuffer = new uint16[bufferSize];
	uint16* pSaturnColor = (uint16*)(pInColorData);

	for (int i = 0; i < bufferSize; i += 1)
	{
		unsigned short color = SwapByteOrder(pSaturnColor[i]);
		const uint8 r = (color & 0x7C00) >> 10;
		const uint8 g = (color & 0x3e0) >> 5;
		const uint8 b = (color & 0x1f);

		pColorBuffer[i] = (b << 10) + (g << 5) + r;
	}

	BitmapWriter bitmap;
	const bool bResult = bitmap.CreateBitmap(inFileName, inWidth, -inHeight, 16, (char*)pColorBuffer, bufferSize * 2, nullptr, 0, true);

	delete pColorBuffer;

	return bResult;
}
