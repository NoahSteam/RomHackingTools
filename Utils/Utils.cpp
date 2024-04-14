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
#include <assert.h>
#include <algorithm> // for std::sort
#include <unordered_map>
#include <unordered_set>

#include "Utils.h"
#include "..\ExternalTools\lodepng.h"
#include "decompress_rtns.h"
#include "..\ExternalTools\crc.h"

using std::string;
using std::vector;

const string Seperators("\\");

//CRC Code is from: https://www.codeguru.com/cpp/cpp/algorithms/checksum/article.php/c5103/CRC32-Generating-a-checksum-for-a-file.htm
// Static CRC table
DWORD GCrc32Table[256] =
{
	0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
	0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
	0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
	0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
	0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
	0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
	0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
	0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
	0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
	0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
	0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
	0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
	0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
	0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
	0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
	0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,

	0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
	0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
	0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
	0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
	0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
	0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
	0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
	0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
	0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
	0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
	0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
	0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
	0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
	0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
	0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
	0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,

	0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
	0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
	0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
	0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
	0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
	0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
	0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
	0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
	0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
	0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
	0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
	0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
	0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
	0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
	0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
	0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,

	0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
	0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
	0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
	0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
	0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
	0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
	0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
	0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
	0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
	0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
	0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
	0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
	0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
	0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
	0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
	0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};

void CalcCrc32(const BYTE byte, DWORD &dwCrc32)
{
	dwCrc32 = ((dwCrc32) >> 8) ^ GCrc32Table[(byte) ^ ((dwCrc32) & 0x000000FF)];
}

uint32 CalculateDataCRC(char* pData, uint32 dataSize)
{
	return CRC::Calculate(pData, dataSize, CRC::CRC_32());
}

void SwapByteOrderInPlace(char* pData, unsigned int numBytes)
{
	std::reverse(pData, (pData + numBytes));
}

int FourByteAlign(int InValue)
{
	return InValue + (4 - (InValue % 4));
}

void FindAllFilesWithinDirectory(const string& inDirectoryPath, vector<FileNameContainer>& outFileNames)
{
	WIN32_FIND_DATA fileData;

	const string currentPath = inDirectoryPath + string("\\");
	const string anyFilePath = inDirectoryPath + string("\\*");

	//get the first file in the directory
	HANDLE result = FindFirstFile(anyFilePath.c_str(), &fileData);

	while (result != INVALID_HANDLE_VALUE)
	{
		//unsigned nameLength = static_cast<unsigned> (strlen(fileData.cFileName));

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
			outFileNames.push_back(std::move(FileNameContainer(fileData.cFileName, filePath.c_str())));
		}

		if (!FindNextFile(result, &fileData))
			break;
	}
}

void FindAllDirectoriesWithinDirectory(const std::string& inDirectoryPath, std::vector<string>& outDirectories)
{
	WIN32_FIND_DATA fileData;

	const string currentPath = inDirectoryPath + string("\\");
	const string anyFilePath = inDirectoryPath + string("\\*");

	//get the first file in the directory
	HANDLE result = FindFirstFile(anyFilePath.c_str(), &fileData);

	while (result != INVALID_HANDLE_VALUE)
	{
		//unsigned nameLength = static_cast<unsigned> (strlen(fileData.cFileName));

		//skip if the file is just a '.'
		if (fileData.cFileName[0] == '.')
		{
			if (!FindNextFile(result, &fileData))
				break;
			continue;
		}

		if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && strcmp(fileData.cFileName, ".") != 0 && strcmp(fileData.cFileName, "..") != 0)
		{
			const string subDirectoryPath = fileData.cFileName;
			outDirectories.push_back(subDirectoryPath);
		}

		if (!FindNextFile(result, &fileData))
			break;
	}
}

bool DoesDirectoryExist(const std::string& dirName)
{
	DWORD fileAttributes = GetFileAttributesA(dirName.c_str());
	if( fileAttributes == INVALID_FILE_ATTRIBUTES )
	{
		return false;
	}

	if( fileAttributes & FILE_ATTRIBUTE_DIRECTORY )
	{
		return true;
	}

	return false;
}

bool DoesFileExist(const std::string& dirName)
{
	DWORD fileAttributes = GetFileAttributesA(dirName.c_str());
	if (fileAttributes == INVALID_FILE_ATTRIBUTES || fileAttributes == INVALID_FILE_SIZE)
	{
		return false;
	}

	return true;
}

bool CreateDirectoryHelper(const std::string& dirName)
{
	if( CreateDirectory(dirName.c_str(), NULL) || ERROR_ALREADY_EXISTS == GetLastError() )
	{
		return true;
	}
	
	printf("Unable to create directory: %s\n", dirName.c_str());

	return false;
}

bool AreFilesTheSame(const FileData& file1Data, const FileNameContainer& file2Name)
{
	FILE* pSecondFile = nullptr;
	const errno_t errorValue = fopen_s(&pSecondFile, file2Name.mFullPath.c_str(), "rb");
	if( errorValue )
	{
		return false;
	}

	//Figure out the file size by
	fseek(pSecondFile, 0, SEEK_END);
	const unsigned long secondFileSize = ftell(pSecondFile);
	fseek(pSecondFile, 0, SEEK_SET);

	//Close the file
	fclose(pSecondFile);

	//If the file sizes are the same, then compare the data
	if( secondFileSize == file1Data.GetDataSize() )
	{
		FileData secondFileData;
		if( secondFileData.InitializeFileData(file2Name) && file1Data.DoesThisFileContain(secondFileData, nullptr, false) )
		{
			return true;
		}
	}

	return false;
}

void GetAllFilesOfType(const vector<FileNameContainer>& allFiles, const char* pInFileType, vector<FileNameContainer>& outFiles)
{
	const string validTextFile(pInFileType);

	for (const FileNameContainer& fileName : allFiles)
	{
		std::size_t found = fileName.mFileName.find(validTextFile);
		if (found != std::string::npos)
		{
			outFiles.push_back(fileName);
		}
	}
}

////////////////////////////
//        FileData        //
////////////////////////////
FileData::~FileData()
{
	Close();
}

void FileData::Close()
{
	//release memory
	delete[] mpData;
	mpData = nullptr;
}

bool FileData::ReadInFileData(const char* pFileName, bool bInErrorOnFail)
{
	//Open file in read-binary mode
	FILE* pFile = nullptr;
	errno_t errorValue = fopen_s(&pFile, pFileName, "rb");
	if( errorValue )
	{
		if(bInErrorOnFail)
		{
			printf("Unable to open file: %s.  Error code: %i \n", pFileName, errorValue);
		}
		
		return false;
	}

	//Figure out the file size by seeking to the end of the file and requesting the position of the file
	fseek(pFile, 0, SEEK_END);
	mFileSize = ftell(pFile);
	fseek(pFile, 0, SEEK_SET); //reset to the start of the file

	if( !mFileSize )
	{

		printf("No data found within file: %s \n", pFileName);
		return false;
	}

	//Allocate bufferA
	if( mBufferSize < mFileSize )
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

bool FileData::IsDataTheSame(const char* pData1, const char* pData2, const unsigned long memSize)
{
	for(unsigned long currIndex = 0; currIndex < memSize; ++currIndex)
	{
		const uint8 data1 = (uint8)pData1[currIndex];
		const uint8 data2 = (uint8)pData2[currIndex];
		if( data1 != data2 &&
		    !( (data1 == 0xff && data2 == 0) || 
		       (data1 == 0 && data2 == 0xff) 
		     )
		   )
		{
			return false;
		}
	}

	return true;
}

bool FileData::InitializeFileData(const FileNameContainer& inFileData, bool bInErrorOnFail)
{
	mFileName = inFileData.mFileName;
	mFullPath = inFileData.mFullPath;

	return ReadInFileData(mFullPath.c_str(), bInErrorOnFail);
}

bool FileData::InitializeFileData(const char* pFileName, const char* pFullPath, bool bInErrorOnFail)
{
	mFileName = pFileName;
	mFullPath = pFullPath;

	return ReadInFileData(mFullPath.c_str(), bInErrorOnFail);
}

bool FileData::InitializeFileData(const string& inFullPath, bool bInErrorOnFail)
{
	const FileNameContainer nameInfo(inFullPath);

	return InitializeFileData(nameInfo, bInErrorOnFail);
}

void FileData::WriteToFile(const char* pFileName) const
{
	FileWriter outFile;
	if( outFile.OpenFileForWrite(pFileName) )
	{
		outFile.WriteData(GetData(), GetDataSize());
	}
}

bool FileData::DoesThisFileContain(const FileData& otherFile, vector<unsigned long>* pOutOffsets, bool bFindMultiple) const
{	
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
			if( pOutOffsets )
			{
				pOutOffsets->push_back(currentOffset);
			}

			if( !bFindMultiple )
			{
				return true;
			}
		}

		++currentOffset;
	}

	return pOutOffsets ? pOutOffsets->size() > 0 : false;
}

bool FindDataWithinBuffer(const char* pBuffer, unsigned long bufferSize, const char* pSearchData, const unsigned int searchDataSize, unsigned long &outIndex)
{
	if( searchDataSize > bufferSize )
	{
		return false;
	}

	unsigned long       currentOffset = 0;
	const unsigned long finalOffset   = bufferSize - searchDataSize;

	while( currentOffset <= finalOffset )
	{
		if( FileData::IsDataTheSame(pBuffer + currentOffset, pSearchData, searchDataSize) )
		{
			outIndex = currentOffset;
			return true;
		}

		++currentOffset;
	}

	return false;
}

bool FileData::FindDataIndex(const char* pInData, unsigned long inDataLength, unsigned long& outIndex) const
{
	return FindDataWithinBuffer(mpData, mFileSize, pInData, inDataLength, outIndex);
}

unsigned long FileData::GetCRC()
{
	if( !mbCrcCalculated )
	{
		for(unsigned long i = 0; i < mBufferSize; ++i)
		{
			CalcCrc32(mpData[i], mCrc);
		}

		mbCrcCalculated = true;
	}

	return mCrc;
}

bool FileData::ReadData(unsigned long inDataOffset, char* pOutData, unsigned long inDataSize, bool bSwapEndianness)
{
	if( memcpy_s(pOutData, inDataSize, mpData + inDataOffset, inDataSize) )
	{
		return false;
	}

	if( bSwapEndianness )
	{
		SwapByteOrderInPlace((char*)pOutData, inDataSize);
	}

	return true;
}

