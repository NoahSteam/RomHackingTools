#pragma once

class TextInImage
{
public:
	~TextInImage()
	{
		if(mHDC)
		{
			ReleaseDC(NULL, mHDC);
			mHDC = 0;
		}
		
		if(mHFont)
		{
			DeleteObject(mHFont);
			mHFont = 0;
		}
	}

	bool Initialize(const char* pInFontSheetImagePath)
	{
		BitmapReader patchedImageData;
		if(!patchedImageData.ReadBitmap(pInFontSheetImagePath))
		{
			return false;
		}

		mHDC = CreateDC("DISPLAY", NULL, NULL, NULL);
		HFONT mHFont = CreateFont(11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
								 DEFAULT_PITCH | FF_DONTCARE, "Calibri");
		SelectObject(mHDC, mHFont);
		
		int offset = 0;
		for(char letter = ' '; letter <= '~'; ++letter)
		{
			mCharacterXOffset[letter] = offset;
			offset += GetCharacterDimension(letter).cx + 2; //2 for empty space on left & right of character
		}

		return true;
	}

	int GetTextWidth(const std::string& inString)
	{
		SIZE totalSize;
		totalSize.cx = totalSize.cy = 0;

		GetTextExtentPoint32(mHDC, inString.c_str(), inString.length(), &totalSize);

		return totalSize.cx;
	}

	const char* GetCharacterData(char inLetter)
	{
		return nullptr;
	}

	SIZE GetCharacterDimension(char inLetter) const
	{
		static char buffer[3] = { 0, 0, 0 };
		snprintf(buffer, 3, "%c", inLetter);

		SIZE size;
		GetTextExtentPoint32(mHDC, (char*)(&buffer[0]), 1, &size);

		return size;
	}

private:
	HDC mHDC{0};
	HFONT mHFont{0};
	std::map<char, int> mCharacterXOffset;
};

bool WriteTextIntoImage(const std::string& pInFontSheetName, const std::string& pPatchedImagePath, const std::string& pTextFilePath, const std::string& pInOutputPath)
{
	TextInImage textInImageInterface;
	if(!textInImageInterface.Initialize(pInFontSheetName.c_str()))
	{
		return false;
	}

	BitmapReader outputImageData;
	if (!outputImageData.ReadBitmap(pPatchedImagePath))
	{
		return false;
	}

	FileNameContainer textFileName(pTextFilePath);
	TextFileData textFile(textFileName);
	if(!textFile.InitializeTextFile(false, false))
	{
		return false;
	}

	const size_t numLines = textFile.mLines.size();
	for(size_t i = 0; i < numLines; ++i)
	{
		const int textWidth = textInImageInterface.GetTextWidth(textFile.mLines[i].mFullLine);
		const int startX = outputImageData.mBitmapData.mInfoHeader.mImageWidth/2 - textWidth/2;
		assert(startX >= 0);

		if(textWidth > outputImageData.mBitmapData.mInfoHeader.mImageWidth)
		{
			continue;
		}

		int currentX = startX;
		const size_t numLetters = textFile.mLines[i].mFullLine.size();
		for(size_t letterIndex = 0; letterIndex < numLetters; ++letterIndex)
		{
			const char* pCharacterData = textInImageInterface.GetCharacterData(textFile.mLines[i].mFullLine[letterIndex]);
			const SIZE characterDim = textInImageInterface.GetCharacterDimension(textFile.mLines[i].mFullLine[letterIndex]);
			printf("%i\n", characterDim.cx);
		//	outputImageData.WriteTile(pCharacterData, currentX, characterDim.cx, characterDim.cy);
		}
	}

	return true;
}
