#pragma once

void ExtractBattleSimulator(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
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
		CreateSpreadSheetForImages("BattleSimulator", "BattleSimulator\\PNG\\", inOutputDirectory, "BattleSimulator.php");
	}
}