//////////////////////////////
//        TextFileData      //
//////////////////////////////
bool TextFileData::InitializeTextFile(bool bFixupSpecialCharacters, bool bCollapseDots)
{
	FILE* pFile              = nullptr;
	const errno_t errorValue = fopen_s(&pFile, mFileNameInfo.mFullPath.c_str(), "r");
	if( errorValue )
	{
		printf("Unable to open file: %s.  Error code: %i \n", mFileNameInfo.mFileName.c_str(), errorValue);
		return false;
	}
	
	static const int bufferSize = 1024*1024;
	static char buffer[bufferSize];
	static char fixedString[bufferSize];
	memset(buffer, 0, bufferSize);
	memset(fixedString, 0, bufferSize);

	const string space(" ");
	const string newLine("<br>");
	char* pToken   = nullptr;

	int lineCount = 1;
	while( fgets(buffer, bufferSize, pFile) != nullptr )
	{	
		TextLine newLineOfText;
	//	char *pNextToken   = nullptr;;
		char* pContext     = nullptr;
		const char* pDelim = " \n";
		const unsigned char specialToken = 0x80;
		pToken = strtok_s(buffer, pDelim, &pContext);
		while(pToken != NULL)
		{
			//Hack to fix up unicode value of '…'.  '…' is replaced with '@'
			memset(fixedString, 0, bufferSize);
			const size_t tokenLen = strlen(pToken);
			for(size_t t = 0, f = 0; t < tokenLen; t)
			{	
				unsigned char uToken = (unsigned char)pToken[t];

				//Skip past UTF8 byte order marker
				if( uToken == (unsigned char)0xef || uToken == (unsigned char)0xbb || uToken == (unsigned char)0xbf )
				{
					t += 1;
				}
				
				//è
				else if( bFixupSpecialCharacters &&
						uToken == 0xc3 &&
						t + 1 < tokenLen && 
						(unsigned char)(pToken[t+1]) >= 0x80 &&
						(unsigned char)(pToken[t+1]) <= 0xbf )
				{
					const unsigned char uTokenNext = (unsigned char)pToken[t+1];
					const unsigned char fontSheetIndex = 192 + (uTokenNext - 0x80);

					t += 2;
					fixedString[f++] = fontSheetIndex;
				}

				else if( bFixupSpecialCharacters && 
						(uToken == (unsigned char)0xe2 && (unsigned char)pToken[t+1] == (unsigned char)0x80 && (unsigned char)pToken[t+2] == (unsigned char)0xa6 ) ||
						(uToken == (unsigned char)'.' && (unsigned char)pToken[t + 1] == (unsigned char)'.' && (unsigned char)pToken[t + 2] == (unsigned char)'.')
						)
				{
					t += 3;

					if( bCollapseDots )
					{
						fixedString[f++] = (unsigned char)133;//'@';
					}
					else
					{
						fixedString[f++] = '.';
						fixedString[f++] = '.';
						fixedString[f++] = '.';
					}
				}
				else if( bFixupSpecialCharacters && uToken == (unsigned char)0xe2 && (unsigned char)pToken[t+1] == (unsigned char)0x80 && (unsigned char)pToken[t+2] == (unsigned char)0x99 )
				{
					t += 3;
					fixedString[f++] = '\'';
				}
				else if( bFixupSpecialCharacters && uToken == (unsigned char)0xe2 && (unsigned char)pToken[t+1] == (unsigned char)0x80 && (unsigned char)pToken[t+2] == (unsigned char)0x9c )
				{
					t += 3;
					fixedString[f++] = '\"';
				}
				else if( bFixupSpecialCharacters && uToken == (unsigned char)0xe2 && (unsigned char)pToken[t+1] == (unsigned char)0x80 && (unsigned char)pToken[t+2] == (unsigned char)0x9d )
				{
					t += 3;
					fixedString[f++] = '\"';
				}
				else if( bFixupSpecialCharacters && uToken == (unsigned char)0xe2 && (unsigned char)pToken[t+1] == (unsigned char)0x80 && (unsigned char)pToken[t+2] == (unsigned char)0x98 )
				{
					t += 3;
					fixedString[f++] = '\'';
				}
				else if( bFixupSpecialCharacters && uToken == (unsigned char)0xe2 )
				{
					printf("Unhandled multi-byte character in string %s in %s\n", pToken, mFileNameInfo.mFileName.c_str());

					t += 3;
				}
				else
				{
					fixedString[f++] = pToken[t++];
				}
			}

			//Split out newLine characters
			bool bConditionallyAddSpaceAtEnd = false;
			string testString(fixedString);
			size_t pos = testString == newLine ? std::string::npos : testString.find(newLine);
			if( pos != std::string::npos )
			{
				std::string token;
				while( pos != std::string::npos ) 
				{
					token = testString.substr(0, pos);
					if( pos > 0 )
					{
						newLineOfText.mWords.push_back(token);
						newLineOfText.mFullLine += token + space;

						newLineOfText.mWords.push_back(newLine);
						newLineOfText.mFullLine += newLine + space;

						testString.erase(0, pos + newLine.length());
					}
					else
					{
						newLineOfText.mWords.push_back(newLine);
						newLineOfText.mFullLine += newLine + space;

						testString.erase(0, pos + newLine.length());
					}

					pos = testString.find(newLine);
				}

				if( testString.size() )
				{
					newLineOfText.mWords.push_back(testString);
					newLineOfText.mFullLine += testString + space;
				}
			}
			else
			{
				newLineOfText.mWords.push_back(fixedString);
				newLineOfText.mFullLine += string(fixedString);
				bConditionallyAddSpaceAtEnd = true;
			}

			pToken = strtok_s(NULL, pDelim, &pContext);
			if(pToken && bConditionallyAddSpaceAtEnd)
			{
				newLineOfText.mFullLine += space;
			}
		}

		for(const std::string& word : newLineOfText.mWords)
		{
			if( word == newLine )
			{
				++newLineOfText.mNumberOfLines;
			}
		}

		mLines.push_back(std::move(newLineOfText));
		++lineCount;
	}

	fclose(pFile);

	return true;
}

//////////////////////////////
//        FileWriter        //
//////////////////////////////
FileWriter::~FileWriter()
{
	Close();
}

bool FileWriter::OpenFileForWrite(const string& inFileName)
{
	mFileName = inFileName;

	errno_t errorValue = fopen_s(&mpFileHandle, mFileName.c_str(), "wb");
	if( errorValue )
	{
		printf("Unable to create file: %s.  Error code: %i \n", mFileName.c_str(), errorValue);
		return false;
	}

	return true;
}

bool FileWriter::WriteData(const void* pInData, unsigned long inDataSize, bool bInSwapEndianness)
{
	if( !mpFileHandle )
	{
		return false;
	}

	if (bInSwapEndianness)
	{
		char* pSwappedData = new char[inDataSize];
		memcpy_s(pSwappedData, inDataSize, pInData, inDataSize);
		SwapByteOrderInPlace(pSwappedData, inDataSize);

		const unsigned long numElemsWritten = (unsigned long)fwrite(pSwappedData, sizeof(char), inDataSize, mpFileHandle);
		mDataSize += numElemsWritten * sizeof(char);

		delete[] pSwappedData;

		return numElemsWritten == inDataSize;
	}
	
	const unsigned long numElemsWritten = (unsigned long)fwrite(pInData, sizeof(char), inDataSize, mpFileHandle);
	mDataSize                          += numElemsWritten*sizeof(char);

	return numElemsWritten == inDataSize;

}

bool FileWriter::WriteDataAtOffset(const void* pInData, unsigned long inSize, unsigned long inOffset, bool bSwapEndianness)
{
	fseek(mpFileHandle, inOffset, SEEK_SET);

	bool bSuccess = false;
	if( bSwapEndianness )
	{
		char* pSwappedData = new char[inSize];
		memcpy_s(pSwappedData, inSize, pInData, inSize);
		SwapByteOrderInPlace(pSwappedData, inSize);

		bSuccess = WriteData(pSwappedData, inSize);

		delete[] pSwappedData;
	}
	else
	{
		bSuccess = WriteData(pInData, inSize);
	}

	fseek(mpFileHandle, 0, SEEK_END);

	return bSuccess;
}

void FileWriter::SeekToStart()
{
	fseek(mpFileHandle, 0, SEEK_SET);
}

void FileWriter::Close()
{
	if( mpFileHandle )
	{
		fclose(mpFileHandle);
	}

	mpFileHandle = nullptr;
	mDataSize    = 0;
}

//////////////////////////////////
//        TextFileWriter        //
//////////////////////////////////
TextFileWriter::~TextFileWriter()
{
	Close();
}

bool TextFileWriter::OpenFileForWrite(const string& inFileName)
{
	mFileName = inFileName;

	errno_t errorValue = fopen_s(&mpFileHandle, mFileName.c_str(), "w");
	if( errorValue )
	{
		printf("Unable to create file: %s.  Error code: %i \n", mFileName.c_str(), errorValue);
		return false;
	}

	return true;
}

void TextFileWriter::Printf(const char* fmt, ...) 
{
	assert(mpFileHandle);
	if (!mpFileHandle)
	{
		return;
	}

	va_list args;
	va_start(args, fmt);
	vfprintf(mpFileHandle, fmt, args);
	va_end(args);
}

void TextFileWriter::WriteString(const string& inString)
{
	assert(mpFileHandle);
	if( !mpFileHandle )
	{
		return;
	}

	if( fwrite(inString.c_str(), 1, inString.size(), mpFileHandle) != inString.size() )
	{
		printf("Unable to write string: %s in file: %s", inString.c_str(), mFileName.c_str());
	}
}

void TextFileWriter::WriteStringWithNewLine(const string& inString)
{
	WriteString(inString);
	AddNewLine();
}

void TextFileWriter::AddNewLine()
{
	assert(mpFileHandle);
	if( !mpFileHandle )
	{
		return;
	}

	fprintf(mpFileHandle, "\n");
}

void TextFileWriter::Close()
{
	if( mpFileHandle )
	{
		fclose(mpFileHandle);
	}

	mpFileHandle = nullptr;
}

