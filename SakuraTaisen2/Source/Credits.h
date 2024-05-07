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

uint32 DecodeCreditsData(uint8* r7, uint32 r11)
{
	uint32 r0 = 0;
	uint32 r5 = 0;
	uint32 r10 = 0xAAAA5555;
	uint32 r9 = 0xAC53AC53;
	uint32 r6 = SwapByteOrder( *(uint32*)r7 );
	uint32 r8 = SwapByteOrder( ((uint32*)r7)[1] );
	const uint8* startAddress = r7;
//	LAB_06010a3a
	while(1)
	{
		uint32 r4 = r11;
		if (r4 < 0)
		{
			r4 += 0x3f;
		}

		r4 = r4 >> 6;
		if(++r4 <= r5)
		{
			break;
		}

		uint32 r1 = 0x7fff;
		uint32 r3 = r6;
		if(r0 > r1)
		{
			r0 = 0;
			uint32 r2 = 0;
			r4 = 0xf;

			//LAB_06010a5e
			do 
			{
				r1 = SwapByteOrder(*(uint32*)r7);
				++r2;
				r1 = r3^r1;
				(*(uint32*)r7) = SwapByteOrder(r1);
				r7 += 4;
				r3 = r3 ^ r10;
				r3 += r9;
			} while (r2 <= r4);

			r2 = r6;
			r1 = 0x13579BDF;
			r6 = r6^r1;
			r6 += r8;
			r8 = r2;
		}
		else
		{
			r7 += 0x40;
		}

		r1 = 0x800;
		r0 += r1;
		r1 = 0x7ff;
		r1 &= r6;
		r0 += r1;
		++r5;
	}

	return r7 - startAddress;
}

uint32 DecodeCreditsDataOld(uint32* param_1, int param_2)
{
	uint32 uVar1;
	int iVar2;
	uint32 uVar3 = 0;
	int iVar4;
	int iVar5;
	uint32 uVar6;
	uint32 uVar7;
	uint8* pStartAddresss = (uint8*)param_1;

	iVar2 = 0xAC53AC53;//DAT_06010aa4; r8
	uVar1 = 0xAAAA5555;//DAT_06010aa0; r9
	iVar5 = 0;
	uVar7 = SwapByteOrder(param_1[1]);
	uVar6 = SwapByteOrder (*param_1);
	while (true) 
	{
		iVar4 = param_2;
		if (param_2 < 0) 
		{
			iVar4 = param_2 + 0x3f;
		}

		//r4 = iVar4
		iVar4 = iVar4 >> 6;
		uVar3 = iVar4;
		//uVar3 = (*(code*)PTR_FUN_06010aa8)(); //shar r4 6 times
		if (iVar4 + 1 <= iVar5) 
		{
			break;
		}

		if ((uint32)0x7fff < uVar3) 
		{
			iVar4 = 0;
			uVar3 = uVar6;
			do 
			{
				iVar4 = iVar4 + 1;

				const uint32 encodedValue = SwapByteOrder(*param_1);
				*param_1 = SwapByteOrder(encodedValue ^ uVar3);
				param_1 = param_1 + 1;
				uVar3 = (uVar3 ^ uVar1) + iVar2;
			} while (iVar4 < 0x10);
			uVar3 = (uVar6 ^ 0x13579BDF) + uVar7;
			uVar7 = uVar6;
		}
		else 
		{
			param_1 = param_1 + 0x10;
			uVar3 = uVar6;
		}
		iVar5 = iVar5 + 1;
		uVar6 = uVar3;
	}

	return ((uint8*)param_1 - pStartAddresss) + 4;
}

void ExtractRollingCredits(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	const string ext = bInBmp ? ".bmp" : ".png";

	struct EndingFileInfo
	{
		const char* pFileName;
		const char* pOutputDir;
		const int offsetToKeys;
		const int numKeys;
	};

	EndingFileInfo endingFiles[2] =
	{
		{"SAKURA1\\EDSTF.ALL", "RollingCredits\\", 0x30da0, 20},
		{"SAKURA1\\EDDATA.ALL", "Credits\\", 0x2fe80, 386}, //11a28, 3fe8
	};
	
	struct EndingFileKeyData
	{
		uint32 key;
		uint32 unknown;

		void ChangeByeOrder()
		{
			key = SwapByteOrder(key);
			unknown = SwapByteOrder(unknown);
		}
	};

	const std::string endiFilePath = inRootDirectory + "SAKURA3\\0000ENDI.BIN";
	FileData ediFile;
	if (!ediFile.InitializeFileData(endiFilePath))
	{
		return;
	}

	for(int fileIndex = 0; fileIndex < 2; ++fileIndex)
	{
		const string rollingCreditsDir = inOutputDirectory + endingFiles[fileIndex].pOutputDir;
		CreateDirectoryHelper(rollingCreditsDir);

		const std::string edsFilePath = inRootDirectory + endingFiles[fileIndex].pFileName;
		FileData edsFile;
		if (!edsFile.InitializeFileData(edsFilePath))
		{
			return;
		}

		EndingFileKeyData* pKeyData = new EndingFileKeyData[endingFiles[fileIndex].numKeys];
		const size_t keyDataSize = sizeof(pKeyData[0]) * endingFiles[fileIndex].numKeys;
		memcpy_s(pKeyData, keyDataSize, ediFile.GetData() + endingFiles[fileIndex].offsetToKeys, keyDataSize);

		for(int i = 0; i < endingFiles[fileIndex].numKeys; ++i)
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
			const uint32 decodedSize = DecodeCreditsData((uint8*)buffer, pKeyData[i].key);
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

void ExtractCreditsData(const std::string& inRootDirectory, const std::string& inOutputDirectory, const bool bInBmp)
{
	CreateDirectoryHelper(inOutputDirectory);

	ExtractRollingCredits(inRootDirectory, inOutputDirectory, bInBmp);
}