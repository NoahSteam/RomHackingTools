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
	const unsigned long finalOffset   = mFileSize - otherFile.mFileSize;

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
//        TextFileData      //
//////////////////////////////
bool TextFileData::InitializeTextFile()
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
	memset(buffer, 0, bufferSize);

	char* pToken = nullptr;
	while( fgets(buffer, bufferSize, pFile) != nullptr )
	{
		TextLine newLineOfText;
		char *pNextToken   = nullptr;;
		char* pContext     = nullptr;
		const char* pDelim = " \n";

		pToken = strtok_s(buffer, pDelim, &pContext);
		while(pToken != NULL)
		{
			newLineOfText.mWords.push_back(pToken);

			pToken = strtok_s(NULL, pDelim, &pContext);
		}

		mLines.push_back(std::move(newLineOfText));
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

void TextFileWriter::Close()
{
	if( mpFileHandle )
	{
		fclose(mpFileHandle);
	}

	mpFileHandle = nullptr;
}

///////////////////////////////
//        PaletteData        //
///////////////////////////////
PaletteData::~PaletteData()
{
	delete[] mpPaletteData;
	mpPaletteData = nullptr;
}

void PaletteData::CreateFrom15BitData(const char* pInPaletteData, int inPaletteSize)
{
	assert(inPaletteSize == 32);

	//Create 32bit palette from the 16 bit(5:5:5 bgr) palette in SakuraTaisen
	const int numColorInPalette = inPaletteSize/2;
	mNumBytesInPalette          = numColorInPalette*4;
	mpPaletteData               = new char[mNumBytesInPalette];
	const float full5BitValue   = (float)0x1f;
	for(int i = 0, origIdx = 0; i < mNumBytesInPalette; i += 4, origIdx += 2)
	{
		unsigned short color = ((pInPaletteData[origIdx] << 8) + (unsigned char)pInPaletteData[origIdx+1]);

		//Ugly conversion of 5bit values to 8bit.  Probably a better way to do this.
		//Masking the r,g,b components and then bringing the result into a [0,255] range.
		mpPaletteData[i]   = (char)floor( ( ((color & 0x001f)/full5BitValue) * 255.f) + 0.5f);
		mpPaletteData[i+1] = (char)floor( ( ( ((color & 0x03E0) >> 5)/full5BitValue) * 255.f) + 0.5f);
		mpPaletteData[i+2] = (char)floor( ( ( ((color & 0x7C00) >> 10)/full5BitValue) * 255.f) + 0.5f);
		mpPaletteData[i+3] = 0;
	}
}

bool PaletteData::CreateFrom32BitData(const char* pInPaletteData, int inPaletteSize)
{
	if( inPaletteSize != 64 )
	{
		printf("Unsupported palette type");
		return false;
	}

	const int numColors        = 16;
	const int numBytesPerColor = 2;

	mNumBytesInPalette = numColors*numBytesPerColor;
	mpPaletteData      = new char[mNumBytesInPalette];

	for(int i = 0, outIndex = 0; i < inPaletteSize; i += 4, outIndex += 2)
	{
		assert(outIndex + 1 < mNumBytesInPalette);

		//Bring into 15bit [0,31] range 
		const unsigned char r = (unsigned char)floor( ((unsigned char)(pInPaletteData[i+0])/255.f)*31.f + 0.5f);
		const unsigned char g = (unsigned char)floor( ((unsigned char)(pInPaletteData[i+1])/255.f)*31.f + 0.5f);
		const unsigned char b = (unsigned char)floor( ((unsigned char)(pInPaletteData[i+2])/255.f)*31.f + 0.5f);

		//Swap byte order so data is written in big endian order
		unsigned short outColor = (r << 10) + (g << 5) + b;
		std::reverse((char*)&outColor, ((char*)&outColor + 2));

		//copy data over to the palette
		memcpy(&mpPaletteData[outIndex], &outColor, 2);
	}

	return true;
}

void PaletteData::SetValue(int index, unsigned short value)
{
	assert(index*2 + 1 < mNumBytesInPalette);

	memcpy(mpPaletteData + index*2, &value, sizeof(value));
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
//        BitmapReader        //
/////////////////////////////////
bool BitmapReader::ReadBitmap(const string& inBitmapName)
{
	FileData inFile;
	if( !inFile.InitializeFileData(inBitmapName.c_str(), inBitmapName.c_str()) )
	{
		return false;
	}

	const char* pFileData = inFile.GetData();
	memcpy(&mBitmapData.mFileHeader, pFileData, sizeof(mBitmapData.mFileHeader));

	if( mBitmapData.mFileHeader.mType != 0x4D42 )
	{
		printf("%s is not a bitmap file", inBitmapName.c_str());
		return false;
	}

	memcpy(&mBitmapData.mInfoHeader, pFileData + sizeof(mBitmapData.mFileHeader), sizeof(mBitmapData.mInfoHeader));

	if( mBitmapData.mInfoHeader.mBitCount != 4 )
	{
		printf("%s has unsupported bit count [%i]", inBitmapName.c_str(), mBitmapData.mInfoHeader.mBitCount);
		return false;	
	}

	//Read in palette
	const int numColorsInPalette          = (int)pow(2, mBitmapData.mInfoHeader.mBitCount);
	const int numBytesPerColor            = 4;
	mBitmapData.mPaletteData.mSizeInBytes = numColorsInPalette*numBytesPerColor;
	mBitmapData.mPaletteData.mpRGBA       = new char[mBitmapData.mPaletteData.mSizeInBytes];
	memcpy(mBitmapData.mPaletteData.mpRGBA, pFileData + sizeof(mBitmapData.mFileHeader) + sizeof(mBitmapData.mInfoHeader), mBitmapData.mPaletteData.mSizeInBytes);

	//Read in color data
	const int numColorBytes             = abs(mBitmapData.mInfoHeader.mImageHeight)*mBitmapData.mInfoHeader.mImageWidth / 2;
	mBitmapData.mColorData.mSizeInBytes = numColorBytes;
	mBitmapData.mColorData.mpRGBA       = new char[numColorBytes];
	memcpy(mBitmapData.mColorData.mpRGBA, pFileData + mBitmapData.mFileHeader.mOffsetToData, numColorBytes);

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
			assert( offset + y*mBytesPerRow + x < mBufferSize );

			pOutData[y*mBytesPerRow + x] = pInData[inDataOffset++];
		}
	}
}

