/*******************************************************************************
FindData - Program that searches recursively through directories
searching for specified data

(c) Copyright 2018 Rizwan Ahmed

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*******************************************************************************/

#include <stdio.h>
#include <windows.h>
#include <vector>
#include <string>
#include <thread>
#include <list>
#include <chrono>
#include "..\Utils\Utils.h"

using std::vector;
using std::string;
using std::thread;
using std::list;

int GAvailableThreads = 0;

void FindDataWithinMemoryFile(const FileNameContainer& inFileName, const FileData& inFileData, MatchInfo& outMatch, float inPercentage)
{	
	FileData currentFile;
	
	const bool bResult = currentFile.InitializeFileData(inFileName);
	if( !bResult )
	{
		return;
	}

	printf("Looking for: %s [%0.2f%%]\n", inFileName.mFileName.c_str(), inPercentage);

	vector<unsigned long> foundOffsets;
	if( inFileData.DoesThisFileContain(currentFile, foundOffsets, false) )
	{
		outMatch.mbFoundMatch = true;
		outMatch.mFileName    = inFileName.mFileName;
		outMatch.mOffset      = foundOffsets[0];
	}

	++GAvailableThreads;
}

void FindDataWithinMemoryFiles(const vector<FileNameContainer>& inFileNames, const FileData& inFileData, vector<MatchInfo>& outMatches)
{
	const unsigned int numThreadsSupported = std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1;
	GAvailableThreads = numThreadsSupported;

	vector<MatchInfo> matchInfos;
	list<thread> threads;

	matchInfos.resize(inFileNames.size());

	FileData currentFile;
	const size_t numFiles = inFileNames.size();
	const float numFilesF = (float)numFiles;
	for (size_t fileIndex = 0; fileIndex < numFiles; ++fileIndex)
	{
		//Spawn threads
		while(fileIndex < numFiles)
		{
			const float completionPercentage = (fileIndex / (numFilesF)*100.f);

			thread newThread = thread(FindDataWithinMemoryFile, inFileNames[fileIndex], std::ref(inFileData), std::ref(matchInfos[fileIndex]), completionPercentage);
			newThread.detach();
			++fileIndex;
			--GAvailableThreads;

			while(GAvailableThreads == 0)
			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		}

		//Store results
		const size_t numResults = matchInfos.size();
		for (unsigned int resultIndex = 0; resultIndex < numResults; ++resultIndex)
		{
			if (matchInfos[resultIndex].mbFoundMatch)
			{
				outMatches.push_back(std::move(matchInfos[resultIndex]));
			}
		}

		matchInfos.clear();
		threads.clear();
	}
}

void PrintMatches(const vector<MatchInfo>& inInfo)
{
	if( inInfo.size() )
	{
		for (const MatchInfo& info : inInfo)
		{
			printf("Found a match in %s @0x%08x\n", info.mFileName.c_str(), info.mOffset);
		}
	}
	else
	{
		printf("No matches found\n");
	}
}

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		printf("usage: MemoryAnalyzer [memoryFilePath] [data files directory]\n");
		return 1;
	}

	const char* pMemoryFileName = argv[1];
	const char* pSearchDirectory = argv[2];	

	//Open up the data file
	FileData dataFile;
	if (!dataFile.InitializeFileData(pMemoryFileName, pMemoryFileName))
	{
		return false;
	}

	//Find all files within the requested directory
	vector<FileNameContainer> fileNames;
	FindAllFilesWithinDirectory(string(pSearchDirectory), fileNames);

	//Find which files have the data passed in
	vector<MatchInfo> matches;
	FindDataWithinMemoryFiles(fileNames, dataFile, matches);

	//Output matches
	PrintMatches(matches);
}
