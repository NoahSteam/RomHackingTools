#pragma once

bool UpdateSyncDataFromSKFiles(SW2SyncFile& inSyncFile, vector<SakuraTextFile>& inSakuraFiles)
{
	//Gather sync ids from all sakura files for this sync file into a map
	//Go through all sync ids in the sync file and fill out the timing data in a new container
	//	-If an id is not found, use the data in the sync file
	//  -Keep track of the offsets
	//Update sync file data

	//Figure out which sync file this is (01, 02, etc.)
	std::string syncFileDigits;
	if (isdigit(*(char*)(inSyncFile.mFileName.mNoExtension.c_str() + 4)))
	{
		syncFileDigits += inSyncFile.mFileName.mNoExtension.at(4);
		syncFileDigits += inSyncFile.mFileName.mNoExtension.at(5);
	}
	else
	{
		return true;
	}
	
	//Grab all sync data from SK files associated with this sync file
	SyncIdToSyncData syncIdsFromSkFiles;
	for (SakuraTextFile& skFile : inSakuraFiles)
	{
		string skFileNumber;
		if (isdigit(*(char*)(skFile.mFileNameInfo.mNoExtension.c_str() + 2)))
		{
			skFileNumber += skFile.mFileNameInfo.mNoExtension.at(2);
			skFileNumber += skFile.mFileNameInfo.mNoExtension.at(3);
		}

		if (skFileNumber != syncFileDigits)
		{
			continue;
		}

		const int numLinesInSkFile = (int)skFile.mLines.size();

		//Create map of all syncIds and their data
		for (SyncIdToSyncData::iterator iter = skFile.mSyncFileData.mPatchedSyncIdToEntry.begin(); iter != skFile.mSyncFileData.mPatchedSyncIdToEntry.end(); ++iter)
		{
			if (syncIdsFromSkFiles.find(iter->first) != syncIdsFromSkFiles.end())
			{
				continue;
			}

			//Only patch auto-resume lines.  These are lines that have 0xfa in the 3rd last byte and something besides 0xfc in the first byte
			bool bNeedsSyncingFixup = false;
			if(skFile.mLipSyncIdToSequenceEntryMap.find(iter->first) != skFile.mLipSyncIdToSequenceEntryMap.end())
			{
				const SakuraTextFile::SequenceEntry& sequenceEntry = skFile.mLipSyncIdToSequenceEntryMap[iter->first];
				const int textIndex = sequenceEntry.mTextIndex - 1;
				if(textIndex > numLinesInSkFile)
				{
					printf("Invalid line index %i in %s\n", textIndex, skFile.mFileNameInfo.mFileName.c_str());
					continue;
				}

				const SakuraString& sakuraLine = skFile.mLines[textIndex];

				const int numChars = (int)sakuraLine.mChars.size();
				if(numChars > 3)
				{
					//This line requires its sync data to be fixed up
					if( sakuraLine.mChars[0].mIndex != (uint16)0xfc && sakuraLine.mChars[numChars - 3].mIndex == (uint16)0xfa )
					{
						bNeedsSyncingFixup = true;

						printf("LipSyncFix: Line: %i in %s\n", textIndex, skFile.mFileNameInfo.mFileName.c_str());
					}
				}
			}

			if(bNeedsSyncingFixup)
			{
				syncIdsFromSkFiles[iter->first] = iter->second;
			}
		}
	}

	//Array of timing data
	std::vector<TimingDataVector> newTimingDataArray;
	for (const SyncData& syncData : inSyncFile.mSyncEntries)
	{
		//If we don't have an entry, use the existing timing data from this sk file
		if (syncIdsFromSkFiles.find(syncData.syncEntry.syncID) == syncIdsFromSkFiles.end())
		{
			newTimingDataArray.push_back(syncData.timingData);
		}
		else
		{
			//Otherwise use the timing data from the updated sync file
			newTimingDataArray.push_back(syncIdsFromSkFiles[syncData.syncEntry.syncID].timingData);
		}
	}

	//Populate patched sync data
	inSyncFile.mPatchedSyncEntries = inSyncFile.mSyncEntries;
	assert(newTimingDataArray.size() == inSyncFile.mPatchedSyncEntries.size());

	//Generate patched data
	uint32 timingDataOffset = inSyncFile.mSyncEntries.size() * sizeof(SyncEntry);
	const size_t numEntries = inSyncFile.mSyncEntries.size();	
	for(size_t i = 0; i < numEntries; ++i)
	{
		SyncData& patchedSyncData = inSyncFile.mPatchedSyncEntries[i];

		patchedSyncData.timingData = newTimingDataArray[i];
		patchedSyncData.syncEntry.offsetToTimingData = (patchedSyncData.syncEntry.offsetToTimingData & 0xffff0000) + timingDataOffset;

		timingDataOffset += (uint32)patchedSyncData.timingData.size();

		if(timingDataOffset > 0x20000)
		{
			printf("UpdateSyncDataFromSKFiles: Timing data offset is too big in %s.\n", inSyncFile.mFileName.mFileName.c_str());
			return false;
		}
	}

	FileReadWriter syncFileWriter;
	if(!syncFileWriter.OpenFile(inSyncFile.mFileName.mFullPath))
	{
		return false;
	}

	//Write header
	unsigned long writeOffset = 0;
	const size_t numPatchedSyncEntries = inSyncFile.mPatchedSyncEntries.size();
	for (size_t i = 0; i < numPatchedSyncEntries; ++i)
	{
		const SyncData& patchedSyncData = inSyncFile.mPatchedSyncEntries[i];
		SyncEntry entryToWrite = patchedSyncData.syncEntry;
		entryToWrite.SwapEndianess();

		syncFileWriter.WriteData(writeOffset, (char*)&entryToWrite, sizeof(entryToWrite), false);

		writeOffset += sizeof(entryToWrite);
	}

	//Write timings
	for(const TimingDataVector& timingData : newTimingDataArray)
	{
		syncFileWriter.WriteData(writeOffset, (const char*)timingData.data(), timingData.size(), false);

		writeOffset += timingData.size();
	}

	const uint32 maxFileSize = 0x20000;
	if(writeOffset > maxFileSize)
	{
		printf("File %s is too big.  Should be %i, is %i\n", inSyncFile.mFileName.mFileName.c_str(), maxFileSize, writeOffset);
	}
	return true;
}

