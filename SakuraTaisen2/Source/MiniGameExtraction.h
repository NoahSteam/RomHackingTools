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

const int NumMiniGames = 9;
const MiniGameFiles miniGameFiles[NumMiniGames] =
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

struct MiniGameMiscImageData
{
	uint16 numImages;
	uint16  width;
	uint16  height;
	uint8  bpp;
};

const int NumCommonImageEntries = 22;
const MiniGameMiscImageData MGCommonImages[NumCommonImageEntries] =
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
const MiniGameMiscImageData MGRMImages[NumMgrmEntries] =
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
const int NumMgrnEntries = 9;
MiniGameMiscImageData MGRNImages[NumMgrnEntries] =
{
	1, 64, 16,  4,
	1, 80, 16,  4,
	1, 64, 16,  4,
	1, 16, 160, 4,
	1, 8,  32,  4,
	1, 16, 16,  4,
	1, 16, 16,  4,
	1, 32, 16,  4,
	1, 16, 16,  4,
};

//Starting at 0xd420
const int NumMgskEntries = 6;
const MiniGameMiscImageData MGSKImages[NumMgrnEntries] =
{
	3, 40, 16,  4,
	1, 16, 160, 4,
	1, 8,  32,  4,
	2, 16, 16,  4,
	1, 32, 16,  4,
	1, 16, 16,  4
};

const int NumMiniGameMGFiles = 4;
const char* pMiniGameMGFiles[NumMiniGameMGFiles] =
{
	"MGMRDATA",
	"MGRNDATA",
	"MGSKDATA",
	"MINIGAME" //.CG
};

