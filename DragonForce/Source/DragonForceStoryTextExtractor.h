#pragma once

bool IsFirstByteOfJis(uint8 inByte)
{
	return (inByte > 0x80 && inByte < 0xa0) || (inByte >= 0xe0 && inByte <= 0xfc);
}

bool IsValidSingleByteCharacter(uint8 inByte)
{
	return (inByte >= 0x20 && inByte < 0x7f) || (inByte >= 0xa1 && inByte <= 0xdf);
}

class DragonForceStoryTextExtractorNew
{
public:
	struct Header_OffsetData
	{
		uint32 Offset;
	};

	struct Section
	{
		string line;
		vector<char> prefix;
		vector<char> postfix;

		void AddCharacter(char byte)
		{
			line += byte;
		}

		void AddJIS(char byte1, char byte2)
		{
			line += byte1;
			line += byte2;
		}

		void AddMisc(char byte)
		{
			if( line.size() )
			{
				postfix.push_back(byte);
			}
			else
			{
				prefix.push_back(byte);
			}
		}
	};

	struct Entry
	{
		vector<Section> sections;
		vector<char> byteData;
		uint32 offset;

		void AddSection(uint32 inStartOffset, uint32 inEndOffset, const uint8* pInData)
		{
			Section newSection;
			uint32 currOffset = entryStart;
			while (currOffset < entryEnd)
			{
				uint8 byte = pInData[currOffset];

				if (IsValidSingleByteCharacter(byte) || byte == 0x0d)
				{
					newSection.AddCharacter(byte);
					newEntry.AddByteData(byte);

					++currOffset;
				}
				else if (IsFirstByteOfJis(byte))
				{
					newLine.line += byte;
					newLine.line += pInData[currOffset + 1];

					newLine.byteData.push_back(byte);
					newLine.byteData.push_back(pInData[currOffset + 1]);

					currOffset += 2;
				}
				else
				{
					newLine.postfix.push_back(byte);
					++currOffset;
				}
			}
		}
	};

	vector<Entry> entries;

public:
	DragonForceStoryTextExtractorNew(const FileNameContainer& inFilePath) : mFileName(inFilePath) {}

