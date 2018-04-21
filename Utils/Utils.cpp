#include <stdio.h>
#include <windows.h>
#include <vector>
#include <string>

#include "Utils.h"

using std::string;
using std::vector;

void FindAllFilesWithinDirectory(const string& inDirectoryPath, vector<FileName>& outFileNames)
{
	WIN32_FIND_DATA fileData;

	const string currentPath = inDirectoryPath + string("\\");
	const string anyFilePath = inDirectoryPath + string("\\*");

	//get the first file in the directory
	HANDLE result = FindFirstFile(anyFilePath.c_str(), &fileData);

	while (result != INVALID_HANDLE_VALUE)
	{
		unsigned nameLength = static_cast<unsigned> (strlen(fileData.cFileName));

		//skip if the file is just a '.'
		if (fileData.cFileName[0] == '.')
		{
			if (!FindNextFile(result, &fileData))
				break;
			continue;
		}

		if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && strcmp(fileData.cFileName, ".") != 0 && strcmp(fileData.cFileName, "..") != 0)
		{
			const string subDirectoryPath = inDirectoryPath + string("\\") + string(fileData.cFileName);
			FindAllFilesWithinDirectory(subDirectoryPath, outFileNames);
		}
		else
		{
			string filePath = currentPath + string(fileData.cFileName);
			outFileNames.push_back(std::move(FileName(fileData.cFileName, filePath.c_str())));
		}

		if (!FindNextFile(result, &fileData))
			break;
	}
}

////////////////////////////
//        FileData        //
////////////////////////////
FileData::~FileData()
{
	//release memory
	delete[] mpData;
	mpData = nullptr;
}

bool FileData::OpenFile(const char* pFileName)
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

	//Allocate bufferA
	if (mBufferSize < mFileSize)
	{
		delete[] mpData;

		mpData = new char[mFileSize];
		mBufferSize = mFileSize;
	}

	//read in data
	fread((void*)mpData, sizeof(char), mFileSize, pFile);

	//close the file
	fclose(pFile);

	return true;
}

bool FileData::IsDataTheSame(const char* pData1, const char* pData2, const unsigned long memSize) const
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

bool FileData::InitializeFileData(const FileName& inFileData)
{
	mFileName = inFileData.mFileName;
	mFullPath = inFileData.mFullPath;

	return OpenFile(mFullPath.c_str());
}

bool FileData::InitializeFileData(const char* pFileName, const char* pFullPath)
{
	mFileName = pFileName;
	mFullPath = pFullPath;

	return OpenFile(mFullPath.c_str());
}

bool FileData::DoesThisFileContain(const FileData& otherFile, unsigned long& outOffset) const
{	
	outOffset = 0;

	if( otherFile.mFileSize > mFileSize )
	{
		return false;
	}

	unsigned long       currentOffset = 0;
	const unsigned long finalOffset = mFileSize - otherFile.mFileSize;

	while( currentOffset <= finalOffset )
	{
		if( IsDataTheSame(mpData + currentOffset, otherFile.mpData, otherFile.mFileSize) )
		{
			outOffset = currentOffset;
			return true;
		}

		++currentOffset;
	}

	return false;
}

bool FileData::DataComparer::DoesFile1ContainFile2(unsigned long& outOffset) const
{
	outOffset = 0;

	if(mFile2.mFileSize > mFile1.mFileSize)
	{
		return false;
	}

	unsigned long       currentOffset = 0;
	const unsigned long finalOffset = mFile1.mFileSize - mFile2.mFileSize;

	while(currentOffset <= finalOffset)
	{
		const char* pData1 = mFile1.mpData + currentOffset;
		const char* pData2 = mFile2.mpData;

		bool bDataIsTheSame = true;
		for(unsigned long currIndex = 0; currIndex < mFile2.mFileSize; ++currIndex)
		{
			if( pData1[currIndex] != pData2[currIndex] )
			{
				bDataIsTheSame = false;
				break;
			}
		}

		if( bDataIsTheSame )
		{
			outOffset = currentOffset;
			return true;
		}

		++currentOffset;
	}

	return false;
}

bool FileData::Async_DoesThisFileContain(const FileData& thisFile, const FileData& otherFile, unsigned long& outOffset)
{
	FileData::DataComparer* pNewCompare = new FileData::DataComparer(thisFile, otherFile);

	const bool bResult = pNewCompare->DoesFile1ContainFile2(outOffset);

	delete pNewCompare;

	return bResult;
}