#pragma once

bool ExtractVDP2Files(const std::string& InSakuraDirectory, const std::string& InOutputDirectory)
{
	CreateDirectoryHelper(InOutputDirectory);

	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(InSakuraDirectory, allFiles);

	vector<FileNameContainer> vdp2Files;
	GetAllFilesOfType(allFiles, "VDP2", vdp2Files);

	vector<FileNameContainer> colorFiles;
	GetAllFilesOfType(allFiles, "COL.BIN", colorFiles);

	const size_t numFiles = vdp2Files.size();
	if(numFiles != colorFiles.size())
	{
		printf("VDP2 and Color file count mismatch \n");
		return false;
	}

	const std::string bmpExt(".bmp");
	const unsigned int offsetToColorData = 0x40040;
	for(size_t i = 0; i < numFiles; ++i)
	{	
		FileData vdp2Data;
		if (!vdp2Data.InitializeFileData(vdp2Files[i]))
		{
			continue;
		}


		FileData paletteFile;
		if(!paletteFile.InitializeFileData(colorFiles[i]))
		{
			continue;
		}

		PaletteData paletteData;
		paletteData.CreateFrom15BitData(paletteFile.GetData(), 512);

		printf("Extracting %s\n", vdp2Files[i].mFileName.c_str());

		const std::string outFileName = InOutputDirectory + vdp2Files[i].mNoExtension + bmpExt;

		SakuraFontSheet sakuraFontSheet;
		if (!sakuraFontSheet.CreateFontSheetFromData(vdp2Data.GetData() + offsetToColorData, 320*224, 8, 8, false))
		{
			continue;
		}

		const int numTiles = (320/8) * (224/8);
		unsigned int tileOffsets[numTiles];
		vdp2Data.ReadData(0, (char*)tileOffsets, numTiles*sizeof(int), false);
		for(unsigned int& tileValue : tileOffsets)
		{
			tileValue = (SwapByteOrder(tileValue)/2) - 1;
		}

		BitmapSurface sakuraStringBmp;
		sakuraStringBmp.CreateSurface(320, 224, BitmapSurface::EBitsPerPixel::kBPP_8, paletteData.GetData(), paletteData.GetSize());

	//	const string bitmapFileName = InOutputDirectory + vdp2Files[i].mNoExtension + std::string("TileSheet") +  bmpExt;
	//	ExtractImageFromData(vdp2Data.GetData(), 8 * 8 * (320 / 8) * (224 / 8) + offsetToColorData, bitmapFileName, paletteFile.GetData(), 512, false, 8, 8, 320 / 8, 256, offsetToColorData, true, true);

		int currRow = 0;
		int currCol = 0;
		for (int charIndex = 0; charIndex < numTiles; ++charIndex)
		{
			SakuraString::SakuraChar sakuraChar;
			sakuraChar.mIndex = tileOffsets[charIndex];

			const char* pTileData = sakuraFontSheet.GetCharacterTile(sakuraChar);
			sakuraStringBmp.AddTile(pTileData, 64, currCol, currRow, 8, 8);

			currCol += 8;
			if(currCol == 320)
			{
				currCol = 0;
				currRow += 8;
			}
		}

		sakuraStringBmp.WriteToFile(outFileName);
	}

	return true;
}
