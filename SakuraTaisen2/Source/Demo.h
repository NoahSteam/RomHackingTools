#pragma once

struct WPAllInfo
{
	bool bIsWpall;
	unsigned int colorOffset;
	unsigned int colorSize;
	unsigned int tileOffset;
	unsigned int paletteOffset;
};

const WPAllInfo TiledLoadScreenImages[3] =
{
	//	{true,  0x40,    0xfc00,  0x10000, 0x11800},
	//	{true,  0x12040, 0x11480, 0x23800, 0x25000},
	//	{true,  0x25840, 0xf500,  0x35000, 0x36800},
		{false, 0x40,    44992,  0xa800, 0xc000},
		{false, 0xc840,  44992,  0x17800, 0x19000},
		{false, 0x19840, 44992,  0x24800, 0x26000}
};

void ExtractDemoWPALL(const std::string& inDemoDirectory, const std::string& inOutputDirectory)
{
	CreateDirectoryHelper(inOutputDirectory);

	//Coming soon
	if(0)
	{
		BmpToSaturnConverter patchedImageData;
		unsigned int tileDim = 8;
		if (!patchedImageData.ConvertBmpToSakuraFormat(inDemoDirectory + "ComingSoon.bmp", false, 0, &tileDim, &tileDim) )
		{
			return;
		}
		patchedImageData.PackTiles();
		FileWriter outData;
		outData.OpenFileForWrite((inOutputDirectory + "ComingSoon.bin"));
		outData.WriteData(patchedImageData.mpPackedTiles, patchedImageData.mPackedTileSize, false);
	}

	//N_Load
	{
		const std::string nLoadPath = inDemoDirectory + "N_LOAD.ALL";
		FileData nLoadData;
		if( !nLoadData.InitializeFileData(nLoadPath) )
		{
			return;
		}

		const std::string wpallPath = inDemoDirectory + "WPALL1.ALL";
		FileData wpallData;
		if( !wpallData.InitializeFileData(wpallPath) )
		{
			return;
		}

		for( int i = 0; i < 3; ++i )
		{
			FileData* pFileData = TiledLoadScreenImages[i].bIsWpall ? &wpallData : &nLoadData;

			SakuraFontSheet tileSheet;
			if( !tileSheet.CreateFontSheetFromData(pFileData->GetData() + TiledLoadScreenImages[i].colorOffset, TiledLoadScreenImages[i].colorSize, 8, 8, false) )
			{
				return;
			}

			//Create 32bit palette data
			PaletteData paletteData;
			paletteData.CreateFrom15BitData(pFileData->GetData() + TiledLoadScreenImages[i].paletteOffset, 512);

			BitmapSurface sakuraStringBmp;
			sakuraStringBmp.CreateSurface(320, 224, BitmapSurface::EBitsPerPixel::kBPP_8, paletteData.GetData(), paletteData.GetSize());

			const string bitmapFileName = inOutputDirectory + std::string("TileSheet") + std::to_string(i) + ".bmp";
			ExtractImageFromData(pFileData->GetData() + TiledLoadScreenImages[i].colorOffset, 8 * 8 * (320 / 8) * (224 / 8), bitmapFileName, pFileData->GetData() + TiledLoadScreenImages[i].paletteOffset, 512, false, 8, 8, 320 / 8, 256, 0, true, true);

			unsigned int tiles[1120];
			memcpy_s(tiles, sizeof(tiles), pFileData->GetData() + TiledLoadScreenImages[i].tileOffset, sizeof(tiles));
			int x = 0;
			int y = 0;

			for (int t = 0; t < 1120; ++t)
			{
				const int tileValue = (SwapByteOrder(tiles[t])/2) - 1;
				tiles[t] = tileValue;
			}

			for( int t = 0; t < 1120; ++t )
			{
				SakuraString::SakuraChar sakuraChar;
				sakuraChar.mIndex = tiles[t];

				const char* pTileData = tileSheet.GetCharacterTile(sakuraChar);
				sakuraStringBmp.AddTile(pTileData, 64, x, y, 8, 8, BitmapSurface::kFlipNone);

				x += 8;
				if( x == 320 )
				{
					x = 0;
					y += 8;
				}
			}

			std::string outFile;
			if(TiledLoadScreenImages[i].bIsWpall )
			{
				outFile = inOutputDirectory + "wpall_" + std::to_string(i) + ".bmp";
			}
			else
			{
				outFile = inOutputDirectory + "nload_" + std::to_string(i) + ".bmp";
			}
			
			sakuraStringBmp.WriteToFile(outFile, true);
		}
	}
}

