#pragma once
#pragma pack(push, 1)
struct MainMenuImageInfo
{
	uint16 reserved;
	uint16 widthDiv8;
	uint16 heightDiv8;
	uint16 offsetDiv8;
};
#pragma pack(pop)

void ExtractMainMenu(const string& rootSakuraDirectory, bool bBmp, const string& outDirectory)
{
	CreateDirectoryHelper(outDirectory);

	const string sakuraFilePath = rootSakuraDirectory + string("SAKURA1\\TITLE.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());
	FileData sakuraFileData;
	if (!sakuraFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return;
	}

	const string bmpExt = bBmp ? string(".bmp") : (".png");

	const char* filesToExtract[1] = { "PBOOK_FL"};

	//GOVERTV1
	for (int fileIndex = 0; fileIndex < 1; ++fileIndex)
	{
		const string dataFilePath = rootSakuraDirectory + string("SAKURA1\\") + string(filesToExtract[fileIndex]) + string(".CG");
		FileNameContainer dataFileNameInfo(dataFilePath.c_str());

		FileData fileData;
		if (!fileData.InitializeFileData(dataFileNameInfo))
		{
			return;
		}

		const int paletteSize = 32;

		PaletteData palette;
		if (!palette.CreateFrom15BitData(sakuraFileData.GetData() + 0x00002250, paletteSize))
		{
			printf("Unable to create palette 1.\n");
			return;
		}

		const string finalOutputDirectory = outDirectory + string(filesToExtract[fileIndex]) + string("\\");
		CreateDirectoryHelper(finalOutputDirectory);

		const int numEntries = 167;
		MainMenuImageInfo lookupTable[numEntries];
		const unsigned int offsetToData = 0xE1B8;
		memcpy_s(lookupTable, sizeof(lookupTable), sakuraFileData.GetData() + offsetToData, sizeof(lookupTable));

		const int baseOffset = 0x3ef60;

		for (int i = 0; i < numEntries; ++i)
		{
			const int offset = SwapByteOrder(lookupTable[i].offsetDiv8) * 8;
			const int width  = SwapByteOrder(lookupTable[i].widthDiv8) * 8;
			const int height = SwapByteOrder(lookupTable[i].heightDiv8) * 8;

			const string outFileName = finalOutputDirectory + std::to_string(i) + bmpExt;

			BitmapWriter outBmp;
			outBmp.CreateBitmap(outFileName, width, -height, 4, fileData.GetData() + baseOffset + offset, (width * height) / 2, palette.GetData(), palette.GetSize(), bBmp);
		}
	}
}