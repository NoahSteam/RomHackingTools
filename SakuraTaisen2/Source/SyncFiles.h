#pragma once

struct SyncEntry
{
	uint32 offsetToTimingData;
	uint32 syncID;

	void SwapEndianess()
	{
		offsetToTimingData = SwapByteOrder(offsetToTimingData);
		syncID = SwapByteOrder(syncID);
	}
};

//Each entry ends with 5a00
struct SyncData
{
	SyncEntry syncEntry;
	std::vector<uint8> timingData;

	int GetNumTimingValues() const
	{
		int count = 0;

		for(uint8 t : timingData)
		{
			if(t == (uint8)0x2e)
			{
				++count;
			}
		}

		return count;
	}

	bool AreTimingDatasTheSame(const std::vector<uint8>& InOtherTiming) const
	{
		if(InOtherTiming.size() != timingData.size())
		{
			return false;
		}

		const size_t numValues = timingData.size();
		for(size_t i = 0; i < numValues; ++i)
		{
			if(timingData[i] != InOtherTiming[i])
			{
				return false;
			}
		}

		return true;
	}
};

struct SW2SyncFile
{
	FileNameContainer mFileName;
	std::vector<SyncData> mSyncEntries;
	std::unordered_map<uint32, SyncData> mSyncIdToEntry;
	std::unordered_map<uint32, int> mSyncIdToEntryIndex;

	bool InitializeSyncFile(const std::string& InFilePath)
	{
		mFileName = FileNameContainer(InFilePath);

		FileData fileData;
		if(!fileData.InitializeFileData(mFileName))
		{
			return false;
		}

		//Figure out how many entries are there
		const SyncEntry* pSyncEntries = (SyncEntry*)fileData.GetData();
		uint32 numEntries = 0;
		while(1)
		{
			if((pSyncEntries[numEntries].offsetToTimingData & 0xff00) != 0x2600)
			{
				break;
			}
			++numEntries;
		}
		
		//Read in table
		SyncEntry* pSyncTable = new SyncEntry[numEntries];
		memcpy_s(pSyncTable, sizeof(SyncEntry) * numEntries, fileData.GetData(), sizeof(SyncEntry) * numEntries);

		//Read in timing data
		for (uint32 i = 0; i < numEntries; ++i)
		{
			//Fix endianness
			pSyncTable[i].SwapEndianess();

			//Create new entry
			SyncData newData;
			newData.syncEntry = pSyncTable[i];

			const uint32 offsetToTiming = newData.syncEntry.offsetToTimingData & 0xffff;

			//Read in timing data
			uint8* pTimingData = (uint8*)(fileData.GetData() + offsetToTiming);
			while(*pTimingData != 0x0)
			{
				newData.timingData.push_back(*pTimingData);
				++pTimingData;
			}
		
			//Add entry
			mSyncEntries.push_back(newData);
			mSyncIdToEntry[newData.syncEntry.syncID] = newData;
			mSyncIdToEntryIndex[newData.syncEntry.syncID] = (int)mSyncEntries.size() - 1;
		}

		return true;
	}
};

void FindAllSyncData(const vector<FileNameContainer>& inFiles, vector<SW2SyncFile>& outData)
{
	for(const FileNameContainer& fileName : inFiles)
	{
		SW2SyncFile syncFile;
		if(syncFile.InitializeSyncFile(fileName.mFullPath))
		{
			outData.push_back(syncFile);
		}
	}
}