void ExtractLoadScreens(const std::string& inDemoDirectory, const std::string& inOutputDirectory)
{
	CreateDirectoryHelper(inOutputDirectory);

	//N_Load
	{
		const std::string nLoadPath = inDemoDirectory + "N_LOAD.ALL";
		FileData nLoadData;
		if (!nLoadData.InitializeFileData(nLoadPath))
		{
			return;
		}

		const std::string wpallPath = inDemoDirectory + "WPALL1.ALL";
		FileData wpallData;
		if (!wpallData.InitializeFileData(wpallPath))
		{
			return;
		}

		for (int i = 0; i < 3; ++i)
		{
			FileData* pFileData = TiledLoadScreenImages[i].bIsWpall ? &wpallData : &nLoadData;

			SakuraFontSheet tileSheet;
			if (!tileSheet.CreateFontSheetFromData(pFileData->GetData() + TiledLoadScreenImages[i].colorOffset, TiledLoadScreenImages[i].colorSize, 8, 8, false))
			{
				return;
			}

			//Create 32bit palette data
			PaletteData paletteData;
			paletteData.CreateFrom15BitData(pFileData->GetData() + TiledLoadScreenImages[i].paletteOffset, 512);

			BitmapSurface sakuraStringBmp;
			sakuraStringBmp.CreateSurface(320, 224, BitmapSurface::EBitsPerPixel::kBPP_8, paletteData.GetData(), paletteData.GetSize());

			const string bitmapFileName = inOutputDirectory + std::string("TileSheet") + std::to_string(i) + ".bmp";
			ExtractImageFromData(pFileData->GetData() + TiledLoadScreenImages[i].colorOffset, 8 * 8 * (320 / 8) * (224 / 8), bitmapFileName, pFileData->GetData() + TiledLoadScreenImages[i].paletteOffset, 512, false, 8, 8, 320 / 8, 256, 0, true, true);

			unsigned int tiles[1120];
			memcpy_s(tiles, sizeof(tiles), pFileData->GetData() + TiledLoadScreenImages[i].tileOffset, sizeof(tiles));
			int x = 0;
			int y = 0;

			for (int t = 0; t < 1120; ++t)
			{
				const int tileValue = (SwapByteOrder(tiles[t]) / 2) - 1;
				tiles[t] = tileValue;
			}

			for (int t = 0; t < 1120; ++t)
			{
				SakuraString::SakuraChar sakuraChar;
				sakuraChar.mIndex = tiles[t];

				const char* pTileData = tileSheet.GetCharacterTile(sakuraChar);
				sakuraStringBmp.AddTile(pTileData, 64, x, y, 8, 8, BitmapSurface::kFlipNone);

				x += 8;
				if (x == 320)
				{
					x = 0;
					y += 8;
				}
			}

			std::string outFile;
			if (TiledLoadScreenImages[i].bIsWpall)
			{
				outFile = inOutputDirectory + "wpall_" + std::to_string(i) + ".bmp";
			}
			else
			{
				outFile = inOutputDirectory + "nload_" + std::to_string(i) + ".bmp";
			}

			sakuraStringBmp.WriteToFile(outFile, true);
		}
	}
}


bool PatchLoadScreens(const std::string& inPatchedSakuraDirectory, const std::string& inTranslatedDataDirectory)
{
	const std::string nLoadPath = inPatchedSakuraDirectory + "SAKURA1\\N_LOAD.ALL";
	FileReadWriter nLoadData;
	if (!nLoadData.OpenFile(nLoadPath))
	{
		return false;
	}

	for (int i = 0; i < 3; ++i)
	{
		const std::string patchedImagePath = inTranslatedDataDirectory + std::string("N_Load\\") + std::to_string(i) + std::string(".bmp");
		const uint32 tileDim = 8;
		BmpToSaturnConverter patchedImage;
		if (!patchedImage.ConvertBmpToSakuraFormat(patchedImagePath, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim))
		{
			return false;
		}

		TileSetOptimizer optimizedTileSet;
		optimizedTileSet.OptimizeTileSet(patchedImage);
		optimizedTileSet.PackTiles();

		if (optimizedTileSet.GetPackedTileSize() > TiledLoadScreenImages[i].colorSize)
		{
			printf("TileSet is too big for %s", patchedImagePath.c_str());
			return false;
		}
		nLoadData.WriteData(TiledLoadScreenImages[i].colorOffset, optimizedTileSet.GetPackedTiles(), optimizedTileSet.GetPackedTileSize());

		const std::vector<int>& tileIndices = optimizedTileSet.GetTileIndices();
		const size_t numIndices = tileIndices.size();
		const size_t expectedIndices = 1120;
		assert(numIndices == expectedIndices);

		int vdp2Indices[expectedIndices];
		for (size_t i = 0; i < numIndices; ++i)
		{
			const int indice = (tileIndices[i] + 1) * 2;
			vdp2Indices[i] = SwapByteOrder(indice);
		}
		nLoadData.WriteData(TiledLoadScreenImages[i].tileOffset, (char*)vdp2Indices, sizeof(vdp2Indices[0]) * expectedIndices, false);
	}

	return true;
}