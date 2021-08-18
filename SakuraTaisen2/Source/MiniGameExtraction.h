#pragma once

#pragma pack(push, 1)
struct MiniGameImageInfo
{
	uint16 offsetDiv8;
	uint16 unknown;
	uint8  widthDiv8;
	uint8  height;
	uint16 zero;

	void SwapEndianess()
	{
		offsetDiv8 = SwapByteOrder(offsetDiv8);
	}
};
#pragma pack(pop)

struct MiniGameFiles
{
	const char* pCGFileName;
	const char* pDataFileName;
	uint16      imageInfoTableOffset;
	uint32      standardPaletteOffset;
};

void ExtractMiscMiniGameImages(const string& inSakuraDirectory, bool bInIsBmp, const PaletteData& inPalette, const string& inOutputDirectory)
{
	struct MiscImageData
	{
		uint16 numImages;
		uint16  width;
		uint16  height;
		uint8  bpp;
	};

	const int NumCommonImageEntries = 22;
	MiscImageData imageEntriesCommon[NumCommonImageEntries] =
	{
		//MGMRDATA
		1, 144, 144, 4,
		1, 160, 24, 4,
		1, 264, 32, 4,
		2, 144, 8, 4,
		2, 8,   136, 4,
		1, 160, 24, 4,
		1, 72,  24, 4,
		1, 24,  24, 4,
		3, 16,  16, 4,
		1, 16,  24, 4,
		3, 16,  16, 4,
		4, 64,  16, 4,
		1, 48,  16, 4,
		1, 64,  16, 4,
		1, 32,  16, 4,
		6, 16,  16, 4,
		1, 96,  16, 4,
		10, 16, 16, 4,
		1, 136, 144,8, //0x7080
		1, 144, 16, 4, //0xBD20
		4, 128, 16, 4,
		1, 80,  16, 4,
	};

	//Starting at 0xD420
	const int NumMgrmEntries = 9;
	MiscImageData imageEntriesMgrm[NumMgrmEntries] =
	{
		1, 96, 16, 4,
		1, 80, 16, 4,
		1, 96, 16, 4,
		10,16, 16, 4,
		2, 8,  8,  4,
		1, 8,  16, 4,
		2, 16, 16, 4,
		1, 32, 16, 4,
		1, 16, 16, 4,
		//	11,40, 48, 4// (character sprites)
	};

	//Starting at 0xd420
	const int NumMgrnEntries = 8;
	MiscImageData imageEntriesMgrn[NumMgrnEntries] =
	{	
		1, 64, 16,  4,
		1, 80, 16,  4,
		1, 64, 16,  4,
		1, 16, 160, 4,
		1, 8,  32,  4,
		1, 16, 32,  4,
		1, 32, 16,  4,
		1, 16, 16,  4,
	};

	//Starting at 0xd420
	const int NumMgskEntries = 6;
	MiscImageData imageEntriesMgsk[NumMgrnEntries] =
	{
		3, 40, 16,  4,
		1, 16, 160, 4,
		1, 8,  32,  4,
		2, 16, 16,  4,
		1, 32, 16,  4,
		1, 16, 16,  4
	};

	const int NumFiles = 4;
	const char* pFileNames[NumFiles] =
	{
		"MGMRDATA",
		"MGRNDATA",
		"MGSKDATA",
		"MINIGAME" //.CG
	};
	
	const string Sakura3Directory = inSakuraDirectory + string("SAKURA3\\");
	const string imageExt = bInIsBmp ? ".bmp" : ".png";
	const string cgExt(".CG");
	char nameBuffer[20];

	auto ExtractImagesFromFile = 
	[&cgExt, &imageExt, &inPalette, &nameBuffer, bInIsBmp](int NumImageEntries, MiscImageData* pImageEntries, uint32& offset, int& imageCount, const string& finalOutputDir, const FileData& fileData)->bool
	{
		for (int entry = 0; entry < NumImageEntries; ++entry)
		{
			const MiscImageData& imageSet = pImageEntries[entry];

			for (uint16 imageIndex = 0; imageIndex < imageSet.numImages; ++imageIndex)
			{
				if (imageSet.bpp == 4)
				{
					if (offset + (imageSet.width * imageSet.height) / 2 > fileData.GetDataSize())
					{
						return false;
					}

					snprintf(nameBuffer, 20, "%i_%08x", imageCount++, offset);
					const string outFileName = finalOutputDir + string(nameBuffer) + imageExt;

					BitmapWriter outBmp;
					outBmp.CreateBitmap(outFileName, imageSet.width, -imageSet.height, imageSet.bpp, fileData.GetData() + offset, (imageSet.width * imageSet.height) / 2, inPalette.GetData(), inPalette.GetSize(), bInIsBmp);
				}

				offset += imageSet.bpp == 4 ? imageSet.width * imageSet.height / 2 : imageSet.width * imageSet.height;
				if (imageSet.bpp == 8)
				{
					offset += 0x20;
				}
			}
		}

		return true;
	};
	
	for( int fileIndex = 0; fileIndex < NumFiles; ++fileIndex )
	{
		printf("Extracting: %s\n", pFileNames[fileIndex]);

		//Create output directory
		const string finalOutputDir = inOutputDirectory + string(pFileNames[fileIndex]) + Seperators;
		CreateDirectoryHelper(finalOutputDir);

		const string fileExt = fileIndex < 3 ? ".BIN" : ".CG";
		FileNameContainer fileName((Sakura3Directory + string(pFileNames[fileIndex]) + fileExt).c_str());
		FileData fileData;
		if( !fileData.InitializeFileData(fileName) )
		{
			return;
		}

		int imageCount = 1;
		uint32 offset = 0;
		
		ExtractImagesFromFile(NumCommonImageEntries, imageEntriesCommon, offset, imageCount, finalOutputDir, fileData);

		if( fileIndex == 0 )
		{
			ExtractImagesFromFile(NumMgrmEntries, imageEntriesMgrm, offset, imageCount, finalOutputDir, fileData);
		}
		else if( fileIndex == 1 )
		{
			ExtractImagesFromFile(NumMgrnEntries, imageEntriesMgrn, offset, imageCount, finalOutputDir, fileData);
		}
		else if( fileIndex == 2 )
		{
			ExtractImagesFromFile(NumMgskEntries, imageEntriesMgsk, offset, imageCount, finalOutputDir, fileData);
		}
	}
}

