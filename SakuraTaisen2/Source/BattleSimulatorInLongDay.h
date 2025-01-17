#pragma once

struct BattleSimImageInfo
{
	int offset;
	uint8 width;
	uint8 height;
	bool b4Bit;
};

const BattleSimImageInfo images[] =
{
	0x1f0a0, 40, 16, true,
	0x1f0a0, 40, 16, true,
	0x1f0a0, 40, 16, true,
	0x1f0a0, 40, 16, true,
	0x1f0a0, 40, 20, true,
	0x1f740, 40, 20, true,

	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,
	0x1f960, 64, 22, true,

	0x23760, 168, 22, true,

	0x23ea0, 48, 72, true,
	0x23ea0, 168, 22, true,

	0x24ca0, 48, 72, true,
	0x24ca0, 168, 22, true,

	0x25aa0, 48, 72, true,

	0x1ec20, 24, 16, false,
	0x1ec20, 24, 16, false,
	0x1ec20, 24, 16, false,
};

void ExtractBattleSimVDP2(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	std::string outputDirectory = inOutputDirectory + "VDP2\\";
	CreateDirectoryHelper(outputDirectory);
	if (!bInBmp)
	{
		outputDirectory += "PNG\\";

		CreateDirectoryHelper(outputDirectory);
	}

	const string vdp2Path = inRootDirectory + string("SAKURA2\\M92VDP2.BIN");
	const string colPath = inRootDirectory + string("SAKURA2\\M92COL.BIN");

	const string ext = bInBmp ? ".bmp" : ".png";
	ExtractTiledFullScreenImage<uint32>(vdp2Path, colPath, 0x2e040, 0x40,    0, outputDirectory + std::string("1") + ext, bInBmp);
	ExtractTiledFullScreenImage<uint16>(vdp2Path, colPath, 0x2f1c0, 0x20040, 0, outputDirectory + std::string("2") + ext, bInBmp);
}

