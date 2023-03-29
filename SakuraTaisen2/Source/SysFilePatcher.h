namespace SysFilePatcher
{
	const int OffsetToText = 0xE000;
}

struct FixedSysFileHeader
{
	int mNumEntries{ 0 };
	int* mpOffsetsToTextEntries{nullptr};

	~FixedSysFileHeader()
	{
		delete[] mpOffsetsToTextEntries;
	}

	int GetHeaderSize() const
	{
		return sizeof(mNumEntries) + sizeof(int)*mNumEntries;
	}

	void CreateFixedHeader(const vector<SakuraString>& inTranslatedLines)
	{
		mNumEntries = (int)inTranslatedLines.size();

		if(mNumEntries)
		{
			mpOffsetsToTextEntries = new int[mNumEntries];

			const int newlineBytes = 2;
			const int initialBytes = sizeof(int); //for number of entries
			int currentOffset      = initialBytes + mNumEntries*sizeof(int);
			int entryIndex         = 0;
			
			while(entryIndex < mNumEntries)
			{	
				mpOffsetsToTextEntries[entryIndex] = SwapByteOrder(currentOffset);
				currentOffset += inTranslatedLines[entryIndex].GetNumBytes();
				++entryIndex;
			}
		}
	}

	void OutputToFile(FileReadWriter& InOutputFile)
	{
		if(mpOffsetsToTextEntries)
		{
			InOutputFile.WriteData(SysFilePatcher::OffsetToText, (const char*)&mNumEntries, sizeof(mNumEntries), true);
			InOutputFile.WriteData(SysFilePatcher::OffsetToText + sizeof(mNumEntries), (const char*)mpOffsetsToTextEntries, sizeof(int)*mNumEntries);
		}
	}
};

bool ExtractAllSysFiles(const vector<FileNameContainer>& InSysFiles, vector<SysFileExtractor>& InExtractedSysFiles)
{	
	//Empty file
	const string fileToSkip = "SYS49";

	InExtractedSysFiles.reserve(InSysFiles.size());
	for (const FileNameContainer& sysFile : InSysFiles)
	{
		if(sysFile.mNoExtension == fileToSkip)
		{
			continue;
		}

		{
			SysFileExtractor newEntry;
			InExtractedSysFiles.emplace_back(std::move(newEntry));
		}


		SysFileExtractor& newEntry = InExtractedSysFiles.back();
		if (newEntry.Initialize(sysFile.mFullPath))
		{
			newEntry.ParseStrings();
		}
		else
		{
			return false;
		}
	}

	return true;
}

