#pragma once

/*
* All of them are contained in WPALL1.ALL

Format:
Image data (320 * 224 @8bpp)
Palette (512 byes)
*/

bool PatchTitleScreens(const string& InRootOutputDirectory, const string& InDataDirectory, int InDiscNumber)
{
	printf("Patching Title Screens\n");

	const string wpallDirectory = InDataDirectory + string("ChapterScreens\\WPALL") + std::to_string(InDiscNumber) + Seperators;

	string wpallFileName = InRootOutputDirectory + string("\\SAKURA1\\WPALL") + std::to_string(InDiscNumber) + string(".ALL");
	FileReadWriter wpallFile;
	if(!wpallFile.OpenFile(wpallFileName))
	{
		return false;
	}

	vector<FileNameContainer> wpallImages;
	FindAllFilesWithinDirectory(wpallDirectory, wpallImages);

	int offset = InDiscNumber == 1 ? 0x190040 : InDiscNumber == 2 ? 0x221040 : 0x10e040;
	const int imageStride = 320*224;
	const int numImages = (int)wpallImages.size();
	unsigned int tileDim = 8;
	for(int i = 0; i < numImages; ++i)
	{
		const string patchedImagePath = wpallImages[i].mFullPath;

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

		//Special case for disc 3 where the last screen is after a bunch of other epilogue images
		if( InDiscNumber == 3 && i == 2 )
		{
			offset = 0x25B040;
		}

		wpallFile.WriteData(offset, patchedImageData.mpPackedTiles, patchedImageData.mPackedTileSize);
		offset += imageStride + 4096;
	}

	return true;
}
