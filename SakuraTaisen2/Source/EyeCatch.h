#pragma once

struct TMarkInfo
{
	int width;
	int height;
};

const int NumTMarkFiles = 5;
const TMarkInfo TMarkImages[NumTMarkFiles] = 
{
	{80, 16},
	{80, 16},
	{80, 16},
	{80, 16},
	{96, 16}
};

void ExtractEyeCatch(const std::string& inSakuraDir, const std::string& inOutputDirectory, bool bInBmp)
{
	CreateDirectoryHelper(inOutputDirectory);

	const std::string imgExt = bInBmp ? ".bmp" : ".png";

	//T_MARK.CG
	{
		const std::string tmarkFilePath = inSakuraDir + "T_MARK.CG";
		FileData tmarkFile;
		if (!tmarkFile.InitializeFileData(tmarkFilePath))
		{
			return;
		}

		const std::string pbEyeFilePath = inSakuraDir + "PB_EYE.CL";
		FileData pbEyeFile;
		if (!pbEyeFile.InitializeFileData(pbEyeFilePath))
		{
			return;
		}

		const std::string tmarkOutputDir = inOutputDirectory + "TMark\\";
		CreateDirectoryHelper(tmarkOutputDir);

		unsigned int currOffset = 0xa800;
		for(int i = 0; i < NumTMarkFiles; ++i)
		{
			const string bitmapFileName = tmarkOutputDir + std::to_string(i) + ".bmp";

			const int imgSize = (TMarkImages[i].width * TMarkImages[i].height) / 2;
			ExtractImageFromData(tmarkFile.GetData() + currOffset, imgSize, bitmapFileName, pbEyeFile.GetData() + 0x2a0, 32, true, TMarkImages[i].width, TMarkImages[i].height, 1, 256, 0, true, bInBmp);

			currOffset += imgSize;
		}
	}

	//EYECATCH.ALL
	{
		const std::string eyeCatchAllFilePath = inSakuraDir + "EYECATCH.ALL";
		FileData eyeCatchAllFile;
		if (!eyeCatchAllFile.InitializeFileData(eyeCatchAllFilePath))
		{
			return;
		}

		const std::string eyeCatchAllDir = inOutputDirectory + "EyeCatchAll\\";

		CreateDirectoryHelper(eyeCatchAllDir);

		const unsigned long fileSize = eyeCatchAllFile.GetDataSize();
		unsigned long currOffset = 0;
		const uint32 imgSize = 320*224;
		const uint32 paletteOffset = 0x12000;
		int imageIndex = 0;
		while(currOffset < fileSize)
		{
			const string bitmapFileName = eyeCatchAllDir + std::to_string(imageIndex++) + ".bmp";
			ExtractImageFromData(eyeCatchAllFile.GetData() + currOffset + 0x40, imgSize, bitmapFileName, eyeCatchAllFile.GetData() + currOffset + paletteOffset, 512, false, 8, 8, 320 / 8, 256, 0, true, bInBmp);

			currOffset += 0x12800;
		}
	}
}

bool PatchEyeCatch(const std::string& inPatchedSakuraDirectory, const std::string& inTranslatedDataDirectory)
{
	//T_MARK
	{
		const string sakuraFilePath = inPatchedSakuraDirectory + string("SAKURA1\\T_MARK.CG");
		FileReadWriter tmarkFile;
		if (!tmarkFile.OpenFile(sakuraFilePath))
		{
			return false;
		}

		const string translatedDirectory = inTranslatedDataDirectory + "\\EyeCatch\\TMark\\";

		const string bmpExt(".bmp");
		const int numEntries = 129;
		int baseOffset = 0;
		unsigned int currOffset = 0xa800;
		for (int i = 0; i < NumTMarkFiles; ++i)
		{
			const string translatedFileName = translatedDirectory + std::to_string(i) + bmpExt;

			BmpToSaturnConverter convertedImage;
			if (!convertedImage.ConvertBmpToSakuraFormat(translatedFileName, false))
			{
				return false;
			}

			if(convertedImage.mPalette.GetNumColors() != 16)
			{
				printf("%s needs to be 4bpp\n", translatedFileName.c_str());
				continue;
			}

			const int width = TMarkImages[i].width;
			const int height = TMarkImages[i].height;
			if (convertedImage.GetImageWidth() != width || abs(convertedImage.GetImageHeight()) != height)
			{
				printf("Dimensions for %s should be %ix%i, but are %ix%i\n", translatedFileName.c_str(), width, height, convertedImage.GetImageWidth(), convertedImage.GetImageHeight());
				continue;
			}

			const int imgSize = (width * height) / 2;
			tmarkFile.WriteData(currOffset, convertedImage.GetImageData(), convertedImage.GetImageDataSize());

			currOffset += imgSize;
		}
	}

	//EyeCatchAll
	{
		const std::string eyeCatchAllFilePath = inPatchedSakuraDirectory + "SAKURA1\\EYECATCH.ALL";
		FileReadWriter eyeCatchFile;
		if (!eyeCatchFile.OpenFile(eyeCatchAllFilePath))
		{
			return false;
		}

		const string translatedDirectory = inTranslatedDataDirectory + "\\EyeCatch\\EyeCatchAll\\";

		const unsigned long fileSize = eyeCatchFile.GetFileSize();
		int imageIndex = 3;
		unsigned long currOffset = 0x12800 * imageIndex;
		const uint32 imgSize = 320 * 224;
		unsigned int tileDim = 8;
		while (currOffset < fileSize)
		{
			const string bitmapFileName = translatedDirectory + std::to_string(imageIndex++) + ".bmp";
			
			BmpToSaturnConverter patchedImageData;
			if (!patchedImageData.ConvertBmpToSakuraFormat(bitmapFileName, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim))
			{
				printf("CompressedImagePatcher: Couldn't convert image: %s.\n", bitmapFileName.c_str());
				return false;
			}

			patchedImageData.PackTiles();

			if (patchedImageData.mPackedTileSize != imgSize)
			{
				printf("Image %s is too big.  Should be 320x224 @8bpp\n", bitmapFileName.c_str());
				return false;
			}

			eyeCatchFile.WriteData(currOffset, patchedImageData.mpPackedTiles, patchedImageData.mPackedTileSize);

			currOffset += 0x12800;
		}
	}

	return true;
}