bool InsertTextForSysFile(SysFileExtractor& inSysFile, 
                          const vector<FileNameContainer>& inTranslatedTextFiles, 
                          const TileExtractor& inTranslatedFontSheet,
                          const string& inOutputDirectory)
{
#define IncrementLine_Sys()\
    ++currLine;\
    charCount = 0;\
    if( currLine > maxLines ) \
	{\
		printf("Doesn't Fit: %s\n", textLine.mFullLine.c_str());\
		bFailedToAddLine = true;\
	}\
    translatedString.AddChar( GTranslationLookupTable.GetIndex('\n') );

	//Insert new line if needed
#define ConditionallyAddNewLine_Sys()\
	if( word.size() + charCount > maxCharsPerLine )\
	{\
		IncrementLine_Sys()\
	}

	if(inSysFile.mLines.size() == 0)
	{
		return true;
	}

	printf("\nInserting text for: %s\n", inSysFile.mFileNameInfo.mFileName.c_str());

	const string UntranslatedEnglishString("Untranslated");
	const string UnusedEnglishString("Unused");

	//Used for verifying timing data
	map<int, int> lineTimingMap;

	//Figure out output file name
	const string outFileName = inOutputDirectory + inSysFile.mFileNameInfo.mFileName;

	//Create output file
	FileReadWriter outFile;
	if (!outFile.OpenFile(outFileName))
	{
		printf("Unable to open the output file %s.\n", outFileName.c_str());

		return false;
	}

	//Search for the corresponding translated file name
	const FileNameContainer* pMatchingTranslatedFileName = nullptr;
	for (const FileNameContainer& translatedFileName : inTranslatedTextFiles)
	{
		if (translatedFileName.mNoExtension.find(inSysFile.mFileNameInfo.mNoExtension) != string::npos)
		{
			pMatchingTranslatedFileName = &translatedFileName;
			break;
		}
	}

	vector<SakuraString> translatedLines;
	if (pMatchingTranslatedFileName)
	{
		//Open translated text file
		TextFileData translatedFile(*pMatchingTranslatedFileName);
		if (!translatedFile.InitializeTextFile())
		{
			printf("Unable to open the translation file %s.\n", pMatchingTranslatedFileName->mFullPath.c_str());
			return false;
		}

		//Make sure we have the correct amount of lines
		if (inSysFile.mLines.size() < translatedFile.mLines.size())
		{
			printf("Unable to translate file: %s because the translation has too many lines. Expected: %zi Got: %zi\n", pMatchingTranslatedFileName->mNoExtension.c_str(), inSysFile.mLines.size(), translatedFile.mLines.size());
			return false;
		}

		//Get converted lines of text
		const int maxCharsPerLine = MaxCharsPerLine;//(240 / OutTileSpacingX);
		const int maxLines = MaxLines;
		const size_t numTranslatedLines = translatedFile.mLines.size();
		
		for (size_t translatedLineIndex = 0; translatedLineIndex < numTranslatedLines; ++translatedLineIndex)
		{
			const TextFileData::TextLine& textLine = translatedFile.mLines[translatedLineIndex];
			int charCount = 0;
			int currLine = 1;
			const size_t numWords = textLine.mWords.size();
			bool bAlreadyShowedError = false;
			SakuraString translatedString;

			//Output line id
			translatedString.AddChar(inSysFile.mLineIds[translatedLineIndex].mId);
			translatedString.AddChar(inSysFile.mLineIds[translatedLineIndex].mEnder);

			//Figure out if this is a lips entry
			const size_t currSakuraStringIndex = translatedLineIndex;
			
			if (numWords &&
				 (textLine.mWords[0] == UntranslatedEnglishString ||
				 textLine.mWords[0] == UnusedEnglishString)
				)
			{
				printf("Error: Untranslated line. (File: %s Line: %zu)\n", inSysFile.mFileNameInfo.mFileName.c_str(), currSakuraStringIndex + 1);
			}

			//If untranslated, then write out the file and line number
			if (numWords == 0 ||
				(numWords == 1 && (textLine.mWords[0] == UntranslatedEnglishString || textLine.mWords[0] == UnusedEnglishString))
				)
			{
				bool bUseShorthand = false;
#if USE_SINGLE_BYTE_LOOKUPS && USE_4_BYTE_OFFSETS 
				if (numTranslatedLines > 1200)
				{
					bUseShorthand = true;
				}
#endif

				if (numWords == 0)
				{
					printf("Warning: Blank line. (File: %s Line: %zu)\n", inSysFile.mFileNameInfo.mFileName.c_str(), currSakuraStringIndex + 1);
				}

				SakuraString translatedSakuraString;
				const string baseUntranslatedString = inSysFile.mFileNameInfo.mNoExtension + string(":");
				const string untranslatedString = bUseShorthand ? "U" : baseUntranslatedString + std::to_string(translatedLineIndex + 1); //Just print out a U for unused lines to save space

				translatedSakuraString.AddChar(inSysFile.mLineIds[translatedLineIndex].mId);
				translatedSakuraString.AddChar(inSysFile.mLineIds[translatedLineIndex].mEnder);
				translatedSakuraString.AddString(untranslatedString, 0, 0, false);
				translatedSakuraString.AddChar(0xffff); //End of line

				translatedLines.push_back(translatedSakuraString);
				continue;
			}

			for (size_t wordIndex = 0; wordIndex < numWords; ++wordIndex)
			{
				const string& word = textLine.mWords[wordIndex];
				bool bFailedToAddLine = false;
				if (word.size() > maxCharsPerLine)
				{
					printf("Unable to insert word because it is longer than %i characters: %s[%zi]\n", maxCharsPerLine, word.c_str(), word.size());
					continue;
				}

				//Check to see if this is a newline
				const bool bNewLineWord = word == NewLineWord;
				if (bNewLineWord)
				{
					IncrementLine_Sys();

					//bFailedToAddLine set inside IncrementLine_Sys
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
					if (charCount + 1 > maxCharsPerLine)
					{
						IncrementLine_Sys();
					}
					else
					{
						translatedString.AddChar(GTranslationLookupTable.GetIndex(' '));
						++charCount;
					}

					continue;
				}

				//Insert new line if needed
				ConditionallyAddNewLine_Sys();

				//Add the word
				const size_t numLettersInWord = word.size();
				if (word.size() + charCount > maxCharsPerLine)
				{
					IncrementLine_Sys();
				}

				const char* pWord = word.c_str();
				for (size_t letterIndex = 0; letterIndex < numLettersInWord; ++letterIndex)
				{
					translatedString.AddChar(GTranslationLookupTable.GetIndex(pWord[letterIndex]));
					++charCount;
				}

				//bFailedToAddLine is set inside ConditionallyAddNewLine_Sys
				if (bFailedToAddLine)
				{
					break;
				}

				//Add space
				if (wordIndex + 1 < numWords)
				{
					//Insert new line if needed
					const string& nextWord = textLine.mWords[wordIndex + 1];
					if (nextWord != NewLineWord)
					{
						//If adding the next word will put us over the word limit, add a new line
						if (nextWord.size() + charCount > maxCharsPerLine)
						{
							ConditionallyAddNewLine_Sys();
						}
						else //Otherwise add a space
						{
							if (charCount + 1 >= maxCharsPerLine)
							{
								IncrementLine_Sys();
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

#if USE_SINGLE_BYTE_LOOKUPS
			if ((translatedString.mChars.size()) % 2 != 0)
			{
				translatedString.AddChar(' ');
			}
#endif

			//String ends with ffff
			translatedString.AddChar(0xffff);
			translatedLines.push_back(std::move(translatedString));
		}

		//Fill out everything else with "Untranslated"
		const string baseUntranslatedString = inSysFile.mFileNameInfo.mNoExtension + string(": ");
		const size_t untranslatedCount = inSysFile.mLines.size() - translatedFile.mLines.size();
		for (size_t i = 0; i < untranslatedCount; ++i)
		{
			const string untranslatedString = baseUntranslatedString + std::to_string(i + translatedFile.mLines.size() + 1);
			SakuraString translatedSakuraString;

			const size_t currSakuraStringIndex = i + translatedFile.mLines.size();
			translatedSakuraString.AddChar(inSysFile.mLineIds[i].mId);
			translatedSakuraString.AddChar(inSysFile.mLineIds[i].mEnder);
			translatedSakuraString.AddString(untranslatedString, 0, 0, false);
			translatedSakuraString.AddChar(0xffff); //End of line

			translatedLines.push_back(translatedSakuraString);
		}

	}//if(pMatchingTranslatedFileName)
	else
	{
		//Fill out everything else with "Untranslated"
		const string baseUntranslatedString = inSysFile.mFileNameInfo.mNoExtension + string(": ");
		for (size_t i = 0; i < inSysFile.mLines.size(); ++i)
		{
			const string untranslatedString = baseUntranslatedString + std::to_string(i + 1);
			SakuraString translatedSakuraString;
			translatedSakuraString.AddChar(inSysFile.mLineIds[i].mId);
			translatedSakuraString.AddChar(inSysFile.mLineIds[i].mEnder);
			translatedSakuraString.AddString(untranslatedString, 0, 0, false);
			translatedSakuraString.AddChar(0xffff); //End of line
			translatedLines.push_back(translatedSakuraString);
		}
	}

	//Write header
	FixedSysFileHeader fixedHeader;
	fixedHeader.CreateFixedHeader(translatedLines);
	fixedHeader.OutputToFile(outFile);

	//Output data
	size_t dataIndex = 0;
	size_t translationIndex = 0;
	const size_t numInsertedLines = translatedLines.size();
	unsigned int numBytesWritten = 0;
	int writeOffset = SysFilePatcher::OffsetToText + fixedHeader.GetHeaderSize();
	while (1)
	{	
		if (translationIndex < numInsertedLines)
		{
			vector<unsigned short> translationData;
			translatedLines[translationIndex].GetDataArray(translationData);
			outFile.WriteData(writeOffset, (const char*)translationData.data(), translationData.size() * sizeof(short));

			const unsigned int byteCount = translationData.size() * sizeof(short);
			numBytesWritten += byteCount;
			writeOffset += byteCount;
		}

		++translationIndex;

		if (translationIndex >= numInsertedLines)
		{
			break;
		}
	}

	const int MaxTextSize = 0x11800 - SysFilePatcher::OffsetToText;
	if (numBytesWritten > MaxTextSize)
	{
		printf("WARNING: TBL file %s is too big: %u out of %i.\n", inSysFile.mFileNameInfo.mFileName.c_str(), numBytesWritten, MaxTextSize);
	}

	//Output fontsheet
	inTranslatedFontSheet.OutputTiles(outFile, -1, 0);
	
	return true;
}

bool InsertSysFileText(const string& inRootSakuraTaisenDirectory, const string& inTranslatedTextDirectory, const string& outDirectory,
	TileExtractor& inTranslatedFontSheet, bool bInOutputToCorrespondingDirectory = false, bool bInCreateOutput = true)
{
	printf("Inserting SysFile Text\n");

	///////////
	// 	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inRootSakuraTaisenDirectory, allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> sysFiles;
	GetAllFilesOfType(allFiles, ".MES", sysFiles);
	GetAllFilesOfType(allFiles, "LOW.BIN", sysFiles);

	//Extract all sys files
	vector<SysFileExtractor> extractedSysFiles;
	if(!ExtractAllSysFiles(sysFiles, extractedSysFiles))
	{
		return false;
	}
	///////////////

	//Find all translated text files
	vector<FileNameContainer> translatedTextFiles;
	const string translatedFileDirectory = inTranslatedTextDirectory + Seperators + "Translation";
	FindAllFilesWithinDirectory(translatedFileDirectory, translatedTextFiles);

	const string outputDirectory = outDirectory + Seperators + "SAKURA2\\";
	CreateDirectoryHelper(outputDirectory);

	//Insert text
	for (SysFileExtractor& sysFile : extractedSysFiles)
	{
		InsertTextForSysFile(sysFile, translatedTextFiles, inTranslatedFontSheet, outputDirectory);
	}

	return true;
}