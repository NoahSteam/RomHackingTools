#pragma once

struct TycoonOffsetKeyInfo
{
	uint32 offset;
	uint32 key;

	void FixupAfterLoading()
	{
		offset = SwapByteOrder(offset) * 0x800;
		key = SwapByteOrder(key);
	}
};

TycoonOffsetKeyInfo GTycoonKeyEntries[] = 
{
	0x1000,     0x2b4d0,
	0x2c800,    0x996c,
	0x36800,    0xb6c4,
	0xb5800,    0x335c,
	0xbc800,    0x870c,
	0xb9000,    0x335c,
	0xf0800,    0x89dc,
	0xf9800,    0x89dc,
	0x102800,   0x89dc,
	0x10b800,   0x89dc,
	0x11d800,   0x1000c, //335c also
	0x1db000,   0x335c,
	0x1de800,   0x335c,
	0x1f3800,   0x31dc,
	0x1f7000,   0x31dc,
	0x1fa800,   0x31dc,
	0x1fe000,   0x31dc,
	0x201800,   0x31dc,
	0x239800,   0x335c,
	0x23d000,   0x335c,
	0x240800,   0x335c,
	0x244000,   0x335c,
	0x247800,   0x335c,
	0x27f800,   0x2df4,
	0x28b800,   0x2df4,
	0x28e800,   0x2df4,
	0x294800,   0x2df4,
	0x2d9800,   0x12904,		
	0x2fa800,   0x12904,
	0x304800,   0x12904,
	0x2ec800,   0x12904,
	
	
	0x114800,   0x1000c,
	
	/*
	0x282800,   0x2df4,
	0x285800,   0x2df4,
	0x288800,   0x2df4,
	0x291800,   0x2df4,
	0x297800,   0x2df4,
	0x29A800,   0x2df4,
	0x29D800,   0x2df4,
	0x2a0800,   0x2df4,*/
};

//r10 = 0xb17;
uint64 DecodeTycoonImages(uint8* inDataStream, uint32 inKey)
{
	uint32 r0 = 0;
	uint32 r9 = 0xAAAA5555;
	uint32 r8 = 0xAC53AC53;
	uint32 r6 = SwapByteOrder(*(uint32*)inDataStream);
	uint32 r5 = SwapByteOrder(((uint32*)inDataStream)[1]);
	const uint8* startAddress = inDataStream;

	while(1) //LAB_0600ff06
	{
		uint32 r4 = inKey;
		if(r4 < 0)
		{
			r4 += 0x09;
		}

		//LAB_0600ff10
		r4 = r4 >> 8;
		uint32 r2 = r6;
		if(++r4 <= r0)
		{
			break;
		}

		uint32 numBytesProcessed = 0;
		const uint32 dataBlockSize = 0x3f;

		do //LAB_0600ff22
		{
			uint32 r1 = SwapByteOrder(*(uint32*)inDataStream);
			++numBytesProcessed;
			r1 = r2 ^ r1;
			(*(uint32*)inDataStream) = SwapByteOrder(r1);
			inDataStream += 4;
			r2 = r2 ^ r9;
			r2 += r8;
		} while (numBytesProcessed <= dataBlockSize);

		r2 = r6;
		r6 = r6 ^ 0x13579BDF;
		r6 += r5;
		r5 = r2;
		++r0;
	}

	return (inDataStream - startAddress)  + sizeof(uint32); //+sizeof(uint32) because the size includes the full 4 final bytes
}

