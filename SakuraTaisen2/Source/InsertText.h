#pragma once

struct SakuraTextFileFixedHeader
{
	struct StringInfo
	{
		unsigned short stringId;
		unsigned short offsetFromTableStart;

		explicit StringInfo(unsigned short inStringId, unsigned short inOffsetFromTableStart) : stringId(SwapByteOrder(inStringId)), offsetFromTableStart(SwapByteOrder(inOffsetFromTableStart))
		{
		}
	};

#pragma pack(push, 2)
	struct StringInfo8Bytes
	{
		unsigned int stringId;
		unsigned int offsetFromTableStart;

		explicit StringInfo8Bytes(unsigned int inStringId, unsigned int inOffsetFromTableStart) : stringId(SwapByteOrder(inStringId)), offsetFromTableStart(SwapByteOrder(inOffsetFromTableStart))
		{
		}
	};
#pragma pack(pop)

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
		mStringInfo.push_back(StringInfo(inInfo[0].mStringId, inInfo[0].mNumCharactersPrecedingThisString));
		mStringInfo8Bytes.push_back(StringInfo8Bytes(inInfo[0].mStringId, inInfo[0].mNumCharactersPrecedingThisString));

		int totalCharCount = 0;
		unsigned short prevValue = 0;
		unsigned int   prevValue8Bytes = 0; prevValue8Bytes = 0;
		const size_t numEntries = inInfo.size() - 1;
		for (size_t i = 0; i < numEntries; ++i)
		{
#if USE_SINGLE_BYTE_LOOKUPS
			const unsigned short newOffset = bIsMesFile ? (unsigned short)inStrings[i].mChars.size() + prevValue : (unsigned short)inStrings[i].mChars.size() + 3 + prevValue; //+ trailingZeros;
			const unsigned int   newOffset8bytes = inStrings[i].mChars.size() + 3 + prevValue8Bytes; //+ trailingZeros;
			totalCharCount += inStrings[i].mChars.size() + 3;
			prevValue8Bytes = newOffset8bytes;
#else
			const unsigned short newOffset = (unsigned short)inStrings[i].mChars.size() + prevValue; //+ trailingZeros;
			totalCharCount += (unsigned short)inStrings[i].mChars.size();
#endif
			prevValue = newOffset;
			mStringInfo.push_back(StringInfo(inInfo[i + 1].mStringId, newOffset));

#if USE_4_BYTE_OFFSETS
			mStringInfo8Bytes.push_back(StringInfo8Bytes(inInfo[i + 1].mStringId, newOffset8bytes));
#endif
		}

		const bool bCheckTotalCharError = !USE_4_BYTE_OFFSETS || bIsMesFile;
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

		if ((unsigned short)(sizeof(int) + sizeof(int) + sizeof(int) * inInfo.size() + sizeof(int) * inInfo.size()) >> 1 > 0xffff)
		{
			printf("\nERROR:Translated file %s string table exceeds 2 byte limit\n", inSakuraFile.mFileNameInfo.mFileName.c_str());
			return false;
		}

		mOffsetToTable = inSakuraFile.mFileHeader.OffsetToTextHeader;
		mOffsetToTable8Bytes = (sizeof(int) + sizeof(int) + sizeof(int) * inInfo.size() + sizeof(int) * inInfo.size()) >> 1;

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
		const int totalSize = SwapByteOrder(mStringInfo.size() * sizeof(SakuraTextFileFixedHeader::StringInfo));
		outFile.WriteData(&totalSize, sizeof(totalSize));

