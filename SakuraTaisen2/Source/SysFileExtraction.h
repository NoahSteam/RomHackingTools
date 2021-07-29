#pragma once
//0x0 4bpp 16x16 font sheet up to 0xE000
//0xE000 to 0xE3A0 text table
//0xE398 start of text entries up to 0x104cA

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

	void Initialize(const char* pFileName, const char* pPaletteFileName)
	{

	}
	/*void ParseStrings()
	{
		static const unsigned short EndOfLineCharacter = 0xffff;
		const unsigned int NumBytesPerCharacter = 2;

		while (1)
		{
			const unsigned short currValue = SwapByteOrder(pWordBuffer[currentIndex++]);

			SakuraString newLineOfText;
			newLineOfText.AddChar(currValue);
			
			if (EndOfLineCharacter == currValue)
			{
				break;
			}

			if ((currentIndex + offsetToEntry) >= (unsigned int)TextDataSize)
			{
				break;
			}
		}

		newLineOfText.mOffsetToStringData = 0; //0 in SW2 because there is no ID to each dialog entry
		mLines.push_back(newLineOfText);

		if ((currentIndex + offsetToEntry) >= (unsigned int)TextDataSize)
		{
			break;
		}
	}*/

	void ReadInFontSheetData()
	{
		mFontSheetData.mDataSize = 0xE000;

		mFontSheetData.mpData = new char[mFontSheetData.mDataSize];
		memcpy_s(mFontSheetData.mpData, mFontSheetData.mDataSize, mFileData.GetData(), mFontSheetData.mDataSize);
	}

	void ExtractFontSheet(const string& inOutputDirectory)
	{
		if (!CreateDirectoryHelper(inOutputDirectory))
		{
			printf("Output directory %s not found.\n", inOutputDirectory.c_str());
			return;
		}

		const string extension(".bmp");

		//Write out bitmaps for all of the lines found in the sakura text files
		{
			//Create output directory for this file
			const string outFileName = inOutputDirectory + mFileName.mNoExtension + extension;

			SakuraFontSheet sakuraFontSheet;
			if (sakuraFontSheet.CreateFontSheetFromData(mFontSheetData.mpData, mFontSheetData.mDataSize))
			{
				ExtractImageFromData(mFontSheetData.mpData, mFontSheetData.mDataSize, outFileName, mPaletteFileData.GetData(), mPaletteFileData.GetDataSize(),
					16, 16, 255, 256, 0, true, true);
			}

		}
	}
};