#pragma once

class DragonForceCSVReader
{
public:

	struct LineEntry
	{
		string japanese;
		string google;
		string english;

		void Clear()
		{
			japanese.clear();
			google.clear();
			english.clear();
		}
	};

	vector<LineEntry> lines;

	bool InitializeFile(const string& fileName)
	{
		FileNameContainer fileNameInfo(fileName.c_str());
		FileData fileData;
		if( !fileData.InitializeFileData(fileNameInfo) )
		{
			return false;
		}

		uint32 index = 0;
		const uint32 fileSize = fileData.GetDataSize();
		const char* pData = fileData.GetData();
		int entryType = 0;
		LineEntry entry;
		char buffer[2048];
		memset(buffer, 0, sizeof(buffer));
		uint32 bufferIndex = 0;

		while( index < fileSize )
		{
			if( pData[index] == ',' )
			{
				if( entryType == 0 )
				{
					entry.japanese = string(buffer);
				}
				else if( entryType == 1 )
				{
					entry.google = string(buffer);
				}
				else
				{
					entry.english = string(buffer);
				}
				memset(buffer, 0, sizeof(buffer));
				bufferIndex = 0;

				++entryType;
			}
			else if( pData[index] == 0xA )
			{
				lines.push_back(entry);
				entry.Clear();

				memset(buffer, 0, sizeof(buffer));
				bufferIndex = 0;
				entryType = 0;
			}
			else
			{
				buffer[bufferIndex++] = pData[index];
			}
			++index;
		}
	}
};
