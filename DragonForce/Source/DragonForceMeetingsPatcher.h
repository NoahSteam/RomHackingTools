#pragma once

struct MeetingFileInfo
{
	string FileName;
	uint32 AddressToCopyFrom;
	uint32 AddressToCopyTo;
};

bool PatchMeetingFiles(const string& inPs2DataFilePath, const string& inSaturnGamePath)
{
	FileReadWriter ps2DataFile;
	if( !ps2DataFile.OpenFile(inPs2DataFilePath) )
	{
		return false;
	}

	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inSaturnGamePath, allFiles);

	//Find all the font sheet files
	vector<FileNameContainer> meetingFiles;
	GetAllFilesOfType(allFiles, "MT_VDP", meetingFiles);

	//Setup info array
	const size_t numInfoEntries = 1;
	MeetingFileInfo meetingFileInfos[numInfoEntries] =
	{
		{"MT_VDP0", 0xE2C0, 0x07C282C0},
	};

	//Copy data over from all of the files
	for(const FileNameContainer& meetingFileName : meetingFiles)
	{
		for(size_t i = 0; i < numInfoEntries; ++i)
		{
			if( meetingFileName.mNoExtension == meetingFileInfos[i].FileName )
			{
				const string saturnFilePath = inSaturnGamePath + Seperators + meetingFileInfos[i].FileName + string(".grp");
				FileData saturnFile;
				if( saturnFile.InitializeFileData(FileNameContainer(saturnFilePath)) )
				{
					ps2DataFile.WriteData(meetingFileInfos[i].AddressToCopyTo, saturnFile.GetData() + meetingFileInfos[i].AddressToCopyFrom, saturnFile.GetDataSize() - meetingFileInfos[i].AddressToCopyFrom, false);
				}
				else
				{
					return false;
				}
			}
		}
	}

	return true;
}