//////////////////////////////////
//        FileReadWriter        //
//////////////////////////////////
FileReadWriter::~FileReadWriter()
{
	Close();
}

bool FileReadWriter::OpenFile(const std::string& inFileName)
{
	errno_t errorValue = fopen_s(&mpFileHandle, inFileName.c_str(), "r+b");
	if( errorValue )
	{
		printf("Unable to open file %s.  Error: %i\n", inFileName.c_str(), errorValue);
		return false;
	}

	return true;
}

void FileReadWriter::Close()
{
	if( mpFileHandle )
	{
		fclose(mpFileHandle);
	}

	mpFileHandle = nullptr;
}

bool FileReadWriter::WriteData(const char* pInData, unsigned long inDataSize)
{
	if (!mpFileHandle)
	{
		return false;
	}

	auto position = ftell(mpFileHandle);
	if(position > 0)
	{
		int k = 0;
		++k;
	}

	const unsigned long numElemsWritten = (unsigned long)fwrite(pInData, sizeof(char), inDataSize, mpFileHandle);

	return numElemsWritten == inDataSize;
}

bool FileReadWriter::WriteData(unsigned long inFileOffset, const char* pInData, unsigned long inDataSize)
{
	if( !mpFileHandle )
	{
		return false;
	}

	fseek(mpFileHandle, inFileOffset, SEEK_SET);
	
	const unsigned long numElemsWritten = (unsigned long)fwrite(pInData, sizeof(char), inDataSize, mpFileHandle);

	fseek(mpFileHandle, 0, SEEK_SET);

	return numElemsWritten == inDataSize;
}

bool FileReadWriter::WriteData(unsigned long inFileOffset, const char* pInData, unsigned long inDataSize, bool bSwapEndianness)
{
	if( !mpFileHandle )
	{
		return false;
	}

	const char* pWriteData = pInData;
	bool bFreeWriteData = false;
	if( bSwapEndianness )
	{
		char* pSwapBuffer = new char[inDataSize];
		bFreeWriteData = true;

		memcpy_s((void*)pSwapBuffer, inDataSize, pInData, inDataSize);
		SwapByteOrderInPlace(pSwapBuffer, inDataSize);

		pWriteData = pSwapBuffer;
	}

	fseek(mpFileHandle, inFileOffset, SEEK_SET);
	
	const unsigned long numElemsWritten = (unsigned long)fwrite(pWriteData, sizeof(char), inDataSize, mpFileHandle);

	fseek(mpFileHandle, 0, SEEK_SET);

	if( bFreeWriteData )
	{
		delete[] pWriteData;
		pWriteData = nullptr;
	}

	return numElemsWritten == inDataSize;
}

bool FileReadWriter::ReadData(unsigned long inFileOffset, char* pOutData, unsigned long inDataSize, bool bSwapEndianness)
{
	if( !mpFileHandle )
	{
		return false;
	}

	fseek(mpFileHandle, inFileOffset, SEEK_SET);

	if( !fread_s(pOutData, inDataSize, inDataSize, 1, mpFileHandle) )
	{
		return false;
	}

	fseek(mpFileHandle, 0, SEEK_SET);

	if( bSwapEndianness )
	{
		SwapByteOrderInPlace((char*)pOutData, inDataSize);
	}

	return true;
}

///////////////////////////////
//        PaletteData        //
///////////////////////////////
PaletteData::~PaletteData()
{
	delete[] mpPaletteData;
	mpPaletteData = nullptr;
}

bool PaletteData::CreateFrom15BitData(const char* pInPaletteData, int inPaletteSize)
{
	if( inPaletteSize != 32 && inPaletteSize != 128 && inPaletteSize != 256 && inPaletteSize != 512 )
	{
		printf("Unsupported palette type");
		return false;
	}

	//Create 32bit palette from the 16 bit(5:5:5 bgr) palette in SakuraTaisen
	mNumColors                = inPaletteSize/2;
	mNumBytesInPalette        = mNumColors*4;
	mpPaletteData             = new char[mNumBytesInPalette];
	const float full5BitValue = (float)0x1f;
	for(int i = 0, origIdx = 0; i < mNumBytesInPalette; i += 4, origIdx += 2)
	{
		unsigned short color = ((pInPaletteData[origIdx] << 8) + (unsigned char)pInPaletteData[origIdx+1]);
		
		//Ugly conversion of 5bit values to 8bit.  Probably a better way to do this.
		//Masking the r,g,b components and then bringing the result into a [0,255] range.
		mpPaletteData[i+2] = (char)floorf( ( ((color & 0x001f)/full5BitValue) * 255.f) + 0.5f);
		mpPaletteData[i+1] = (char)floorf( ( ( ((color & 0x03E0) >> 5)/full5BitValue) * 255.f) + 0.5f);
		mpPaletteData[i+0] = (char)floorf( ( ( ((color & 0x7C00) >> 10)/full5BitValue) * 255.f) + 0.5f);
		mpPaletteData[i+3] = 0;
	}

	return true;
}

bool PaletteData::CreateFrom24BitData(const char* pInPaletteData, int inPaletteSize)
{
	if( inPaletteSize != 1024 )
	{
		printf("Unsupported palette type");
		return false;
	}

	//Create 32bit palette from the 24 bit(bgr) palette in SakuraTaisen
	mNumColors         = inPaletteSize/4;
	mNumBytesInPalette = mNumColors*4;
	mpPaletteData      = new char[mNumBytesInPalette];
	for(int i = 0, origIdx = 0; i < mNumBytesInPalette; i += 4, origIdx += 4)
	{
		unsigned char blue  = pInPaletteData[origIdx + 1];
		unsigned char green = pInPaletteData[origIdx + 2];
		unsigned char red   = pInPaletteData[origIdx + 3];

		//Ugly conversion of 5bit values to 8bit.  Probably a better way to do this.
		//Masking the r,g,b components and then bringing the result into a [0,255] range.
		mpPaletteData[i+0] = red;
		mpPaletteData[i+1] = green;
		mpPaletteData[i+2] = blue;
		mpPaletteData[i+3] = 0;
	}

	return true;
}

bool PaletteData::CreateFrom32BitData(const char* pInPaletteData, int inPaletteSize, bool bDropFirstBit)
{
	if( !(inPaletteSize == 64 || inPaletteSize == 128 || inPaletteSize == 256 || inPaletteSize == 1024) )
	{
		printf("PaletteData::CreateFrom32BitData: Unsupported palette type");
		return false;
	}

	mNumColors                 = inPaletteSize/4;
	const int numBytesPerColor = 2;

	mNumBytesInPalette = mNumColors*numBytesPerColor;
	mpPaletteData      = new char[mNumBytesInPalette];

	const unsigned short bitMask = bDropFirstBit ? 0x7fff : 0xffff;
	for(int i = 0, outIndex = 0; i < inPaletteSize; i += 4, outIndex += 2)
	{
		assert(outIndex + 1 < mNumBytesInPalette);

		const unsigned char r = (unsigned char)floorf( ((unsigned char)(pInPaletteData[i+0])/255.f)*31.f + 0.5f);
		const unsigned char g = (unsigned char)floorf( ((unsigned char)(pInPaletteData[i+1])/255.f)*31.f + 0.5f);
		const unsigned char b = (unsigned char)floorf( ((unsigned char)(pInPaletteData[i+2])/255.f)*31.f + 0.5f);

		//Swap byte order so data is written in big endian order.  Drop off the first bit if needed.
		unsigned short outColor = ((r << 10) + (g << 5) + b) & bitMask;
		std::reverse((char*)&outColor, ((char*)&outColor + 2));

		//copy data over to the palette
		memcpy(&mpPaletteData[outIndex], &outColor, 2);
	}

	return true;
}

bool PaletteData::CreateFromMicrosoftPalette(const char* pInPaletteData, int inPaletteSize)
{
	if (!(inPaletteSize == 1024))
	{
		printf("PaletteData::CreateFrom32BitData: Unsupported palette type");
		return false;
	}

	mNumColors = inPaletteSize / 4;
	const int numBytesPerColor = 4;

	mNumBytesInPalette = mNumColors * numBytesPerColor;
	mpPaletteData = new char[mNumBytesInPalette];

	for (int i = 0, outIndex = 0; i < inPaletteSize; i += 4, outIndex += 4)
	{
		assert(outIndex + 1 < mNumBytesInPalette);

		const unsigned char r = (unsigned char)(pInPaletteData[i + 0]);
		const unsigned char g = (unsigned char)(pInPaletteData[i + 1]);
		const unsigned char b = (unsigned char)(pInPaletteData[i + 2]);
		const unsigned char a = (unsigned char)(pInPaletteData[i + 4]);

		mpPaletteData[outIndex + 0] = b;
		mpPaletteData[outIndex + 1] = g;
		mpPaletteData[outIndex + 2] = r;
		mpPaletteData[outIndex + 3] = a;
	}

	return true;
}

void PaletteData::SetValue(int index, unsigned short value)
{
	assert(index*2 + 1 < mNumBytesInPalette);

	memcpy(mpPaletteData + index*2, &value, sizeof(value));
}

////////////////////////////////////////
//        MicrosoftPaletteData        //
////////////////////////////////////////
bool MicrosoftPaletteData::CreatePaletteData(const FileData& inData, PaletteData& outData)
{
	if( inData.GetDataSize() <= sizeof(MicrosoftPaletteData::Header) )
	{
		printf("MicrosoftPaletteData size is too small\n");
		return false;
	}

	Header header;
	memcpy_s(&header, sizeof(header), inData.GetData(), sizeof(header));

	const int numColors = header.paletteSize;
	const int dataOffset = sizeof(header);

	outData.CreateFromMicrosoftPalette(inData.GetData() + dataOffset, numColors*4);

	return true;
}

