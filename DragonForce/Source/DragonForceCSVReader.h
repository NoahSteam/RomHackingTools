#pragma once

class DragonForceCSVReader
{
public:

	struct LineEntry
	{
		string japanese;
		string google;
		string english;
		bool bShouldPatch{false};

		void Clear()
		{
			japanese.clear();
			google.clear();
			english.clear();
			bShouldPatch = false;
		}
	};

	vector<LineEntry> mLines;

	bool DoesEntryExist(const LineEntry& inEntry) const
	{
		for( const LineEntry& existingEntry : mLines )
		{
			if( existingEntry.japanese == inEntry.japanese )
			{
				printf("Ignoring entry for: %s with Google: %s and Eng: %s \n", inEntry.japanese.c_str(), inEntry.google.c_str(), inEntry.english.c_str());
				return true;
			}
		}

		return false;
	}

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
				else if(entryType == 2)
				{
					entry.english = string(buffer);
				}
				memset(buffer, 0, sizeof(buffer));
				bufferIndex = 0;

				++entryType;
			}

			//Newline
			else if( pData[index] == '\n' || pData[index] == '\r' )
			{
				//Should patch
				entry.bShouldPatch = buffer[0] != '0';

				if( entry.bShouldPatch && !DoesEntryExist(entry) )
				{
					mLines.push_back(entry);
				}

				//Reset entry
				entry.Clear();

				memset(buffer, 0, sizeof(buffer));
				bufferIndex = 0;
				entryType = 0;

				//Skip next newline character
				if( pData[index + 1] == '\n' || pData[index + 1] == '\r' )
				{
					++index;
				}
			}
			else
			{
				buffer[bufferIndex++] = pData[index];
			}
			++index;
		}

		return true;
	}

};
