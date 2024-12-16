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

		EndingFileKeyData* pKeyData = new EndingFileKeyData[GEndingFiles[fileIndex].numKeys];
		const size_t keyDataSize = sizeof(pKeyData[0]) * GEndingFiles[fileIndex].numKeys;
		memcpy_s(pKeyData, keyDataSize, ediFile.GetData() + GEndingFiles[fileIndex].offsetToKeys, keyDataSize);

		for(int i = 0; i < GEndingFiles[fileIndex].numKeys; ++i)
		{
			pKeyData[i].ChangeByeOrder();
		}

		uint32 offset = 0;
		char buffer[0x1c000];
		int i = 0;
		const uint32 fileSize = (uint32)edsFile.GetDataSize();
		while(offset < fileSize)
		{	
			const string bitmapFileName = rollingCreditsDir + std::to_string(i) + ext;
			//const string bitmapFileName = rollingCreditsDir + IntToHexString(offset) + ext;

			EDSTFHeader header;
			memcpy_s(&header, sizeof(header), edsFile.GetData() + offset, sizeof(header));
			header.ChangeByteOrder();

			if(header.width > 320)
			{
				break;
			}
			memcpy_s(buffer, sizeof(buffer), edsFile.GetData() + offset, header.colorDataSize + 40 + 512);

			const uint32 headerSize = sizeof(header);
			const uint32 decodedSize = DecodeCreditsData((uint32*)buffer, pKeyData[i].blocksToDecode);
			const uint32 imageSize = header.width * header.height;
			const int paletteOffset = fileIndex == 0 ? 0x14028 : offset + imageSize + headerSize;

			if(offset + imageSize > fileSize)
			{
				break;
			}
			ExtractImageFromData(buffer + headerSize, imageSize, bitmapFileName, buffer + imageSize + headerSize, 512, false, 8, 8, header.width / 8, 256, 0, true, bInBmp);

			offset += imageSize + sizeof(header) + 512;
			while(offset < fileSize)
			{
				if(*((uint8*)(edsFile.GetData() + offset)))
				{
					offset -= 2;
					break;
				}
				++offset;
			}

			++i;
		}

		delete[] pKeyData;
	}
}

void TestEncodeCredits(const std::string& inRootDirectory, const std::string& inOutputDirectory)
{

	const std::string endiFilePath = inRootDirectory + "SAKURA3\\0000ENDI.BIN";
	FileData ediFile;
	if (!ediFile.InitializeFileData(endiFilePath))
	{
		return;
	}

	for (int fileIndex = 0; fileIndex < 2; ++fileIndex)
	{
		CreateDirectoryHelper(inOutputDirectory);

		const std::string edsFilePath = inRootDirectory + GEndingFiles[fileIndex].pFileName;
		FileData edsFile;
		if (!edsFile.InitializeFileData(edsFilePath))
		{
			return;
		}

		EndingFileKeyData* pKeyData = new EndingFileKeyData[GEndingFiles[fileIndex].numKeys];
		const size_t keyDataSize = sizeof(pKeyData[0]) * GEndingFiles[fileIndex].numKeys;
		memcpy_s(pKeyData, keyDataSize, ediFile.GetData() + GEndingFiles[fileIndex].offsetToKeys, keyDataSize);

		for (int i = 0; i < GEndingFiles[fileIndex].numKeys; ++i)
		{
			pKeyData[i].ChangeByeOrder();
		}

		uint32 offset = 0;
		const int bufferSize = 0x15000;
		char buffer[bufferSize];
		char encodedBuffer[bufferSize];
		int i = 0;
		const uint32 fileSize = (uint32)edsFile.GetDataSize();
		while (offset < fileSize)
		{
			EDSTFHeader header;
			memcpy_s(&header, sizeof(header), edsFile.GetData() + offset, sizeof(header));
			header.ChangeByteOrder();

			if (header.width > 320)
			{
				break;
			}
			memset(buffer, 0, sizeof(buffer));
			memcpy_s(buffer, sizeof(buffer), edsFile.GetData() + offset, header.colorDataSize + 40 + 512);

			if(pKeyData[i].blocksToDecode > bufferSize)
			{
				printf("Buffer is too small.  Should be at least %i bytes\b", pKeyData[i].blocksToDecode);
			}

			const uint32 headerSize = sizeof(header);
			const uint32 decodedSize = DecodeCreditsData((uint32*)buffer, pKeyData[i].blocksToDecode);

			/*
			FileWriter decodedDataFile;
			if (decodedDataFile.OpenFileForWrite(inOutputDirectory + string("decoded_") + std::to_string(offset)))
			{
				decodedDataFile.WriteData(buffer, bufferSize * 4);
				decodedDataFile.Close();
			}*/

			memset(encodedBuffer, 0, sizeof(encodedBuffer));
			memcpy_s(encodedBuffer, sizeof(encodedBuffer), buffer, decodedSize);

			std::vector<uint32> outEncodedData;
			const uint32 firstValue = SwapByteOrder(*(uint32*)buffer);
			EncodeCreditsData(firstValue, (uint32*)encodedBuffer, decodedSize, outEncodedData);
			
			FileWriter outFile;
			if(outFile.OpenFileForWrite(inOutputDirectory + std::to_string(offset)))
			{
				outFile.WriteData(outEncodedData.data(), outEncodedData.size()*4);
			}
		}
	}
}

void ExtractCreditsData(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	CreateDirectoryHelper(inOutputDirectory);

	ExtractRollingCredits(inRootDirectory, inOutputDirectory, bInBmp);
}

bool PatchCredits(const std::string& inPatchedSakuraDirectory, const std::string& inTranslatedDataDirectory)
{
	TestEncodeCredits(inPatchedSakuraDirectory, "a:\\SakuraWars2\\EncodingTest\\");

	return true;
}