////////////////////////////////
//        BitmapWriter        //
////////////////////////////////
bool BitmapWriter::CreateBitmap(const string& inFileName, int inWidth, int inHeight, int bitsPerPixel, const char* pInColorData, int inColorSize, const char* pInPaletteData, int inPaletteSize, bool bForceBitmapFormat)
{	
	if( !bForceBitmapFormat )//bitsPerPixel == 4 )
	{
		SaveAsPNG(inFileName, inWidth, inHeight, bitsPerPixel, pInColorData, inColorSize, pInPaletteData, inPaletteSize);
	}
	else
	{
		FileWriter outFile;
		if( !outFile.OpenFileForWrite(inFileName) )
		{
			return false;
		}

		BitmapData::FileHeader fileHeader;
		const int fileSize          = sizeof(BitmapData::FileHeader) + sizeof(BitmapData::InfoHeader) + inColorSize + inPaletteSize;
		const int offsetToColorData = sizeof(BitmapData::FileHeader) + sizeof(BitmapData::InfoHeader) + inPaletteSize;
		fileHeader.Initialize(fileSize, offsetToColorData);

		BitmapData::InfoHeader infoHeader;
		infoHeader.Initialize(inWidth, inHeight, (short)bitsPerPixel);

		outFile.WriteData(&fileHeader, sizeof(fileHeader));
		outFile.WriteData(&infoHeader, sizeof(infoHeader));

		if( pInPaletteData )
		{
			outFile.WriteData(pInPaletteData, inPaletteSize);
		}

		outFile.WriteData(pInColorData, inColorSize);

		if( bitsPerPixel == 8  )
		{
			const int numExpectedBytes = inWidth*abs(inHeight);
			if( inColorSize < numExpectedBytes )
			{
				const int blankBytes = numExpectedBytes - inColorSize;
				char* pRemainingPixels = new char[blankBytes];
				memset(pRemainingPixels, 0, blankBytes);

				outFile.WriteData(pRemainingPixels, blankBytes);

				delete[] pRemainingPixels;
			}
		}

		outFile.Close();
	}
	

	return true;
}

bool BitmapWriter::SaveAsPNG(const string& inFileName, int inWidth, int inHeight, int bitsPerPixel, const char* pInColorData, int /*inColorSize*/, const char* pInPaletteData, int inPaletteSize)
{
	//create encoder and set settings and info (optional)
	lodepng::State state;
	
	if( bitsPerPixel != 4 && bitsPerPixel != 8 )
	{
		printf("Unable to save PNG for %s.  Only 4bit and 8bit paletted images supported.\n", inFileName.c_str());
		return false;
	}

	//generate palette
	for(int i = 0; i < inPaletteSize; i += 4)
	{
		const unsigned char r = pInPaletteData[i+2];
		const unsigned char g = pInPaletteData[i+1];
		const unsigned char b = pInPaletteData[i+0];

		lodepng_palette_add(&state.info_png.color, r, g, b, 255);
		lodepng_palette_add(&state.info_raw, r, g, b, 255);
	}
	
	//both the raw image and the encoded image must get colorType 3 (palette)
	state.info_png.color.colortype = LCT_PALETTE; //if you comment this line, and create the above palette in info_raw instead, then you get the same image in a RGBA PNG.
	state.info_png.color.bitdepth  = bitsPerPixel;
	state.info_raw.colortype       = LCT_PALETTE;
	state.info_raw.bitdepth        = bitsPerPixel;
	state.encoder.auto_convert     = 0; //bitsPerPixel specify ourselves exactly what output PNG color mode we want
	
	//encode and save
	std::vector<unsigned char> buffer;
	unsigned error = lodepng::encode(buffer, (const unsigned char*)pInColorData, inWidth, abs(inHeight), state);
	if(error)
	{
		return false;
	}

	const size_t lastIndex = inFileName.find_last_of(".");
	string pngFileName     = inFileName.substr(0, lastIndex) + string(".png");

	unsigned result = lodepng::save_file(buffer, pngFileName.c_str());

	return !result;
}


/////////////////////////////////
//        BitmapReader        //
/////////////////////////////////
bool BitmapReader::ReadBitmap(const string& inBitmapName, bool bInErrorOnFail)
{
	FileData inFile;
	if( !inFile.InitializeFileData(inBitmapName.c_str(), inBitmapName.c_str(), bInErrorOnFail) )
	{
		return false;
	}

	mFilename = inBitmapName;

	const char* pFileData = inFile.GetData();
	memcpy(&mBitmapData.mFileHeader, pFileData, sizeof(mBitmapData.mFileHeader));

	if( mBitmapData.mFileHeader.mType != 0x4D42 )
	{
		printf("%s is not a bitmap file", inBitmapName.c_str());
		return false;
	}

	memcpy(&mBitmapData.mInfoHeader, pFileData + sizeof(mBitmapData.mFileHeader), sizeof(mBitmapData.mInfoHeader));

	if( !(mBitmapData.mInfoHeader.mBitCount == 4 || mBitmapData.mInfoHeader.mBitCount == 8) )
	{
		printf("%s has unsupported bit count [%i]", inBitmapName.c_str(), mBitmapData.mInfoHeader.mBitCount);
		return false;	
	}

	if(mBitmapData.mInfoHeader.mBitCount == 32)
	{
		const int numColorBytes = abs(mBitmapData.mInfoHeader.mImageHeight) * mBitmapData.mInfoHeader.mImageWidth * 4;
		if (mBitmapData.mFileHeader.mOffsetToData + numColorBytes != inFile.GetDataSize())
		{
			printf("%s doesn't have the correct amount of data.\n", inBitmapName.c_str());
			return false;
		}

		mBitmapData.mColorData.mSizeInBytes = numColorBytes;
		mBitmapData.mColorData.mpRGBA = new char[numColorBytes];
		memcpy(mBitmapData.mColorData.mpRGBA, pFileData + mBitmapData.mFileHeader.mOffsetToData, numColorBytes);	
	}
	else
	{
		//Read in palette
		const int numColorsInPalette          = (int)pow(2, mBitmapData.mInfoHeader.mBitCount);
		const int numBytesPerColor            = 4;
		mBitmapData.mPaletteData.mSizeInBytes = numColorsInPalette * numBytesPerColor;
		mBitmapData.mPaletteData.mpRGBA       = new char[mBitmapData.mPaletteData.mSizeInBytes];
		int offsetToPalette                   = sizeof(mBitmapData.mFileHeader) + mBitmapData.mInfoHeader.mInfoHeaderSize;//mBitmapData.mFileHeader.mOffsetToData - mBitmapData.mInfoHeader.mNumImportantColors*numBytesPerColor;//mBitmapData.mPaletteData.mSizeInBytes;
		if (offsetToPalette < 0)
		{
			printf("%s has an invalid offset to palette [%i]", inBitmapName.c_str(), offsetToPalette);
			offsetToPalette = sizeof(mBitmapData.mFileHeader) + sizeof(mBitmapData.mInfoHeader);
		}
		memcpy(mBitmapData.mPaletteData.mpRGBA, pFileData + offsetToPalette, mBitmapData.mPaletteData.mSizeInBytes);

		//Read in color data
		const int divisor                   = mBitmapData.mInfoHeader.mBitCount == 4 ? 2 : 1;
		const int numColorBytes             = abs(mBitmapData.mInfoHeader.mImageHeight) * mBitmapData.mInfoHeader.mImageWidth / divisor;
		mBitmapData.mColorData.mSizeInBytes = numColorBytes;
		mBitmapData.mColorData.mpRGBA       = new char[numColorBytes];
		memcpy(mBitmapData.mColorData.mpRGBA, pFileData + mBitmapData.mFileHeader.mOffsetToData, numColorBytes);
	}


	return true;
}

bool BitmapReader::Save()
{
	FileWriter outFile;
	if( !outFile.OpenFileForWrite(mFilename) )
	{
		return false;
	}

	outFile.WriteData(&mBitmapData.mFileHeader, sizeof(mBitmapData.mFileHeader));
	outFile.WriteData(&mBitmapData.mInfoHeader, sizeof(mBitmapData.mInfoHeader));

	if( mBitmapData.mPaletteData.mSizeInBytes )
	{
		outFile.WriteData(mBitmapData.mPaletteData.mpRGBA, mBitmapData.mPaletteData.mSizeInBytes);
	}

	outFile.WriteData(mBitmapData.mColorData.mpRGBA, mBitmapData.mColorData.mSizeInBytes);

	return true;
}

/////////////////////////////////
//        BitmapSurface        //
/////////////////////////////////
BitmapSurface::~BitmapSurface()
{
	delete[] mpBuffer;
	mpBuffer    = nullptr;
	mBufferSize = 0;

	if( mbOwnsPalette )
	{
		delete[] mpPalette;
		mpPalette = nullptr;
	}
}

bool BitmapSurface::CreateSurface(int inWidth, int inHeight, EBitsPerPixel bitsPerPixel, const char* pPalette,
								  int paletteSize, bool bDuplicatePalette)
{
	const int width = abs(inWidth);
	const int height = abs(inHeight);
	const int numPixels = width*height;
	const int numBytes  = bitsPerPixel == kBPP_4 ? numPixels*4 : numPixels;
	mBytesPerRow        = bitsPerPixel == kBPP_4 ? width/2 : width;

	mWidth        = width;
	mHeight       = height;
	mBitsPerPixel = bitsPerPixel;

	mpBuffer    = new char[numBytes];
	mBufferSize = numBytes;

	if( bDuplicatePalette )
	{
		mbOwnsPalette = true;

		mpPalette = new char[paletteSize];
		memcpy_s(const_cast<char*>(mpPalette), paletteSize, pPalette, paletteSize);
	}
	else
	{
		mpPalette = pPalette;
	}
	mPaletteSize = paletteSize;

	memset(mpBuffer, 0, numBytes);

	return true;
}

