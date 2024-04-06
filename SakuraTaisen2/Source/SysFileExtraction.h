#pragma once
//0x0 4bpp 16x16 font sheet up to 0xE000
//0xE000 to 0xE3A0 text table
//0xE39C start of text entries up to 0x104cA

//Second Long value specifies offset to start of text.  Skip 4 bytes past that to get to text

const int TextTableStart = 0xE000;

class SysFileExtractor
{
	struct FontSheet
	{
		char* mpData{ nullptr };
		unsigned int mDataSize{ 0 };

		~FontSheet()
		{
			delete[] mpData;
			mpData = nullptr;

			mDataSize = 0;
		}
	};

	struct TextHeader
	{
		int mNumEntries{0};
		std::vector<int> mOffsetsToTextEntries;
	};

	struct SysLineId
	{
		uint16 mCharacterID;
		uint16 mSoundID;
	};

public:
	FileNameContainer    mFileNameInfo;
	FileData             mFileData;
	FontSheet            mFontSheetData;
	TextHeader           mTextHeader;
	vector<SakuraString> mLines;
	vector<SysLineId>    mLineIds;

	SysFileExtractor(){}

	SysFileExtractor(SysFileExtractor&& rhs) : 
		mFileNameInfo(std::move(rhs.mFileNameInfo)), 
		mFontSheetData(std::move(rhs.mFontSheetData)),
		mTextHeader(std::move(rhs.mTextHeader)),
		mLines(std::move(rhs.mLines)),
		mLineIds(std::move(rhs.mLineIds))
	{
		rhs.mFontSheetData.mpData = nullptr;
	}

	SysFileExtractor& operator=(SysFileExtractor&& rhs)
	{
		mFileNameInfo = std::move(rhs.mFileNameInfo);
		mLines = std::move(rhs.mLines);
		mLineIds = std::move(rhs.mLineIds);
		mFontSheetData = std::move(rhs.mFontSheetData);
		mTextHeader = rhs.mTextHeader;
	}

	bool Initialize(const string& inFileName)
	{
		mFileNameInfo = FileNameContainer(inFileName.c_str());
		if( !mFileData.InitializeFileData(mFileNameInfo) )
		{
			return false;
		}
		
		return true;
	}

	size_t GetFileSize() const
	{
		return mFileData.GetDataSize();
	}

	void ParseTextHeader()
	{
		if (mFileData.GetDataSize() <= TextTableStart)
		{
			return;
		}

		uint32* pTextEntry = (uint32*)(mFileData.GetData() + TextTableStart);
		mTextHeader.mNumEntries = SwapByteOrder(*pTextEntry);

		for(int i = 1; i <= mTextHeader.mNumEntries; ++i)
		{
			const int offsetToEntry = SwapByteOrder(*(pTextEntry + i));
			mTextHeader.mOffsetsToTextEntries.push_back(offsetToEntry);
		}
	}

