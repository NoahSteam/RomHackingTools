#pragma once

void ExtractBattleAnimViewer(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	CreateDirectoryHelper(inOutputDirectory);

	const string sakuraFilePath = inRootDirectory + string("SAKURA2\\M91VDP1.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());
	FileData sakuraFileData;
	if (!sakuraFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return;
	}

	FileData paletteFileData;
	if(!paletteFileData.InitializeFileData(inRootDirectory + "SAKURA2\\M91COL.BIN"))
	{
		return;
	}

	const string ext = bInBmp ? ".bmp" : ".png";
	const int imageWidth = 88;
	const int imageHeight = 14;
	const int imageSize = imageWidth*imageHeight/2;
	int offset = 0xa0e0;
	const int numImages = 65;
	for(int i = 0; i < numImages; ++i)
	{
		const string bitmapFileName = inOutputDirectory + std::to_string(i) + ext;
		ExtractImageFromData(sakuraFileData.GetData() + offset, imageSize, bitmapFileName, paletteFileData.GetData() + 0xe60, 32, true, imageWidth, imageHeight, 1, 256, 0, true, bInBmp);

		offset += 0x50*8;
	}

	if(!bInBmp)
	{
		CreateSpreadSheetForImages("BattleAnimViewer", "BattleAnimViewer\\PNG\\", inOutputDirectory, "BattleAnimViewer.php");
	}
}

bool CreateTranslatedBattleAnimViewerImages(const string& InTranslationDirectory, const string& InFontSheetWidePath, const string& InFontSheetNarrowPath)
{
	const string originalImageDirectory = InTranslationDirectory + "BattleAnimViewer\\";
	const string translatedImageDirectory = InTranslationDirectory + "BattleAnimViewer\\Translated\\";
	CreateDirectoryHelper(translatedImageDirectory);

	//Create font sheets
	Tiled8BitFontSheet fontSheetWide;
	if (!fontSheetWide.CreateFontSheet(InFontSheetWidePath, originalImageDirectory, ' '))
	{
		return false;
	}

	Tiled8BitFontSheet fontSheetNarrow;
	if (!fontSheetNarrow.CreateFontSheet(InFontSheetNarrowPath, originalImageDirectory, ' '))
	{
		return false;
	}
	
	fontSheetWide.SwapColors(1, 15);
	fontSheetWide.SwapColors(7, 1);
	fontSheetWide.SwapColors(12, 5);
	fontSheetWide.SwapColors(8, 13);
	fontSheetNarrow.SwapColors(1, 15);
	fontSheetNarrow.SwapColors(7, 1);
	fontSheetNarrow.SwapColors(12, 5);
	fontSheetNarrow.SwapColors(8, 13);

	//Create translation directory
	CreateDirectoryHelper(translatedImageDirectory);

	//Duplicate original images
	CopyFiles(originalImageDirectory, translatedImageDirectory);

	//Remove text from original images
	unordered_set<char> colorsToIgnore;
	colorsToIgnore.insert(0);
	ReplaceColors(translatedImageDirectory, 15, colorsToIgnore);

	//Find all created translated images
	vector<FileNameContainer> translatedImages;
	FindAllFilesWithinDirectory(translatedImageDirectory, translatedImages);

	// Sort the file names alphabetically
	std::sort(translatedImages.begin(), translatedImages.end(), [](const FileNameContainer& a, const FileNameContainer& b)
		{
			return std::atoi(a.mNoExtension.c_str()) < std::atoi(b.mNoExtension.c_str());
		});

	//Text data
	FileNameContainer textFileName(InTranslationDirectory + "Translation\\BattleAnimViewer.txt");
	TextFileData textFile(textFileName);
	if (!textFile.InitializeTextFile(true, true))
	{
		return false;
	}

	if (textFile.mLines.size() != 65)
	{
		printf("Not enough translated lines of text in StatusScreen.txt.  Need 65, one for each image.\n");
		return false;
	}

	const bool bCenter = true;

	//Insert text into images
	const int numImages = (int)translatedImages.size();
	int textIndex = 0;
	for (int i = 0; i < numImages; ++i)
	{
		int leftOffset = 0;
		int rightOffset = 0;
		bool bForceNarrow = false;

		std::string text = textFile.mLines[textIndex].mFullLine;
		if(text.c_str()[0] == '*')
		{
			text = text.c_str() + 1;
			bForceNarrow = true;
		}

		//See if wide font sheet will fit
		if (bForceNarrow || !WriteTextIntoImageUsingFontSheet(text, translatedImages[i], fontSheetWide, true, leftOffset, rightOffset, 0, bCenter))
		{
			//Otherwise try the narrow one
			WriteTextIntoImageUsingFontSheet(text, translatedImages[i], fontSheetNarrow, false, leftOffset, rightOffset, 0, bCenter);
		}

		++textIndex;
	}

	printf("Finished Successfully");

	return true;
}