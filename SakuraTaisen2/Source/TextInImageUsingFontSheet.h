#pragma once

class Tiled8BitFontSheet
{
public:
	bool CreateFontSheet(const std::string& inFontSheetPath, const std::string& inTempOutputPath, char inStartingLetter, BitmapReader* pInFontSheet = nullptr)
	{
		CreateDirectoryHelper(inTempOutputPath);

		mStartingLetter = inStartingLetter;

		BitmapReader newFontSheet;
		BitmapReader& fontSheet = pInFontSheet ? *pInFontSheet : newFontSheet;
		if( !pInFontSheet && !fontSheet.ReadBitmap(inFontSheetPath) )
		{
			return false;
		}

		//Convert font sheet to 8 bit if needed
		BitmapFormatConverter fontSheet8Bit;
		if( fontSheet.GetBitCount() == 4 )
		{
			if( !fontSheet8Bit.ConvertFrom4BitTo8Bit(fontSheet) )
			{
				return false;
			}
		
			const std::string createdFontSheetPath = inTempOutputPath + "\\TempFontSheet.bmp";
			fontSheet8Bit.WriteToFile(createdFontSheetPath.c_str());
		
			//Now read in the created bitmap
			BitmapReader createdFontReader;
			if( !createdFontReader.ReadBitmap(createdFontSheetPath) )
			{
				return false;
			}

			//Extract the tiles
			mFontSheet.ExtractTiles(16, 16, 16, 16, createdFontReader, 1);

			DeleteFile(createdFontSheetPath.c_str());
		}
		else if( fontSheet.GetBitCount() != 8 )
		{
			printf("%s needs to be either 4 bit or 8 bit. Is %i bit instead.\n", fontSheet.GetFileName().c_str(), fontSheet.GetBitCount());
			return false;
		}
		else
		{
			//We were passed in a 8 bit font sheet, so just extract that
			mFontSheet.ExtractTiles(16, 16, 16, 16, fontSheet);
		}

		const int numTiles = (int)mFontSheet.mTiles.size();
		for( int tileIndex = 0; tileIndex < numTiles; ++tileIndex )
		{
			int width = mFontSheet.mTiles[tileIndex].mBytesInWidthOfContent;
			if( width == 0 )
			{
				width = 4;
			}

			mLetterToWidth[mStartingLetter + tileIndex] = width;
		}

		return true;
	}
	
	const TileExtractor::Tile* GetTileForCharacter(char InLetter)
	{
		//î
		if((uint8)InLetter == (uint8)'î')
		{
			InLetter = 'z' + 1;
		}
		else if((uint8)InLetter == (uint8)'ü')
		{
			InLetter = 'z' + 2;
		}
		else if((uint8)InLetter == (uint8)'è')
		{
			InLetter = 'z' + 3;
		}

		const int index = (int)(InLetter) - (int)(mStartingLetter);
		if( index < 0 || index >= (int)mFontSheet.mTiles.size() )
		{
			printf("GetTileForCharacter: Invalid letter %c\n", InLetter);
			return nullptr;
		}

		return &mFontSheet.mTiles[index];
	}

	int GetWidthOfCharacter(char InLetter) const
	{
		//î
		if ((uint8)InLetter == (uint8)'î')
		{
			InLetter = 'z' + 1;
		}
		else if ((uint8)InLetter == (uint8)'ü')
		{
			InLetter = 'z' + 2;
		}
		else if ((uint8)InLetter == (uint8)'è')
		{
			InLetter = 'z' + 3;
		}

		std::map<int, int>::const_iterator iter = mLetterToWidth.find(InLetter);
		if( iter != mLetterToWidth.end() )
		{
			return iter->second;
		}

		printf("GetWidthOfCharacter: %c\n", InLetter);
		return 0;
	}

	int GetWidthOfText(const std::string& InString) const
	{
		int width = 0;

		const size_t numLetters = InString.length();
		for(size_t i = 0; i < numLetters; ++i )
		{
			width += GetWidthOfCharacter(InString[i]);
		}

		return width;
	}

	void SwapColors(const char inSearchColor, const char inReplaceColor)
	{
		mFontSheet.SwapColorsOnTiles(inSearchColor, inReplaceColor);
	}

	TileExtractor mFontSheet;

private:
	std::map<int, int> mLetterToWidth;
	char mStartingLetter{0};
};

