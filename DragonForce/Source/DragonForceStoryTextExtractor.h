#pragma once

class DragonForceStoryTextExtractor
{
	bool IsFirstByteOfJis(uint8 u)
	{
		return (u > 0x81 && u < 0xa0) || (u >= 0xe0 && u <= 0xef);
	}

	bool IsValidCharacter(char c, char prevByte)
	{
		//return ((uint8)c >= 0x20 && (uint8)c < 0x7f) || (uint8)c > 0x80;
		const uint8 currU = (uint8)c;
		const uint8 prevU = (uint8)prevByte;

		const bool bIsSingleByteChar = (currU >= 0x20 && currU < 0x7f) || (currU > 0xa0 && currU < 0xe0);
		const bool bIsFirstByteOfJis = IsFirstByteOfJis(currU);

		const bool bWasPrevValidFirst  = IsFirstByteOfJis(prevU);
		const bool bIsSecondOddJisByte = bWasPrevValidFirst && (currU >= 0x40 && currU <= 0x9e && currU != 0x7f);
		const bool bIsSecondJisByte    = bWasPrevValidFirst && ((bIsSecondOddJisByte && prevU%2 != 0) || (!bIsSecondOddJisByte && prevU%2 == 0));

		return bIsSingleByteChar || bIsFirstByteOfJis || bIsSecondOddJisByte || bIsSecondJisByte;
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

	bool ParseText()
	{	
		FileData fileData;
		if( !fileData.InitializeFileData(mFileName) )
		{
			return false;
		}

		const char* pData = fileData.GetData();
		const uint32 fileSize = (uint32)fileData.GetDataSize();
		uint32 offset = 0x15d40;
		LineData newLine;
		char byte = 0;
		char prevByte = 0;
		while( offset < fileSize)
		{
			prevByte = byte;
			byte = pData[offset];
			if( !IsValidCharacter(byte, prevByte) )
			{
				vector<char> postFix;

				char nextByte = 0;
				do
				{
					byte     = pData[offset];
					nextByte = pData[++offset];
					postFix.push_back(byte);
				}while(!IsValidCharacter(nextByte, byte) && (offset < fileSize));

				newLine.postfix = postFix;
				mLines.push_back(newLine);

				newLine.Reset();
			}
			else
			{
				newLine.line += byte;

				++offset;
			}
		}

		return true;
	}
};

bool ParseSaturnStoryText(const string& inRootDirectory, vector<DragonForceStoryTextExtractor>& outData)
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
		if( !newFile.ParseText() )
		{
			return false;
		}

		outData.push_back(newFile);
	}

	return true;
}

bool DumpSaturnStoryText(const string& inRootDirectory, const string& inOutputPath)
{
	CreateDirectoryHelper(inOutputPath);

	vector<DragonForceStoryTextExtractor> storyText;

	if( !ParseSaturnStoryText(inRootDirectory, storyText) )
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
