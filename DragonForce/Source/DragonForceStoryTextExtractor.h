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
	struct LineData
	{
		string line;
		vector<char> postfix;

		void Reset()
		{
			line.clear();
			postfix.clear();
		}
	};

	vector<LineData> mLines;
	FileNameContainer mFileName;

public:
	DragonForceStoryTextExtractor(const FileNameContainer& inFilePath) : mFileName(inFilePath){}

	bool ParseText(uint32 inOffset)
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

bool ParseSaturnStoryText(const string& inRootDirectory, vector<DragonForceStoryTextExtractor>& outData, uint32 inOffset)
{
	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory( (inRootDirectory + "\\Game"), allFiles);

	//Find all the font sheet files
	vector<FileNameContainer> meetingFiles;
	GetAllFilesOfType(allFiles, "MT_VDP", meetingFiles);

	for(const FileNameContainer& fileName : meetingFiles)
	{
		DragonForceStoryTextExtractor newFile(fileName);
		if( !newFile.ParseText(inOffset) )
		{
			return false;
		}

		outData.push_back(newFile);
	}

	return true;
}

bool DumpSaturnStoryText(const string& inRootDirectory, const string& inOutputPath, uint32 inOffsetToText)
{
	CreateDirectoryHelper(inOutputPath);

	vector<DragonForceStoryTextExtractor> storyText;

	if( !ParseSaturnStoryText(inRootDirectory, storyText, inOffsetToText) )
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

		//	const char colon = ':';
		//	outFile.WriteData(&colon, 1);
			
			//Print postfix
			for(const char byte : lineData.postfix)
			{
		//		const string& strValue = ByteToStringLUT[(uint8)byte];
		//		outFile.WriteData(strValue.c_str(), strValue.length());
		//		outFile.WriteData(&spaceChar, 1);
			}

			const char newLine = '\n';
			outFile.WriteData(&newLine, 1);
		}
	}

	return true;
}
