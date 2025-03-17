#pragma once

KinematronEncodingKeyInfo GTycoonKeyEntries[] =
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
	
	//Found later
	0x114800,   0x1000c,
};

//r10 = 0xb17;
//Same as DecodeKinematronData
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

void ExtractTycoonIntroImages(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp, bool bInFullExtraction)
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

	//0000DAIF - Has key data there
	const std::string daifFilePath = inRootDirectory + "SAKURA3\\0000DAIF.BIN";
	FileData daifFile;
	if (!daifFile.InitializeFileData(daifFilePath))
	{
		return;
	}
	const int totalKeys = 183;
	KinematronEncodingKeyInfo keys[totalKeys];
	daifFile.ReadData(0x46df8, (char*)keys, sizeof(keys), false);
	for( int i = 0; i < totalKeys; ++i )
	{
		keys[i].FixupAfterLoading();
	}

	const uint32 fileSize = (uint32)cardFile.GetDataSize();
	const uint32 bufferSize = 0x2c000;
	char* buffer = new char[bufferSize];
	//uint32 dataSetOffset = 0x002d9800;//0x1000;
	
	const int numEntries = bInFullExtraction ? totalKeys : sizeof(GTycoonKeyEntries)/sizeof(GTycoonKeyEntries[0]);

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
		
		const uint32 dataSetOffset = bInFullExtraction ? keys[k].offset : GTycoonKeyEntries[k].offset;
		const uint32 copySize = dataSetOffset + bufferSize > cardFile.GetDataSize() ? cardFile.GetDataSize() - dataSetOffset : bufferSize;
		memcpy_s(buffer, copySize, cardFile.GetData() + dataSetOffset, copySize);

		const uint32 decodedSize = bInFullExtraction ? DecodeKinematronData((uint32*)buffer, keys[k].key) : DecodeKinematronData((uint32*)buffer, GTycoonKeyEntries[k].key);
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
				
				const char* pPalette = buffer + paletteOffset;
				
				if(k == 10)
				{
					if(i == 19)
						pPalette = daifFile.GetData() + 0x00048324;
					else if(i == 39)				
						pPalette = daifFile.GetData() + 0x00048c44;
					else if(i == 43)
						pPalette = daifFile.GetData() + 0x000489c4;
				}
				ExtractImageFromData(buffer + imageOffset, imageSize, imageFileName, pPalette, 512, bIs4Bit, imageWidth, imageHeight, 1, 256, 0, true, bInBmp);

				imageOffset += imageSize;
			}
		}
	}

	delete[] buffer;
}

void ExtractTycoonSplashScreen(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	const std::string cardFilePath = inRootDirectory + "SAKURA3\\CARD_DAT.ALL";
	ExtractTiledFullScreenImage<uint32>(cardFilePath, cardFilePath, 0x6b000, 0x6a020, 0x6c800, inOutputDirectory + "SplashScreen.bmp", bInBmp, 4, ETileIndiceType::MinusOne);
}

