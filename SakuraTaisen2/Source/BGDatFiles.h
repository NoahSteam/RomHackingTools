#pragma once

void ExtractBGDatFiles(const string& InRootSakuraDirectory, const string& InOutDirectory)
{
	CreateDirectoryHelper(InOutDirectory);

	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(InRootSakuraDirectory + "SAKURA2", allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> datFiles;
	GetAllFilesOfType(allFiles, ".DAT", datFiles);

	const string bmpExt(".bmp");

	const int numFiles = (int)datFiles.size();
	for(int i = 0; i < numFiles; ++i)
	{
		if(datFiles[i].mNoExtension.size() < 3 || (datFiles[i].mNoExtension[0] != 'B' && datFiles[i].mNoExtension[1] != 'G'))
		{
			continue;
		}

		FileData fileData;
		if(!fileData.InitializeFileData(datFiles[i]))
		{
			continue;
		}

		const char* pColorData = fileData.GetData() + 0x710;
		const char* pPaletteData = fileData.GetData() + 0x510;
		const string outFileName = InOutDirectory + datFiles[i].mNoExtension + bmpExt;

		const int imageWidth = 288;
		const int imageHeight = 144;
		const int numBytesPerTile = 8*8;
		const int numTiles = (imageWidth / 8) * (imageHeight / 8);
		SakuraFontSheet tileSheet;
		if (!tileSheet.CreateFontSheetFromData(pColorData, numTiles * numBytesPerTile, 8, 8, false))
		{
			return;
		}

		//Create 32bit palette data
		PaletteData paletteData;
		paletteData.CreateFrom15BitData(pPaletteData, 512);

		BitmapSurface sakuraStringBmp;
		sakuraStringBmp.CreateSurface(imageWidth, imageHeight, BitmapSurface::EBitsPerPixel::kBPP_8, paletteData.GetData(), paletteData.GetSize());

		uint16* pTiles = new uint16[numTiles];
		const unsigned int tileDataSize = sizeof(pTiles[0]) * numTiles;
		memcpy_s(pTiles, tileDataSize, fileData.GetData(), tileDataSize);

		//Convert tile index into game format
		for (int t = 0; t < numTiles; ++t)
		{
			const int tileValue = (SwapByteOrder<uint16>(pTiles[t]) / 2);
			pTiles[t] = tileValue;
		}

		//Create imate from tiles
		int x = 0;
		int y = 0;
		for (int t = 0; t < numTiles; ++t)
		{
			SakuraString::SakuraChar sakuraChar;
			sakuraChar.mIndex = pTiles[t];

			const char* pTileData = tileSheet.GetCharacterTile(sakuraChar);
			sakuraStringBmp.AddTile(pTileData, numBytesPerTile, x, y, 8, 8, BitmapSurface::kFlipNone);

			x += 8;
			if (x == imageWidth)
			{
				x = 0;
				y += 8;
			}
		}

		sakuraStringBmp.WriteToFile(outFileName, true);

		delete[] pTiles;
	}
}

bool PatchBGDatFiles(const string& InPatchedSakuraDirectory, const string& InTranslatedDataDirectory)
{
	printf("Patching BGDatFiles\n");

	vector<FileNameContainer> bossImages;
	FindAllFilesWithinDirectory(InTranslatedDataDirectory + "BossImages", bossImages);

	const int imageWidth = 288;
	const int imageHeight = 144;
	const int numBytesPerTile = 8 * 8;
	const int numTiles = (imageWidth / 8) * (imageHeight / 8);

	const int numFiles = (int)bossImages.size();
	for (int i = 0; i < numFiles; ++i)
	{
		const string bgFilePath = InPatchedSakuraDirectory + string("SAKURA2\\") + bossImages[i].mNoExtension + string(".DAT");

		PatchTiledImage<uint16>(bossImages[i].mFullPath, bgFilePath, imageWidth*imageHeight, 0x710,	0, numTiles, false);
	}

	return true;
}
