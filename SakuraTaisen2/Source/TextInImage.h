#pragma once

#define TextFontName "Rockwell Condensed"
#define TextFontSize 13
#define TextYOffset 0

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
	
		unsigned int closestMatch = InColorValue;
		unsigned int closestMatchDiff = 0xffffffff;
		unsigned int closestIndex = 0;
		for( std::map<unsigned int, unsigned int>::const_iterator iter = mOrigPaletteColorToIndex.begin(); iter != mOrigPaletteColorToIndex.end(); ++iter )
		{
			//0 is assumed to be the alpha color, so ignore it
			if(iter->second == 0 || iter->second == 15)
			{
				continue;
			}

			unsigned int colorValue = iter->first;
			const unsigned int bigValue = colorValue > InColorValue ? colorValue : InColorValue;
			const unsigned int smallValue = colorValue > InColorValue ? InColorValue : colorValue;
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

		mhFont = CreateFont(TextFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, TextFontName);

		return true;
	}

	void AntiAliasImage(char* pInImageData, const unsigned int inWidth, const unsigned int inHeight)
	{
		const unsigned int channels = 4;

		char* pOriginal = new char[channels * inWidth * inHeight];
		memcpy_s(pOriginal, channels * inWidth * inHeight, pInImageData, channels * inWidth * inHeight);

		for (unsigned int y = 1; y < inHeight - 1; y++)
		{
			for (unsigned int x = 1; x < inWidth - 1; x++)
			{
				// Get the color of the current pixel
				const unsigned int r = (uint8)pOriginal[y * inWidth * channels + x * channels + 0];
				const unsigned int g = (uint8)pOriginal[y * inWidth * channels + x * channels + 1];
				const unsigned int b = (uint8)pOriginal[y * inWidth * channels + x * channels + 2];
				const unsigned int a = (uint8)pOriginal[y * inWidth * channels + x * channels + 3];

				const bool bAntiAlias = (r + g + b + a != 0) && x > 3 && y > 3 && x < inWidth - 3 && y < inHeight - 3;
				bool bHasBeenOutput = false;

				// Average the color values of the current pixel and its neighbours
				if(bAntiAlias)
				{
					bool bHasTextNeighbor = false;

					//Are any of the neighoring pixels black?
					unsigned int r_total = 0, g_total = 0, b_total = 0, a_total = 0;
					for (int i = -1; i <= 1; i++)
					{
						for (int j = -1; j <= 1; j++)
						{
							const bool bIsEdge = !(x + j > 3 && y + i > 3 && x + j < inWidth - 3 && y + i < inHeight - 3);
							if(bIsEdge)
							{
								continue;
							}

							const uint8 rNeighbor = (uint8)pOriginal[(y + i) * inWidth * channels + (x + j) * channels + 0];
							const uint8 gNeighbor = (uint8)pOriginal[(y + i) * inWidth * channels + (x + j) * channels + 1];
							const uint8 bNeighbor = (uint8)pOriginal[(y + i) * inWidth * channels + (x + j) * channels + 2];
							const uint8 aNeighbor = (uint8)pOriginal[(y + i) * inWidth * channels + (x + j) * channels + 3];

							bHasTextNeighbor = (rNeighbor + gNeighbor + bNeighbor == 0);
							if(bHasTextNeighbor)
							{
								pInImageData[y * inWidth * channels + x * channels + 0] = (uint8)(((float)r + rNeighbor) / 2.f);
								pInImageData[y * inWidth * channels + x * channels + 1] = (uint8)(((float)g + gNeighbor) / 2.f);
								pInImageData[y * inWidth * channels + x * channels + 2] = (uint8)(((float)b + bNeighbor) / 2.f);
								pInImageData[y * inWidth * channels + x * channels + 3] = aNeighbor;//(uint8)(a_total / 12);
								bHasBeenOutput =  true;
								break;
							}
						}
					}

					if(bHasTextNeighbor && 0)
					{
						// Average the color values of the current pixel and its neighbours
						unsigned int r_total = 0, g_total = 0, b_total = 0, a_total = 0;
						for (int i = -1; i <= 1; i++)
						{
							for (int j = -1; j <= 1; j++)
							{
								r_total += (uint8)pOriginal[(y + i) * inWidth * channels + (x + j) * channels + 0];
								g_total += (uint8)pOriginal[(y + i) * inWidth * channels + (x + j) * channels + 1];
								b_total += (uint8)pOriginal[(y + i) * inWidth * channels + (x + j) * channels + 2];
								a_total += (uint8)pOriginal[(y + i) * inWidth * channels + (x + j) * channels + 3];
							}
						}

						// Set the new color values of the current pixel
						pInImageData[y * inWidth * channels + x * channels + 0] = (uint8)255;//(uint8)(r_total / 12);
						pInImageData[y * inWidth * channels + x * channels + 1] = (uint8)255;//(uint8)(g_total / 12);
						pInImageData[y * inWidth * channels + x * channels + 2] = (uint8)255;//(uint8)(b_total / 12);
						pInImageData[y * inWidth * channels + x * channels + 3] = 0;//(uint8)(a_total / 12);

						bHasBeenOutput = true;
					}
				}

				if (!bHasBeenOutput)
				{
					pInImageData[y * inWidth * channels + x * channels + 0] = r;
					pInImageData[y * inWidth * channels + x * channels + 1] = g;
					pInImageData[y * inWidth * channels + x * channels + 2] = b;
					pInImageData[y * inWidth * channels + x * channels + 3] = a;
				}
			}
		}

		delete[] pOriginal;
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
		const int yPosition = TextYOffset;//bitmap.bmHeight/2 - (textSize.cy/2)
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

		//AntiAlias - doesn't work
		//AntiAliasImage(pFlippedImage, bitmap.bmWidth, bitmap.bmHeight);

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
bool WriteTextIntoImage2(const std::string& pTextFilePath,
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

//Used to create translated images for MName_CG and InfoName files
bool WriteTextIntoImage(const std::string& pTextFilePath,
	const std::string& inImageSizesFile,
	const std::string& inOriginalImagesDirectory,
	const std::string& pInOutputPath)
{
	CreateDirectoryHelper(pInOutputPath);

	FileNameContainer textFileName(pTextFilePath);
	TextFileData textFile(textFileName);
	if (!textFile.InitializeTextFile(false, false))
	{
		return false;
	}

	FileNameContainer imageSizesFileName(inImageSizesFile);
	TextFileData imageSizesFile(imageSizesFileName);
	if (!imageSizesFile.InitializeTextFile(false, false))
	{
		return false;
	}

	// Blank images in which to write text
	typedef std::map<unsigned int, TextInImage*> ImageWidthToTextInImage;
	ImageWidthToTextInImage imageWidthTextInImage;
	const size_t numImages = imageSizesFile.mLines.size();
	for (size_t i = 0; i < numImages; ++i)
	{
		const std::string sourceImagePath = imageSizesFileName.mPathOnly + Seperators + imageSizesFile.mLines[i].mFullLine;
		TextInImage* pOutputImageInterface = new TextInImage;
		if (!pOutputImageInterface->Initialize(sourceImagePath.c_str()))
		{
			printf("Unable to load bitmap: %s.  Try resaving in Paint.\n", sourceImagePath.c_str());
			return false;
		}

		imageWidthTextInImage[pOutputImageInterface->GetImageWidth()] = pOutputImageInterface;
	}

	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inOriginalImagesDirectory, allFiles);

	// Sort the file names alphabetically
	std::sort(allFiles.begin(), allFiles.end(), [](const FileNameContainer& a, const FileNameContainer& b)
	{
		return std::atoi(a.mNoExtension.c_str()) <  std::atoi(b.mNoExtension.c_str());
	});

	vector<FileNameContainer> bmpFiles;
	GetAllFilesOfType(allFiles, ".bmp", bmpFiles);

	if(bmpFiles.size() != allFiles.size())
	{
		printf("File and translation mismatch\n");
		return false;
	}

	//Go through all lines of text
	const size_t numLines = textFile.mLines.size();
	for (size_t i = 0; i < numLines; ++i)
	{
		std::string outFileName = pInOutputPath + bmpFiles[i].mFileName;
		BitmapReader originalImage;
		if (!originalImage.ReadBitmap(bmpFiles[i].mFullPath))
		{
			continue;
		}

		ImageWidthToTextInImage::iterator interfaceIter = imageWidthTextInImage.find(originalImage.mBitmapData.mInfoHeader.mImageWidth);
		if (interfaceIter == imageWidthTextInImage.end())
		{
			printf("Unable to find interface for image of width: %i for image: %s", originalImage.mBitmapData.mInfoHeader.mImageWidth, bmpFiles[i].mFileName.c_str());
			continue;
		}
		interfaceIter->second->OutputImageWithText(textFile.mLines[i].mFullLine.c_str(), outFileName.c_str());
	};

	printf("Finished\n");

	return true;
}
