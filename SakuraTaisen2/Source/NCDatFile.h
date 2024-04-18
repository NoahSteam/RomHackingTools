#pragma once

struct NCDatFileHeader
{
	uint32 unknown1;
	uint32 unknown2;
	uint32 numImages;
	uint32 imageTableSize;
	uint32 unknown3[8];
	uint32 offsetToPalette;
	uint32 offsetToImageTable;
	uint32 unknown4[4];
	uint32 offsetToImages;
	char   misc[52];

	void SwapEndianness()
	{
		unknown1           = SwapByteOrder(unknown1);
		unknown2           = SwapByteOrder(unknown2);
		numImages          = SwapByteOrder(numImages);
		imageTableSize     = SwapByteOrder(imageTableSize);
		offsetToImageTable = SwapByteOrder(offsetToImageTable);
		offsetToPalette    = SwapByteOrder(offsetToPalette);
		offsetToImages     = SwapByteOrder(offsetToImages);
	}
};

struct NCDatImageHeader
{
	uint16 width;
	uint16 height;
	uint16 unknown1;
	uint16 unknown2;
	uint32 offsetFromPrevImage;
	uint32 imageSize;

	void SwapEndianness()
	{
		width               = SwapByteOrder(width);
		height              = SwapByteOrder(height);
		unknown1            = SwapByteOrder(unknown1);
		unknown2            = SwapByteOrder(unknown2);
		offsetFromPrevImage = SwapByteOrder(offsetFromPrevImage);
		imageSize           = SwapByteOrder(imageSize);
	}
};

void ExtractNCDatFile(const std::string& inFilePath, const std::string& inOutputDirectory, bool bInBmp)
{
	FileData fileData;
	if (!fileData.InitializeFileData(inFilePath))
	{
		return;
	}

	CreateDirectoryHelper(inOutputDirectory);

	const std::string imgExt = bInBmp ? ".bmp" : ".png";

	const unsigned long fileSize = fileData.GetDataSize();
	unsigned long currFileOffset = 0;
	int batchIndex = 0;
	while (currFileOffset < fileSize)
	{
		NCDatFileHeader header;
		fileData.ReadData(currFileOffset, (char*)&header, sizeof(header), false);
		header.SwapEndianness();

		while (header.unknown1 != 0x100 || header.unknown2 != 0x200)
		{
			currFileOffset += 2;

			if (currFileOffset >= fileSize)
			{
				return;
			}

			fileData.ReadData(currFileOffset, (char*)&header, sizeof(header), false);
			header.SwapEndianness();
		}
		
		printf("Outputting Batch %i from 0x%08x\n", batchIndex, currFileOffset);

		//Read in image headers
		NCDatImageHeader* pImageHeaders = new NCDatImageHeader[header.numImages];
		fileData.ReadData(currFileOffset + header.offsetToImageTable, (char*)pImageHeaders, sizeof(NCDatImageHeader)*header.numImages, false);

		//Grab palette
		const char* pPalette = fileData.GetData() + currFileOffset + header.offsetToPalette;

		//Skip to image table
		currFileOffset += header.offsetToImages;

		int offsetToImage = 0;

		//Output each image
		for(unsigned long imageIndex = 0; imageIndex < header.numImages; ++imageIndex)
		{
			NCDatImageHeader& imageHeader = pImageHeaders[imageIndex];
			imageHeader.SwapEndianness();

			const string bitmapFileName = inOutputDirectory + std::to_string(batchIndex) + std::string("_") + std::to_string(imageIndex) + ".bmp";

			ExtractImageFromData(fileData.GetData() + currFileOffset, imageHeader.width * imageHeader.height, bitmapFileName, pPalette, 512, false, imageHeader.width, imageHeader.height, 1, 256, 0, true, bInBmp);

			currFileOffset += imageHeader.imageSize;
		}
		
		++batchIndex;
	}
}