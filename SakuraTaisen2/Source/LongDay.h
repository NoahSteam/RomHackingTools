#pragma once

bool ExtractLongDayOptionsText(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	struct LongDayTextBlock
	{
		uint16 NumCharEntries;
		uint32 Offset;
		uint32 FontSheetOffset;
	};
	const LongDayTextBlock textBlocks[] =
	{
		{936/2, 0x4e54, 0},
		{467, 0x53f0, 0x2800},
		{196/2, 0x58ba, 0x4800},
	};

	const string extension = bInBmp ? ".bmp" : ".png";

	const string outputDirectory = inOutputDirectory + "OptionsText\\";
	CreateDirectoryHelper(outputDirectory);

	//OMKTEST
	const std::string omkTestFilePath = inRootDirectory + "SAKURA1\\OMKTEST.BIN";
	FileData omkTestFile;
	if (!omkTestFile.InitializeFileData(omkTestFilePath))
	{
		return false;
	}

	//OMKFNT
	const std::string omkFntFilePath = inRootDirectory + "SAKURA1\\OMKFNT.BIN";
	FileData omkFntFile;
	if (!omkFntFile.InitializeFileData(omkFntFilePath))
	{
		return false;
	}

	//Create palette data
	FileData paletteFileData;
	const std::string sakuraFilePath = inRootDirectory + "SAKURA.BIN";
	if (!paletteFileData.InitializeFileData(sakuraFilePath))
	{
		return false;
	}

	PaletteData paletteData;
	paletteData.CreateFrom15BitData(paletteFileData.GetData() + 0x77c9c, 32);

	const int numTextBlocks = 3;
	for(int textBlockIndex = 0; textBlockIndex < numTextBlocks; ++textBlockIndex)
	{
		const string blockOutputDir = inOutputDirectory + string("OptionsText\\") + std::to_string(textBlockIndex) + Seperators;
		CreateDirectoryHelper(blockOutputDir);

		const LongDayTextBlock& textBlock = textBlocks[textBlockIndex];

		PRSDecompressor decompressedFontSheet;
		decompressedFontSheet.UncompressData((void*)(omkFntFile.GetData() + textBlock.FontSheetOffset), omkFntFile.GetDataSize() - textBlock.FontSheetOffset);

		const int tileDim = 16;
		SakuraFontSheet tileSheet;
		if (!tileSheet.CreateFontSheetFromData(decompressedFontSheet.mpUncompressedData, decompressedFontSheet.mUncompressedDataSize, tileDim, tileDim, true))
		{
			return false;
		}

		//Dump fontsheet
		ExtractImageFromData(decompressedFontSheet.mpUncompressedData, decompressedFontSheet.mUncompressedDataSize,
			blockOutputDir + "FontSheet.png", paletteFileData.GetData() + 0x77c9c, 32, true, 16, 16, 16, 256, 0, true, false);

		//Read int character entries
		const uint16 numCharEntries = textBlock.NumCharEntries;
		uint16* pCharEntries = new uint16[numCharEntries];
		memcpy_s(pCharEntries, numCharEntries * sizeof(uint16), omkTestFile.GetData() + textBlock.Offset, numCharEntries * sizeof(uint16));

		//Generate string entries
		std::vector<SakuraString> sakuraStrings;
		SakuraString currString;

		for (uint16 i = 0; i < numCharEntries; ++i)
		{
			uint16& entry = pCharEntries[i];
			entry = SwapByteOrder(entry);

			if (entry == 0)
			{
				sakuraStrings.push_back(currString);
				currString.Clear();
			}
			else
			{
				currString.AddChar(entry - 1);
			}
		}

		const uint32 numStringEntries = (uint32)sakuraStrings.size();
		for (size_t lineIndex = 0; lineIndex < numStringEntries; ++lineIndex)
		{
			const SakuraString sakuraString(sakuraStrings[lineIndex]);
			if (sakuraString.mChars.size() > 255)
			{
				continue;
			}

			const int numSakuraLines = sakuraString.GetNumberOfLines();

			BitmapSurface sakuraStringBmp;
			sakuraStringBmp.CreateSurface(SakuraString::MaxCharsPerLine * tileDim, tileDim * numSakuraLines, BitmapSurface::EBitsPerPixel::kBPP_4,
				paletteData.GetData(), paletteData.GetSize());

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

				const char* pData = tileSheet.GetCharacterTile(sakuraChar);

				sakuraStringBmp.AddTile(pData, tileSheet.GetTileSizeInBytes(), currCol * tileDim, currRow * tileDim, tileDim, tileDim);

				++currCol;
			}

			const string bitmapName = blockOutputDir + std::to_string(lineIndex + 1) + extension;
			sakuraStringBmp.WriteToFile(bitmapName, bInBmp);
		}

	}
	
	return true;
}