class TextInImageUsingFontSheet
{
public:
	bool InitializeImage(const char* pBgndImage)
	{
		BitmapReader bgndImage;
		if( !bgndImage.ReadBitmap(pBgndImage) )
		{
			return false;
		}

		char* pColorData = bgndImage.GetColorData();
		int colorDataSize = bgndImage.GetColorDataSize();
		char* pPalette = bgndImage.GetPaletteData();
		int paletteDataSize = bgndImage.GetPaletteDataSize();

		//If a 4bit image was passed in, convert it to 8bit first
		BitmapFormatConverter converter;
		bool bUseConverteData = false;
		if( bgndImage.mBitmapData.mInfoHeader.mBitCount == 4 )
		{
			if( !converter.ConvertFrom4BitTo8Bit(pBgndImage) )
			{
				return false;
			}

			pColorData = converter.GetConvertedData();
			colorDataSize = converter.GetConvertedDataSize();
			pPalette = converter.GetPaletteData();
			paletteDataSize = converter.GetPaletteDataSize();
		}

		mImageCanvas.CreateSurface(bgndImage.GetWidth(), bgndImage.GetHeight(), 
								   BitmapSurface::EBitsPerPixel::kBPP_8, pPalette, paletteDataSize, true);

		mImageCanvas.AddTile(pColorData, colorDataSize, 0, 0, bgndImage.GetWidth(), abs(bgndImage.GetHeight()));
		
		return true;
	}

	bool AddTile(int inXLocation, int inYLocation, const TileExtractor::Tile& inTile, int inWidthOfCharacter)
	{
		if( inXLocation + inWidthOfCharacter > mImageCanvas.GetWidth() - mRightOffset)
		{
			return false;
		}

		mImageCanvas.AddPartialTile(inTile.mpTile, inTile.mTileSize, inXLocation, inYLocation, inWidthOfCharacter, inTile.mTileWidth, inTile.mTileHeight);
		return true;
	}

	bool OutputBitmap(const std::string& inOutputPath, bool b4Bit)
	{
		if(b4Bit)
		{
			BitmapFormatConverter converter;
			if(!converter.ConvertColorDataFrom8BitTo4Bit(mImageCanvas.GetColorData(), mImageCanvas.GetColorDataSize()))
			{
				printf("Unable to output %s\n", inOutputPath.c_str());
				return false;
			}

			BitmapWriter outBmp;
			outBmp.CreateBitmap(inOutputPath, mImageCanvas.GetWidth(), -1*mImageCanvas.GetHeight(), 4, converter.GetConvertedData(), 
			                    converter.GetConvertedDataSize(), mImageCanvas.GetPaletteData(), 16*4, true);
		}
		else
		{
			mImageCanvas.WriteToFile(inOutputPath, true);
		}

		return true;
	}

	int GetWidth() const {return mImageCanvas.GetWidth();}

	void SetRightOffset(int InOffset) {mRightOffset = InOffset;}

private:
	BitmapSurface mImageCanvas;
	int mRightOffset{0};
};

bool WriteTextIntoImageUsingFontSheet(	const std::string& InText, const FileNameContainer& InImagePath, Tiled8BitFontSheet& InFontSheet, 
										bool bInFailIfImageDoesntFit, int inLeftOffset, int inRightOffset, int yOffset, bool bInCenter = true)
{
	static string bmpExt(".bmp");
	if (InImagePath.mExtention != bmpExt)
	{
		printf("Image must be a bitmap. %s\n", InImagePath.mFullPath.c_str());
		return false;
	}

	//Create canvas
	TextInImageUsingFontSheet translatedImage;
	if (!translatedImage.InitializeImage(InImagePath.mFullPath.c_str()))
	{
		return false;
	}

	translatedImage.SetRightOffset(inRightOffset);

	//Draw text into image
	const int textWidth = InFontSheet.GetWidthOfText(InText) - (inRightOffset);// + inLeftOffset);
	int xLocation = bInCenter ? inLeftOffset + (translatedImage.GetWidth() / 2) - (textWidth / 2) : inLeftOffset;
	if (xLocation < 0)
	{
		xLocation = 0;
	}

	//Print out letters
	const size_t numLetters = InText.size();
	for (size_t letterIndex = 0; letterIndex < numLetters; ++letterIndex)
	{
		const TileExtractor::Tile* pTile = InFontSheet.GetTileForCharacter(InText[letterIndex]);
		if (!pTile)
		{
			continue;
		}

		const int widthOfCharacter = InFontSheet.GetWidthOfCharacter(InText[letterIndex]);

		if (!translatedImage.AddTile(xLocation, yOffset, *pTile, widthOfCharacter))
		{
			if(bInFailIfImageDoesntFit)
			{
				return false;
			}

			printf("Unable to fit %s. %i/%i : %s\n", InImagePath.mFileName.c_str(), textWidth, translatedImage.GetWidth(), InText.c_str());
		}
		xLocation += widthOfCharacter;
	}

	translatedImage.OutputBitmap(InImagePath.mFullPath, true);

	return true;
}

