#pragma once

void SwapColors(const string& inDirectory, const char inSearchColor, const char inNewColor)
{
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inDirectory, allFiles);

	vector<FileNameContainer> bmpFiles;
	GetAllFilesOfType(allFiles, ".bmp", bmpFiles);

	for (const FileNameContainer& fileName : bmpFiles)
	{
		BitmapReader bmpData;
		if (!bmpData.ReadBitmap(fileName.mFullPath))
		{
			printf("Failed to read %s\n", fileName.mFullPath.c_str());
			return;
		}

		bmpData.SwapColors(inSearchColor, inNewColor);

		const int imageHeight = bmpData.mBitmapData.mInfoHeader.mImageHeight * -1;
		BitmapWriter outBitmap;
		outBitmap.CreateBitmap(fileName.mFullPath, bmpData.mBitmapData.mInfoHeader.mImageWidth, -abs(imageHeight), 4, bmpData.GetColorData(), bmpData.GetColorDataSize(),
			bmpData.mBitmapData.mPaletteData.mpRGBA, bmpData.mBitmapData.mPaletteData.mSizeInBytes, true);
	}

	printf("Success\n");
}

void ReplaceColors(const string& inDirectory, const char inNewColor, unordered_set<char>& inIgnoreColors, std::unordered_set<std::string>* pInIgnoreFiles = nullptr)
{
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inDirectory, allFiles);

	vector<FileNameContainer> bmpFiles;
	GetAllFilesOfType(allFiles, ".bmp", bmpFiles);

	for (const FileNameContainer& fileName : bmpFiles)
	{
		if(pInIgnoreFiles && pInIgnoreFiles->find(fileName.mNoExtension) != pInIgnoreFiles->end())
		{
			continue;
		}

		BitmapReader bmpData;
		if (!bmpData.ReadBitmap(fileName.mFullPath))
		{
			printf("Failed to read %s\n", fileName.mFullPath.c_str());
			return;
		}
		
		bmpData.ReplaceColors(inNewColor, inIgnoreColors);

		const int imageHeight = bmpData.mBitmapData.mInfoHeader.mImageHeight * -1;
		BitmapWriter outBitmap;
		outBitmap.CreateBitmap(fileName.mFullPath, bmpData.mBitmapData.mInfoHeader.mImageWidth, imageHeight, bmpData.mBitmapData.mInfoHeader.mBitCount, bmpData.GetColorData(), bmpData.GetColorDataSize(),
			bmpData.mBitmapData.mPaletteData.mpRGBA, bmpData.mBitmapData.mPaletteData.mSizeInBytes, true);
	}

	printf("Successfully Replaced Colors\n");
}

void SetColorInPalette(const string& inDirectory, const string& inOutDirectory, int inPaletteIndex, const uint32 inNewColor)
{
	CreateDirectoryHelper(inOutDirectory);

	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inDirectory, allFiles);

	vector<FileNameContainer> bmpFiles;
	GetAllFilesOfType(allFiles, ".bmp", bmpFiles);

	for (const FileNameContainer& fileName : bmpFiles)
	{
		BitmapReader bmpData;
		if (!bmpData.ReadBitmap(fileName.mFullPath))
		{
			printf("Failed to read %s\n", fileName.mFullPath.c_str());
			return;
		}

		bmpData.SetPaletteValueAtIndex(inPaletteIndex, inNewColor);

		const int imageHeight = bmpData.mBitmapData.mInfoHeader.mImageHeight * -1;
		BitmapWriter outBitmap;
		outBitmap.CreateBitmap(inOutDirectory + fileName.mFileName, bmpData.mBitmapData.mInfoHeader.mImageWidth, imageHeight, 4, bmpData.GetColorData(), bmpData.GetColorDataSize(),
			bmpData.mBitmapData.mPaletteData.mpRGBA, bmpData.mBitmapData.mPaletteData.mSizeInBytes, true);
	}	
}