void ExtractTycoonIntroImages(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	std::string outputDirectory = inOutputDirectory + "IntroImages\\";
	const std::string binOutputDirectory = inOutputDirectory + "IntroImages\\BIN\\";
	CreateDirectoryHelper(outputDirectory);
	CreateDirectoryHelper(binOutputDirectory);

	const std::string imageExt = bInBmp ? ".bmp" : ".png";

	if(!bInBmp)
	{
		outputDirectory = outputDirectory + "PNG\\";
		CreateDirectoryHelper(outputDirectory);
	//	CreateSpreadSheetForImages("TrumpWars", "TrumpWars\\PNG\\", outputDirectory, "TrumpWars.php");
	}

	//CARD_DAT
	const std::string cardFilePath = inRootDirectory + "SAKURA3\\CARD_DAT.ALL";
	FileData cardFile;
	if (!cardFile.InitializeFileData(cardFilePath))
	{
		return;
	}

	const std::string vdp2Image1 = inOutputDirectory + "VDP2Image1" + imageExt;
	ExtractImageWithTileSet<uint32>(cardFilePath, 192, 64, cardFilePath, 0xd4800, 0xd3020, 0xd5000, vdp2Image1, bInBmp, 4, ETileIndiceType::MinusOne);

	//0000DAIF - Has key data there, but its convoluted, so just manually define the key table (GTycoonKeyEntries)
	const std::string daifFilePath = inRootDirectory + "SAKURA3\\0000DAIF.BIN";
	FileData daifFile;
	if (!daifFile.InitializeFileData(daifFilePath))
	{
		return;
	}
	const int numKeys = 183;
	TycoonOffsetKeyInfo keys[numKeys];
	daifFile.ReadData(0x46df8, (char*)keys, sizeof(keys), false);
	for( int i = 0; i < numKeys; ++i )
	{
		keys[i].FixupAfterLoading();
	}

	const uint32 fileSize = (uint32)cardFile.GetDataSize();
	const uint32 bufferSize = 0x2c000;
	char* buffer = new char[bufferSize];
	//uint32 dataSetOffset = 0x002d9800;//0x1000;
	
	const int numEntries = numKeys;//sizeof(GTycoonKeyEntries)/sizeof(GTycoonKeyEntries[0]);

	FileWriter decodedDataFile;
	for (int k = 0; k < numEntries; ++k)
	{
		if(bInBmp)
		{
			const std::string decodedFilePath = binOutputDirectory + std::to_string(k) + ".bin";
			if (!decodedDataFile.OpenFileForWrite(decodedFilePath))
			{
				break;
			}
		}
		
		const uint32 dataSetOffset = keys[k].offset;//GTycoonKeyEntries[k].offset;
		const uint32 copySize = dataSetOffset + bufferSize > cardFile.GetDataSize() ? cardFile.GetDataSize() - dataSetOffset : bufferSize;
		memcpy_s(buffer, copySize, cardFile.GetData() + dataSetOffset, copySize);

		const uint32 decodedSize = DecodeTycoonImages((uint8*)buffer, keys[k].key);//GTycoonKeyEntries[k].key);
		if(decodedSize > bufferSize)
		{
			assert(decodedSize < bufferSize);
		}

		const uint32 offsetToImageTable = SwapByteOrder(*((uint32*)(buffer + 0x20))); //always 0x1110?
		const uint32 offsetToPaletteData = 0x110;
		const int paletteOffset = 0x110;

		if(bInBmp)
		{
			decodedDataFile.WriteData(buffer, decodedSize, false);
			decodedDataFile.Close();
		}

		if(offsetToImageTable < decodedSize)
		{
			const uint16 numImagesInSet = SwapByteOrder(*(uint16*)(buffer + offsetToImageTable));
			uint32 imageOffset = offsetToImageTable + numImagesInSet * 16 + 16;
			for (uint32 i = 0; i < numImagesInSet; ++i)
			{
				const uint32 offsetToNextImageInfo = offsetToImageTable + 16 + i * 16;
				const uint16 imageWidth            = SwapByteOrder(*(uint16*)(buffer + offsetToNextImageInfo));
				const uint16 imageHeight           = SwapByteOrder(*(uint16*)(buffer + offsetToNextImageInfo + 2));
				const uint16 bitSize               = SwapByteOrder(*(uint16*)(buffer + offsetToNextImageInfo + 4));
				const uint16 paletteOffset         = SwapByteOrder(*(uint16*)(buffer + offsetToNextImageInfo + 6))*2 + offsetToPaletteData;
				const bool bIs4Bit                 = bitSize == 0;
				const uint32 imageSize             = bIs4Bit ? (imageWidth * imageHeight) >> 1 : imageWidth * imageHeight;

				const std::string imageFileName = outputDirectory + std::to_string(k) + std::string("_") + IntToHexString(dataSetOffset) + std::string("_") + std::to_string(i) + imageExt;
				ExtractImageFromData(buffer + imageOffset, imageSize, imageFileName, buffer + paletteOffset, 512, bIs4Bit, imageWidth, imageHeight, 1, 256, 0, true, bInBmp);

				imageOffset += imageSize;
			}
		}
	}

	delete[] buffer;
}

