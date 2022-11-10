#pragma once

/*
* All of them are contained in WPALL1.ALL

Format:
Image data (320 * 224 @8bpp)
Palette (512 byes)
*/

bool PatchTitleScreens(const string& InRootOutputDirectory, const string& InDataDirectory)
{
	string wpallFileName = InRootOutputDirectory + "\\SAKURA1\\WPALL1.ALL";
	FileReadWriter wpallFile;
	if(!wpallFile.OpenFile(wpallFileName))
	{
		return false;
	}

	struct ImageData
	{
		const char* pName;
		int imageNumber;
	};

	const ImageData patchedImages[] =
	{
		{"WPALL20.bmp", 19},
		{"WPALL21.bmp", 20},
		{"WPALL22.bmp", 21},
		{"WPALL23.bmp", 22},
		{"WPALL24.bmp", 23}
	};

	int offset = 0x190040;
	const int imageStride = 320*224;
	const int numImages = sizeof(patchedImages) / sizeof(ImageData);
	unsigned int tileDim = 8;
	for(int i = 0; i < numImages; ++i)
	{
		const string patchedImagePath = InDataDirectory + patchedImages[i].pName;

		BmpToSaturnConverter patchedImageData;
		if (!patchedImageData.ConvertBmpToSakuraFormat(patchedImagePath, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim))
		{
			printf("CompressedImagePatcher: Couldn't convert image: %s.\n", patchedImagePath.c_str());
			return false;
		}

		patchedImageData.PackTiles();

		if (patchedImageData.mPackedTileSize != imageStride)
		{
			printf("Image %s is too big.  Should be 320x240 @8bpp\n", patchedImagePath.c_str());
			return false;
		}

		wpallFile.WriteData(offset, patchedImageData.mpPackedTiles, patchedImageData.mPackedTileSize);
		offset += imageStride + 4096;
	}

	return true;
}
