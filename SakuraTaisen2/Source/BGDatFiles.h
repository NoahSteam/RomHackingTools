#pragma once

void ExtractBGDatFiles(const string& InRootSakuraDirectory, const string& InOutDirectory)
{
	CreateDirectoryHelper(InOutDirectory);

	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(InRootSakuraDirectory + "SAKURA2", allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> datFiles;
	GetAllFilesOfType(allFiles, ".DAT", datFiles);

	const string bmpExt(".bmp");

	const int numFiles = (int)datFiles.size();
	for(int i = 0; i < numFiles; ++i)
	{
		if(datFiles[i].mNoExtension.size() < 3 || (datFiles[i].mNoExtension[0] != 'B' && datFiles[i].mNoExtension[1] != 'G'))
		{
			continue;
		}

		FileData fileData;
		if(!fileData.InitializeFileData(datFiles[i]))
		{
			continue;
		}

		const char* pImageData = fileData.GetData() + 0x710;
		const char* pPaletteData = fileData.GetData() + 0x510;
		const string outFileName = InOutDirectory + datFiles[i].mNoExtension + bmpExt;

		ExtractImageFromData(pImageData, fileData.GetDataSize() - 0x710, outFileName, pPaletteData, 512, false, 8, 8,
							36, 256, 0, true, false);
	}
}

bool PatchBGDatFiles(const string& InPatchedSakuraDirectory, const string& InTranslatedDataDirectory)
{
	printf("Patching BGDatFiles\n");

	vector<FileNameContainer> bossImages;
	FindAllFilesWithinDirectory(InTranslatedDataDirectory + "BossImages", bossImages);

	const int numFiles = (int)bossImages.size();
	for (int i = 0; i < numFiles; ++i)
	{
		FileReadWriter targetFile;
		if (!targetFile.OpenFile(InPatchedSakuraDirectory + string("SAKURA2\\") + bossImages[i].mNoExtension + string(".DAT")))
		{
			return false;
		}

		const uint32 tileDim = 8;
		BmpToSaturnConverter patchedImage;
		if (!patchedImage.ConvertBmpToSakuraFormat(bossImages[i].mFullPath, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim))
		{
			return false;
		}

		patchedImage.PackTiles();

		targetFile.WriteData(0x710, patchedImage.mpPackedTiles, patchedImage.mPackedTileSize);
	}

	return true;
}