void ExtractTycoonRulesScreen(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	CreateDirectoryHelper(inOutputDirectory);

	ExtractTycoonIntroImages(inRootDirectory, inOutputDirectory, bInBmp);

	//CARD_DAT
	const std::string cardFilePath = inRootDirectory + "SAKURA3\\CARD_DAT.ALL";
	FileData cardFile;
	if (!cardFile.InitializeFileData(cardFilePath))
	{
		return;
	}

	const uint32 tileOffset     = 0x166800;
	const uint32 colorOffset    = 0x155040;
	const uint32 palettteOffset = 0x168000;
	SakuraFontSheet tileSheet;
	if (!tileSheet.CreateFontSheetFromData(cardFile.GetData() + colorOffset, 40*28*64, 8, 8, false))
	{
		return;
	}

	//Create 32bit palette data
	PaletteData paletteData;
	paletteData.CreateFrom15BitData(cardFile.GetData() + palettteOffset, 512);

	BitmapSurface sakuraStringBmp;
	sakuraStringBmp.CreateSurface(320, 224, BitmapSurface::EBitsPerPixel::kBPP_8, paletteData.GetData(), paletteData.GetSize());

	unsigned int tiles[1120];
	memcpy_s(tiles, sizeof(tiles), cardFile.GetData() + tileOffset, sizeof(tiles));
	
	//Convert tile index into game format
	for (int t = 0; t < 1120; ++t)
	{
		const int tileValue = (SwapByteOrder(tiles[t]) / 2) - 1;
		tiles[t] = tileValue;
	}

	//Output tiles
	int x = 0;
	int y = 0;
	for (int t = 0; t < 1120; ++t)
	{
		SakuraString::SakuraChar sakuraChar;
		sakuraChar.mIndex = tiles[t];

		const char* pTileData = tileSheet.GetCharacterTile(sakuraChar);
		sakuraStringBmp.AddTile(pTileData, 64, x, y, 8, 8, BitmapSurface::kFlipNone);

		x += 8;
		if (x == 320)
		{
			x = 0;
			y += 8;
		}
	}

	const std::string imageExt = bInBmp ? ".bmp" : ".png";
	const std::string outFile = inOutputDirectory + "TiledImage" + imageExt;
	sakuraStringBmp.WriteToFile(outFile, true);	

	printf("Created %s\n", outFile.c_str());
}

bool PatchTycoonRulesScreen(const std::string& inPatchedSakuraDirectory, const std::string& inTranslatedDataDirectory)
{
	const std::string cardFilePath = inPatchedSakuraDirectory + "SAKURA3\\CARD_DAT.ALL";
	FileReadWriter cardFileData;
	if (!cardFileData.OpenFile(cardFilePath))
	{
		return false;
	}

	const std::string patchedImagePath = inTranslatedDataDirectory + std::string("Tycoon\\Rules.bmp");
	const uint32 tileDim = 8;
	BmpToSaturnConverter patchedImage;
	if (!patchedImage.ConvertBmpToSakuraFormat(patchedImagePath, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim))
	{
		return false;
	}

	TileSetOptimizer optimizedTileSet;
	optimizedTileSet.OptimizeTileSet(patchedImage);
	optimizedTileSet.PackTiles();

	std::vector<int> packedIndices;
	optimizedTileSet.GetTiledIndicesInSaturnFormat(packedIndices);

	cardFileData.WriteData(0x155040, optimizedTileSet.GetPackedTiles(), optimizedTileSet.GetPackedTileSize(), false);
	cardFileData.WriteData(0x166800, (char*)packedIndices.data(), packedIndices.size()*sizeof(int), false);

	return true;
}

