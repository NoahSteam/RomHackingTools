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

		//Single byte encoding
		{0x18096, 0x6390, 0x6391}, //0601d096 mov.w @r9, r3 //6391 to 6390
		{0x180cc, 0x6390, 0x6391}, //0601d0cc mov.w @r9, r3 //6391 to 6390
		{0x180ae, 0x7901, 0x7902}, //0601d0ae add 2,r9      //7902 to 7901

		//High byte letters fix for single byte encoding
		{0x1809a, 0x623c, 0x623d}, //0601d09a extu.w r3, r2 //623d to 623c
		{0x180a4, 0x613c, 0x613d}, //0601d0a4 extu.w r3, r1 //613d to 613c
		{0x180d0, 0x623c, 0x623d}, //0601d0d0 extu.w r3, r2 //623d to 623c
		{0x180d6, 0x613c, 0x613d}, //0601d0d6 extu.w r3, r1 //613d to 613c
		
		{0x18116, 0x00ff, 0xffff}, //0601d116 ffff to 00ff
		{0x1811e, 0x00fe, 0xfffe}, //0601d11e fffe to 00fe

		//Shift final text box position
		{0x19fe2, 0x0043, 0x009b}, //0601efe2 has 009b someone's final line
		{0x1b274, 0x0043, 0x009b}, //06020274 has 009b for Sakura's final line
		{0x1bd02, 0x0043, 0x009b}, //06020d02 has 009b for Revolution tutorial final line
		{0x1c6aa, 0xe643, 0xe67b}, //060216aa has mov 0x7b, r6 for Joker's second to final line (e67b)
		{0x1c77c, 0x0043, 0x008b}, //0602177c has 008b for Joker's final line
		{0x1e564, 0x0043, 0x008b}, //06023564 has 008b for Reni's final line

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

void CreatePatchedTycoonTutorialLine(const TextFileData::TextLine& InTextLine, const int InTranslatedLineIndex, vector<SakuraString>& OutLines)
{
#define IncrementTycoonLine()\
    ++currLine;\
    charCount = 0;\
    if( currLine > maxLines ) \
	{\
		printf("Doesn't Fit: %s\n", InTextLine.mFullLine.c_str());\
		bFailedToAddLine = true;\
	}\
    translatedString.AddChar( GTranslationLookupTable.GetIndex('\n') );

	//Insert new line if needed
#define ConditionallyAddNewTycoonLine()\
	if( word.size() + charCount > MaxCharsPerLine )\
	{\
		if( !bAlreadyShowedError )\
		{\
		}\
		else\
		{\
			IncrementTycoonLine()\
		}\
	}

	SakuraString translatedString;
	int charCount = 0;
	int currLine = 0;
	const int maxLines = 4;
	bool bAlreadyShowedError = false;
	const string untranslated("Untranslated");

	const size_t numWords = InTextLine.mWords.size();
	for (size_t wordIndex = 0; wordIndex < numWords; ++wordIndex)
	{
		const string& word = InTextLine.mWords[wordIndex];

		bool bFailedToAddLine = false;
		if (word.size() > MaxCharsPerLine)
		{
			printf("Unable to insert word because it is longer than %i characters: %s[%zi]\n", MaxCharsPerLine, word.c_str(), word.size());
			continue;
		}

		//Check to see if this is a newline
		const bool bNewLineWord = word == NewLineWord;
		if (bNewLineWord)
		{
			IncrementTycoonLine();

			//bFailedToAddLine set inside IncrementLine
			if (bFailedToAddLine)
			{
				//	break;
			}

			continue;
		}

		//Check to see if this is a space
		const bool bSpaceWord = word == SpaceWord;
		if (bSpaceWord)
		{
			if (charCount + 1 > MaxCharsPerLine)
			{				
				IncrementTycoonLine();
			
				//bFailedToAddLine set inside IncrementLine
				if (bFailedToAddLine)
				{
					//	break;
				}
			}
			else
			{
				translatedString.AddChar(GTranslationLookupTable.GetIndex(' '));
				++charCount;
			}

			continue;
		}
		else if (word == IgnoreCharacter)
		{
			continue;
		}

		//Insert new line if needed
		ConditionallyAddNewTycoonLine();

		//Add the word
		const size_t numLettersInWord = word.size();
		if (word.size() + charCount > MaxCharsPerLine)
		{
			IncrementTycoonLine();
		}

		const char* pWord = word.c_str();
		for (size_t letterIndex = 0; letterIndex < numLettersInWord; ++letterIndex)
		{
			translatedString.AddChar(GTranslationLookupTable.GetIndex(pWord[letterIndex]));
			++charCount;
		}

		//bFailedToAddLine is set inside ConditionallyAddNewLine
		if (bFailedToAddLine)
		{
			break;
		}

		//Add space
		if (wordIndex + 1 < numWords)
		{
			//Insert new line if needed
			const string& nextWord = InTextLine.mWords[wordIndex + 1];
			if (nextWord != NewLineWord)
			{
				//If adding the next word will put us over the word limit, add a new line
				if (nextWord.size() + charCount > MaxCharsPerLine)
				{
					ConditionallyAddNewTycoonLine();
				}
				else //Otherwise add a space
				{
					if (charCount + 1 >= MaxCharsPerLine)
					{						
						IncrementTycoonLine();
					}
					else
					{
						translatedString.AddChar(GTranslationLookupTable.GetIndex(' '));
						++charCount;
					}
				}
			}
		}
	}//for(wordIndex < numWords)

	//Strings need to start at 2byte boundaries, so pad this one if needed so next string is at boundary
	if ((translatedString.mChars.size()) % 2 == 0)
	{
		translatedString.AddChar(' ');
	}

	translatedString.AddChar(0xffff);

	OutLines.push_back(translatedString);
}

