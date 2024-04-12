#pragma once

void GetSideOffsetsForInfoNameImage(const FileNameContainer& InImageName, int& OutLeftOffset, int& OutRightOffset)
{
	OutLeftOffset = OutRightOffset = 0;

	BitmapReader bmpData;
	if (!bmpData.ReadBitmap(InImageName.mFullPath))
	{
		printf("Failed to read %s\n", InImageName.mFullPath.c_str());
		return;
	}

	const char* pColorData = bmpData.mBitmapData.mColorData.mpRGBA;

	const int imageHeight = abs(bmpData.mBitmapData.mInfoHeader.mImageHeight);
	for (int y = 0; y < imageHeight; ++y)
	{
		bool bBreak = false;
		for (int x = 0; x < bmpData.mBitmapData.mInfoHeader.mImageWidth / 2; ++x)
		{
			const int currentPixel = y * bmpData.mBitmapData.mInfoHeader.mImageWidth / 2 + x;
			const char colorValue1 = (pColorData[currentPixel] & 0xf0) >> 4;			
			if(colorValue1 != 0)
			{
				bBreak = true;
				break;
			}

			++OutLeftOffset;

			const char colorValue2 = pColorData[currentPixel] & 0x0f;
			if (colorValue2 != 0)
			{
				bBreak = true;
				break;
			}

			++OutLeftOffset;
		} //for x

		if(bBreak)
		{
			break;
		}
	}

	//Right offset
	for (int y = 0; y < imageHeight; ++y)
	{
		bool bBreak = false;
		for (int x = (bmpData.mBitmapData.mInfoHeader.mImageWidth / 2) - 1; x >= 0; --x)
		{
			const int currentPixel = y * bmpData.mBitmapData.mInfoHeader.mImageWidth / 2 + x;
			
			//Right pixel first
			const char colorValue2 = pColorData[currentPixel] & 0x0f;
			if (colorValue2 != 0)
			{
				bBreak = true;
				break;
			}

			++OutRightOffset;

			//Left pixel
			const char colorValue1 = (pColorData[currentPixel] & 0xf0) >> 4;
			if (colorValue1 != 0)
			{
				bBreak = true;
				break;
			}

			++OutRightOffset;
		} //for x

		if (bBreak)
		{
			break;
		}
	}
}

bool CreateTranslatedInfoNameImages(const string& InTranslationDirectory, const string& InFontSheetWidePath, const string& InFontSheetNarrowPath)
{
	const string originalImageDirectory = InTranslationDirectory + "InfoName\\Original\\";
	const string translatedImageDirectory = InTranslationDirectory + "InfoName\\Translated\\";

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

	//Create translation directory
	CreateDirectoryHelper(translatedImageDirectory);

	//Duplicate original images
	CopyFiles(originalImageDirectory, translatedImageDirectory);

	//Remove text from original images
	unordered_set<char> colorsToIgnore;
	colorsToIgnore.insert(0);
	ReplaceColors(translatedImageDirectory, 1, colorsToIgnore);

	//Find all created translated images
	vector<FileNameContainer> translatedImages;
	FindAllFilesWithinDirectory(translatedImageDirectory, translatedImages);

	// Sort the file names alphabetically
	std::sort(translatedImages.begin(), translatedImages.end(), [](const FileNameContainer& a, const FileNameContainer& b) 
	{
		return strcmp(a.mNoExtension.c_str(), b.mNoExtension.c_str()) < 0;
	});

	//Text data
	FileNameContainer textFileName(InTranslationDirectory + "Translation\\InfoName.txt");
	TextFileData textFile(textFileName);
	if (!textFile.InitializeTextFile(true, true))
	{
		return false;
	}

	if (textFile.mLines.size() != translatedImages.size())
	{
		printf("Image and translation mismatch.  Images:%i TranslationLine:%i\n", (int)translatedImages.size(), (int)textFile.mLines.size());
		return false;
	}

	//Insert text into images
	const int numImages = (int)translatedImages.size();
	for(int i = 0; i < numImages; ++i)
	{
		int leftOffset = 0;
		int rightOffset = 0;
		GetSideOffsetsForInfoNameImage(translatedImages[i], leftOffset, rightOffset);

		//See if wide font sheet will fit
		if(!WriteTextIntoImageUsingFontSheet(textFile.mLines[i].mFullLine, translatedImages[i], fontSheetWide, true, leftOffset, rightOffset))
		{
			//Otherwise try the narrow one
			WriteTextIntoImageUsingFontSheet(textFile.mLines[i].mFullLine, translatedImages[i], fontSheetNarrow, false, leftOffset, rightOffset);
		}
	}

	printf("Finished Successfully");

	return true;
}
