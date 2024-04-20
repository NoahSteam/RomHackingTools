#pragma once

struct StatusScreenImageInfo
{
	int width;
	int height;
	bool b4Bit;
};

const StatusScreenImageInfo StatusScreenImages[] = 
{
	152,64, false,
	24, 48, false,
	48,  8, false,
	48,  8, false,
	72, 48, false,
	24, 48, false,
	24, 48, false,
	24, 48, false,
	24, 48, false,
	24, 48, false,
	24, 48, false,
	24, 48, false,
	48, 16, true,
	32, 16, true,
	48, 16, true,	
	32, 16, true,
	32, 16, true,
	64, 16, true,
	48, 16, true,
	48, 16, true,
	64, 16, true,
	64, 16, true,
	64, 16, true,
	64, 16, true,
	64, 16, true,
	64, 16, true,
	16,  16, true,
	16,  16, true,
	16,  16, true,
	16,  16, true,
	16,  16, true,
	8,  16, true,
	8,  16, true,
	8,  16, true,
	8,  16, true,
	8,  16, true,
	8,  16, true,
	8,  16, true,
	8,  16, true,
	8,  16, true,
	8,  16, true,
	72, 48, false,
	64, 16, true,
	64, 16, true,
	64, 16, true,
	64, 16, true,
	64, 16, true,
	64, 16, true,
};

void ExtractStatusScreen(const std::string& inSakuraDir, const std::string& inOutputDirectory, bool bInBmp)
{
	CreateDirectoryHelper(inOutputDirectory);

	const std::string imgExt = bInBmp ? ".bmp" : ".png";

	const std::string statusFilePath = inSakuraDir + "SINRAISP.BIN";
	FileData statusFile;
	if (!statusFile.InitializeFileData(statusFilePath))
	{
		return;
	}

	const std::string paletteFilePath = inSakuraDir + "SINRAICL.BIN";
	FileData paletteFile;
	if (!paletteFile.InitializeFileData(paletteFilePath))
	{
		return;
	}

	PRSDecompressor decompressed;
	decompressed.UncompressData((void*)(statusFile.GetData()), statusFile.GetDataSize());

	int currOffset = 0;
	const int numImages = sizeof(StatusScreenImages)/sizeof(StatusScreenImages[0]);
	for(int i = 0; i < numImages; ++i)
	{
		const bool b4Bit = StatusScreenImages[i].b4Bit;
		const int width = StatusScreenImages[i].width;
		const int height = StatusScreenImages[i].height;
		const int imgSize = b4Bit ? (width*height)/2 : width*height;
		const char* pPalette = b4Bit ? paletteFile.GetData() + 0x600 : paletteFile.GetData() + 0x200;
		const int paletteSize = b4Bit ? 32 : 512;

		const string bitmapFileName = inOutputDirectory + std::to_string(i) + imgExt;
		ExtractImageFromData(decompressed.mpUncompressedData + currOffset, imgSize, bitmapFileName, pPalette, paletteSize, b4Bit, width, height, 1, 256, 0, true, bInBmp);

		currOffset += imgSize;
	}
}