void ExtractTycoonRulesScreen(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp, const bool bInFullExtraction)
{
	CreateDirectoryHelper(inOutputDirectory);

	ExtractTycoonIntroImages(inRootDirectory, inOutputDirectory, bInBmp, bInFullExtraction);

	ExtractTycoonSplashScreen(inRootDirectory, inOutputDirectory, bInBmp);

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

bool PatchTycoonWarsLogoLocation(char* InData)
{
	//Patch location of "Tycoon Wars" so that Tycoon is on the left
	uint8& tycoonX = *(uint8*)(InData + 0x973d);
	uint8& warsX1  = *(uint8*)(InData + 0x975d);
	uint8& warsX2  = *(uint8*)(InData + 0x977d);

	if(tycoonX != 0x84 )
	{
		printf("Patching Tycoon Error: Expected 0x84 for position of Tycoon, but got 0x%02x", tycoonX);
		return false;
	}

	if(warsX1 != 0x3a )
	{
		printf("Patching Tycoon Error: Expected 0x3a for position of W, but got 0x%02x", warsX1);
		return false;
	}

	if(warsX2 != 0x3a )
	{
		printf("Patching Tycoon Error: Expected 0x3a for position of ars, but got 0x%02x", warsX2);
		return false;
	}

	tycoonX = 0x3a;
	warsX1 = 0x64;
	warsX2 = 0x64;

	return true;
}

bool PatchVeryRichLocation(char* InData)
{
	//Patch location of "Tycoon Wars" so that Tycoon is on the left
	uint8& veryX    = *(uint8*)(InData + 0xe9fd);
	uint8& richX    = *(uint8*)(InData + 0xea1d);
	uint8& exclaim1 = *(uint8*)(InData + 0xea5d);
	uint8& exclaim2 = *(uint8*)(InData + 0xea7d);

	if(veryX != 0x00 )
	{
		printf("Patching VeryRich Error: Expected 0x84 for position of Very, but got 0x%02x\n", veryX);
		return false;
	}

	if(richX != 0x40 )
	{
		printf("Patching VeryRich Error: Expected 0x40 for position of Rich, but got 0x%02x\n", richX);
		return false;
	}

	if(exclaim1 != 0xc4 )
	{
		printf("Patching VeryRich Error: Expected 0xc4 for position of !1, but got 0x%02x\n", exclaim1);
		return false;
	}

	if(exclaim2 != 0xd7 )
	{
		printf("Patching VeryRich Error: Expected 0xd7 for position of !2, but got 0x%02x\n", exclaim2);
		return false;
	}

	veryX = 0x20;
	richX = 0x60;
	exclaim1 = 0xa0;
	exclaim2 = 0xaa;

	return true;
}

bool PatchTycoonRichLocation(char* InData)
{
	//Patch location of "Poor" so that it is centered
	uint8& richX = *(uint8*)(InData + 0xea9d);
	uint8& exclaimX = *(uint8*)(InData + 0xeadd);

	if (richX != 0x18)
	{
		printf("Patching Tycoon Error: Expected 0x18 for position of Rich, but got 0x%02x", richX);
		return false;
	}

	if (exclaimX != 0xb8)
	{
		printf("Patching Tycoon Error: Expected 0xb8 for position of (Rich)!, but got 0x%02x", exclaimX);
		return false;
	}

	richX = 0x40;
	exclaimX = 0x7c;

	return true;
}

bool PatchDownFallLocation(char* InData)
{
	//Patch location of "DownFall" so that DownFall are together
	uint8& downX = *(uint8*)(InData + 0xeddd);
	uint8& dot1X = *(uint8*)(InData + 0xed7d);
	uint8& dot1Y = *(uint8*)(InData + 0xed7f);
	uint8& dot2X = *(uint8*)(InData + 0xed9d);
	uint8& dot2Y = *(uint8*)(InData + 0xed9f);
	uint8& dot3X = *(uint8*)(InData + 0xedbd);
	uint8& dot3Y = *(uint8*)(InData + 0xedbf);

	if(downX != 0x12 )
	{
		printf("Patching DownFall Error: Expected 0x12 for position of Tycoon, but got 0x%02x\n", downX);
		return false;
	}

	if(dot1X != 0xb4)
	{
		printf("Patching DownFall. Error: Expected 0xb4 for position of .1, but got 0x%02x\n", dot1X);
		return false;
	}

	if(dot2X != 0xc8)
	{
		printf("Patching DownFall.. Error: Expected 0xc8 for position of .2, but got 0x%02x\n", dot2X);
		return false;
	}

	if(dot3X != 0xdc)
	{
		printf("Patching DownFall... Error: Expected 0xdc for position of .3, but got 0x%02x\n", dot3X);
		return false;
	}

	downX = 0x1c;
	dot1X = 0xaa;
	dot1Y = 0x2a;
	dot2X = 0xba;
	dot2Y = 0x2a;
	dot3X = 0xca;
	dot3Y = 0x2a;

	return true;
}

bool PatchTycoonPoorLocation(char* InData)
{
	//Patch location of "Poor" so that it is centered
	uint8& poorX = *(uint8*)(InData + 0xeafd);

	if (poorX != 0x18)
	{
		printf("Patching Tycoon Error: Expected 0x18 for position of Poor, but got 0x%02x", poorX);
		return false;
	}

	poorX = 0x50;

	return true;
}

bool PatchVeryPoorLocation(char* InData)
{
	//Patch location of "DownFall" so that DownFall are together
	uint8& veryX = *(uint8*)(InData + 0xeb3d);
	uint8& poorX = *(uint8*)(InData + 0xeb5d);
	uint8& dot1X = *(uint8*)(InData + 0xeb9d);
	uint8& dot1Y = *(uint8*)(InData + 0xeb9f);
	uint8& dot2X = *(uint8*)(InData + 0xebbd);
	uint8& dot2Y = *(uint8*)(InData + 0xebbf);
	uint8& dot3X = *(uint8*)(InData + 0xebdd);
	uint8& dot3Y = *(uint8*)(InData + 0xebdf);

	if(veryX != 0 )
	{
		printf("Patching Very Error: Expected 0 for position of Very, but got 0x%02x\n", veryX);
		return false;
	}

	if(poorX != 0x40 )
	{
		printf("Patching Poor Error: Expected 0x40 for position of Poor, but got 0x%02x\n", poorX);
		return false;
	}

	if(dot1X != 0xc4)
	{
		printf("Patching Very Poor. Error: Expected 0xc4 for position of .1, but got 0x%02x\n", dot1X);
		return false;
	}

	if(dot2X != 0xd8)
	{
		printf("Patching Very Poor.. Error: Expected 0xd8 for position of .2, but got 0x%02x\n", dot2X);
		return false;
	}

	if(dot3X != 0xec)
	{
		printf("Patching Very Poor... Error: Expected 0xec for position of .3, but got 0x%02x\n", dot3X);
		return false;
	}

	veryX = 0x10;
	poorX = 0x50;
	dot1X = 0x90;
	dot1Y = 0x20;
	dot2X = 0xa0;
	dot2Y = 0x20;
	dot3X = 0xb0;
	dot3Y = 0x20;

	return true;
}

static const bool UpdateTycoonValue(char* InData, uint32 InOffset, uint8 InNew, uint8 InOld)
{
	if((uint8)InData[InOffset] != InOld)
	{
		printf("UpdateTycoonValue: At %0x08x, Expected: 0x%01x, Found: 0x%01x\n", InOffset, InOld, InData[InOffset]);
		return false;
	}

	uint8& value = *(uint8*)(InData + InOffset);
	value = InNew;

	return true;
}

//From TrekkiesUnite118
bool PatchTycoonIntroLogoOrientation(char* InData)
{
	bool bResult = true;

	bResult = UpdateTycoonValue(InData, 0x1161, 0x38, 0x40);
	bResult = UpdateTycoonValue(InData, 0x1163, 0x30, 0x38);
	bResult = UpdateTycoonValue(InData, 0x116a, 0x2a, 0x1c);
	bResult = UpdateTycoonValue(InData, 0x116e, 0x0a, 0x0e);
	bResult = UpdateTycoonValue(InData, 0x116f, 0x80, 0x00);

	bResult = UpdateTycoonValue(InData, 0x1171, 0x40, 0x38);
	bResult = UpdateTycoonValue(InData, 0x1173, 0x38, 0x30);
	bResult = UpdateTycoonValue(InData, 0x117a, 0x1c, 0x2a);
	bResult = UpdateTycoonValue(InData, 0x117e, 0x0e, 0x0a);
	bResult = UpdateTycoonValue(InData, 0x117f, 0x00, 0x80);

	bResult = UpdateTycoonValue(InData, 0xafcf, 0x35, 0x45);
	bResult = UpdateTycoonValue(InData, 0xafed, 0x3b, 0x38);
	bResult = UpdateTycoonValue(InData, 0xafef, 0x55, 0x56);

	bResult = UpdateTycoonValue(InData, 0xb00d, 0x58, 0x48);
	bResult = UpdateTycoonValue(InData, 0xb00f, 0x58, 0x56);
	bResult = UpdateTycoonValue(InData, 0xb02d, 0x74, 0x60);
	bResult = UpdateTycoonValue(InData, 0xb02f, 0x55, 0x52);
	bResult = UpdateTycoonValue(InData, 0xb04d, 0x91, 0x80);
	bResult = UpdateTycoonValue(InData, 0xb04f, 0x4c, 0x44);
	bResult = UpdateTycoonValue(InData, 0xb06d, 0xb8, 0xb0);
	bResult = UpdateTycoonValue(InData, 0xb06f, 0x44, 0x4c);

	return bResult;

}

bool PatchTycoonEncodedImages(const std::string& inPatchedSakuraDirectory, const std::string& inTranslatedDataDirectory)
{
	//CARD_DAT
	const std::string cardFilePath = inPatchedSakuraDirectory + "SAKURA3\\CARD_DAT.ALL";
	const std::string tycoonImageDir = inTranslatedDataDirectory + "Tycoon\\";

	const std::string vdp2Image1 = tycoonImageDir + "VDP2Image1.bmp";
	if( !PatchTiledImage<uint32>(vdp2Image1, cardFilePath, 192 * 64 / 2, 0xd3020, 0xd4800, (192 / 8) * (64 / 8), ETileIndiceType::MinusOne) )
	{
		return false;
	}

	auto OnBlockDecoded = [](int InBlockNumber, char* InDecodedData, uint32 InDataSize)->bool
	{
		if(InBlockNumber == 1)
		{
			if(!PatchTycoonWarsLogoLocation(InDecodedData))
			{
				return false;
			}
		}
		else if(InBlockNumber == 2)
		{
			if(!PatchTycoonIntroLogoOrientation(InDecodedData))
			{
				return false;
			}
		}
		else if(InBlockNumber == 10)
		{
			bool bResult = true;
			bResult &= PatchVeryRichLocation(InDecodedData);
			bResult &= PatchTycoonRichLocation(InDecodedData);
			bResult &= PatchDownFallLocation(InDecodedData);
			bResult &= PatchTycoonPoorLocation(InDecodedData);
			bResult &= PatchVeryPoorLocation(InDecodedData);

			if(!bResult)
			{
				return false;
			}
		}

		return true;
    };

	return PatchKinematronEncodedImages(cardFilePath, tycoonImageDir, GTycoonKeyEntries, sizeof(GTycoonKeyEntries) / sizeof(GTycoonKeyEntries[0]), OnBlockDecoded);
}

bool PatchTycoonSplashScreen(const std::string& inPatchedSakuraDirectory, const std::string& inTranslatedDataDirectory)
{
	const int maxTiles = 75;

	//Patch Tycoon splash screen
	if (!PatchTiledImage<uint32>(inTranslatedDataDirectory + "Tycoon\\TycoonSplash.bmp", inPatchedSakuraDirectory + "SAKURA3\\CARD_DAT.ALL", (320 * 244) / 2, 0x6a020, 0x6b000, 1120, ETileIndiceType::MinusOne, maxTiles))
	{
		return false;
	}

	//Original game only expects 67 tiles.  We need to increase this to 76
	{
		const string daifFilePath = inPatchedSakuraDirectory + "SAKURA3\\0000DAIF.BIN";
		FileData daifFileData;
		if(!daifFileData.InitializeFileData(daifFilePath))
		{
			return false;
		}

		FileReadWriter daifFile;
		if (!daifFile.OpenFile(daifFilePath))
		{
			return false;
		}

		//Update where in VDP2 ram our images reside.  We need more space, so move the address a little.
		{
			const int vdp2TileSetAddressOriginal = 0x25e40000;
			const int vdp2TileSetAddress = 0x25e3fe00;
		
			// Verify we are writing to the correct place in the file
			int originalVdp2Address = 0;
			daifFileData.ReadData(0x139a4, (char*)&originalVdp2Address, sizeof(originalVdp2Address), true);
			if(originalVdp2Address != vdp2TileSetAddressOriginal)
			{
				printf("PatchTycoonSplashScreen: At 0x139a4 in 0000DAIF.BIN, expected vdp2 address to be 0x%08x, got 0x%08x\n", vdp2TileSetAddressOriginal, originalVdp2Address);
				return false;
			}

			//Update the address now
			daifFile.WriteData(0x139a4, (char*)&vdp2TileSetAddress, sizeof(vdp2TileSetAddress), true);
		}

		//Update number of bytes in the tile set
		{
			const uint16 numBytesInTileSetOriginal = 0x880;
			const uint16 numBytesInTileSet = (maxTiles + 1) * 8 * 4; //Game divides this value by 4, so multiply it in here
			
			// Verify we are writing to the correct place in the file
			uint16 originalSize = 0;
			daifFileData.ReadData(0x46e46, (char*)&originalSize, sizeof(originalSize), true);
			if (originalSize != numBytesInTileSetOriginal)
			{
				printf("PatchTycoonSplashScreen: At 0x139a4 in 0000DAIF.BIN, expected tile size to be to be 0x%04x, got 0x%04x\n", numBytesInTileSetOriginal, originalSize);
				return false;
			}

			daifFile.WriteData(0x46e46, (char*)&numBytesInTileSet, sizeof(numBytesInTileSet), true);
		}
	}

	return true;
}

bool PatchTycoon(const std::string& inPatchedSakuraDirectory, const std::string& inTranslatedDataDirectory)
{
	printf("Patching Tycoon\n");

	if( !PatchTycoonRulesScreen(inPatchedSakuraDirectory, inTranslatedDataDirectory) )
	{
		return false;
	}

	if(!PatchTycoonSplashScreen(inPatchedSakuraDirectory, inTranslatedDataDirectory))
	{
		return false;
	}

	return PatchTycoonEncodedImages(inPatchedSakuraDirectory, inTranslatedDataDirectory);
}
