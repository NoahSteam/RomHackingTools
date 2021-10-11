#pragma once

class DragonForceStoryTextExtractor
{
	bool IsFirstByteOfJis(uint8 inByte)
	{
		return (inByte > 0x80 && inByte < 0xa0) || (inByte >= 0xe0 && inByte <= 0xfc);
	}

	bool IsValidSingleByteCharacter(uint8 inByte)
	{
		return (inByte >= 0x20 && inByte < 0x7f) || (inByte >= 0xa1 && inByte <= 0xdf);
	}

public:
	struct Header_OffsetData
	{
		uint32 Offset;
	};
	
	struct LineData
	{
		string line;
		vector<char> postfix;
		vector<char> byteData;
		uint32 offset;
		uint32 crc;

		void Reset()
		{
			line.clear();
			postfix.clear();
			byteData.clear();
			offset = 0;
			crc = 0;
		}
	};

	vector<LineData> mLines;
	FileNameContainer mFileName;

public:
	DragonForceStoryTextExtractor(const FileNameContainer& inFilePath) : mFileName(inFilePath){}

	bool ParseText(uint32 offsetToHeader, const uint32 inNumEntriesInHeader, const uint32 inOffsetToStrings)
	{
		FileData fileData;
		if( !fileData.InitializeFileData(mFileName) )
		{
			return false;
		}

		//Read offsets for all strings
		Header_OffsetData* pOffsetsToStrings = new Header_OffsetData[inNumEntriesInHeader];
		memcpy_s(pOffsetsToStrings, inNumEntriesInHeader * sizeof(Header_OffsetData), fileData.GetData() + offsetToHeader, inNumEntriesInHeader * sizeof(Header_OffsetData));
		for( uint32 stringIndex = 0; stringIndex < inNumEntriesInHeader; ++stringIndex )
		{
			pOffsetsToStrings[stringIndex].Offset = SwapByteOrder(pOffsetsToStrings[stringIndex].Offset); //Fix endianness
		}

		const uint8* pData = (uint8*)fileData.GetData();
		const uint32 offsetToStringTable = inOffsetToStrings;

		//Parse strings
		for( uint32 stringIndex = 0; stringIndex < inNumEntriesInHeader; ++stringIndex )
		{
			const uint32 stringStart = pOffsetsToStrings[stringIndex].Offset + offsetToStringTable;
			const uint32 stringEnd   = offsetToStringTable + (stringIndex + 1 < inNumEntriesInHeader ? pOffsetsToStrings[stringIndex + 1].Offset : fileData.GetDataSize());

			LineData newLine;
			uint32 currOffset = stringStart;
			while( currOffset < stringEnd )
			{
				uint8 byte = (uint8)pData[currOffset];

				newLine.byteData.push_back(byte);
				if(IsValidSingleByteCharacter(byte) || byte == 0x0d)
				{
					if( byte == 0x0d )
						newLine.line += ' ';
					else
						newLine.line += byte;
					++currOffset;
				}
				else if( IsFirstByteOfJis(byte) )
				{
					newLine.line += byte;
					newLine.line += (uint8)pData[++currOffset];
					++currOffset;
				}
				else
				{
					newLine.postfix.push_back(byte);
					++currOffset;
				}
			}

			newLine.offset = stringStart - offsetToStringTable;
			newLine.crc = CalculateDataCRC(newLine.byteData.data(), (uint32)newLine.byteData.size());

			mLines.push_back(newLine);
		}

		//Free memory
		delete[] pOffsetsToStrings;

		return true;
	}

	bool ParseTextOld(uint32 inOffset, uint32 inNumEntries)
	{	
		FileData fileData;
		if( !fileData.InitializeFileData(mFileName) )
		{
			return false;
		}

		const char* pData = fileData.GetData();
		const uint32 fileSize = (uint32)fileData.GetDataSize();
		uint32 offset = inOffset;//0x15d40;
		LineData newLine;
		vector<char> postFix;
		uint8 byte = 0;
		while( offset < fileSize)
		{
			byte = (uint8)pData[offset];

			if(IsValidSingleByteCharacter(byte))
			{
				newLine.line += byte;
				++offset;
			}
			else if(byte < 0x20)
			{
				uint8 nextByte = (uint8)pData[offset + 1];
				if (byte == 0x0d || byte == 0x06)
				{
					if (byte == 0x06 || (!IsValidSingleByteCharacter(nextByte) && !IsFirstByteOfJis(nextByte)))
					{
						postFix.push_back(byte);

						const uint32 startOffset = offset;

						//while(!IsValidSingleByteCharacter(nextByte) && !IsFirstByteOfJis(nextByte))
						while(	(byte == 0x02 && !IsFirstByteOfJis(nextByte)) ||
								 byte == 0x01 ||
								(byte < 0x04 && (nextByte == 0x71 || nextByte == 0x28 || nextByte == 0x29 || nextByte == 0x2a || nextByte == 0x2b || nextByte == 0x3d)) ||
								!(IsValidSingleByteCharacter(nextByte) || IsFirstByteOfJis(nextByte)) )
						{
							postFix.push_back(nextByte);
							byte     = nextByte;
							nextByte = (uint8)pData[++offset];
						}

						if( startOffset == offset )
						{
							++offset;
						}

						//End of line
						newLine.postfix = postFix;
						mLines.push_back(newLine);

						postFix.clear();
						newLine.Reset();

						continue;
					}
					else
					{
						newLine.line += byte;
						++offset;
					}
				}
				else
				{
					postFix.push_back(byte);
					++offset;
				}
			}
			else if( IsFirstByteOfJis(byte) )
			{
				newLine.line += byte;
				newLine.line += (uint8)pData[++offset];
				++offset;
			}
		}

		return true;
	}
};

