#pragma once

class TextInImage
{
public:
	~TextInImage()
	{
		// Release the font
		DeleteObject(mhFont);
	}

	unsigned int GetClosestColorIndex(unsigned int InColorValue)
	{
		std::map<unsigned int, unsigned int>::const_iterator foundValue = mOrigPaletteColorToIndex.find(InColorValue);

		if( foundValue != mOrigPaletteColorToIndex.end())
		{
			return foundValue->second; //return index into palette
		}
	
		unsigned int closestMatch = 0;
		unsigned int closestMatchDiff = 0xffffffff;
		unsigned int closestIndex = 0;
		for( std::map<unsigned int, unsigned int>::const_iterator iter = mOrigPaletteColorToIndex.begin(); iter != mOrigPaletteColorToIndex.end(); ++iter )
		{
			unsigned int colorValue = iter->first;
			const unsigned int bigValue = colorValue > closestMatch ? colorValue : closestMatch;
			const unsigned int smallValue = colorValue > closestMatch ? closestMatch : colorValue;
			if( bigValue - smallValue < closestMatchDiff )
			{
				closestMatch = colorValue;
				closestMatchDiff = bigValue - smallValue;
				closestIndex = iter->second;
			}
		}
		
		return closestIndex;
	}

	bool Initialize(const char* pInSourceImage)
	{
		if( !mSourceImage.ReadBitmap(pInSourceImage) )
		{
			return false;
		}

		//Create palette lookup table for source image
		for( int i = 0; i < 16; ++i )
		{
			uint8* pColorData = (uint8*)&mSourceImage.mBitmapData.mPaletteData.mpRGBA[i*4];
			const unsigned int colorValue = (pColorData[2] << 16) + (pColorData[1] << 8) + (pColorData[0]);
			mOrigPaletteColorToIndex[colorValue] = i;
		}

		mhFont = CreateFont(13, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Calibri");

		return true;
	}

	bool OutputImageWithText(const char* pInText, const char* pInOutputImageName)
	{
		HBITMAP hBitmap = (HBITMAP)LoadImage(NULL, mSourceImage.GetFileName().c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		if(!hBitmap)
		{
			return false;
		}

		// Create a device context for the image
		HDC hdcImage = CreateCompatibleDC(NULL);
		SelectObject(hdcImage, hBitmap);

		// Set the text color
		SetTextColor(hdcImage, RGB(0, 0, 0));

		// Set the text background color
		SetBkColor(hdcImage, TRANSPARENT);
		SetBkMode(hdcImage, TRANSPARENT);

		// Set the font
		SelectObject(hdcImage, mhFont);

		// Get Bitmap Info
		BITMAP bitmap;
		GetObject(hBitmap, sizeof(BITMAP), &bitmap);

		const int colorBufferSize = bitmap.bmHeight * bitmap.bmWidthBytes;
		char* pColorBuffer = new char[colorBufferSize];
		char* pFlippedImage = new char[colorBufferSize];

		//Get image colors
		GetBitmapBits(hBitmap, colorBufferSize, pColorBuffer);

		//Get palette values
		std::set<unsigned int> newColors;
		for(int y = 0; y < bitmap.bmHeight; ++y)
		{
			for(int x = 0; x < bitmap.bmWidth; ++x)
			{
				const int offset = y*bitmap.bmWidthBytes + x;
				const uint8* pSourceColor = (uint8*)&pColorBuffer[y*bitmap.bmWidthBytes + x*4];
				const unsigned int colorValue = (pSourceColor[2] << 16) + (pSourceColor[1] << 8) + (pSourceColor[0]);
				newColors.insert(colorValue);
			}
		}

		SIZE textSize;
		const size_t textLen = strlen(pInText);
		GetTextExtentPoint32(hdcImage, pInText, textLen, &textSize);

		// Draw the text on the image
		const int yPosition = 0;//bitmap.bmHeight/2 - (textSize.cy/2)
		TextOut(hdcImage, bitmap.bmWidth/2 - (textSize.cx/2), yPosition, pInText, textLen);

		//Get image data again now that it has been rendered
		GetBitmapBits(hBitmap, colorBufferSize, pColorBuffer);

		//Flip data
		std::set<unsigned int> originalColors;
		int origY = 0;
		for(int y = bitmap.bmHeight - 1; y >= 0; --y, origY++)
		{
			for(int x = 0; x < bitmap.bmWidthBytes; ++x)
			{
				const int offset = y*bitmap.bmWidthBytes + x;
				pFlippedImage[offset] = pColorBuffer[origY*bitmap.bmWidthBytes + x];
			}
		}

		const unsigned int palettedImageSize = (bitmap.bmWidth * bitmap.bmHeight) / 2;
		char* pPalettedImage = new char[palettedImageSize];

		//Output 4bpp paletted image
		const int bytesPerPixelInHBitmap = 4;
		for(int y = 0; y < bitmap.bmHeight; ++y)
		{
			for(int x = 0, outX = 0; x < bitmap.bmWidth; x += 2, ++outX)
			{
				//1st pixel
				const int offset1 = y*bitmap.bmWidthBytes + x*bytesPerPixelInHBitmap;
				uint8* pSourceColor1 = (uint8*)&pFlippedImage[offset1];
				const unsigned int colorValue1 = (pSourceColor1[2] << 16) + (pSourceColor1[1] << 8) + (pSourceColor1[0]);
	
				//2nd pixel
				const int offset2 = offset1 + 4;
				uint8* pSourceColor2 = (uint8*)&pFlippedImage[offset2];
				const unsigned int colorValue2 = (pSourceColor2[2] << 16) + (pSourceColor2[1] << 8) + (pSourceColor2[0]);

				const unsigned int paletteIndex1 = GetClosestColorIndex(colorValue1);
				const unsigned int paletteIndex2 = GetClosestColorIndex(colorValue2);
							 
				const uint8 outValue = (paletteIndex1 << 4) + (paletteIndex2);
				const unsigned int palettedImageIndex = y*(bitmap.bmWidth/2) + outX;
				assert(palettedImageIndex < palettedImageSize);
				pPalettedImage[palettedImageIndex] = outValue;
			}
		}

		//Output 4pbb image
		BitmapWriter outBitmap;
		outBitmap.CreateBitmap(pInOutputImageName, bitmap.bmWidth, bitmap.bmHeight, 4, pPalettedImage,
			colorBufferSize, mSourceImage.mBitmapData.mPaletteData.mpRGBA, mSourceImage.mBitmapData.mPaletteData.mSizeInBytes, true);

		// Save the image
		DeleteDC(hdcImage);
		
		delete[] pColorBuffer;
		delete[] pFlippedImage;
		delete[] pPalettedImage;

		return true;
	}

private:
	HFONT mhFont;
	std::map<unsigned int, unsigned int> mOrigPaletteColorToIndex;
	BitmapReader mSourceImage;

};

bool WriteTextIntoImage(const std::string& pInFontSheetName, const std::string& pPatchedImagePath, const std::string& pTextFilePath, const std::string& pInOutputPath)
{	
	TextInImage outputImageInterface;
	outputImageInterface.Initialize(pPatchedImagePath.c_str());

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
		std::string outFileName = pInOutputPath + std::to_string(i) + ".bmp";
		outputImageInterface.OutputImageWithText(textFile.mLines[i].mFullLine.c_str(), outFileName.c_str());
	};

	return true;
}


class TextInImageOld
{
public:
	~TextInImageOld()
	{
	}