void ExtractMiniGames(const string& inSakuraDirectory, bool bInIsBmp, const string& inOutputDirectory)
{
	CreateDirectoryHelper(inOutputDirectory);

	FileData nbgFile;
	if (!nbgFile.InitializeFileData("0000ORIH.BIN", (inSakuraDirectory + string("SAKURA3\\0000ORIH.BIN")).c_str()))
	{
		return;
	}

	PaletteData standardPalette;
	if (!standardPalette.CreateFrom15BitData(nbgFile.GetData() + 0x00033058, 32))
	{
		printf("Unable to create palette.\n");
		return;
	}

	/*
	PaletteData firstImagePalette;
	if (!firstImagePalette.CreateFrom15BitData(nbgFile.GetData() + 0x000331f8, 512))
	{
		printf("Unable to create palette.\n");
		return;
	}
	*/

	const int NumMiniGames = 9;
	MiniGameFiles miniGameFiles[NumMiniGames] =
	{
		"INST_AL", "0000DAIF.BIN", 0xcf64, 0x46770,
		"INST_IR", "0000IRIS.BIN", 0xcd10, 0x339e0,
		"INST_KN", "0000KANN.BIN", 0xc628, 0x3afc4,
		"INST_KR", "0000KORA.BIN", 0xcbc8, 0x3c478,
		"INST_MR", "0000MARI.BIN", 0x8fc8, 0x35178,
		"INST_OH", "0000ORIH.BIN", 0xcbcc, 0x33058,
		"INST_RN", "0000RENI.BIN", 0xa3f4, 0x30e34,
		"INST_SK", "0000SAKU.BIN", 0x8c00, 0x38f00,
		"INST_SM", "0000SUMI.BIN", 0xcd20, 0x31dac,
	};

	const string imageExt = bInIsBmp ? ".bmp" : ".png";
	const string cgExt(".CG");
	const string Sakura3Directory = inSakuraDirectory + string("SAKURA3\\");
	for(int i = 0; i < NumMiniGames; ++i)
	{
		printf("Extracting: %s\n", miniGameFiles[i].pCGFileName);

		const string miniGameOutputDir = inOutputDirectory + miniGameFiles[i].pCGFileName + Seperators;
		CreateDirectoryHelper(miniGameOutputDir);

		FileNameContainer cgFileName( (Sakura3Directory + string(miniGameFiles[i].pCGFileName) + cgExt).c_str() );
		FileData cgFileData;
		if( !cgFileData.InitializeFileData(cgFileName) )
		{
			return;
		}

		FileNameContainer binFileName( (Sakura3Directory + string(miniGameFiles[i].pDataFileName)).c_str() );
		FileData binFileData;
		if( !binFileData.InitializeFileData(binFileName) )
		{
			return;
		}

		uint16 offsetToTable = miniGameFiles[i].imageInfoTableOffset;
		MiniGameImageInfo imageInfo;
		memcpy_s(&imageInfo, sizeof(imageInfo), binFileData.GetData() + offsetToTable, sizeof(imageInfo));
		imageInfo.SwapEndianess();

		PaletteData standardPalette;
		if (!standardPalette.CreateFrom15BitData(binFileData.GetData() + miniGameFiles[i].standardPaletteOffset, 32))
		{
			printf("Unable to create palette.\n");
			return;
		}

		PaletteData logoPalette;
		if (!logoPalette.CreateFrom15BitData(binFileData.GetData() + miniGameFiles[i].standardPaletteOffset - 32, 32))
		{
			printf("Unable to create palette.\n");
			return;
		}

		PaletteData firstImagePalette;
		if (!firstImagePalette.CreateFrom15BitData(binFileData.GetData() + miniGameFiles[i].standardPaletteOffset + 96, 512))
		{
			printf("Unable to create palette.\n");
			return;
		}

		int imageNumber = 0;
		while(imageInfo.unknown != 0)
		{
			const string outFileName = miniGameOutputDir + std::to_string(imageNumber) + imageExt;

			BitmapWriter outBmp;
			if( imageNumber == 0 )
			{
				outBmp.CreateBitmap(outFileName, imageInfo.widthDiv8*8, -imageInfo.height, 8, cgFileData.GetData() + imageInfo.offsetDiv8*8, (imageInfo.widthDiv8*8 * imageInfo.height), firstImagePalette.GetData(), firstImagePalette.GetSize(), bInIsBmp);
			}
			else if( imageNumber == 1 )
			{
				outBmp.CreateBitmap(outFileName, imageInfo.widthDiv8*8, -imageInfo.height, 4, cgFileData.GetData() + imageInfo.offsetDiv8*8, (imageInfo.widthDiv8*8 * imageInfo.height) / 2, logoPalette.GetData(), logoPalette.GetSize(), bInIsBmp);
			}
			else
			{
				outBmp.CreateBitmap(outFileName, imageInfo.widthDiv8*8, -imageInfo.height, 4, cgFileData.GetData() + imageInfo.offsetDiv8*8, (imageInfo.widthDiv8*8 * imageInfo.height) / 2, standardPalette.GetData(), standardPalette.GetSize(), bInIsBmp);
			}
			
			//read next entry
			offsetToTable += sizeof(MiniGameImageInfo);
			memcpy_s(&imageInfo, sizeof(imageInfo), binFileData.GetData() + offsetToTable, sizeof(imageInfo));
			imageInfo.SwapEndianess();

			++imageNumber;
		}
	}

	ExtractMiscMiniGameImages(inSakuraDirectory, bInIsBmp, standardPalette, inOutputDirectory);
}