		for(StringInfo& stringInfo : mStringInfo)
		{
			outFile.WriteData(&stringInfo.stringId, sizeof(stringInfo.stringId));
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

bool InsertText(const string& inRootSakuraTaisenDirectory, const string& inTranslatedTextDirectory, const string& outDirectory, 
                TileExtractor& inTranslatedFontSheet, bool bInOutputToCorrespondingDirectory = false)
{
#define IncrementLine()\
    ++currLine;\
    charCount = 0;\
    if( currLine > maxLines ) \
	{\
		printf("Unable to fully insert line because it is longer than %i characters: %s\n", maxLines*maxCharsPerLine, textLine.mFullLine.c_str());\
		bFailedToAddLine = true;\
	}\
    translatedString.AddChar( GTranslationLookupTable.GetIndex('\n') );

	//Insert new line if needed
#define ConditionallyAddNewLine()\
	if( word.size() + charCount > maxCharsPerLine )\
	{\
		if( bIsLipsEntry && !bAlreadyShowedError )\
		{\
			printf("LIPS line is too long[D %i].  Needs to be a max of %i characters long. %s\n", translatedLineIndex + 1, maxCharsPerLine, textLine.mFullLine.c_str());\
			bAlreadyShowedError = true;\
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
	FindAllSakuraText(scenarioFiles, sakuraTextFiles);
	///////////////
	
	//Find all translated text files
	vector<FileNameContainer> translatedTextFiles;
	const string translatedFileDirectory = inTranslatedTextDirectory + Seperators + "Translation";
	FindAllFilesWithinDirectory(translatedFileDirectory, translatedTextFiles);

	const string UntranslatedEnglishString("Untranslated");
	const string UnusedEnglishString("Unused");
	const bool bIsMESFile = false;
	const string outputDirectory = outDirectory + Seperators + "SAKURA1\\";

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
			if (translatedFileName.mNoExtension.find(sakuraFile.mFileNameInfo.mNoExtension) != string::npos)
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
				printf("Unable to translate file: %s because the translation has too many lines.\n", pMatchingTranslatedFileName->mNoExtension.c_str());
				return false;
			}

			//Get converted lines of text
			const int maxCharsPerLine             = 27;//(240 / OutTileSpacingX);
			const int maxLines                    = MaxLines;
			const size_t numTranslatedLines       = translatedFile.mLines.size();
			unsigned short numCharsPrintedForMES  = 0;
			unsigned short numCharsPrintedFortTBL = 0;

			auto UpdateNumTimingCharsPrinted = [&](int inTranslatedLineIndex, const SakuraString& inTranslatedString)->bool
			{
				if (sakuraFile.mLines[inTranslatedLineIndex].mChars[0].mIndex != 0)
				{
					const short numLinesInString = static_cast<short>(inTranslatedString.GetNumberOfLines()) - 1;
					if (numLinesInString < 0)
					{
						printf("Invalid number of lines in string\n");
						return false;
					}

					//-3 because 2 for the initial bytes and one for the trailing zero
					const unsigned short numCharsPrintedInLine = (unsigned short)inTranslatedString.GetNumberOfActualCharacters();//(unsigned short)inTranslatedString.GetNumberOfPrintedCharacters() - 3;//(static_cast<short>(translatedString.mChars.size()) - 3 - numLinesInString);
					if ((short)numCharsPrintedInLine < 0)
					{
						printf("Invalid number of characters in string\n");
						return false;
					}

#if FIX_TIMING_DATA
					if (1)
					{
						if ((int)sakuraFile.mLineTimingData.size() < inTranslatedLineIndex)
						{
							printf("Not enough timing data lines in %s.  Expected %i, got %i", sakuraFile.mFileNameInfo.mFileName.c_str(), inTranslatedLineIndex, sakuraFile.mLineTimingData.size());

							numCharsPrintedFortTBL += (numCharsPrintedInLine + 1) * 2;
						}
						else
						{
							const int numPrefixBytes      = sakuraFile.mLineTimingData[inTranslatedLineIndex].GetNumPrefixBytes();
							const int originalTimingCount = (int)sakuraFile.mLineTimingData[inTranslatedLineIndex].GetNumBytesInLine_NoKeyValues();
							int finalPrintedCount         = numCharsPrintedInLine + originalTimingCount + 2;

							//Extra 0x6e bytes will be added
							if (numCharsPrintedInLine >= (originalTimingCount - numPrefixBytes))
							{
								finalPrintedCount += (numCharsPrintedInLine - originalTimingCount) + numPrefixBytes;
							}

							if (finalPrintedCount >= 65536)
							{
								printf("FinalPrintedCount is too big for %s line %i\n", sakuraFile.mFileNameInfo.mNoExtension.c_str(), inTranslatedLineIndex);
							}
							numCharsPrintedFortTBL += (unsigned short)finalPrintedCount;

							//Add extra byte to make things two byte aligned
							if (finalPrintedCount % 2 != 0)
							{
								numCharsPrintedFortTBL += 1;
								finalPrintedCount += 1;
							}

							lineTimingMap[inTranslatedLineIndex] = finalPrintedCount;
						}
					}
#else
					numCharsPrintedFortTBL += (numCharsPrintedInLine + 1) * 2;
#endif

					numCharsPrintedForMES += numCharsPrintedInLine * 2 + 1;
				}

				return true;
			};

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
				const bool bIsLipsEntry =  false;//sakuraFile.mSequenceEntries[currSakuraStringIndex].mbIsLips;
				const bool bIsUnused = false;

				if (bIsLipsEntry)
				{
					if (textLine.GetNumberOfLines() != sakuraFile.mLines[currSakuraStringIndex].GetNumberOfLines())
					{
						printf("LIPS ERROR - Translated LIPS line does not have expected number of options. Expected: %i, Has: %i (File: %s Line: %u)\n", sakuraFile.mLines[currSakuraStringIndex].GetNumberOfLines(), textLine.GetNumberOfLines(), sakuraFile.mFileNameInfo.mFileName.c_str(), currSakuraStringIndex + 1);
					}
				}

				if (textLine.mWords[0] == UntranslatedEnglishString ||
					(textLine.mWords[0] == UnusedEnglishString && !bIsUnused)
					)
				{
					printf("Error: Untranslated line. (File: %s Line: %u)\n", sakuraFile.mFileNameInfo.mFileName.c_str(), currSakuraStringIndex + 1);
				}

				//If untranslated, then write out the file and line number
				if (// bIsUnused ||
					textLine.mWords.size() == 0 ||
					(textLine.mWords.size() == 1 && (textLine.mWords[0] == UntranslatedEnglishString || textLine.mWords[0] == UnusedEnglishString))
					)
				{
					bool bUseShorthand = false;
#if USE_SINGLE_BYTE_LOOKUPS && USE_4_BYTE_OFFSETS 
					if (numTranslatedLines > 1200)
					{
						bUseShorthand = true;
					}
#endif

					if (textLine.mWords.size() == 0)
					{
						printf("Warning: Blank line. (File: %s Line: %u)\n", sakuraFile.mFileNameInfo.mFileName.c_str(), currSakuraStringIndex + 1);
					}

					SakuraString translatedSakuraString;
					unsigned short timingData = 0;
					if (sakuraFile.mLines[translatedLineIndex].mChars[0].mIndex != 0 && textLine.mFullLine.size() > 1)
					{
						timingData = (numCharsPrintedFortTBL >> 1);
					}
					
					const string baseUntranslatedString = sakuraFile.mFileNameInfo.mNoExtension + string(":");
					const string untranslatedString = bUseShorthand && !timingData ? "U" : baseUntranslatedString + std::to_string(translatedLineIndex + 1); //Just print out a U for unused lines to save space

					translatedSakuraString.AddString(untranslatedString, sakuraFile.mLines[currSakuraStringIndex].mChars[0].mIndex, timingData, true);
					UpdateNumTimingCharsPrinted(translatedLineIndex, translatedSakuraString);

					translatedLines.push_back(translatedSakuraString);
					continue;
				}
				
				for (size_t wordIndex = 0; wordIndex < numWords; ++wordIndex)
				{
					const string& word = textLine.mWords[wordIndex];
					bool bFailedToAddLine = false;
					if (word.size() > maxCharsPerLine)
					{
						printf("Unable to insert word because it is longer than %i characters: %s[%i]\n", maxCharsPerLine, word.c_str(), word.size());
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
								printf("LIPS line is too long[A].  Needs to be a max of %i characters long. %s\n", maxCharsPerLine, textLine.mFullLine.c_str());
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

					//Insert new line if needed
					ConditionallyAddNewLine();

					//Add the word
					const size_t numLettersInWord = word.size();
					if (word.size() + charCount > maxCharsPerLine)
					{
						if (bIsLipsEntry && !bAlreadyShowedError)
						{
							printf("LIPS line is too long [B].  Needs to be a max of %i characters long. %s\n", maxCharsPerLine, textLine.mFullLine.c_str());
							bAlreadyShowedError = true;
							//	break;
						}
						else
						{
							IncrementLine();
						}
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

					//Add sapce
					if (wordIndex + 1 < numWords)
					{
						//Insert new line if needed
						const string& nextWord = textLine.mWords[wordIndex + 1];
						if (nextWord != NewLineWord)
						{
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
										printf("LIPS line is too long[C].  Needs to be a max of %i characters long. %s\n", maxCharsPerLine, textLine.mFullLine.c_str());
										bAlreadyShowedError = true;
										//		break;
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
				}

#if USE_SINGLE_BYTE_LOOKUPS
				if ((translatedString.mChars.size()) % 2 != 0)
				{
					translatedString.AddChar(' ');
				}
#endif

				//String ends with ffff
				translatedString.AddChar(0xffff);

				UpdateNumTimingCharsPrinted(translatedLineIndex, translatedString);
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

				UpdateNumTimingCharsPrinted(currSakuraStringIndex, translatedSakuraString);

				translatedLines.push_back(translatedSakuraString);
			}

		}//if(pMatchingTranslatedFileName)
		else
		{
			//Fill out everything else with "Untranslated"
			const string baseUntranslatedString = sakuraFile.mFileNameInfo.mNoExtension + string(": ");
			for (size_t i = 0; i < sakuraFile.mLines.size(); ++i)
			{
				const string untranslatedString = baseUntranslatedString + std::to_string(i + 1);
				SakuraString translatedSakuraString;
				translatedSakuraString.AddString(untranslatedString, sakuraFile.mLines[i].mChars[0].mIndex, 0, !bIsMESFile);
				translatedLines.push_back(translatedSakuraString);
			}
		}

		SakuraTextFileFixedHeader fixedHeader;
		fixedHeader.CreateFixedHeader(sakuraFile.mStringInfoArray, sakuraFile, translatedLines, bIsMESFile);

		//Write header
#if USE_4_BYTE_OFFSETS
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
			const unsigned int bigEndianOffsetToTable = SwapByteOrder(fixedHeader.mOffsetToTable8Bytes);
			const unsigned int bigEndianTableEnd = SwapByteOrder(fixedHeader.mTableEnd8Bytes);
			outFile.WriteData(&bigEndianOffsetToTable, sizeof(bigEndianOffsetToTable));
			outFile.WriteData(&bigEndianTableEnd, sizeof(bigEndianTableEnd));
			outFile.WriteData(&fixedHeader.mStringInfo8Bytes[0], fixedHeader.mStringInfo8Bytes.size() * sizeof(fixedHeader.mStringInfo8Bytes[0]));
		} 
#else
		//Create file header
		SakuraTextFile::SakuraHeader newFileHeader = sakuraFile.mFileHeader;
		newFileHeader.OffsetToFontSheet = GetNumBytesInLines(translatedLines) + newFileHeader.OffsetToText;
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

			++translationIndex;

			if (translationIndex >= numInsertedLines)
			{
				break;
			}
		}

		if (numSingleBytesWritten > 0 && numSingleBytesWritten % 2 != 0)
		{
			char dummy = 0;
			outFile.WriteData(&dummy, sizeof(dummy));
		}

		inTranslatedFontSheet.OutputTiles(outFile, -1);

		if (outFile.GetFileSize() > MaxTBLFileSize)
		{
			printf("WARNING: TBL file %s is too big: %lu.\n", sakuraFile.mFileNameInfo.mFileName.c_str(), outFile.GetFileSize());
		}
	}

	printf("InsertText Succeeded\n");

	return true;
}
