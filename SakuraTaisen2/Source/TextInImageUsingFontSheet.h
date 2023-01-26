#pragma once

class Tiled8BitFontSheet
{
public:
	bool CreateFontSheet(const std::string& inFontSheetPath, const std::string& inTempOutputPath)
	{
		CreateDirectoryHelper(inTempOutputPath);

		BitmapReader fontSheet;
		if( !fontSheet.ReadBitmap(inFontSheetPath) )
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
			mFontSheet.ExtractTiles(16, 16, 16, 16, createdFontReader);
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

		return true;
	}

	TileExtractor mFontSheet;
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
								   BitmapSurface::EBitsPerPixel::kBPP_8, pPalette, paletteDataSize);

		mImageCanvas.AddTile(pColorData, colorDataSize, 0, 0, bgndImage.GetWidth(), bgndImage.GetHeight());
	}

	void AddTile()
	{

	}

private:
	BitmapSurface mImageCanvas;
};

bool WriteTextIntoImageUsingFontSheet(
	const std::string& pInTextFilePath,
	const std::string& inFontSheetPath,
	const std::string& inBgndImageDirectory,
	const std::string& inOriginalImagesDirectory,
	const std::string& pInOutputPath)
{
	CreateDirectoryHelper(pInOutputPath);

	Tiled8BitFontSheet fontSheet;
	if( !fontSheet.CreateFontSheet(inFontSheetPath, pInOutputPath) )
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
		if( canvasImage.GetBitCount() != 8 )
		{
			printf("Warning: Canvas image needs to be 8bits.  This canvas can't be used: %s\n", canvasFile.mFileName.c_str());
			continue;
		}

		canvasSizeToPathName[canvasImage.GetWidth()] = canvasFile;
	}

	std::vector<FileNameContainer> allOriginalImages;
	FindAllFilesWithinDirectory(inOriginalImagesDirectory, allOriginalImages);
	for( const FileNameContainer& originalFileName : allOriginalImages )
	{
		if( originalFileName.mExtention != bmpExt )
		{
			continue;
		}

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

		//Todo, read in translation and add it into the translatedImage
		TextInImageUsingFontSheet translatedImage;
		translatedImage.InitializeImage(canvasToUse->second.mFullPath.c_str());
	}
}
