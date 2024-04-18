#pragma once

struct NBGFileHeader
{
	uint32 unknown;
	uint32 unknown2;
	uint32 imageWidth;
	uint32 imageHeight;
	uint32 paletteOffset;
	uint32 paletteSize;
	uint32 entrySize;
	uint32 lastValue;
	char unknown3[64];

	void SwapEndianness()
	{
		unknown = SwapByteOrder(unknown);
		unknown2 = SwapByteOrder(unknown2);
		imageWidth  = SwapByteOrder(imageWidth);
		imageHeight = SwapByteOrder(imageHeight);
		paletteOffset = SwapByteOrder(paletteOffset);
		paletteSize = SwapByteOrder(paletteSize);
		entrySize   = SwapByteOrder(entrySize);
		lastValue   = SwapByteOrder(lastValue);
	}
};

void ExtractNBGFile(const std::string& inFilePath, const std::string& inOutputDirectory, bool bInBmp)
{
	FileData fileData;
	if(!fileData.InitializeFileData(inFilePath))
	{
		return;
	}

	CreateDirectoryHelper(inOutputDirectory);

	const std::string imgExt = bInBmp ? ".bmp" : ".png";

	const unsigned long fileSize = fileData.GetDataSize();
	unsigned long currFileOffset = 0;
	int imageIndex = 0;
	std::vector<NBGFileHeader> headers;
	while(currFileOffset < fileSize)
	{
		NBGFileHeader header;
		fileData.ReadData(currFileOffset, (char*)&header, sizeof(header), false);
		header.SwapEndianness();

		if(header.imageHeight != 144 || header.imageWidth != 288 || header.paletteSize != 512)
		{
			while(header.unknown != 0x20)
			{
				currFileOffset += 16;

				if(currFileOffset >= fileSize)
				{
					return;
				}

				fileData.ReadData(currFileOffset, (char*)&header, sizeof(header), false);
				header.SwapEndianness();
			}
		}

		printf("Outputting %i from 0x%08x\n", imageIndex, currFileOffset + sizeof(header));

		headers.push_back(header);
		const string bitmapFileName = inOutputDirectory + std::to_string(imageIndex++) + ".bmp";

		ExtractImageFromData(fileData.GetData() + currFileOffset + sizeof(header), header.imageWidth*header.imageHeight, bitmapFileName, fileData.GetData() + currFileOffset + header.paletteOffset, 512, false, 8, 8, header.imageWidth / 8, 256, 0, true, bInBmp);

		currFileOffset += 0xa800;
	}
}

bool PatchNBGFile(const std::string& inPatchedSakuraDirectory, const std::string& inTranslatedDataDirectory, int inDiscNumber)
{
	printf("Patch NBGFile\n");

	std::set<int> imagesToPatch;
	std::string nbgFilePath;
	if(inDiscNumber == 1)
	{
		nbgFilePath = inPatchedSakuraDirectory + "SAKURA1\\NBGFILE1.ALL";
		imagesToPatch.insert(342);
		imagesToPatch.insert(343);
		imagesToPatch.insert(344);
		imagesToPatch.insert(345);
		imagesToPatch.insert(346);
		imagesToPatch.insert(370);
	}
	else if(inDiscNumber == 2)
	{
		nbgFilePath = inPatchedSakuraDirectory + "SAKURA1\\NBGFILE2.ALL";
		imagesToPatch.insert(342);
		imagesToPatch.insert(343);
		imagesToPatch.insert(344);
		imagesToPatch.insert(345);
		imagesToPatch.insert(346);
		imagesToPatch.insert(370);
	}
	else
	{
		//Nothing to patch for disc 3
		return true; 
	//	nbgFilePath = inPatchedSakuraDirectory + "SAKURA1\\NBGFILE3.ALL";
	}

	FileReadWriter nbgFile;
	if(!nbgFile.OpenFile(nbgFilePath))
	{
		return false;
	}

	//Images are all the same, so just use sames images from Disc1 directory
	const uint32 tileDim = 8;
	const std::string translatedImageDir = inTranslatedDataDirectory + std::string("NBGFiles") + std::string("\\Disc1\\");

	const unsigned long fileSize = nbgFile.GetFileSize();
	unsigned long currFileOffset = 0;
	int imageIndex = 0;
	std::vector<NBGFileHeader> headers;
	while (currFileOffset < fileSize)
	{
		NBGFileHeader header;
		nbgFile.ReadData(currFileOffset, (char*)&header, sizeof(header), false);
		header.SwapEndianness();

		if (header.imageHeight != 144 || header.imageWidth != 288 || header.paletteSize != 512)
		{
			while (header.unknown != 0x20)
			{
				currFileOffset += 16;

				if (currFileOffset >= fileSize)
				{
					return true;
				}

				nbgFile.ReadData(currFileOffset, (char*)&header, sizeof(header), false);
				header.SwapEndianness();
			}
		}

		//See if this image should be patched
		if(imagesToPatch.find(imageIndex) == imagesToPatch.end())
		{
			currFileOffset += 0xa800;
			++imageIndex;
			continue;
		}

		//Convert image to sw2 format
		const std::string translatedImage = translatedImageDir + std::to_string(imageIndex++) + ".bmp";
		BmpToSaturnConverter patchedImageData;
		if (!patchedImageData.ConvertBmpToSakuraFormat(translatedImage, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim))
		{
			return false;
		}
		patchedImageData.PackTiles();

		if(patchedImageData.GetImageWidth() != header.imageWidth || patchedImageData.GetImageHeight() != header.imageHeight)
		{
			printf("Was expecting dimensions %ix%i, got %ix%i for %s\n", header.imageWidth, header.imageHeight, patchedImageData.GetImageWidth(), patchedImageData.GetImageHeight(), translatedImage.c_str());
			return false;
		}

		nbgFile.WriteData(currFileOffset + sizeof(header), patchedImageData.mpPackedTiles, patchedImageData.mPackedTileSize, false);

		currFileOffset += 0xa800;
	}

	return true;
}