	void ParseStrings()
	{
		ParseTextHeader();

		static const unsigned short EndOfLineCharacter = 0xffff;
		const uint16 idTag = 0x7fff;
		const unsigned int NumBytesPerCharacter = 2;

		if(mFileData.GetDataSize() <= TextTableStart)
		{
			return;
		}

		//Find start of text entries
		const int offsetToStringHeader = TextTableStart;
		int offsetToString = 0;
		{
			uint32* pWordBuffer = (uint32*)(mFileData.GetData() + offsetToStringHeader);
			offsetToString = TextTableStart + SwapByteOrder(pWordBuffer[1]);
		}
		if((unsigned long)offsetToString >= mFileData.GetDataSize())
		{
			return;
		}

		uint16* pWordBuffer      = (uint16*)(mFileData.GetData() + offsetToString);
		int currentIndex         = 0;
		SakuraString newLineOfText;
		bool bIsNewEntry = true;
		while (1)
		{
			const unsigned short currValue = SwapByteOrder(pWordBuffer[currentIndex]);
			const unsigned short nextValue = SwapByteOrder(pWordBuffer[++currentIndex]);

			if( currValue == 0 && nextValue == 0 )
			{
				break;
			}

			if( newLineOfText.mChars.size() == 0 )
			{
			//	printf("Line:%i at %08x\n", mLines.size(), currentIndex*2 + offsetToString);
			}

			//Each entry needs starts with an id
			if (bIsNewEntry)
			{
				SysLineId id;
				id.mCharacterID = currValue;
				id.mSoundID = nextValue;
				mLineIds.push_back(id);
				bIsNewEntry = false;
				currentIndex++;
				continue;
			}

			newLineOfText.AddChar(currValue);
			
			if (EndOfLineCharacter == currValue)
			{
				newLineOfText.mOffsetToStringData = 0; //0 in SW2 because there is no ID to each dialog entry
				mLines.push_back(newLineOfText);
				newLineOfText.Clear();
				bIsNewEntry = true;
			}
		}

		assert(mLineIds.size() == mLines.size());
	}

	void ExtractFontSheet(const string& inOutputDirectory, const string& inPaletteFileName)
	{
		if (!CreateDirectoryHelper(inOutputDirectory))
		{
			printf("Output directory %s not found.\n", inOutputDirectory.c_str());
			return;
		}
		
		FileData paletteFileData;
		FileNameContainer paletteFileName(inPaletteFileName.c_str());
		if (!paletteFileData.InitializeFileData(paletteFileName))
		{
			return;
		}

		ReadInFontSheetData();

		const string extension(".bmp");

		//Create output directory for this file
		const string outFileName = inOutputDirectory + mFileNameInfo.mNoExtension + extension;

		SakuraFontSheet sakuraFontSheet;
		if (sakuraFontSheet.CreateFontSheetFromData(mFontSheetData.mpData, mFontSheetData.mDataSize))
		{
			ExtractImageFromData(mFontSheetData.mpData, mFontSheetData.mDataSize, outFileName, paletteFileData.GetData(), paletteFileData.GetDataSize(),
								 paletteFileData.GetDataSize() == 32, 16, 16, 255, 256, 0, true, true);
		}
	}

	void DumpText(const string& inOutputDirectory, const PaletteData& inPaletteData)
	{
		if( mFileData.GetDataSize() < TextTableStart)
		{
			return;
		}

		ReadInFontSheetData();
		ParseStrings();

		//Create font sheet
		SakuraFontSheet sakuraFontSheet;
		if (!sakuraFontSheet.CreateFontSheetFromData(mFontSheetData.mpData, mFontSheetData.mDataSize))
		{
			return;
		}

		const string extension(".png");

		//Dump out the dialog for each line
		int stringIndex = 0;
		const int tileDim = 16;
		for (size_t lineIndex = 0; lineIndex < mLines.size(); ++lineIndex)
		{
			const SakuraString& sakuraString = mLines[lineIndex];
			if (sakuraString.mChars.size() > 255)
			{
				continue;
			}

			const int numSakuraLines = sakuraString.GetNumberOfLines();

			BitmapSurface sakuraStringBmp;
			sakuraStringBmp.CreateSurface(SakuraString::MaxCharsPerLine * tileDim, tileDim * numSakuraLines, BitmapSurface::EBitsPerPixel::kBPP_4, 
										  inPaletteData.GetData(), inPaletteData.GetSize());

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
			sakuraStringBmp.WriteToFile(bitmapName);

			++stringIndex;
		}
	}

private:

	void ReadInFontSheetData()
	{
		delete[] mFontSheetData.mpData;

		mFontSheetData.mDataSize = 0xE000;

		mFontSheetData.mpData = new char[mFontSheetData.mDataSize];
		memcpy_s(mFontSheetData.mpData, mFontSheetData.mDataSize, mFileData.GetData(), mFontSheetData.mDataSize);
	}
};