bool OutputPatchedTycoonTutorialText(FileReadWriter& inCardFile, vector<SakuraString>& inTranslatedLines)
{
	vector<uint32> newEntryOffsets;
	newEntryOffsets.push_back( SwapByteOrder((uint32)166)); //First value is the number of entries

	//Output data
	size_t dataIndex = 0;
	size_t translationIndex = 0;
	const size_t numInsertedLines = inTranslatedLines.size();
	unsigned int numSingleBytesWritten = 0;
	int entryOffset = 0x29c;
	int offset = 0x31829c;
	while (1)
	{
		if (translationIndex < numInsertedLines)
		{
			vector<unsigned char> translationData;
			inTranslatedLines[dataIndex].GetSingleByteDataArray(translationData);
			inCardFile.WriteData(offset, (char*)translationData.data(), translationData.size() * sizeof(char));

			const int numBytesWritten = (int)translationData.size() * sizeof(char);
			numSingleBytesWritten += numBytesWritten;
			offset += numBytesWritten;

			newEntryOffsets.push_back(SwapByteOrder(entryOffset));

			entryOffset += numBytesWritten;
		}

		++dataIndex;
		++translationIndex;

		if (translationIndex >= numInsertedLines)
		{
			break;
		}
	}

	const size_t sizeLimit = 0x31a950 - 0x31829c;
	if(numSingleBytesWritten > sizeLimit)
	{
		printf("WARNING: Tutorial text is over by %i bytes\n", numSingleBytesWritten - sizeLimit);

	}
	//Output new entry table
	inCardFile.WriteData(0x318000, (char*)newEntryOffsets.data(), newEntryOffsets.size()*sizeof(uint32));

	return true;
}

bool PatchTycoonTutorialText(const string& inDataDirectory, const string& inPatchedDirectory, FileReadWriter& inCardFile)
{
	const FileNameContainer fileName(inDataDirectory + "Translation\\TycoonTutorial.txt");
	TextFileData textFile(fileName);
	if(!textFile.InitializeTextFile())
	{
		return false;
	}

	const size_t numLines = 166;
	if(textFile.mLines.size() != numLines)
	{
		printf("Should be 166 lines in TycoonTutorial.txt\n");
		return false;
	}

	const string untranslated("Untranslated");
	vector<SakuraString> translatedLines;
	for (size_t translatedLineIndex = 0; translatedLineIndex < numLines; ++translatedLineIndex)
	{
		TextFileData::TextLine& textLine = textFile.mLines[translatedLineIndex];

		if( textLine.mWords.size() == 1 && textLine.mWords[0] == untranslated)
		{
			textLine.mWords[0] = std::to_string(translatedLineIndex + 1);
		}

		CreatePatchedTycoonTutorialLine(textLine, translatedLineIndex, translatedLines);
	}

	OutputPatchedTycoonTutorialText(inCardFile, translatedLines);

	return true;
}

bool PatchTycoonTutorial(const string& inOriginalDirectory, const string& inPatchedDirectory, const string& inDataDirectory, const TileExtractor& inFontSheet)
{
	printf("Patching Tycoon Tutorial\n");

	FileReadWriter cardFile;
	if (!cardFile.OpenFile(inPatchedDirectory + "\\SAKURA3\\CARD_DAT.ALL"))
	{
		return false;
	}

	if(!PatchTycoonTutorialRenderingCode(inOriginalDirectory, inPatchedDirectory))
	{
		return false;
	}

	if(!PatchTycoonTutorialText(inDataDirectory, inPatchedDirectory, cardFile))
	{
		return false;
	}

	//Write out new font sheet
	inFontSheet.OutputTiles(cardFile, -1, 0x30e780 + 0x80);

	return true;
}