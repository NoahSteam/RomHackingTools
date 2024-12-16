#pragma once

struct EDSTFHeader
{
	uint32 decodeKey1;
	uint32 decodeKey2;
	uint32 unknown3;
	uint32 colorDataSize;
	uint32 width;
	uint32 height;
	uint32 unknown4;
	uint32 unknown5;
	uint32 unknown6;
	uint32 unknown7;

	void ChangeByteOrder()
	{
		decodeKey1 = SwapByteOrder(decodeKey1);
		decodeKey2 = SwapByteOrder(decodeKey2);
		unknown3 = SwapByteOrder(unknown3);
		colorDataSize = SwapByteOrder(colorDataSize);
		unknown4 = SwapByteOrder(unknown4);
		width = SwapByteOrder(width);
		height = SwapByteOrder(height);
		unknown5 = SwapByteOrder(unknown5);
		unknown6 = SwapByteOrder(unknown6);
		unknown7 = SwapByteOrder(unknown7);
	}
};

struct EndingFileInfo
{
	const char* pFileName;
	const char* pOutputDir;
	const int offsetToKeys;
	const int numKeys;
};

EndingFileInfo GEndingFiles[2] =
{
	{"SAKURA1\\EDSTF.ALL", "RollingCredits\\", 0x30da0, 20},
	{"SAKURA1\\EDDATA.ALL", "Credits\\", 0x2fe80, 386}, //11a28, 3fe8
};

struct EndingFileKeyData
{
	uint32 blocksToDecode;
	uint32 unknown;

	void ChangeByeOrder()
	{
		blocksToDecode = SwapByteOrder(blocksToDecode);
		unknown = SwapByteOrder(unknown);
	}
};

void EncodeCreditsData(uint32 inFirstValue, const uint32* const pInDecodedData, const uint32 inDecodedDataSize, std::vector<uint32>& outEncodedData)
{
	uint32 prevKey = inFirstValue;
	uint32 decodedIndex = 0;
	uint32 secondValue = SwapByteOrder(pInDecodedData[1]);
	uint32 r0 = 0;
	const uint32 numEntriesInData = inDecodedDataSize >> 2;
	const uint32 numEntriesInBlock = 0x10;
	int bytesWritten = 0;
	while (decodedIndex < numEntriesInData)
	{
		if (r0 > 0x7fff)
		{
			r0 = 0;

			uint32 key = prevKey;
			uint32 entryIndex = 0;
			while (entryIndex < numEntriesInBlock)
			{
				const uint32 given = SwapByteOrder(pInDecodedData[decodedIndex]);
				const uint32 encoded = given ^ key;
				key = (key ^ 0xaaaa5555) + 0xac53ac53;

				outEncodedData.push_back(SwapByteOrder(encoded));
				++decodedIndex;
				++entryIndex;
				bytesWritten += 4;
			}

			const uint32 oldPrevKey = prevKey;
			prevKey = (prevKey ^ 0x13579bdf) + secondValue;
			secondValue = oldPrevKey;
		}
		else
		{
			for (int i = 0; i < 0x10; ++i)
			{
				const uint32 given = pInDecodedData[decodedIndex];
				outEncodedData.push_back(given);

				bytesWritten += 4;
				++decodedIndex;
			}
		}

		r0 += (prevKey & 0x7ff) + 0x800;
	}
}

uint32 DecodeCreditsData(uint32* pInEncodedData, const uint32 inParam2)
{
	const uint8* startAddress = (uint8*)pInEncodedData;

	uint32 firstValue = SwapByteOrder(*pInEncodedData);
	uint32 secondValue = SwapByteOrder(pInEncodedData[1]);

	//Figure out the number of blocks to decode
	uint32 numBlocksToDecode = inParam2;
	if (numBlocksToDecode < 0)
	{
		numBlocksToDecode += 0x3f; //This is never hit
	}
	numBlocksToDecode = (numBlocksToDecode >> 6) + 1;

	int bytesSkipped = 0;

	//	LAB_06010a3a
	uint32 r0 = 0;
	for (uint32 numBlocksDecoded = 0; numBlocksDecoded <= numBlocksToDecode; ++numBlocksDecoded)
	{
		uint32 key = firstValue;

		//Skip past un-encoded data
		if (r0 > 0x7fff)
		{
			r0 = 0;
			
			//LAB_06010a5e
			for(uint32 r2 = 0; r2 < 0x10; ++r2)
			{
				const uint32 encodedValue = SwapByteOrder(*(uint32*)pInEncodedData);
				*pInEncodedData = SwapByteOrder(key ^ encodedValue);				
				key = (key ^ 0xAAAA5555) + 0xAC53AC53;
				++pInEncodedData;
			}

			const uint32 oldFirstValue = firstValue;
			firstValue = (firstValue ^ 0x13579BDF) + secondValue;			
			secondValue = oldFirstValue;
		}
		else
		{
			pInEncodedData += 0x10;
			bytesSkipped += 0x10*4;
		}

		r0 += (firstValue & 0x7ff) + 0x800;
	}

	return (uint8*)pInEncodedData - startAddress;
}

