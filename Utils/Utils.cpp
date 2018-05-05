#include <stdio.h>
#include <windows.h>
#include <vector>
#include <string>
#include <assert.h>

#include "Utils.h"

using std::string;
using std::vector;

void FindAllFilesWithinDirectory(const string& inDirectoryPath, vector<FileNameContainer>& outFileNames)
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
			outFileNames.push_back(std::move(FileNameContainer(fileData.cFileName, filePath.c_str())));
		}

		if (!FindNextFile(result, &fileData))
			break;
	}
}

bool CreateDirectoryHelper(const std::string& dirName)
{
	if( CreateDirectory(dirName.c_str(), NULL) || ERROR_ALREADY_EXISTS == GetLastError() )
	{
		return true;
	}
	
	printf("Unable to create directory: %s", dirName.c_str());

	return false;
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

bool FileData::ReadInFileData(const char* pFileName)
{
	//Open file in read-binary mode
	FILE* pFile = nullptr;
	errno_t errorValue = fopen_s(&pFile, pFileName, "rb");
	if( errorValue )
	{
		printf("Unable to open file: %s.  Error code: %i \n", pFileName, errorValue);
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

bool FileData::InitializeFileData(const FileNameContainer& inFileData)
{
	mFileName = inFileData.mFileName;
	mFullPath = inFileData.mFullPath;

	return ReadInFileData(mFullPath.c_str());
}

bool FileData::InitializeFileData(const char* pFileName, const char* pFullPath)
{
	mFileName = pFileName;
	mFullPath = pFullPath;

	return ReadInFileData(mFullPath.c_str());
}

bool FileData::DoesThisFileContain(const FileData& otherFile, vector<unsigned long>& outOffsets, bool bFindMultiple) const
{	
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
			outOffsets.push_back(currentOffset);

			if( !bFindMultiple )
			{
				return true;
			}
		}

		++currentOffset;
	}

	return outOffsets.size() > 0;
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

bool FileWriter::WriteData(const void* pInData, unsigned long inDataSize)
{
	if( !mpFileHandle )
	{
		return false;
	}

	const unsigned long numElemsWritten = (unsigned long)fwrite(pInData, sizeof(char), inDataSize, mpFileHandle);
	mDataSize                          += numElemsWritten*sizeof(char);

	return numElemsWritten == inDataSize;
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

////////////////////////////////
//        BitmapWriter        //
////////////////////////////////
bool BitmapWriter::CreateBitmap(const string& inFileName, int inWidth, int inHeight, int bitsPerPixel, const char* pInColorData, int inColorSize, const char* pInPaletteData, int inPaletteSize)
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
	infoHeader.Initialize(inWidth, inHeight, bitsPerPixel);

	outFile.WriteData(&fileHeader, sizeof(fileHeader));
	outFile.WriteData(&infoHeader, sizeof(infoHeader));

	if( pInPaletteData )
	{
		outFile.WriteData(pInPaletteData, inPaletteSize);
	}

	outFile.WriteData(pInColorData, inColorSize);
	
	outFile.Close();

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
}

bool BitmapSurface::CreateSurface(int width, int height, EBitsPerPixel bitsPerPixel, const char* pPalette, int paletteSize)
{
	const int numPixels = width*height;
	const int numBytes  = bitsPerPixel == kBPP_4 ? numPixels*4 : numPixels;
	mBytesPerRow        = bitsPerPixel == kBPP_4 ? width/2 : width;

	mWidth        = width;
	mHeight       = height;
	mBitsPerPixel = bitsPerPixel;

	mpBuffer    = new char[numBytes];
	mBufferSize = numBytes;

	mpPalette    = pPalette;
	mPaletteSize = paletteSize;

	memset(mpBuffer, 0, numBytes);

	return true;
}

void BitmapSurface::AddTile(const char* pInData, int dataSize, int inX, int inY, int width, int height)
{
	const int startX = inX/2;
	const int offset = (inY*mBytesPerRow + startX);
	char* pOutData   = mpBuffer + offset;

	assert(offset < mBufferSize);
	int inDataOffset = 0;
	for(int y = 0; y < height; ++y)
	{
		for(int x = 0; x < 8; ++x)
		{
			assert( offset + y*mBytesPerRow + x + startX < mBufferSize );

			pOutData[y*mBytesPerRow + x + startX] = pInData[inDataOffset++];
		}
	}
}

bool BitmapSurface::WriteToFile(const std::string& fileName)
{
	BitmapWriter bitmap;

	return bitmap.CreateBitmap(fileName, mWidth, -mHeight, mBitsPerPixel, mpBuffer, mBufferSize, mpPalette, mPaletteSize);
}
