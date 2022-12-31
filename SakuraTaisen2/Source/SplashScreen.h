#pragma once

bool PatchSplashScreen(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	//Tiled version
	if(0) //Leave disabled until we actually run into this screen
	{
		const string cesaFilePath = inPatchedSakuraDirectory + string("SAKURA1\\CESALOGO.ALL");
		FileReadWriter cesaFile;
		if (!cesaFile.OpenFile(cesaFilePath))
		{
			return false;
		}

		//Tiled image
		const string imageDataPath = inTranslatedDataDirectory + string("SplashScreen\\CESALOGO.bmp");
		BmpToSaturnConverter patchedImageData;
		const unsigned int tileDim = 8;
		if (!patchedImageData.ConvertBmpToSakuraFormat(imageDataPath, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim))
		{
			printf("PatchSplashScreen: Couldn't convert image: %s.\n", imageDataPath.c_str());
			return false;
		}
		patchedImageData.PackTiles();

		const string tileCsvPath = inTranslatedDataDirectory + string("SplashScreen\\CesaLogoTileMap.csv");
		CsvTileReader tileData;
		if (!tileData.ReadInTiles(tileCsvPath))
		{
			return false;
		}

		if (tileData.mTileEntries.size() != (320 / 8) * (224 / 8))
		{
			printf("CesaLogoTileMap.csv does not have the expected number of tiles [40 * 28].  Found %zu instead.\n", tileData.mTileEntries.size());
			return false;
		}

		//Write image data
		cesaFile.WriteData(0xb020, patchedImageData.mpPackedTiles, patchedImageData.mPackedTileSize, false);

		//Write tile info
		tileData.SwapEndianess();
		cesaFile.WriteData(0xc000, (char*)tileData.mTileEntries.data(), tileData.mTileEntries.size() * sizeof(int));
	}

	//Fullscreen
	{
		const string titleFilePath = inPatchedSakuraDirectory + string("SAKURA1\\TITLE.BIN");
		FileReadWriter titleFile;
		if (!titleFile.OpenFile(titleFilePath))
		{
			return false;
		}

		//Fullscreen
		const string fullscreenImageDataPath = inTranslatedDataDirectory + string("SplashScreen\\CESALOGO_Fullscreen.bmp");
		BmpToSaturnConverter patchedFullscreenImageData;
		const unsigned int tileDim = 8;
		if (!patchedFullscreenImageData.ConvertBmpToSakuraFormat(fullscreenImageDataPath, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim))
		{
			printf("PatchSplashScreen: Couldn't convert image: %s.\n", fullscreenImageDataPath.c_str());
			return false;
		}
		patchedFullscreenImageData.PackTiles();

		PRSCompressor compressor;
		compressor.CompressData((void*)patchedFullscreenImageData.mpPackedTiles, patchedFullscreenImageData.mPackedTileSize, PRSCompressor::kCompressOption_None);
		if (compressor.mCompressedSize > 1599)
		{
			printf("CESALOGO.BMP is too large when compressed.  Should be less that 1599, is %i.\n", compressor.mCompressedSize);
			return false;
		}

		//Write image data
		titleFile.WriteData(0x23b80, compressor.mpCompressedData, compressor.mCompressedSize, false);
	}

	return true;
}