bool ParseSaturnStoryText(const string& inRootDirectory, vector<DragonForceStoryTextExtractor>& outData, uint32 inOffsetToHeader, uint32 inNumEntries, uint32 inOffsetToStrings)
{
	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inRootDirectory, allFiles);

	//Find all the font sheet files
	vector<FileNameContainer> meetingFiles;
	GetAllFilesOfType(allFiles, "MT_VDP", meetingFiles);

	for(const FileNameContainer& fileName : meetingFiles)
	{
		DragonForceStoryTextExtractor newFile(fileName);
		if( !newFile.ParseText(inOffsetToHeader, inNumEntries, inOffsetToStrings) )
		{
			return false;
		}

		outData.push_back(newFile);
	}

	return true;
}

bool DumpSaturnStoryText(const string& inRootDirectory, const string& inOutputPath, uint32 inOffsetToToHeader, uint32 inNumEntries, uint32 inOffsetToStringTable)
{
	CreateDirectoryHelper(inOutputPath);

	vector<DragonForceStoryTextExtractor> storyText;

	if( !ParseSaturnStoryText(inRootDirectory, storyText, inOffsetToToHeader, inNumEntries, inOffsetToStringTable) )
	{
		return false;
	}

	char tempBuffer[10];
	vector<string> ByteToStringLUT;
	for(unsigned char c = 0; c <= 0xff; ++c)
	{
		snprintf(tempBuffer, 10, "%02x", c);
		ByteToStringLUT.push_back(tempBuffer);

		if( c == 0xff )
		{
			break;
		}
	}

	const char spaceChar = ' ';
	const string outExt(".txt");
	for(const DragonForceStoryTextExtractor& textData : storyText)
	{
		//Create output file
		FileWriter outFile;
		const string& outPath = inOutputPath + textData.mFileName.mNoExtension + outExt;
		if (!outFile.OpenFileForWrite(outPath))
		{
			continue;
		}

		for(const DragonForceStoryTextExtractor::LineData& lineData : textData.mLines)
		{
			//print offset
			char offsetBuffer[20] = {0};
			snprintf(offsetBuffer, 20, "%04x ", lineData.offset);
			outFile.WriteData(offsetBuffer, strnlen_s(offsetBuffer, 20));

			//Print text
			for(auto iter = lineData.line.cbegin(); iter != lineData.line.cend(); ++iter)
			{
				if( *iter == 0x0d )
				{
					outFile.WriteData(&spaceChar, 1);
				}
				else
				{
					outFile.WriteData( &(*iter), 1 );
				}
			}

			const char colon = ':';
			outFile.WriteData(&colon, 1);
			
			//Print postfix
			for(const char byte : lineData.postfix)
			{
				const string& strValue = ByteToStringLUT[(uint8)byte];
				outFile.WriteData(strValue.c_str(), strValue.length());
				outFile.WriteData(&spaceChar, 1);
			}

			const char newLine = '\n';
			outFile.WriteData(&newLine, 1);
		}
	}

	return true;
}

bool DumpStoryTextData(const string& inRootDirectory, const string& inOutputPath, uint32 inOffsetToToHeader, uint32 inNumEntries, uint32 inOffsetToStringTable)
{
	CreateDirectoryHelper(inOutputPath);

	vector<DragonForceStoryTextExtractor> storyText;

	if( !ParseSaturnStoryText(inRootDirectory, storyText, inOffsetToToHeader, inNumEntries, inOffsetToStringTable) )
	{
		return false;
	}

	const char spaceChar = ' ';
	const string outExt(".bin");
	for(const DragonForceStoryTextExtractor& textData : storyText)
	{
		//Create output file
		FileWriter outFile;
		const string& outPath = inOutputPath + textData.mFileName.mNoExtension + outExt;
		if (!outFile.OpenFileForWrite(outPath))
		{
			continue;
		}

		const uint32 numEntries = (uint32)textData.mLines.size();
		outFile.WriteData(&numEntries, sizeof(numEntries));

		for(const DragonForceStoryTextExtractor::LineData& lineData : textData.mLines)
		{
			outFile.WriteData(&lineData.crc, sizeof(lineData.crc));
		//	outFile.WriteData(&lineData.offset, sizeof(lineData.offset));
		//	outFile.WriteData(lineData.byteData.data(), (unsigned long)lineData.byteData.size());
		}
	}

	return true;
}
