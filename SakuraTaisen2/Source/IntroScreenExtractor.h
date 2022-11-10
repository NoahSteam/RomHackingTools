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

	struct ImageData
	{
		int width;
		int height;
	};

	ImageData images[] =
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

	int imageOffset = 0;
	const int numImages = sizeof(images) / sizeof(ImageData);
	for (int i = 0; i < numImages; ++i)
	{
		const string outputFile = tt2lOutDirectory + string("image_") + std::to_string(i) + string(".bmp");
		const char* pImageData = decompressor.mpUncompressedData;

		ExtractImageFromData(pImageData + imageOffset, images[i].width * images[i].height, outputFile,
			ttl2cgbData.GetData() + 0x200,
			512, false, images[i].width, images[i].height,
			1, 256, 0, true, true);

		imageOffset += images[i].width * images[i].height;
	}
}

void ExtractTitle(const string& InSakuraRootDir, const string& OutDirectory)
{
	const string tt2lOutDirectory = OutDirectory + "TITLE\\";
	CreateDirectoryHelper(tt2lOutDirectory);

	const FileNameContainer ttl2cbgFileName((InSakuraRootDir + string("SAKURA1\\TITLE.BIN")));
	FileData ttl2cgbData;
	if (!ttl2cgbData.InitializeFileData(ttl2cbgFileName))
	{
		return;
	}

	PRSDecompressor decompressor;
	if (!decompressor.UncompressData((void*)(ttl2cgbData.GetData() + 0x18158), ttl2cgbData.GetDataSize() - 0x18158))
	{
		return;
	}

	struct ImageData
	{
		int width;
		int height;
	};

	ImageData images[] =
	{
		{32, 15},
		{104,24},
		{24, 40},
		{56, 24},
		{24, 16},
		{64, 16},
		{64, 32},
		{56, 16},
		{48, 32}
	};

	int imageOffset = 0x6700;
	const int numImages = sizeof(images) / sizeof(ImageData);
	for (int i = 0; i < numImages; ++i)
	{
		const string outputFile = tt2lOutDirectory + string("image_") + std::to_string(i) + string(".bmp");
		const char* pImageData = decompressor.mpUncompressedData;

		ExtractImageFromData(pImageData + imageOffset, images[i].width * images[i].height, outputFile,
			ttl2cgbData.GetData() + 0x1349c,
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
