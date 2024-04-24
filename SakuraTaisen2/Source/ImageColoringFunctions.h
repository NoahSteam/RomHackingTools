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

		const int numBytes = bmpData.mBitmapData.mColorData.mSizeInBytes;
		char* pModifiedData = new char[numBytes];
		const char* pOriginalData = bmpData.mBitmapData.mColorData.mpRGBA;
		memcpy_s(pModifiedData, numBytes, pOriginalData, numBytes);

		//Flip image
		const int imageHeight = abs(bmpData.mBitmapData.mInfoHeader.mImageHeight);
		/*
		for (int y = 0; y < imageHeight; ++y)
		{
			for (int x = 0; x < bmpData.mBitmapData.mInfoHeader.mImageWidth / 2; ++x)
			{
				const int currentPixel = y * bmpData.mBitmapData.mInfoHeader.mImageWidth / 2 + x;
				const int outPixel = ((imageHeight - 1) - y) * bmpData.mBitmapData.mInfoHeader.mImageWidth / 2 + x;
				pModifiedData[outPixel] = pOriginalData[currentPixel];
			}
		}*/

		//Swap colors
		for (int y = 0; y < imageHeight; ++y)
		{
			for (int x = 0; x < bmpData.mBitmapData.mInfoHeader.mImageWidth / 2; ++x)
			{
				const int currentPixel = y * bmpData.mBitmapData.mInfoHeader.mImageWidth / 2 + x;

				const char colorValue1 = (pModifiedData[currentPixel] & 0xf0) >> 4;
				if (colorValue1 == inSearchColor)
				{
					pModifiedData[currentPixel] = (pModifiedData[currentPixel] & 0x0f) + (inNewColor << 4);

				}//if( colorValue1 == 1)

				char colorValue2 = pModifiedData[currentPixel] & 0x0f;
				if (colorValue2 == inSearchColor)
				{
					pModifiedData[currentPixel] = (pModifiedData[currentPixel] & 0xf0) + inNewColor;
				}

			} //for x
		}

		BitmapWriter outBitmap;
		outBitmap.CreateBitmap(fileName.mFullPath, bmpData.mBitmapData.mInfoHeader.mImageWidth, -abs(imageHeight), 4, pModifiedData, numBytes,
			bmpData.mBitmapData.mPaletteData.mpRGBA, bmpData.mBitmapData.mPaletteData.mSizeInBytes, true);
	}

	printf("Success\n");
}

void ReplaceColors(const string& inDirectory, const char inNewColor, unordered_set<char>& inIgnoreColors)
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

		const int numBytes = bmpData.mBitmapData.mColorData.mSizeInBytes;
		char* pModifiedData = new char[numBytes];
		const char* pOriginalData = bmpData.mBitmapData.mColorData.mpRGBA;
		memcpy_s(pModifiedData, numBytes, pOriginalData, numBytes);

		//Swap colors
		const int imageHeight = abs(bmpData.mBitmapData.mInfoHeader.mImageHeight);
		for (int y = 0; y < imageHeight; ++y)
		{
			for (int x = 0; x < bmpData.mBitmapData.mInfoHeader.mImageWidth / 2; ++x)
			{
				const int currentPixel = y * bmpData.mBitmapData.mInfoHeader.mImageWidth / 2 + x;

				const char colorValue1 = (pModifiedData[currentPixel] & 0xf0) >> 4;
				if (inIgnoreColors.find(colorValue1) == inIgnoreColors.end())
				{
					pModifiedData[currentPixel] = (pModifiedData[currentPixel] & 0x0f) + (inNewColor << 4);

				}//if( colorValue1 == 1)

				char colorValue2 = pModifiedData[currentPixel] & 0x0f;
				if (inIgnoreColors.find(colorValue2) == inIgnoreColors.end())
				{
					pModifiedData[currentPixel] = (pModifiedData[currentPixel] & 0xf0) + inNewColor;
				}

			} //for x
		}

		BitmapWriter outBitmap;
		outBitmap.CreateBitmap(fileName.mFullPath, bmpData.mBitmapData.mInfoHeader.mImageWidth, -abs(imageHeight), 4, pModifiedData, numBytes,
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

		int imageHeight = bmpData.mBitmapData.mInfoHeader.mImageHeight * -1;
		BitmapWriter outBitmap;
		outBitmap.CreateBitmap(inOutDirectory + fileName.mFileName, bmpData.mBitmapData.mInfoHeader.mImageWidth, imageHeight, 4, bmpData.GetColorData(), bmpData.GetColorDataSize(),
			bmpData.mBitmapData.mPaletteData.mpRGBA, bmpData.mBitmapData.mPaletteData.mSizeInBytes, true);
	}	
}
