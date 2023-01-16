#pragma once

class TextInImage
{
	HFONT mhFont;

public:
	~TextInImage()
	{
		// Release the font
		DeleteObject(mhFont);
	}

	bool Initialize(const char* pFontSheetName)
	{
		HBITMAP hBitmap = (HBITMAP)LoadImage(NULL, pFontSheetName, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
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
		mhFont = CreateFont(13, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Calibri");

		SelectObject(hdcImage, mhFont);

		// Get Bitmap Info
		BITMAP bitmap;
		GetObject(hBitmap, sizeof(BITMAP), &bitmap);

		SIZE textSize;
		GetTextExtentPoint32(hdcImage, "Hello", strlen("Hello"), &textSize);

		// Draw the text on the image
		TextOut(hdcImage, bitmap.bmWidth/2 - (textSize.cx/2), bitmap.bmHeight/2 - (textSize.cy/2), "Hello", 5);

		const int numColors = 256;
		RGBQUAD* palette = new RGBQUAD[numColors];
		GetDIBColorTable(hdcImage, 0, numColors, palette);

		const int colorBufferSize = bitmap.bmHeight * bitmap.bmWidthBytes;
		char* pColorBuffer = new char[colorBufferSize];
		char* pColorBuffer2 = new char[colorBufferSize];
		GetBitmapBits(hBitmap, colorBufferSize, pColorBuffer);

		int origY = 0;
		for(int y = bitmap.bmHeight - 1; y >= 0; --y, origY++)
		{
			for(int x = 0; x < bitmap.bmWidthBytes; ++x)
			{
				const int offset = y*bitmap.bmWidthBytes + x;
				
				pColorBuffer2[offset] = pColorBuffer[origY*bitmap.bmWidthBytes + x];
			}
		}

		BitmapWriter outBitmap;
		outBitmap.CreateBitmap("a:\\SakuraWars2\\Output\\Test.bmp", bitmap.bmWidth, bitmap.bmHeight, bitmap.bmBitsPixel, pColorBuffer2,
			colorBufferSize, (char*)palette, sizeof(RGBQUAD) * numColors, true);

		// Save the image
		DeleteDC(hdcImage);
		
		delete[] pColorBuffer;
		delete[] pColorBuffer2;
		delete[] palette;

		return true;
	}
};

bool WriteTextIntoImage(const std::string& pInFontSheetName, const std::string& pPatchedImagePath, const std::string& pTextFilePath, const std::string& pInOutputPath)
{	
	TextInImage outputImage;
	outputImage.Initialize(pPatchedImagePath.c_str());

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
