#pragma once

bool PatchLipSyncDataForAdventure(const string& inTranslatedDataDirectory);

struct SakuraTextFileFixedHeader
{
	struct StringInfo
	{
		unsigned int offsetFromTableStart;

		explicit StringInfo(unsigned int inOffsetFromTableStart) : offsetFromTableStart(SwapByteOrder(inOffsetFromTableStart))
		{
		}
	};

	struct StringInfo8Bytes
	{
		unsigned int offsetFromTableStart;

		explicit StringInfo8Bytes(unsigned int inOffsetFromTableStart) : offsetFromTableStart(SwapByteOrder(inOffsetFromTableStart))
		{
		}
	};

	unsigned short           mOffsetToTable;
	unsigned int             mOffsetToTable8Bytes;
	unsigned short           mTableEnd;
	unsigned int             mTableEnd8Bytes;
	vector<StringInfo>       mStringInfo;
	vector<StringInfo8Bytes> mStringInfo8Bytes;

	bool CreateFixedHeader(const vector<SakuraTextFile::SakuraStringInfo>& inInfo, const SakuraTextFile& inSakuraFile, const vector<SakuraString>& inStrings, bool bIsMesFile)
	{
		if(!inInfo.size())
		{
			return true;
		}

		//All TBL files start with this entries
		//mStringInfo.push_back( SwapByteOrder(inInfo[0].mFullValue) );
		mStringInfo.push_back(StringInfo(inInfo[0].mNumCharactersPrecedingThisString));
		mStringInfo8Bytes.push_back(StringInfo8Bytes(inInfo[0].mNumCharactersPrecedingThisString));
		
		int totalCharCount = 0;
		unsigned int prevValue = 0;
		const size_t numEntries = inInfo.size() - 1;
		for (size_t i = 0; i < numEntries; ++i)
		{
#if USE_SINGLE_BYTE_LOOKUPS
			unsigned int newOffset = bIsMesFile ? (unsigned short)inStrings[i].mChars.size() + prevValue : (inStrings[i].mChars.size() + prevValue); //+ trailingZeros;
			
			totalCharCount += inStrings[i].mChars.size();
			prevValue = newOffset;
			if(newOffset % 2 != 0)
			{
				printf("\nERROR:Translated file %s. Line %i is not 2byte alignedu.\n", inSakuraFile.mFileNameInfo.mFileName.c_str(), i);
			}

			newOffset /= 2;
#else
			const unsigned int newOffset = (unsigned int)inStrings[i].mChars.size() + prevValue; //+ trailingZeros;
			totalCharCount += (unsigned short)inStrings[i].mChars.size();
			prevValue = newOffset;
#endif
			
			mStringInfo.push_back(StringInfo(newOffset));
		}

		const bool bCheckTotalCharError = false;//SW2 uses a 4byte lookup table instead of 2 bytes in SW1 !USE_4_BYTE_OFFSETS || bIsMesFile;
		if (bCheckTotalCharError && totalCharCount > 0xffff)
		{
			printf("\nERROR:Translated file %s exceeds max char count. Max is %u.  File has %i\n", inSakuraFile.mFileNameInfo.mFileName.c_str(), 0xffff, totalCharCount);
		}

		//Figure out text table size
		unsigned long stringTableSize = 0;
		for (const SakuraString& sakuraString : inStrings)
		{
			const bool bUseSingleByteLookups = !bIsMesFile && USE_SINGLE_BYTE_LOOKUPS;
			if (bUseSingleByteLookups)
			{
				stringTableSize += sakuraString.GetSingleByteDataSize();
			}
			else
			{
				stringTableSize += sakuraString.mChars.size() * 2;
			}
		}

		if (stringTableSize % 2 != 0)
		{
			stringTableSize++;
		}
		//Done figuring out table size

		/*
		if ((unsigned short)(sizeof(int) + sizeof(int) + sizeof(int) * inInfo.size() + sizeof(int) * inInfo.size()) >> 1 > 0xffff)
		{
			printf("\nERROR:Translated file %s string table exceeds 2 byte limit\n", inSakuraFile.mFileNameInfo.mFileName.c_str());
			return false;
		}*/

		mOffsetToTable = inSakuraFile.mFileHeader.OffsetToTextHeader;
		mOffsetToTable8Bytes = (sizeof(int) + sizeof(int) + sizeof(int) * (int)inInfo.size() + sizeof(int) * (int)inInfo.size()) >> 1;

		unsigned long timingDataL = ((mOffsetToTable << 1) + stringTableSize) >> 1;
		unsigned long timingData8Bytes = ((mOffsetToTable8Bytes << 1) + stringTableSize) >> 1;
		if (timingDataL / 2 > 0xffff)
		{
			if (bCheckTotalCharError)
			{
				const int sizeToReduce = (timingDataL / 2) - 0xffff;
				printf("\nERROR:Translated file %s is too big. Timing data is too far down.  Needs to be reduced by %i bytes (%i characters)\n", inSakuraFile.mFileNameInfo.mFileName.c_str(), sizeToReduce, sizeToReduce / 2);
				return false;
			}
		}
		mTableEnd = (unsigned short)timingDataL;
		mTableEnd8Bytes = timingData8Bytes;

		if (stringTableSize / 2 > 0xffff)
		{
			if (bCheckTotalCharError)
			{
				const int sizeToReduce = (stringTableSize / 2) - 0xffff;
				printf("\nERROR:Translated file %s is too big.  Needs to be reduced by %i bytes (%i characters)\n", inSakuraFile.mFileNameInfo.mFileName.c_str(), sizeToReduce, sizeToReduce);
				return false;
			}
		}

		return true;
	}

