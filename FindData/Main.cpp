#include <stdio.h>
#include <windows.h>
#include <vector>
#include <string>

using std::vector;
using std::string;

struct MatchInfo
{
	string mFileName;

	MatchInfo(const string& inString) : mFileName(inString) {}
};

struct FileName
{
	FileName(const char* pFileName, const char* pFullPath) : mFileName(pFileName), mFullPath(pFullPath) {}

	string mFileName;
	string mFullPath;
};

class FileData
{
	string        mFileName;
	string        mFullPath;
	unsigned long mFileSize;
	const char*   mpData;

private:
	bool OpenFile(const char* pFileName)
	{
		//Open file in read-binary mode
		FILE* pFile = nullptr;
		errno_t errorValue = fopen_s(&pFile, pFileName, "rb");
		if (errorValue)
		{
			printf("Unable to open file: %s.  Error code: %i \n", pFileName, errorValue);
			return false;
		}

		//Figure out the file size by seeking to the end of the file and requesting the position of the file
		fseek(pFile, 0, SEEK_END);
		mFileSize = ftell(pFile);
		fseek(pFile, 0, SEEK_SET); //reset to the start of the file

		if (!mFileSize)
		{

			printf("No data found within file: %s \n", pFileName);
			return false;
		}
		
		//Allocate buffer
		mpData = new char[mFileSize];

		//read in data
		fread((void*)mpData, sizeof(char), mFileSize, pFile);

		//close the file
		fclose(pFile);

		return true;
	}

	bool IsDataTheSame(const char* pData1, const char* pData2, const unsigned long memSize)
	{
		for(unsigned long currIndex = 0; currIndex < memSize; ++currIndex)
		{
			if( pData1[currIndex] != pData2[currIndex] )
			{
				return false;
			}
		}

		return true;
	}

public:
	FileData() : mFileSize(0), mpData(nullptr) {}

	~FileData()
	{
		//release memory
		delete[] mpData;
		mpData = nullptr;
	}

	bool InitializeFileData(const FileName& inFileData)
	{
		mFileName = inFileData.mFileName;
		mFullPath = inFileData.mFullPath;

		return OpenFile(mFullPath.c_str());
	}

	bool InitializeFileData(const char* pFileName, const char* pFullPath)
	{
		mFileName = pFileName;
		mFullPath = pFullPath;

		return OpenFile(mFullPath.c_str());
	}

	bool DoesThisFileContain(const FileData& otherFile)
	{
		printf("Scanning within: %s \n", mFileName.c_str());

		if( otherFile.mFileSize > mFileSize )
		{
			return false;
		}

		unsigned long       currentOffset = 0;
		const unsigned long finalOffset   = mFileSize - otherFile.mFileSize;

		while( currentOffset <= finalOffset )
		{
			if( IsDataTheSame(mpData + currentOffset, otherFile.mpData, otherFile.mFileSize) )
			{
				return true;
			}

			++currentOffset;
		}

		return false;
	}
};

void FindAllFilesWithinDirectory(const string& inDirectoryPath, vector<FileName>& outFileNames)
{
	WIN32_FIND_DATA fileData;

	const string currentPath = inDirectoryPath + string("\\");
	const string anyFilePath = inDirectoryPath + string("\\*");
	
	//get the first file in the directory
	HANDLE result = FindFirstFile(anyFilePath.c_str(), &fileData);

	while( result != INVALID_HANDLE_VALUE )
	{
		unsigned nameLength = static_cast<unsigned> (strlen(fileData.cFileName));

		//skip if the file is just a '.'
		if( fileData.cFileName[0] == '.' )
		{
			if( !FindNextFile(result, &fileData) )
				break;
			continue;
		}

		if( fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && strcmp(fileData.cFileName, ".") != 0 && strcmp(fileData.cFileName, "..") != 0)
		{
			const string subDirectoryPath = inDirectoryPath + string("\\") + string(fileData.cFileName);
			FindAllFilesWithinDirectory(subDirectoryPath, outFileNames);
		}
		else
		{
			string filePath = currentPath + string(fileData.cFileName);
			outFileNames.push_back(std::move(FileName(fileData.cFileName, filePath.c_str())));
		}		

		if( !FindNextFile(result, &fileData) )
			break;
	}
}

void FindDataWithinFiles(const vector<FileName>& inFileNames, const FileData& inFileData, vector<MatchInfo>& outMatches)
{
	const size_t numFiles = inFileNames.size();
	for(size_t i = 0; i < numFiles; ++i)
	{
		FileData currentFile;
		const bool bResult = currentFile.InitializeFileData( inFileNames[i] );
		if( !bResult )
		{
			continue;
		}

		const bool bFoundData = currentFile.DoesThisFileContain(inFileData);
		if( bFoundData )
		{
			outMatches.push_back( std::move(MatchInfo(inFileNames[i].mFileName)) );			
		}
	}
}

void PrintMatches(const vector<MatchInfo>& inInfo)
{
	for(const MatchInfo& info : inInfo)
	{
		printf("Found a match: %s \n", info.mFileName.c_str());
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
	vector<FileName> fileNames;
	FindAllFilesWithinDirectory( string(pSearchDirectory), fileNames);

	//Find which files have the data passed in
	vector<MatchInfo> matches;
	FindDataWithinFiles(fileNames, dataFile, matches);

	PrintMatches(matches);
}
