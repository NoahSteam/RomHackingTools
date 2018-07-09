#include <stdio.h>
#include <windows.h>
#include <vector>
#include <string>
#include <assert.h>

#include "Utils.h"
#include "..\ExternalTools\lodepng.h"

using std::string;
using std::vector;

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

unsigned long FileData::GetCRC()
{
	if( !mbCrcCalculated )
	{
		for(unsigned long i = 0; i < mBufferSize; ++i)
		{
			CalcCrc32(mpData[i], mCrc);
		}
	}

	return mCrc;
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
	static char fixedString[bufferSize];
	memset(buffer, 0, bufferSize);
	memset(fixedString, 0, bufferSize);

	const string space(" ");
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
			//Hack to fix up unicode value of '…'.  '…' is replaced with '@'
			memset(fixedString, 0, bufferSize);
			const size_t tokenLen = strlen(pToken);
			for(size_t t = 0, f = 0; t < tokenLen; t)
			{
				if( pToken[t] == -30 )
				{
					t += 3;

					fixedString[f++] = '@';
				}
				else
				{
					fixedString[f++] = pToken[t++];
				}
			}

			newLineOfText.mWords.push_back(fixedString);
			newLineOfText.mFullLine += string(fixedString) + space;

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

bool PaletteData::CreateFrom15BitData(const char* pInPaletteData, int inPaletteSize)
{
	if( inPaletteSize != 32 && inPaletteSize != 512 )
	{
		printf("Unsupported palette type");
		return false;
	}

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
		mpPaletteData[i+2]  = (char)floor( ( ((color & 0x001f)/full5BitValue) * 255.f) + 0.5f);
		mpPaletteData[i+1] = (char)floor( ( ( ((color & 0x03E0) >> 5)/full5BitValue) * 255.f) + 0.5f);
		mpPaletteData[i+0] = (char)floor( ( ( ((color & 0x7C00) >> 10)/full5BitValue) * 255.f) + 0.5f);
		mpPaletteData[i+3] = 0;
	}

	return true;
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
	if( 1 ) //bitsPerPixel == 4 )
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
		infoHeader.Initialize(inWidth, inHeight, bitsPerPixel);

		outFile.WriteData(&fileHeader, sizeof(fileHeader));
		outFile.WriteData(&infoHeader, sizeof(infoHeader));

		if( pInPaletteData )
		{
			outFile.WriteData(pInPaletteData, inPaletteSize);
		}

		outFile.WriteData(pInColorData, inColorSize);

		if( bitsPerPixel == 8 )
		{
			const int numExpectedBytes = inWidth*abs(inHeight);
			if( inColorSize < numExpectedBytes )
			{
				const int blankBytes = numExpectedBytes - inColorSize;
				char* pRemainingPixels = new char[blankBytes];
				memset(pRemainingPixels, 0, sizeof(blankBytes));

				outFile.WriteData(pRemainingPixels, blankBytes);

				delete[] pRemainingPixels;
			}
		}

		outFile.Close();
	}
	

	return true;
}

bool BitmapWriter::SaveAsPNG(const string& inFileName, int inWidth, int inHeight, int bitsPerPixel, const char* pInColorData, int inColorSize, const char* pInPaletteData, int inPaletteSize)
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
bool TileExtractor::ExtractTiles(unsigned int inTileWidth, unsigned int inTileHeight, unsigned int outTileWidth, unsigned int outTileHeight, const BitmapReader& inBitmap)
{
	if( inTileWidth > outTileWidth || inTileHeight > outTileHeight )
	{
		printf("Can't extract tiles.  Invalid tile sizes\n");
		return false;
	}

	if( inBitmap.mBitmapData.mInfoHeader.mBitCount != 4 )
	{
		printf("Can't extract tiles.  Only 4 bit paletted bitmaps supported\n");
		return false;
	}

	if( inBitmap.mBitmapData.mInfoHeader.mImageWidth % inTileWidth != 0 || inBitmap.mBitmapData.mInfoHeader.mImageHeight % inTileHeight != 0 )
	{
		//printf("Can't extract tiles.  Only 4 bit paletted bitmaps supported\n");
		//return false;
	}

	const unsigned int numRows    = (abs(inBitmap.mBitmapData.mInfoHeader.mImageHeight)/inTileHeight);
	const unsigned int numColumns = (inBitmap.mBitmapData.mInfoHeader.mImageWidth/inTileWidth);
	const unsigned int numTiles   = numRows * numColumns;
	const char* pLinearData       = inBitmap.mBitmapData.mColorData.mpRGBA;
	
	//Allocate tiles
	mTiles.resize(numTiles);

	const unsigned int numBytesPerTileWidth = inTileWidth/2;
	const unsigned int stride               = numBytesPerTileWidth*numColumns;
	const unsigned int numBytesPerTile      = (outTileWidth*outTileHeight)/2;//(inTileWidth*inTileHeight)/2; //4bits per pixel
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
			memset(mTiles[currTile].mpTile, 0, numBytesPerTile);

			unsigned int linearDataIndex = y*stride*inTileHeight + x*numBytesPerTileWidth;
			for(unsigned int tilePixelY = 0; tilePixelY < inTileHeight; ++tilePixelY)
			{
				for(unsigned int tilePixelX = 0; tilePixelX < numBytesPerTileWidth; ++tilePixelX)
				{
					const unsigned int linearY = inTileHeight - tilePixelY - 1;
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