	bool ParseText(uint32 offsetToHeader, const uint32 inNumEntriesInHeader, const uint32 inOffsetToStrings)
	{
		FileData fileData;
		if (!fileData.InitializeFileData(mFileName))
		{
			return false;
		}

		//Read offsets for all strings
		Header_OffsetData* pOffsetsToEntries = new Header_OffsetData[inNumEntriesInHeader];
		memcpy_s(pOffsetsToEntries, inNumEntriesInHeader * sizeof(Header_OffsetData), fileData.GetData() + offsetToHeader, inNumEntriesInHeader * sizeof(Header_OffsetData));
		for (uint32 stringIndex = 0; stringIndex < inNumEntriesInHeader; ++stringIndex)
		{
			pOffsetsToEntries[stringIndex].Offset = SwapByteOrder(pOffsetsToEntries[stringIndex].Offset); //Fix endianness
		}

		const uint8* pData = (uint8*)fileData.GetData();
		const uint32 offsetToEntriesTable = inOffsetToStrings;

		Entry newEntry;

		//Parse strings
		for (uint32 entryIndex = 0; entryIndex < inNumEntriesInHeader; ++entryIndex)
		{
			const uint32 entryStart = pOffsetsToEntries[entryIndex].Offset + offsetToEntriesTable;
			const uint32 entryEnd   = (entryIndex + 1 < inNumEntriesInHeader ? offsetToEntriesTable + pOffsetsToEntries[entryIndex + 1].Offset : fileData.GetDataSize());

			newEntry.AddSection(entryStart, entryEnd, pData);
		}

		return true;
	}
};

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
	
	struct Section
	{
		string line;
		vector<char> prefix;
		vector<char> postfix;
	};

	struct Entry
	{
		vector<Section> sections;
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
			const uint32 stringEnd   = (stringIndex + 1 < inNumEntriesInHeader ? offsetToStringTable + pOffsetsToStrings[stringIndex + 1].Offset : fileData.GetDataSize());

			LineData newLine;
			uint32 currOffset = stringStart;
			while( currOffset < stringEnd )
			{
				uint8 byte = (uint8)pData[currOffset];

			//	newLine.byteData.push_back(byte);
				if(IsValidSingleByteCharacter(byte) || byte == 0x0d)
				{
					if( byte == 0x0d )
						newLine.line += ' ';
					else
					{
						newLine.line += byte;
						newLine.byteData.push_back(byte);
					}

					++currOffset;
				}
				else if( IsFirstByteOfJis(byte) )
				{
					newLine.line += byte;
					newLine.line += (uint8)pData[currOffset + 1];

					newLine.byteData.push_back(byte);
					newLine.byteData.push_back((uint8)pData[currOffset + 1]);

					currOffset += 2;
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
	const string nextEntry("--\n");
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
		//	char offsetBuffer[20] = {0};
		//	snprintf(offsetBuffer, 20, "%04x ", lineData.offset);
		//	outFile.WriteData(offsetBuffer, strnlen_s(offsetBuffer, 20));

			//Print text
			for(auto iter = lineData.line.cbegin(); iter != lineData.line.cend(); ++iter)
			{
				if( *iter == 0x0d )
				{
					outFile.WriteData(&spaceChar, 1);
				}
			//	else
				{
					outFile.WriteData( &(*iter), 1 );
				}
			}

		//	const char colon = ':';
		//	outFile.WriteData(&colon, 1);
			
			//Print postfix
			for(const char byte : lineData.postfix)
			{
		//		const string& strValue = ByteToStringLUT[(uint8)byte];
		//		outFile.WriteData(strValue.c_str(), strValue.length());
		//		outFile.WriteData(&spaceChar, 1);
			}

		//	outFile.WriteData(nextEntry.c_str(), nextEntry.length());
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

bool FindMatchesInMeetingFiles(const string& inFilePath1, const string& inFilePath2)
{
	FileData file1;
	if( !file1.InitializeFileData(inFilePath1) )
	{
		return false;
	}

	FileData file2;
	if (!file2.InitializeFileData(inFilePath2))
	{
		return false;
	}

	//Read file1
	uint32 numEntries1 = 0;
	file1.ReadData(0, (char*)&numEntries1, sizeof(numEntries1), false);

	uint32* pEntries1 = new uint32[numEntries1];
	memcpy_s(pEntries1, sizeof(uint32)*numEntries1, file1.GetData() + sizeof(uint32), sizeof(uint32)*numEntries1);

	vector<uint32> crcsInFile1;
	for (uint32 i = 0; i < numEntries1; ++i)
	{
		crcsInFile1.push_back( pEntries1[i] );
	}

	//Read file2
	uint32 numEntries2 = 0;
	file2.ReadData(0, (char*)&numEntries2, sizeof(numEntries2), false);

	uint32* pEntries2 = new uint32[numEntries2];
	memcpy_s(pEntries2, sizeof(uint32) * numEntries2, file2.GetData() + sizeof(uint32), sizeof(uint32) * numEntries2);

	unordered_map<uint32, uint32> crcToLineInFile2;
	for (uint32 i = 0; i < numEntries2; ++i)
	{
		crcToLineInFile2[pEntries2[i]] = i;
	}

	//Find matches
	printf("Matches - LineInFlile1 : LineInFile2\n");
	for(uint32 i = 0; i < numEntries1; ++i)
	{
		unordered_map<uint32, uint32>::iterator iter = crcToLineInFile2.find(crcsInFile1[i]);
		if( iter != crcToLineInFile2.end() )
		{
			printf("%i : %i\n", i, iter->second);
		}
	}

	delete[] pEntries1;
	delete[] pEntries2;

	return true;
}