void BitmapSurface::AddTile(const char* pInData, int inDataSize, int inX, int inY, int width, int height, EFlipFlag flipFlag)
{
	++mNumTiles;

	const int startX = mBitsPerPixel == kBPP_4 ? inX/2 : inX;
	const int tileStride = mBitsPerPixel == kBPP_4 ? width/2 : width;
	const int offset = (inY*mBytesPerRow + startX);
	char* pOutData   = mpBuffer + offset;
	
	assert(offset < mBufferSize);

	int inDataOffset = 0;
	int numBytesWritten = 0;
	if( flipFlag == EFlipFlag::kFlipNone )
	{
		for (int y = 0; y < height && y < mHeight; ++y)
		{
			for (int x = 0; x < tileStride; ++x) //used to be 8 insntead of maxX
			{
				assert(offset + y * mBytesPerRow + x < mBufferSize);

				assert(inDataOffset < inDataSize);

				pOutData[y * mBytesPerRow + x] = pInData[inDataOffset++];			
				++numBytesWritten;
			}
		}
	}
	else if(flipFlag == EFlipFlag::kFlipVert)
	{
		for (int y = height - 1; y >= 0; --y)
		{
			for (int x = 0; x < tileStride; ++x) //used to be 8 insntead of maxX
			{
				assert(offset + y * mBytesPerRow + x < mBufferSize);

				assert(inDataOffset < inDataSize);
				pOutData[y * mBytesPerRow + x] = pInData[inDataOffset++];
			}
		}
	}
	else if (flipFlag == EFlipFlag::kFlipHoriz)
	{
		for (int y = 0; y < height; ++y)
		{
			for (int x = tileStride - 1; x >= 0; --x) //used to be 8 insntead of width
			{
				assert(offset + y * mBytesPerRow + x < mBufferSize);

				assert(inDataOffset < inDataSize);
				pOutData[y * mBytesPerRow + x] = pInData[inDataOffset++];
			}
		}
	}
	else
	{
		for (int y = height - 1; y >= 0; --y)
		{
			for (int x = tileStride - 1; x >= 0; --x) //used to be 8 insntead of width
			{
				assert(offset + y * mBytesPerRow + x < mBufferSize);

				assert(inDataOffset < inDataSize);
				pOutData[y * mBytesPerRow + x] = pInData[inDataOffset++];
			}
		}
	}
}

void BitmapSurface::AddPartialTile(const char* pInData, int inDataSize, int inX, int inY, 
								   int numBytesInWidthToCopy, int inWidth, int inHeight)
{
	++mNumTiles;

	if( mBitsPerPixel != kBPP_8 )
	{
		printf("BitmapSurface::AddPartialTile: Only 8bit surfaces supported\n");
	}
	
	for (int y = 0; y < inHeight && y < mHeight; ++y)
	{
		for (int x = 0; x < numBytesInWidthToCopy; ++x) //used to be 8 insntead of maxX
		{
			const int writeOffset = ((inY + y)*mBytesPerRow + inX + x);
			const int readOffset = (y*inWidth + x);
			assert(writeOffset < mBufferSize);
			assert(readOffset < inDataSize);
;
			mpBuffer[writeOffset] = pInData[readOffset];
		}
	}
}

bool BitmapSurface::WriteToFile(const std::string& fileName, bool bForceBitmap) const
{
	BitmapWriter bitmap;

	return bitmap.CreateBitmap(fileName, mWidth, -mHeight, mBitsPerPixel, mpBuffer, mBufferSize, mpPalette, mPaletteSize, bForceBitmap);
}

/////////////////////////////////////////
//        BitmapFormatConverter        //
/////////////////////////////////////////
BitmapFormatConverter::~BitmapFormatConverter()
{
	delete[] mpConvertedData;
	delete[] mpPaletteData;

	mpConvertedData = nullptr;
	mpPaletteData = nullptr;
}

bool BitmapFormatConverter::ConvertFrom15BitTo32Bit(const char* pInData, int InDataSize)
{
	delete[] mpConvertedData;

	const unsigned short* pColorData = (unsigned short*)(pInData);
	mConvertedDataSize = InDataSize*2;
	mpConvertedData = new char[mConvertedDataSize];
	int destIndex = 0;
	const float full5BitValue = (float)0x1f;

	for( int sourceIndex = 0; sourceIndex < InDataSize/2; sourceIndex++)
	{	
		const unsigned short sourceColor = SwapByteOrder(pColorData[sourceIndex]);
		const char b = (sourceColor >> 10) & (0x1f);
		const char g = (sourceColor >> 5) & (0x1f);
		const char r = sourceColor & (0x1f);
		mpConvertedData[destIndex + 0] = (char)floorf( ( ((b)/full5BitValue) * 255.f) + 0.5f);
		mpConvertedData[destIndex + 1] = (char)floorf( ( ((g)/full5BitValue) * 255.f) + 0.5f);
		mpConvertedData[destIndex + 2] = (char)floorf( ( ((r)/full5BitValue) * 255.f) + 0.5f);
		mpConvertedData[destIndex + 3] = 0;

		destIndex += 4;
	}

	return true;
}

bool BitmapFormatConverter::ConvertFrom32BitTo15Bit(const char* pInBGRAData, int InDataSize)
{
	delete[] mpConvertedData;

	mConvertedDataSize = InDataSize >> 1;
	mpConvertedData = new char[mConvertedDataSize];

	const float maxFullValue = (float)0xff;
	const float max5BitValue = (float )(1 << 5);
	int destIndex = 0;
	for( int i = 0; i < InDataSize; i += 4 )
	{
		const uint32 b = (uint32)(floorf( ((float)pInBGRAData[i + 0]/maxFullValue) * max5BitValue + 0.5f) );
		const uint32 g = (uint32)(floorf( ((float)pInBGRAData[i + 1]/maxFullValue) * max5BitValue + 0.5f) );
		const uint32 r = (uint32)(floorf( ((float)pInBGRAData[i + 2]/maxFullValue) * max5BitValue + 0.5f) );

		const unsigned short convertedValue = (b << 10) + (g << 5) + r;
		mpConvertedData[destIndex + 0] = (char)((convertedValue & (unsigned short)0xff00) >> 8);
		mpConvertedData[destIndex + 1] = (char)(convertedValue & (unsigned short)0x00ff);
		destIndex += 2;
	}

	return true;
}

bool BitmapFormatConverter::ConvertFrom4BitTo8Bit(const BitmapReader& InSource4BitBitmap)
{
	if( InSource4BitBitmap.GetBitCount() != 4 )
	{
		printf("%s needs to be 4bpp image.  Is %ibpp instead.\n", InSource4BitBitmap.GetFileName().c_str(), InSource4BitBitmap.GetBitCount());
		return false;
	}

	if( !ConvertColorDataFrom4BitTo8Bit(InSource4BitBitmap.GetColorData(), InSource4BitBitmap.GetColorDataSize()) )
	{
		return false;
	}

	
	mPaletteDataSize = sizeof(int)*256;
	mpPaletteData = new char[mPaletteDataSize];
	memset(mpPaletteData, 0, mPaletteDataSize);
	memcpy_s(mpPaletteData, mPaletteDataSize, InSource4BitBitmap.GetPaletteData(), InSource4BitBitmap.GetPaletteDataSize());

	mWidth = InSource4BitBitmap.GetWidth();
	mHeight = InSource4BitBitmap.GetHeight();

	mBitCount = 8;

	return true;

}

bool BitmapFormatConverter::ConvertFrom4BitTo8Bit(const char* pIn4BitBmpPath)
{
	BitmapReader bitmap4Bit;
	if( !bitmap4Bit.ReadBitmap(pIn4BitBmpPath) )
	{
		return false;
	}

	return ConvertFrom4BitTo8Bit(bitmap4Bit);
}

bool BitmapFormatConverter::ConvertFrom8BitTo4Bit(const char* pIn8BitBmpPath)
{
	BitmapReader bitmap8Bit;
	if( !bitmap8Bit.ReadBitmap(pIn8BitBmpPath) )
	{
		return false;
	}

	if( !ConvertColorDataFrom8BitTo4Bit(bitmap8Bit.GetColorData(), bitmap8Bit.GetColorDataSize()) )
	{
		return false;
	}

	//Copy over the first 16 colors
	mPaletteDataSize = sizeof(int)*16;
	mpPaletteData = new char[mPaletteDataSize];
	memset(mpPaletteData, 0, mPaletteDataSize);
	memcpy_s(mpPaletteData, mPaletteDataSize, bitmap8Bit.GetPaletteData(), mPaletteDataSize);

	mWidth = bitmap8Bit.GetWidth();
	mHeight = bitmap8Bit.GetHeight();

	mBitCount = 4;

	return true;
}

bool BitmapFormatConverter::ConvertColorDataFrom4BitTo8Bit(const char* pInData, const size_t inDataSize)
{
	if( inDataSize == 0 )
	{
		return false;
	}

	delete[] mpConvertedData;

	mConvertedDataSize = inDataSize*2;
	mpConvertedData = new char[mConvertedDataSize];

	//4bit color data has 2 pixels per byte
	for( size_t sourceIndex = 0, destIndex = 0; sourceIndex < inDataSize; ++sourceIndex, destIndex+=2 )
	{
		mpConvertedData[destIndex+0] = (char)((unsigned char)pInData[sourceIndex] >> 4);
		mpConvertedData[destIndex+1] = (char)((unsigned char)pInData[sourceIndex] & 0x0f);
	}

	return true;
}

bool BitmapFormatConverter::ConvertColorDataFrom8BitTo4Bit(const char* pInData, const size_t inDataSize)
{
	delete[] mpConvertedData;

	if( inDataSize == 0 )
	{
		return false;
	}

	//Take into account a non-even 8bit data set
	mConvertedDataSize = (inDataSize/2) + (inDataSize%2);
	mpConvertedData = new char[mConvertedDataSize];

	for( size_t sourceIndex = 0, destIndex = 0; sourceIndex < inDataSize; sourceIndex += 2, ++destIndex )
	{
		const char value4Bit = (char)(((unsigned char)pInData[sourceIndex] << 4) + (unsigned char)pInData[sourceIndex+1]);
		mpConvertedData[destIndex] = value4Bit;
	}

	return true;
}

void BitmapFormatConverter::WriteToFile(const char* pInOutputPath)
{
	BitmapWriter w;
	w.CreateBitmap(pInOutputPath, mWidth, mHeight, mBitCount, mpConvertedData, (int)mConvertedDataSize, 
				   (char*)mpPaletteData, (int)mPaletteDataSize, true);
}