bool BitmapSurface::WriteToFile(const std::string& fileName)
{
	BitmapWriter bitmap;

	return bitmap.CreateBitmap(fileName, mWidth, -mHeight, mBitsPerPixel, mpBuffer, mBufferSize, mpPalette, mPaletteSize);
}

/////////////////////////////////
//        TileExtractor        //
/////////////////////////////////
bool TileExtractor::ExtractTiles(unsigned int tileWidth, unsigned int tileHeight, const BitmapReader& inBitmap)
{
	if( inBitmap.mBitmapData.mInfoHeader.mBitCount != 4 )
	{
		printf("Can't extract tiles.  Only 4 bit paletted bitmaps supported");
		return false;
	}

	if( inBitmap.mBitmapData.mInfoHeader.mImageWidth % tileWidth != 0 || inBitmap.mBitmapData.mInfoHeader.mImageHeight % tileHeight != 0 )
	{
		printf("Can't extract tiles.  Only 4 bit paletted bitmaps supported");
		return false;
	}

	const unsigned int numRows    = (abs(inBitmap.mBitmapData.mInfoHeader.mImageHeight)/tileHeight);
	const unsigned int numColumns = (inBitmap.mBitmapData.mInfoHeader.mImageWidth/tileWidth);
	const unsigned int numTiles   = numRows * numColumns;
	const char* pLinearData       = inBitmap.mBitmapData.mColorData.mpRGBA;
	
	//Allocate tiles
	mTiles.resize(numTiles);

	const unsigned int numBytesPerTileWidth = tileWidth/2;
	const unsigned int stride               = numBytesPerTileWidth*numColumns;
	const unsigned int numBytesPerTile      = (tileWidth*tileHeight)/2; //4bits per pixel
	unsigned int currTile                   = 0;

	//Bitmaps are stored upside down, so start form the bottom
	for(int y = numRows - 1; y >= 0; --y)
	{
		for(unsigned int x = 0; x < numColumns; ++x)
		{
			mTiles[currTile].mpTile    = new char[numBytesPerTile];
			mTiles[currTile].mTileSize = numBytesPerTile;
			mTiles[currTile].mX        = x;
			mTiles[currTile].mY        = y;

			unsigned int linearDataIndex = y*stride*tileHeight + x*numBytesPerTileWidth;
			for(unsigned int tilePixelY = 0; tilePixelY < tileHeight; ++tilePixelY)
			{
				for(unsigned int tilePixelX = 0; tilePixelX < numBytesPerTileWidth; ++tilePixelX)
				{
					const unsigned int linearY = tileHeight - tilePixelY - 1;
					assert( (int)linearY >= 0 );
					assert(tilePixelY*numBytesPerTileWidth + tilePixelX < numBytesPerTile);
					assert(linearDataIndex + linearY*stride + tilePixelX < (unsigned int)inBitmap.mBitmapData.mColorData.mSizeInBytes);

					const unsigned char linearColorValue = pLinearData[linearDataIndex + linearY*stride + tilePixelX];
					mTiles[currTile].mpTile[tilePixelY*numBytesPerTileWidth + tilePixelX] = linearColorValue;
				}
			}

			++currTile;
		}
	}

	return true;
}
