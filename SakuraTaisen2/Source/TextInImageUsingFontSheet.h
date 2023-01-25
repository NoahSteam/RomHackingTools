#pragma once

class TextInImageUsingFontSheet
{
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
	}
	else if( fontSheet.GetBitCount() != 8 )
	{
		printf("%s needs to be either 4 bit or 8 bit. Is %i bit instead.\n", fontSheet.GetFileName().c_str(), fontSheet.GetBitCount());
	}

	TileExtractor fontSheetExtractor;
	fontSheetExtractor.ExtractTiles()
}
