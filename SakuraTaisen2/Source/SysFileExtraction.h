#pragma once
//0x0 4bpp 16x16 font sheet up to 0xE000
//0xE000 to 0xE3A0 text table
//0xE39C start of text entries up to 0x104cA

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

	FileNameContainer    mFileName;
	FileData             mFileData;
	FileData             mPaletteFileData;
	FontSheet            mFontSheetData;
	vector<SakuraString> mLines;

public:
	bool Initialize(const string& inFileName, const string &inPaletteFileName)
	{
		mFileName = FileNameContainer(inFileName.c_str());
		if( !mFileData.InitializeFileData(mFileName) )
		{
			return false;
		}

		FileNameContainer paletteFileName(inPaletteFileName.c_str());
		if( !mPaletteFileData.InitializeFileData(paletteFileName) )
		{
			return false;
		}

		return true;
	}

	void ParseStrings()
	{
		static const unsigned short EndOfLineCharacter = 0xffff;
		const uint16 idTag = 0x7fff;
		const unsigned int NumBytesPerCharacter = 2;

		const int offsetToString = 0xE39C;
		uint16* pWordBuffer      = (uint16*)(mFileData.GetData() + offsetToString);
		int currentIndex         = 0;
		SakuraString newLineOfText;

		while (1)
		{
			const unsigned short currValue = SwapByteOrder(pWordBuffer[currentIndex++]);
			const unsigned short nextValue = SwapByteOrder(pWordBuffer[currentIndex]);

			if( nextValue == idTag)
			{
				++currentIndex;
				continue;
			}

			if( currValue == 0 )
			{
				break;
			}

			if( newLineOfText.mChars.size() == 0 )
			{
				printf("Line:%i at %08x\n", newLineOfText.GetNumberOfLines(), currentIndex + offsetToString);
			}

			newLineOfText.AddChar(currValue);
			
			if (EndOfLineCharacter == currValue)
			{
				newLineOfText.mOffsetToStringData = 0; //0 in SW2 because there is no ID to each dialog entry
				mLines.push_back(newLineOfText);
				newLineOfText.Clear();
			}
		}
	}

	void ExtractFontSheet(const string& inOutputDirectory)
	{
		if (!CreateDirectoryHelper(inOutputDirectory))
		{
			printf("Output directory %s not found.\n", inOutputDirectory.c_str());
			return;
		}

		ReadInFontSheetData();

		const string extension(".bmp");

		//Create output directory for this file
		const string outFileName = inOutputDirectory + mFileName.mNoExtension + extension;

		SakuraFontSheet sakuraFontSheet;
		if (sakuraFontSheet.CreateFontSheetFromData(mFontSheetData.mpData, mFontSheetData.mDataSize))
		{
			ExtractImageFromData(mFontSheetData.mpData, mFontSheetData.mDataSize, outFileName, mPaletteFileData.GetData(), mPaletteFileData.GetDataSize(),
				16, 16, 255, 256, 0, true, true);
		}
	}

	void DumpText(const string& inOutputDirectory)
	{
		ReadInFontSheetData();
		ParseStrings();

		//Create font sheet
		SakuraFontSheet sakuraFontSheet;
		if (!sakuraFontSheet.CreateFontSheetFromData(mFontSheetData.mpData, mFontSheetData.mDataSize))
		{
			return;
		}

		PaletteData paletteData;
		paletteData.CreateFrom15BitData(mPaletteFileData.GetData(), mPaletteFileData.GetDataSize());

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

				const char* pData = sakuraFontSheet.GetCharacterTile(sakuraChar);

				sakuraStringBmp.AddTile(pData, sakuraFontSheet.GetTileSizeInBytes(), currCol * 16, currRow * 16, tileDim, tileDim);

				++currCol;
			}

			const string bitmapName = inOutputDirectory + std::to_string(stringIndex + 1) + extension;
			sakuraStringBmp.WriteToFile(bitmapName);

			++stringIndex;
		}
	}

private:	void ReadInFontSheetData()
	{
		delete[] mFontSheetData.mpData;

		mFontSheetData.mDataSize = 0xE000;

		mFontSheetData.mpData = new char[mFontSheetData.mDataSize];
		memcpy_s(mFontSheetData.mpData, mFontSheetData.mDataSize, mFileData.GetData(), mFontSheetData.mDataSize);
	}
};