void ExtractRollingCredits(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	const string ext = bInBmp ? ".bmp" : ".png";

	const std::string endiFilePath = inRootDirectory + "SAKURA3\\0000ENDI.BIN";
	FileData ediFile;
	if (!ediFile.InitializeFileData(endiFilePath))
	{
		return;
	}

	const uint32 bufferSize = 0x15000;
	char* buffer = new char[bufferSize];
	for(int fileIndex = 0; fileIndex < 2; ++fileIndex)
	{
		const string rollingCreditsDir = inOutputDirectory + GEndingFiles[fileIndex].pOutputDir;
		CreateDirectoryHelper(rollingCreditsDir);

		const std::string edsFilePath = inRootDirectory + GEndingFiles[fileIndex].pFileName;
		FileData edsFile;
		if (!edsFile.InitializeFileData(edsFilePath))
		{
			return;
		}

		//Read in key data
		EndingFileKeyData* pKeyData = new EndingFileKeyData[GEndingFiles[fileIndex].numKeys];
		const size_t keyDataSize = sizeof(pKeyData[0]) * GEndingFiles[fileIndex].numKeys;
		memcpy_s(pKeyData, keyDataSize, ediFile.GetData() + GEndingFiles[fileIndex].offsetToKeys, keyDataSize);

		for(int i = 0; i < GEndingFiles[fileIndex].numKeys; ++i)
		{
			pKeyData[i].ChangeByeOrder();
		}

		//Decode each image within the file
		uint32 offset = 0;		
		int imageNumber = 0;
		const uint32 fileSize = (uint32)edsFile.GetDataSize();
		while(offset < fileSize)
		{	
			const string bitmapFileName = rollingCreditsDir + std::to_string(imageNumber) + ext;

			//Header for the block
			EDSTFHeader header;
			memcpy_s(&header, sizeof(header), edsFile.GetData() + offset, sizeof(header));
			header.ChangeByteOrder();

			//Sanity check
			if(header.width > 320)
			{
				break;
			}

			//Read in image data
			memcpy_s(buffer, bufferSize, edsFile.GetData() + offset, header.colorDataSize + 40 + 512);
			
			//Decode the data
			const uint32 decodedSize = DecodeCreditsData((uint32*)buffer, pKeyData[imageNumber].blocksToDecode);

			const uint32 headerSize = sizeof(header);
			const uint32 imageSize = header.width * header.height;
			if(offset + imageSize > fileSize)
			{
				break;
			}

			//	const int paletteOffset = fileIndex == 0 ? 0x14028 : offset + imageSize + headerSize;
			//Extract the image
			ExtractImageFromData(buffer + headerSize, imageSize, bitmapFileName, buffer + imageSize + headerSize, 512, false, 8, 8, header.width / 8, 256, 0, true, bInBmp);

			//Go to the next image
			offset += imageSize + sizeof(header) + 512;

			//Find start of the next block
			while(offset < fileSize)
			{
				if(*((uint8*)(edsFile.GetData() + offset)))
				{
					offset -= 2;
					break;
				}
				++offset;
			}
			++imageNumber;
		}

		delete[] pKeyData;
	}
	
	delete[] buffer;
}

void ExtractCreditsData(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	CreateDirectoryHelper(inOutputDirectory);

	ExtractRollingCredits(inRootDirectory, inOutputDirectory, bInBmp);
}