/////////////////////////////////
//        TileExtractor        //
/////////////////////////////////
void TileExtractor::FixupIndexOfAlphaColor(const unsigned short inIndexOfAlphaColor, bool bInIs4bit)
{
	if( bInIs4bit )
	{
		for(TileExtractor::Tile& tile : mTiles)
		{
			for(unsigned int i = 0; i < tile.mTileSize; i++)
			{
				const unsigned short paletteIndex1 = (tile.mpTile[i] & 0xF0) >> 4;
				const unsigned short paletteIndex2 = (tile.mpTile[i] & 0x0F);

				if(paletteIndex1 == 0)
				{
					tile.mpTile[i] = (char)((inIndexOfAlphaColor << 4) + paletteIndex2);
				}
				else if(paletteIndex1 == inIndexOfAlphaColor)
				{
					tile.mpTile[i] = (char)paletteIndex2;
				}

				if(paletteIndex2 == 0)
				{
					tile.mpTile[i] = (char)((tile.mpTile[i] & 0xF0) + inIndexOfAlphaColor);
				}
				else if(paletteIndex2 == inIndexOfAlphaColor)
				{
					tile.mpTile[i] = tile.mpTile[i] & 0xF0;
				}
			}
		}
	}
	else
	{
		const unsigned char indexOfAlphaColor = (unsigned char)inIndexOfAlphaColor;

		for(TileExtractor::Tile& tile : mTiles)
		{
			for(unsigned int i = 0; i < tile.mTileSize; i++)
			{
				unsigned char paletteIndex = (unsigned char)tile.mpTile[i];

				if(paletteIndex == 0)
				{
					tile.mpTile[i] = indexOfAlphaColor;
				}
				else if(paletteIndex == indexOfAlphaColor)
				{
					tile.mpTile[i] = 0;
				}
			}
		}
	}
}

bool TileExtractor::ExtractTiles(unsigned int inTileWidth, int inTileHeight, unsigned int outTileWidth, 
								 unsigned int outTileHeight, const BitmapReader& inBitmap, int inAlphaIndex)
{
	if( inTileHeight < 0 )
	{
	//	printf("Can't extract tiles.  Image can't be stored with a negative width\n");
	//	return false;
	}

	const unsigned int inAbsTileHeight = abs(inTileHeight);
	if( inTileWidth > outTileWidth || inAbsTileHeight > outTileHeight )
	{
		printf("Can't extract tiles.  Invalid tile sizes\n");
		return false;
	}

	if( !(inBitmap.GetBitCount() == 4 || inBitmap.GetBitCount() == 8) )
	{
		printf("Can't extract tiles.  Only 4 and 8 bit paletted bitmaps supported\n");
		return false;
	}

	if( inBitmap.mBitmapData.mInfoHeader.mImageWidth % inTileWidth != 0 || inBitmap.mBitmapData.mInfoHeader.mImageHeight % inAbsTileHeight != 0 )
	{
		//printf("Can't extract tiles.  Only 4 bit paletted bitmaps supported\n");
		//return false;
	}
	
	const int numRows             = (abs(inBitmap.mBitmapData.mInfoHeader.mImageHeight)/inAbsTileHeight);
	const unsigned int numColumns = (inBitmap.mBitmapData.mInfoHeader.mImageWidth/inTileWidth);
	const unsigned int numTiles   = numRows * numColumns;
	const char* pLinearData       = inBitmap.mBitmapData.mColorData.mpRGBA;
	
	//Allocate tiles
	mTiles.resize(numTiles);

	const bool bIs4Bit                      = inBitmap.GetBitCount() == 4;
	const unsigned int divisor              = bIs4Bit ? 2 : 1;
	const unsigned int numBytesPerTileWidth = inTileWidth/divisor;
	const unsigned int stride               = numBytesPerTileWidth*numColumns;
	mTileByteSize                           = (outTileWidth*outTileHeight)/divisor;//(inTileWidth*inTileHeight)/2; //4bits per pixel
	unsigned int currTile                   = 0;
	const bool bReadInReverse               = inTileHeight >= 0 ? true : false;

	//Bitmaps are stored upside down, so start form the bottom
	for(int y = bReadInReverse ? numRows - 1 : 0; bReadInReverse ? y >= 0 : y < numRows; bReadInReverse ? --y : ++y)
	{
		for(unsigned int x = 0; x < numColumns; ++x)
		{
			mTiles[currTile].mpTile    = new char[mTileByteSize];
			mTiles[currTile].mTileSize = mTileByteSize;
			mTiles[currTile].mX        = x;
			mTiles[currTile].mY        = y;
			mTiles[currTile].mTileWidth = inTileWidth;
			mTiles[currTile].mTileHeight = inAbsTileHeight;

			mDataSize                 += mTileByteSize;
			memset(mTiles[currTile].mpTile, 0, mTileByteSize);

			unsigned int linearDataIndex = y*stride*inAbsTileHeight + x*numBytesPerTileWidth;
			for(unsigned int tilePixelY = 0; tilePixelY < inAbsTileHeight; ++tilePixelY)
			{
				for(unsigned int tilePixelX = 0; tilePixelX < numBytesPerTileWidth; ++tilePixelX)
				{
					const unsigned int linearY = bReadInReverse ? inAbsTileHeight - tilePixelY - 1 : tilePixelY;
					assert( (int)linearY >= 0 );
					assert(tilePixelY*numBytesPerTileWidth + tilePixelX < mTileByteSize);
					assert(linearDataIndex + linearY*stride + tilePixelX < (unsigned int)inBitmap.mBitmapData.mColorData.mSizeInBytes);

					const unsigned char linearColorValue = pLinearData[linearDataIndex + linearY*stride + tilePixelX];
					mTiles[currTile].mpTile[tilePixelY*numBytesPerTileWidth + tilePixelX] = linearColorValue;

					if( linearColorValue != inAlphaIndex )
					{
						if( mTiles[currTile].mBytesInWidthOfContent < tilePixelX + 1)
						{
							mTiles[currTile].mBytesInWidthOfContent = tilePixelX + 1;
						}

						const unsigned int rightPixelX = bIs4Bit ? (tilePixelX)*2 + ((linearColorValue & 0xf0) ? 2 : 1) : tilePixelX;
						if( mTiles[currTile].mWidthOfContent < rightPixelX + 1)
						{
							mTiles[currTile].mWidthOfContent = rightPixelX + 1;
						}
					}
				}
			}

			++currTile;
		}
	}

	return true;
}

void TileExtractor::AddTile(const Tile& InTile)
{
	mDataSize += InTile.mTileSize;
	
	mTiles.push_back(std::move(InTile));
}

void TileExtractor::OutputTiles(FileReadWriter& outFile, int inStartingTile, int inOffset) const
{
	fseek(outFile.GetFileHandle(), inOffset, SEEK_SET);

	//Negative values mean that the first tile should be repeated x amount of times
	while (inStartingTile < 0)
	{
		const Tile& tile = mTiles[0];
		outFile.WriteData(tile.mpTile, tile.mTileSize);

		inStartingTile++;
	}

	const int numTiles = (int)mTiles.size();
	for (int i = inStartingTile; i < numTiles; ++i)
	{
		const Tile& tile = mTiles[i];
		outFile.WriteData(tile.mpTile, tile.mTileSize);
	}
}

uint32 TileExtractor::GetTileCRC(int inTileIndex) const
{
	assert(inTileIndex >= 0 && inTileIndex < (int)mTiles.size());

	return mTiles[inTileIndex].GetFontTileCRC();
}

void TileExtractor::OutputTiles(FileWriter& outFile, int inStartingTile) const
{
	//Negative values mean that the first tile should be repeated x amount of times
	while(inStartingTile < 0)
	{
		const Tile& tile = mTiles[0];
		outFile.WriteData(tile.mpTile, tile.mTileSize);

		inStartingTile++;
	}

	const int numTiles = (int)mTiles.size();
	for(int i = inStartingTile; i < numTiles; ++i)
	{
		const Tile& tile = mTiles[i];
		outFile.WriteData(tile.mpTile, tile.mTileSize);
	}
}

////////////////////////////////////////
//        BmpToSaturnConverter        //
////////////////////////////////////////

BmpToSaturnConverter::~BmpToSaturnConverter()
{
	delete mpPackedTiles;
	mpPackedTiles = nullptr;
	mPackedTileSize = 0;
}

bool BmpToSaturnConverter::ConvertBmpToSakuraFormat(const string& inBmpPath, bool bFixupAlphaColor, const unsigned short inAlphaColor, const unsigned int* pTileWidth, const unsigned int* pTileHeight)
{
	//Read in translated font sheet
	BitmapReader origBmp;
	if (!origBmp.ReadBitmap(inBmpPath))
	{
		return false;
	}

	const unsigned int imageWidth = pTileWidth ? *pTileWidth : origBmp.mBitmapData.mInfoHeader.mImageWidth;
	const int imageHeight = pTileHeight ? *pTileHeight : origBmp.mBitmapData.mInfoHeader.mImageHeight;

	if (!mTileExtractor.ExtractTiles(imageWidth, imageHeight, imageWidth, abs(imageHeight), origBmp))
	{
		return false;
	}

	mTileExtractor.mImageWidth = abs(origBmp.mBitmapData.mInfoHeader.mImageWidth);
	mTileExtractor.mImageHeight = abs(origBmp.mBitmapData.mInfoHeader.mImageHeight);

	//Convert it to the SakuraTaisen format
	if (!mPalette.CreateFrom32BitData(origBmp.mBitmapData.mPaletteData.mpRGBA, origBmp.mBitmapData.mPaletteData.mSizeInBytes, false))
	{
		return false;
	}

	//Fix up palette
	if (bFixupAlphaColor)
	{
		//First index needs to have the transparent color
		int indexOfAlphaColor = -1;
		for (int i = 0; i < mPalette.GetNumColors(); ++i)
		{
			assert(i * 2 < mPalette.GetSize());

			const unsigned short color = *((short*)(mPalette.GetData() + i * 2));
			if (color == inAlphaColor)
			{
				const unsigned short oldColor0 = *((unsigned short*)mPalette.GetData());
				mPalette.SetValue(0, inAlphaColor);
				mPalette.SetValue(i, oldColor0);
				indexOfAlphaColor = i;
				break;
			}
		}

		if (indexOfAlphaColor == -1)
		{
			printf("Alpha Color not found.  Palette will not be correct. \n");
			indexOfAlphaColor = 0;
		}

		//Fix up color data now that the palette has been modified
		if (indexOfAlphaColor != -1)
		{
			mTileExtractor.FixupIndexOfAlphaColor((unsigned short)indexOfAlphaColor, origBmp.mBitmapData.mInfoHeader.mBitCount == 4);
		}
	}

	return true;
}