bool PatchTycoonImages(const std::string& inPatchedSakuraDirectory, const std::string& inTranslatedDataDirectory)
{
	//CARD_DAT
	const std::string cardFilePath = inPatchedSakuraDirectory + "SAKURA3\\CARD_DAT.ALL";
	FileReadWriter cardFile;
	if (!cardFile.OpenFile(cardFilePath))
	{
		return false;
	}

	const std::string tycoonImageDir = inTranslatedDataDirectory + "Tycoon\\";

	const std::string imageExt(".bmp");
	const uint32 bufferSize = 0x2c000;
	char* buffer = new char[bufferSize];
	
	const int numEntries = sizeof(GTycoonKeyEntries)/sizeof(GTycoonKeyEntries[0]);

	for (int k = 0; k < numEntries; ++k)
	{
		const uint32 dataSetOffset = GTycoonKeyEntries[k].offset;
		const uint32 copySize = dataSetOffset + bufferSize > cardFile.GetFileSize() ? cardFile.GetFileSize() - dataSetOffset : bufferSize;
		cardFile.ReadData(dataSetOffset, buffer, copySize, false);
		
		const uint32 decodedSize = DecodeTycoonImages((uint8*)buffer, GTycoonKeyEntries[k].key);
		if(decodedSize > bufferSize)
		{
			assert(decodedSize < bufferSize);
		}

		//Create patched data buffer
		const uint32 offsetToImageTable = SwapByteOrder(*((uint32*)(buffer + 0x20)));
		if(offsetToImageTable < decodedSize)
		{
			const uint16 numImagesInSet = SwapByteOrder(*(uint16*)(buffer + offsetToImageTable));
			uint32 imageOffset = offsetToImageTable + numImagesInSet * 16 + 16;
			for (uint32 i = 0; i < numImagesInSet; ++i)
			{
				const uint32 offsetToNextImageInfo = offsetToImageTable + 16 + i * 16;
				const uint16 imageWidth            = SwapByteOrder(*(uint16*)(buffer + offsetToNextImageInfo));
				const uint16 imageHeight           = SwapByteOrder(*(uint16*)(buffer + offsetToNextImageInfo + 2));
				const uint16 bitSize               = SwapByteOrder(*(uint16*)(buffer + offsetToNextImageInfo + 4));
				const bool bIs4Bit                 = bitSize == 0;
				const uint32 imageSize             = bIs4Bit ? (imageWidth * imageHeight) >> 1 : imageWidth * imageHeight;

				const std::string imageFileName = tycoonImageDir + std::to_string(k) + std::string("_") + IntToHexString(dataSetOffset) + std::string("_") + std::to_string(i) + imageExt;
				BmpToSaturnConverter patchedImage;
				if (patchedImage.ConvertBmpToSakuraFormat(imageFileName, false, BmpToSaturnConverter::CYAN, nullptr, nullptr, false))
				{
					if( imageSize != patchedImage.GetImageDataSize() )
					{
						printf("Image size mismatch for %s. Expected %i, got %i\n", imageFileName.c_str(), imageSize, patchedImage.GetImageDataSize());
						return false;
					}

					memcpy_s(buffer + imageOffset, imageSize, patchedImage.GetImageData(), imageSize);
				}
				
				imageOffset += imageSize;
			}
		}

		//Encode data buffer
		vector<uint32> encodedData;
		EncodeKinematronData((uint32*)buffer, decodedSize, encodedData);
	
		const uint32 encodedSize = encodedData.size() * sizeof(uint32);
		if(encodedSize != decodedSize)
		{
			printf("PatchTycoonImages: Re-encoded data size is not the same as the original encoded data size. Expected %i, got %i \n", decodedSize, encodedSize);
			return false;
		}

		cardFile.WriteData(dataSetOffset, (char*)encodedData.data(), encodedSize);
	}

	delete[] buffer;

	return true;
}

bool PatchTycoon(const std::string& inPatchedSakuraDirectory, const std::string& inTranslatedDataDirectory)
{
	printf("Patching Tycoon\n");

	if( !PatchTycoonRulesScreen(inPatchedSakuraDirectory, inTranslatedDataDirectory) )
	{
		return false;
	}

	return PatchTycoonImages(inPatchedSakuraDirectory, inTranslatedDataDirectory);
}
