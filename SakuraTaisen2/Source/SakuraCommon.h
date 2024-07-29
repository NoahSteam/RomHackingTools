#pragma once

struct PatchingData
{
	union Command
	{
		uint32 longWord;
		uint16 word;
		uint8 byte;
	};

	uint32 address;
	Command command;
	Command originalCommand;
};

struct PatchingEntry
{
	const char* pFileName;
	PatchingData data;
};

bool PatchCodeInFile(std::string& InFileName, PatchingData* pInPatchingData, int InNumEntries)
{
	FileReadWriter sakuraBin;
	if (!sakuraBin.OpenFile(InFileName))
	{
		return false;
	}

	for (int i = 0; i < InNumEntries; ++i)
	{
		const bool bIsLongWord = (pInPatchingData[i].command.longWord) > 0xffff;
		if (bIsLongWord)
		{
			unsigned int originalValue = 0;
			sakuraBin.ReadData(pInPatchingData[i].address, (char*)&originalValue, sizeof(originalValue), true);
			if (originalValue != pInPatchingData[i].originalCommand.longWord)
			{
				printf("In %s at 0x%08x, expecting 0x%08x but found 0x%08x\n", InFileName.c_str(), pInPatchingData[i].address, pInPatchingData[i].originalCommand.longWord, originalValue);
				return false;
			}

			sakuraBin.WriteData(pInPatchingData[i].address, (char*)&pInPatchingData[i].command.longWord, 4, true);
			continue;
		}

		const bool bIsWord = (pInPatchingData[i].command.word & 0xff00) != 0 || pInPatchingData[i].command.byte == 0x09 || pInPatchingData[i].originalCommand.word == 0xffff;
		if (bIsWord)
		{
			unsigned short originalValue = 0;
			sakuraBin.ReadData(pInPatchingData[i].address, (char*)&originalValue, sizeof(originalValue), true);
			if (originalValue != pInPatchingData[i].originalCommand.word)
			{
				printf("In %s at 0x%08x, expecting 0x%04x but found 0x%04x\n", InFileName.c_str(), pInPatchingData[i].address, pInPatchingData[i].originalCommand.word, originalValue);
				return false;
			}

			sakuraBin.WriteData(pInPatchingData[i].address, (char*)&pInPatchingData[i].command.word, 2, true);
		}
		else
		{
			unsigned char originalValue = 0;
			sakuraBin.ReadData(pInPatchingData[i].address + 1, (char*)&originalValue, sizeof(originalValue), true);
			if (originalValue != pInPatchingData[i].originalCommand.byte)
			{
				printf("In %s at 0x%08x, expecting 0x%01x but found 0x%01x\n", InFileName.c_str(), pInPatchingData[i].address, pInPatchingData[i].originalCommand.byte, originalValue);
				return false;
			}

			sakuraBin.WriteData(pInPatchingData[i].address + 1, (char*)&pInPatchingData[i].command.byte, 1, false);
		}
	}

	return true;
}

