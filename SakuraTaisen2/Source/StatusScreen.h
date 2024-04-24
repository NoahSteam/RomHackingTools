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

bool PatchStatusScreen(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	printf("Patching Status Screen\n");

	//Output file
	const std::string statusFilePath = inPatchedSakuraDirectory + "SAKURA1\\SINRAISP.BIN";
	FileReadWriter outFileWriter;
	if (!outFileWriter.OpenFile(statusFilePath))
	{
		return false;
	}
	
	MemoryBlocks patchedDataBlock;
	const std::string translatedImageDir = inTranslatedDataDirectory + std::string("StatusScreen\\");
	const int numImages = sizeof(StatusScreenImages) / sizeof(StatusScreenImages[0]);
	for (int i = 0; i < numImages; ++i)
	{
		std::string translatedImage = translatedImageDir + std::to_string(i) + ".bmp";

		//Convert image to sw2 format
		BmpToSaturnConverter patchedImageData;
		if (!patchedImageData.ConvertBmpToSakuraFormat(translatedImage, false, BmpToSaturnConverter::CYAN))
		{
			return false;
		}
		
		if(patchedImageData.GetImageWidth() != StatusScreenImages[i].width || patchedImageData.GetImageHeight() != StatusScreenImages[i].height)
		{
			printf("%s has invalid dimensions.  Expected %ix%i, got %ix%i.\n", translatedImage.c_str(), StatusScreenImages[i].width, StatusScreenImages[i].height, patchedImageData.GetImageWidth(), patchedImageData.GetImageHeight());
			return false;
		}

		patchedDataBlock.AddBlock(patchedImageData.GetImageData(), 0, patchedImageData.GetImageDataSize());
	}

	patchedDataBlock.CombineBlocks();

	//Compress data
	PRSCompressor compressor;
	compressor.CompressData((void*)patchedDataBlock.GetCombinedData(), patchedDataBlock.GetTotalSize(), PRSCompressor::kCompressOption_None);

	if (compressor.mCompressedSize >= 13431)
	{
		printf("%s is too large when compressed.  Should be less that %i, is %i.\n", statusFilePath.c_str(), 13431, compressor.mCompressedSize);
		return false;
	}

	outFileWriter.WriteData(0, compressor.mpCompressedData, compressor.mCompressedSize, false);
	
	return true;
}