bool PatchLipSyncDataForAdventure(const string& inPatchedDirectory)
{
	printf("Patching LipSync Data for SK files\n");

	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inPatchedDirectory + string("SAKURA1\\"), allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> scenarioFiles;
	GetAllFilesOfType(allFiles, "SK0", scenarioFiles);
	GetAllFilesOfType(allFiles, "SK1", scenarioFiles);

	vector<SakuraTextFile> skFiles;
	FindAllSakuraText(scenarioFiles, skFiles, true);

	for (SakuraTextFile& skFile : skFiles)
	{
		skFile.GenerateSyncData();
	}

	//Extract sync data
	vector<FileNameContainer> syncFileNames;
	vector<SW2SyncFile> syncFiles;
	GetAllFilesOfType(allFiles, "SYNC", syncFileNames);
	FindAllSyncData(syncFileNames, syncFiles);

	for (SW2SyncFile& syncFile : syncFiles)
	{
		if(!UpdateSyncDataFromSKFiles(syncFile, skFiles))
		{
			return false;
		}
	}

	return true;
}

void ValidateSyncData(const string& inSakura1Directory, const string& inOutputDirectory)
{
	CreateDirectoryHelper(inOutputDirectory);

	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inSakura1Directory, allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> scenarioFiles;
	GetAllFilesOfType(allFiles, "SK0", scenarioFiles);
	GetAllFilesOfType(allFiles, "SK1", scenarioFiles);
	GetAllFilesOfType(allFiles, "SKC", scenarioFiles);

	//Extract the text
	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(scenarioFiles, sakuraTextFiles, false);

	//Extract sync data
	vector<FileNameContainer> syncFileNames;
	vector<SW2SyncFile> syncFiles;
	GetAllFilesOfType(allFiles, "SYNC", syncFileNames);
	FindAllSyncData(syncFileNames, syncFiles);

	struct SyncDataInfo
	{
		const char* pFileName;
		uint16 syncId;
	};

	//Make sure all sync ids from the sakuraTextFiles exist within the syncFiles and vice-versa

	//Gather all sync data
	std::unordered_map<std::string, std::unordered_set<uint16>> syncIds;
	for (const SW2SyncFile& syncFile : syncFiles)
	{
		for (const SyncData& syncData : syncFile.mSyncEntries)
		{
			syncIds[syncFile.mFileName.mNoExtension].insert(syncData.syncEntry.syncID);
		}
	}

	typedef std::unordered_map<uint16, SyncDataInfo> SyncIDToSyncData;
	typedef std::unordered_map<std::string, SyncIDToSyncData> SyncFileToSyncData;

	SyncFileToSyncData syncDataForSyncFiles;

	//TODO: Once you find 0102, keep going till you find another 0102. In between if a 01013 is found, then textId and lipId preceeds it 

	//Find sync files for each SK file
	for (const SakuraTextFile& sakuraFile : sakuraTextFiles)
	{
		std::string syncFileName("SYNC");
		if (isdigit(*(char*)(sakuraFile.mFileNameInfo.mNoExtension.c_str() + 2)))
		{
			syncFileName += sakuraFile.mFileNameInfo.mNoExtension.at(2);
			syncFileName += sakuraFile.mFileNameInfo.mNoExtension.at(3);
		}
		else
		{
			continue;
		}

		int expectedTextId = 1;
		if (sakuraFile.mSequenceEntries.size())
		{
			for (const SakuraTextFile::SequenceEntry& seqEntry : sakuraFile.mSequenceEntries)
			{
				if (seqEntry.mTextIndex != expectedTextId)
				{
					expectedTextId = seqEntry.mTextIndex;
				}
				++expectedTextId;

				if (seqEntry.mLipSyncId)
				{
					if (syncIds.find(syncFileName) != syncIds.end())
					{
						const auto& syncIdSet = syncIds[syncFileName];
						if (syncIdSet.find(seqEntry.mLipSyncId) == syncIdSet.end())
						{
							printf("%s: 0x%02x\n", syncFileName.c_str(), seqEntry.mLipSyncId);
						}
					}

					SyncDataInfo info;
					info.pFileName = sakuraFile.mFileNameInfo.mNoExtension.c_str();
					info.syncId = seqEntry.mLipSyncId;
					syncDataForSyncFiles[syncFileName][seqEntry.mLipSyncId] = info;
				}
			}
		}
		else
		{
			printf("No sync file for %s\n", sakuraFile.mFileNameInfo.mFileName.c_str());
		}
	}


	for (const SakuraTextFile& sakuraFile : sakuraTextFiles)
	{
		printf("%s: %i, TextLines: %i\n", sakuraFile.mFileNameInfo.mNoExtension.c_str(), (int)sakuraFile.mSequenceEntries.size(), (int)sakuraFile.mLines.size());

		const string outFileName = inOutputDirectory + sakuraFile.mFileNameInfo.mNoExtension + ".txt";
		TextFileWriter outFile;
		outFile.OpenFileForWrite(outFileName);

		for (const SakuraTextFile::SequenceEntry& seqEntry : sakuraFile.mSequenceEntries)
		{
			outFile.Printf("Entry: %02x Sync: %02x 0x%05x ", seqEntry.mTextIndex, seqEntry.mLipSyncId, seqEntry.mAddress);

			if (seqEntry.mTextIndex >= sakuraFile.mLines.size())
			{
				outFile.Printf("INVALID NUMBER OF LINES IN %s\n", sakuraFile.mFileNameInfo.mFileName.c_str());
			}
			else
			{
				const int numPrintedCharacters = sakuraFile.mLines[seqEntry.mTextIndex - 1].GetNumberOfActualCharacters();

				const uint32 lipSyncId = seqEntry.mLipSyncId;
				if (sakuraFile.mSyncFileData.mSyncIdToEntry.find(lipSyncId) != sakuraFile.mSyncFileData.mSyncIdToEntry.end())
				{
					const SyncData& syncData = sakuraFile.mSyncFileData.mSyncIdToEntry.at(lipSyncId);

					const int numTimingValues = syncData.GetNumTimingValues();
					if (abs(numTimingValues - numPrintedCharacters) > 1)
					{
			//			outFile.Printf("MISMATCH: ExpectedCharacters: %i, SK_Characters: %i Timing: ", numTimingValues, numPrintedCharacters);
					}
		//			else
					{
						outFile.Printf("ExpectedCharacters: %i, SK_Characters: %i ", numTimingValues, numPrintedCharacters);
					}

					for (uint8 t : syncData.timingData)
					{
						outFile.Printf("%02x ", t);
					}

					outFile.Printf("\n");
				}
				else
				{
					outFile.Printf("\n");
				}
			}
		}
	}
}

