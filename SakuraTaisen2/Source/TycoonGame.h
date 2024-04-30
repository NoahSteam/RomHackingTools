#pragma once

void ExtractTycoonRulesScreen(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	CreateDirectoryHelper(inOutputDirectory);

	//N_Load
	const std::string cardFilePath = inRootDirectory + "SAKURA3\\CARD_DAT.ALL";
	FileData cardFile;
	if (!cardFile.InitializeFileData(cardFilePath))
	{
		return;
	}

	const uint32 tileOffset = 0x166800;
	const uint32 colorOffset = 0x155040;
	const uint32 palettteOffset = 0x168000;
	SakuraFontSheet tileSheet;
	if (!tileSheet.CreateFontSheetFromData(cardFile.GetData() + colorOffset, 40*28*64, 8, 8, false))
	{
		return;
	}

	//Create 32bit palette data
	PaletteData paletteData;
	paletteData.CreateFrom15BitData(cardFile.GetData() + palettteOffset, 512);

	BitmapSurface sakuraStringBmp;
	sakuraStringBmp.CreateSurface(320, 224, BitmapSurface::EBitsPerPixel::kBPP_8, paletteData.GetData(), paletteData.GetSize());

	unsigned int tiles[1120];
	memcpy_s(tiles, sizeof(tiles), cardFile.GetData() + tileOffset, sizeof(tiles));
	int x = 0;
	int y = 0;

	//Convert tile index into game format
	for (int t = 0; t < 1120; ++t)
	{
		const int tileValue = (SwapByteOrder(tiles[t]) / 2) - 1;
		tiles[t] = tileValue;
	}

	//Output tiles
	for (int t = 0; t < 1120; ++t)
	{
		SakuraString::SakuraChar sakuraChar;
		sakuraChar.mIndex = tiles[t];

		const char* pTileData = tileSheet.GetCharacterTile(sakuraChar);
		sakuraStringBmp.AddTile(pTileData, 64, x, y, 8, 8, BitmapSurface::kFlipNone);

		x += 8;
		if (x == 320)
		{
			x = 0;
			y += 8;
		}
	}

	const std::string imageExt = bInBmp ? ".bmp" : ".png";
	const std::string outFile = inOutputDirectory + "TiledImage" + imageExt;
	sakuraStringBmp.WriteToFile(outFile, true);	
}