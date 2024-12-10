#pragma once

struct StatusScreenImageInfo
{
	int width;
	int height;
	bool b4Bit;
	int paletteOffset;
};

const StatusScreenImageInfo StatusScreenImages[] = 
{
	152,64, false, 0x800,
	24, 48, false, 0x200,
	48,  8, false, 0x200,
	48,  8, false, 0x200,
	72, 48, false, 0x200,
	24, 48, false, 0x200,
	24, 48, false, 0x200,
	24, 48, false, 0x200,
	24, 48, false, 0x200,
	24, 48, false, 0x200,
	24, 48, false, 0x200,
	24, 48, false, 0x200,
	48, 16, true, 0x600,
	32, 16, true, 0x600,
	48, 16, true, 0x600,	
	32, 16, true, 0x600,
	32, 16, true, 0x600,
	64, 16, true, 0x600,
	48, 16, true, 0x600,
	48, 16, true, 0x600,
	64, 16, true, 0x600,
	64, 16, true, 0x600,
	64, 16, true, 0x600,
	64, 16, true, 0x600,
	64, 16, true, 0x600,
	64, 16, true, 0x600,
	16, 16, true, 0x600,
	16, 16, true, 0x600,
	16, 16, true, 0x600,
	16, 16, true, 0x600,
	16, 16, true, 0x600,
	8,  16, true, 0x600,
	8,  16, true, 0x600,
	8,  16, true, 0x600,
	8,  16, true, 0x600,
	8,  16, true, 0x600,
	8,  16, true, 0x600,
	8,  16, true, 0x600,
	8,  16, true, 0x600,
	8,  16, true, 0x600,
	8,  16, true, 0x600,
	72, 48, false,0x200,
	64, 16, true, 0x600,
	64, 16, true, 0x600,
	64, 16, true, 0x600,
	64, 16, true, 0x600,
	64, 16, true, 0x600,
	64, 16, true, 0x600,
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
		const char* pPalette = paletteFile.GetData() + StatusScreenImages[i].paletteOffset;
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
	FileReadWriter sinraispWriter;
	if (!sinraispWriter.OpenFile(statusFilePath))
	{
		return false;
	}
		
	const std::string statusCodeFilePath = inPatchedSakuraDirectory + "SAKURA1\\SINRAIP.BIN";
	FileReadWriter sinraipWriter;
	if (!sinraipWriter.OpenFile(statusCodeFilePath))
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

	sinraispWriter.WriteData(0, compressor.mpCompressedData, compressor.mCompressedSize, false);
	
	//Give two extra pixels of spacing along the X axis for drawing the names so they aren't clipped by the character portrait
	const uint8 nameSpacing = 0x4c;
	sinraipWriter.WriteData(0x1ebb, (char*)&nameSpacing, sizeof(nameSpacing), false);

	return true;
}

bool CreateTranslatedStatusScreenImages(const string& InTranslationDirectory, const string& InFontSheetWidePath, const string& InFontSheetNarrowPath)
{
	const string originalImageDirectory = InTranslationDirectory + "StatusScreen\\";
	const string translatedImageDirectory = InTranslationDirectory + "StatusScreen\\Translated\\";

	//Create font sheets
	Tiled8BitFontSheet fontSheetWide;
	if (!fontSheetWide.CreateFontSheet(InFontSheetWidePath, translatedImageDirectory, ' '))
	{
		return false;
	}

	Tiled8BitFontSheet fontSheetNarrow;
	if (!fontSheetNarrow.CreateFontSheet(InFontSheetNarrowPath, translatedImageDirectory, ' '))
	{
		return false;
	}

	fontSheetWide.SwapColors(1, 0);
	fontSheetWide.SwapColors(7, 15);
	fontSheetWide.SwapColors(8, 4);
	fontSheetWide.SwapColors(12, 2);
	fontSheetNarrow.SwapColors(1, 0);
	fontSheetNarrow.SwapColors(7, 15);
	fontSheetNarrow.SwapColors(8, 4);
	fontSheetNarrow.SwapColors(12, 2);

	//Create translation directory
	CreateDirectoryHelper(translatedImageDirectory);

	//Duplicate original images
	CopyFiles(originalImageDirectory, translatedImageDirectory);

	//Remove text from original images
	unordered_set<char> colorsToIgnore;
	colorsToIgnore.insert(0);
	ReplaceColors(translatedImageDirectory, 0, colorsToIgnore);

	//Find all created translated images
	vector<FileNameContainer> translatedImages;
	FindAllFilesWithinDirectory(translatedImageDirectory, translatedImages);

	// Sort the file names alphabetically
	std::sort(translatedImages.begin(), translatedImages.end(), [](const FileNameContainer& a, const FileNameContainer& b)
		{
			return std::atoi(a.mNoExtension.c_str()) < std::atoi(b.mNoExtension.c_str());
		});

	//Text data
	FileNameContainer textFileName(InTranslationDirectory + "Translation\\StatusScreen.txt");
	TextFileData textFile(textFileName);
	if (!textFile.InitializeTextFile(true, true))
	{
		return false;
	}

	if (textFile.mLines.size() != 35)
	{
		printf("Not enough translated lines of text in StatusScreen.txt.  Need 35, one for each image.\n");
		return false;
	}

	//Insert text into images
	const int numImages = (int)translatedImages.size();
	int textIndex = 0;
	for (int i = 12; i < numImages; ++i)
	{
		if(i == 41)
		{
			continue;
		}

		std::string text = textFile.mLines[textIndex].mFullLine;
		std::replace(text.begin(), text.end(), '^', ' ');

		//center the + and - signs
		const bool bCenter = i >= 29 && i <= 30;

		int leftOffset = 1;
		int rightOffset = 0;

		//See if wide font sheet will fit
		if (!WriteTextIntoImageUsingFontSheet(text, translatedImages[i], fontSheetWide, true, leftOffset, rightOffset, bCenter, 0))
		{
			//Otherwise try the narrow one
			WriteTextIntoImageUsingFontSheet(text, translatedImages[i], fontSheetNarrow, false, leftOffset, rightOffset, bCenter, 0);
		}

		++textIndex;
	}

	printf("Finished Successfully");

	return true;
}