void ExtractBattleSimulator(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	std::string outputDirectory(inOutputDirectory);	
	CreateDirectoryHelper(outputDirectory);
	if(!bInBmp)
	{
		outputDirectory += "PNG\\";
		CreateDirectoryHelper(outputDirectory);
	}

	const string sakuraFilePath = inRootDirectory + string("SAKURA2\\M92VDP1.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());
	FileData sakuraFileData;
	if (!sakuraFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return;
	}

	FileData paletteFileData;
	if (!paletteFileData.InitializeFileData(inRootDirectory + "SAKURA2\\M92COL.BIN"))
	{
		return;
	}

	const string ext = bInBmp ? ".bmp" : ".png";
	int offset = images[0].offset;
	int prevOffset = offset;
	const int numImages = sizeof(images) / sizeof(images[0]);

	for (int i = 0; i < numImages; ++i)
	{
		if (images[i].offset != prevOffset)
		{
			offset = images[i].offset;
			prevOffset = offset;
		}

		const bool b4bit = images[i].b4Bit;
		const int paletteSize = b4bit ? 32 : 512;
		const int imageWidth = images[i].width;
		const int imageHeight = images[i].height;
		const int imageSize = b4bit ? imageWidth * imageHeight / 2 : imageWidth * imageHeight;
		const int paletteOffset = i < 6 ? 0xe00 : b4bit ? 0xe80 : 0;

		const string bitmapFileName = outputDirectory + std::to_string(i) + ext;
		ExtractImageFromData(sakuraFileData.GetData() + offset, imageSize, bitmapFileName, paletteFileData.GetData() + paletteOffset, paletteSize, b4bit, imageWidth, imageHeight, 1, 256, 0, true, bInBmp);

		offset += imageSize;
	}

	if (!bInBmp)
	{
		CreateSpreadSheetForImages("BattleSim", "BattleSim\\PNG\\", outputDirectory, "BattleSim.php");
	}

	ExtractBattleSimVDP2(inRootDirectory, outputDirectory, bInBmp);
}

bool CreateTranslatedBattleResultImages(const string& InTranslationDirectory, const string& InFontSheetWidePath, const string& InFontSheetNarrowPath)
{
	const string originalImageDirectory = InTranslationDirectory + "BattleSimulator\\VDP2\\ButtonImages\\";

	TranslatedImageCreator imageCreator;
	if (!imageCreator.InitializeCreator(originalImageDirectory, InFontSheetWidePath, InFontSheetNarrowPath))
	{
		return false;
	}

	return imageCreator.CreateImages(InTranslationDirectory + "Translation\\BattleSimulatorResultsButtons.txt", 6);
}

bool CreateTranslatedBattleSimulatorImages(const string& InTranslationDirectory, const string& InFontSheetWidePath, const string& InFontSheetNarrowPath)
{
	const string originalImageDirectory = InTranslationDirectory + "BattleSimulator\\";
	
	TranslatedImageCreator imageCreator;
	if(!imageCreator.InitializeCreator(originalImageDirectory, InFontSheetWidePath, InFontSheetNarrowPath))
	{
		return false;
	}

	imageCreator.SwapFontSheetColors(1, 15);
	imageCreator.SwapFontSheetColors(7, 1);
	imageCreator.SwapFontSheetColors(12, 5);
	imageCreator.SwapFontSheetColors(8, 13);

	if(!imageCreator.CreateImages(InTranslationDirectory + "Translation\\BattleSimulator.txt", 6))
	{
		return false;
	}

	return CreateTranslatedBattleResultImages(InTranslationDirectory, InFontSheetWidePath, InFontSheetNarrowPath);
}

bool PatchBattleSimulator(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	printf("Patching Battle Simulator\n");

	const string translatedDir = inTranslatedDataDirectory + "BattleSimulator\\";

	//Output file
	const std::string vdp1FilePath = inPatchedSakuraDirectory + string("SAKURA2\\M92VDP1.BIN");
	FileReadWriter outFileWriter;
	if (!outFileWriter.OpenFile(vdp1FilePath))
	{
		return false;
	}

	const string ext(".bmp");
	int offset = images[0].offset;
	int prevOffset = offset;
	const int numImages = sizeof(images) / sizeof(images[0]);

	for (int i = 0; i < numImages; ++i)
	{
		if (images[i].offset != prevOffset)
		{
			offset = images[i].offset;
			prevOffset = offset;
		}

		const int imageWidth = images[i].width;
		const int imageHeight = images[i].height;
		const int imageSize = imageWidth * imageHeight / 2;
		
		const string translatedImage = translatedDir + std::to_string(i) + ext;

		//Convert image to sw2 format
		BmpToSaturnConverter patchedImageData;
		if (!patchedImageData.ConvertBmpToSakuraFormat(translatedImage, false, BmpToSaturnConverter::CYAN))
		{
			return false;
		}

		if (patchedImageData.GetImageWidth() != imageWidth || patchedImageData.GetImageHeight() != imageHeight)
		{
			printf("%s has invalid dimensions.  Expected %ix%i, got %ix%i.\n", translatedImage.c_str(), imageWidth, imageHeight, patchedImageData.GetImageWidth(), patchedImageData.GetImageHeight());
			return false;
		}

		outFileWriter.WriteData(offset, patchedImageData.GetImageData(), patchedImageData.GetImageDataSize(), false);

		offset += imageSize;
	}

	if(!PatchTiledImage<uint32>(translatedDir + "VDP2\\1.bmp", inPatchedSakuraDirectory + string("SAKURA2\\M92VDP2.BIN"), 0x2e000, 0x40, 0x2e040))
	{
		return false;
	}

	return PatchTiledImage<uint16>(translatedDir + "VDP2\\2.bmp", inPatchedSakuraDirectory + string("SAKURA2\\M92VDP2.BIN"), (uint32)0x2f1c0 - (uint32)0x20040, 0x20040, 0x2f1c0);
}