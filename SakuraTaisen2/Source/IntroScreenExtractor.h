//Static logo
struct TTL2ImageData
{
	uint8 widthDiv8;
	uint8 height;
	uint16 unknown1;
	uint16 offsetToNextDiv8;
	uint16 unknown2;

	void SwapEndianess()
	{
		offsetToNextDiv8 = SwapByteOrder(offsetToNextDiv8);
	}
};

//For animated logo
#pragma pack(push, 1)
struct TitleImageData
{
	uint16 offsetToNextDiv8;
	uint16 unknown1;
	uint8 widthDiv8;
	uint8 height;
	uint16 unknown2;

	void SwapEndianess()
	{
		offsetToNextDiv8 = SwapByteOrder(offsetToNextDiv8);
	}
};
#pragma pack(pop)
/*static const TTL2ImageData TTL2ImageTable[] =
{
	{192,64},
	{256,24},
	{80,88},
	{40,8},
	{8, 8},
	{8, 32},
	{208, 16},
	{96, 8},
	{160, 16}
};*/

void ExtractTTL2(const string& InSakuraRootDir, const string& OutDirectory)
{
	const string tt2lOutDirectory = OutDirectory + "TTL2\\";
	CreateDirectoryHelper(tt2lOutDirectory);

	const FileNameContainer ttl2cbgFileName((InSakuraRootDir + string("SAKURA1\\TTL2CGB.BIN")));
	FileData ttl2cgbData;
	if (!ttl2cgbData.InitializeFileData(ttl2cbgFileName))
	{
		return;
	}

	PRSDecompressor decompressor;
	if (!decompressor.UncompressData((void*)(ttl2cgbData.GetData() + 0x400), ttl2cgbData.GetDataSize() - 0x400))
	{
		return;
	}

	const FileNameContainer titleFileName((InSakuraRootDir + string("SAKURA1\\TITLE.BIN")));
	FileData titleFileData;
	if (!titleFileData.InitializeFileData(titleFileName))
	{
		return;
	}

	const int numImages = 10;
	TTL2ImageData ttl2ImageTable[numImages];
	titleFileData.ReadData(0xdee0, (char*)ttl2ImageTable, sizeof(ttl2ImageTable), false);
	for(int i = 0; i < numImages; ++i)
	{
		ttl2ImageTable[i].SwapEndianess();
	}

	int imageOffset = 0;
	for (int i = 0; i < numImages; ++i)
	{
		const string outputFile = tt2lOutDirectory + string("image_") + std::to_string(i) + string(".bmp");
		const char* pImageData = decompressor.mpUncompressedData;
		const int width = ttl2ImageTable[i].widthDiv8*8;

		ExtractImageFromData(pImageData + imageOffset, width * ttl2ImageTable[i].height, outputFile,
			ttl2cgbData.GetData() + 0x200,
			512, false, width, ttl2ImageTable[i].height,
			1, 256, 0, true, true);

		imageOffset = ttl2ImageTable[i].offsetToNextDiv8 * 8;//width * TTL2ImageTable[i].height;
	}
}

