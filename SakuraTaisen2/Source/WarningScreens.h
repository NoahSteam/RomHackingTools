#pragma once

void ExtractWarningScreens(const std::string& inSakuraDir, const std::string& inOutputDirectory, bool bInBmp)
{
	CreateDirectoryHelper(inOutputDirectory);

	const FileNameContainer warningFileName(inSakuraDir + string("SAKURA1\\CACG.ALL"));
	FileData warningFileData;
	if (!warningFileData.InitializeFileData(warningFileName))
	{
		return;
	}

	const FileNameContainer sakuraFileName(inSakuraDir + string("SAKURA.BIN"));
	FileData sakuraFileData;
	if (!sakuraFileData.InitializeFileData(sakuraFileName))
	{
		return;
	}

	const string imgExt = bInBmp ? ".bmp" : ".png";
	const int imgSize = 0x9000;
	int imageOffset = 0;
	const int numImages = 19;
	for (int i = 0; i < numImages; ++i)
	{
		const string outputFile = inOutputDirectory + string("image_") + std::to_string(i) + imgExt;

		ExtractImageFromData(warningFileData.GetData() + imageOffset, imgSize, outputFile,
							 sakuraFileData.GetData() + 0x70984, 32, true, 8, 8, 40, 256, 0, true, bInBmp);

		imageOffset += imgSize;
	}
}

bool PatchWarningScreens(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	printf("Patching Warning Screens\n");

	string warningFileName(inPatchedSakuraDirectory + string("SAKURA1\\CACG.ALL"));
	FileReadWriter warningFileData;
	if (!warningFileData.OpenFile(warningFileName))
	{
		return false;
	}

	const string imgExt(".bmp");
	const int imgSize = 0x9000;
	int imageOffset = 0;
	const int numImages = 19;
	for (int i = 0; i < numImages; ++i)
	{
		const string translatedImagePath = inTranslatedDataDirectory + string("WarningScreens\\image_") + std::to_string(i) + imgExt;

		const uint32 tileDim = 8;
		BmpToSaturnConverter patchedImage;
		if (!patchedImage.ConvertBmpToSakuraFormat(translatedImagePath, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim))
		{
			return false;
		}
		patchedImage.PackTiles();

		warningFileData.WriteData(imageOffset, patchedImage.mpPackedTiles, patchedImage.mPackedTileSize);

		imageOffset += imgSize;
	}

	return true;
}