void ExtractLongDayData(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	CreateDirectoryHelper(inOutputDirectory);

	if(!ExtractLongDayOptionsText(inRootDirectory, inOutputDirectory, bInBmp))
	{
		return;
	}

	CreateSpreadSheetForImages("LongDayOptions0", "LongDay\\PNG\\OptionsText\\0\\", inOutputDirectory + "OptionsText\\0\\", "..\\..\\..\\..\\Translation\\LongDayOptions0.php");
	CreateSpreadSheetForImages("LongDayOptions1", "LongDay\\PNG\\OptionsText\\1\\", inOutputDirectory + "OptionsText\\1\\", "..\\..\\..\\..\\Translation\\LongDayOptions1.php");
	CreateSpreadSheetForImages("LongDayOptions2", "LongDay\\PNG\\OptionsText\\2\\", inOutputDirectory + "OptionsText\\2\\", "..\\..\\..\\..\\Translation\\LongDayOptions2.php");

	printf("Success\n");
}

bool PatchLongDayText(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	//OMKTEST
	const string omkTestFilePath = inPatchedSakuraDirectory + "SAKURA1\\OMKTEST.BIN";
	FileReadWriter omkTestFile;
	if (!omkTestFile.OpenFile(omkTestFilePath))
	{
		return false;
	}

	//OMKFNT
	const string omkFntFilePath = inPatchedSakuraDirectory + "SAKURA1\\OMKFNT.BIN";
	FileReadWriter omkFntFile;
	if (!omkFntFile.OpenFile(omkFntFilePath))
	{
		return false;
	}

	//Create font sheet
	const uint32 fontWidth = 16;
	const uint32 fontHeight = 16;
	const string translatedFontSheetPath = inTranslatedDataDirectory + "8x12.bmp";
	BmpToSaturnConverter translatedFontSheet;
	if (!translatedFontSheet.ConvertBmpToSakuraFormat(translatedFontSheetPath, false, BmpToSaturnConverter::CYAN, &fontWidth, &fontHeight))
	{
		return false;
	}
	translatedFontSheet.PackTiles();

	//Compress it
	PRSCompressor compressedFont;
	compressedFont.CompressData((void*)translatedFontSheet.mpPackedTiles, translatedFontSheet.mPackedTileSize);

	struct OMKFileEntry
	{
		const string TranslationFilePath; 
		uint32       MaxFontFileSize;
		int          FontSheetOffset;
		uint32       NumLines;
		uint32       FooterStartValue;
		int          StringTableOffset;
		uint32       FooterOffset;
	};

	OMKFileEntry omkFileEntries[] = 
	{
		{inTranslatedDataDirectory + "\\Translation\\LongDayOptions0.txt", 7908,      0, 60, 0x2a4e54, 0x4e54, 0x5200},
		{inTranslatedDataDirectory + "\\Translation\\LongDayMovies.txt",   7908, 0x2800, 55, 0x2a53f0, 0x53f0, 0x5798},
		{inTranslatedDataDirectory + "\\Translation\\LongDayOptions2.txt", 7908, 0x4800, 20, 0x2a58ba, 0x58ba, 0x5980},
	};

	const int numOmkEntries = sizeof(omkFileEntries) / sizeof(omkFileEntries[0]);
	for(int omkIndex = 0; omkIndex < numOmkEntries; ++omkIndex)
	{
		const OMKFileEntry& omkEntry = omkFileEntries[omkIndex];

		if (compressedFont.mCompressedSize > omkEntry.MaxFontFileSize)
		{
			printf("Compressed size for the font sheet is too big. Must be less than %i, but is %i\n", omkEntry.MaxFontFileSize, compressedFont.mCompressedSize);
			return false;
		}

		//Output the font sheet
		omkFntFile.WriteData(omkEntry.FontSheetOffset, compressedFont.mpCompressedData, compressedFont.mCompressedSize, false);

		//Open translated text file
		const FileNameContainer translatedFileName(omkEntry.TranslationFilePath);
		TextFileData translatedFile(translatedFileName);
		if (!translatedFile.InitializeTextFile())
		{
			printf("Unable to open the translation file %s.\n", translatedFileName.mFullPath.c_str());
			return false;
		}

		//Make sure we have the correct amount of lines
		const uint32 numLines = translatedFile.mLines.size();
		if (numLines < omkEntry.NumLines)
		{
			printf("Unable to translate file: %s because the translation has too many lines. Expected: %zi Got: %zi\n", translatedFileName.mNoExtension.c_str(), omkEntry.NumLines, translatedFile.mLines.size());
			return false;
		}

		//Create sakura strings
		uint32 footerEntryOffset = 0;
		const uint32 footerStartValue = omkEntry.FooterStartValue;
		vector<uint32> footerData;
		vector<SakuraString> sakuraFormatLines;
		for (uint32 i = 0; i < numLines; ++i)
		{
			SakuraString newSakuraString;

			const TextFileData::TextLine& translatedLine = translatedFile.mLines[i];
			const char* pWord = translatedLine.mFullLine.c_str();
			size_t numLetters = translatedLine.mFullLine.size();
			if (numLetters > 24)
			{
				printf("Exceeds 24 char limit: %s\n", translatedLine.mFullLine.c_str());
			}

			for (size_t letterIndex = 0; letterIndex < numLetters; ++letterIndex)
			{
				newSakuraString.AddChar(pWord[letterIndex]);
			}
			newSakuraString.AddChar(0);

			if (numLetters % 2)
			{
				newSakuraString.AddChar(0);
			}

			sakuraFormatLines.push_back(newSakuraString);

			footerData.push_back(footerStartValue + footerEntryOffset);
			footerEntryOffset += newSakuraString.GetNumberOfPrintedCharacters() + 1; //+1 for the extra 00 of the last character
		}

		//Write strings
		uint32 writeOffset = omkEntry.StringTableOffset;
		int lineIndex = 0;
		for (const SakuraString& sakuraString : sakuraFormatLines)
		{
			vector<uint8> dataArray;
			sakuraString.GetSingleByteDataArray(dataArray);

			const uint32 dataSize = dataArray.size() * sizeof(uint8);
			if (writeOffset + dataSize >= omkEntry.FooterOffset)
			{
				printf("Translation for Long Data does not fit in string table. Got up to: %s\n", translatedFile.mLines[lineIndex].mFullLine.c_str());
				break;
			}

			omkTestFile.WriteData(writeOffset, (const char*)dataArray.data(), dataSize, false);
			writeOffset += dataSize;
			++lineIndex;
		}

		//Write footer
		writeOffset = omkEntry.FooterOffset;
		const uint32 numFooterValues = (uint32)footerData.size();
		for (uint32 i = 0; i < numFooterValues; ++i)
		{
			omkTestFile.WriteData(writeOffset, (char*)&footerData[i], sizeof(uint32), true);
			writeOffset += sizeof(uint32);

			if (i == 0)
			{
				omkTestFile.WriteData(writeOffset, (char*)&footerData[0], sizeof(uint32), true);
				writeOffset += sizeof(uint32);
			}
		}
		omkTestFile.WriteData(writeOffset, (char*)&footerData[0], sizeof(uint32), true);
		omkTestFile.WriteData(writeOffset + sizeof(uint32), (char*)&footerData[0], sizeof(uint32), true);
	}
	
	return true;
}