bool WriteTextIntoImagesUsingFontSheet(
	const std::string& pInTextFilePath,
	const std::string& inFontSheetPath,
	const std::string& inBgndImageDirectory,
	const std::string& inOriginalImagesDirectory,
	const std::string& pInOutputPath)
{
	CreateDirectoryHelper(pInOutputPath);

	Tiled8BitFontSheet fontSheet;
	if( !fontSheet.CreateFontSheet(inFontSheetPath, pInOutputPath, ' ') )
	{
		return false;
	}

	//Text data
	FileNameContainer textFileName(pInTextFilePath);
	TextFileData textFile(textFileName);
	if (!textFile.InitializeTextFile(false, false))
	{
		return false;
	}

	//Create a map of canvas map files to their widths
	typedef std::map<int, FileNameContainer> CanvasSizeToPathName;
	CanvasSizeToPathName canvasSizeToPathName;
	std::vector<FileNameContainer> allCanvasImages;
	FindAllFilesWithinDirectory(inBgndImageDirectory, allCanvasImages);

	//Find all canvas images and make a map of them
	const std::string bmpExt(".bmp");
	for( const FileNameContainer& canvasFile : allCanvasImages )
	{
		if( canvasFile.mExtention != bmpExt )
		{
			continue;
		}

		BitmapReader canvasImage;
		if(canvasImage.ReadBitmap(canvasFile.mFullPath.c_str()))
		{
			canvasSizeToPathName[canvasImage.GetWidth()] = canvasFile;
		}
	}

	//Create translated version of all original images
	std::vector<FileNameContainer> allOriginalImages;
	FindAllFilesWithinDirectory(inOriginalImagesDirectory, allOriginalImages);

	if( textFile.mLines.size() != allOriginalImages.size() )
	{
		printf("Image and translation mismatch.  Images:%i TranslationLine:%i\n", (int)allOriginalImages.size(), (int)textFile.mLines.size());
		return false;
	}

	const size_t numImages = allOriginalImages.size();
	for( size_t imageIndex = 0; imageIndex < numImages; ++imageIndex )
	{
		const TextFileData::TextLine& textLine = textFile.mLines[imageIndex];
		const FileNameContainer& originalFileName = allOriginalImages[imageIndex];
		if( originalFileName.mExtention != bmpExt )
		{
			continue;
		}

		// Read in orginal image so we can determine which canvas size to use
		BitmapReader originalImage;
		if( !originalImage.ReadBitmap(originalFileName.mFullPath) )
		{
			continue;
		}

		CanvasSizeToPathName::iterator canvasToUse = canvasSizeToPathName.find(originalImage.GetWidth());
		if( canvasToUse == canvasSizeToPathName.end() )
		{
			printf("No canvas found for: %s. Has width of %i\n", originalFileName.mFileName.c_str(), originalImage.GetWidth());
			continue;
		}

		//Create canvas
		TextInImageUsingFontSheet translatedImage;
		if( !translatedImage.InitializeImage(canvasToUse->second.mFullPath.c_str()) )
		{
			continue;
		}

		//Draw text into image
		const int textWidth = fontSheet.GetWidthOfText(textLine.mFullLine);
		int xLocation = (originalImage.GetWidth()/2) - (textWidth/2);
		if( xLocation < 0 )
		{
			xLocation = 0;
		}

		//Print out letters
		const size_t numLetters = textLine.mFullLine.size();
		for( size_t letterIndex = 0; letterIndex < numLetters; ++letterIndex )
		{
			const TileExtractor::Tile* pTile = fontSheet.GetTileForCharacter(textLine.mFullLine[letterIndex]);
			if( !pTile )
			{
				continue;
			}

			const int widthOfCharacter = fontSheet.GetWidthOfCharacter(textLine.mFullLine[letterIndex]);

			if( !translatedImage.AddTile(xLocation, 0, *pTile, widthOfCharacter) )
			{
				printf("Unable to fit %s. %i/%i : %s\n", originalFileName.mFileName.c_str(), textWidth, originalImage.GetWidth(), textLine.mFullLine.c_str());
				break;
			}
			xLocation += widthOfCharacter;
		}

		translatedImage.OutputBitmap(pInOutputPath + originalFileName.mFileName, true);
	}

	return true;
}
