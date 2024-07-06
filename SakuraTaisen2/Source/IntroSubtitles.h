#pragma once

void ExtractIntroSubtitles(const std::string& inRootSakuraDirectory, const std::string& inOutputDirectory)
{
	CreateDirectoryHelper(inOutputDirectory);

	FileData subFile;
	if(!subFile.InitializeFileData(inRootSakuraDirectory + "SAKURA1\\OPCPK.BG"))
	{
		return;
	}

	PRSDecompressor decompressor;
	decompressor.UncompressData((void*)(subFile.GetData() + 0xc400), subFile.GetDataSize() - 0xc400);

	const uint32 width = 272;
	const uint32 height = 16;
	int offset = 0;
	for(int i = 0; i < 6; ++i)
	{
		const string imgName = inOutputDirectory + std::to_string(i) + ".bmp";
		ExtractImageFromData(decompressor.mpUncompressedData + offset, width*height, imgName, subFile.GetData(), 512, false, width, height, 1, 256, 0, true, true);

		offset += width*height;
	}
}

bool PatchIntroSubtitles(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory, int InDisc)
{
	printf("Patching Intro Subtitles\n");

	const string translatedDir = inTranslatedDataDirectory + "IntroSubtitles\\";

	const char* pFiles[] =
	{
		"SAKURA1\\OPCPK.BG", "SAKURA1\\OPBG_1.BIN",
		"SAKURA1\\OPCPK.BG", "SAKURA1\\OPBG_2.BIN",
		"SAKURA1\\OPCPK.BG", "SAKURA1\\OPBG_3.BIN"
	};

	const int startingIndex = InDisc == 1 ? 0 : InDisc == 2 ? 2 : 4;
	const int endingIndex   = startingIndex + 2;
	for(int i = startingIndex; i < endingIndex; ++i)
	{
		FileReadWriter subFile;
		if (!subFile.OpenFile(inPatchedSakuraDirectory + pFiles[i]))
		{
			return false;
		}

		MemoryBlocks patchedData;

		const uint32 width = 272;
		const uint32 height = 16;
		for(int f = 0; f < 6; ++f)
		{
			const string translatedImagePath = translatedDir + std::to_string(i) + ".bmp";
			BmpToSaturnConverter translatedImage;
			if (!translatedImage.ConvertBmpToSakuraFormat(translatedImagePath, false, BmpToSaturnConverter::CYAN, &width, &height))
			{
				return false;
			}

			patchedData.AddBlock(translatedImage.GetImageData(), 0, translatedImage.GetImageDataSize(), false);
		}

		patchedData.CombineBlocks();

		PRSCompressor compressor;
		compressor.CompressData(patchedData.GetCombinedData(), patchedData.GetTotalSize(), PRSCompressor::kCompressOption_None);

		if(compressor.mCompressedSize > 9098)
		{
			printf("PatchIntroSubtitles: Compressed size is too big. Must be less than 9099.  Is %ul\n", compressor.mCompressedSize);
			return false;
		}

		subFile.WriteData(0xc400, compressor.mpCompressedData, compressor.mCompressedSize, false);
	}

	return true;
}