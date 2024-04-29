#pragma once

void ExtractWarningScreens(const std::string& inSakuraDir, const std::string& inOutputDirectory, bool bInBmp)
{
	CreateDirectoryHelper(inOutputDirectory);

	const FileNameContainer warningFileName(inSakuraDir + string("SAKURA1\\CACG.ALL"));
	FileData titleFileData;
	if (!titleFileData.InitializeFileData(warningFileName))
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
	int imageOffset = 0;
	const int numImages = 19;
	for (int i = 0; i < numImages; ++i)
	{
		const string outputFile = inOutputDirectory + string("image_") + std::to_string(i) + imgExt;
		const int imgSize = 0x9000;

		ExtractImageFromData(titleFileData.GetData() + imageOffset, imgSize, outputFile,
							 sakuraFileData.GetData() + 0x70984, 32, true, 8, 8, 40, 256, 0, true, bInBmp);

		imageOffset += imgSize;
	}
}