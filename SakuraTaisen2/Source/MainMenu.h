#pragma once

#define GenericImageEntry(imagePath) imageList.emplace_back(lookupTable[lutIndex].widthDiv8, lookupTable[lutIndex].heightDiv8, lookupTable[lutIndex].offsetDiv8, imagePath);\
	                              ++lutIndex

#pragma pack(push, 1)
struct MainMenuImageInfo
{
	uint16 reserved;
	uint16 widthDiv8;
	uint16 heightDiv8;
	uint16 offsetDiv8;
};

struct CustomMenuImageInfo
{
	CustomMenuImageInfo(){}
	CustomMenuImageInfo(uint16 InWidth, uint16 InHeight, uint16 InOffset, const string& InImageName) : width(InWidth), height(InHeight), offset(InOffset), imageName(InImageName){}

	uint16 width{ 0 };
	uint16 height{ 0 };
	uint16 offset{ 0 };
	string imageName;
};
#pragma pack(pop)

//Found manually
const uint16 CharBioImageOffset = (0x320a0 / 8);
const int NumCustomMainMenuImages = 17;
MainMenuImageInfo ManuallyFoundMainMenuImages[NumCustomMainMenuImages]
{
	0, 128, 16, (uint16)(0x3c460 / 8),
	0, 32, 16,  (uint16)(0x3c460 / 8),
	0, 16, 16,  (uint16)(0x3c460 / 8),
	0, 48, 16,  (uint16)(0x3c460 / 8),

	0, 64, 16, (uint16)(0x3d660 / 8),
	0, 72, 16, (uint16)(0x3d660 / 8),
	0, 48, 16, (uint16)(0x3d660 / 8),

	//Return image
	0, 16, 48, (uint16)(0x54620 / 8),

	//Character bios
	0, 72, 120, CharBioImageOffset,
	0, 72, 120, CharBioImageOffset,
	0, 72, 120, CharBioImageOffset,
	0, 72, 120, CharBioImageOffset,
	0, 72, 120, CharBioImageOffset,
	0, 72, 120, CharBioImageOffset,
	0, 72, 120, CharBioImageOffset,
	0, 72, 120, CharBioImageOffset,

	//Missing game setting
	0, 16, 112, (uint16)(0x542a0 / 8)
};

const int NumPBEyeImages = 12;
MainMenuImageInfo PBEyeImages[NumPBEyeImages]
{
	0, 128, 16, (uint16)(0xc940),
	0, 112, 16, (uint16)(0xc940),
	0, 112, 16, (uint16)(0xc940),
	0, 112, 16, (uint16)(0xc940),
	0, 128, 16, (uint16)(0xc940),
	0, 32,  16, (uint16)(0xc940),
	0, 16,  16, (uint16)(0xc940),
	0, 48,  16, (uint16)(0xc940),
	0, 88,  64, (uint16)(0xc940),
	0, 64,  16, (uint16)(0xc940),
	0, 72,  16, (uint16)(0xc940),
	0, 48,  16, (uint16)(0xc940),
};

const int NumPBookRCImages = 15;
MainMenuImageInfo PBookRCImages[NumPBookRCImages]
{
	0, 112, 112,0,
	0, 32, 112, 0,
	0, 32, 112, 0,
	0, 32, 112, 0,
	0, 32, 112, 0,
	0, 32, 112, 0,
	0, 32, 112, 0,
	0, 32, 112, 0,
	0, 32, 112, 0,
	0, 32, 112, 0,
	0, 32, 112, 0,
	0, 16, 112, 0,
	0, 40, 40,  0,
	0, 40, 40,  0,
	0, 40, 40,  0,
};

const int NumTetyoADImages = 21;
MainMenuImageInfo TetyoADImages[NumTetyoADImages]
{
	0, 88,  24, (uint16)0x9080,
	0, 96,  16, (uint16)0x9080,
	0, 16,  16, (uint16)0x9080,
	0, 16,  16, (uint16)0x9080,
	0, 16,  16, (uint16)0x9080,
	0, 16,  16, (uint16)0x9080,
	0, 64,  16, (uint16)0x9080,
	0, 56,  16, (uint16)0x9080,
	0, 8,   16, (uint16)0x9080,
	0, 56,  16, (uint16)0x9080,
	0, 32,  16, (uint16)0x9080,
	0, 32,  16, (uint16)0x9080,
	0, 32,  16, (uint16)0x9080,
	0, 112, 24, (uint16)0x9080,
	0, 80,  16, (uint16)0x9080,
	0, 104, 16, (uint16)0x9080,
	0, 32,  16, (uint16)0x9080,
	0, 32,  16, (uint16)0x9080,
	0, 112, 16, (uint16)0x9080,
	0, 112, 16, (uint16)0x9080,
	0, 16,  32, (uint16)0x9080,
};

const int NumCmdWinImages = 3;
MainMenuImageInfo CmdWinImages[NumCmdWinImages]
{
	0, 64,  16, (uint16)(0xb00),
	0, 72,  16, (uint16)(0xb00),
	0, 48,  16, (uint16)(0xb00),
};

std::unordered_set<int> PBookFLFilesToSkip;

bool PatchCustomImages( vector<CustomMenuImageInfo> InImageInfo,
						const string& InPatchedFilePath)
{
	FileReadWriter pbookFileData;
	if (!pbookFileData.OpenFile(InPatchedFilePath))
	{
		return false;
	}

	const string bmpExt(".bmp");
	int accumulatedOffset = 0;
	int prevOffset = 0;
	const int numImages = (int)InImageInfo.size();
	for (int i = 0; i < numImages; ++i)
	{
		const int offset = InImageInfo[i].offset;
		const int width = InImageInfo[i].width;
		const int height = InImageInfo[i].height;

		//For when the addresses jump
		if (offset != prevOffset)
		{
			accumulatedOffset = 0;
		}

		const string translatedImagePath = InImageInfo[i].imageName + bmpExt;
		
		//dummy entries
		if (InImageInfo[i].imageName.size() == 0)
		{
			accumulatedOffset += width * height / 2;
			prevOffset = offset;
			continue;
		}

		BmpToSaturnConverter convertedImage;
		if (!convertedImage.ConvertBmpToSakuraFormat(translatedImagePath, false))
		{
			return false;
		}

		if (convertedImage.GetImageHeight() != height)
		{
			printf("Expected height of %i, got %i for %s\n", height, convertedImage.GetImageHeight(), translatedImagePath.c_str());
			return false;
		}

		if (convertedImage.GetImageWidth() != width)
		{
			printf("Expected width of %i, got %i for %s\n", width, convertedImage.GetImageWidth(), translatedImagePath.c_str());
			return false;
		}

		if (convertedImage.GetImageDataSize() != ((width * height) >> 1))
		{
			printf("Expected data size of %i, got %i for %s\n", (width * height) >> 1, convertedImage.GetImageDataSize(), translatedImagePath.c_str());
			return false;
		}

		pbookFileData.WriteData(offset + accumulatedOffset, convertedImage.GetImageData(), convertedImage.GetImageDataSize(), false);

		accumulatedOffset += width * height / 2;
		prevOffset = offset;
	}

	return true;
}

bool Patch_Hanko(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	vector<CustomMenuImageInfo> hankoImages;
	
	const string translatedDirectory = inTranslatedDataDirectory + string("PBOOK_FL\\");

	//Character bios
	hankoImages.emplace_back(72, 120, 0, translatedDirectory + "175");
	hankoImages.emplace_back(72, 120, 0, translatedDirectory + "176");
	hankoImages.emplace_back(72, 120, 0, translatedDirectory + "177");
	hankoImages.emplace_back(72, 120, 0, translatedDirectory + "178");
	hankoImages.emplace_back(72, 120, 0, translatedDirectory + "179");
	hankoImages.emplace_back(72, 120, 0, translatedDirectory + "180");
	hankoImages.emplace_back(72, 120, 0, translatedDirectory + "181");
	hankoImages.emplace_back(72, 120, 0, translatedDirectory + "182");

	return PatchCustomImages(hankoImages, inPatchedSakuraDirectory + string("SAKURA1\\HANKO.CG") );
}