void ExtractMiscMiniGameImages(const string& inSakuraDirectory, bool bInIsBmp, const PaletteData& inPalette, const string& inOutputDirectory)
{
	const string Sakura3Directory = inSakuraDirectory + string("SAKURA3\\");
	const string imageExt = bInIsBmp ? ".bmp" : ".png";
	const string cgExt(".CG");
	char nameBuffer[20];

	auto ExtractImagesFromFile = 
	[&cgExt, &imageExt, &inPalette, &nameBuffer, bInIsBmp](int NumImageEntries, const MiniGameMiscImageData* pImageEntries, uint32& offset, int& imageCount, const string& finalOutputDir, const FileData& fileData)->bool
	{
		for (int entry = 0; entry < NumImageEntries; ++entry)
		{
			const MiniGameMiscImageData& imageSet = pImageEntries[entry];

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
	
	for( int fileIndex = 0; fileIndex < NumMiniGameMGFiles; ++fileIndex )
	{
		printf("Extracting: %s\n", pMiniGameMGFiles[fileIndex]);

		//Create output directory
		const string finalOutputDir = inOutputDirectory + string(pMiniGameMGFiles[fileIndex]) + Seperators;
		CreateDirectoryHelper(finalOutputDir);

		const string fileExt = fileIndex < 3 ? ".BIN" : ".CG";
		FileNameContainer fileName((Sakura3Directory + string(pMiniGameMGFiles[fileIndex]) + fileExt).c_str());
		FileData fileData;
		if( !fileData.InitializeFileData(fileName) )
		{
			return;
		}

		int imageCount = 1;
		uint32 offset = 0;
		
		ExtractImagesFromFile(NumCommonImageEntries, MGCommonImages, offset, imageCount, finalOutputDir, fileData);

		if( fileIndex == 0 )
		{
			ExtractImagesFromFile(NumMgrmEntries, MGRMImages, offset, imageCount, finalOutputDir, fileData);
		}
		else if( fileIndex == 1 )
		{
			ExtractImagesFromFile(NumMgrnEntries, MGRNImages, offset, imageCount, finalOutputDir, fileData);
		}
		else if( fileIndex == 2 )
		{
			ExtractImagesFromFile(NumMgskEntries, MGSKImages, offset, imageCount, finalOutputDir, fileData);
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

	FileData sakuraFile;
	if (!sakuraFile.InitializeFileData("0000SAKU.BIN", (inSakuraDirectory + string("SAKURA3\\0000SAKU.BIN")).c_str()))
	{
		return;
	}

	FileData reniFile;
	if (!reniFile.InitializeFileData("0000RENI.BIN", (inSakuraDirectory + string("SAKURA3\\0000RENI.BIN")).c_str()))
	{
		return;
	}

	PaletteData logoPaletteSaku;
	if (!logoPaletteSaku.CreateFrom15BitData(sakuraFile.GetData() + 0x00038ee0, 32))
	{
		printf("Unable to create palette.\n");
		return;
	}

	PaletteData logoPaletteReni;
	if (!logoPaletteReni.CreateFrom15BitData(reniFile.GetData() + 0x00030e14, 32))
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

//	ExtractMiscMiniGameImages(inSakuraDirectory, bInIsBmp, standardPalette, inOutputDirectory);
	ExtractMiscMiniGameImages(inSakuraDirectory, bInIsBmp, logoPaletteReni, inOutputDirectory);
}

bool PatchMiscMiniGameFiles(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	const string translatedDir = inTranslatedDataDirectory + "MiniGames\\";
	const string commonImagesDir = translatedDir + "MINIGAME\\";
	const string imageExt(".bmp");
	char nameBuffer[20];

	auto PatchMGFile =
		[&imageExt, &nameBuffer, &commonImagesDir](int InNumImageEntries, const MiniGameMiscImageData* pInImageEntries, uint32& InOffset, int& inImageCount, const string& miniGameFileDir, FileReadWriter& inFileWriter)->bool
	{
		for (int entry = 0; entry < InNumImageEntries; ++entry)
		{
			const MiniGameMiscImageData& imageSet = pInImageEntries[entry];

			for (uint16 imageIndex = 0; imageIndex < imageSet.numImages; ++imageIndex)
			{
				if (imageSet.bpp == 4)
				{
					if (InOffset + (imageSet.width * imageSet.height) / 2 > inFileWriter.GetFileSize())
					{
						return true;
					}

					snprintf(nameBuffer, 20, "%i_%08x", inImageCount++, InOffset);
					string imagePath = miniGameFileDir + string(nameBuffer) + imageExt;
					BmpToSaturnConverter translatedImage;
					bool bFoundImage = translatedImage.ConvertBmpToSakuraFormat(imagePath, false, BmpToSaturnConverter::CYAN, nullptr, nullptr, false);
					if(!bFoundImage)
					{
						imagePath = commonImagesDir + string(nameBuffer) + imageExt;
						bFoundImage = translatedImage.ConvertBmpToSakuraFormat(imagePath, false, BmpToSaturnConverter::CYAN, nullptr, nullptr, false);
					}

					if(bFoundImage)
					{
						if (translatedImage.GetImageWidth() != imageSet.width || translatedImage.GetImageHeight() != imageSet.height)
						{
							printf("Image dimensions mismatch in %s.  Expected: %ix%i Got: %ix%i", imagePath.c_str(), imageSet.width, imageSet.height, translatedImage.GetImageWidth(), translatedImage.GetImageHeight());
							return false;
						}

						if (!((translatedImage.mPalette.GetNumColors() == 16 && imageSet.bpp == 4) ||
							(translatedImage.mPalette.GetNumColors() == 256 && imageSet.bpp == 8))
							)
						{
							printf("Image bpp mismatch in %s.  Expected: %i", imagePath.c_str(), imageSet.bpp);
							return false;
						}

						inFileWriter.WriteData(InOffset, translatedImage.GetImageData(), translatedImage.GetImageDataSize(), false);
					}
					else
					{
						int kd = 0;
						++kd;
					}
				}

				InOffset += imageSet.bpp == 4 ? imageSet.width * imageSet.height / 2 : imageSet.width * imageSet.height;
				if (imageSet.bpp == 8)
				{
					InOffset += 0x20;
				}
			}
		}
		return true;
	};

	//Patch MG files;
	const string sakura3Directory = inPatchedSakuraDirectory + string("SAKURA3\\");
	for (int fileIndex = 0; fileIndex < NumMiniGameMGFiles; ++fileIndex)
	{
		const string miniGameFileDir = translatedDir + string(pMiniGameMGFiles[fileIndex]) + Seperators;

		const string fileExt = fileIndex < 3 ? ".BIN" : ".CG";
		FileNameContainer fileName((sakura3Directory + string(pMiniGameMGFiles[fileIndex]) + fileExt).c_str());
		FileReadWriter fileData;
		if (!fileData.OpenFile(fileName.mFullPath))
		{
			return false;
		}

		int imageCount = 1;
		uint32 offset = 0;

		if(!PatchMGFile(NumCommonImageEntries, MGCommonImages, offset, imageCount, miniGameFileDir, fileData))
		{
			return false;
		}

		if (fileIndex == 0)
		{
			if(!PatchMGFile(NumMgrmEntries, MGRMImages, offset, imageCount, miniGameFileDir, fileData))
			{
				return false;
			}
		}
		else if (fileIndex == 1)
		{
			if(!PatchMGFile(NumMgrnEntries, MGRNImages, offset, imageCount, miniGameFileDir, fileData))
			{
				return false;
			}
		}
		else if (fileIndex == 2)
		{
			if(!PatchMGFile(NumMgskEntries, MGSKImages, offset, imageCount, miniGameFileDir, fileData))
			{
				return false;
			}
		}
	}

	return true;
}

bool PatchMiniGames(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	printf("Patching MiniGames\n");

	const string translatedDir = inTranslatedDataDirectory + "MiniGames\\";
	const string imgExt(".bmp");
	const string cgExt(".CG");
	const string sakura3Directory = inPatchedSakuraDirectory + string("SAKURA3\\");

	for (int i = 0; i < NumMiniGames; ++i)
	{
		//Header info file
		FileNameContainer binFileName((sakura3Directory + string(miniGameFiles[i].pDataFileName)).c_str());
		FileData binFileData;
		if (!binFileData.InitializeFileData(binFileName))
		{
			return false;
		}

		//Image file
		FileNameContainer cgFileName((sakura3Directory + string(miniGameFiles[i].pCGFileName) + cgExt).c_str());
		FileReadWriter cgFileData;
		if (!cgFileData.OpenFile(cgFileName.mFullPath))
		{
			return false;
		}

		//Read header
		uint16 offsetToTable = miniGameFiles[i].imageInfoTableOffset;
		MiniGameImageInfo imageInfo;
		binFileData.ReadData(offsetToTable, (char*)&imageInfo, sizeof(imageInfo), false);
		imageInfo.SwapEndianess();

		//Translated image dir
		const string miniGameFileDir = translatedDir + miniGameFiles[i].pCGFileName + Seperators;

		int imageNumber = 0;
		while (imageInfo.unknown != 0)
		{	
			//First image is the minigame artwork, skip that
			if (imageNumber != 0)
			{
				const string imagePath = miniGameFileDir + std::to_string(imageNumber) + imgExt;

				BmpToSaturnConverter convertedImage;
				if (!convertedImage.ConvertBmpToSakuraFormat(imagePath, false))
				{
					return false;
				}

				if (convertedImage.GetImageWidth() != imageInfo.widthDiv8 * 8 || convertedImage.GetImageHeight() != imageInfo.height)
				{
					printf("Image dimensions mismatch in %s.  Expected: %ix%i Got: %ix%i", imagePath.c_str(), imageInfo.widthDiv8 * 8, imageInfo.height, convertedImage.GetImageWidth(), convertedImage.GetImageHeight());
					return false;
				}

				cgFileData.WriteData(imageInfo.offsetDiv8 * 8, convertedImage.GetImageData(), convertedImage.GetImageDataSize(), false);
			}

			//read next entry
			offsetToTable += sizeof(MiniGameImageInfo);
			binFileData.ReadData(offsetToTable, (char*)&imageInfo, sizeof(imageInfo), false);
			imageInfo.SwapEndianess();

			++imageNumber;
		}
	}

	return PatchMiscMiniGameFiles(inPatchedSakuraDirectory, inTranslatedDataDirectory);
}
