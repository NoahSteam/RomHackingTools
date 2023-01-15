struct TTL2ImageData
{
	int width;
	int height;
};

static const TTL2ImageData TTL2ImageTable[] =
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
};

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

	int imageOffset = 0;
	const int numImages = sizeof(TTL2ImageTable) / sizeof(TTL2ImageData);
	for (int i = 0; i < numImages; ++i)
	{
		const string outputFile = tt2lOutDirectory + string("image_") + std::to_string(i) + string(".bmp");
		const char* pImageData = decompressor.mpUncompressedData;

		ExtractImageFromData(pImageData + imageOffset, TTL2ImageTable[i].width * TTL2ImageTable[i].height, outputFile,
			ttl2cgbData.GetData() + 0x200,
			512, false, TTL2ImageTable[i].width, TTL2ImageTable[i].height,
			1, 256, 0, true, true);

		imageOffset += TTL2ImageTable[i].width * TTL2ImageTable[i].height;
	}
}

void ExtractTitle(const string& InSakuraRootDir, const string& OutDirectory)
{
	const string tt2lOutDirectory = OutDirectory + "TITLE\\";
	CreateDirectoryHelper(tt2lOutDirectory);

	const FileNameContainer ttl2cbgFileName((InSakuraRootDir + string("SAKURA1\\TITLE.BIN")));
	FileData titleFileData;
	if (!titleFileData.InitializeFileData(ttl2cbgFileName))
	{
		return;
	}

	PRSDecompressor decompressor;
	if (!decompressor.UncompressData((void*)(titleFileData.GetData() + 0x18158), titleFileData.GetDataSize() - 0x18158))
	{
		return;
	}

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
}

void ExtractIntrosScreens(const string& InSakuraRootDir, const string& OutDirectory)
{
	CreateDirectoryHelper(OutDirectory);

	ExtractTTL2(InSakuraRootDir, OutDirectory);
	ExtractTitle(InSakuraRootDir, OutDirectory);
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

	const string titleFormattingPath = inTranslatedDataDirectory + string("IntroImages\\TTL2\\DisplayFormatting.bin");
	FileData titleFormattingData;
	if(!titleFormattingData.InitializeFileData(titleFormattingPath))
	{
		return false;
	}

	titleFile.WriteData(0xdee0, titleFormattingData.GetData(), titleFormattingData.GetDataSize(), false);

	PRSDecompressor decompressor;
	if (!decompressor.UncompressData((void*)(ttl2Data.GetData() + 0x400), ttl2Data.GetDataSize() - 0x400))
	{
		return false;
	}
	
	MemoryBlocks patchedImageBuffer;
	const string ttl2ImageDir = inTranslatedDataDirectory + "IntroImages\\TTL2\\";
	const int numImages = sizeof(TTL2ImageTable) / sizeof(TTL2ImageData);
	for (int i = 0; i < numImages; ++i)
	{
		const string inputImagePath = ttl2ImageDir + string("image_") + std::to_string(i) + string(".bmp");
		BmpToSaturnConverter patchedImageData;
		if (!patchedImageData.ConvertBmpToSakuraFormat(inputImagePath, false))
		{
			return false;
		}
		patchedImageData.PackTiles();

		patchedImageBuffer.AddBlock(patchedImageData.mpPackedTiles, 0, patchedImageData.mPackedTileSize);
	}

	//Combine patched data into contiguous block
	patchedImageBuffer.CombineBlocks();

	//Technically our images are bigger than the original, but there is a bunch of extra space in the file so 33792 is our actual limit
	PRSCompressor compressor;
	compressor.CompressData(decompressor.mpUncompressedData, decompressor.mUncompressedDataSize);
	if(compressor.mCompressedSize > 33792)
	{
		printf("PatchTTL2: Compressed data is too large. Expected: %i, Got: %li\n", 33792, compressor.mCompressedSize);
		return false;
	}

	ttl2File.WriteData(0x400, compressor.mpCompressedData, compressor.mCompressedSize, false);

	return true;
}

bool PatchIntroScreens(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	return PatchTTL2(inPatchedSakuraDirectory, inTranslatedDataDirectory);
}