bool Patch_PBook_EC(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	const string sakuraFilePath = inPatchedSakuraDirectory + string("SAKURA.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());
	FileData sakuraFileData;
	if (!sakuraFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return false;
	}

	const string pbookFLDirectory = inTranslatedDataDirectory + string("PBOOK_FL\\");
	const string pbookECDirectory = inTranslatedDataDirectory + string("PBOOK_EC\\");

	const int numEntries = 111;
	MainMenuImageInfo lookupTable[numEntries];
	const unsigned int offsetToData = 0x82fb4;
	memcpy_s(lookupTable, sizeof(lookupTable), sakuraFileData.GetData() + offsetToData, sizeof(lookupTable));

	vector<CustomMenuImageInfo> images;
	for (int i = 0; i < numEntries; ++i)
	{
		if (i >= 92 && i <= 102)
		{
			continue;
		}

		const int width = SwapByteOrder(lookupTable[i].widthDiv8) * 8;
		const int height = SwapByteOrder(lookupTable[i].heightDiv8) * 8;
		
		string translatedImagePath;
		if(i == 62)
		{
			translatedImagePath = pbookECDirectory + std::to_string(62);
		}
		else if(i >= 103)
		{
			translatedImagePath = pbookFLDirectory + std::to_string(i + 72);
		}
		else if(i < 63 && PBookFLFilesToSkip.find(i + 76) == PBookFLFilesToSkip.end())
		{
			translatedImagePath = pbookFLDirectory + std::to_string(i + 76);
		}
		else if (i >= 63 && PBookFLFilesToSkip.find(i + 75) == PBookFLFilesToSkip.end())
		{
			translatedImagePath = pbookFLDirectory + std::to_string(i + 75);
		}

		images.emplace_back(width, height, 0, translatedImagePath);
	}

	return PatchCustomImages(images, inPatchedSakuraDirectory + string("SAKURA1\\PBOOK_EC.CG"));
}

//Battle pause menu
bool Patch_PBook_BT(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	const string sakuraFilePath = inPatchedSakuraDirectory + string("SAKURA2\\SLGNTBK.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());
	FileData tableFileData;
	if (!tableFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return false;
	}

	const int numEntries = 62;
	MainMenuImageInfo lookupTable[numEntries];
	const unsigned int offsetToData = 0x5dc0;
	memcpy_s(lookupTable, sizeof(lookupTable), tableFileData.GetData() + offsetToData, sizeof(lookupTable));

	vector<CustomMenuImageInfo> imageList;

	const string translatedDirectory = inTranslatedDataDirectory + string("PBOOK_FL\\");

	int lutIndex = 0;
	const int offsetToImageData = 0xfb20;
#define MenuImageEntry(imagePath) imageList.emplace_back(SwapByteOrder(lookupTable[lutIndex].widthDiv8)*8, SwapByteOrder(lookupTable[lutIndex].heightDiv8)*8, offsetToImageData, imagePath);\
	                              ++lutIndex

	//Mostly duplicate images from PBOOK_FL, so just reuse them
	MenuImageEntry(translatedDirectory + "13");//0
	MenuImageEntry(translatedDirectory + "14");//1
	MenuImageEntry("");//2
	MenuImageEntry(translatedDirectory + "16");//3
	MenuImageEntry("");//4
	MenuImageEntry(translatedDirectory + "18");//5
	MenuImageEntry(translatedDirectory + "19");//6
	MenuImageEntry("");//7
	MenuImageEntry(translatedDirectory + "21");//8
	MenuImageEntry("");//9
	MenuImageEntry(translatedDirectory + "23");//10
	MenuImageEntry(translatedDirectory + "24");//11
	MenuImageEntry(translatedDirectory + "25");//12
	MenuImageEntry(translatedDirectory + "26");//13 off
	MenuImageEntry("");//14
	MenuImageEntry(translatedDirectory + "28");//15
	MenuImageEntry(translatedDirectory + "29");//16
	MenuImageEntry(translatedDirectory + "30");//17
	MenuImageEntry(translatedDirectory + "32");//18
	MenuImageEntry("");//19
	MenuImageEntry("");//20
	MenuImageEntry("");//21
	MenuImageEntry(translatedDirectory + "36");//22
	MenuImageEntry(translatedDirectory + "37");//23
	MenuImageEntry(translatedDirectory + "38");//24
	MenuImageEntry(translatedDirectory + "39");//25
	MenuImageEntry(translatedDirectory + "40");//26
	MenuImageEntry(translatedDirectory + "41");//27
	MenuImageEntry(translatedDirectory + "42");//28
	MenuImageEntry(translatedDirectory + "43");//29
	MenuImageEntry(translatedDirectory + "44");//30
	MenuImageEntry(translatedDirectory + "45");//31
	MenuImageEntry(translatedDirectory + "46");//32
	MenuImageEntry(translatedDirectory + "47");//33
	MenuImageEntry(translatedDirectory + "48");//34
	MenuImageEntry(translatedDirectory + "49");//35
	MenuImageEntry("");//36
	MenuImageEntry(translatedDirectory + "51");//37
	MenuImageEntry(translatedDirectory + "52");//38
	MenuImageEntry("");//39
	MenuImageEntry(translatedDirectory + "54");//40
	MenuImageEntry("");//41
	MenuImageEntry("");//42
	MenuImageEntry("");//43
	MenuImageEntry("");//44
	MenuImageEntry("");//45
	MenuImageEntry(translatedDirectory + "60");//46
	MenuImageEntry(translatedDirectory + "42");//47
	MenuImageEntry(translatedDirectory + "62");//48
	MenuImageEntry("");//49
	MenuImageEntry(translatedDirectory + "64");//50

	bool bResult = PatchCustomImages(imageList, inPatchedSakuraDirectory + string("SAKURA1\\PBOOK_BT.CG"));
	bResult = bResult && PatchCustomImages(imageList, inPatchedSakuraDirectory + string("SAKURA2\\PBOOK_BT.CG"));

	return bResult;
}

//Battle pause menu
bool Patch_PB_Eye(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{	
	vector<CustomMenuImageInfo> imageList;

	const string pbEye_Dir = inTranslatedDataDirectory + string("PB_Eye\\");

	int lutIndex = 0;

	MainMenuImageInfo* lookupTable = &PBEyeImages[0];
	GenericImageEntry(pbEye_Dir + "0");//0
	GenericImageEntry(pbEye_Dir + "1");//1
	GenericImageEntry(pbEye_Dir + "2");//2
	GenericImageEntry(pbEye_Dir + "3");//3
	GenericImageEntry(pbEye_Dir + "4");//4
	GenericImageEntry(pbEye_Dir + "5");//5 Yes
	GenericImageEntry("");//6
	GenericImageEntry(pbEye_Dir + "7");//7  No
	GenericImageEntry("");//8 
	GenericImageEntry(pbEye_Dir + "9");//9 Item
	GenericImageEntry(pbEye_Dir + "10");//10 Option
	GenericImageEntry(pbEye_Dir + "11");//11 Return
	bool bResult = PatchCustomImages(imageList, inPatchedSakuraDirectory + string("SAKURA1\\PB_EYE.CG"));

	//WIN_CMD
	lutIndex = 0;
	imageList.clear();
	lookupTable = &CmdWinImages[0];
	GenericImageEntry(pbEye_Dir + "9");//0 Item
	GenericImageEntry(pbEye_Dir + "10");//0 Option
	GenericImageEntry(pbEye_Dir + "11");//2 Return

	bResult &= PatchCustomImages(imageList, inPatchedSakuraDirectory + string("SAKURA1\\CMD_WIN.CG"));

	return bResult;
}

bool Patch_PBook_RC(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	vector<CustomMenuImageInfo> imageList;

	const string pbRC_Dir = inTranslatedDataDirectory + string("PBOOK_RC\\");

	int lutIndex = 0;
	
	MainMenuImageInfo* lookupTable = &PBookRCImages[0];
	GenericImageEntry(pbRC_Dir + "0");
	GenericImageEntry(pbRC_Dir + "1");
	GenericImageEntry(pbRC_Dir + "2");
	GenericImageEntry(pbRC_Dir + "3");
	GenericImageEntry(pbRC_Dir + "4");
	GenericImageEntry(pbRC_Dir + "5");
	GenericImageEntry(pbRC_Dir + "6");
	GenericImageEntry(pbRC_Dir + "7");
	GenericImageEntry(pbRC_Dir + "8");
	GenericImageEntry(pbRC_Dir + "9");
	GenericImageEntry(pbRC_Dir + "10");
	GenericImageEntry(pbRC_Dir + "11");
	GenericImageEntry(pbRC_Dir + "12");
	GenericImageEntry(pbRC_Dir + "13");
	GenericImageEntry(pbRC_Dir + "14");

	bool bResult = PatchCustomImages(imageList, inPatchedSakuraDirectory + string("SAKURA1\\PBOOK_RC.CG"));

	return bResult;
}

bool Patch_TETYO_AD(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	vector<CustomMenuImageInfo> imageList;

	const string tetyo_Dir = inTranslatedDataDirectory + string("TETYO_AD\\");

	int lutIndex = 0;

	MainMenuImageInfo* lookupTable = &TetyoADImages[0];
	GenericImageEntry(tetyo_Dir + "0");
	GenericImageEntry(tetyo_Dir + "1");
	GenericImageEntry(tetyo_Dir + "2");
	GenericImageEntry(tetyo_Dir + "3");
	GenericImageEntry(tetyo_Dir + "4");
	GenericImageEntry(tetyo_Dir + "5");
	GenericImageEntry(tetyo_Dir + "6");
	GenericImageEntry(tetyo_Dir + "7");
	GenericImageEntry(tetyo_Dir + "8");
	GenericImageEntry(tetyo_Dir + "9");
	GenericImageEntry(tetyo_Dir + "10");
	GenericImageEntry(tetyo_Dir + "11");
	GenericImageEntry(tetyo_Dir + "12");
	GenericImageEntry(tetyo_Dir + "13");
	GenericImageEntry(tetyo_Dir + "14");
	GenericImageEntry(tetyo_Dir + "15");
	GenericImageEntry(tetyo_Dir + "16");
	GenericImageEntry(tetyo_Dir + "17");
	GenericImageEntry(tetyo_Dir + "18");
	GenericImageEntry(tetyo_Dir + "19");
	GenericImageEntry(tetyo_Dir + "20");

	bool bResult = PatchCustomImages(imageList, inPatchedSakuraDirectory + string("SAKURA1\\TETYO_AD.CG"));

	return bResult;
}

bool PatchMainMenu(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	printf("Patching Main Menu\n");

	const string titleFilePath = inPatchedSakuraDirectory + string("SAKURA1\\TITLE.BIN");
	FileData titleFileData;
	if (!titleFileData.InitializeFileData(titleFilePath))
	{
		return false;
	}

	const string pbookFilePath = inPatchedSakuraDirectory + string("SAKURA1\\PBOOK_FL.CG");
	FileReadWriter pbookFileData;
	if (!pbookFileData.OpenFile(pbookFilePath))
	{
		return false;
	}

	const string translatedDirectory = inTranslatedDataDirectory + string("PBOOK_FL\\");
	const int numEntries = 167;
	MainMenuImageInfo lookupTable[numEntries];
	const unsigned int offsetToData = 0xE1B8;
	titleFileData.GetData();
	memcpy_s((void*)&lookupTable, sizeof(lookupTable), titleFileData.GetData() + offsetToData, sizeof(lookupTable));

	const string bmpExt(".bmp");
	const int baseOffset = 0x3ef60;
	vector<int> addressesPatched;

	PBookFLFilesToSkip.insert(2);
	PBookFLFilesToSkip.insert(8);
	PBookFLFilesToSkip.insert(15);
	PBookFLFilesToSkip.insert(20);
	PBookFLFilesToSkip.insert(27);
	PBookFLFilesToSkip.insert(50);
	PBookFLFilesToSkip.insert(53);
	PBookFLFilesToSkip.insert(55);
	PBookFLFilesToSkip.insert(56);
	PBookFLFilesToSkip.insert(63);
	PBookFLFilesToSkip.insert(67);
	PBookFLFilesToSkip.insert(69);
	PBookFLFilesToSkip.insert(79);
	PBookFLFilesToSkip.insert(142);

	for (int i = 0; i < numEntries; ++i)
	{
		if(PBookFLFilesToSkip.find(i) != PBookFLFilesToSkip.end())
		{
			continue;
		}

		const int offset = SwapByteOrder(lookupTable[i].offsetDiv8) * 8;
		const int width = SwapByteOrder(lookupTable[i].widthDiv8) * 8;
		const int height = SwapByteOrder(lookupTable[i].heightDiv8) * 8;

		const string translatedImagePath = translatedDirectory + std::to_string(i) + bmpExt;

		BmpToSaturnConverter convertedImage;
		if(!convertedImage.ConvertBmpToSakuraFormat(translatedImagePath, false))
		{
			continue;
		}

		if(convertedImage.GetImageHeight() != height)
		{
			printf("Expected height of %i, got %i for %s\n", height, convertedImage.GetImageHeight(), translatedImagePath.c_str());
			return false;
		}

		if(convertedImage.GetImageWidth() != width)
		{
			printf("Expected width of %i, got %i for %s\n", width, convertedImage.GetImageWidth(), translatedImagePath.c_str());
			return false;
		}

		if (convertedImage.GetImageDataSize() != ((width * height) >> 1))
		{
			printf("Expected data size of %i, got %i for %s\n", (width*height)>>1, convertedImage.GetImageWidth(), translatedImagePath.c_str());
			return false;
		}

		pbookFileData.WriteData(baseOffset + offset, convertedImage.GetImageData(), convertedImage.GetImageDataSize(), false);

		addressesPatched.push_back(baseOffset + offset);
	}

	int accumulatedOffset = 0;
	int prevOffset = 0;
	for (int i = 0; i < NumCustomMainMenuImages; ++i)
	{
		const int offset = ManuallyFoundMainMenuImages[i].offsetDiv8 * 8;
		const int width = ManuallyFoundMainMenuImages[i].widthDiv8;
		const int height = ManuallyFoundMainMenuImages[i].heightDiv8;

		//For when the addresses jump
		if (offset != prevOffset)
		{
			accumulatedOffset = 0;
		}

		const string translatedImagePath = translatedDirectory + std::to_string(i + numEntries) + bmpExt;

		BmpToSaturnConverter convertedImage;
		if (!convertedImage.ConvertBmpToSakuraFormat(translatedImagePath, false))
		{
			return false;
		}

		if (convertedImage.GetImageHeight() != height)
		{
			printf("Expected height of %i, got %i for %s\n", height, convertedImage.GetImageHeight(), translatedImagePath.c_str());
			return false;
		}

		if (convertedImage.GetImageWidth() != width)
		{
			printf("Expected width of %i, got %i for %s\n", width, convertedImage.GetImageWidth(), translatedImagePath.c_str());
			return false;
		}

		if (convertedImage.GetImageDataSize() != ((width * height) >> 1))
		{
			printf("Expected data size of %i, got %i for %s\n", (width * height) >> 1, convertedImage.GetImageDataSize(), translatedImagePath.c_str());
			return false;
		}

		pbookFileData.WriteData(offset + accumulatedOffset, convertedImage.GetImageData(), convertedImage.GetImageDataSize(), false);

		accumulatedOffset += width * height / 2;
		prevOffset = offset;

	}

	if(!Patch_Hanko(inPatchedSakuraDirectory, inTranslatedDataDirectory))
	{
		return false;
	}

	if(!Patch_PBook_EC(inPatchedSakuraDirectory, inTranslatedDataDirectory))
	{
		return false;
	}

	if (!Patch_PBook_BT(inPatchedSakuraDirectory, inTranslatedDataDirectory))
	{
		return false;
	}

	if (!Patch_PB_Eye(inPatchedSakuraDirectory, inTranslatedDataDirectory))
	{
		return false;
	}

	if (!Patch_PBook_RC(inPatchedSakuraDirectory, inTranslatedDataDirectory))
	{
		return false;
	}

	if(!Patch_TETYO_AD(inPatchedSakuraDirectory, inTranslatedDataDirectory))
	{
		return false;
	}
	
	return true;
}

void ExtractPBookFL(const string& rootSakuraDirectory, bool bBmp, const string& outDirectory)
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

	//PBOOK_FL
	const string dataFilePath = rootSakuraDirectory + string("SAKURA1\\") + string("PBOOK_FL") + string(".CG");
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

	PaletteData charBioPalette;
	if (!charBioPalette.CreateFrom15BitData(sakuraFileData.GetData() + 0x00002350, paletteSize))
	{
		printf("Unable to create charBioPalette 1.\n");
		return;
	}

	const string finalOutputDirectory = outDirectory + string("PBOOK_FL") + string("\\");
	CreateDirectoryHelper(finalOutputDirectory);

	const int numEntries = 167;
	MainMenuImageInfo lookupTable[numEntries];
	const unsigned int offsetToData = 0xE1B8;
	memcpy_s(lookupTable, sizeof(lookupTable), sakuraFileData.GetData() + offsetToData, sizeof(lookupTable));

	std::vector<uint32> addresses;
	const int baseOffset = 0x3ef60;
	for (int i = 0; i < numEntries; ++i)
	{
		const int offset = SwapByteOrder(lookupTable[i].offsetDiv8) * 8;
		const int width  = SwapByteOrder(lookupTable[i].widthDiv8) * 8;
		const int height = SwapByteOrder(lookupTable[i].heightDiv8) * 8;

		const string outFileName = finalOutputDirectory + std::to_string(i) + bmpExt;

		addresses.push_back(baseOffset + offset);

		BitmapWriter outBmp;
		outBmp.CreateBitmap(outFileName, width, -height, 4, fileData.GetData() + baseOffset + offset, (width * height) / 2, palette.GetData(), palette.GetSize(), bBmp);
	}

	//Manually found images
	int accumulatedOffset = 0;
	int prevOffset = 0;
	for (int i = 0; i < NumCustomMainMenuImages; ++i)
	{
		const int offset = ManuallyFoundMainMenuImages[i].offsetDiv8 * 8;
		const int width = ManuallyFoundMainMenuImages[i].widthDiv8;
		const int height = ManuallyFoundMainMenuImages[i].heightDiv8;

		//For when the addresses jump
		if(offset != prevOffset)
		{
			accumulatedOffset = 0;
		}

		const string outFileName = finalOutputDirectory + std::to_string(i + numEntries) + bmpExt;

		PaletteData* pPalette = ManuallyFoundMainMenuImages[i].offsetDiv8 == CharBioImageOffset ? &charBioPalette : &palette;
		BitmapWriter outBmp;
		outBmp.CreateBitmap(outFileName, width, -height, 4, fileData.GetData() + offset + accumulatedOffset, (width * height) / 2, pPalette->GetData(), pPalette->GetSize(), bBmp);

		accumulatedOffset += width * height / 2;
		prevOffset = offset;
	}
}

void ExtractPBookEC(const string& rootSakuraDirectory, bool bBmp, const string& outDirectory)
{
	CreateDirectoryHelper(outDirectory);

	const string sakuraFilePath = rootSakuraDirectory + string("SAKURA.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());
	FileData sakuraFileData;
	if (!sakuraFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return;
	}

	const string bmpExt = bBmp ? string(".bmp") : (".png");

	//PBOOK_EC
	const string dataFilePath = rootSakuraDirectory + string("SAKURA1\\") + string("PBOOK_EC") + string(".CG");
	FileNameContainer dataFileNameInfo(dataFilePath.c_str());

	FileData fileData;
	if (!fileData.InitializeFileData(dataFileNameInfo))
	{
		return;
	}

	const int paletteSize = 32;

	PaletteData palette;
	if (!palette.CreateFrom15BitData(sakuraFileData.GetData() + 0x824a0, paletteSize))
	{
		printf("Unable to create palette 1.\n");
		return;
	}

	const string finalOutputDirectory = outDirectory + string("PBOOK_EC") + string("\\");
	CreateDirectoryHelper(finalOutputDirectory);

	const int numEntries = 111;
	MainMenuImageInfo lookupTable[numEntries];
	const unsigned int offsetToData = 0x82fb4;
	memcpy_s(lookupTable, sizeof(lookupTable), sakuraFileData.GetData() + offsetToData, sizeof(lookupTable));

	std::vector<uint32> addresses;
	int offset = 0;
	for (int i = 0; i < numEntries; ++i)
	{
		if(i >= 92 && i <= 102)
		{
			continue;
		}

		const int offset = SwapByteOrder(lookupTable[i].offsetDiv8) * 8;
		const int width = SwapByteOrder(lookupTable[i].widthDiv8) * 8;
		const int height = SwapByteOrder(lookupTable[i].heightDiv8) * 8;

		const string outFileName = finalOutputDirectory + std::to_string(i) + bmpExt;

		addresses.push_back(offset);

		BitmapWriter outBmp;
		outBmp.CreateBitmap(outFileName, width, -height, 4, fileData.GetData() + offset, (width * height) / 2, palette.GetData(), palette.GetSize(), bBmp);
	}

	/*
	//Manually found images
	int accumulatedOffset = 0;
	int prevOffset = 0;
	for (int i = 0; i < NumCustomMainMenuImages; ++i)
	{
		const int offset = ManuallyFoundMainMenuImages[i].offsetDiv8 * 8;
		const int width = ManuallyFoundMainMenuImages[i].widthDiv8;
		const int height = ManuallyFoundMainMenuImages[i].heightDiv8;

		//For when the addresses jump
		if (offset != prevOffset)
		{
			accumulatedOffset = 0;
		}

		const string outFileName = finalOutputDirectory + std::to_string(i + numEntries) + bmpExt;

		PaletteData* pPalette = ManuallyFoundMainMenuImages[i].offsetDiv8 == CharBioImageOffset ? &charBioPalette : &palette;
		BitmapWriter outBmp;
		outBmp.CreateBitmap(outFileName, width, -height, 4, fileData.GetData() + offset + accumulatedOffset, (width * height) / 2, pPalette->GetData(), pPalette->GetSize(), bBmp);

		accumulatedOffset += width * height / 2;
		prevOffset = offset;
	}*/
}

void ExtractBattlePauseMenu(const string& rootSakuraDirectory, bool bBmp, const string& outDirectory)
{
	CreateDirectoryHelper(outDirectory);

	const string sakuraFilePath = rootSakuraDirectory + string("SAKURA2\\SLGNTBK.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());
	FileData tableFileData;
	if (!tableFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return;
	}

	const string bmpExt = bBmp ? string(".bmp") : (".png");

	const char* filesToExtract[1] = { "PBOOK_BT" };

	//PBOOK_BT
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
		if (!palette.CreateFrom15BitData(tableFileData.GetData() + 0x40c, paletteSize))
		{
			printf("Unable to create palette 1.\n");
			return;
		}

		const string finalOutputDirectory = outDirectory + string(filesToExtract[fileIndex]) + string("\\");
		CreateDirectoryHelper(finalOutputDirectory);

		const int numEntries = 62;
		MainMenuImageInfo lookupTable[numEntries];
		const unsigned int offsetToData = 0x5dc0;
		memcpy_s(lookupTable, sizeof(lookupTable), tableFileData.GetData() + offsetToData, sizeof(lookupTable));

		const int baseOffset = 0xfb20;

		for (int i = 0; i < numEntries; ++i)
		{
			const int offset = SwapByteOrder(lookupTable[i].offsetDiv8) * 8;
			const int width = SwapByteOrder(lookupTable[i].widthDiv8) * 8;
			const int height = SwapByteOrder(lookupTable[i].heightDiv8) * 8;

			const string outFileName = finalOutputDirectory + std::to_string(i) + bmpExt;

			BitmapWriter outBmp;
			outBmp.CreateBitmap(outFileName, width, -height, 4, fileData.GetData() + baseOffset + offset, (width * height) / 2, palette.GetData(), palette.GetSize(), bBmp);
		}
	}
}

bool ExtractPBEye(const string& rootSakuraDirectory, bool bBmp, const string& outDirectory)
{
	CreateDirectoryHelper(outDirectory);

	const string sakuraFilePath = rootSakuraDirectory + string("SAKURA1\\TITLE.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());
	FileData sakuraFileData;
	if (!sakuraFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return false;
	}

	PaletteData palette;
	if (!palette.CreateFrom15BitData(sakuraFileData.GetData() + 0x00002250, 32))
	{
		printf("Unable to create palette 1.\n");
		return false;
	}

	const string bmpExt = bBmp ? string(".bmp") : (".png");

	struct FileExtractInfo
	{
		const char* pOutDirName;
		const char* pFileName;
		MainMenuImageInfo* pImages;
		int numImages;
	};

	FileExtractInfo filesToExtract[] = 
	{	{"PB_EYE", "SAKURA1\\PB_EYE.CG", &PBEyeImages[0], sizeof(PBEyeImages)/sizeof(PBEyeImages[0])},
		{"CMD_WIN", "SAKURA1\\CMD_WIN.CG", &CmdWinImages[0], sizeof(CmdWinImages) / sizeof(CmdWinImages[0])},
		{"PBookRCImages", "SAKURA1\\PBOOK_RC.CG", &PBookRCImages[0], sizeof(PBookRCImages)/sizeof(PBookRCImages[0])},
		{"TetyoADImages", "SAKURA1\\TETYO_AD.CG", &TetyoADImages[0], sizeof(TetyoADImages) / sizeof(TetyoADImages[0])},
	};

	//PBOOK_FL
	const int numFiles = sizeof(filesToExtract)/sizeof(FileExtractInfo);
	for (int fileIndex = 0; fileIndex < numFiles; ++fileIndex)
	{
		const string dataFilePath = rootSakuraDirectory + string(filesToExtract[fileIndex].pFileName);
		FileData fileData;
		if (!fileData.InitializeFileData(dataFilePath))
		{
			return false;
		}

		const string finalOutputDirectory = outDirectory + string(filesToExtract[fileIndex].pOutDirName) + string("\\");
		CreateDirectoryHelper(finalOutputDirectory);

		//Manually found images
		int accumulatedOffset = 0;
		int prevOffset = 0;
		const int numImages = filesToExtract[fileIndex].numImages;
		for (int i = 0; i < numImages; ++i)
		{
			const int offset = filesToExtract[fileIndex].pImages[i].offsetDiv8;
			const int width  = filesToExtract[fileIndex].pImages[i].widthDiv8;
			const int height = filesToExtract[fileIndex].pImages[i].heightDiv8;

			//For when the addresses jump
			if (offset != prevOffset)
			{
				accumulatedOffset = 0;
			}

			const string outFileName = finalOutputDirectory + std::to_string(i) + bmpExt;

			BitmapWriter outBmp;
			outBmp.CreateBitmap(outFileName, width, -height, 4, fileData.GetData() + offset + accumulatedOffset, (width * height) / 2, palette.GetData(), palette.GetSize(), bBmp);

			accumulatedOffset += width * height / 2;
			prevOffset = offset;
		}
	}

	return true;
}

bool CreateNamePBookFLSpreadsheet(const string& imageDirectory)
{
	TextFileWriter htmlFile;
	const string htmlFileName = imageDirectory + string("..\\..\\Translation\\PBOOK_FL.php");
	if (!htmlFile.OpenFileForWrite(htmlFileName))
	{
		printf("Unable to create an html file: %s", htmlFileName.c_str());
		return false;
	}

	htmlFile.WriteString("<html>\n");
	htmlFile.WriteString("	<head><style>textarea {width: 100%;top: 0; left: 0; right: 0; bottom: 0; position: absolute; resize: none;-webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;} table {border-collapse: collapse;} table, th, td { position: relative; border: 1px solid black;}#myProgress {width: 100%;	background-color: #ddd;} #myBar {width: 1%;height: 30px; background-color: #4CAF50;} .rotate90{ -webkit - transform: rotate(270deg); -moz - transform: rotate(270deg); -o - transform: rotate(270deg); -ms - transform: rotate(270deg); transform: rotate(270deg); }</style>\n");
	htmlFile.WriteString("	<div id=\"FileName\" style=\"display: none;\">PBOOK_FL</div>\n");
	htmlFile.WriteString("	<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.1/jquery.min.js\">\n");
	htmlFile.WriteString("		$( window ).on( \"load\", function()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			OnStartup();\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("	</script>\n\n");
	htmlFile.WriteString("	<script type=\"text/javascript\">\n");
	htmlFile.WriteString("		function SaveData(inDialogImageName, inDivID)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			var translatedText = document.getElementById(inDivID).value;\n");
	htmlFile.WriteString("			var fileName = document.getElementById(\"FileName\").innerHTML;\n");
	htmlFile.WriteString("			$.ajax({\n");
	htmlFile.WriteString("				type: \"POST\",\n");
	htmlFile.WriteString("				url: \"UpdateTranslationTest.php\",\n");
	htmlFile.WriteString("				data: { inTBLFileName: fileName, inImageName:inDialogImageName, inTranslation:translatedText, inDivId:inDivID, inCrc:0 },\n");
	htmlFile.WriteString("				success: function(result)\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					var trId = \"tr_\" + inDivID;\n");
	htmlFile.WriteString("					if( translatedText != \"Untranslated\" && translatedText != \"<div>Untranslated</div>\")\n");
	htmlFile.WriteString("					{\n");
	htmlFile.WriteString("						if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
	htmlFile.WriteString("						{\n");
	htmlFile.WriteString("							document.getElementById(trId).bgColor = \"#e3fec8\";\n");
	htmlFile.WriteString("						}\n");
	htmlFile.WriteString("					}\n");
	htmlFile.WriteString("				else\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					document.getElementById(trId).bgColor = \"#fefec8\";\n");
	htmlFile.WriteString("				}\n");
	htmlFile.WriteString("			}\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("}\n");
	htmlFile.WriteString("		function SaveEdits(inDialogImageName, inDivID)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			SaveData(inDialogImageName, inDivID);\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("		function ExportData()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			$(\"textarea\").each ( function ()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				var thisText = $(this).text();\n");
	htmlFile.WriteString("				thisText = thisText.replace(/<br>/g, '&ltbr&gt');\n");
	htmlFile.WriteString("				thisText = thisText.replace(/<sp>/g, '&ltsp&gt');\n");
	htmlFile.WriteString("				document.write(thisText + \"<br>\");\n");
	htmlFile.WriteString("			});\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	function LoadData()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		var fileName = document.getElementById(\"FileName\").innerHTML;\n");
	htmlFile.WriteString("		$.ajax({\n");
	htmlFile.WriteString("		type: \"POST\",\n");
	htmlFile.WriteString("			  url: \"GetTranslationData.php\",\n");
	htmlFile.WriteString("				data: { inTBLFileName: fileName},\n");
	htmlFile.WriteString("				success: function(result)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			var json = $.parseJSON(result);\n");
	htmlFile.WriteString("			var i;\n");
	htmlFile.WriteString("			for (i = 0; i < json.length; i++)\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				var jsonEntry = json[i];\n");
	htmlFile.WriteString("				var english   = jsonEntry.English.replace(/\\\\/g, \'\');\n");
	htmlFile.WriteString("				var divId     = \"#\" + jsonEntry.DivId;\n");
	htmlFile.WriteString("					var trId      = \"tr_\" + jsonEntry.DivId;\n");
	htmlFile.WriteString("					if( english != \"Untranslated\" && english != \"<div>Untranslated</div>\")\n");
	htmlFile.WriteString("					{\n");
	htmlFile.WriteString("						if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
	htmlFile.WriteString("						{\n");
	htmlFile.WriteString("						       document.getElementById(trId).bgColor = \"#e3fec8\";\n");
	htmlFile.WriteString("						}\n");
	htmlFile.WriteString("						$(divId).html(english);\n");
	htmlFile.WriteString("					}\n");
	htmlFile.WriteString("			}\n");
	htmlFile.WriteString("		},\n");
	htmlFile.WriteString("		error: function()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			alert('Unable to load data');\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	function FixOnChangeEditableElements()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		var tags = document.querySelectorAll('[contenteditable=true][onChange]');\n");
	htmlFile.WriteString("		for (var i=tags.length-1; i>=0; i--) if (typeof(tags[i].onblur)!='function')\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			tags[i].onfocus = function()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				this.data_orig=this.innerHTML;\n");
	htmlFile.WriteString("			};\n");
	htmlFile.WriteString("			tags[i].onblur = function()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				if( this.innerHTML != this.data_orig)\n");
	htmlFile.WriteString("					this.onchange();\n");
	htmlFile.WriteString("				delete this.data_orig;\n");
	htmlFile.WriteString("			};\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	function OnStartup()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		FixOnChangeEditableElements();\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	</script>\n");
	htmlFile.WriteString("	</head>\n");
	htmlFile.WriteString("	\n");
	htmlFile.WriteString("	<body>\n");
	htmlFile.WriteString("	<?php include 'GetUserPermissions.php';\n");
	htmlFile.WriteString("	$bPermissionFound = false;\n");
	htmlFile.WriteString("	foreach ($allowedFiles as $value)\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		if( $value == \"PBOOK_FL\" )\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			$bPermissionFound = true;\n");
	htmlFile.WriteString("			break;\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	if( $bPermissionFound || $masterUnlock )\n\t{\n?>");
	htmlFile.WriteString("		<article><header align=\"center\"><h1>File: PBOOK_FL</h1></header></article>\n");
	htmlFile.WriteString("			<br>\n");
	htmlFile.WriteString("			<b>Instructions:</b><br>\n");
	htmlFile.WriteString("			-This page is best displayed using Chrome.  Otherwise some of the table borders are missing for some reason.<br>\n");
	htmlFile.WriteString("			-Your changes are automatically saved.<br>\n");
	htmlFile.WriteString("			-Press the Load Data button when you come back to the page to load your changes.<br>\n");
	htmlFile.WriteString("			<b>Naming Conventions:</n><br>\n");
	htmlFile.WriteString("			<a href=\"https://docs.google.com/spreadsheets/d/1imZZn_SfbmMxBpEnyj8_oZ1BEnMN0q0Ck1XYQgKggm4/edit?usp=sharing\" target=\"_blank\">Click here to view the naming conventions for Characters, Locations, and Terms</a> <br>\n");
	htmlFile.WriteString("			<?php\n");
	htmlFile.WriteString("				$currUser = $_SERVER['PHP_AUTH_USER'];\n");
	htmlFile.WriteString("				if( $currUser == \"rahmed\" )\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					echo \"<input align=\\\"center\\\" type=\\\"button\\\" value=\\\"Export Data\\\" onclick=\\\"ExportData()\\\"/>\";\n");
	htmlFile.WriteString("				}\n");
	htmlFile.WriteString("			?>\n\n");
	htmlFile.WriteString("			<table align=\"center\">\n");
	htmlFile.WriteString("				<tr>\n");
	htmlFile.WriteString("					<td>\n");
	htmlFile.WriteString("						<input align=\"center\" type=\"button\" value=\"Load Data\" onclick=\"LoadData()\"/>\n");
	htmlFile.WriteString("					</td>\n");
	htmlFile.WriteString("				</tr>\n");
	htmlFile.WriteString("			</table><br>\n\n");
	htmlFile.WriteString("			<table>\n");
	htmlFile.WriteString("				<tr bgcolor=\"#c8c8fe\">\n");
	htmlFile.WriteString("					<th>#</th>\n");
	htmlFile.WriteString("					<th>Japanese</th>\n");
	htmlFile.WriteString("					<th>English</th>\n");
	htmlFile.WriteString("				</tr>\n");

	vector<FileNameContainer> fileNames;
	FindAllFilesWithinDirectory(imageDirectory, fileNames);

	int i = 0;
	for (const FileNameContainer& fileName : fileNames)
	{
		const char* pImageNumber = fileName.mNoExtension.c_str();

		fprintf(htmlFile.GetFileHandle(), "				<tr id=\"tr_edit_%i\" bgcolor=\"#fefec8\">\n", i);
		fprintf(htmlFile.GetFileHandle(), "					<td align=\"center\" width=\"20\">%s</td><td width=\"128\" height=\"16\" align=\"center\"><img src=\"..\\ExtractedData\\SakuraWars2\\Disc1\\PBOOK_FL\\%s.png\" class=\"rotate90\" ></td><td width=\"480\"><textarea id=\"edit_%i\" contenteditable=true onchange=\"SaveEdits('%i.bmp', 'edit_%i')\" style=\"border: none; width: 100%%; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;\">Untranslated</textarea></td>\n", pImageNumber, pImageNumber, i, i, i);
		fprintf(htmlFile.GetFileHandle(), "				</tr>\n");

		++i;
	}

	htmlFile.WriteString("			</table><br>\n");
	htmlFile.WriteString("			<?php\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	?></body>\n");
	htmlFile.WriteString("	</html>\n");

	return true;

}

bool CreateNamePBookBTSpreadsheet(const string& imageDirectory)
{
	TextFileWriter htmlFile;
	const string htmlFileName = imageDirectory + string("..\\..\\..\\Translation\\PBOOK_BT.php");
	if (!htmlFile.OpenFileForWrite(htmlFileName))
	{
		printf("Unable to create an html file: %s", htmlFileName.c_str());
		return false;
	}

	htmlFile.WriteString("<html>\n");
	htmlFile.WriteString("	<head><style>textarea {width: 100%;top: 0; left: 0; right: 0; bottom: 0; position: absolute; resize: none;-webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;} table {border-collapse: collapse;} table, th, td { position: relative; border: 1px solid black;}#myProgress {width: 100%;	background-color: #ddd;} #myBar {width: 1%;height: 30px; background-color: #4CAF50;} .rotate90{ -webkit - transform: rotate(270deg); -moz - transform: rotate(270deg); -o - transform: rotate(270deg); -ms - transform: rotate(270deg); transform: rotate(270deg); }</style>\n");
	htmlFile.WriteString("	<div id=\"FileName\" style=\"display: none;\">PBOOK_BT</div>\n");
	htmlFile.WriteString("	<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.1/jquery.min.js\">\n");
	htmlFile.WriteString("		$( window ).on( \"load\", function()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			OnStartup();\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("	</script>\n\n");
	htmlFile.WriteString("	<script type=\"text/javascript\">\n");
	htmlFile.WriteString("		function SaveData(inDialogImageName, inDivID)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			var translatedText = document.getElementById(inDivID).value;\n");
	htmlFile.WriteString("			var fileName = document.getElementById(\"FileName\").innerHTML;\n");
	htmlFile.WriteString("			$.ajax({\n");
	htmlFile.WriteString("				type: \"POST\",\n");
	htmlFile.WriteString("				url: \"UpdateTranslationTest.php\",\n");
	htmlFile.WriteString("				data: { inTBLFileName: fileName, inImageName:inDialogImageName, inTranslation:translatedText, inDivId:inDivID, inCrc:0 },\n");
	htmlFile.WriteString("				success: function(result)\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					var trId = \"tr_\" + inDivID;\n");
	htmlFile.WriteString("					if( translatedText != \"Untranslated\" && translatedText != \"<div>Untranslated</div>\")\n");
	htmlFile.WriteString("					{\n");
	htmlFile.WriteString("						if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
	htmlFile.WriteString("						{\n");
	htmlFile.WriteString("							document.getElementById(trId).bgColor = \"#e3fec8\";\n");
	htmlFile.WriteString("						}\n");
	htmlFile.WriteString("					}\n");
	htmlFile.WriteString("				else\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					document.getElementById(trId).bgColor = \"#fefec8\";\n");
	htmlFile.WriteString("				}\n");
	htmlFile.WriteString("			}\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("}\n");
	htmlFile.WriteString("		function SaveEdits(inDialogImageName, inDivID)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			SaveData(inDialogImageName, inDivID);\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("		function ExportData()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			$(\"textarea\").each ( function ()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				var thisText = $(this).text();\n");
	htmlFile.WriteString("				thisText = thisText.replace(/<br>/g, '&ltbr&gt');\n");
	htmlFile.WriteString("				thisText = thisText.replace(/<sp>/g, '&ltsp&gt');\n");
	htmlFile.WriteString("				document.write(thisText + \"<br>\");\n");
	htmlFile.WriteString("			});\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	function LoadData()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		var fileName = document.getElementById(\"FileName\").innerHTML;\n");
	htmlFile.WriteString("		$.ajax({\n");
	htmlFile.WriteString("		type: \"POST\",\n");
	htmlFile.WriteString("			  url: \"GetTranslationData.php\",\n");
	htmlFile.WriteString("				data: { inTBLFileName: fileName},\n");
	htmlFile.WriteString("				success: function(result)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			var json = $.parseJSON(result);\n");
	htmlFile.WriteString("			var i;\n");
	htmlFile.WriteString("			for (i = 0; i < json.length; i++)\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				var jsonEntry = json[i];\n");
	htmlFile.WriteString("				var english   = jsonEntry.English.replace(/\\\\/g, \'\');\n");
	htmlFile.WriteString("				var divId     = \"#\" + jsonEntry.DivId;\n");
	htmlFile.WriteString("					var trId      = \"tr_\" + jsonEntry.DivId;\n");
	htmlFile.WriteString("					if( english != \"Untranslated\" && english != \"<div>Untranslated</div>\")\n");
	htmlFile.WriteString("					{\n");
	htmlFile.WriteString("						if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
	htmlFile.WriteString("						{\n");
	htmlFile.WriteString("						       document.getElementById(trId).bgColor = \"#e3fec8\";\n");
	htmlFile.WriteString("						}\n");
	htmlFile.WriteString("						$(divId).html(english);\n");
	htmlFile.WriteString("					}\n");
	htmlFile.WriteString("			}\n");
	htmlFile.WriteString("		},\n");
	htmlFile.WriteString("		error: function()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			alert('Unable to load data');\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	function FixOnChangeEditableElements()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		var tags = document.querySelectorAll('[contenteditable=true][onChange]');\n");
	htmlFile.WriteString("		for (var i=tags.length-1; i>=0; i--) if (typeof(tags[i].onblur)!='function')\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			tags[i].onfocus = function()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				this.data_orig=this.innerHTML;\n");
	htmlFile.WriteString("			};\n");
	htmlFile.WriteString("			tags[i].onblur = function()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				if( this.innerHTML != this.data_orig)\n");
	htmlFile.WriteString("					this.onchange();\n");
	htmlFile.WriteString("				delete this.data_orig;\n");
	htmlFile.WriteString("			};\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	function OnStartup()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		FixOnChangeEditableElements();\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	</script>\n");
	htmlFile.WriteString("	</head>\n");
	htmlFile.WriteString("	\n");
	htmlFile.WriteString("	<body>\n");
	htmlFile.WriteString("	<?php include 'GetUserPermissions.php';\n");
	htmlFile.WriteString("	$bPermissionFound = false;\n");
	htmlFile.WriteString("	foreach ($allowedFiles as $value)\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		if( $value == \"PBOOPBOOK_BTK_FL\" )\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			$bPermissionFound = true;\n");
	htmlFile.WriteString("			break;\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	if( $bPermissionFound || $masterUnlock )\n\t{\n?>");
	htmlFile.WriteString("		<article><header align=\"center\"><h1>File: PBOOK_BT</h1></header></article>\n");
	htmlFile.WriteString("			<br>\n");
	htmlFile.WriteString("			<b>Instructions:</b><br>\n");
	htmlFile.WriteString("			-This page is best displayed using Chrome.  Otherwise some of the table borders are missing for some reason.<br>\n");
	htmlFile.WriteString("			-Your changes are automatically saved.<br>\n");
	htmlFile.WriteString("			-Press the Load Data button when you come back to the page to load your changes.<br>\n");
	htmlFile.WriteString("			<b>Naming Conventions:</n><br>\n");
	htmlFile.WriteString("			<a href=\"https://docs.google.com/spreadsheets/d/1imZZn_SfbmMxBpEnyj8_oZ1BEnMN0q0Ck1XYQgKggm4/edit?usp=sharing\" target=\"_blank\">Click here to view the naming conventions for Characters, Locations, and Terms</a> <br>\n");
	htmlFile.WriteString("			<?php\n");
	htmlFile.WriteString("				$currUser = $_SERVER['PHP_AUTH_USER'];\n");
	htmlFile.WriteString("				if( $currUser == \"rahmed\" )\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					echo \"<input align=\\\"center\\\" type=\\\"button\\\" value=\\\"Export Data\\\" onclick=\\\"ExportData()\\\"/>\";\n");
	htmlFile.WriteString("				}\n");
	htmlFile.WriteString("			?>\n\n");
	htmlFile.WriteString("			<table align=\"center\">\n");
	htmlFile.WriteString("				<tr>\n");
	htmlFile.WriteString("					<td>\n");
	htmlFile.WriteString("						<input align=\"center\" type=\"button\" value=\"Load Data\" onclick=\"LoadData()\"/>\n");
	htmlFile.WriteString("					</td>\n");
	htmlFile.WriteString("				</tr>\n");
	htmlFile.WriteString("			</table><br>\n\n");
	htmlFile.WriteString("			<table>\n");
	htmlFile.WriteString("				<tr bgcolor=\"#c8c8fe\">\n");
	htmlFile.WriteString("					<th>#</th>\n");
	htmlFile.WriteString("					<th>Japanese</th>\n");
	htmlFile.WriteString("					<th>English</th>\n");
	htmlFile.WriteString("				</tr>\n");

	vector<FileNameContainer> fileNames;
	FindAllFilesWithinDirectory(imageDirectory, fileNames);

	int i = 0;
	for (const FileNameContainer& fileName : fileNames)
	{
		const char* pImageNumber = fileName.mNoExtension.c_str();

		fprintf(htmlFile.GetFileHandle(), "				<tr id=\"tr_edit_%i\" bgcolor=\"#fefec8\">\n", i);
		fprintf(htmlFile.GetFileHandle(), "					<td align=\"center\" width=\"20\">%s</td><td width=\"128\" height=\"16\" align=\"center\"><img src=\"..\\ExtractedData\\SakuraWars2\\Disc1\\PBOOK_BT\\%s.png\" class=\"rotate90\" ></td><td width=\"480\"><textarea id=\"edit_%i\" contenteditable=true onchange=\"SaveEdits('%i.bmp', 'edit_%i')\" style=\"border: none; width: 100%%; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;\">Untranslated</textarea></td>\n", pImageNumber, pImageNumber, i, i, i);
		fprintf(htmlFile.GetFileHandle(), "				</tr>\n");

		++i;
	}

	htmlFile.WriteString("			</table><br>\n");
	htmlFile.WriteString("			<?php\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	?></body>\n");
	htmlFile.WriteString("	</html>\n");

	return true;

}

bool CreateNamePBookECSpreadsheet(const string& imageDirectory)
{
	TextFileWriter htmlFile;
	const string htmlFileName = imageDirectory + string("..\\..\\..\\Translation\\PBOOK_EC.php");
	if (!htmlFile.OpenFileForWrite(htmlFileName))
	{
		printf("Unable to create an html file: %s", htmlFileName.c_str());
		return false;
	}

	htmlFile.WriteString("<html>\n");
	htmlFile.WriteString("	<head><style>textarea {width: 100%;top: 0; left: 0; right: 0; bottom: 0; position: absolute; resize: none;-webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;} table {border-collapse: collapse;} table, th, td { position: relative; border: 1px solid black;}#myProgress {width: 100%;	background-color: #ddd;} #myBar {width: 1%;height: 30px; background-color: #4CAF50;} .rotate90{ -webkit - transform: rotate(270deg); -moz - transform: rotate(270deg); -o - transform: rotate(270deg); -ms - transform: rotate(270deg); transform: rotate(270deg); }</style>\n");
	htmlFile.WriteString("	<div id=\"FileName\" style=\"display: none;\">PBOOK_EC</div>\n");
	htmlFile.WriteString("	<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.1/jquery.min.js\">\n");
	htmlFile.WriteString("		$( window ).on( \"load\", function()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			OnStartup();\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("	</script>\n\n");
	htmlFile.WriteString("	<script type=\"text/javascript\">\n");
	htmlFile.WriteString("		function SaveData(inDialogImageName, inDivID)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			var translatedText = document.getElementById(inDivID).value;\n");
	htmlFile.WriteString("			var fileName = document.getElementById(\"FileName\").innerHTML;\n");
	htmlFile.WriteString("			$.ajax({\n");
	htmlFile.WriteString("				type: \"POST\",\n");
	htmlFile.WriteString("				url: \"UpdateTranslationTest.php\",\n");
	htmlFile.WriteString("				data: { inTBLFileName: fileName, inImageName:inDialogImageName, inTranslation:translatedText, inDivId:inDivID, inCrc:0 },\n");
	htmlFile.WriteString("				success: function(result)\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					var trId = \"tr_\" + inDivID;\n");
	htmlFile.WriteString("					if( translatedText != \"Untranslated\" && translatedText != \"<div>Untranslated</div>\")\n");
	htmlFile.WriteString("					{\n");
	htmlFile.WriteString("						if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
	htmlFile.WriteString("						{\n");
	htmlFile.WriteString("							document.getElementById(trId).bgColor = \"#e3fec8\";\n");
	htmlFile.WriteString("						}\n");
	htmlFile.WriteString("					}\n");
	htmlFile.WriteString("				else\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					document.getElementById(trId).bgColor = \"#fefec8\";\n");
	htmlFile.WriteString("				}\n");
	htmlFile.WriteString("			}\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("}\n");
	htmlFile.WriteString("		function SaveEdits(inDialogImageName, inDivID)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			SaveData(inDialogImageName, inDivID);\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("		function ExportData()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			$(\"textarea\").each ( function ()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				var thisText = $(this).text();\n");
	htmlFile.WriteString("				thisText = thisText.replace(/<br>/g, '&ltbr&gt');\n");
	htmlFile.WriteString("				thisText = thisText.replace(/<sp>/g, '&ltsp&gt');\n");
	htmlFile.WriteString("				document.write(thisText + \"<br>\");\n");
	htmlFile.WriteString("			});\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	function LoadData()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		var fileName = document.getElementById(\"FileName\").innerHTML;\n");
	htmlFile.WriteString("		$.ajax({\n");
	htmlFile.WriteString("		type: \"POST\",\n");
	htmlFile.WriteString("			  url: \"GetTranslationData.php\",\n");
	htmlFile.WriteString("				data: { inTBLFileName: fileName},\n");
	htmlFile.WriteString("				success: function(result)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			var json = $.parseJSON(result);\n");
	htmlFile.WriteString("			var i;\n");
	htmlFile.WriteString("			for (i = 0; i < json.length; i++)\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				var jsonEntry = json[i];\n");
	htmlFile.WriteString("				var english   = jsonEntry.English.replace(/\\\\/g, \'\');\n");
	htmlFile.WriteString("				var divId     = \"#\" + jsonEntry.DivId;\n");
	htmlFile.WriteString("					var trId      = \"tr_\" + jsonEntry.DivId;\n");
	htmlFile.WriteString("					if( english != \"Untranslated\" && english != \"<div>Untranslated</div>\")\n");
	htmlFile.WriteString("					{\n");
	htmlFile.WriteString("						if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
	htmlFile.WriteString("						{\n");
	htmlFile.WriteString("						       document.getElementById(trId).bgColor = \"#e3fec8\";\n");
	htmlFile.WriteString("						}\n");
	htmlFile.WriteString("						$(divId).html(english);\n");
	htmlFile.WriteString("					}\n");
	htmlFile.WriteString("			}\n");
	htmlFile.WriteString("		},\n");
	htmlFile.WriteString("		error: function()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			alert('Unable to load data');\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	function FixOnChangeEditableElements()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		var tags = document.querySelectorAll('[contenteditable=true][onChange]');\n");
	htmlFile.WriteString("		for (var i=tags.length-1; i>=0; i--) if (typeof(tags[i].onblur)!='function')\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			tags[i].onfocus = function()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				this.data_orig=this.innerHTML;\n");
	htmlFile.WriteString("			};\n");
	htmlFile.WriteString("			tags[i].onblur = function()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				if( this.innerHTML != this.data_orig)\n");
	htmlFile.WriteString("					this.onchange();\n");
	htmlFile.WriteString("				delete this.data_orig;\n");
	htmlFile.WriteString("			};\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	function OnStartup()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		FixOnChangeEditableElements();\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	</script>\n");
	htmlFile.WriteString("	</head>\n");
	htmlFile.WriteString("	\n");
	htmlFile.WriteString("	<body>\n");
	htmlFile.WriteString("	<?php include 'GetUserPermissions.php';\n");
	htmlFile.WriteString("	$bPermissionFound = false;\n");
	htmlFile.WriteString("	foreach ($allowedFiles as $value)\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		if( $value == \"PBOOK_EC\" )\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			$bPermissionFound = true;\n");
	htmlFile.WriteString("			break;\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	if( $bPermissionFound || $masterUnlock )\n\t{\n?>");
	htmlFile.WriteString("		<article><header align=\"center\"><h1>File: PBOOK_EC</h1></header></article>\n");
	htmlFile.WriteString("			<br>\n");
	htmlFile.WriteString("			<b>Instructions:</b><br>\n");
	htmlFile.WriteString("			-This page is best displayed using Chrome.  Otherwise some of the table borders are missing for some reason.<br>\n");
	htmlFile.WriteString("			-Your changes are automatically saved.<br>\n");
	htmlFile.WriteString("			-Press the Load Data button when you come back to the page to load your changes.<br>\n");
	htmlFile.WriteString("			<b>Naming Conventions:</n><br>\n");
	htmlFile.WriteString("			<a href=\"https://docs.google.com/spreadsheets/d/1imZZn_SfbmMxBpEnyj8_oZ1BEnMN0q0Ck1XYQgKggm4/edit?usp=sharing\" target=\"_blank\">Click here to view the naming conventions for Characters, Locations, and Terms</a> <br>\n");
	htmlFile.WriteString("			<?php\n");
	htmlFile.WriteString("				$currUser = $_SERVER['PHP_AUTH_USER'];\n");
	htmlFile.WriteString("				if( $currUser == \"rahmed\" )\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					echo \"<input align=\\\"center\\\" type=\\\"button\\\" value=\\\"Export Data\\\" onclick=\\\"ExportData()\\\"/>\";\n");
	htmlFile.WriteString("				}\n");
	htmlFile.WriteString("			?>\n\n");
	htmlFile.WriteString("			<table align=\"center\">\n");
	htmlFile.WriteString("				<tr>\n");
	htmlFile.WriteString("					<td>\n");
	htmlFile.WriteString("						<input align=\"center\" type=\"button\" value=\"Load Data\" onclick=\"LoadData()\"/>\n");
	htmlFile.WriteString("					</td>\n");
	htmlFile.WriteString("				</tr>\n");
	htmlFile.WriteString("			</table><br>\n\n");
	htmlFile.WriteString("			<table>\n");
	htmlFile.WriteString("				<tr bgcolor=\"#c8c8fe\">\n");
	htmlFile.WriteString("					<th>#</th>\n");
	htmlFile.WriteString("					<th>Japanese</th>\n");
	htmlFile.WriteString("					<th>English</th>\n");
	htmlFile.WriteString("				</tr>\n");

	vector<FileNameContainer> fileNames;
	FindAllFilesWithinDirectory(imageDirectory, fileNames);

	std::sort(fileNames.begin(), fileNames.end(), [](const FileNameContainer& a, const FileNameContainer& b)
		{
			return std::atoi(a.mNoExtension.c_str()) < std::atoi(b.mNoExtension.c_str());
		});

	int i = 0;
	for (const FileNameContainer& fileName : fileNames)
	{
		const char* pImageNumber = fileName.mNoExtension.c_str();

		fprintf(htmlFile.GetFileHandle(), "				<tr id=\"tr_edit_%i\" bgcolor=\"#fefec8\">\n", i);
		fprintf(htmlFile.GetFileHandle(), "					<td align=\"center\" width=\"20\">%s</td><td width=\"128\" height=\"16\" align=\"center\"><img src=\"..\\ExtractedData\\SakuraWars2\\Disc1\\PBOOK_EC\\%s.png\" class=\"rotate90\" ></td><td width=\"480\"><textarea id=\"edit_%i\" contenteditable=true onchange=\"SaveEdits('%i.bmp', 'edit_%i')\" style=\"border: none; width: 100%%; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;\">Untranslated</textarea></td>\n", pImageNumber, pImageNumber, i, i, i);
		fprintf(htmlFile.GetFileHandle(), "				</tr>\n");

		++i;
	}

	htmlFile.WriteString("			</table><br>\n");
	htmlFile.WriteString("			<?php\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	?></body>\n");
	htmlFile.WriteString("	</html>\n");

	return true;

}
