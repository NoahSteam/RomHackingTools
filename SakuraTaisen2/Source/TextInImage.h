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
		if(!mFontSheet.ReadBitmap(pInFontSheetImagePath))
		{
			return false;
		}

		mHDC = CreateDC("DISPLAY", NULL, NULL, NULL);
		HFONT mHFont = CreateFont(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_CHARACTER_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
								 DEFAULT_PITCH | FF_DONTCARE, "Calibri");
		SelectObject(mHDC, mHFont);
		
		int offset = 0;
		for(char letter = ' '; letter <= '~'; ++letter)
		{
			const SIZE size = GetCharacterDimension(letter);
			mCharacterXOffset[letter] = offset;
			mCharacterSizes[letter] = size;
			offset += size.cx + 1; //2 for empty space on left & right of character
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

	void GetCharacterData(char inLetter, std::vector<char>& outData)
	{
		const SIZE characterDimension = GetCharacterDimension(inLetter);
		const int divisor = mFontSheet.mBitmapData.mInfoHeader.mBitCount == 4 ? 2 : 1;
		const int startX = mCharacterXOffset[inLetter] / divisor;
		const int bytesPerRowInImage = mFontSheet.mBitmapData.mInfoHeader.mImageWidth / divisor;
		const int bytesPerRowInTile = (characterDimension.cx + (characterDimension.cx % 2)) / divisor; //needs to be even amount
		char* pImageData   = mFontSheet.mBitmapData.mColorData.mpRGBA;
	
		assert(startX < mFontSheet.mBitmapData.mColorData.mSizeInBytes);

		for (int y = 0; y < mFontSheet.mBitmapData.mInfoHeader.mImageHeight; ++y)
		{
			for (int x = 0; x < bytesPerRowInTile; ++x) //used to be 8 insntead of width
			{
				const int dataIndex = x + startX + (y * bytesPerRowInImage);
				assert(dataIndex < mFontSheet.mBitmapData.mColorData.mSizeInBytes);

				outData.push_back(pImageData[dataIndex]);
			}
		}
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
	BitmapReader mFontSheet;
	std::map<char, int> mCharacterXOffset;
	std::map<char, SIZE> mCharacterSizes;
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

	//Go through all lines of text
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

		BitmapSurface createdImage;
		createdImage.CreateSurface(outputImageData.mBitmapData.mInfoHeader.mImageWidth, 
								   outputImageData.mBitmapData.mInfoHeader.mImageHeight, 
								   BitmapSurface::kBPP_4, 
								   outputImageData.mBitmapData.mPaletteData.mpRGBA, 
								   outputImageData.mBitmapData.mPaletteData.mSizeInBytes);

		//Print each letter
		int currentX = startX;
		const int numLetters = (int)textFile.mLines[i].mFullLine.size() - 1;
		for(int letterIndex = 0; letterIndex < numLetters; ++letterIndex)
		{
			std::vector<char> characterData;
			textInImageInterface.GetCharacterData(textFile.mLines[i].mFullLine[letterIndex], characterData);
			const char* pCharacterData = &characterData.front();
			const SIZE characterDim = textInImageInterface.GetCharacterDimension(textFile.mLines[i].mFullLine[letterIndex]);
			createdImage.AddTile(pCharacterData, (int)characterData.size(), currentX, 0, characterDim.cx, outputImageData.mBitmapData.mInfoHeader.mImageHeight, BitmapSurface::kFlipVert);
			currentX += characterDim.cx;
		}

		std::string outFileName = pInOutputPath + std::to_string(i) + ".bmp";
		createdImage.WriteToFile(outFileName, true);
	}

	return true;
}