	void OutputToFile(FileWriter& outFile)
	{
		const int totalSize = SwapByteOrder((int)mStringInfo.size() + 1);//* sizeof(SakuraTextFileFixedHeader::StringInfo));
		outFile.WriteData(&totalSize, sizeof(totalSize));

		for(StringInfo& stringInfo : mStringInfo)
		{
			outFile.WriteData(&stringInfo.offsetFromTableStart, sizeof(stringInfo.offsetFromTableStart));
		}
	}
	
};

static int GetNumBytesInLines(const vector<SakuraString>& inTranslatedLines)
{
	int numBytes = 0;
	for(const SakuraString& entry : inTranslatedLines)
	{
		numBytes += entry.GetNumBytes();
	}

	return numBytes;
}

bool InsertText(const string& inRootSakuraTaisenDirectory, const string& inTranslatedDataDirectory, const string& inPatchedDirectory, 
                TileExtractor& inTranslatedFontSheet, bool bInOutputToCorrespondingDirectory = false, bool bInCreateOutput = true)
{
#define IncrementLine()\
    ++currLine;\
    charCount = 0;\
    if( currLine > maxLines ) \
	{\
		printf("Doesn't Fit: %s\n", textLine.mFullLine.c_str());\
		bFailedToAddLine = true;\
	}\
    translatedString.AddChar( GTranslationLookupTable.GetIndex('\n') );

	///*printf("LIPS too long[D Line:%i]: %s\n", translatedLineIndex + 1, textLine.mFullLine.c_str());\bAlreadyShowedError = true;\*/
	//Insert new line if needed
#define ConditionallyAddNewLine()\
	if( word.size() + charCount > maxCharsPerLine )\
	{\
		if( bIsLipsEntry && !bAlreadyShowedError )\
		{\
		}\
		else\
		{\
			IncrementLine()\
		}\
	}

	printf("Inserting Text\n");

	///////////
	// 	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inRootSakuraTaisenDirectory, allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> scenarioFiles;
	GetAllFilesOfType(allFiles, "SK0", scenarioFiles);
 	GetAllFilesOfType(allFiles, "SK1", scenarioFiles);
	GetAllFilesOfType(allFiles, "SKC", scenarioFiles);

	//Extract the text
	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(scenarioFiles, sakuraTextFiles, false);
	///////////////
	
	//Find all translated text files
	vector<FileNameContainer> translatedTextFiles;
	const string translatedFileDirectory = inTranslatedDataDirectory + Seperators + "Translation";
	FindAllFilesWithinDirectory(translatedFileDirectory, translatedTextFiles);

	const string UntranslatedEnglishString("Untranslated");
	const string UnusedEnglishString("Unused");
	const bool bIsMESFile = false;
	const string outputDirectory = inPatchedDirectory + Seperators + "SAKURA1\\";
	CreateDirectoryHelper(outputDirectory);

	//Insert text
	for (const SakuraTextFile& sakuraFile : sakuraTextFiles)
	{
		printf("\nInserting text for: %s\n", sakuraFile.mFileNameInfo.mFileName.c_str());

		//Used for verifying timing data
		map<int, int> lineTimingMap;

		//Figure out output file name
		string outFileName;	
		outFileName = outputDirectory + sakuraFile.mFileNameInfo.mFileName;
		
		//Create output file
		FileWriter outFile;
		if (!outFile.OpenFileForWrite(outFileName))
		{
			printf("Unable to open the output file %s.\n", outFileName.c_str());

			return false;
		}

		//Search for the corresponding translated file name
		const FileNameContainer* pMatchingTranslatedFileName = nullptr;
		for (const FileNameContainer& translatedFileName : translatedTextFiles)
		{
			//if (translatedFileName.mNoExtension.find(sakuraFile.mFileNameInfo.mNoExtension) != string::npos)
			if (translatedFileName.mNoExtension == sakuraFile.mFileNameInfo.mNoExtension)
			{
				pMatchingTranslatedFileName = &translatedFileName;
				break;
			}
		}

	//	static const int dirPlusLineBufferSize = 255;
	//	char dirPlusLineNumberBuffer[dirPlusLineBufferSize];

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
			if (sakuraFile.mLines.size() < translatedFile.mLines.size())
			{
				printf("Unable to translate file: %s because the translation has too many lines. Expected: %zi Got: %zi\n", pMatchingTranslatedFileName->mNoExtension.c_str(), sakuraFile.mLines.size(), translatedFile.mLines.size());
				return false;
			}

			//Get converted lines of text
			const int maxCharsPerLine             = MaxCharsPerLine;//(240 / OutTileSpacingX);
			const int maxLines                    = MaxLines;
			const size_t numTranslatedLines       = translatedFile.mLines.size();
			unsigned short numCharsPrintedForMES  = 0;
			unsigned short numCharsPrintedFortTBL = 0;
			
			for (size_t translatedLineIndex = 0; translatedLineIndex < numTranslatedLines; ++translatedLineIndex)
			{
				const TextFileData::TextLine& textLine = translatedFile.mLines[translatedLineIndex];
				int charCount            = 0;
				int currLine             = 1;
				const size_t numWords    = textLine.mWords.size();
				bool bAlreadyShowedError = false;
				SakuraString translatedString;

				//Figure out if this is a lips entry
				const size_t currSakuraStringIndex = translatedLineIndex;
				const size_t sequenceIndex = translatedLineIndex + 1; //sequence ids are 1 based, not 0 based
				const unordered_map<uint16, SakuraTextFile::SequenceEntry>::const_iterator sequenceEntry = sakuraFile.mTextIdToSequenceEntryMap.find(sequenceIndex);
				const bool bIsLipsEntry = false;//TODO: Lips are not identified correctly always sequenceEntry != sakuraFile.mTextIdToSequenceEntryMap.end() ? sequenceEntry->second.mbIsLips : false;
				const bool bIsUnused = false;

				if (bIsLipsEntry)
				{
					if (textLine.GetNumberOfLines() != sakuraFile.mLines[currSakuraStringIndex].GetNumberOfLines())
					{
					//	printf("LIPS ERROR - Translated LIPS line does not have expected number of options. Expected: %i, Has: %i (File: %s Line: %zu)\n", sakuraFile.mLines[currSakuraStringIndex].GetNumberOfLines(), textLine.GetNumberOfLines(), sakuraFile.mFileNameInfo.mFileName.c_str(), currSakuraStringIndex + 1);
					}
				}

				if (numWords && 
					(textLine.mWords[0] == UntranslatedEnglishString ||
					(textLine.mWords[0] == UnusedEnglishString && !bIsUnused)
					)
					)
				{
					printf("Error: Untranslated line. (File: %s Line: %zu)\n", sakuraFile.mFileNameInfo.mFileName.c_str(), currSakuraStringIndex + 1);
				}

				//If untranslated, then write out the file and line number
				if (// bIsUnused ||
					numWords == 0 ||
					(numWords == 1 && (textLine.mWords[0] == UntranslatedEnglishString || textLine.mWords[0] == UnusedEnglishString))
					)
				{
					bool bUseShorthand = false;
#if USE_SINGLE_BYTE_LOOKUPS// && USE_4_BYTE_OFFSETS 
					if (numTranslatedLines > 1200)
					{
						bUseShorthand = true;
					}
#endif

					SakuraString translatedSakuraString;

					bool bHandledEntry = false;
					const int numBytesInOriginalText = sakuraFile.mLines[currSakuraStringIndex].mChars.size();
					if (numBytesInOriginalText >= 3)
					{
						assert(numBytesInOriginalText - 3 >= 0);

						if (sakuraFile.mLines[currSakuraStringIndex].mChars[numBytesInOriginalText - 3].mIndex == 0xfffa)
						{
							translatedSakuraString.AddChar(0xfa);
							translatedSakuraString.AddChar(sakuraFile.mLines[currSakuraStringIndex].mChars[numBytesInOriginalText - 2].mIndex);

							bHandledEntry = true;
						}

						if (sakuraFile.mLines[currSakuraStringIndex].mChars[0].mIndex == 0xfffc)
						{
							translatedSakuraString.AddChar(sakuraFile.mLines[currSakuraStringIndex].mChars[1].mIndex, true);
							translatedSakuraString.AddChar(0xfc, true);

							bHandledEntry = true;
						}
					}
					
					if(!bHandledEntry)
					{
						if (numWords == 0)
						{
							printf("Warning: Blank line. (File: %s Line: %zu)\n", sakuraFile.mFileNameInfo.mFileName.c_str(), currSakuraStringIndex + 1);
						}
						
						unsigned short timingData = 0;
						if (sakuraFile.mLines[translatedLineIndex].mChars[0].mIndex != 0 && textLine.mFullLine.size() > 1)
						{
							timingData = (numCharsPrintedFortTBL >> 1);
						}

						const string baseUntranslatedString = sakuraFile.mFileNameInfo.mNoExtension + string(":");
						const string untranslatedString = bUseShorthand && !timingData ? "U" : baseUntranslatedString + std::to_string(translatedLineIndex + 1); //Just print out a U for unused lines to save space

						translatedSakuraString.AddString(untranslatedString, sakuraFile.mLines[currSakuraStringIndex].mChars[0].mIndex, timingData, true);
					}

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
						IncrementLine();

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
						if (charCount + 1 > maxCharsPerLine)
						{
							if (bIsLipsEntry)
							{
						//		printf("LIPS too long[A Line: %zi]:  %s\n", translatedLineIndex, textLine.mFullLine.c_str());
								//break;
							}
							else
							{
								IncrementLine();
							}

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
					else if(word == IgnoreCharacter)
					{
						continue;
					}

					//Insert new line if needed
					ConditionallyAddNewLine();

					//Add the word
					const size_t numLettersInWord = word.size();
					if (word.size() + charCount > maxCharsPerLine)
					{
						if (bIsLipsEntry && !bAlreadyShowedError)
						{
							printf("LIPS too long [B Line: %zi]: %s\n", translatedLineIndex + 1, textLine.mFullLine.c_str());
							bAlreadyShowedError = true;
							break;
						}
						else
						{
							IncrementLine();
						}
					}

					const char* pWord = word.c_str();
					for (size_t letterIndex = 0; letterIndex < numLettersInWord; ++letterIndex)
					{
						if (bIsLipsEntry && charCount > maxCharsPerLine)
						{
							printf("LIPS too long [E Line: %zi]: %s\n", translatedLineIndex + 1, textLine.mFullLine.c_str());
							break;
						}

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
						const string& nextWord = textLine.mWords[wordIndex + 1];
						if (nextWord != NewLineWord)
						{
							if (bIsLipsEntry && charCount + nextWord.size() > maxCharsPerLine)
							{
								printf("LIPS too long [F Line: %zi]: %s\n", translatedLineIndex + 1, textLine.mFullLine.c_str());
								break;
							}

							//If adding the next word will put us over the word limit, add a new line
							if (nextWord.size() + charCount > maxCharsPerLine)
							{
								ConditionallyAddNewLine();
							}
							else //Otherwise add a space
							{
								if (charCount + 1 >= maxCharsPerLine)
								{
									if (bIsLipsEntry && !bAlreadyShowedError)
									{
										printf("LIPS too long[C Line: %zi]: %s\n", translatedLineIndex + 1, textLine.mFullLine.c_str());
										bAlreadyShowedError = true;
										break;
									}
									else
									{
										IncrementLine();
									}
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

				//Special line ender codes (FFFD, FFFA)
				const int numBytesInOriginalText = sakuraFile.mLines[currSakuraStringIndex].mChars.size();
				if(numBytesInOriginalText >= 3)
				{
					assert(numBytesInOriginalText - 3 >= 0);

					if(sakuraFile.mLines[currSakuraStringIndex].mChars[numBytesInOriginalText - 3].mIndex == 0xfffa)
					{
						translatedString.AddChar(0xfa);
						translatedString.AddChar(sakuraFile.mLines[currSakuraStringIndex].mChars[numBytesInOriginalText-2].mIndex);
					}

					//Can Leave this because it causes formatting issues that need to be dealt with manually
					//For examples, search for fffd in the TextCode files.  SK0808 has an example on lines 59&60					
					if (sakuraFile.mLines[currSakuraStringIndex].mChars[0].mIndex == 0xfffc)
					{
						translatedString.AddChar(sakuraFile.mLines[currSakuraStringIndex].mChars[1].mIndex, true);
						translatedString.AddChar(0xfc, true);

						if(sakuraFile.mLines[currSakuraStringIndex].mChars[numBytesInOriginalText - 2].mIndex == 0xfffd)
						{
							translatedString.AddChar(0xfd);
							printf("Verify Formatting (fc fd): %i\n", currSakuraStringIndex + 1);
						}
					}
					//The FFFD character will have the game combine another line after this line.  But there isn't a way to know
					//which line it will combine.  Meaning the game can end up having the line cut off if we don't manually format it.
					//Example: Quiz minigame in LongDay in 1303 has "Are you ready? Get set…" + "Start!"
					/**/
					else if (sakuraFile.mLines[currSakuraStringIndex].mChars[numBytesInOriginalText - 2].mIndex == 0xfffd)
					{
						translatedString.AddChar(0xfd);

						printf("Verify Formatting (fd): %i\n", currSakuraStringIndex + 1);
					}
					/**/

					//If original line ende with a newline.  This usually happens if the line will be appended with some other line with a fffd byte code
					if(sakuraFile.mLines[currSakuraStringIndex].mChars[numBytesInOriginalText - 2].mIndex == 0xfffe)
					{
						if(!translatedString.EndsWithLineBreak())
						{
							translatedString.AddChar(0xfe);
							printf("Original line ended with a line break: %i\n", currSakuraStringIndex + 1);
						}
					}
				}				

#if USE_SINGLE_BYTE_LOOKUPS
				//Strings need to start at 2byte boundaries, so pad this one if needed so next string is at boundary
				if ((translatedString.mChars.size()) % 2 == 0)
				{
					translatedString.AddChar(' ');
				}
#endif

				//String ends with ffff
				translatedString.AddChar(0xffff);

				translatedLines.push_back(std::move(translatedString));
			}

			//Fill out everything else with "Untranslated"
			const string baseUntranslatedString = sakuraFile.mFileNameInfo.mNoExtension + string(": ");
			const size_t untranslatedCount      = sakuraFile.mLines.size() - translatedFile.mLines.size();
			const unsigned short timingData     = numCharsPrintedFortTBL >> 1;
			for (size_t i = 0; i < untranslatedCount; ++i)
			{
				const string untranslatedString = baseUntranslatedString + std::to_string(i + translatedFile.mLines.size() + 1);
				SakuraString translatedSakuraString;

				const size_t currSakuraStringIndex = i + translatedFile.mLines.size();
				translatedSakuraString.AddString(untranslatedString, sakuraFile.mLines[currSakuraStringIndex].mChars[0].mIndex, timingData, !bIsMESFile);
				translatedSakuraString.AddChar(0xffff); //End of line

				translatedLines.push_back(translatedSakuraString);
			}

		}//if(pMatchingTranslatedFileName)
		else
		{
			printf("Unable to find translation file for %s\n", sakuraFile.mFileNameInfo.mFileName.c_str());

			//Fill out everything else with "Untranslated"
			const string baseUntranslatedString = sakuraFile.mFileNameInfo.mNoExtension + string(": ");
			for (size_t i = 0; i < sakuraFile.mLines.size(); ++i)
			{
				const string untranslatedString = baseUntranslatedString + std::to_string(i + 1);
				SakuraString translatedSakuraString;
				translatedSakuraString.AddString(untranslatedString, sakuraFile.mLines[i].mChars[0].mIndex, 0, !bIsMESFile);
				translatedSakuraString.AddChar(0xffff); //End of line
				translatedLines.push_back(translatedSakuraString);
			}
		}

		SakuraTextFileFixedHeader fixedHeader;
		fixedHeader.CreateFixedHeader(sakuraFile.mStringInfoArray, sakuraFile, translatedLines, bIsMESFile);

		//Create file header
		SakuraTextFile::SakuraHeader newFileHeader = sakuraFile.mFileHeader;

#if USE_4_BYTE_OFFSETS&0
		if (bIsMESFile)
		{
			const unsigned short bigEndianOffsetToTable = SwapByteOrder(fixedHeader.mOffsetToTable);
			const unsigned short bigEndianTableEnd = SwapByteOrder(fixedHeader.mTableEnd);
			outFile.WriteData(&bigEndianOffsetToTable, sizeof(bigEndianOffsetToTable));
			outFile.WriteData(&bigEndianTableEnd, sizeof(bigEndianTableEnd));
			outFile.WriteData(&fixedHeader.mStringInfo[0], fixedHeader.mStringInfo.size() * sizeof(fixedHeader.mStringInfo[0]));
		}
		else
		{
			newFileHeader.OffsetToFontSheet = FourByteAlign(GetNumBytesInLines(translatedLines) + newFileHeader.OffsetToText);
			newFileHeader.FontSheetDataSize = inTranslatedFontSheet.GetDataSize() + inTranslatedFontSheet.GetSizeOfSingleTile(); //repeat first tile

			outFile.WriteData(&newFileHeader, sizeof(newFileHeader));
			outFile.WriteData(sakuraFile.mpHeaderToText, sakuraFile.mHeaderToTextSize);
			fixedHeader.OutputToFile(outFile);
		} 
#else	
		newFileHeader.OffsetToFontSheet = FourByteAlign(GetNumBytesInLines(translatedLines) + newFileHeader.OffsetToText);
		newFileHeader.FontSheetDataSize = inTranslatedFontSheet.GetDataSize() + inTranslatedFontSheet.GetSizeOfSingleTile(); //repeat first tile
		newFileHeader.SwapByteOrder(); //Convert to little endian

		outFile.WriteData(&newFileHeader, sizeof(newFileHeader));
		outFile.WriteData(sakuraFile.mpHeaderToText, sakuraFile.mHeaderToTextSize);

		fixedHeader.OutputToFile(outFile);
#endif
		
		//Output data
		size_t dataIndex                   = 0;
		size_t translationIndex            = 0;
		const size_t numInsertedLines      = translatedLines.size();
		unsigned int numSingleBytesWritten = 0;
		while (1)
		{
			if (translationIndex < numInsertedLines)
			{
#if USE_SINGLE_BYTE_LOOKUPS
				if (bIsMESFile)
				{
					vector<unsigned short> translationData;
					translatedLines[dataIndex].GetDataArray(translationData);
					outFile.WriteData(translationData.data(), translationData.size() * sizeof(short));
				}
				else
				{
					vector<unsigned char> translationData;
					translatedLines[dataIndex].GetSingleByteDataArray(translationData);
					outFile.WriteData(translationData.data(), translationData.size() * sizeof(char));
					numSingleBytesWritten += translationData.size() * sizeof(char);
				}
#else
				vector<unsigned short> translationData;
				translatedLines[translationIndex].GetDataArray(translationData);
				outFile.WriteData(translationData.data(), translationData.size() * sizeof(short));
#endif		
			}

			++dataIndex;
			++translationIndex;

//			if (dataIndex >= numDataSegments && translationIndex >= numInsertedLines)
			if (translationIndex >= numInsertedLines)
			{
				break;
			}
		}

		//Font data needs to be at a 4 byte aligned address
		const int actualOffsetToFontSheet = SwapByteOrder(newFileHeader.OffsetToFontSheet);
		assert(actualOffsetToFontSheet >= (int)outFile.GetFileSize());
		if (actualOffsetToFontSheet != (int)outFile.GetFileSize())
		{
			const int numBytesToPad = actualOffsetToFontSheet - (int)outFile.GetFileSize();
			for(int n = 0; n < numBytesToPad; ++n)
			{
				const char dummy = 0;
				outFile.WriteData(&dummy, sizeof(dummy));
			}
		}

		inTranslatedFontSheet.OutputTiles(outFile, -1);

		if (outFile.GetFileSize() > MaxTBLFileSize)
		{
			printf("WARNING: TBL file %s is too big: %lu.\n", sakuraFile.mFileNameInfo.mFileName.c_str(), outFile.GetFileSize());
		}

		if(!bInCreateOutput)
		{
			outFile.Close();
			DeleteFile(outFile.GetFileName().c_str());
		}
	}

	printf("InsertText Succeeded\n");

	return true;
}

bool VerifyText(const std::string& inTranslatedDirectory, const std::string& inSourceGameDirectory, const std::string& inPatchedDirectory)
{
	//Create translated font sheet
	const string translatedFontSheetPath = inTranslatedDirectory + Seperators + "8x12.bmp";
	TileExtractor translatedFontSheet;
	PaletteData translatedFontSheetPalette;
	if (!CreateTranslatedFontSheet(translatedFontSheetPath, translatedFontSheet, translatedFontSheetPalette, 16, 16))
	{
		printf("Unable to create font sheet");
		return false;
	}

	//Insert scenario text
	if (!InsertText(inSourceGameDirectory, inTranslatedDirectory, inPatchedDirectory, translatedFontSheet, false, false))
	{
		printf("Text insertion failed\n");
		return false;
	}

	printf("Verification Complete\n");
	return true;
}
