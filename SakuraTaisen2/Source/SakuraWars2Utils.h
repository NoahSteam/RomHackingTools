#pragma once

bool FixupWKLImageColors(const string& WklDirectory, const string& InNewPaletteBmp, const string& OutDirectory)
{
	CreateDirectoryHelper(OutDirectory);

	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(WklDirectory, allFiles);

	vector<FileNameContainer> bmpFiles;
	GetAllFilesOfType(allFiles, ".bmp", bmpFiles);

	BitmapReader newBmpPalette;
	if(!newBmpPalette.ReadBitmap(InNewPaletteBmp))
	{
		printf("Failed to read %s\n", InNewPaletteBmp.c_str());
		return false;
	}

	for (const FileNameContainer& fileName : bmpFiles)
	{
		BitmapReader bmpData;
		if (!bmpData.ReadBitmap(fileName.mFullPath))
		{
			printf("Failed to read %s\n", fileName.mFullPath.c_str());
			return false;
		}

		const int numBytes = bmpData.mBitmapData.mColorData.mSizeInBytes;
		char* pModifiedData = new char[numBytes];
		const char* pOriginalData = bmpData.mBitmapData.mColorData.mpRGBA;
		memcpy_s(pModifiedData, numBytes, pOriginalData, numBytes);

		//Flip image
		const int imageHeight = abs(bmpData.mBitmapData.mInfoHeader.mImageHeight);
		for (int y = 0; y < imageHeight; ++y)
		{
			for (int x = 0; x < bmpData.mBitmapData.mInfoHeader.mImageWidth / 2; ++x)
			{
				const int currentPixel = y * bmpData.mBitmapData.mInfoHeader.mImageWidth / 2 + x;
				const int outPixel = ((imageHeight - 1) - y) * bmpData.mBitmapData.mInfoHeader.mImageWidth / 2 + x;
				pModifiedData[outPixel] = pOriginalData[currentPixel];
			}
		}

		const char originalBgndColor = 0;
		const char originalTextColor = 1;
		const char originalBorderColor = 3;
		const char newBgndColor = 1;
		const char newTextColor = 7;
		const char newBorderColor = 8;

		//Bold colors
		for (int y = 0; y < imageHeight; ++y)
		{
			for (int x = 0; x < bmpData.mBitmapData.mInfoHeader.mImageWidth / 2; ++x)
			{
				const int currentPixel = y * bmpData.mBitmapData.mInfoHeader.mImageWidth / 2 + x;

				const char colorValue1 = (pModifiedData[currentPixel] & 0xf0) >> 4;
				if (colorValue1 == originalBgndColor)
				{
					pModifiedData[currentPixel] = (pModifiedData[currentPixel] & 0x0f) + (newBgndColor << 4);
				}
				else if(colorValue1 == originalTextColor)
				{
					pModifiedData[currentPixel] = (pModifiedData[currentPixel] & 0x0f) + (newTextColor << 4);
				}
				else if (colorValue1 == originalBorderColor)
				{
					pModifiedData[currentPixel] = (pModifiedData[currentPixel] & 0x0f) + (newBorderColor << 4);
				}

				char colorValue2 = pModifiedData[currentPixel] & 0x0f;
				if (colorValue2 == originalBgndColor)
				{
					pModifiedData[currentPixel] = (pModifiedData[currentPixel] & 0xf0) + newBgndColor;
				}
				else if (colorValue2 == originalTextColor)
				{
					pModifiedData[currentPixel] = (pModifiedData[currentPixel] & 0xf0) + newTextColor;
				}
				else if (colorValue2 == originalBorderColor)
				{
					pModifiedData[currentPixel] = (pModifiedData[currentPixel] & 0xf0) + newBorderColor;
				}

			} //for x
		}

		const string outFile = OutDirectory + fileName.mFileName;

		BitmapWriter outBitmap;
		outBitmap.CreateBitmap(outFile, bmpData.mBitmapData.mInfoHeader.mImageWidth, -abs(imageHeight), 4, pModifiedData, numBytes,
			newBmpPalette.mBitmapData.mPaletteData.mpRGBA, newBmpPalette.mBitmapData.mPaletteData.mSizeInBytes, true);
	}

	printf("Success\n");

	return true;
}

void ExtractTiledImage(const char* pFileName, const char* pOutFileName, int dataOffset, int paletteOffset)
{
	const FileNameContainer inFileName(pFileName);
	const string outFileName(pOutFileName);

	FileData inFileData;
	if( !inFileData.InitializeFileData(inFileName) )
	{
		printf("ExtractTiledImage %s failed\n", pFileName);
		return;
	}

	ExtractImageFromData(inFileData.GetData(), inFileData.GetDataSize(), outFileName, inFileData.GetData() + paletteOffset, 512, false, 8, 8, 320/8, 256, dataOffset, true, true);
}
