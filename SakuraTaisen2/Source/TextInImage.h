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
			printf("Unable to LoadImage %s. Try resaving in MS Paint.\n", mSourceImage.GetFileName().c_str());
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
			palettedImageSize, mSourceImage.mBitmapData.mPaletteData.mpRGBA, mSourceImage.mBitmapData.mPaletteData.mSizeInBytes, true);

		// Save the image
		DeleteDC(hdcImage);
		
		delete[] pColorBuffer;
		delete[] pFlippedImage;
		delete[] pPalettedImage;

		return true;
	}

	unsigned int GetImageWidth() const {return mSourceImage.mBitmapData.mInfoHeader.mImageWidth;}

private:
	HFONT mhFont;
	std::map<unsigned int, unsigned int> mOrigPaletteColorToIndex;
	BitmapReader mSourceImage;

};

//Used to create translated images for MName_CG and InfoName files
bool WriteTextIntoImage(const std::string& pTextFilePath,
						const std::string& inImageSizesFile,
						const std::string& inOriginalImagesDirectory,
						const std::string& pInOutputPath)
{	
	CreateDirectoryHelper(pInOutputPath);

	FileNameContainer textFileName(pTextFilePath);
	TextFileData textFile(textFileName);
	if(!textFile.InitializeTextFile(false, false))
	{
		return false;
	}
	
	FileNameContainer imageSizesFileName(inImageSizesFile);
	TextFileData imageSizesFile(imageSizesFileName);
	if(!imageSizesFile.InitializeTextFile(false, false))
	{
		return false;
	}

	// Blank images in which to write text
	typedef std::map<unsigned int, TextInImage*> ImageWidthToTextInImage;
	ImageWidthToTextInImage imageWidthTextInImage;
	const size_t numImages = imageSizesFile.mLines.size();
	for( size_t i = 0; i < numImages; ++i )
	{
		const std::string sourceImagePath = imageSizesFileName.mPathOnly + Seperators + imageSizesFile.mLines[i].mFullLine;
		TextInImage* pOutputImageInterface = new TextInImage;
		if( !pOutputImageInterface->Initialize(sourceImagePath.c_str()) )
		{
			printf("Unable to load bitmap: %s.  Try resaving in Paint.\n", sourceImagePath.c_str());
			return false;
		}

		imageWidthTextInImage[pOutputImageInterface->GetImageWidth()] = pOutputImageInterface;
	}

	//Go through all lines of text
	const std::string bmpExt(".bmp");
	const size_t numLines = textFile.mLines.size();
	for(size_t i = 0; i < numLines; ++i)
	{
		std::string outFileName = pInOutputPath + std::to_string(i) + bmpExt;
		std::string originalImagePath = inOriginalImagesDirectory + std::to_string(i) + bmpExt;
		BitmapReader originalImage;
		if( !originalImage.ReadBitmap(originalImagePath) )
		{
			continue;
		}

		ImageWidthToTextInImage::iterator interfaceIter = imageWidthTextInImage.find(originalImage.mBitmapData.mInfoHeader.mImageWidth);
		if( interfaceIter == imageWidthTextInImage.end() )
		{
			printf("Unable to find interface for image of width: %i for image: %s", originalImage.mBitmapData.mInfoHeader.mImageWidth, originalImagePath.c_str());
			continue;
		}
		interfaceIter->second->OutputImageWithText(textFile.mLines[i].mFullLine.c_str(), outFileName.c_str());
	};

	printf("Finished\n");

	return true;
}
