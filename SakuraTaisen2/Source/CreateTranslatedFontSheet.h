#pragma once

bool CreateTranslatedFontSheet(	const string& inTranslatedFontSheet, TileExtractor& outSheet, PaletteData& outPalette, unsigned int inTileDimX, 
								unsigned int inTileDimY)
{
	//Read in translated font sheet
	BitmapReader origTranslatedBmp;
	if (!origTranslatedBmp.ReadBitmap(inTranslatedFontSheet))
	{
		return false;
	}

	if (!outSheet.ExtractTiles(inTileDimX, inTileDimY, inTileDimX, inTileDimY, origTranslatedBmp))
	{
		return false;
	}

	//Convert it to the SakuraTaisen format
	outPalette.CreateFrom32BitData(origTranslatedBmp.mBitmapData.mPaletteData.mpRGBA, origTranslatedBmp.mBitmapData.mPaletteData.mSizeInBytes, false);
	if (outPalette.GetNumColors() != 16)
	{
		printf("CreateTranslatedFontSheet %s has %i colors.  Needs to be a 4bit 16 color image.\n", inTranslatedFontSheet.c_str(), outPalette.GetNumColors());
		return false;
	}

	//Fix up palette
	//First index needs to have the transparent color, in our case that's white
	int indexOfAlphaColor = -1;
	const unsigned short alphaColor = 0x7fff; //In little endian order
	for (int i = 0; i < 16; ++i)
	{
		assert(i * 2 < outPalette.GetSize());

		const unsigned short color = *((short*)(outPalette.GetData() + i * 2));
		if (color == alphaColor)
		{
			const unsigned short oldColor0 = *((unsigned short*)outPalette.GetData());
			outPalette.SetValue(0, alphaColor);
			outPalette.SetValue(i, oldColor0);
			indexOfAlphaColor = i;
			break;
		}
	}

	if (indexOfAlphaColor == -1)
	{
		printf("Alpha Color not found for %s.  Palette will not be correct. \n", inTranslatedFontSheet.c_str());
		indexOfAlphaColor = 0;
	}

	outSheet.FixupIndexOfAlphaColor((unsigned short)indexOfAlphaColor, true);

	return true;
}