void ExtractTitle(const string& InSakuraRootDir, const string& OutDirectory)
{
#define USE_DEMO_VERSION 0

#if USE_DEMO_VERSION
	const int offsetToImageTable = 0xac54;
	const int offsetToCompressedImages = 0x15c54;
	const int offsetToPalette1 = 0x10f98;
	const int offsetToPalette2 = 0x11198;
#else
	const int offsetToImageTable = 0xc538;
	const int offsetToCompressedImages = 0x18158;
	const int offsetToPalette1 = 0x1349c;
	const int offsetToPalette2 = 0x1369c;
#endif

	const string tt2lOutDirectory = OutDirectory + "TITLE\\";
	CreateDirectoryHelper(tt2lOutDirectory);

	const FileNameContainer ttl2cbgFileName((InSakuraRootDir + string("SAKURA1\\TITLE.BIN")));
	FileData titleFileData;
	if (!titleFileData.InitializeFileData(ttl2cbgFileName))
	{
		return;
	}

	//Read image table
	const int numImages = 62;
	TitleImageData imageTable[numImages];
	titleFileData.ReadData(offsetToImageTable, (char*)imageTable, sizeof(imageTable), false);
	for(int i = 0; i < numImages; ++i)
	{
		imageTable[i].SwapEndianess();
	}

	//Uncompress image data
	PRSDecompressor decompressor;
	if (!decompressor.UncompressData((void*)(titleFileData.GetData() + offsetToCompressedImages), titleFileData.GetDataSize() - offsetToCompressedImages))
	{
		return;
	}

	const char* pImageData = decompressor.mpUncompressedData;
	for (int i = 0; i < numImages; ++i)
	{
		const string outputFile = tt2lOutDirectory + string("image_") + std::to_string(i) + string(".bmp");
	//	const string outputFile = tt2lOutDirectory + string("image_") + IntToHexString(imageTable[i].offsetToNextDiv8 * 8) + string(".bmp");
		const int width = imageTable[i].widthDiv8 * 8;
		const int height = imageTable[i].height;
		const int paletteOffset = i < 30 ? offsetToPalette1 : offsetToPalette2;
		const bool bIs4Bit = (i >= 0 && i <= 12) || i >= 60;
		const int dataSize = bIs4Bit ? (width * height) >> 1 : width * height;

		ExtractImageFromData(pImageData + imageTable[i].offsetToNextDiv8*8, dataSize, outputFile,
			titleFileData.GetData() + paletteOffset,
			bIs4Bit ? 32 : 512, bIs4Bit, width, height,
			1, 256, 0, true, true);

		//imageOffset += images[i].width * images[i].height;
	}

	#if 0
	struct ImageData
	{
		int width;
		int height;
		int offset;
		int paletteOffset;
	};

	ImageData images[] =
	{
		{32, 16, 0x6700, 0x1349c}, //0
		{104,24, 0x6700, 0x1349c}, //1
		{24, 40, 0x6700, 0x1349c}, //2
		{56, 24, 0x6700, 0x1349c}, //3
		{24, 16, 0x6700, 0x1349c}, //4
		{64, 16, 0x6700, 0x1349c}, //5
		{64, 32, 0x6700, 0x1349c}, //6
		{56, 16, 0x6700, 0x1349c}, //7
		{48, 32, 0x6700, 0x1349c}, //8

		{32, 16, 0x10340, 0x1369c}, //9
		{104,24, 0x10340, 0x1369c}, //10
		{24, 40, 0x10340, 0x1369c}, //11
		{56, 24, 0x10340, 0x1369c}, //12
		{72, 64, 0x10340, 0x1369c}, //13
		{72, 64, 0x10340, 0x1369c}, //14
		{72, 64, 0x10340, 0x1369c}, //15
		{64, 56, 0x10340, 0x1369c}, //14
		{64, 56, 0x10340, 0x1369c}, //14
		{64, 56, 0x10340, 0x1369c}, //14
		{40, 64, 0x10340, 0x1369c}, //15
		{48, 32, 0x10340, 0x1369c}, //16

		{32, 48,  0x92c0, 0x1369c}, //17
		{64, 24,  0x92c0, 0x1369c}, //18
		{72, 32,  0x92c0, 0x1369c}, //19
		{16, 8,   0x92c0, 0x1369c}, //20
		{104,32,  0x92c0, 0x1369c}, //21
		{88, 32,  0x92c0, 0x1369c}, //22
		{24, 24,  0x92c0, 0x1369c}, //23
		{56, 32,  0x92c0, 0x1369c}, //24
		{32, 40,  0x92c0, 0x1369c}, //25
		{48, 48,  0x92c0, 0x1369c}, //26
		{112,32,  0x92c0, 0x1369c}, //27
		{8,  8,   0x92c0, 0x1369c}, //28
		{24, 32,  0x92c0, 0x1369c}, //29
		{48, 16,  0x92c0, 0x1369c}, //30
		{32, 16,  0x92c0, 0x1369c}, //31
		{32, 16,  0x92c0, 0x1369c}, //32
		{112,24,  0x92c0, 0x1369c}, //33
		{24, 40,  0x92c0, 0x1369c}, //34
		{56, 24,  0x92c0, 0x1369c}, //35
	};

	int imageOffset = 0x6700;
	const int numImages = sizeof(images) / sizeof(ImageData);
	for (int i = 0; i < numImages; ++i)
	{
		const string outputFile = tt2lOutDirectory + string("image_") + std::to_string(i) + string(".bmp");
		const char* pImageData = decompressor.mpUncompressedData;

		if (i > 0 && images[i - 1].offset != images[i].offset)
		{
			imageOffset = images[i].offset;
		}

		ExtractImageFromData(pImageData + imageOffset, images[i].width * images[i].height, outputFile,
			titleFileData.GetData() + images[i].paletteOffset,
			512, false, images[i].width, images[i].height,
			1, 256, 0, true, true);

		imageOffset += images[i].width * images[i].height;
	}
	#endif
}