bool CreateExtraCharacterEntriesInTimingData(TimingDataVector& inTimingVector, int inNumCharacters, int inLineNumber, const std::string& inFileName)
{
	int numExistingChars = 0;
	for(uint8 value : inTimingVector)
	{
		if(value == (uint8)0x4e || value == (uint8)0x2e)
		{
			++numExistingChars;
		}
	}

	if(numExistingChars < inNumCharacters)
	{
		int numCharsToAdd = inNumCharacters - numExistingChars;
		for (uint8& value : inTimingVector)
		{
			if (!(value == (uint8)0x4e || value == (uint8)0x2e))
			{
				value = (uint8)0x2e;
				--numCharsToAdd;
				if(numCharsToAdd == 0)
				{
					break;
				}
			}
		}

		if (numCharsToAdd > 0)
		{
			printf("CreateExtraCharacterEntriesInTimingData: Remove %i characters from line: %i (%s)\n", numCharsToAdd, inLineNumber, inFileName.c_str());
		}

		return true;
	}

	return false;
}

bool HackUpdateSyncDataForAutoResumeLines(const string& inPatchedDirectory, vector<SakuraTextFile>& inSakuraFiles)
{
	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inPatchedDirectory + string("SAKURA1\\"), allFiles);

	vector<FileNameContainer> syncFileNames;
	vector<SW2SyncFile> syncFiles;
	GetAllFilesOfType(allFiles, "SYNC", syncFileNames);
	FindAllSyncData(syncFileNames, syncFiles);

	for(const SakuraTextFile& sakuraFile : inSakuraFiles)
	{
		std::string syncFileName("SYNC");
		if (isdigit(*(char*)(sakuraFile.mFileNameInfo.mNoExtension.c_str() + 2)))
		{
			syncFileName += sakuraFile.mFileNameInfo.mNoExtension.at(2);
			syncFileName += sakuraFile.mFileNameInfo.mNoExtension.at(3);
		}
		else
		{
			continue;
		}

		//Find sync file for this SK file
		SW2SyncFile* pSyncFile = nullptr;
		for(SW2SyncFile& syncFile : syncFiles)
		{
			if(syncFile.mFileName.mNoExtension == syncFileName)
			{
				pSyncFile = &syncFile;
			}
		}

		if(!pSyncFile)
		{
			continue;
		}

		FileReadWriter syncFileWriter;
		if (!syncFileWriter.OpenFile(pSyncFile->mFileName.mFullPath))
		{
			return false;
		}
		
		const int numLines = (int)sakuraFile.mLines.size();
		for(int lineNumber = 0; lineNumber < numLines; ++lineNumber)
		{
			const SakuraString& sakuraLine = sakuraFile.mLines[lineNumber];
			const int numChars = (int)sakuraLine.mChars.size();
			if(numChars < 3)
			{
				continue;
			}

			//This line requires its sync data to be fixed up
			if(sakuraLine.mChars[0].mIndex != (uint16)0xfc && sakuraLine.mChars[numChars-3].mIndex == (uint16)0xfa)
			{
				const SakuraTextFile::SequenceEntry* pSequenceEntry = sakuraFile.GetSequenceEntryForLineNumber(lineNumber + 1);
				if(!pSequenceEntry)
				{
					printf("No sync data found for line: %i in %s within %s\n", lineNumber+1, sakuraFile.mFileNameInfo.mFileName.c_str(), pSyncFile->mFileName.mFileName.c_str());
					continue;
				}

				const uint16 lipSyncId = pSequenceEntry->mLipSyncId;
				if(pSyncFile->mSyncIdToEntry.find(lipSyncId) == pSyncFile->mSyncIdToEntry.end())
				{
					printf("No sync data found for line: %i in %s within %s\n", lineNumber+1, sakuraFile.mFileNameInfo.mFileName.c_str(), pSyncFile->mFileName.mFileName.c_str());
					continue;//return false;
				}

				SyncData* pSyncData = &pSyncFile->mSyncIdToEntry[lipSyncId];
				if(CreateExtraCharacterEntriesInTimingData(pSyncData->timingData, sakuraLine.GetNumberOfPrintedCharacters(), lineNumber+1, sakuraFile.mFileNameInfo.mNoExtension))
				{
					syncFileWriter.WriteData(pSyncData->syncEntry.offsetToTimingData & 0xffff, (const char*)pSyncData->timingData.data(), pSyncData->timingData.size(), false);

					printf("Hack Fixed SyncData for line: %i in %s within %s\n", lineNumber, sakuraFile.mFileNameInfo.mFileName.c_str(), pSyncFile->mFileName.mFileName.c_str());
				}
			}
		}
	}

	return true;
}