void BmpToSaturnConverter::PackTiles()
{
	if (mTileExtractor.mTiles.size())
	{
		mPackedTileSize = (unsigned int)mTileExtractor.mTiles.size() * mTileExtractor.mTiles[0].mTileSize;
		mpPackedTiles = new char[mPackedTileSize];
		
		int packedTileOffset = 0;
		for (const TileExtractor::Tile& tile : mTileExtractor.mTiles)
		{
			memcpy_s(mpPackedTiles + packedTileOffset, mPackedTileSize - packedTileOffset, tile.mpTile, tile.mTileSize);

			packedTileOffset += tile.mTileSize;
		}
	}
}

const char* BmpToSaturnConverter::GetImageData() const
{
	return mTileExtractor.mTiles[0].mpTile;
}

unsigned int BmpToSaturnConverter::GetImageDataSize() const
{
	return mTileExtractor.mTiles[0].mTileSize;
}


////////////////////////////////////
//        TileSetOptimizer        //
////////////////////////////////////
void TileSetOptimizer::OptimizeTileSet(const BmpToSaturnConverter& InSourceImage)
{
	std::unordered_map<uint32, std::vector<int>> crcToTileIndex;
	std::unordered_map<int, uint32> tileIndexToCrc;

	//Create mappings of tiles to their crcs
	int numUniqueTiles = 0;
	const int numTiles = InSourceImage.mTileExtractor.GetNumTiles();
	for (int tileIndex = 0; tileIndex < numTiles; ++tileIndex)
	{
		const uint32 tileCrc = InSourceImage.mTileExtractor.GetTileCRC(tileIndex);
		tileIndexToCrc[tileIndex] = tileCrc;

		//If a tile of this crc doesn't exist, then it's a unique tile
		if (crcToTileIndex[tileCrc].size() == 0)
		{
			++numUniqueTiles;
		}

		crcToTileIndex[tileCrc].push_back(tileIndex);
	}

	//Create optimized tile set
	int optimizedTileIndex = 0;
	std::unordered_set<int> processedCrcs;
	std::unordered_map<uint32, int> crcToOptimizedIndex;
	for (int tileIndex = 0; tileIndex < numTiles; ++tileIndex)
	{
		const uint32 tileCrc = tileIndexToCrc[tileIndex];
		assert(crcToTileIndex.find(tileCrc) != crcToTileIndex.end());

		//If a tile of this crc hasn't been processed, add it to the optimized tile set
		if (processedCrcs.find(tileCrc) == processedCrcs.end())
		{
			processedCrcs.insert(tileCrc);

			mOptimizedTileSet.AddTile(InSourceImage.mTileExtractor.mTiles[tileIndex]);

			const int optimizedTileIndex = mOptimizedTileSet.GetNumTiles() - 1;
			mTiledIndicesForOriginalImage.push_back(optimizedTileIndex);
			crcToOptimizedIndex[tileCrc] = optimizedTileIndex;
		}
		else
		{
			assert(crcToOptimizedIndex.find(tileCrc) != crcToOptimizedIndex.end());

			const int duplicateTileIndex = crcToOptimizedIndex[tileCrc];
			mTiledIndicesForOriginalImage.push_back(duplicateTileIndex);
		}
	}
}

void TileSetOptimizer::OutputImage(const std::string& InOutputPath, bool bInForceBmp) const
{
	FileWriter outFile;
	if(!outFile.OpenFileForWrite(InOutputPath))
	{
		return;
	}

	mOptimizedTileSet.OutputTiles(outFile, 0);
}

void TileSetOptimizer::PackTiles()
{
	if (mOptimizedTileSet.mTiles.size())
	{
		mPackedTileSize = (unsigned int)mOptimizedTileSet.mTiles.size() * mOptimizedTileSet.mTiles[0].mTileSize;
		mpPackedTiles = new char[mPackedTileSize];

		int packedTileOffset = 0;
		for (const TileExtractor::Tile& tile : mOptimizedTileSet.mTiles)
		{
			memcpy_s(mpPackedTiles + packedTileOffset, mPackedTileSize - packedTileOffset, tile.mpTile, tile.mTileSize);

			packedTileOffset += tile.mTileSize;
		}
	}
}

///////////////////////////
//        TileMap        //
///////////////////////////
bool TileMap::CreateFontSheetFromData(const char* pInData, unsigned int inDataSize)
{
	const int numBytesPerTile = 8 * 8;
	if (inDataSize % numBytesPerTile != 0)
	{
		printf("CreateFontSheet: Invalid size for data.  Should be multiple of 64, is %u", inDataSize);
		return false;
	}

	const int numTilesInFile = inDataSize / numBytesPerTile;
	for (int currTile = 0; currTile < numTilesInFile; ++currTile)
	{
		mTiles.push_back(std::move(TileData(&pInData[currTile * numBytesPerTile], numBytesPerTile)));
	}

	return true;
}

bool TileMap::CreateFontSheet(const FileNameContainer& inFileNameInfo)
{
	FileData fontFile;
	if (!fontFile.InitializeFileData(inFileNameInfo))
	{
		return false;
	}

	const int numBytesPerTile = 64*64;
	if (fontFile.GetDataSize() % numBytesPerTile != 0)
	{
		printf("CreateFontSheet: Invalid size for file %s", inFileNameInfo.mFileName.c_str());
		return false;
	}

	const char* pFontSheetData = fontFile.GetData();
	const int numTilesInFile = fontFile.GetDataSize() / numBytesPerTile;
	for (int currTile = 0; currTile < numTilesInFile; ++currTile)
	{
		mTiles.push_back(std::move(TileData(&pFontSheetData[currTile * numBytesPerTile], numBytesPerTile)));
	}

	return true;
}

const char* TileMap::GetTileData(int inTileIndex) const
{
	assert(inTileIndex >= 0 && inTileIndex < (int)mTiles.size());
	if (inTileIndex >= (int)mTiles.size())
	{
		inTileIndex = 0;
	}

	return inTileIndex < (int)mTiles.size() ? mTiles[inTileIndex].GetFontTileData() : nullptr;
}

////////////////////////////////
//        MemoryBlocks        //
////////////////////////////////
MemoryBlocks::~MemoryBlocks()
{
	for(MemoryBlocks::Block& block : mBlocks)
	{
		delete[] block.pData;
		block.pData = nullptr;
	}

	mBlocks.clear();

	delete[] mpCombinedBlocks;
	mpCombinedBlocks = nullptr;
}

char* MemoryBlocks::AddBlock(const char* pOriginalData, unsigned int offset, unsigned int blockSize, bool bReverseBytes)
{
	mBlocks.push_back( MemoryBlocks::Block() );

	MemoryBlocks::Block& newBlock = mBlocks[mBlocks.size() - 1];
	newBlock.pData                = new char[blockSize];
	newBlock.blockSize            = blockSize;
	mCombinedSize += blockSize;

	memcpy_s(newBlock.pData, blockSize, pOriginalData + offset, blockSize);

	if( bReverseBytes )
	{
		std::reverse(newBlock.pData, (newBlock.pData + blockSize));
	}

	return newBlock.pData;
}

void MemoryBlocks::AddBlock(const MemoryBlocks& inBlock)
{
	for(unsigned int b = 0; b < inBlock.GetNumberOfBlocks(); ++b)
	{
		AddBlock(inBlock.GetBlock(b).pData, 0, inBlock.GetBlock(b).blockSize);
	}
}

void MemoryBlocks::CombineBlocks()
{
	delete[] mpCombinedBlocks;
	mpCombinedBlocks = nullptr;
	
	//Combine data into a contiguous block
	unsigned int totalOffset = 0;
	mpCombinedBlocks = new char[mCombinedSize];
	for(const MemoryBlocks::Block& block : mBlocks)
	{
		memcpy_s(mpCombinedBlocks + totalOffset, mCombinedSize - totalOffset, block.pData, block.blockSize);
		totalOffset += block.blockSize;
	}
}

size_t MemoryBlocks::GetNumberOfBlocks() const
{
	return mBlocks.size();
}

const MemoryBlocks::Block& MemoryBlocks::GetBlock(unsigned int blockIndex) const
{
	assert(blockIndex < mBlocks.size());

	return mBlocks[blockIndex];
}

bool MemoryBlocks::WriteInBlock(unsigned int blockIndex, unsigned int offset, const char* pData, unsigned int dataSize)
{
	if( blockIndex >= mBlocks.size() )
	{
		printf("MemoryBlocks::WriteInBlock: Invalid block index\n");
		return false;
	}

	if( offset + dataSize >= mBlocks[blockIndex].blockSize )
	{
		printf("MemoryBlocks::WriteInBlock: Data will not fit in the block\n");
		return false;
	}

	memcpy_s(mBlocks[blockIndex].pData + offset, mBlocks[blockIndex].blockSize - offset, pData, dataSize);
	return true;
}

/////////////////////////////////////
//        PuyoPRSCompressor        //
/////////////////////////////////////
void PuyoPrsCompressor::CompressData(const void* pInData, const unsigned long inDataSize)
{
	const uint8* pSourceData = (const uint8*)pInData;

	// Get the source length
	int sourceLength = inDataSize;
	
	uint8 bitPos = 0;
	uint8 controlByte = 0;

	int position = 0;
	int currentLookBehindPosition, currentLookBehindLength;
	int lookBehindOffset, lookBehindLength;

	std::vector<uint8> data;

	while (position < sourceLength)
	{
		currentLookBehindLength = 0;
		lookBehindOffset = 0;
		lookBehindLength = 0;

		for (currentLookBehindPosition = position - 1; (currentLookBehindPosition >= 0) && (currentLookBehindPosition >= position - 0x1FF0) && (lookBehindLength < 256); currentLookBehindPosition--)
		{
			currentLookBehindLength = 1;
			if (pSourceData[currentLookBehindPosition] == pSourceData[position])
			{
				do
				{
					currentLookBehindLength++;
				} while ((currentLookBehindLength <= 256) &&
					(position + currentLookBehindLength <= (int)inDataSize) &&
					pSourceData[currentLookBehindPosition + currentLookBehindLength - 1] == pSourceData[position + currentLookBehindLength - 1]);

				currentLookBehindLength--;
				if (((currentLookBehindLength >= 2 && currentLookBehindPosition - position >= -0x100) || currentLookBehindLength >= 3) && currentLookBehindLength > lookBehindLength)
				{
					lookBehindOffset = currentLookBehindPosition - position;
					lookBehindLength = currentLookBehindLength;
				}
			}
		}

		if (lookBehindLength == 0)
		{
			data.push_back(pSourceData[position++]);
			PutControlBit(1, controlByte, bitPos, data, mCompressedData);
		}
		else
		{
			Copy(lookBehindOffset, lookBehindLength, controlByte, bitPos, data, mCompressedData);
			position += lookBehindLength;
		}
	}

	PutControlBit(0, controlByte, bitPos, data, mCompressedData);
	PutControlBit(1, controlByte, bitPos, data, mCompressedData);
	if (bitPos != 0)
	{
		controlByte = (byte)((controlByte << bitPos) >> 8);
		Flush(controlByte, bitPos, data, mCompressedData);
	}

	mCompressedData.push_back(0);
	mCompressedData.push_back(0);
}