void ExtractIntrosScreens(const string& InSakuraRootDir, const string& OutDirectory)
{
	CreateDirectoryHelper(OutDirectory);

	ExtractTTL2(InSakuraRootDir, OutDirectory);
	ExtractTitle(InSakuraRootDir, OutDirectory);
}

bool PatchTitleFile(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	const string titlePath = inPatchedSakuraDirectory + string("SAKURA1\\TITLE.BIN");

	FileData titleFileData;
	if (!titleFileData.InitializeFileData(titlePath))
	{
		return false;
	}

	FileReadWriter titleFile;
	if (!titleFile.OpenFile(titlePath))
	{
		return false;
	}

	const int offsetToImageTable = 0xc538;
	const int offsetToCompressedImages = 0x18158;

	//Read image table
	const int numImages = 62;
	TitleImageData imageTable[numImages];
	titleFile.ReadData(offsetToImageTable, (char*)imageTable, sizeof(imageTable), false);
	for (int i = 0; i < numImages; ++i)
	{
		imageTable[i].SwapEndianess();
	}

	//Uncompress image data
	PRSDecompressor decompressor;
	if (!decompressor.UncompressData((void*)(titleFileData.GetData() + offsetToCompressedImages), titleFileData.GetDataSize() - offsetToCompressedImages))
	{
		return false;
	}

	//Create blocks of patched image data
	MemoryBlocks patchedImageBuffer;
	const string ttl2ImageDir = inTranslatedDataDirectory + "IntroImages\\TITLE\\";
	uint32 offset = 0;
	for (int i = 0; i < numImages; ++i)
	{
		const string inputImagePath = ttl2ImageDir + string("image_") + std::to_string(i) + string(".bmp");
		BmpToSaturnConverter patchedImageData;
		if (!patchedImageData.ConvertBmpToSakuraFormat(inputImagePath, false))
		{
			return false;
		}
		patchedImageBuffer.AddBlock(patchedImageData.GetImageData(), 0, patchedImageData.GetImageDataSize());

		imageTable[i].widthDiv8        = patchedImageData.GetImageWidth() / 8;
		imageTable[i].height           = patchedImageData.GetImageHeight();
		imageTable[i].offsetToNextDiv8 = SwapByteOrder<uint16>(offset / 8);
		offset += patchedImageData.GetImageDataSize();
	}		

	//Update image table
	titleFile.WriteData(offsetToImageTable, (char*)imageTable, sizeof(imageTable), false);
	
	//Combine patched data into contiguous block
	patchedImageBuffer.CombineBlocks();

	//Copy over original compressed data and then paste our new images over that to retain any other data that might exist after the images
	char* pNewData = new char[decompressor.mUncompressedDataSize];
	memcpy_s(pNewData, decompressor.mUncompressedDataSize, decompressor.mpUncompressedData, decompressor.mUncompressedDataSize);
	memcpy_s(pNewData, patchedImageBuffer.GetTotalSize(), patchedImageBuffer.GetCombinedData(), patchedImageBuffer.GetTotalSize());

	//Compress it all
	PRSCompressor compressor;
	compressor.CompressData(pNewData, decompressor.mUncompressedDataSize);

	//Clear the buffer now that it's compressed
	delete[] pNewData;

	//Make sure we don't go over our size limit
	if (compressor.mCompressedSize > decompressor.mUncompressedDataSize)
	{
		printf("PatchTitle: Compressed data is too large. Expected: %i, Got: %li\n", decompressor.mUncompressedDataSize, compressor.mCompressedSize);
		return false;
	}

	//Write out compressed image data
	titleFile.WriteData(offsetToCompressedImages, compressor.mpCompressedData, compressor.mCompressedSize, false);

	//Copy formatting data since image sizes changed
	FileData patchedFormattingData;
	if(!patchedFormattingData.InitializeFileData((ttl2ImageDir + "IntroAnimationFormatting.bin")))
	{
		return false;
	}

	titleFile.WriteData(0xc728, patchedFormattingData.GetData(), patchedFormattingData.GetDataSize(), false);

	return true;
}