bool HackFixAutoResumeLines(const string& inPatchedDirectory)
{
	printf("HackFixAutoResumeLines\n");

	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inPatchedDirectory + string("SAKURA1\\"), allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> scenarioFiles;
	GetAllFilesOfType(allFiles, "SK0", scenarioFiles);
	GetAllFilesOfType(allFiles, "SK1", scenarioFiles);

	vector<SakuraTextFile> skFiles;
	FindAllSakuraText(scenarioFiles, skFiles, true);
		
	if (!HackUpdateSyncDataForAutoResumeLines(inPatchedDirectory, skFiles))
	{
		return false;
	}
	
	return true;
}

bool VerifySyncDataForAutoResumeLines(const string& inPatchedDirectory, vector<SakuraTextFile>& inSakuraFiles)
{
	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inPatchedDirectory + string("SAKURA1\\"), allFiles);

	vector<FileNameContainer> syncFileNames;
	vector<SW2SyncFile> syncFiles;
	GetAllFilesOfType(allFiles, "SYNC", syncFileNames);
	FindAllSyncData(syncFileNames, syncFiles);

	for (const SakuraTextFile& sakuraFile : inSakuraFiles)
	{
		std::string syncFileName("SYNC");
		if (isdigit(*(char*)(sakuraFile.mFileNameInfo.mNoExtension.c_str() + 2)))
		{
			syncFileName += sakuraFile.mFileNameInfo.mNoExtension.at(2);
			syncFileName += sakuraFile.mFileNameInfo.mNoExtension.at(3);
		}
		else
		{
			continue;
		}

		//Find sync file for this SK file
		SW2SyncFile* pSyncFile = nullptr;
		for (SW2SyncFile& syncFile : syncFiles)
		{
			if (syncFile.mFileName.mNoExtension == syncFileName)
			{
				pSyncFile = &syncFile;
			}
		}

		if (!pSyncFile)
		{
			continue;
		}

		FileReadWriter syncFileWriter;
		if (!syncFileWriter.OpenFile(pSyncFile->mFileName.mFullPath))
		{
			return false;
		}

		const int numLines = (int)sakuraFile.mLines.size();
		for (int lineNumber = 0; lineNumber < numLines; ++lineNumber)
		{
			const SakuraString& sakuraLine = sakuraFile.mLines[lineNumber];
			const int numChars = (int)sakuraLine.mChars.size();
			if (numChars < 3)
			{
				continue;
			}

			//This line requires its sync data to be fixed up
			if (sakuraLine.mChars[0].mIndex != (uint16)0xfc && sakuraLine.mChars[numChars - 3].mIndex == (uint16)0xfa)
			{
				const SakuraTextFile::SequenceEntry* pSequenceEntry = sakuraFile.GetSequenceEntryForLineNumber(lineNumber + 1);
				if (!pSequenceEntry)
				{
					printf("No sync data found for line: %i in %s within %s\n", lineNumber + 1, sakuraFile.mFileNameInfo.mFileName.c_str(), pSyncFile->mFileName.mFileName.c_str());
					continue;
				}

				const uint16 lipSyncId = pSequenceEntry->mLipSyncId;
				if (pSyncFile->mSyncIdToEntry.find(lipSyncId) == pSyncFile->mSyncIdToEntry.end())
				{
					printf("No sync data found for line: %i in %s within %s\n", lineNumber + 1, sakuraFile.mFileNameInfo.mFileName.c_str(), pSyncFile->mFileName.mFileName.c_str());
					continue;//return false;
				}
			}
		}
	}

	return true;
}

bool VerifyAutoResumeLines(const string& inPatchedDirectory)
{
	printf("HackFixAutoResumeLines\n");

	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inPatchedDirectory + string("SAKURA1\\"), allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> scenarioFiles;
	GetAllFilesOfType(allFiles, "SK0", scenarioFiles);
	GetAllFilesOfType(allFiles, "SK1", scenarioFiles);

	vector<SakuraTextFile> skFiles;
	FindAllSakuraText(scenarioFiles, skFiles, true);

	if (!VerifySyncDataForAutoResumeLines(inPatchedDirectory, skFiles))
	{
		return false;
	}

	return true;
}