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

bool PatchTycoonTutorialRenderingCode(const string& inOriginalDirectory, const string& inPatchedDirectory)
{
	FileData originalData;
	if(!originalData.InitializeFileData(inOriginalDirectory + "SAKURA3\\0000DAIF.BIN"))
	{
		return false;
	}

	FileReadWriter patchedFile;
	if(!patchedFile.OpenFile(inPatchedDirectory + "SAKURA3\\0000DAIF.BIN"))
	{
		return false;
	}

	struct PatchingData
	{
		uint32 offset;
		uint16 newCode;
		uint16 originalCode;
	};

	PatchingData patchingData[] = 
	{
		{0x17f2a, 0x010c, 0x0210}, //Dimensions: 0601cf2a contains 0210

		//Multiply by 6 (12/2) instead of 16 to get to correct character within sprite sheet in VDP1 ram
		{0x17eea, 0xe106, 0x62e3}, //0601ceea mov 0x6, r1
		{0x17eec, 0x7202, 0x7208}, //0601ceec add 0x2, r2
		{0x17eee, 0x0147, 0x4408}, //0601ceee mulu.w r4,r1
		{0x17ef0, 0x041a, 0x4408}, //0601cef0 macl r4

		//X, Y spacing
		{0x180ba, 0x710c, 0x7110}, //0601d0ba add 0x10, r1 (Y spacing)
		{0x180c8, 0x7108, 0x7110}, //0601d0c6 add 0x10, r1 (X spacing)
	};

	const int numEntries = sizeof(patchingData) / sizeof(PatchingData);
	for(int i = 0; i < numEntries; ++i)
	{
		const PatchingData currEntry = patchingData[i];

		const uint16 originalByte = SwapByteOrder(*(uint16*)(originalData.GetData() + currEntry.offset));
		if(originalByte != currEntry.originalCode)
		{
			printf("In 0000DAIF.BIN at 0x%08x, expecting 0x%04x but found 0x%04x\n", currEntry.offset, currEntry.originalCode, originalByte);
			return false;
		}

		patchedFile.WriteData(currEntry.offset, (char*)&currEntry.newCode, sizeof(currEntry.newCode), true);
	}

	return true;
}

bool PatchTycoonTutorial(const string& inOriginalDirectory, const string& inPatchedDirectory, const string& inDataDirectory, const TileExtractor& inFontSheet)
{
	printf("Patching Tycoon Tutorial\n");

	if(!PatchTycoonTutorialRenderingCode(inOriginalDirectory, inPatchedDirectory))
	{
		return false;
	}

	//Write out new font sheet
	FileReadWriter cardFile;
	if (!cardFile.OpenFile(inPatchedDirectory + "\\SAKURA3\\CARD_DAT.ALL"))
	{
		return false;
	}

	inFontSheet.OutputTiles(cardFile, -1, 0x30e780 + 0x80);

	return true;
}