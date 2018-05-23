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
#include "..\Utils\Utils.h"

using std::vector;
using std::string;

void FindDataWithinFiles(const vector<FileNameContainer>& inFileNames, const FileData& inFileData, vector<MatchInfo>& outMatches)
{
	FileData currentFile;
	const size_t numFiles = inFileNames.size();
	for(size_t i = 0; i < numFiles; ++i)
	{	
		const bool bResult = currentFile.InitializeFileData( inFileNames[i] );
		if( !bResult )
		{
			continue;
		}

		printf("Scanning within: %s \n", inFileNames[i].mFileName.c_str());

		vector<unsigned long> foundOffsets;
		const bool bFoundData = currentFile.DoesThisFileContain(inFileData, &foundOffsets, true);
		if( bFoundData )
		{
			for(unsigned long offset : foundOffsets)
			{
				outMatches.push_back( std::move(MatchInfo(inFileNames[i].mFileName, offset)) );
			}
		}
	}
}

void PrintMatches(const vector<MatchInfo>& inInfo)
{
	if( inInfo.size() )
	{
		for(const MatchInfo& info : inInfo)
		{
			printf("Found a match in %s @0x%08x\n", info.mFileName.c_str(), info.mOffset);
		}
	}
	else
	{
		printf("No matches found");
	}
}

int main(int argc, char *argv[])
{
	if( argc != 3 )
	{
		printf("usage: FindData [path] [data filename]\n");
		return 1;
	}

	const char* pSearchDirectory = argv[1];
	const char* pDataFileName    = argv[2];
		
	//Open up the data file
	FileData dataFile;
	if( !dataFile.InitializeFileData(pDataFileName, pDataFileName) )
	{
		return false;
	}

	//Find all files within the requested directory
	vector<FileNameContainer> fileNames;
	FindAllFilesWithinDirectory( string(pSearchDirectory), fileNames);

	//Find which files have the data passed in
	vector<MatchInfo> matches;
	FindDataWithinFiles(fileNames, dataFile, matches);

	PrintMatches(matches);
}