bool PatchLongDayTextRenderingCode(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	/*
	2a01ac mov.w @r11, r1       //61b1 => 61b0

	2a01b8 add r0, r0           //300c => 0009
	2a01ba mov.w @(r0, r11), r1 //01bd => 01bc
	2a01bc extu.w r1, r6        //661d => 661c
	
	2a01da add 10, r1           //7110 => 7108 horizontal spacing
	
	2a01e6 add r0, r0           //300c => 0009
	2a01e8 mov.w @(r0, r11), r1 //01bd => 01bc
	2a01ea extu.w r1,r1         //611d => 611c

	2a01f0 mov 0xb, r1          //e10b => e117	
	*/

	const uint32 baseAddress = 0x1ac;
	PatchingData patchingInfo[] =
	{
		baseAddress + 0,  0x61b0, 0x61b1,
		baseAddress + 12, 0x0009, 0x300c,
		baseAddress + 14, 0x01bc, 0x01bd,
		baseAddress + 16, 0x661c, 0x661d,
		baseAddress + 46, 0x7108, 0x7110, //spacing
		baseAddress + 58, 0x0009, 0x300c,
		baseAddress + 60, 0x01bc, 0x01bd,
		baseAddress + 62, 0x611c, 0x611d,
		baseAddress + 68, 0xe117, 0xe10b, //max characters
	};

	return PatchCodeInFile(inPatchedSakuraDirectory + "SAKURA1\\OMKTEST.BIN", patchingInfo, sizeof(patchingInfo)/sizeof(patchingInfo[0]));
}

bool PatchLongDay(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	printf("Patching Long Day\n");

	if(!PatchLongDayText(inPatchedSakuraDirectory, inTranslatedDataDirectory))
	{
		return false;
	}

	return PatchLongDayTextRenderingCode(inPatchedSakuraDirectory, inTranslatedDataDirectory);
}