enum class ETileIndiceType : uint8
{
	DoubleMinusOne,
	MinusOne
};
template<typename TileByteType>
void ExtractImageWithTileSet(const std::string& inTileFilePath, uint32 inWidth, uint32 inHeight, const std::string& inColFilePath,
							 uint32 inTileOffset, uint32 inColorOffset, uint32 inPaletteOffset, const std::string& inOutFileName, 
							 const bool bInBmp, int inBitCount = 8, ETileIndiceType inIndiceType = ETileIndiceType::DoubleMinusOne)
{
	FileData tileFile;
	if (!tileFile.InitializeFileData(inTileFilePath))
	{
		return;
	}

	FileData colorFile;
	if (!colorFile.InitializeFileData(inColFilePath))
	{
		return;
	}

	const int tileDim = 8;
	const int bytesInTile = tileDim * tileDim;
	const uint32 numColumns = inWidth / 8;
	const uint32 numRows = inHeight / 8;
	const int fontSheetDataSize = inBitCount == 4 ? (numColumns * numRows * bytesInTile)/2 : (numColumns*numRows * bytesInTile);
	SakuraFontSheet tileSheet;
	if (!tileSheet.CreateFontSheetFromData(tileFile.GetData() + inColorOffset, fontSheetDataSize, tileDim, tileDim, inBitCount == 4))
	{
		return;
	}

	//Create 32bit palette data
	PaletteData paletteData;
	paletteData.CreateFrom15BitData(colorFile.GetData() + inPaletteOffset, inBitCount == 4 ? 32 : 512);

	const BitmapSurface::EBitsPerPixel bitCount = inBitCount == 4 ? BitmapSurface::EBitsPerPixel::kBPP_4 : BitmapSurface::EBitsPerPixel::kBPP_8;
	BitmapSurface sakuraStringBmp;
	sakuraStringBmp.CreateSurface(inWidth, inHeight, bitCount, paletteData.GetData(), paletteData.GetSize());

	const uint32 numTiles = numColumns * numRows;
	TileByteType* tiles = new TileByteType[numTiles];
	memcpy_s(tiles, sizeof(tiles[0])*numTiles, tileFile.GetData() + inTileOffset, sizeof(tiles[0])*numTiles);

	//Convert tile index into game format
	if( inIndiceType == ETileIndiceType::DoubleMinusOne )
	{		
		for (int t = 0; t < numTiles; ++t)
		{
			const int tileValue = (SwapByteOrder<TileByteType>(tiles[t]) / 2) - 1;
			tiles[t] = tileValue;
		}
	}
	else
	{
		for (int t = 0; t < numTiles; ++t)
		{
			const int tileValue = SwapByteOrder<TileByteType>(tiles[t]) - 1;
			tiles[t] = tileValue;
		}
	}

	//Output tiles
	int x = 0;
	int y = 0;
	for (int t = 0; t < numTiles; ++t)
	{
		SakuraString::SakuraChar sakuraChar;
		sakuraChar.mIndex = tiles[t];

		const char* pTileData = tileSheet.GetCharacterTile(sakuraChar);
		sakuraStringBmp.AddTile(pTileData, 64, x, y, 8, 8, BitmapSurface::kFlipNone);

		x += 8;
		if (x == inWidth)
		{
			x = 0;
			y += 8;
		}
	}

	sakuraStringBmp.WriteToFile(inOutFileName, true);

	delete[] tiles;
}

template<typename TileByteType>
void ExtractTiledFullScreenImage(const std::string& inTileFilePath, const std::string& inColFilePath, uint32 inTileOffset, uint32 inColorOffset, 
						uint32 inPaletteOffset, const std::string& inOutFileName, const bool bInBmp, int inBitCount = 8)
{
	ExtractImageWithTileSet<TileByteType>(inTileFilePath, 320, 224, inColFilePath, inTileOffset, inColorOffset, inPaletteOffset, 
										  inOutFileName, bInBmp, inBitCount);
}

template<typename TileByteType>
bool PatchTiledImage(const std::string& InPatchedImagePath, const std::string InSakuraFilePath, const uint32 InColorDataSize, const int InColorDataOffset,
                     const int InTileDataOffset, const int InNumIndices = 1120, bool bInOffsetTileByOne = true)
{
	const uint32 tileDim = 8;
	BmpToSaturnConverter patchedImage;
	if (!patchedImage.ConvertBmpToSakuraFormat(InPatchedImagePath, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim))
	{
		return false;
	}

	FileReadWriter outputFile;
	if(!outputFile.OpenFile(InSakuraFilePath))
	{
		return false;
	}

	TileSetOptimizer optimizedTileSet;
	optimizedTileSet.OptimizeTileSet(patchedImage);
	optimizedTileSet.PackTiles();

	if (optimizedTileSet.GetPackedTileSize() > InColorDataSize)
	{
		printf("TileSet is too big for %s\n", InPatchedImagePath.c_str());
		return false;
	}
	outputFile.WriteData(InColorDataOffset, optimizedTileSet.GetPackedTiles(), optimizedTileSet.GetPackedTileSize());

	const std::vector<int>& tileIndices = optimizedTileSet.GetTileIndices();
	const size_t numIndices = tileIndices.size();
	assert(numIndices == InNumIndices);

	TileByteType* pVdp2Indices = new TileByteType[numIndices];
	const int offset = bInOffsetTileByOne ? 1 : 0;
	for (size_t i = 0; i < numIndices; ++i)
	{
		const TileByteType indice = TileByteType((tileIndices[i] + offset) * 2);
		pVdp2Indices[i] = SwapByteOrder(indice);
	}
	
	outputFile.WriteData(InTileDataOffset, (char*)pVdp2Indices, sizeof(pVdp2Indices[0]) * numIndices, false);

	delete[] pVdp2Indices;

	return true;
}