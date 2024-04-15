#pragma once

struct WPAllInfo
{
	bool bIsWpall;
	unsigned int colorOffset;
	unsigned int colorSize;
	unsigned int tileOffset;
	unsigned int paletteOffset;
};

const WPAllInfo DemoTiledLoadScreenImages[6] =
{
		{true,  0x40,    0xfc00,  0x10000, 0x11800},
		{true,  0x12040, 0x11480, 0x23800, 0x25000},
		{true,  0x25840, 0xf500,  0x35000, 0x36800},
		{false, 0x40,    44992,  0xa800, 0xc000},
		{false, 0xc840,  44992,  0x17800, 0x19000},
		{false, 0x19840, 44992,  0x24800, 0x26000}
};

struct NLoadInfo
{
	unsigned int colorOffset;
	unsigned int colorSize;
	unsigned int tileOffset;
	unsigned int paletteOffset;
};
const NLoadInfo TiledLoadScreenImages[3] =
{
	{0x40,    44992,  0xa800, 0xc000},
	{0xc840,  44992,  0x17800, 0x19000},
	{0x19840, 44992,  0x24800, 0x26000}
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

		for( int i = 0; i < 6; ++i )
		{
			FileData* pFileData = DemoTiledLoadScreenImages[i].bIsWpall ? &wpallData : &nLoadData;

			SakuraFontSheet tileSheet;
			if( !tileSheet.CreateFontSheetFromData(pFileData->GetData() + DemoTiledLoadScreenImages[i].colorOffset, DemoTiledLoadScreenImages[i].colorSize, 8, 8, false) )
			{
				return;
			}

			//Create 32bit palette data
			PaletteData paletteData;
			paletteData.CreateFrom15BitData(pFileData->GetData() + DemoTiledLoadScreenImages[i].paletteOffset, 512);

			BitmapSurface sakuraStringBmp;
			sakuraStringBmp.CreateSurface(320, 224, BitmapSurface::EBitsPerPixel::kBPP_8, paletteData.GetData(), paletteData.GetSize());

			const string bitmapFileName = inOutputDirectory + std::string("TileSheet") + std::to_string(i) + ".bmp";
			ExtractImageFromData(pFileData->GetData() + DemoTiledLoadScreenImages[i].colorOffset, 8 * 8 * (320 / 8) * (224 / 8), bitmapFileName, pFileData->GetData() + TiledLoadScreenImages[i].paletteOffset, 512, false, 8, 8, 320 / 8, 256, 0, true, true);

			unsigned int tiles[1120];
			memcpy_s(tiles, sizeof(tiles), pFileData->GetData() + DemoTiledLoadScreenImages[i].tileOffset, sizeof(tiles));
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
			if(DemoTiledLoadScreenImages[i].bIsWpall)
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
	const std::string nLoadPath = inDemoDirectory + "N_LOAD.ALL";
	FileData nLoadData;
	if (!nLoadData.InitializeFileData(nLoadPath))
	{
		return;
	}

	const int numLoadImages = sizeof(TiledLoadScreenImages)/sizeof(TiledLoadScreenImages[0]);
	for (int i = 0; i < numLoadImages; ++i)
	{
		SakuraFontSheet tileSheet;
		if (!tileSheet.CreateFontSheetFromData(nLoadData.GetData() + TiledLoadScreenImages[i].colorOffset, TiledLoadScreenImages[i].colorSize, 8, 8, false))
		{
			return;
		}

		//Create 32bit palette data
		PaletteData paletteData;
		paletteData.CreateFrom15BitData(nLoadData.GetData() + TiledLoadScreenImages[i].paletteOffset, 512);

		BitmapSurface sakuraStringBmp;
		sakuraStringBmp.CreateSurface(320, 224, BitmapSurface::EBitsPerPixel::kBPP_8, paletteData.GetData(), paletteData.GetSize());

		const string bitmapFileName = inOutputDirectory + std::string("TileSheet") + std::to_string(i) + ".bmp";
		ExtractImageFromData(nLoadData.GetData() + TiledLoadScreenImages[i].colorOffset, 8 * 8 * (320 / 8) * (224 / 8), bitmapFileName, nLoadData.GetData() + TiledLoadScreenImages[i].paletteOffset, 512, false, 8, 8, 320 / 8, 256, 0, true, true);

		unsigned int tiles[1120];
		memcpy_s(tiles, sizeof(tiles), nLoadData.GetData() + TiledLoadScreenImages[i].tileOffset, sizeof(tiles));
		int x = 0;
		int y = 0;

		//Convert tile index into game format
		for (int t = 0; t < 1120; ++t)
		{
			const int tileValue = (SwapByteOrder(tiles[t]) / 2) - 1;
			tiles[t] = tileValue;
		}

		//Output tiles
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

		const std::string outFile = inOutputDirectory + "nload_" + std::to_string(i) + ".bmp";
		sakuraStringBmp.WriteToFile(outFile, true);			
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

	const int numLoadImages = sizeof(TiledLoadScreenImages) / sizeof(TiledLoadScreenImages[0]);
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

//Compressed tiled load screens displayed going into battles
//***Unique files for each disc***
void ExtractBattleLoadScreens(const std::string& inRootSakuraDirectory, const std::string& inOutputDirectory)
{
	CreateDirectoryHelper(inOutputDirectory);

	const std::string searchDir = inRootSakuraDirectory + "SAKURA2\\";
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(searchDir, allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> loadFiles;
	GetAllFilesOfType(allFiles, "LOAD", loadFiles);

	const int numFiles = (int)loadFiles.size();
	for(int i = 0; i < numFiles; ++i)
	{
		FileData fileData;
		if(!fileData.InitializeFileData(loadFiles[i], true))
		{
			continue;
		}

		PRSDecompressor decompressor;
		if (!decompressor.UncompressData((void*)fileData.GetData(), fileData.GetDataSize()))
		{
			printf("Unable to decompress %s\n", loadFiles[i].mFileName.c_str());
			continue;
		}

		const string bitmapFileName = inOutputDirectory + std::to_string(i) + ".bmp";
		ExtractImageFromData(decompressor.mpUncompressedData, decompressor.mUncompressedDataSize, bitmapFileName, fileData.GetData() + 0x8000, 512, false, 8, 8, 320/8, 256, 0, true, true);
	}
}

bool PatchBattleLoadScreens(const std::string& inPatchedSakuraDirectory, const std::string& inTranslatedDataDirectory, int inDiscNumber)
{
	const std::string searchDir = inPatchedSakuraDirectory + "SAKURA2\\";
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(searchDir, allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> loadFiles;
	GetAllFilesOfType(allFiles, "LOAD", loadFiles);
	
	const uint32 tileDim = 8;
	const std::string translatedImageDir = inTranslatedDataDirectory + std::string("BattleLoadScreens") + std::string("\\Disc") + std::to_string(inDiscNumber) + std::string("\\");
	const int numFiles = (int)loadFiles.size();
	for (int i = 0; i < numFiles; ++i)
	{
		std::string translatedImage = translatedImageDir + std::to_string(i) + ".bmp";

		//Convert image to sw2 format
		BmpToSaturnConverter patchedImageData;
		if (!patchedImageData.ConvertBmpToSakuraFormat(translatedImage, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim))
		{
			return false;
		}
		patchedImageData.PackTiles();

		//Compress data
		PRSCompressor compressor;
		compressor.CompressData((void*)patchedImageData.mpPackedTiles, patchedImageData.mPackedTileSize, PRSCompressor::kCompressOption_None);

		if (compressor.mCompressedSize >= 0x8000)
		{
			printf("%s is too large when compressed.  Should be less that %i, is %i.\n", loadFiles[i].mFileName.c_str(), 0x8000, compressor.mCompressedSize);
			return false;
		}

		//Write image data
		FileReadWriter loadFileWriter;
		if (!loadFileWriter.OpenFile(loadFiles[i].mFullPath))
		{
			return false;
		}

		loadFileWriter.WriteData(0, compressor.mpCompressedData, compressor.mCompressedSize, false);
	}

	return true;
}
