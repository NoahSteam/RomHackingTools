#pragma once

class DragonForceTextFinder
{
public:
	struct LineData
	{
		string Line;
		uint32 Offset{0};
		uint32 NumBytes{0};
		uint32 NumZerosAfterLine{0};
	};

	bool FindText(const string& inFilePath, const uint32 inOffset)
	{
		FileData dataFile;
		if (!dataFile.InitializeFileData(FileNameContainer(inFilePath.c_str())))
		{
			return false;
		}

		const uint32 bufferSize = 2048;
		char buffer[bufferSize];
		memset(buffer, 0, sizeof(buffer));

		uint32 bufferIndex = 0;
		uint32 fileOffset = inOffset;
		uint32 entryNumber = 0;
		uint32 stringOffset = 0;
		bool bStringStartFound = false;
		const char* pData = dataFile.GetData();
		const uint32 dataSize = (uint32)dataFile.GetDataSize();
		while (fileOffset < dataSize)
		{
			if (pData[fileOffset] == 0)
			{
				++fileOffset;
				continue;
			}

			if( !bStringStartFound )
			{
				stringOffset = fileOffset;
				bStringStartFound = true;
			}

			buffer[bufferIndex++] = pData[fileOffset++];
			if (bufferIndex > bufferSize)
			{
				printf("Buffer too small\n");
				return false;
			}

			//End of line
			if (pData[fileOffset] == 0)
			{
				//Find number of zeros after the end
				uint32 secondFileOffset = fileOffset;
				while( secondFileOffset < dataSize && pData[secondFileOffset] == 0 )
				{
					++secondFileOffset;
				}

				bStringStartFound = false;

				//Add entry
				LineData lineData;
				lineData.Line = string(buffer);
				lineData.Offset = stringOffset;
				lineData.NumBytes = bufferIndex;
				lineData.NumZerosAfterLine = secondFileOffset - fileOffset;
				mLines.push_back(lineData);

				//Clear buffer for next line
				memset(buffer, 0, sizeof(buffer));

				++entryNumber;
				bufferIndex = 0;
			}
		}

		CreateStringToLineDataMap();

		return true;
	}

	bool DoesDataExistForLine(const string& inString) const
	{
		return mStringToLineData.find(inString) != mStringToLineData.end();
	}

	const vector<const LineData*>& GetLineDataFromString(const string& inString) const
	{
		auto iter = mStringToLineData.find(inString);
		return iter->second;
	}

private:
	void CreateStringToLineDataMap()
	{
		for( LineData& lineData : mLines )
		{
			mStringToLineData[lineData.Line].push_back(&lineData);
		}
	}

private:
	vector<LineData> mLines;
	unordered_map<string, vector<const LineData*>> mStringToLineData;

};
