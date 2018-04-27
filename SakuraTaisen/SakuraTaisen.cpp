/*******************************************************************************
SakuraTaisen - Tools for modifying Sakura Taisen

(c) Copyright 2018 Rizwan Ahmed aka NoahSteam

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
#include <list>
#include "..\Utils\Utils.h"

using std::vector;
using std::string;
using std::list;

struct SakuraString
{
	struct SakuraChar
	{
		SakuraChar() : mRow(0), mColumn(0){}
		SakuraChar(unsigned char inRow, unsigned char inColumn) : mRow(inRow), mColumn(inColumn){}

		unsigned char mRow;
		unsigned char mColumn;
	};

	vector<SakuraChar> mChars;

	void AddChar(char row, char column)
	{
		mChars.push_back( std::move(SakuraChar(row, column)) );
	}
};

struct SakuraTextFile
{
	FileName             mFileName;
	vector<SakuraString> mLines;

private:
	long  mFileSize;
	FILE* mpFile;	
	char* mpBuffer;

public:
	SakuraTextFile(const FileName& fileName) : mFileName(fileName), mFileSize(0), mpFile(nullptr), mpBuffer(nullptr){}

	~SakuraTextFile()
	{
		Close();
	}

	bool OpenFile()
	{
		//Open file in read-binary mode
		mpFile = nullptr;
		errno_t errorValue = fopen_s(&mpFile, mFileName.mFullPath.c_str(), "rb");
		if (errorValue)
		{
			printf("Unable to open file: %s.  Error code: %i \n", mFileName.mFileName.c_str(), errorValue);
			return false;
		}

		//Get file size
		fseek(mpFile, 0, SEEK_END);
		mFileSize = ftell(mpFile);
		fseek(mpFile, 0, SEEK_SET); //reset to the start of the file

		//Allocate buffer to read in data
		mpBuffer = new char[mFileSize];

		//Read in data
		size_t numBytesRead = fread_s(mpBuffer, mFileSize, sizeof(char), mFileSize, mpFile);
		if( numBytesRead != mFileSize )
		{
			printf("Unable to read all of file: %s.\n", mFileName.mFileName.c_str());

			Close();
			return false;
		}

		return true;
	}

	bool FindNextSakuraString(long &inOutLocation)
	{
		while( inOutLocation + 6 < mFileSize )
		{
			if (mpBuffer[inOutLocation + 0] == 0 &&
				mpBuffer[inOutLocation + 1] == 0 &&
				mpBuffer[inOutLocation + 2] == 0 &&
				mpBuffer[inOutLocation + 3] == 0 &&
				mpBuffer[inOutLocation + 4] == 0 &&
				mpBuffer[inOutLocation + 5] == 0
				)
			{
				inOutLocation += 6;
				return true;
			}

			++inOutLocation;
		}

		return false;
	}

	void ReadInText()
	{
		long currentLocation = 0;
		bool bNextStringFound = FindNextSakuraString(currentLocation);

		while( bNextStringFound && currentLocation < mFileSize )
		{
			long nextStringLocation = currentLocation;
			bNextStringFound        = FindNextSakuraString(nextStringLocation);
			const long endOfString  = bNextStringFound ? nextStringLocation - 6 : mFileSize;

			SakuraString newLineOfText;
			bool bWasValidString = false;
			while(currentLocation + 1 < endOfString )
			{
				newLineOfText.AddChar( mpBuffer[currentLocation], mpBuffer[currentLocation+1] );
				currentLocation += 2;
				bWasValidString = true;

				if(currentLocation > nextStringLocation)
				{
					break;
				}
			}

			currentLocation = nextStringLocation;

			if( bWasValidString )
			{
				mLines.push_back(newLineOfText);
			}
		}
	}

	void Close()
	{
		if( mpFile )
		{
			fclose(mpFile);
		}

		delete[] mpBuffer;

		mpBuffer = nullptr;
		mpFile  = nullptr;
	}
};

void FindAllSakuraText(const vector<FileName>& inFiles, vector<SakuraTextFile>& outText)
{
	for(const FileName& fileName : inFiles)
	{
		printf("Extracting: %s\n", fileName.mFileName.c_str());

		SakuraTextFile sakuraFile(fileName);
		if( !sakuraFile.OpenFile() )
		{
			continue;
		}

		sakuraFile.ReadInText();
		sakuraFile.Close();

		outText.push_back( std::move(sakuraFile) );
	}
}

void GetAllTextFiles(const vector<FileName>& allFiles, vector<FileName>& outFiles)
{
	const string validTextFile("TBL.BIN");

	for(const FileName& fileName : allFiles)
	{
		std::size_t found = fileName.mFileName.find(validTextFile);
		if (found != std::string::npos)
		{
			outFiles.push_back(fileName);
		}
	}
}

void DumpExtractedSakuraText(const vector<SakuraTextFile>& inSakuraTextFiles, const string& outDirectory)
{
	const string txt(".txt");

	for(const SakuraTextFile& textFile : inSakuraTextFiles)
	{
		const size_t lastIndex   = textFile.mFileName.mFileName.find_last_of(".");
		const string rawName     = textFile.mFileName.mFileName.substr(0, lastIndex);
		const string outFileName = outDirectory + rawName + txt;

		FILE* pOutFile     = nullptr;
		errno_t errorValue = fopen_s(&pOutFile, outFileName.c_str(), "w");
		if (errorValue)
		{
			printf("Unable to open out file: %s.  Error code: %i \n", outFileName.c_str(), errorValue);
			continue;
		}

		for(const SakuraString& sakuraString : textFile.mLines)
		{
			printf("Dumping text for: %s\n", textFile.mFileName.mFileName.c_str());

			for(const SakuraString::SakuraChar& sakuraChar : sakuraString.mChars)
			{
				fprintf(pOutFile, "%02x%02x ", sakuraChar.mRow, sakuraChar.mColumn);
			}

			fprintf(pOutFile, "\n");
		}		

		fclose(pOutFile);
	}
}

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		printf("usage: SakuraTaisen [dataFileDirectory outDirectory]\n");
		return 1;
	}

	const char* pSearchDirectory = argv[1];	
	const char* pOutDirectory    = argv[2];

	//Find all files within the requested directory
	vector<FileName> allFiles;
	FindAllFilesWithinDirectory(string(pSearchDirectory), allFiles);

	vector<FileName> textFiles;
	GetAllTextFiles(allFiles, textFiles);

	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(textFiles, sakuraTextFiles);

	string outDirectory = string(pOutDirectory) + string("\\");
	DumpExtractedSakuraText(sakuraTextFiles, outDirectory);
}