	bool Initialize(const char* pInFontSheetImagePath)
	{
		if(!mFontSheet.ReadBitmap(pInFontSheetImagePath))
		{
			return false;
		}

		mTileExtractor;
		if( !mTileExtractor.ExtractTiles(16, 16, 16, 16, mFontSheet) )
		{
			return false;
		}

		for(char letter = ' '; letter <= '~'; ++letter)
		{
			const TileExtractor::Tile& tile = mTileExtractor.mTiles[letter];
			mCharacterSizes[letter].cx = tile.mWidthOfContent;
		}

		mCharacterSizes[' '].cx = 2;

		return true;
	}

	int GetTextWidth(const std::string& inString)
	{
		SIZE totalSize;
		totalSize.cx = totalSize.cy = 0;

		const int numCharacters = inString.length() - 1;
		for( int i = 0; i < numCharacters; ++i )
		{
			totalSize.cx += GetCharacterDimension(inString[i]).cx;
		}
		return totalSize.cx;
	}

	void GetCharacterData(char inLetter, std::vector<char>& outData)
	{
		const TileExtractor::Tile& tile = mTileExtractor.mTiles[inLetter];
		const int maxX = 8;//tile.mBytesInWidthOfContent;

		for (int y = 0; y < 16; ++y)
		{
			for (int x = 0; x < maxX; ++x) //used to be 8 insntead of width
			{
				const unsigned int dataIndex = x + (y * 8);
				assert(dataIndex < tile.mTileSize);

				outData.push_back(tile.mpTile[dataIndex]);
			}
		}
	}

	SIZE GetCharacterDimension(char inLetter) const
	{
		SIZE size = mCharacterSizes.find(inLetter)->second;
		return size;
	}

private:
	BitmapReader mFontSheet;
	TileExtractor mTileExtractor;
	std::map<char, SIZE> mCharacterSizes;
};

bool WriteTextIntoImageOld(const std::string& pInFontSheetName, const std::string& pPatchedImagePath, const std::string& pTextFilePath, const std::string& pInOutputPath)
{
	TextInImageOld textInImageInterface;
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
		int startX = outputImageData.mBitmapData.mInfoHeader.mImageWidth/2 - textWidth/2;
		if( startX < 0 )
		{
			startX = 0;
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
			if( !characterData.size() )
			{
				continue;
			}
			const char* pCharacterData = &characterData.front();
			const SIZE characterDim = textInImageInterface.GetCharacterDimension(textFile.mLines[i].mFullLine[letterIndex]);
			const int characterDimX = characterDim.cx;
			if( characterDimX + currentX >= createdImage.GetWidth() )
			{
				break;
			}
			createdImage.AddTile(pCharacterData, (int)characterData.size(), currentX, 0, characterDimX, outputImageData.mBitmapData.mInfoHeader.mImageHeight, BitmapSurface::kFlipNone);
			currentX += characterDimX;
		}

		std::string outFileName = pInOutputPath + std::to_string(i) + ".bmp";
		createdImage.WriteToFile(outFileName, true);
	}

	return true;
}