void PuyoPrsCompressor::Copy(int offset, int size, uint8& controlByte, uint8& bitPos, std::vector<uint8>& data, std::vector<uint8>& destination)
{
	if ((offset >= -0x100) && (size <= 5))
	{
		size -= 2;
		PutControlBit(0, controlByte, bitPos, data, destination);
		PutControlBit(0, controlByte, bitPos, data, destination);
		PutControlBit((size >> 1) & 1, controlByte, bitPos, data, destination);
		data.push_back((uint8)(offset & 0xFF));
		PutControlBit(size & 1, controlByte, bitPos, data, destination);
	}
	else
	{
		if (size <= 9)
		{
			PutControlBit(0, controlByte, bitPos, data, destination);
			data.push_back((uint8)(((offset << 3) & 0xF8) | ((size - 2) & 0x07)));
			data.push_back((uint8)((offset >> 5) & 0xFF));
			PutControlBit(1, controlByte, bitPos, data, destination);
		}
		else
		{
			PutControlBit(0, controlByte, bitPos, data, destination);
			data.push_back((uint8)((offset << 3) & 0xF8));
			data.push_back((uint8)((offset >> 5) & 0xFF));
			data.push_back((uint8)(size - 1));
			PutControlBit(1, controlByte, bitPos, data, destination);
		}
	}
}

void PuyoPrsCompressor::PutControlBit(int bit, uint8& controlByte, uint8& bitPos, std::vector<uint8>& data, std::vector<uint8>& destination)
{
	controlByte >>= 1;
	controlByte |= (byte)(bit << 7);
	bitPos++;
	if (bitPos >= 8)
	{
		Flush(controlByte, bitPos, data, destination);
	}
}

void PuyoPrsCompressor::Flush(uint8& inControlByte, uint8& inBitPos, std::vector<uint8>& inData, std::vector<uint8>& inDestination)
{
	inDestination.push_back(inControlByte);
	inControlByte = 0;
	inBitPos = 0;

	inDestination.insert(std::end(inDestination), std::begin(inData), std::end(inData));
	inData.clear();
}

/////////////////////////////////
//        PRSCompressor        //
/////////////////////////////////
PRSCompressor::~PRSCompressor()
{
	Reset();
}

void PRSCompressor::CompressData(const void* pInData, const unsigned long inDataSize, ECompressOption compressOption)
{
	Reset();

	PuyoPrsCompressor puyoCompressor;
	puyoCompressor.CompressData(pInData, inDataSize);

	mCompressedSize = (unsigned long)puyoCompressor.GetCompressedData().size();
	mpCompressedData = new char[mCompressedSize];
	memcpy_s(mpCompressedData, mCompressedSize, puyoCompressor.GetCompressedData().data(), mCompressedSize);
	
//	mpCompressedData = new char[inDataSize];
//	mCompressedSize = prs_compress((uint8_t*)pInData, (uint8_t**)&mpCompressedData, inDataSize);

	if( compressOption != PRSCompressor::kCompressOption_None )
	{
		unsigned long newSize = 0;

		if( compressOption == PRSCompressor::kCompressOption_TwoByteAlign && mCompressedSize%2 != 0 )
		{
			newSize = mCompressedSize + mCompressedSize%2;
		}
		else if( compressOption == PRSCompressor::kCompressOption_FourByteAlign && mCompressedSize % 4 != 0 )
		{
			const unsigned long paddingAmount = mCompressedSize%4 == 0 ? 0 : (4 - mCompressedSize%4);
			newSize = mCompressedSize + paddingAmount;
		}

		if( newSize != 0 )
		{
			char* pNewData = new char[newSize];
			memset(pNewData, 0, newSize);

			memcpy_s(pNewData, newSize, mpCompressedData, mCompressedSize);
		
			delete[] mpCompressedData;
			mpCompressedData = pNewData;
			mCompressedSize  = newSize;
		}
	}
}

void PRSCompressor::Reset()
{
	delete[] mpCompressedData;
	mpCompressedData = nullptr;

	mCompressedSize = 0;
}

///////////////////////////////////
//        PRSDecompressor        //
///////////////////////////////////
PRSDecompressor::~PRSDecompressor()
{
	delete[] mpUncompressedData;
	mpUncompressedData = nullptr;
}

bool PRSDecompressor::UncompressData(void* pInData, unsigned int inDataSize)
{
	unsigned char* pBuffer = nullptr;
	int decompressedSize = prs_decompress_buf((uint8_t*)pInData, &pBuffer, inDataSize, mCompressedSize);
	if( decompressedSize > 0 )
	{
		mpUncompressedData    = std::move( reinterpret_cast<char*>(pBuffer) );
		mUncompressedDataSize = (unsigned long)decompressedSize;

		return true;
	}

	return false;
	/*
	mUncompressedDataSize = prs_decompress_size((void*)pInData);
	mpUncompressedData    = new char[mUncompressedDataSize + 1];
	memset(mpUncompressedData, 0, mUncompressedDataSize + 1);
	prs_decompress(pInData, mpUncompressedData, mUncompressedDataSize);

	if( mpUncompressedData[mUncompressedDataSize] != 0 )
	{
		int k = 0;
		++k;
	}
	*/
}

/////////////////////////////////
//        CsvTileReader        //
/////////////////////////////////
bool CsvTileReader::ReadInTiles(const string& inFilePath)
{
	FILE* tileFile = nullptr;
	errno_t errorValue = fopen_s(&tileFile, inFilePath.c_str(), "r");
	if (errorValue)
	{
		printf("Unable to open %s\n", inFilePath.c_str());
		return false;
	}

	while (feof(tileFile) != EOF)
	{
		char comma;
		int tileIndex;

		if (fscanf_s(tileFile, "%i%c", &tileIndex, &comma, 2) == EOF)
		{
			break;
		}

		mTileEntries.push_back(SwapByteOrder(tileIndex));
	}

	fclose(tileFile);

	return true;
}

void CsvTileReader::SwapEndianess()
{
	for(int& t : mTileEntries)
	{
		t = SwapByteOrder(t);
	}
}

/////////////////////////////////////
//        YabauseToMednafin        //
/////////////////////////////////////
void YabauseToMednafin::ConvertData(const string& inFileName, const string& outFileName)
{
	FileData yabauseFile;
	if( !yabauseFile.InitializeFileData( FileNameContainer(inFileName.c_str()) ) )
	{
		printf("Unable to open %s\n", inFileName.c_str());
		return;
	}

	FileWriter outFile;
	if( !outFile.OpenFileForWrite(outFileName) )
	{
		printf("Unable to open %s", outFileName.c_str());
		return;
	}

	const unsigned long yabauseFileSize = yabauseFile.GetDataSize();
	for(unsigned long i = 0; i < yabauseFileSize; ++i)
	{
		if( i % 2 != 0 )
		{
			outFile.WriteData( (char*)(&yabauseFile.GetData()[i]), 1);
		}
	}

	printf("Successfully converted Yabause Save to Mednafin!\n");
}

/////////////////////////////////////
//        YabauseToMednafin        //
/////////////////////////////////////
void MednafinToYabause::ConvertData(const string& inFileName, const string& outFileName)
{
	FileData mednafinFile;
	if( !mednafinFile.InitializeFileData( FileNameContainer(inFileName.c_str()) ) )
	{
		printf("Unable to open %s\n", inFileName.c_str());
		return;
	}

	FileWriter outFile;
	if( !outFile.OpenFileForWrite(outFileName) )
	{
		printf("Unable to open %s", outFileName.c_str());
		return;
	}

	const unsigned char ff = 0xff;
	const unsigned long yabauseFileSize = mednafinFile.GetDataSize();
	for(unsigned long i = 0; i < yabauseFileSize; ++i)
	{
		outFile.WriteData(&ff, sizeof(ff));
		outFile.WriteData( (char*)(&mednafinFile.GetData()[i]), 1);
	}

	printf("Successfully converted Yabause Save to Mednafin!\n");
}

////////////////////////////////////
//        Helper Functions        //
////////////////////////////////////
bool CreateTemporaryDirectory(string& outDir)
{
	char buffer[MAX_PATH];
	const DWORD dwRet = GetCurrentDirectory(MAX_PATH, buffer);
	if( !dwRet )
	{
		return false;
	}

	//Create temp work directory
	const string tempDir = string(buffer) + string("\\Temp\\");
	if( !CreateDirectoryHelper(tempDir) )
	{
		return false;
	}

	outDir = tempDir;
	return true;
}

bool CopyFiles(const std::vector<FileNameContainer>& InSourceFiles, const std::string& InOutputDirectory)
{
	//Bring over scenario files
	for (const FileNameContainer& sourceFile : InSourceFiles)
	{
		string subDirectory = "";
		const size_t lastIndex = sourceFile.mPathOnly.find_last_of("\\");
		if (lastIndex != std::string::npos)
		{
			subDirectory = sourceFile.mPathOnly.substr(lastIndex, sourceFile.mPathOnly.size()) + Seperators;
		}

		const string outputFile = InOutputDirectory + subDirectory + sourceFile.mFileName;
		if (!CopyFile(sourceFile.mFullPath.c_str(), outputFile.c_str(), FALSE))\
		{
			printf("Unable to copy over %s\n", sourceFile.mFullPath.c_str());
			return false;
		}
	}

	return true;
}

bool CopyFiles(const std::string& InSourceDirectory, const std::string& InOutputDirectory)
{
	std::vector<FileNameContainer> sourceFiles;
	FindAllFilesWithinDirectory(InSourceDirectory, sourceFiles);

	return CopyFiles(sourceFiles, InOutputDirectory);
}