void ExtractSysFiles(const string& rootSakuraDirectory, const string& inPaletteFileName, const string& outDirectory)
{
	CreateDirectoryHelper(outDirectory);

	const string sakura2Directory = rootSakuraDirectory;// + "SAKURA2\\";

	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(sakura2Directory, allFiles);

	vector<FileNameContainer> scenarioFiles;
	GetAllFilesOfType(allFiles, ".MES", scenarioFiles);
	GetAllFilesOfType(allFiles, "LOW.BIN", scenarioFiles);

	//Create palette data
	FileData paletteFileData;
	FileNameContainer paletteFileName(inPaletteFileName.c_str());
	if (!paletteFileData.InitializeFileData(paletteFileName))
	{
		return;
	}

	PaletteData paletteData;
	paletteData.CreateFrom15BitData(paletteFileData.GetData(), paletteFileData.GetDataSize());
	//

	//Dump files
	for(const FileNameContainer& fileName : scenarioFiles)
	{
		printf("Extracting %s\n", fileName.mFileName.c_str());

		SysFileExtractor extractor;
		if (extractor.Initialize(fileName.mFullPath))
		{
			const string sysOutDirectory = outDirectory + fileName.mNoExtension + Seperators;
			CreateDirectoryHelper(sysOutDirectory);

			extractor.DumpText(sysOutDirectory, paletteData);
		}
	}
}

void ParseAllSysFiles(const vector<FileNameContainer>& inFiles, vector<SysFileExtractor>& outText)
{
	outText.reserve(inFiles.size());

	for (const FileNameContainer& fileName : inFiles)
	{
		printf("Extracting: %s\n", fileName.mFileName.c_str());

		outText.emplace_back();
		SysFileExtractor& extractor = outText.back();
		if (!extractor.Initialize(fileName.mFullPath))
		{
			continue;
		}

		if (extractor.GetFileSize() == 0)
		{
			continue;
		}

		extractor.ParseStrings();
	}
}

void ExtractSysTextCode(const string& inSearchDirectory, const string& inOutputDirectory)
{
	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inSearchDirectory, allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> scenarioFiles;
	GetAllFilesOfType(allFiles, ".MES", scenarioFiles);
	GetAllFilesOfType(allFiles, "LOW.BIN", scenarioFiles);

	//Extract the text
	vector<SysFileExtractor> sakuraTextFiles;
	ParseAllSysFiles(scenarioFiles, sakuraTextFiles);

	CreateDirectoryHelper(inOutputDirectory);

	//Write out hex code for all of the lines found in the sakura text files
	const string txt(".txt");
	const size_t numFiles = sakuraTextFiles.size();
	for (size_t i = 0; i < numFiles; ++i)
	{
		const SysFileExtractor& sakuraText = sakuraTextFiles[i];
		const string outFileName = inOutputDirectory + sakuraText.mFileNameInfo.mNoExtension + txt;

		FILE* pOutFile = nullptr;
		errno_t errorValue = fopen_s(&pOutFile, outFileName.c_str(), "w");
		if (errorValue)
		{
			printf("Unable to open out file: %s.  Error code: %i \n", outFileName.c_str(), errorValue);
			continue;
		}

		printf("Dumping dialog for: %s\n", sakuraText.mFileNameInfo.mNoExtension.c_str());

		//Dump out the dialog for each line
		int stringIndex = 0;
		const int tileDim = 16;
		for (size_t lineIndex = 0; lineIndex < sakuraText.mLines.size(); ++lineIndex)
		{
			const SakuraString& sakuraString = sakuraText.mLines[lineIndex];
			if (sakuraString.mChars.size() > 255)
			{
				continue;
			}

			for (const SakuraString::SakuraChar& sakuraChar : sakuraString.mChars)
			{
				fprintf(pOutFile, "%02x%02x ", sakuraChar.mRow, sakuraChar.mColumn);
			}

			fprintf(pOutFile, "\n");
		}
	}
}