bool PatchTTL2(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	const string ttl2Path = inPatchedSakuraDirectory + string("SAKURA1\\TTL2CGB.BIN");
	FileData ttl2Data;
	if (!ttl2Data.InitializeFileData(ttl2Path))
	{
		return false;
	}

	FileReadWriter ttl2File;
	if (!ttl2File.OpenFile(ttl2Path))
	{
		return false;
	}

	const string titlePath = inPatchedSakuraDirectory + string("SAKURA1\\TITLE.BIN");
	FileReadWriter titleFile;
	if (!titleFile.OpenFile(titlePath))
	{
		return false;
	}

	const int numImages = 10;
	TTL2ImageData ttl2ImageTable[numImages];
	titleFile.ReadData(0xdee0, (char*)ttl2ImageTable, sizeof(ttl2ImageTable), false);
	for (int i = 0; i < numImages; ++i)
	{
		ttl2ImageTable[i].SwapEndianess();
	}

	PRSDecompressor decompressor;
	if (!decompressor.UncompressData((void*)(ttl2Data.GetData() + 0x400), ttl2Data.GetDataSize() - 0x400))
	{
		return false;
	}
	
	MemoryBlocks patchedImageBuffer;
	const string ttl2ImageDir = inTranslatedDataDirectory + "IntroImages\\TTL2\\";
	uint16 offset = 0;
	for (int i = 0; i < numImages; ++i)
	{
		const string inputImagePath = ttl2ImageDir + string("image_") + std::to_string(i) + string(".bmp");
		BmpToSaturnConverter patchedImageData;
		if (!patchedImageData.ConvertBmpToSakuraFormat(inputImagePath, false))
		{
			return false;
		}
		patchedImageBuffer.AddBlock(patchedImageData.GetImageData(), 0, patchedImageData.GetImageDataSize());

		offset += patchedImageData.GetImageDataSize();
		ttl2ImageTable[i].widthDiv8 = patchedImageData.GetImageWidth()/8;
		ttl2ImageTable[i].height = patchedImageData.GetImageHeight();
		ttl2ImageTable[i].offsetToNextDiv8 = SwapByteOrder<uint16>(offset/8);
	}
	ttl2ImageTable[numImages-1].offsetToNextDiv8 = 0;
	titleFile.WriteData(0xdee0, (char*)ttl2ImageTable, sizeof(ttl2ImageTable), false);

	//Combine patched data into contiguous block
	patchedImageBuffer.CombineBlocks();

	//Technically our images are bigger than the original, but there is a bunch of extra space in the file so 33792 is our actual limit
	PRSCompressor compressor;
	compressor.CompressData(patchedImageBuffer.GetCombinedData(), patchedImageBuffer.GetTotalSize());
	if(compressor.mCompressedSize > 33792)
	{
		printf("PatchTTL2: Compressed data is too large. Expected: %i, Got: %li\n", 33792, compressor.mCompressedSize);
		return false;
	}

	ttl2File.WriteData(0x400, compressor.mpCompressedData, compressor.mCompressedSize, false);

	//The "Sakura Wa" image is a different size than the original, so we need to move it back from 0x18 to 0x10
	const char sakuraImagePosition = 0x10;
	titleFile.WriteData(0xdf31, &sakuraImagePosition, sizeof(sakuraImagePosition), false);

	return true;
}

bool PatchIntroScreens(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	if(!PatchTTL2(inPatchedSakuraDirectory, inTranslatedDataDirectory))
	{
		return false;
	}

	return PatchTitleFile(inPatchedSakuraDirectory, inTranslatedDataDirectory);
}
