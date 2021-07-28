#pragma once
//80 images
//64 * 15 4bpp
//48 * 15 1f8
//128 * 15 @0x360
//40x46 8bpp @1840 (character portrait)
//40x32 8bpp @1f80(character eyes)
//40x46 8bpp @2740 (portrait 2
//40x32 8bpp @2e80 (eyes 2)

struct InfoNameImageInfo
{
	int width;
	int height;
	int offset;
	int paletteOffset;
};

void ExtractInfoName(const string& rootSakuraDirectory, const string& paletteFileName, bool bBmp, const string& outDirectory)
{
	CreateDirectoryHelper(outDirectory);

	const string sakuraFilePath = rootSakuraDirectory + string("SAKURA2\\INFONAME.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());
	FileData sakuraFileData;
	if (!sakuraFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return;
	}

	FileNameContainer paletteFileNameInfo(paletteFileName.c_str());
	FileData paletteFileData;
	if (!paletteFileData.InitializeFileData(paletteFileNameInfo))
	{
		return;
	}

	const string bmpExt = bBmp ? string(".bmp") : (".png");

	const int NumImagesInSet = 7;
	InfoNameImageInfo ImageSet[7] =
	{
		{64, 16, 0, 0},
		{48, 15, 0x1E0, 0},
		{128, 15, 0x360, 0},
		{40, 46, 0x1840, 0x1700},// (character portrait)
		{40, 32, 0x1f80, 0x1700},//(character eyes)
		{40, 46, 0x2740, 0x2600},// (portrait 2
		{40, 32, 0x2e80, 0x2600}// (eyes 2)
	};

	{
		PaletteData palette;
		if (!palette.CreateFrom15BitData(paletteFileData.GetData(), 32))
		{
			printf("Unable to create palette 1.\n");
			return;
		}

		const int numEntries = 128;
		int baseOffset = 0;
		for (int i = 0; i < numEntries; ++i)
		{
			const string setName = string("Char") + std::to_string(i) + string("_");

			for(int setIndex = 0; setIndex < NumImagesInSet; ++setIndex)
			{
				const int offset         = ImageSet[setIndex].offset + baseOffset;
				const int width          = ImageSet[setIndex].width;
				const int height         = ImageSet[setIndex].height;
				const int paletteOffset  = ImageSet[setIndex].paletteOffset + baseOffset;
				const int paletteSize    = ImageSet[setIndex].paletteOffset == 0 ? palette.GetSize() : 512;
				const int divisor        = paletteSize == 64 ? 2 : 1;
				const int bpp            = paletteSize == 64 ? 4 : 8;

				PaletteData palette2;
				if (paletteSize == 512 && !palette2.CreateFrom15BitData(sakuraFileData.GetData() + paletteOffset, 512))
				{
					printf("Unable to create palette 1.\n");
					return;
				}
				
				PaletteData& paletteToUse = paletteSize == 512 ? palette2 : palette;

				const string outFileName = outDirectory + setName + std::to_string(setIndex) + bmpExt;

				BitmapWriter outBmp;
				outBmp.CreateBitmap(outFileName, width, -height, bpp, sakuraFileData.GetData() + offset, (width * height) / divisor, paletteToUse.GetData(), paletteToUse.GetSize() , bBmp);
			}

			baseOffset += 0x4000;
		}
	}
}