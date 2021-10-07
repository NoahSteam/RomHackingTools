#pragma once

class DragonForceStoryTextExtractor
{
	struct LineData
	{
		string line;
		vector<char> postfix;
	};

	vector<LineData> mLines;

	bool IsAsciiCharacter(char c)
	{
		return c >= 0x20 && c <= 0x7e;
	}

public:
	bool ParseText(const string& inFilePath)
	{
		FileData fileData;
		if( !fileData.InitializeFileData(FileNameContainer(inFilePath)) )
		{
			return false;
		}

		const char* pData = fileData.GetData();
		uint32 offset = 0x15d40;
		LineData newLine;
		while( offset < fileData.GetDataSize() )
		{
			const char byte = pData[offset];
			if( byte == 0x0D && !IsAsciiCharacter(byte) )
			{
				vec postfix;
			}

		}
	}
};