bool PatchCredits(const std::string& inPatchedSakuraDirectory, const std::string& inTranslatedDataDirectory)
{
	const std::string endiFilePath = inPatchedSakuraDirectory + "SAKURA3\\0000ENDI.BIN";
	FileData ediFile;
	if (!ediFile.InitializeFileData(endiFilePath))
	{
		return false;
	}

	const int bufferSize = 0x15000;
	char* buffer = new char[bufferSize];
	char* encodedBuffer = new char[bufferSize];

	const std::string bmpExt(".bmp");
	for (int fileIndex = 0; fileIndex < 2; ++fileIndex)
	{
		const std::string edsFilePath = inPatchedSakuraDirectory + GEndingFiles[fileIndex].pFileName;
	
		//Load file contents for easy reading
		FileData edsFile;
		if (!edsFile.InitializeFileData(edsFilePath))
		{
			return false;
		}

		//Writer for updated data
		FileReadWriter edsFileWriter;
		if (!edsFileWriter.OpenFile(edsFilePath))
		{
			return false;
		}

		EndingFileKeyData* pKeyData = new EndingFileKeyData[GEndingFiles[fileIndex].numKeys];
		const size_t keyDataSize = sizeof(pKeyData[0]) * GEndingFiles[fileIndex].numKeys;
		memcpy_s(pKeyData, keyDataSize, ediFile.GetData() + GEndingFiles[fileIndex].offsetToKeys, keyDataSize);

		for (int i = 0; i < GEndingFiles[fileIndex].numKeys; ++i)
		{
			pKeyData[i].ChangeByeOrder();
		}

		const string rollingCreditsDir = inTranslatedDataDirectory + "Credits\\" + GEndingFiles[fileIndex].pOutputDir;
		uint32 offset = 0;
		int imageNumber = 0;
		const uint32 fileSize = (uint32)edsFile.GetDataSize();
		while (offset < fileSize)
		{
			//Load&convert patched image
			const string bitmapFileName = rollingCreditsDir + std::to_string(imageNumber) + bmpExt;
			BmpToSaturnConverter patchedImage;
			const unsigned int tileSize = 8;
			const bool bPatchedImageFound = patchedImage.ConvertBmpToSakuraFormat(bitmapFileName, false, BmpToSaturnConverter::CYAN, &tileSize, &tileSize, false);

			//Copy header
			EDSTFHeader header;
			memcpy_s(&header, sizeof(header), edsFile.GetData() + offset, sizeof(header));
			header.ChangeByteOrder();

			//Sanity check
			if (header.width > 320)
			{
				break;
			}

			//Copy over encoded data
			memset(buffer, 0, bufferSize);
			const uint32 expectedDataSize = header.colorDataSize + 40 + 512;
			memcpy_s(buffer, bufferSize, edsFile.GetData() + offset, expectedDataSize);

			//Initialize key data
			if (pKeyData[imageNumber].blocksToDecode > bufferSize)
			{
				printf("Buffer is too small.  Should be at least %i bytes\b", pKeyData[imageNumber].blocksToDecode);
			}

			const uint32 decodedSize = DecodeCreditsData((uint32*)buffer, pKeyData[imageNumber].blocksToDecode);
						
			//Copy patched image data over
			if(bPatchedImageFound)
			{
				patchedImage.PackTiles();

				const uint32 headerSize = sizeof(header);
				memcpy_s(buffer + headerSize, bufferSize, patchedImage.mpPackedTiles, patchedImage.mPackedTileSize);
			}
			
			//Copy decoded data into new buffer so we can encode it
			memset(encodedBuffer, 0, bufferSize);
			memcpy_s(encodedBuffer, bufferSize, buffer, expectedDataSize);
			
			//Encode the data
			std::vector<uint32> outEncodedData;
			const uint32 firstValue = SwapByteOrder(*(uint32*)buffer);
			EncodeCreditsData(firstValue, (uint32*)encodedBuffer, expectedDataSize, outEncodedData);

			//Make sure we decoded the whole dataset
			if(expectedDataSize > outEncodedData.size()*4)
			{
				printf("PatchCredits: Unable too fully decode %s. Expected size: %i, got: %i \n", bitmapFileName.c_str(), expectedDataSize, outEncodedData.size()*4);
				return false;
			}
			
			//Write it out
			edsFileWriter.WriteData(offset, (char*)outEncodedData.data(), expectedDataSize);

			//Go to the next image
			const uint32 imageSize = header.width * header.height;
			offset += imageSize + sizeof(header) + 512;
			while (offset < fileSize)
			{
				if (*((uint8*)(edsFile.GetData() + offset)))
				{
					offset -= 2;
					break;
				}
				++offset;
			}
			++imageNumber;
		}
	}

	delete[] buffer;
	delete[] encodedBuffer;

	return true;
}