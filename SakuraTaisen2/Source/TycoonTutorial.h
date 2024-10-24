void ExtractTycoonTutorial(const std::string& inRootDirectory, const std::string& inOutputDirectory, const string& inPaletteFileName, const bool bInBmp)
{
	CreateDirectoryHelper(inOutputDirectory);

	FileData cardFile;
	if(!cardFile.InitializeFileData(inRootDirectory + "\\SAKURA3\\CARD_DAT.ALL"))
	{
		return;
	}

	const uint32 textTableOffset = 0x318000;
	uint32 numEntries = 0;
	cardFile.ReadData(textTableOffset, (char*)&numEntries, sizeof(numEntries), true);

	//Read in table containings offsets to each string
	uint32* pTextEntries = new uint32[numEntries];
	cardFile.ReadData(textTableOffset + 4, (char*)pTextEntries, sizeof(uint32)*numEntries, false);
	for(uint32 i = 0; i < numEntries; ++i)
	{
		SwapByteOrderInPlace((char*)&pTextEntries[i], 4);
	}

	//Create fontsheet
	SakuraFontSheet sakuraFontSheet;
	if (!sakuraFontSheet.CreateFontSheetFromData(cardFile.GetData() + 0x30e780 + 0x80, 0x9700, 16, 16, true))
	{
		return;
	}

	//Get the palette
	FileData paletteFile;
	if (!paletteFile.InitializeFileData(inPaletteFileName.c_str(), inPaletteFileName.c_str()))
	{
		return;
	}

	//Create 32bit palette data
	PaletteData paletteData;
	paletteData.CreateFrom15BitData(paletteFile.GetData(), paletteFile.GetDataSize());

	//Read in lines
	vector<SakuraString> lines;
	for (uint32 lineIndex = 0; lineIndex < numEntries; ++lineIndex)
	{
		SakuraString sakuraString;
		uint32 offset = pTextEntries[lineIndex] + textTableOffset;
		uint16 charIndex = *(uint16*)(cardFile.GetData() + offset);

		while(charIndex != (uint16)0xffff)
		{
			SwapByteOrderInPlace((char*)&charIndex, sizeof(charIndex));

			SakuraString::SakuraChar sakuraChar(charIndex);
			sakuraString.AddChar(charIndex);

			offset += 2;
			charIndex = *(uint16*)(cardFile.GetData() + offset);
		}

		lines.push_back(sakuraString);
	}

	// Dump out the dialog for each line
	const string extension = bInBmp ? ".bmp" : ".png";
	int stringIndex = 0;
	const int tileDim = 16;
	for (size_t lineIndex = 0; lineIndex < numEntries; ++lineIndex)
	{
		const SakuraString& sakuraString = lines[lineIndex];
		if (sakuraString.mChars.size() > 255)
		{
			continue;
		}

		const int numSakuraLines = sakuraString.GetNumberOfLines();

		BitmapSurface sakuraStringBmp;
		sakuraStringBmp.CreateSurface(SakuraString::MaxCharsPerLine * tileDim, tileDim * numSakuraLines, BitmapSurface::EBitsPerPixel::kBPP_4, paletteData.GetData(), paletteData.GetSize());

		int currRow = 0;
		int currCol = 0;
		for (size_t charIndex = sakuraString.mOffsetToStringData; charIndex < sakuraString.mChars.size(); ++charIndex)
		{
			const SakuraString::SakuraChar& sakuraChar = sakuraString.mChars[charIndex];

			if (sakuraChar.IsNewLine())
			{
				++currRow;
				currCol = 0;
				continue;
			}

			if (sakuraChar.mIndex == 0xFFFF || sakuraChar.mIndex > 255 * 4)
			{
				continue;
			}

			const char* pData = sakuraFontSheet.GetCharacterTile(sakuraChar);

			sakuraStringBmp.AddTile(pData, sakuraFontSheet.GetTileSizeInBytes(), currCol * 16, currRow * 16, tileDim, tileDim);

			++currCol;
		}

		const string bitmapName = inOutputDirectory + std::to_string(stringIndex + 1) + extension;
		sakuraStringBmp.WriteToFile(bitmapName, bInBmp);

		++stringIndex;
	}

	delete[] pTextEntries;

	CreateSpreadSheetForImages("TycoonTutorial", "TycoonTutorial\\PNG\\", inOutputDirectory, "TycoonTutorial.php");

	printf("Success\n");
}
