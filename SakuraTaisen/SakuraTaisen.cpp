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
#include <map>
#include <unordered_map>
#include <algorithm>
#include <utility>
#include "..\Utils\Utils.h"
#include "..\Utils\decompress_rtns.c"
#include <assert.h>

using std::vector;
using std::string;
using std::list;
using std::map;

#define OPTIMIZE_INSERTION_DEBUGGING 0
#define USE_SINGLE_BYTE_LOOKUPS 1
#define USE_4_BYTE_OFFSETS 1
#define USE_TREKKIS_MINIGAME_DATA 1
#define FIX_TIMING_DATA 1
#define FIX_EVT_FILES 0
#define USE_LARGE_BATTLE_MENU 0
#define FIX_SLG_FONT_DRAWING_SIZE 1

const unsigned char OutTileSpacingX = 8;
const unsigned char OutTileSpacingY = 12;
const unsigned long MaxTBLFileSize  = 0x20000;
const unsigned long MaxMESFileSize  = 0x1A800;
const unsigned long MaxWKLFileSize  = 0x82E00;
const char          MaxLines        = 4;
const unsigned char CharTimingStartID = 0x2E;
bool GIsDisc2 = false;

const string PatchedPaletteName("PatchedPalette.BIN");
const string PatchedKNJName("PatchedKNJ.BIN");
const string Disc1("\\Disc1");
const string Disc2("\\Disc2");
const string NewLineWord("<br>");
const string LipsWord("<LIPS>");
const string SpaceWord("<sp>");
const string BMPExtension(".bmp");
const string SlgFontFileSuffix("_SLG");
const unsigned short SpecialDialogIndicator = 0xC351;

void PrintPaletteColors(const string& paletteFile)
{
	FileNameContainer fileName(paletteFile.c_str());

	FileData paletteFileData;
	if( !paletteFileData.InitializeFileData(fileName) )
	{
		printf("Couldn't open file %s\n", paletteFile.c_str());
		return;
	}

	PaletteData paletteData;
	paletteData.CreateFrom15BitData(paletteFileData.GetData(), paletteFileData.GetDataSize());

	const char* pColorData = paletteData.GetData();
	for(int i = 0; i < paletteData.GetNumColors(); ++i)
	{
		printf("%i) R: %u G: %u B: %u\n", i, (unsigned char)pColorData[i*4 + 2], (unsigned char)pColorData[i*4 + 1], (unsigned char)pColorData[i*4 + 0]);
	}
}

struct SakuraCompressedData
{
	char*         mpCompressedData = nullptr;
	unsigned long mDataSize        = 0;
	unsigned long mFrontPad        = 0;

	~SakuraCompressedData()
	{
		delete[] mpCompressedData;
		mpCompressedData = nullptr;
	}

	void PatchDataInMemory(const char* pInData, const unsigned long inDataSize, bool bPad, bool bFrontPad = false, unsigned long padAmount = 0)
	{
		//Compress bgnd image
		PRSCompressor compressor;
		compressor.CompressData((void*)pInData, inDataSize, PRSCompressor::kCompressOption_None);

		//Pad compressed data is it is divisible by 2
		if(bPad)
		{
			if( compressor.mCompressedSize < padAmount )
			{
				mDataSize = padAmount;

				if( bFrontPad )
				{
					mFrontPad = padAmount - compressor.mCompressedSize;
				}
			}
			else
			{
				const unsigned long alignmentPadding = (compressor.mCompressedSize % 4) == 0 ? 0 : 4 - (compressor.mCompressedSize % 4);
				mDataSize = compressor.mCompressedSize + (compressor.mCompressedSize % 4);
			}

			mpCompressedData = new char[mDataSize];
			memset(mpCompressedData, 0, mDataSize);
			memcpy_s(mpCompressedData + mFrontPad, mDataSize - mFrontPad, compressor.mpCompressedData, compressor.mCompressedSize);
		}
		else
		{
			mpCompressedData = std::move(compressor.mpCompressedData);
			mDataSize = compressor.mCompressedSize;
		}
	}

	bool PatchDataFromFile(const string& inFileName, bool bPad)
	{
		//Load bgnd image
		const FileNameContainer fileNameInfo(inFileName.c_str());
		FileData fileData;
		if( !fileData.InitializeFileData(fileNameInfo) )
		{
			return false;
		}

		//Compress bgnd image
		PatchDataInMemory(fileData.GetData(), fileData.GetDataSize(), bPad);

		return true;
	}
};

class SakuraTranslationTable
{
public:
	const unsigned short GetIndex(unsigned char inChar) const
	{
		/*
		if( inChar == '@' )
		{
			return 133;//'u' + 16;// + 1;//'…' + 1;
		}*/

		if( inChar == '\n' )
		{
			return 0x0a0d;
		}

		if( inChar == 0xA0 )
		{
			return ' ';// + 1;
		}

		return inChar;// + 1;
	}
};
const SakuraTranslationTable GTranslationLookupTable;

class SakuraStringLookUpValue
{
public:
	vector<short> mValuesForEachChar;

	SakuraStringLookUpValue(const string& inString)
	{
		for(string::const_iterator iter = inString.begin(); iter != inString.end(); ++iter)
		{
			if( *iter == '\n' )
			{
				continue;
			}

			mValuesForEachChar.push_back( GTranslationLookupTable.GetIndex(*iter) );
		}
	}
};

class SakuraFontTile
{
	char* mpData;

public:
	SakuraFontTile(const char* pInData, int dataSize)
	{
		mpData = new char[dataSize];
		memcpy(mpData, pInData, dataSize);
	}

	SakuraFontTile(SakuraFontTile&& other) : mpData(other.mpData)
	{
		other.mpData = nullptr;
	}

	SakuraFontTile& operator=(SakuraFontTile&& other)
	{
		if (this != &other)
		{
			delete[] mpData;
			mpData       = other.mpData;
			other.mpData = nullptr;
		}

		return *this;
	}

	~SakuraFontTile()
	{
		delete[] mpData;
		mpData = nullptr;
	}

	const char* GetFontTileData() const
	{
		return mpData;
	}
};

struct SakuraString
{
	struct SakuraChar
	{
		SakuraChar() : mRow(0), mColumn(0){}
		SakuraChar(unsigned char inRow, unsigned char inColumn) : mRow(inRow), mColumn(inColumn)
		{
			mIndex = (inRow << 8) + inColumn;
			assert(mIndex > 0);
		}

		SakuraChar(unsigned short inIndex)
		{
			mRow    = (inIndex & 0xff00) >> 8;
			mColumn = (inIndex & 0x00ff);
			mIndex  = inIndex;
		}

		bool IsNewLine() const
		{
			return mIndex == NewLine;
		}

		unsigned char    mRow;
		unsigned char    mColumn;
		unsigned short   mIndex;
		static const int NewLine = 0x0A0D;
	};

	vector<SakuraChar> mChars;
	static const int   MaxCharsPerLine     = 15;
	unsigned short     mOffsetToStringData = 0;

	bool AddChar(unsigned short index, bool bPushFront = false)
	{
		if( bPushFront )
		{
			mChars.insert( mChars.begin(), std::move(SakuraChar(index)) );
		}
		else
		{
			mChars.push_back( std::move(SakuraChar(index)) );
		}

		return true;
	}

	bool AddChar(char row, char column, bool bPushFront)
	{
		if( row == 0 && column == 0 )
		{
			return false; 
		}

		if( bPushFront )
		{
			mChars.insert( mChars.begin(), std::move(SakuraChar(row, column)) );
		}
		else
		{
			mChars.push_back( std::move(SakuraChar(row, column)) );
		}

		return true;
	}

	void AddString(const string& inString, unsigned short specialValue, unsigned short numCharsPrinted, bool bAddPadByte)
	{
		if( specialValue )
		{
			mChars.push_back( std::move(SakuraChar(specialValue)) );
			mChars.push_back( std::move(SakuraChar(numCharsPrinted)) );
		}
		else
		{
			mChars.push_back( std::move(SakuraChar(0)) );
			mChars.push_back( std::move(SakuraChar(0)) );
		}

		for(string::const_iterator iter = inString.begin(); iter != inString.end(); ++iter)
		{
			const unsigned short value = GTranslationLookupTable.GetIndex(*iter);
			mChars.push_back( std::move(SakuraChar(value)) );
		}

		if( bAddPadByte && mChars.size() % 2 != 0 )
		{
#if USE_SINGLE_BYTE_LOOKUPS
			mChars.push_back( std::move(SakuraChar(' ')) );
#endif
		}

		mChars.push_back( std::move(SakuraChar(0)) );

		mOffsetToStringData = 2;
	}

	int GetNumberOfLines() const
	{
		int numLines = 1;
		for(const SakuraChar& sakuraChar : mChars)
		{
			if( sakuraChar.mIndex == SakuraChar::NewLine )
			{
				++numLines;
			}
		}

		return numLines;
	}

	int GetNumberOfActualCharacters() const
	{
		const size_t charCount = mChars.size();
		int returnValue = 0;
		for(size_t i = 2; i < charCount; ++i)
		{
			if( mChars[i].mIndex && mChars[i].mIndex != SakuraChar::NewLine )
			{
				++returnValue;
			}
		}

		return returnValue;
	}

	int GetNumberOfPrintedCharacters() const
	{
		int numChars = 0;
		for(const SakuraChar& sakuraChar : mChars)
		{
			if( sakuraChar.mIndex != SakuraChar::NewLine )
			{
				++numChars;
			}
		}

		return numChars;
	}

	int GetMaxCharactersInLine() const
	{
		int maxCharacters = 0;
		int currCount     = 0;
		for(const SakuraChar& sakuraChar : mChars)
		{
			if( sakuraChar.mIndex == SakuraChar::NewLine )
			{
				if( currCount > maxCharacters )
				{
					maxCharacters = currCount;
				}

				currCount = 0;
			}
			else
			{
				++currCount;
			}			
		}

		if( currCount > maxCharacters )
		{
			maxCharacters = currCount;
		}

		return maxCharacters;
	}

	void GetDataArray(vector<unsigned short>& outData) const
	{
		outData.clear();

		for(const SakuraChar& sakuraChar : mChars)
		{
			//Change endianness so that it gets written in big endian order
			const short valueBE = ((sakuraChar.mIndex&0xff00) >> 8) + ((sakuraChar.mIndex & 0x00ff) << 8);

			outData.push_back(valueBE);
		}
	}

	unsigned short GetSingleByteDataSize() const
	{
		if( mChars.size() > 3 )
		{
			const unsigned short returnSize = (unsigned short)(mChars.size()) + 3;
			return returnSize;
		}
		
		return (unsigned short)mChars.size() * sizeof(short);
	}

	void GetSingleByteDataArray(vector<unsigned char>& outData) const
	{
		outData.clear();

		int count = 0;
		for(const SakuraChar& sakuraChar : mChars)
		{	
			if(count < 2)
			{
				//Change endianness so that it gets written in big endian order
				const char value1 = (char)((sakuraChar.mIndex & 0xff00) >> 8);// ((sakuraChar.mIndex & 0xff00) >> 8) + ((sakuraChar.mIndex & 0x00ff) << 8);
				const char value2 = (char)(sakuraChar.mIndex & 0x00ff);
				outData.push_back(value1);
				outData.push_back(value2);
			}
			else
			{
				const char value1 = (char)((sakuraChar.mIndex & 0xff00) >> 8);
				const char value2 = (char)(sakuraChar.mIndex & 0x00ff);

				//If both bytes are 0, then these are the end bytes and they need to be output
				if( value1 == 0 && value2 == 0 )
				{
					outData.push_back(value1);
				}

				outData.push_back(value2);
			}

			++count;
		}
	}

	bool IsVoicedLine() const
	{
		//Voiced line don't start with 0000
		if( mChars.size() > 0 && mChars[0].mIndex != 0 )
		{
			return true;
		}

		return false;
	}
};

class SakuraFontSheet
{
	vector<SakuraFontTile> mCharacterTiles;

public:
	bool CreateFontSheetFromData(const char* pInData, unsigned int inDataSize)
	{
		const int numBytesPerTile = 128;
		if( inDataSize % 128 != 0 )
		{
			printf("CreateFontSheet: Invalid size for data.  Should be multiple of 128, is %u", inDataSize);
			return false;
		}

		const int numTilesInFile = inDataSize / numBytesPerTile;
		for(int currTile = 0; currTile < numTilesInFile; ++currTile)
		{
			mCharacterTiles.push_back( std::move(SakuraFontTile(&pInData[currTile*numBytesPerTile], numBytesPerTile)) );
		}

		return true;
	}

	bool CreateFontSheet(const FileNameContainer& inFileNameInfo)
	{
		FileData fontFile;
		if( !fontFile.InitializeFileData(inFileNameInfo) )
		{
			return false;
		}

		const int numBytesPerTile = 128;
		if( fontFile.GetDataSize() % 128 != 0 )
		{
			printf("CreateFontSheet: Invalid size for file %s", inFileNameInfo.mFileName.c_str());
			return false;
		}

		const char* pFontSheetData = fontFile.GetData();
		const int numTilesInFile   = fontFile.GetDataSize() / numBytesPerTile;
		for(int currTile = 0; currTile < numTilesInFile; ++currTile)
		{
			mCharacterTiles.push_back( std::move(SakuraFontTile(&pFontSheetData[currTile*numBytesPerTile], numBytesPerTile)) );
		}

		return true;
	}

	const char* GetCharacterTile(const SakuraString::SakuraChar& sakuraChar) const
	{	
		int tileIndex = sakuraChar.mIndex - 1;
		assert(tileIndex >= 0);
		//assert(tileIndex < (int)mCharacterTiles.size());
		if( tileIndex >= (int)mCharacterTiles.size() )
		{
			tileIndex = 0;
		}

		return tileIndex < (int)mCharacterTiles.size() ? mCharacterTiles[tileIndex].GetFontTileData() : nullptr;
	}

	unsigned long GetTileSizeInBytes() const
	{
		return 128;
	}
};

struct SakuraTextFile
{
	struct SakuraDataSegment
	{
		char*  pData;
		size_t dataSize;

		SakuraDataSegment() : pData(nullptr), dataSize(0){}

		SakuraDataSegment(char* pInData, size_t inSize)
		{
			dataSize = inSize;
			pData    = new char[dataSize];

			memcpy(pData, pInData, dataSize);
		}

		SakuraDataSegment(SakuraDataSegment&& rhs) : pData(rhs.pData), dataSize(rhs.dataSize)
		{
			rhs.pData = nullptr;
		}

		SakuraDataSegment& operator=(SakuraDataSegment&& rhs)
		{
			if( this != &rhs )
			{
				delete[] pData;
				pData     = rhs.pData;
				dataSize  = rhs.dataSize;
				rhs.pData = nullptr;
			}

			return *this;
		}

		~SakuraDataSegment()
		{
			delete[] pData;
			pData = nullptr;
		}
	};

	struct SakuraStringInfo
	{
		unsigned int   mFullValue;
		unsigned short mStringId;
		unsigned short mOffsetFromPrevString;

		SakuraStringInfo(unsigned short inFirst, unsigned short inSecond) : mStringId(inFirst), mOffsetFromPrevString(inSecond)
		{
			mFullValue = (inFirst << 16) + inSecond;
		}
	};

	struct SakuraTimingData
	{
		struct SakuraCharTimingData
		{
			vector<unsigned char> mTimingBytes;

			size_t GetNumBytes_NoKeyValue() const
			{
				size_t result = 0;

				for(unsigned char value : mTimingBytes)
				{
					if( value != CharTimingStartID )
					{
						++result;
					}
				}

				return result;
			}
		};
		
		vector<SakuraCharTimingData> mTimingForLine;

		size_t GetNumBytesInLine() const
		{
			size_t count = 0;
			for(const SakuraCharTimingData& lineData : mTimingForLine)
			{
				count += lineData.mTimingBytes.size();
			}

			return count;
		}

		size_t GetNumBytesInLine_NoKeyValues() const
		{
			size_t count = 0;
			for(const SakuraCharTimingData& lineData : mTimingForLine)
			{
				count += lineData.GetNumBytes_NoKeyValue();
			}

			return count;
		}

		size_t GetNumPrefixBytes() const
		{
			size_t numPrefixBytes = 0;

			if( mTimingForLine.size() && mTimingForLine[0].mTimingBytes.size() )
			{
				if( mTimingForLine[0].mTimingBytes[0] != CharTimingStartID )
				{	
					while( numPrefixBytes < mTimingForLine[0].mTimingBytes.size() && mTimingForLine[0].mTimingBytes[numPrefixBytes] != CharTimingStartID )
					{
						++numPrefixBytes;
					}
				}
			}

			return numPrefixBytes;
		}

		char* GetByteStream() const
		{
			const size_t numBytes = GetNumBytesInLine();
			char* pByteStream     = new char[numBytes];
			size_t offset         = 0;
			for(const SakuraCharTimingData& lineData : mTimingForLine)
			{
				if( offset >= numBytes )
				{
					printf("SakuraTimingData::GetByteStream: ByteStream buffer is too small\n");
					return pByteStream;
				}

				memcpy_s(pByteStream + offset, numBytes, lineData.mTimingBytes.data(), lineData.mTimingBytes.size());
				offset += lineData.mTimingBytes.size();
			}
			return pByteStream;
		}
	};

	FileNameContainer         mFileNameInfo;
	vector<SakuraString>      mLines;
	SakuraDataSegment         mHeader;
	vector<SakuraDataSegment> mDataSegments;
	vector<SakuraStringInfo>  mStringInfoArray;
	vector<SakuraTimingData>  mLineTimingData;

private:
	unsigned long       mFileSize;
	FILE*               mpFile;
	char*               mpBuffer;

public:
	SakuraTextFile(const FileNameContainer& fileName) : mFileNameInfo(fileName), mFileSize(0), mpFile(nullptr), mpBuffer(nullptr){}

	SakuraTextFile(SakuraTextFile&& rhs) : mFileNameInfo(std::move(rhs.mFileNameInfo)), mLines(std::move(rhs.mLines)), mHeader(std::move(rhs.mHeader)),
		mDataSegments(std::move(rhs.mDataSegments)), mStringInfoArray(std::move(rhs.mStringInfoArray)), mFileSize(rhs.mFileSize),
		mpFile(rhs.mpFile), mpBuffer(std::move(rhs.mpBuffer)), mLineTimingData(std::move(rhs.mLineTimingData))
	{
		rhs.mpBuffer = nullptr;
		rhs.mpFile   = nullptr;
	}

	SakuraTextFile& operator=(SakuraTextFile&& rhs)
	{
		mFileNameInfo    = std::move(rhs.mFileNameInfo);
		mLines           = std::move(rhs.mLines);
		mDataSegments    = std::move(rhs.mDataSegments);
		mStringInfoArray = std::move(rhs.mStringInfoArray);
		mHeader          = std::move(rhs.mHeader);
		mLineTimingData  = std::move(rhs.mLineTimingData);
		mFileSize        = rhs.mFileSize;
		mpFile           = rhs.mpFile;
		mpBuffer         = std::move(rhs.mpBuffer);

		rhs.mpFile       = nullptr;
		rhs.mpBuffer     = nullptr;
	}

	~SakuraTextFile()
	{
		for(size_t i = 0; i < mDataSegments.size(); ++i)
		{
			delete[] mDataSegments[i].pData;

			mDataSegments[i].pData = nullptr;
		}
		mDataSegments.clear();

		Close();
	}

	bool OpenFile()
	{
		//Open file in read-binary mode
		mpFile = nullptr;
		errno_t errorValue = fopen_s(&mpFile, mFileNameInfo.mFullPath.c_str(), "rb");
		if (errorValue)
		{
			printf("Unable to open file: %s.  Error code: %i \n", mFileNameInfo.mFileName.c_str(), errorValue);
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
			printf("Unable to read all of file: %s.\n", mFileNameInfo.mFileName.c_str());

			Close();
			return false;
		}

		return true;
	}

	void ReadInText()
	{
		ParseHeader();
		ParseStrings();
		ParseFooter();

		assert( mStringInfoArray.size() == mLines.size() );
	}

	bool DoesIdExist(unsigned short inId) const
	{
		for(const SakuraStringInfo& info : mStringInfoArray)
		{
			if( info.mStringId == inId )
			{
				return true;
			}
		}

		return false;
	}

	void Close()
	{
		if( mpFile )
		{
			fclose(mpFile);
		}

		delete[] mpBuffer;

		mpBuffer = nullptr;
		mpFile   = nullptr;
	}

	unsigned long GetFileSize() const
	{
		return mFileSize;
	}

	int GetVoicedLineStartIndex() const
	{
		const size_t numLines = mLines.size();
		for(size_t i = 0; i < numLines; ++i)
		{
			if( mLines[i].IsVoicedLine() )
			{
				return i;
			}
		}

		return -1;
	}

private:
	void ParseHeader()
	{
		//First two bytes are the offset that will take us to the strings.  So 0-offset is the header.
		const unsigned short dataSize = SwapByteOrder( *((unsigned short*)mpBuffer) ) * 2;
		mHeader = std::move(SakuraDataSegment(mpBuffer, dataSize));

		assert(dataSize%4 == 0);

		const unsigned int* pDWordBuffer = (unsigned int*)(mpBuffer) + 1;
		const int numStrings = (dataSize/4 - 1); //First four bytes are just for offset info
		for(int i = 0; i < numStrings; ++i)
		{
			const unsigned int stringInfo = SwapByteOrder(pDWordBuffer[i]);
			const unsigned short first    = (stringInfo & 0xffff0000) >> 16;
			const unsigned short second   = (stringInfo & 0x0000ffff);

			mStringInfoArray.push_back( std::move(SakuraStringInfo(first, second)) );
		}
	}

	void ParseFooter()
	{
		//Second two bytes are the offset that will take us to the footer
		const unsigned int offsetToTimingData = SwapByteOrder( *((unsigned short*)(mpBuffer + 2)) ) * 2;
		char* pTimingData                     = mpBuffer + offsetToTimingData;
		int timingDataSize                    = mFileSize - offsetToTimingData;
		int index                             = 0;
		bool bSearchingForStart               = false;

		SakuraTimingData timingForLine;
		SakuraTimingData::SakuraCharTimingData timingForCharacter;
		
		const bool bIsMESFile       = mFileNameInfo.mFileName.find("MES.BIN", 0, 6) != string::npos;
		const size_t startLineIndex = bIsMESFile ? 0 : (size_t)GetVoicedLineStartIndex();
		size_t currentLineIndex     = startLineIndex;

		if( (int)currentLineIndex == -1 && timingDataSize )
		{
			printf("No voiced lines found in %s", mFileNameInfo.mFileName.c_str());
			return;
		}

		if( (int)currentLineIndex == -1 )
		{
			return;
		}

		//Todo: This is a temp fix for MES files
		/*
		if( startLineIndex > 0 )
		{
			mLineTimingData.resize(startLineIndex);
		}
		*/
		mLineTimingData.resize(mLines.size());

		do 
		{
			unsigned char timingChar = pTimingData[index];
			
			//If we found 00, then this is the end of the timing data for the line of text
			if( timingChar == 0 && 
				timingDataSize > 0  && 
				timingForCharacter.mTimingBytes.size()
				)
			//	pTimingData[index + 1] == 0 )
			{
				timingForLine.mTimingForLine.push_back(timingForCharacter);
				timingForCharacter.mTimingBytes.clear();

				//Make sure that the number of timing entries matches the number of characters in this line
				/*
				if( currentLineIndex < mLines.size() )
				{
					const size_t numCharsInLine = (size_t)mLines[currentLineIndex].GetNumberOfActualCharacters();
					if( timingForLine.mTimingForLine.size() != numCharsInLine )
					{
						printf("Number of characters in line[%u] doesn't match number of timing entries[%u] for line %u in %s\n", numCharsInLine, timingForLine.mTimingForLine.size(), currentLineIndex, mFileNameInfo.mFileName.c_str());
					}
				}*/

				const size_t numTimingChararactersForLine = timingForLine.GetNumBytesInLine();
				mLineTimingData[currentLineIndex] = timingForLine;//mLineTimingData.push_back(timingForLine);
				timingForLine.mTimingForLine.clear();
				
				++currentLineIndex;

				//Take into account padding bytes
				if( !bIsMESFile && numTimingChararactersForLine % 2 == 0 )
				{
					index += 2;
					timingDataSize -=2;
				}
				else
				{
					index += 1;
					timingDataSize -=1;
				}

				bSearchingForStart = false;

				if( timingDataSize <= 0 )
				{
					break;
				}

				if( bIsMESFile )
				{
					const size_t numLinesInFile = mLines.size();
					while( mLines[currentLineIndex].mChars[0].mIndex == 0 && currentLineIndex < numLinesInFile )
					{
						++currentLineIndex;
					}
				}
				continue;
			}
			else if( !bSearchingForStart && timingChar == CharTimingStartID && timingForCharacter.mTimingBytes.size() ) //When timing for next character is hit
			{
				timingForLine.mTimingForLine.push_back(timingForCharacter);
				timingForCharacter.mTimingBytes.clear();

				//Start off next entry
				timingForCharacter.mTimingBytes.push_back(timingChar);

				bSearchingForStart = false;
			}
			else
			{	
				//Some lines have data prefixing it
				if( timingForCharacter.mTimingBytes.size() == 0 && timingChar != CharTimingStartID )
				{
					bSearchingForStart = true;
				}
				else if( bSearchingForStart && timingChar == CharTimingStartID )
				{
					bSearchingForStart = false;
				}

				timingForCharacter.mTimingBytes.push_back(timingChar);
			}
			
			--timingDataSize;
			++index;
		} while (timingDataSize > 0);
	
		if( startLineIndex != (size_t)-1 && 
			(mLineTimingData.size() != (mLines.size() - startLineIndex)) )
		{
		//	printf("Line timing count[%u] doesn't match the number of lines[%u] in %s\n", mLineTimingData.size(), mLines.size(), mFileNameInfo.mFileName.c_str());
		}
	}

	void ParseStrings()
	{
		const size_t numStrings = mStringInfoArray.size();
		const bool bIsMESFile   = mFileNameInfo.mFileName.find("MES.BIN", 0, 6) != string::npos;

		for(size_t i = 0; i < numStrings; ++i)
		{
			const unsigned int offsetToString = mStringInfoArray[i].mOffsetFromPrevString*2 + mHeader.dataSize;
			assert(offsetToString < mFileSize);

			SakuraString newLineOfText;
			unsigned short* pWordBuffer = (unsigned short*)&mpBuffer[offsetToString];
			bool bNonZeroValueFound     = false;
			int currentIndex            = 0;
			unsigned short offsetToStringData = 0;

			//The dialog starting at 0xC531 has a special starting tag instead of the usual 00 00
			if( 1 )//mStringInfoArray[i].mStringId == SpecialDialogIndicator || bIsMESFile )//&& !bIsMESFile )
			{
				//First byte
				unsigned short currValue = pWordBuffer[currentIndex++];
				currValue                = SwapByteOrder(currValue);  //Convert to big endian
				newLineOfText.AddChar(currValue);

				//Second byte
				currValue = pWordBuffer[currentIndex++];
				currValue = SwapByteOrder(currValue);  //Convert to big endian
				newLineOfText.AddChar(currValue);

				//assert(currValue == 0);
			}

			while(1)
			{
				unsigned short currValue = pWordBuffer[currentIndex++];
				currValue                = SwapByteOrder(currValue);  //Convert to big endian

				if( currValue != 0 )
				{
					bNonZeroValueFound = true;
				}
				else if( currValue == 0 && !bNonZeroValueFound )
				{
					offsetToStringData++;
				}

				if( bIsMESFile )
				{
					if( currentIndex > 2 || (currentIndex == 1 && pWordBuffer[0] == 0) )
					{
						newLineOfText.AddChar(currValue);
					}
				}
				else
				{
					newLineOfText.AddChar(currValue);
				}

				if( currValue == 0 && bNonZeroValueFound )
				{
					break;
				}
			}

			//newLineOfText.mOffsetToStringData = !bIsMESFile && mStringInfoArray[i].mUnknown >= SpecialDialogIndicator ? 2 : offsetToStringData;
			newLineOfText.mOffsetToStringData = 2;//bIsMESFile || (mStringInfoArray[i].mStringId >= SpecialDialogIndicator) ? 2 : offsetToStringData;
			mLines.push_back(newLineOfText);
		}
	}
};

struct SakuraTextFileFixedHeader
{
	struct StringInfo
	{
		unsigned short stringId;
		unsigned short offsetFromTableStart;

		explicit StringInfo(unsigned short inStringId, unsigned short inOffsetFromTableStart) : stringId( SwapByteOrder(inStringId) ), offsetFromTableStart( SwapByteOrder(inOffsetFromTableStart) )
		{
		}
	};

#pragma pack(push, 2)
	struct StringInfo8Bytes
	{
		unsigned int stringId;
		unsigned int offsetFromTableStart;

		explicit StringInfo8Bytes(unsigned int inStringId, unsigned int inOffsetFromTableStart) : stringId( SwapByteOrder(inStringId) ), offsetFromTableStart( SwapByteOrder(inOffsetFromTableStart) )
		{
		}
	};
#pragma pack(pop)

	unsigned short           mOffsetToTable;
	unsigned int             mOffsetToTable8Bytes;
	unsigned short           mTableEnd;
	unsigned int             mTableEnd8Bytes;
	vector<StringInfo>       mStringInfo;
	vector<StringInfo8Bytes> mStringInfo8Bytes;

	bool CreateFixedHeader(const vector<SakuraTextFile::SakuraStringInfo>& inInfo, const SakuraTextFile& inSakuraFile, const vector<SakuraString>& inStrings, bool bIsMesFile)
	{
		//All TBL files start with this entries
		//mStringInfo.push_back( SwapByteOrder(inInfo[0].mFullValue) );
		mStringInfo.push_back( StringInfo(inInfo[0].mStringId, inInfo[0].mOffsetFromPrevString) );
		mStringInfo8Bytes.push_back( StringInfo8Bytes(inInfo[0].mStringId, inInfo[0].mOffsetFromPrevString) );

		int totalCharCount             = 0;
		unsigned short prevValue       = 0;
		unsigned int   prevValue8Bytes = 0; prevValue8Bytes = 0;
		const size_t numEntries        = inInfo.size() - 1;
		for(size_t i = 0; i < numEntries; ++i)
		{
#if USE_SINGLE_BYTE_LOOKUPS
			const unsigned short newOffset       = bIsMesFile ? (unsigned short)inStrings[i].mChars.size() + prevValue : (unsigned short)inStrings[i].mChars.size() + 3 + prevValue; //+ trailingZeros;
			const unsigned int   newOffset8bytes = inStrings[i].mChars.size() + 3 + prevValue8Bytes; //+ trailingZeros;
			totalCharCount                      += inStrings[i].mChars.size() + 3;
			prevValue8Bytes                      = newOffset8bytes;
#else
			const unsigned short newOffset = (unsigned short)inStrings[i].mChars.size() + prevValue; //+ trailingZeros;
			totalCharCount                += (unsigned short)inStrings[i].mChars.size();
#endif
			prevValue       = newOffset;
			mStringInfo.push_back( StringInfo(inInfo[i + 1].mStringId, newOffset) );

#if USE_4_BYTE_OFFSETS
			mStringInfo8Bytes.push_back( StringInfo8Bytes(inInfo[i + 1].mStringId, newOffset8bytes) );
#endif
		}

		const bool bCheckTotalCharError = !USE_4_BYTE_OFFSETS || bIsMesFile;
		if( bCheckTotalCharError && totalCharCount > 0xffff )
		{
			printf("\nERROR:Translated file %s exceeds max char count. Max is %u.  File has %i\n", inSakuraFile.mFileNameInfo.mFileName.c_str(), 0xffff, totalCharCount);
		}

		//Figure out text table size
		unsigned long stringTableSize = 0;
		for(const SakuraString& sakuraString : inStrings)
		{
			const bool bUseSingleByteLookups = !bIsMesFile && USE_SINGLE_BYTE_LOOKUPS;
			if( bUseSingleByteLookups )
			{
				stringTableSize += sakuraString.GetSingleByteDataSize();
			}
			else
			{
				stringTableSize += sakuraString.mChars.size()*2;
			}
		}

		if( stringTableSize % 2 != 0 )
		{
			stringTableSize++;
		}
		//Done figuring out table size

		if( (unsigned short)( sizeof(int) + sizeof(int) + sizeof(int)*inInfo.size() + sizeof(int)*inInfo.size() ) >> 1 > 0xffff )
		{
			printf("\nERROR:Translated file %s string table exceeds 2 byte limit\n", inSakuraFile.mFileNameInfo.mFileName.c_str());
			return false;
		}

		mOffsetToTable       = SwapByteOrder( ((unsigned short*)inSakuraFile.mHeader.pData)[0] );
		mOffsetToTable8Bytes = ( sizeof(int) + sizeof(int) + sizeof(int)*inInfo.size() + sizeof(int)*inInfo.size() ) >> 1;

		unsigned long timingDataL      = ((mOffsetToTable<<1) + stringTableSize) >> 1;
		unsigned long timingData8Bytes = ((mOffsetToTable8Bytes<<1) + stringTableSize) >> 1;
		if(timingDataL/2 > 0xffff)
		{
			if( bCheckTotalCharError )
			{
				const int sizeToReduce = (timingDataL/2) - 0xffff;
				printf("\nERROR:Translated file %s is too big. Timing data is too far down.  Needs to be reduced by %i bytes (%i characters)\n", inSakuraFile.mFileNameInfo.mFileName.c_str(), sizeToReduce, sizeToReduce/2);
				return false;
			}
		}
		mTableEnd       = (unsigned short)timingDataL;
		mTableEnd8Bytes = timingData8Bytes;

		if(stringTableSize/2 > 0xffff)
		{
			if( bCheckTotalCharError )
			{
				const int sizeToReduce = (stringTableSize/2) - 0xffff;
				printf("\nERROR:Translated file %s is too big.  Needs to be reduced by %i bytes (%i characters)\n", inSakuraFile.mFileNameInfo.mFileName.c_str(), sizeToReduce, sizeToReduce);
				return false;
			}
		}

		return true;
	}
};

void FindAllSakuraText(const vector<FileNameContainer>& inFiles, vector<SakuraTextFile>& outText)
{
	for(const FileNameContainer& fileName : inFiles)
	{
		printf("Extracting: %s\n", fileName.mFileName.c_str());

		SakuraTextFile sakuraFile(fileName);
		if( !sakuraFile.OpenFile() )
		{
			continue;
		}

		if( sakuraFile.GetFileSize() == 0 )
		{
			continue;
		}

		sakuraFile.ReadInText();

		outText.push_back( std::move(sakuraFile) );
	}
}

const FileNameContainer* FindSakuraFile(const vector<FileNameContainer>& allFiles, const char* pFileName)
{
	const string searchName(pFileName);

	for(const FileNameContainer& fileNameInfo : allFiles)
	{
		if( fileNameInfo.mFileName == searchName )
		{
			return &fileNameInfo;
		}
	}

	return nullptr;
}

void DumpExtractedSakuraText(const vector<SakuraTextFile>& inSakuraTextFiles, const string& outDirectory)
{
	const string txt(".txt");
	const string info("_StringInfo");

	int fileNum = 0;
	for(const SakuraTextFile& textFile : inSakuraTextFiles)
	{
		const string outFileName = outDirectory + textFile.mFileNameInfo.mNoExtension + txt;
		const string infoFileName = outDirectory + textFile.mFileNameInfo.mNoExtension + info + txt;

		FILE* pOutFile     = nullptr;
		errno_t errorValue = fopen_s(&pOutFile, outFileName.c_str(), "w");
		if (errorValue)
		{
			printf("Unable to open out file: %s.  Error code: %i \n", outFileName.c_str(), errorValue);
			continue;
		}

		FILE* pOutInfoFile = nullptr;
		errorValue = fopen_s(&pOutInfoFile, infoFileName.c_str(), "w");
		if (errorValue)
		{
			printf("Unable to open out info file: %s.  Error code: %i \n", outFileName.c_str(), errorValue);
			continue;
		}

		printf("Dumping text for: %s\n", textFile.mFileNameInfo.mFileName.c_str());
		int lineNum = 0;
		for(const SakuraString& sakuraString : textFile.mLines)
		{
			fprintf(pOutFile, "Len: %i. Data: ", sakuraString.mChars.size());

			for(const SakuraString::SakuraChar& sakuraChar : sakuraString.mChars)
			{
				fprintf(pOutFile, "%02x%02x ", sakuraChar.mRow, sakuraChar.mColumn);
			}

			++lineNum;
			fprintf(pOutFile, "\n");
		}

		for(size_t i = 0; i < textFile.mStringInfoArray.size(); ++i)
		{
			const SakuraTextFile::SakuraStringInfo& stringInfo = textFile.mStringInfoArray[i];
			const int diffFromPrev = i == 0 ? 0 : stringInfo.mOffsetFromPrevString - textFile.mStringInfoArray[i-1].mOffsetFromPrevString;
			fprintf(pOutInfoFile, "%02x %02x %i DiffFromPrev: %i\n", stringInfo.mStringId, stringInfo.mOffsetFromPrevString, stringInfo.mOffsetFromPrevString, diffFromPrev);
		}

		fclose(pOutFile);
		fclose(pOutInfoFile);
		++fileNum;
	}
}

bool ExtractImageFromData(const char* pInColorData, const unsigned int inColorDataSize, const string& outFileName, const char* pInPaletteData, const unsigned int inPaletteDataSize, const int inTextureDimX, const int inTextureDimY, 
	const int inNumTexturesPerRow, const int inNumColors = 256, const int inDataOffset = 0, bool bInFillEmptyData = true, bool bForceBitmapFormat = false, 
	PaletteData* pInPreMadePaletteData = nullptr)
{
	const int divisor            = inPaletteDataSize == 32 ? 2 : 1; //4bit images only have half the pixels
	const int tileDimX           = inTextureDimX;
	const int tileDimY           = inTextureDimY;
	const int tileBytes          = (tileDimX*tileDimY)/divisor; //4bits per pixel, so only half the amount of bytes as pixels
	const int tilesPerRow        = inNumTexturesPerRow;
	const int bytesPerTile       = tileBytes;
	const int bytesPerTileRow    = bytesPerTile*tilesPerRow;
	const unsigned long dataSize = bInFillEmptyData ? inColorDataSize - inDataOffset : (inTextureDimX*inTextureDimY*inNumTexturesPerRow)/divisor;
	const int numRows            = (int)ceil( dataSize/ (float)bytesPerTileRow);
	const int numColumns         = numRows > 0 ? tilesPerRow : dataSize/bytesPerTileRow;
	const int imageHeight        = numRows*tileDimY;
	const int imageWidth         = numColumns*tileDimX;

	//Create 32bit palette from the 16 bit(5:5:5 bgr) palette in SakuraTaisen
	PaletteData paletteData;
	PaletteData* pPaletteData = pInPreMadePaletteData ? pInPreMadePaletteData : &paletteData;
	if(!pInPreMadePaletteData)
	{
		paletteData.CreateFrom15BitData(pInPaletteData, inPaletteDataSize);
	}

	//Allocate space for tiled data
	int numTiles                     = dataSize/tileBytes;
	int currTileRow                  = 0;
	int currTileCol                  = 0;
	const int bytesInEachTilesWidth  = tileDimX/divisor;
	const int numTiledBytes          = (numRows*bytesPerTileRow);// + numColumns*bytesInEachTilesWidth;
	char* pOutTiledData              = new char[numTiledBytes];
	const int bytesPerHorizontalLine = bytesInEachTilesWidth*numColumns;

	memset(pOutTiledData, 0, numTiledBytes);

	//In Mode 3, the image only has 128 colors so only the 7 lower bits are used. 0x7f = 01111111
	const unsigned char bitMask = inNumColors == 128 ? 0x7f : 0xff;

	//Fill in data
	for(int tileIndex = 0; tileIndex < numTiles; ++tileIndex)
	{
		const char* pTile       = pInColorData + inDataOffset + tileIndex*tileBytes;
		const int outTileOffset = currTileRow*bytesPerHorizontalLine*tileDimY + currTileCol*bytesInEachTilesWidth;
		char* pOutTile          = pOutTiledData + outTileOffset;
		int tilePixel           = 0;
		for(int r = 0; r < tileDimY; ++r)
		{
			for(int c = 0; c < bytesInEachTilesWidth; ++c)
			{
				assert(outTileOffset + r*bytesPerHorizontalLine + c < numTiledBytes);
				pOutTile[r*bytesPerHorizontalLine + c] = pTile[tilePixel++] & bitMask;
			}
		}

		if( ++currTileCol >= numColumns )
		{
			currTileCol = 0;
			++currTileRow;
		}
	}

	BitmapWriter fontBitmap;
	fontBitmap.CreateBitmap(outFileName, imageWidth, -imageHeight, inPaletteDataSize == 32 ? 4 : 8, pOutTiledData, numTiledBytes, pPaletteData->GetData(), pPaletteData->GetSize(), bForceBitmapFormat);

	delete[] pOutTiledData;

	return true;
}

bool ExtractImage(const FileNameContainer& inFileNameContainer, const string& outFileName, const FileData& inPaletteFile, const int inTextureDimX, const int inTextureDimY, const int inNumTexturesPerRow, const int inNumColors = 256,
	const int inDataOffset = 0, bool bInFillEmptyData = true, bool bForceBitmap = false)
{
	FileData fontSheet;
	if( !fontSheet.InitializeFileData(inFileNameContainer) )
	{
		return false;
	}

	printf("Extracting: %s\n", inFileNameContainer.mFileName.c_str());

	return ExtractImageFromData(fontSheet.GetData(), fontSheet.GetDataSize(), outFileName, inPaletteFile.GetData(), inPaletteFile.GetDataSize(), inTextureDimX, inTextureDimY, inNumTexturesPerRow, 
		                        inNumColors, inDataOffset, bInFillEmptyData, bForceBitmap);
}

bool ExtractFontSheetAsBitmap(const FileNameContainer& inFileNameContainer, const string& outFileName, const FileData& inPaletteFile)
{
//	return ExtractImage(inFileNameContainer, outFileName, inPaletteFile, 8, 12, 255, 16, 0, false, true);
	return ExtractImage(inFileNameContainer, outFileName, inPaletteFile, 16, 16, 255);
}

void ExtractAllFontSheets(const vector<FileNameContainer>& inAllFiles, const string& inPaletteFileName, const string& outDir)
{
	FileData paletteFile;
	if( !paletteFile.InitializeFileData(inPaletteFileName.c_str(), inPaletteFileName.c_str()) )
	{
		return;
	}

	vector<FileNameContainer> textFiles;
	GetAllFilesOfType(inAllFiles, "KNJ.BIN", textFiles);
	GetAllFilesOfType(inAllFiles, "MES.FNT", textFiles);

	const string bmpExtension(".bmp");
	string outFileName;
	for(const FileNameContainer& fileNameInfo : textFiles)
	{
		outFileName = outDir + fileNameInfo.mNoExtension + bmpExtension;
		ExtractFontSheetAsBitmap(fileNameInfo, outFileName, paletteFile);
	}
}

void ExtractImages(const string& inFileName, const string& inPaletteFileName, int width, int height, const string& outDir)
{
	FileData paletteFile;
	if( !paletteFile.InitializeFileData(inPaletteFileName.c_str(), inPaletteFileName.c_str()) )
	{
		return;
	}

	FileNameContainer inputFileNameContainer(inFileName.c_str());

	const string bmpExtension(".bmp");
	const string outFileName= outDir + inputFileNameContainer.mNoExtension + bmpExtension;
	ExtractImage(inputFileNameContainer, outFileName, paletteFile, width, height, 1);
}

void DumpSakuraText(const vector<FileNameContainer>& inAllFiles, const string& inOutputDir)
{
	vector<FileNameContainer> textFiles;
	GetAllFilesOfType(inAllFiles, "TBL.BIN", textFiles);
	GetAllFilesOfType(inAllFiles, "MES.BIN", textFiles);

	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(textFiles, sakuraTextFiles);

	DumpExtractedSakuraText(sakuraTextFiles, inOutputDir);
}

struct MiniGameSakuraText
{
	//Read in Sakura formated string (each character is a 2 byte lookup into the font sheet)
	vector<SakuraString> mStrings;

	void ReadInStrings(const char* pFileData, unsigned int offsetToTextData, unsigned int numCharsInFontSheet)
	{
		unsigned short readValue    = 0;
		unsigned int   offset       = offsetToTextData;
		const int stride            = sizeof(unsigned short);
		SakuraString* pSakuraString = new SakuraString();
		while(1)
		{	
			const char* pDataPointer = pFileData + offset;

			//Convert to big endian
			readValue = SwapByteOrder( *((unsigned short*)(pDataPointer)) );

			//If we get a character that is beyond the font sheet, we are reading past the character data block
			if( readValue > numCharsInFontSheet && readValue != 0x0a0d )
			{
				break;
			}

			//If the read value is 0, it is the end of the string
			if( readValue == 0 )
			{
				const char* pNextEntry = pDataPointer + stride;

				mStrings.push_back(*pSakuraString);
				delete pSakuraString;
				pSakuraString = new SakuraString();

				//If the next value is also 0, this is the end of the text data
				const unsigned short nextValue = *((unsigned short*)(pNextEntry));
				if( nextValue == 0 )
				{
					break;
				}
			}
			else
			{
				pSakuraString->AddChar(readValue);
			}

			offset += stride;
		}

		delete pSakuraString;
	}

	void DumpTextImages(const SakuraFontSheet& inFontSheet, const PaletteData& inPaletteData, const string& outputDirectory)
	{
		const string pngDir = outputDirectory + "\\png\\";
		const string bmpDir = outputDirectory + "\\bmp\\";
		CreateDirectoryHelper(outputDirectory);
		CreateDirectoryHelper(pngDir);
		CreateDirectoryHelper(bmpDir);

		const string extension(".bmp");

		//Dump out the dialog for each line
		int stringIndex   = 0;
		const int tileDim = 16;
		for(size_t lineIndex = 0; lineIndex < mStrings.size(); ++lineIndex)
		{	
			const SakuraString& sakuraString = mStrings[lineIndex];
			if( sakuraString.mChars.size() > 255 )
			{
				continue;
			}

			const int numSakuraLines = sakuraString.GetNumberOfLines();

			BitmapSurface sakuraStringBmp;
			sakuraStringBmp.CreateSurface( SakuraString::MaxCharsPerLine*tileDim, tileDim*numSakuraLines, BitmapSurface::EBitsPerPixel::kBPP_4, inPaletteData.GetData(), inPaletteData.GetSize());

			int currRow = 0;
			int currCol = 0;
			for(size_t charIndex = sakuraString.mOffsetToStringData; charIndex < sakuraString.mChars.size(); ++charIndex)
			{
				const SakuraString::SakuraChar& sakuraChar = sakuraString.mChars[charIndex];

				if( sakuraChar.IsNewLine() )
				{
					++currRow;
					currCol = 0;
					continue;
				}

				if( sakuraChar.mIndex == 0 || sakuraChar.mIndex > 255*4 )
				{
					continue;
				}

				const char* pData = inFontSheet.GetCharacterTile(sakuraChar);

				sakuraStringBmp.AddTile(pData, inFontSheet.GetTileSizeInBytes(), currCol*16, currRow*16, tileDim, tileDim);

				++currCol;
			}

			const string pngName    = pngDir + std::to_string(stringIndex + 1) + extension;
			const string bitmapName = bmpDir + std::to_string(stringIndex + 1) + extension;
			sakuraStringBmp.WriteToFile(pngName, false);
			sakuraStringBmp.WriteToFile(bitmapName, true);

			++stringIndex;
		}
	}
};

void ExtractMinigameData(const string& rootSakuraDirectory, const string& translatedDataDirectory, const string& outputDirectory)
{
	//Create temp work directory
	string tempDir;
	if( !CreateTemporaryDirectory(tempDir) )
	{
		printf("Could not create temp work directory.  Error: (%d)\n", GetLastError());
		return;
	}

	CreateDirectoryHelper(outputDirectory);

	struct MiniGameDumper
	{
		string              mRootSakuraDirectory;
		string              mOutputDirectory;
		FileData            mLogoPaletteFile;
		PaletteData         mFontPaletteData;
		const int           mFontPaletteSize = 32;
		const unsigned char mRawPaletteData[32] = { (UCHAR)0x7f, (UCHAR)0xff, (UCHAR)0x08, (UCHAR)0x42, (UCHAR)0x10, (UCHAR)0x84, (UCHAR)0x18, (UCHAR)0xc6, (UCHAR)0x21, (UCHAR)0x08, (UCHAR)0x29, (UCHAR)0x4a, (UCHAR)0x31, (UCHAR)0x8c,
													(UCHAR)0x39, (UCHAR)0xce, (UCHAR)0x42, (UCHAR)0x10, (UCHAR)0x4a, (UCHAR)0x52, (UCHAR)0x52, (UCHAR)0x94, (UCHAR)0x5a, (UCHAR)0xd6, (UCHAR)0x63, (UCHAR)0x18, (UCHAR)0x6b, (UCHAR)0x5a,
													(UCHAR)0x73, (UCHAR)0x9c, (UCHAR)0x7f, (UCHAR)0xff };

		bool Initialize(const string& rootSakuraDirectory, const string& outputDirectory, const string& translatedDataDirectory)
		{	
			mRootSakuraDirectory = rootSakuraDirectory;
			mOutputDirectory     = outputDirectory;

			//Create 32bit palette data
			mFontPaletteData.CreateFrom15BitData((const char*)mRawPaletteData, mFontPaletteSize);

			//Create 32bit palette data
			FileNameContainer logoPaletteFileName((translatedDataDirectory + "MiniGameLogoPalette.bin").c_str());
			if (!mLogoPaletteFile.InitializeFileData(logoPaletteFileName))
			{
				printf("Unable to open MiniGameLogoPalette.bin\n");
				return false;
			}

			return true;
		}

		bool Dump(const char* inMiniGameFileName, int inNumCharactersInFontSheet, unsigned int logoWidth, unsigned int inFontSheetAddress, unsigned int textDataAddress, 
				  bool bIsCompressed = false, const char* pSubDirectory = nullptr)
		{
			const string sakura3Directory = mRootSakuraDirectory + "\\SAKURA3\\";
			FileNameContainer miniGameFileName((sakura3Directory + inMiniGameFileName + ".bin").c_str());
			FileData miniGameFile;
			if (!miniGameFile.InitializeFileData(miniGameFileName))
			{
				printf("Unable to open %s", miniGameFileName.mFullPath.c_str());
				return false;
			}

			//Create font sheet
			const int numBytesPerCharacter = 16 * 16 / 2;
			SakuraFontSheet sakuraFontSheet;
			if( bIsCompressed )
			{
				PRSDecompressor decompressor;
				decompressor.UncompressData((void*)(miniGameFile.GetData() + inFontSheetAddress), (miniGameFile.GetDataSize() - inFontSheetAddress));

				if( !sakuraFontSheet.CreateFontSheetFromData(decompressor.mpUncompressedData, decompressor.mUncompressedDataSize) )
				{
					return false;
				}
			}
			else
			{
				if( !sakuraFontSheet.CreateFontSheetFromData((miniGameFile.GetData() + inFontSheetAddress), numBytesPerCharacter*inNumCharactersInFontSheet) )
				{
					return false;
				}
			}

			string miniGameOutputDirectory = mOutputDirectory + Seperators + inMiniGameFileName + Seperators;
			if( pSubDirectory )
			{
				miniGameOutputDirectory += string(pSubDirectory) + Seperators;
			}

			//Read in sakura text
			MiniGameSakuraText sakuraText;
			sakuraText.ReadInStrings(miniGameFile.GetData(), textDataAddress, inNumCharactersInFontSheet);
			sakuraText.DumpTextImages(sakuraFontSheet, mFontPaletteData, miniGameOutputDirectory);

			if( !bIsCompressed && !pSubDirectory )
			{
				//Dump Logo
				ExtractImageFromData(miniGameFile.GetData() + inFontSheetAddress + inNumCharactersInFontSheet * numBytesPerCharacter, logoWidth * 24 / 2, miniGameOutputDirectory + "bmp\\Logo.bmp",
					mLogoPaletteFile.GetData(), mLogoPaletteFile.GetDataSize(), logoWidth, 24, 1, 256, 0, true, true);

				ExtractImageFromData(miniGameFile.GetData() + inFontSheetAddress + inNumCharactersInFontSheet * numBytesPerCharacter, logoWidth * 24 / 2, miniGameOutputDirectory + "png\\Logo.png",
					mLogoPaletteFile.GetData(), mLogoPaletteFile.GetDataSize(), logoWidth, 24, 1, 256, 0, true, false);
			}

			return true;
		}

		bool DumpRankings(const char* inMiniGameFileName, unsigned int fontSheetAddress, unsigned int inLogoAddress)
		{
			string miniGameOutputDirectory = mOutputDirectory + Seperators + inMiniGameFileName + Seperators;

			const string sakura3Directory = mRootSakuraDirectory + "\\SAKURA3\\";
			FileNameContainer miniGameFileName((sakura3Directory + inMiniGameFileName + ".bin").c_str());
			FileData miniGameFile;
			if (!miniGameFile.InitializeFileData(miniGameFileName))
			{
				printf("Unable to open %s", miniGameFileName.mFullPath.c_str());
				return false;
			}

			ExtractImageFromData(miniGameFile.GetData() + fontSheetAddress, (32 * 48) / 2, miniGameOutputDirectory + "bmp\\RankingText.bmp",
				mLogoPaletteFile.GetData(), mLogoPaletteFile.GetDataSize(), 32, 48, 1, 256, 0, true, true);

			ExtractImageFromData(miniGameFile.GetData() + fontSheetAddress, (32 * 48) / 2, miniGameOutputDirectory + "bmp\\RankingText.png",
				mLogoPaletteFile.GetData(), mLogoPaletteFile.GetDataSize(), 32, 48, 1, 256, 0, true, false);

			//Dump Logo
			ExtractImageFromData(miniGameFile.GetData() + inLogoAddress, 80 * 24 / 2, miniGameOutputDirectory + "bmp\\RankingLogo.bmp",
				mLogoPaletteFile.GetData(), mLogoPaletteFile.GetDataSize(), 80, 24, 1, 256, 0, true, true);

			ExtractImageFromData(miniGameFile.GetData() + inLogoAddress, 80 * 24 / 2, miniGameOutputDirectory + "png\\RankingLogo.png",
				mLogoPaletteFile.GetData(), mLogoPaletteFile.GetDataSize(), 80, 24, 1, 256, 0, true, false);

			return true;
		}
	};


	MiniGameDumper miniGameDumper;
	miniGameDumper.Initialize(rootSakuraDirectory, outputDirectory, translatedDataDirectory);
	miniGameDumper.Dump("MINICOOK", 42,  128, 0x00093568, 0x000aece4);
	miniGameDumper.Dump("MINICOOK", 191, 128, 0x000a3b80, 0x000a60c0, true, "Compressed");
	miniGameDumper.DumpRankings("MINICOOK", 0x00012bbc, 0x0001343c);

	miniGameDumper.Dump("MINIMAIG", 175, 128, 0x0005ecd4, 0x00061130, true, "Compressed");
	miniGameDumper.Dump("MINIMAIG", 43,  128, 0x000504B4, 0x00061e90);

	miniGameDumper.Dump("MINISWIM", 59,  128, 0x0003b1a8, 0x00056b28);
	miniGameDumper.Dump("MINISHOT", 43,  128, 0x00055510, 0x00080D94);
	miniGameDumper.Dump("MINISLOT", 37,  128, 0x0006149C, 0x0007e650);
	miniGameDumper.Dump("MINISOJI", 35,  144, 0x00064810, 0x00074cbc);

	miniGameDumper.Dump("MINIHANA", 111, 128, 0x00092210, 0x000a6b84);
	//miniGameDumper.Dump("MINIHANA", 230, 0x000a8178, 0x000a6b82, true, "Section1"); //Compressed data for font sheet found: 000a8178 00000000 Size: 384 CompressedSize: 12625
	miniGameDumper.Dump("MINIHANA", 230, 128, 0x000a8178, 0x000a6f1c, true, "Section2"); //Compressed data for font sheet found: 000a8178 00000000 Size: 384 CompressedSize: 12625
}

void ExtractStatusScreen(const string& rootSakuraDirectory, const string& translatedDataDirectory, const string& outputDirectory)
{
//	miniGameDumper.Dump("ICATALL.DAT", 48, 0, 0x00013800, 0x0005f428, true, "Section2"); //Compressed data for font sheet found: 000a8178 00000000 Size: 384 CompressedSize: 12625

	CreateDirectoryHelper(outputDirectory);
	CreateDirectoryHelper( (outputDirectory + "png") );
	CreateDirectoryHelper( (outputDirectory + "bmp") );

	const string icatallFilePath = rootSakuraDirectory + "SAKURA1\\ICATALL.DAT";
	FileData icatallFileData;
	if( !icatallFileData.InitializeFileData(FileNameContainer(icatallFilePath.c_str())) )
	{
		return;
	}

	//Dump images
	{
		const string fontFilePath = translatedDataDirectory + "StatusScreenPalette.bin";
		FileData paletteFile;
		if( !paletteFile.InitializeFileData( FileNameContainer(fontFilePath.c_str()) ) )
		{
			return;
		}

		PRSDecompressor uncompressedData;
		uncompressedData.UncompressData( (void*)(icatallFileData.GetData() + 0x00008800), icatallFileData.GetDataSize() - 0x00008800);

		ExtractImageFromData(uncompressedData.mpUncompressedData, 144*384, outputDirectory + "bmp\\Images.bmp",
					paletteFile.GetData(), paletteFile.GetDataSize(), 144, 384, 1, 256, 0, true, true);

		ExtractImageFromData(uncompressedData.mpUncompressedData, 144*384, outputDirectory + "png\\Images.png",
					paletteFile.GetData(), paletteFile.GetDataSize(), 144, 384, 1, 256, 0, true, false);
	}

	//Dump text
	{
		const string sakuraFilePath = rootSakuraDirectory + "SAKURA";
		FileData sakuraFileData;
		if( !sakuraFileData.InitializeFileData(FileNameContainer(sakuraFilePath.c_str())) )
		{
			return;
		}

		//Create font sheet
		SakuraFontSheet sakuraFontSheet;
		PRSDecompressor decompressor;
		decompressor.UncompressData((void*)(icatallFileData.GetData() + 0x00013800), (icatallFileData.GetDataSize() - 0x00013800));

		if( !sakuraFontSheet.CreateFontSheetFromData(decompressor.mpUncompressedData, decompressor.mUncompressedDataSize) )
		{
			printf("Unable to create font sheet.\n");
			return;
		}

		const string fontFilePath = translatedDataDirectory + "Palette.bin";
		FileData fontFile;
		if( !fontFile.InitializeFileData(FileNameContainer(fontFilePath.c_str()) ) )
		{
			return;
		}

		PaletteData fontPaletteData;
		fontPaletteData.CreateFrom15BitData(fontFile.GetData(), fontFile.GetDataSize());

		MiniGameSakuraText sakuraText;
		sakuraText.ReadInStrings(sakuraFileData.GetData(), 0x0005f428, 48);
		sakuraText.DumpTextImages(sakuraFontSheet, fontPaletteData, outputDirectory);
	}
}

void ExtractText(const string& inSearchDirectory, const string& inPaletteFileName, const string& inOutputDirectory)
{
	//Get the palette
	FileData paletteFile;
	if( !paletteFile.InitializeFileData(inPaletteFileName.c_str(), inPaletteFileName.c_str()) )
	{
		return;
	}

	//Create 32bit palette data
	PaletteData paletteData;
	paletteData.CreateFrom15BitData(paletteFile.GetData(), paletteFile.GetDataSize());

	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inSearchDirectory, allFiles);

	//Find all the font sheet files
	vector<FileNameContainer> fontFiles;
	GetAllFilesOfType(allFiles, "KNJ.BIN", fontFiles);
	GetAllFilesOfType(allFiles, "MES.FNT", fontFiles);

	//Find all the text files
	vector<FileNameContainer> textFiles;
	GetAllFilesOfType(allFiles, "TBL.BIN", textFiles);
	GetAllFilesOfType(allFiles, "MES.BIN", textFiles);

	//There needs to be a font sheet for every text file 
	if( textFiles.size() > fontFiles.size() )
	{
		printf("ExtractText: There need to be the same amount of sakura text files as font sheets");

		return;
	}

	//Extract the text
	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(textFiles, sakuraTextFiles);	

	const string extension(".bmp");

	//Write out bitmaps for all of the lines found in the sakura text files
	const size_t numFiles = sakuraTextFiles.size();
	for(size_t i = 0; i < numFiles; ++i)
	{
		const FileNameContainer& fontSheetName = fontFiles[i];
		const SakuraTextFile& sakuraText       = sakuraTextFiles[i];

		const string fontSheetNumber = fontSheetName.mNoExtension.substr(0, fontSheetName.mNoExtension.size() - 3);
		const string sakuraFileNum   = sakuraText.mFileNameInfo.mNoExtension.substr(0, sakuraText.mFileNameInfo.mNoExtension.size() - 3);

		if( sakuraFileNum != fontSheetNumber )
		{
			printf("ExtractText: Font sheet and text file mistmatch. %s %s", fontSheetName.mNoExtension.c_str(), sakuraText.mFileNameInfo.mNoExtension.c_str());
			return;
		}

		//Create output directory for this file
		string fileOutputDir = inOutputDirectory + sakuraText.mFileNameInfo.mNoExtension + string("\\");
		if( !CreateDirectoryHelper(fileOutputDir) )
		{
			continue;
		}

		printf("Dumping dialog for: %s\n", sakuraText.mFileNameInfo.mNoExtension.c_str());

		//Create font sheet
		SakuraFontSheet sakuraFontSheet;
		if( !sakuraFontSheet.CreateFontSheet(fontSheetName) )
		{
			continue;
		}

		//Dump out the dialog for each line
		int stringIndex   = 0;
		const int tileDim = 16;
		for(size_t lineIndex = 0; lineIndex < sakuraText.mLines.size(); ++lineIndex)
		{	
			const SakuraString& sakuraString = sakuraText.mLines[lineIndex];
			if( sakuraString.mChars.size() > 255 )
			{
				continue;
			}

			const int numSakuraLines = sakuraString.GetNumberOfLines();

			BitmapSurface sakuraStringBmp;
			sakuraStringBmp.CreateSurface( SakuraString::MaxCharsPerLine*tileDim, tileDim*numSakuraLines, BitmapSurface::EBitsPerPixel::kBPP_4, paletteData.GetData(), paletteData.GetSize());

			int currRow = 0;
			int currCol = 0;
			for(size_t charIndex = sakuraString.mOffsetToStringData; charIndex < sakuraString.mChars.size(); ++charIndex)
			{
				const SakuraString::SakuraChar& sakuraChar = sakuraString.mChars[charIndex];

				if( sakuraChar.IsNewLine() )
				{
					++currRow;
					currCol = 0;
					continue;
				}

				if( sakuraChar.mIndex == 0 || sakuraChar.mIndex > 255*4 )
				{
					continue;
				}

				const char* pData = sakuraFontSheet.GetCharacterTile(sakuraChar);

				sakuraStringBmp.AddTile(pData, sakuraFontSheet.GetTileSizeInBytes(), currCol*16, currRow*16, tileDim, tileDim);

				++currCol;
			}

			const string bitmapName = fileOutputDir + std::to_string(stringIndex + 1) + extension;
			sakuraStringBmp.WriteToFile(bitmapName);

			++stringIndex;
		}
	}
}

bool CreateTranslatedFontSheet(const string& inTranslatedFontSheet, const string& outPath, bool bAutoName = true, bool bIsSLGFont = false)
{
	//Read in translated font sheet
	BitmapReader origTranslatedBmp;
	if( !origTranslatedBmp.ReadBitmap(inTranslatedFontSheet) )
	{
		return false;
	}

	unsigned int tileDimX = bIsSLGFont ? 8 : 16;
	unsigned int tileDimY = bIsSLGFont ? 12 : 16;
	TileExtractor tileExtractor;
	if( !tileExtractor.ExtractTiles(tileDimX, tileDimY, tileDimX, tileDimY, origTranslatedBmp) )
	{
		return false;
	}

	//Convert it to the SakuraTaisen format
	PaletteData sakuraPalette;
	sakuraPalette.CreateFrom32BitData(origTranslatedBmp.mBitmapData.mPaletteData.mpRGBA, origTranslatedBmp.mBitmapData.mPaletteData.mSizeInBytes, false);
	if( sakuraPalette.GetNumColors() != 16 )
	{
		printf("CreateTranslatedFontSheet %s has %i colors.  Needs to be a 4bit 16 color image.\n", inTranslatedFontSheet.c_str(), sakuraPalette.GetNumColors());
		return false;
	}

	//Fix up palette
	//First index needs to have the transparent color, in our case that's white
	int indexOfAlphaColor = -1;
	const unsigned short alphaColor = 0xff7f; //In little endian order
	for(int i = 0; i < 16; ++i)
	{
		assert(i*2 < sakuraPalette.GetSize());

		const unsigned short color = *((short*)(sakuraPalette.GetData() + i*2));
		if( color == alphaColor )
		{
			const unsigned short oldColor0 = *((unsigned short*)sakuraPalette.GetData());
			sakuraPalette.SetValue(0, alphaColor);
			sakuraPalette.SetValue(i, oldColor0);
			indexOfAlphaColor = i;
			break;
		}
	}

	if( indexOfAlphaColor == -1 )
	{
		printf("Alpha Color not found for %s.  Palette will not be correct. \n", inTranslatedFontSheet.c_str());
		indexOfAlphaColor = 0;
	}

	const string slgFontFileSuffix = bIsSLGFont ? SlgFontFileSuffix : "";
	const string outPaletteName = bAutoName ? outPath + PatchedPaletteName + slgFontFileSuffix : outPath + "Palette.bin";
	const string outTableName   = bAutoName ? outPath + PatchedKNJName + slgFontFileSuffix : outPath + ".bin";

	//Ouptut the palette
	FileWriter outPalette;
	if( !outPalette.OpenFileForWrite(outPaletteName) )
	{
		return false;
	}
	outPalette.WriteData(sakuraPalette.GetData(), sakuraPalette.GetSize());

	//Write out the SakuraTaisen KNJ file
	FileWriter outTable;
	outTable.OpenFileForWrite(outTableName);
	
	tileExtractor.FixupIndexOfAlphaColor((unsigned short)indexOfAlphaColor, true);

	for(TileExtractor::Tile& tile : tileExtractor.mTiles)
	{
		outTable.WriteData(tile.mpTile, tile.mTileSize);
	}

	printf("CreateTranslatedFontSheet Succeeded.\n");

	return true;
}

void ConvertTranslatedText(const string& inTextDir, const string& outDir)
{
	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inTextDir, allFiles);

	//Grab all text files
	vector<FileNameContainer> textFiles;
	GetAllFilesOfType(allFiles, ".txt", textFiles);

	const short spaceLookupValue   = GTranslationLookupTable.GetIndex(' ');
	const short newLineLookupValue = GTranslationLookupTable.GetIndex('\n');
	const short spaceIndex         = ((spaceLookupValue&0xff00) << 16) + (spaceLookupValue&0x00ff);
	const short newLineIndex       = ((newLineLookupValue&0xff00) << 16) + (newLineLookupValue&0x00ff);

	//Parse all text files
	for(const FileNameContainer& fileInfo : textFiles)
	{
		//Parse file
		TextFileData fileData(fileInfo);
		fileData.InitializeTextFile();

		const string outFileName = outDir + fileInfo.mNoExtension + string("_TranslatedCodes.txt");
		TextFileWriter outFile;
		if( !outFile.OpenFileForWrite(outFileName) )
		{
			continue;
		}

		//Go through each line
		const int maxCharsPerLine = 240/OutTileSpacingX;
		const int maxLines        = MaxLines;
		int charCount             = 0;
		int sakuraLineCount       = 0;
		int lineIndex             = 1;
		for(const TextFileData::TextLine& line : fileData.mLines)
		{
			printf("Writing out codes for: %s\n", fileInfo.mFileName.c_str());

			//Now each word
			const int numWords = (int)line.mWords.size();
			for(int wordIndex = 0; wordIndex < numWords; ++wordIndex)
			{
				const std::string& word = line.mWords[wordIndex];
				const SakuraStringLookUpValue lookUpValues(word);

				if(word.size() >= maxCharsPerLine)
				{
					printf("In %s, unable to write word: %s because it is longer than the max characters allowed per line[%i]", fileInfo.mNoExtension.c_str(), word.c_str(), maxCharsPerLine);
					break;
				}

				//Add a new line if needed
				if( word.size() + charCount >= maxCharsPerLine )
				{
					charCount = 0;
					++sakuraLineCount;
					if(sakuraLineCount == maxLines)
					{
						printf("In %s, unable to write line #%i because only %i lines are allowed", fileInfo.mNoExtension.c_str(), lineIndex, maxLines);

						break;
					}
					fprintf(outFile.GetFileHandle(), "%02x%02x ", newLineIndex&0xff00, newLineIndex&0x00ff);					
				}

				//Print look up values for each character in the word
				for(const short lookUpValue : lookUpValues.mValuesForEachChar)
				{
					fprintf(outFile.GetFileHandle(), "%02x%02x ", lookUpValue&0xff00, lookUpValue&0x00ff);
					++charCount;

					//Add a new line if needed
					if( charCount + 1 >= maxCharsPerLine )
					{
						charCount = 0;
						++sakuraLineCount;
						fprintf(outFile.GetFileHandle(), "%02x%02x ", newLineIndex&0xff00, newLineIndex&0x00ff);					
					}
				}

				//Add a new line if needed
				if( word.size() + charCount >= maxCharsPerLine )
				{
					charCount = 0;
					++sakuraLineCount;
					fprintf(outFile.GetFileHandle(), "%02x%02x ", newLineIndex&0xff00, newLineIndex&0x00ff);					
				}
				//Insert space
				else if(wordIndex + 1 < numWords)
				{
					fprintf(outFile.GetFileHandle(), "%02x%02x ", spaceIndex&0xff00, spaceIndex&0x00ff);
				}
			}

			//Newline after all the words in a line
			fprintf(outFile.GetFileHandle(), "\n");

			++lineIndex;
		}
	}
}

bool PatchKNJ(const string& rootDirectory, const string& newKNJ, const string& outDir)
{
	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(string(rootDirectory), allFiles);

	//Get all knj files
	vector<FileNameContainer> originalFontsheets;
	GetAllFilesOfType(allFiles, "KNJ.BIN", originalFontsheets);
	GetAllFilesOfType(allFiles, "MES.FNT", originalFontsheets);

	//Load patched file
	FileData patchedFontData;
	if( !patchedFontData.InitializeFileData(newKNJ.c_str(), newKNJ.c_str()) )
	{
		return false;
	}

	FileData patchedSLGFontData;
	if( !patchedSLGFontData.InitializeFileData((newKNJ + SlgFontFileSuffix).c_str(), (newKNJ + SlgFontFileSuffix).c_str()) )
	{
		return false;
	}

	const string sakura1Directory("\\SAKURA1\\");
	const string sakura2Directory("\\SAKURA2\\");

	//Patch original files
	string outFileName;
	for(const FileNameContainer& knj : originalFontsheets)
	{
		FileData fileToPatch;
		if( !fileToPatch.InitializeFileData(knj) )
		{
			continue;
		}

		printf("Patching %s \n", knj.mFileName.c_str());

		const bool bIsMESFnt = knj.mFileName.find("MES.FNT", 0, 6) != string::npos;
		outFileName          = bIsMESFnt ? outDir + sakura2Directory + knj.mFileName : outDir + sakura1Directory + knj.mFileName;
		
		FileWriter patchedFile;
		if( patchedFile.OpenFileForWrite(outFileName) )
		{
			if( bIsMESFnt )
			{
				patchedFile.WriteData(patchedSLGFontData.GetData(), patchedSLGFontData.GetDataSize());
			}
			else
			{
				patchedFile.WriteData(patchedFontData.GetData(), patchedFontData.GetDataSize());
			}
		}
		else
		{
			printf("Unable to patch: %s \n", knj.mFileName.c_str());
		}
	}

	printf("PatchKNJ Succeeded.\n");

	return true;
}

bool PatchPalettes(const string& rootDirectory, const string& originalPalette, const string& newPalette, const string& inOutputDir, bool bOutputToCorrespondingDirectory = false)
{
	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(string(rootDirectory), allFiles);

	//Load original data
	FileData origData;
	if( !origData.InitializeFileData(originalPalette.c_str(), originalPalette.c_str()) )
	{
		printf("Unable to patch palettes because original palette not found.\n");
		return false;
	}

	//Load new palette data
	FileData newPaletteData;
	if( !newPaletteData.InitializeFileData(newPalette.c_str(), newPalette.c_str()) )
	{
		printf("Unable to patch palettes because new palette not found.\n");
		return false;
	}

	vector<string> patchableFiles;
	patchableFiles.push_back( string("SAKURA") );

	vector<FileNameContainer> foundFiles;

	//Go trhough all files in the SakuraTaisen directory searching for palette data
	for(const FileNameContainer& sakuraFile : allFiles)
	{
		//Only patch certain files right now because other files have a different palette format
		bool bCanPatchFile = false;
		for(const string& patchableFileName : patchableFiles)
		{
			if( sakuraFile.mFileName == patchableFileName )
			{
				bCanPatchFile = true;
				break;
			}
		}
		if( !bCanPatchFile )
		{
			continue;
		}

		printf("Searching within: %s\n", sakuraFile.mNoExtension.c_str());

		FileData sakuraFileData;
		if( sakuraFileData.InitializeFileData(sakuraFile) )
		{
			vector<unsigned long> offsets;
			sakuraFileData.DoesThisFileContain(origData, &offsets, true);

			//If palette data was found, write out new file
			if( offsets.size() )
			{
				foundFiles.push_back(sakuraFile);

				string outFileName;
				if( bOutputToCorrespondingDirectory )
				{
					const char sep = '\\';
					size_t lastSepIndex = sakuraFile.mPathOnly.rfind(sep, sakuraFile.mPathOnly.length());
					if( lastSepIndex != std::string::npos )
					{
						const string subDir = sakuraFile.mPathOnly.substr(lastSepIndex, sakuraFile.mPathOnly.size());
						if( subDir == Disc1 || subDir == Disc2 )
						{
							outFileName = inOutputDir + Seperators + sakuraFile.mFileName;
						}
						else
						{
							outFileName = inOutputDir + subDir + Seperators + sakuraFile.mFileName;
						}
					}
					else
					{
						printf("Unable to find output path for %s\n", sakuraFile.mFullPath.c_str());
						return false;
					}
				}
				else
				{
					outFileName = inOutputDir + sakuraFile.mFileName;
				}

				FileWriter outFile;
				if( !outFile.OpenFileForWrite(outFileName) )
				{
					printf("Unable to create patched file: %s \n", outFileName.c_str());
					return false;
				}

				//Write out patched data
				unsigned long origSakuraOffset = 0;
				unsigned long totalWritten = 0;
				for(size_t offsetIndex = 0; offsetIndex < offsets.size(); ++offsetIndex)
				{
					unsigned long nextOffset = offsets[offsetIndex];

					assert( origSakuraOffset < sakuraFileData.GetDataSize());
					assert( origSakuraOffset < nextOffset );
					outFile.WriteData(sakuraFileData.GetData() + origSakuraOffset, nextOffset - origSakuraOffset);
					outFile.WriteData(newPaletteData.GetData(), newPaletteData.GetDataSize());

					totalWritten += (nextOffset - origSakuraOffset) + 32;

					origSakuraOffset = nextOffset + newPaletteData.GetDataSize();
				}

				//Write out remaining porition of the file
				if( origSakuraOffset < sakuraFileData.GetDataSize() )
				{
					outFile.WriteData(sakuraFileData.GetData() + origSakuraOffset, sakuraFileData.GetDataSize() - origSakuraOffset);

					totalWritten += sakuraFileData.GetDataSize() - origSakuraOffset;
				}

				outFile.Close();

				FileData patchedFile;
				if( !patchedFile.InitializeFileData(outFileName.c_str(), outFileName.c_str()) )
				{
					printf("Unable to verify patched file size: %s\n", outFileName.c_str());
				}
				assert(patchedFile.GetDataSize() == sakuraFileData.GetDataSize());
				if( patchedFile.GetDataSize() != sakuraFileData.GetDataSize() )
				{
					printf("Patched file size is incorrect: %s\n", outFileName.c_str());
				}
			}
		}
	}

	for(const FileNameContainer& patchedFile : foundFiles)
	{
		printf("Patched palette in: %s\n", patchedFile.mFileName.c_str());
	}

	printf("PatchPalettes Succeeded\n");

	return true;
}

struct DialogOrder
{
	typedef map<unsigned short, unsigned short> IdAndImageMap;

	map<unsigned short, vector<int>> idAndOrder;
	map<int, unsigned short>         orderAndId;
	map<int, bool>                   idAndLips;
	IdAndImageMap                    idAndImage;
	bool                             bIsLipsLine = false;
};

bool FindDialogOrder(const string& rootSakuraTaisenDirectory, map<string, DialogOrder>& outOrder)
{
	//Find all files
	vector<FileNameContainer> sakuraTaisenFiles;
	FindAllFilesWithinDirectory(rootSakuraTaisenDirectory, sakuraTaisenFiles);
	if (!sakuraTaisenFiles.size())
	{
		return false;
	}

	//Find all dialog files
	vector<FileNameContainer> tableFiles;
	GetAllFilesOfType(sakuraTaisenFiles, "TBL.BIN", tableFiles);

	for (const FileNameContainer& tableFileName : tableFiles)
	{
		const size_t lastIndex        = tableFileName.mFileName.find_first_of("TBL");
		const string infoFileName     = tableFileName.mFileName.substr(0, lastIndex) + string(".BIN");
		const string infoFileFullPath = tableFileName.mPathOnly + Seperators + infoFileName;

		printf("Parsing %s\n", infoFileName.c_str());

		FileNameContainer infoFileNameInfo(infoFileName.c_str(), infoFileFullPath.c_str());
		FileData infoData;
		if (!infoData.InitializeFileData(infoFileNameInfo))
		{
			continue;
		}

		//Search for 228000
		const unsigned char* pData   = (const unsigned char*)infoData.GetData();
		unsigned long index          = 0;
		int appearance               = 0;
		unsigned short imageId       = 0;
		const unsigned long dataSize = infoData.GetDataSize();
		while (index + 5 < dataSize)
		{
			const bool bIsLipsEntry = (pData[index] == 0x2E && pData[index + 1] == 0x80 && pData[index + 2] == 0x00) ||
				                      (pData[index] == 0x17 && pData[index + 1] == 0x80 && pData[index + 2] == 0x00);

			const bool bIsRegularDialog = (pData[index] == 0x22 && pData[index + 1] == 0x80 && pData[index + 2] == 0x00) ||
				                          (pData[index] == 0x00 && pData[index + 1] == 0x80 && pData[index + 2] == 0x00 && pData[index + 3] == 0x27);

			if( bIsLipsEntry || bIsRegularDialog )
			{
				unsigned short id = pData[index + 4] + (pData[index + 3] << 8);
				outOrder[infoFileNameInfo.mNoExtension].idAndOrder[id].push_back(appearance);
				outOrder[infoFileNameInfo.mNoExtension].orderAndId[appearance] = id;
				outOrder[infoFileNameInfo.mNoExtension].idAndLips[id] = bIsLipsEntry;

				++appearance;
				index += 5;

				//Find next image id if there is one
				long lookAhead = -6;
				while(index + lookAhead - 4 > 0)
				{
					unsigned long lookAheadValue = index + lookAhead;
					if (pData[lookAheadValue] == 0x2B && pData[lookAheadValue + 1] == 0x80 && pData[lookAheadValue + 2] == 0 && pData[lookAheadValue + 4] != 0)
					{
						imageId = pData[lookAheadValue + 4] + (pData[lookAheadValue + 3] << 8);
						break;
					}

					//Break out if the next id is found
					if( (pData[lookAheadValue] == 0x22 && pData[lookAheadValue + 1] == 0x80 && pData[lookAheadValue + 2] == 0x00) ||
						(pData[lookAheadValue] == 0x2E && pData[lookAheadValue + 1] == 0x80 && pData[lookAheadValue + 2] == 0x00) || 
						(pData[lookAheadValue] == 0x17 && pData[lookAheadValue + 1] == 0x80 && pData[lookAheadValue + 2] == 0x00) ||
						(pData[lookAheadValue] == 0x00 && pData[lookAheadValue + 1] == 0x80 && pData[lookAheadValue + 2] == 0x00 && pData[lookAheadValue + 3] == 0x27)
						)
					{
						break;
					}

					--lookAhead;
				}

				outOrder[infoFileNameInfo.mNoExtension].idAndImage[id] = imageId;
			}

			++index;
		}
	}

	return true;
}

bool InsertText(const string& rootSakuraTaisenDirectory, const string& translatedTextDirectory, const string& outDirectory, bool bOutputToCorrespondingDirectory = false)
{
#define IncrementLine()\
    ++currLine;\
    charCount = 0;\
    if( currLine > maxLines ) \
	{\
		printf("Unable to fully insert line because it is longer than %i characters: %s\n", maxLines*maxCharsPerLine, textLine.mFullLine.c_str());\
		bFailedToAddLine = true;\
	}\
    translatedString.AddChar( GTranslationLookupTable.GetIndex('\n') );

	//Insert new line if needed
#define ConditionallyAddNewLine()\
	if( word.size() + charCount > maxCharsPerLine )\
	{\
		if( bIsLipsEntry && !bAlreadyShowedError )\
		{\
			printf("LIPS line is too long[D %i].  Needs to be a max of %i characters long. %s\n", translatedLineIndex + 1, maxCharsPerLine, textLine.mFullLine.c_str());\
			bAlreadyShowedError = true;\
		}\
		else\
		{\
			IncrementLine()\
		}\
	}

	//Find all translated text files
	vector<FileNameContainer> translatedTextFiles;
	FindAllFilesWithinDirectory(string(translatedTextDirectory), translatedTextFiles);

	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(rootSakuraTaisenDirectory, allFiles);

	//Get all files containing dialog
	vector<FileNameContainer> textFiles;
	GetAllFilesOfType(allFiles, "TBL.BIN", textFiles);
	GetAllFilesOfType(allFiles, "MES.BIN", textFiles);

	//Extract the text
	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(textFiles, sakuraTextFiles);

	//Find dialog order
	map<string, DialogOrder> dialogOrder;
	FindDialogOrder(rootSakuraTaisenDirectory, dialogOrder);

	//***Load duplicate info***
	const bool bSearchForDuplicates = false;
	printf("Loading Duplicate Info\n");
	const string duplicatesFileName = translatedTextDirectory + string("\\..\\DuplicatesForTextInsert.txt");
	FileNameContainer dupFileNameContainer(duplicatesFileName.c_str());
	TextFileData duplicatesFile(dupFileNameContainer);
	if( bSearchForDuplicates )
	{
		if( !duplicatesFile.InitializeTextFile(false) )
		{
			printf("Unable to open duplicates file.\n");
			return false;
		}
	}

	printf("Creating Duplicate Map\n");
	const string underScore("_");
	map<string, bool> duplicatesMap; //dir+lineNumber, dummyBool
	const size_t numDupLines = duplicatesFile.mLines.size();
	for(size_t i = 0; i < numDupLines; ++i)
	{
		if( duplicatesFile.mLines[i].mWords.size() != 3 )
		{
			printf("Invalid duplicate file format");
			return false;
		}

		const string dirPlusLine = duplicatesFile.mLines[i].mWords[0] + underScore + duplicatesFile.mLines[i].mWords[1];
		duplicatesMap[dirPlusLine] = true;
	}
	//***Done loading duplicate data***

	const string UntranslatedEnglishString("Untranslated");
	const string UnusedEnglishString("Unused");

	//Insert text
	for(const SakuraTextFile& sakuraFile : sakuraTextFiles)
	{
		printf("\nInserting text for: %s\n", sakuraFile.mFileNameInfo.mFileName.c_str());

		const bool bIsMESFile = sakuraFile.mFileNameInfo.mFileName.find("MES.BIN", 0, 6) != string::npos;

		//Used for verifying timing data
		map<int, int> lineTimingMap;

		//Figure out output file name
		string outFileName;
		if( bOutputToCorrespondingDirectory )
		{
			const char sep = '\\';
			size_t lastSepIndex = sakuraFile.mFileNameInfo.mPathOnly.rfind(sep, sakuraFile.mFileNameInfo.mPathOnly.length());
			if( lastSepIndex != std::string::npos )
			{
				const string subDir = sakuraFile.mFileNameInfo.mPathOnly.substr(lastSepIndex, sakuraFile.mFileNameInfo.mPathOnly.size());
				if( subDir == Disc1 || subDir == Disc2 )
				{
					outFileName = outDirectory + Seperators + sakuraFile.mFileNameInfo.mFileName;
				}
				else
				{
					outFileName = outDirectory + subDir + Seperators + sakuraFile.mFileNameInfo.mFileName;
				}
			}
			else
			{
				printf("Unable to find output path for %s\n", sakuraFile.mFileNameInfo.mFullPath.c_str());
				return false;
			}
		}
		else
		{
			outFileName = outDirectory + sakuraFile.mFileNameInfo.mFileName;
		}

		//Create output file
		FileWriter outFile;
		if( !outFile.OpenFileForWrite(outFileName) )
		{
			printf("Unable to open the output file %s.\n", outFileName.c_str());

			return false;
		}

		//Search for the corresponding translated file name
		const FileNameContainer* pMatchingTranslatedFileName = nullptr;
		for(const FileNameContainer& translatedFileName : translatedTextFiles)
		{
			if( translatedFileName.mNoExtension.find(sakuraFile.mFileNameInfo.mNoExtension) != string::npos )
			{
				pMatchingTranslatedFileName = &translatedFileName;
				break;
			}
		}

		//Find the dialog order for this sakura file
		const size_t lastIndex                                   = sakuraFile.mFileNameInfo.mNoExtension.find_first_of("TBL");
		const string infoFileName                                = sakuraFile.mFileNameInfo.mNoExtension.substr(0, lastIndex);
		map<string, DialogOrder>::const_iterator dialogOrderIter = dialogOrder.find(infoFileName);
		const bool bDialogOrderExists                            = dialogOrderIter != dialogOrder.end();

		static const int dirPlusLineBufferSize = 255;
		char dirPlusLineNumberBuffer[dirPlusLineBufferSize];

		vector<SakuraString> translatedLines;
		if( pMatchingTranslatedFileName )
		{
			//Open translated text file
			TextFileData translatedFile(*pMatchingTranslatedFileName);
			if( !translatedFile.InitializeTextFile() )
			{
				printf("Unable to open the translation file %s.\n", pMatchingTranslatedFileName->mFullPath.c_str());
				return false;
			}

			//Make sure we have the correct amount of lines
			if( sakuraFile.mLines.size() < translatedFile.mLines.size() )
			{
				printf("Unable to translate file: %s because the translation has too many lines.\n", pMatchingTranslatedFileName->mNoExtension.c_str());
				return false;
			}

			//Get converted lines of text
			const int maxCharsPerLine       = (240/OutTileSpacingX);
			const int maxLines              = MaxLines;
			const size_t numTranslatedLines = translatedFile.mLines.size();
			unsigned short numCharsPrintedForMES  = 0;
			unsigned short numCharsPrintedFortTBL = 0;

			auto UpdateNumTimingCharsPrinted = [&](int inTranslatedLineIndex, const SakuraString& inTranslatedString)->bool
			{ 
				if( sakuraFile.mLines[inTranslatedLineIndex].mChars[0].mIndex != 0 )
				{
					const short numLinesInString = static_cast<short>(inTranslatedString.GetNumberOfLines()) - 1;
					if( numLinesInString < 0 )
					{
						printf("Invalid number of lines in string\n");
						return false;
					}

					//-3 because 2 for the initial bytes and one for the trailing zero
					const unsigned short numCharsPrintedInLine = (unsigned short)inTranslatedString.GetNumberOfActualCharacters();//(unsigned short)inTranslatedString.GetNumberOfPrintedCharacters() - 3;//(static_cast<short>(translatedString.mChars.size()) - 3 - numLinesInString);
					if( (short)numCharsPrintedInLine < 0 )
					{
						printf("Invalid number of characters in string\n");
						return false;
					}

#if FIX_TIMING_DATA
					if( !bIsMESFile )
					{
						if( (int)sakuraFile.mLineTimingData.size() < inTranslatedLineIndex )
						{
							printf("Not enough timing data lines in %s.  Expected %i, got %i", sakuraFile.mFileNameInfo.mFileName.c_str(), inTranslatedLineIndex, sakuraFile.mLineTimingData.size());

							numCharsPrintedFortTBL += (numCharsPrintedInLine + 1)*2;
						}
						else
						{
							const int numPrefixBytes              = sakuraFile.mLineTimingData[inTranslatedLineIndex].GetNumPrefixBytes();
							const int originalTimingCount         = (int)sakuraFile.mLineTimingData[inTranslatedLineIndex].GetNumBytesInLine_NoKeyValues();
							int finalPrintedCount                 = numCharsPrintedInLine + originalTimingCount + 2;
							
							//Extra 0x6e bytes will be added
							if( numCharsPrintedInLine >= (originalTimingCount - numPrefixBytes) )
							{
								finalPrintedCount += (numCharsPrintedInLine - originalTimingCount) + numPrefixBytes;
							}

							if( finalPrintedCount >= 65536 )
							{
								printf("FinalPrintedCount is too big for %s line %i\n", sakuraFile.mFileNameInfo.mNoExtension.c_str(), inTranslatedLineIndex);
							}
							numCharsPrintedFortTBL += (unsigned short)finalPrintedCount;

							//Add extra byte to make things two byte aligned
							if( !bIsMESFile && finalPrintedCount%2 != 0 )
							{
								numCharsPrintedFortTBL += 1;
								finalPrintedCount += 1;
							}

							lineTimingMap[inTranslatedLineIndex] = finalPrintedCount;
						}
					}
#else
					numCharsPrintedFortTBL += (numCharsPrintedInLine + 1)*2;
#endif

					numCharsPrintedForMES  += numCharsPrintedInLine*2 + 1;
				}

				return true;
			};

			for(size_t translatedLineIndex = 0; translatedLineIndex < numTranslatedLines; ++translatedLineIndex)
			{	
				const TextFileData::TextLine& textLine = translatedFile.mLines[translatedLineIndex];
				int charCount            = 0;
				int currLine             = 1;
				const size_t numWords    = textLine.mWords.size();
				bool bAlreadyShowedError = false;
				SakuraString translatedString;

				//Figure out if this is a lips entry
				const size_t currSakuraStringIndex = translatedLineIndex;
				const unsigned short id            = sakuraFile.mStringInfoArray[currSakuraStringIndex].mStringId;
				const vector<int>* pOrder          = bDialogOrderExists && dialogOrderIter->second.idAndOrder.find(id) != dialogOrderIter->second.idAndOrder.end() ? &dialogOrderIter->second.idAndOrder.find(id)->second : nullptr;
				const bool bHasLipsTag             = textLine.mWords.size() > 0 && textLine.mWords[0] == LipsWord;
				const bool bIsLipsEntry            = bHasLipsTag ? true : pOrder ? dialogOrderIter->second.idAndLips.find(id)->second : false;
				const bool bIsUnused               = bIsMESFile ? false : pOrder || (id >= 0x9c41 && id <= 0x9cff) ? false : true;

				if( bIsLipsEntry )
				{
					if( textLine.GetNumberOfLines() != sakuraFile.mLines[currSakuraStringIndex].GetNumberOfLines() )
					{
						printf("LIPS ERROR - Translated LIPS line does not have expected number of options. Expected: %i, Has: %i (File: %s Line: %u)\n", sakuraFile.mLines[currSakuraStringIndex].GetNumberOfLines(), textLine.GetNumberOfLines(), sakuraFile.mFileNameInfo.mFileName.c_str(), currSakuraStringIndex + 1);
					}
				}

				if( textLine.mWords[0] == UntranslatedEnglishString || 
					(textLine.mWords[0] == UnusedEnglishString && !bIsUnused )
					)
				{
					printf("Error: Untranslated line. (File: %s Line: %u)\n", sakuraFile.mFileNameInfo.mFileName.c_str(), currSakuraStringIndex + 1);
				}

				//If untranslated, then write out the file and line number
				if(// bIsUnused ||
					textLine.mWords.size() == 0 ||
					(textLine.mWords.size() == 1 && (textLine.mWords[0] == UntranslatedEnglishString || textLine.mWords[0] == UnusedEnglishString) )
					)
				{
					bool bUseShorthand = false;
#if USE_SINGLE_BYTE_LOOKUPS && USE_4_BYTE_OFFSETS 
					if( numTranslatedLines > 1200 )
					{
						bUseShorthand = true;
					}
#endif

					if( textLine.mWords.size() == 0 )
					{
						printf("Warning: Blank line. (File: %s Line: %u)\n", sakuraFile.mFileNameInfo.mFileName.c_str(), currSakuraStringIndex + 1);
					}
					//Verify that this line should indeed be unused
					else if( textLine.mWords[0] == UnusedEnglishString )
					{
						snprintf(dirPlusLineNumberBuffer, dirPlusLineBufferSize, "%s_%i", sakuraFile.mFileNameInfo.mNoExtension.c_str(), translatedLineIndex);

						const string dirPlusLineNumber = dirPlusLineNumberBuffer;
						if( bIsLipsEntry || 
							( bSearchForDuplicates && (duplicatesMap.find(dirPlusLineNumber) == duplicatesMap.end() && !(id >= 0xc351 && id <= 0xcfff) && id != 0x9c41) ) 
							)
						{
							printf("Warning: UnusedString has no duplicates in any other file, so it might be used. (File: %s Line: %u Id: %#06x)\n", sakuraFile.mFileNameInfo.mFileName.c_str(), currSakuraStringIndex + 1, id);
						}
					}

					SakuraString translatedSakuraString;
					unsigned short timingData = 0;
					if( bIsMESFile )
					{
						timingData = numCharsPrintedForMES;
					}
					else
					{
						if( sakuraFile.mLines[translatedLineIndex].mChars[0].mIndex != 0 && textLine.mFullLine.size() > 1 )
						{
							timingData = (numCharsPrintedFortTBL >> 1);
						}
					}

					const string baseUntranslatedString = sakuraFile.mFileNameInfo.mNoExtension + string(":");
					const string untranslatedString = bUseShorthand && !timingData ? "U" : baseUntranslatedString + std::to_string(translatedLineIndex + 1); //Just print out a U for unused lines to save space

					translatedSakuraString.AddString( untranslatedString, sakuraFile.mLines[currSakuraStringIndex].mChars[0].mIndex, timingData, !bIsMESFile);
					//sakuraFile.mLineTimingData[currSakuraStringIndex].GetNumBytesInLine()
					UpdateNumTimingCharsPrinted(translatedLineIndex, translatedSakuraString);

					translatedLines.push_back( translatedSakuraString );
					continue;
				}

				//Lines starting with the indicator 0xC351 have a special two byte value instead of the usual 00 00
				{
					translatedString.AddChar( sakuraFile.mLines[translatedLineIndex].mChars[0].mIndex );

					if( bIsMESFile )
					{
						translatedString.AddChar( numCharsPrintedForMES );
					}
					else
					{
						//If the TBL file has timing data, the second byte of the startin sequence will be the number of timing chars, otherwise its just a 00
						if( sakuraFile.mLines[translatedLineIndex].mChars[0].mIndex != 0 && textLine.mFullLine.size() > 1 )
						{	
							translatedString.AddChar( (numCharsPrintedFortTBL >> 1) );
						}
						else
						{
							translatedString.AddChar(0);
						}
					}
				}

				for(size_t wordIndex = bHasLipsTag ? 1 : 0; wordIndex < numWords; ++wordIndex)
				{
					const string& word    = textLine.mWords[wordIndex];
					bool bFailedToAddLine = false;
					if( word.size() > maxCharsPerLine )
					{
						printf("Unable to insert word because it is longer than %i characters: %s[%i]\n", maxCharsPerLine, word.c_str(), word.size());
						continue;
					}

					//Check to see if this is a newline
					const bool bNewLineWord = word == NewLineWord;
					if( bNewLineWord )
					{
						IncrementLine();

						//bFailedToAddLine set inside IncrementLine
						if( bFailedToAddLine )
						{
						//	break;
						}

						continue;
					}

					//Check to see if this is a space
					const bool bSpaceWord = word == SpaceWord;
					if( bSpaceWord )
					{	
						if( charCount + 1 > maxCharsPerLine )
						{
							if( bIsLipsEntry )
							{
								printf("LIPS line is too long[A].  Needs to be a max of %i characters long. %s\n", maxCharsPerLine, textLine.mFullLine.c_str());
								//break;
							}
							else
							{
								IncrementLine();
							}

							//bFailedToAddLine set inside IncrementLine
							if( bFailedToAddLine )
							{
							//	break;
							}
						}
						else
						{
							translatedString.AddChar( GTranslationLookupTable.GetIndex(' ') );
							++charCount;
						}

						continue;
					}

					//Insert new line if needed
					ConditionallyAddNewLine();
					
					//Add the word
					const size_t numLettersInWord = bNewLineWord ? 1 : word.size();
					if( word.size() + charCount > maxCharsPerLine )
					{
						if( bIsLipsEntry && !bAlreadyShowedError )
						{
							printf("LIPS line is too long [B].  Needs to be a max of %i characters long. %s\n", maxCharsPerLine, textLine.mFullLine.c_str());
							bAlreadyShowedError = true;
						//	break;
						}
						else
						{
							IncrementLine();
						}
					}

					const char* pWord = word.c_str();
					for(size_t letterIndex = 0; letterIndex < numLettersInWord; ++letterIndex)
					{
						translatedString.AddChar( GTranslationLookupTable.GetIndex(pWord[letterIndex]) );
						++charCount;
					}

					//bFailedToAddLine is set inside ConditionallyAddNewLine
					if( bFailedToAddLine )
					{
						break;
					}

					//Add sapce
					if( wordIndex + 1 < numWords )
					{
						//Insert new line if needed
						const string& nextWord = textLine.mWords[wordIndex + 1];
						if( nextWord != NewLineWord )
						{
							//If adding the next word will put us over the word limit, add a new line
							if( nextWord.size() + charCount > maxCharsPerLine )
							{
								ConditionallyAddNewLine();
							}
							else //Otherwise add a space
							{
								if( charCount + 1 >= maxCharsPerLine )
								{
									if( bIsLipsEntry && !bAlreadyShowedError )
									{
										printf("LIPS line is too long[C].  Needs to be a max of %i characters long. %s\n", maxCharsPerLine, textLine.mFullLine.c_str());
										bAlreadyShowedError = true;
								//		break;
									}
									else
									{
										IncrementLine();
									}
								}
								else
								{
									translatedString.AddChar( GTranslationLookupTable.GetIndex(' ') );
									++charCount;
								}
							}
						}
					}
				}

#if USE_SINGLE_BYTE_LOOKUPS
				if( !bIsMESFile && (translatedString.mChars.size())% 2 != 0 )
				{
					translatedString.AddChar(' ');
				}
#endif

				//String ends with 0000
				translatedString.AddChar(0);

				UpdateNumTimingCharsPrinted(translatedLineIndex, translatedString);
				translatedLines.push_back( std::move(translatedString) );
			}

			//Fill out everything else with "Untranslated"
			const string baseUntranslatedString = sakuraFile.mFileNameInfo.mNoExtension + string(": ");
			const size_t untranslatedCount      = sakuraFile.mLines.size() - translatedFile.mLines.size();
			const unsigned short timingData     = bIsMESFile ? numCharsPrintedForMES : (numCharsPrintedFortTBL >> 1);
			for(size_t i = 0; i < untranslatedCount; ++i)
			{
				const string untranslatedString = baseUntranslatedString + std::to_string(i + translatedFile.mLines.size() + 1);
				SakuraString translatedSakuraString;

				const size_t currSakuraStringIndex = i + translatedFile.mLines.size();
				translatedSakuraString.AddString( untranslatedString, sakuraFile.mLines[currSakuraStringIndex].mChars[0].mIndex, timingData, !bIsMESFile);

				UpdateNumTimingCharsPrinted(currSakuraStringIndex, translatedSakuraString);

				translatedLines.push_back( translatedSakuraString );
			}

		}//if(pMatchingTranslatedFileName)
		else
		{
			//Fill out everything else with "Untranslated"
			const string baseUntranslatedString = sakuraFile.mFileNameInfo.mNoExtension + string(": ");
			for(size_t i = 0; i < sakuraFile.mLines.size(); ++i)
			{
				const string untranslatedString = baseUntranslatedString + std::to_string(i + 1);
				SakuraString translatedSakuraString;
				translatedSakuraString.AddString( untranslatedString, sakuraFile.mLines[i].mChars[0].mIndex, 0, !bIsMESFile);
				translatedLines.push_back( translatedSakuraString );
			}		
		}

		SakuraTextFileFixedHeader fixedHeader;
		fixedHeader.CreateFixedHeader(sakuraFile.mStringInfoArray, sakuraFile, translatedLines, bIsMESFile);

		//Write header
#if USE_4_BYTE_OFFSETS
		if( bIsMESFile )
		{
			const unsigned short bigEndianOffsetToTable = SwapByteOrder(fixedHeader.mOffsetToTable);
			const unsigned short bigEndianTableEnd      = SwapByteOrder(fixedHeader.mTableEnd);
			outFile.WriteData(&bigEndianOffsetToTable, sizeof(bigEndianOffsetToTable));
			outFile.WriteData(&bigEndianTableEnd, sizeof(bigEndianTableEnd));
			outFile.WriteData(&fixedHeader.mStringInfo[0], fixedHeader.mStringInfo.size()*sizeof(fixedHeader.mStringInfo[0]));
		}
		else
		{
			const unsigned int bigEndianOffsetToTable = SwapByteOrder(fixedHeader.mOffsetToTable8Bytes);
			const unsigned int bigEndianTableEnd        = SwapByteOrder(fixedHeader.mTableEnd8Bytes);
			outFile.WriteData(&bigEndianOffsetToTable, sizeof(bigEndianOffsetToTable));
			outFile.WriteData(&bigEndianTableEnd, sizeof(bigEndianTableEnd));
			outFile.WriteData(&fixedHeader.mStringInfo8Bytes[0], fixedHeader.mStringInfo8Bytes.size()*sizeof(fixedHeader.mStringInfo8Bytes[0]));
		}
#else
		const unsigned short bigEndianOffsetToTable = SwapByteOrder(fixedHeader.mOffsetToTable);
		const unsigned short bigEndianTableEnd      = SwapByteOrder(fixedHeader.mTableEnd);
		outFile.WriteData(&bigEndianOffsetToTable, sizeof(bigEndianOffsetToTable));
		outFile.WriteData(&bigEndianTableEnd, sizeof(bigEndianTableEnd));
		outFile.WriteData(&fixedHeader.mStringInfo[0], fixedHeader.mStringInfo.size()*sizeof(fixedHeader.mStringInfo[0]));
#endif
		//outFile.WriteData(sakuraFile.mHeader.pData, sakuraFile.mHeader.dataSize);

		//Output data
		const size_t numDataSegments    = sakuraFile.mDataSegments.size();
		size_t dataIndex                = 0;
		size_t translationIndex         = 0;
		const size_t numInsertedLines   = translatedLines.size();
		unsigned int numSingleBytesWritten = 0;
		while(1)
		{
			if( dataIndex < numDataSegments )
			{
				outFile.WriteData(sakuraFile.mDataSegments[dataIndex].pData, sakuraFile.mDataSegments[dataIndex].dataSize);
			}

			if( translationIndex < numInsertedLines )
			{
#if USE_SINGLE_BYTE_LOOKUPS
				if( bIsMESFile )
				{
					vector<unsigned short> translationData;
					translatedLines[dataIndex].GetDataArray(translationData);
					outFile.WriteData(translationData.data(), translationData.size() * sizeof(short));
				}
				else
				{
					vector<unsigned char> translationData;
					translatedLines[dataIndex].GetSingleByteDataArray(translationData);
					outFile.WriteData(translationData.data(), translationData.size() * sizeof(char));
					numSingleBytesWritten += translationData.size() * sizeof(char);
				}
#else
				vector<unsigned short> translationData;
				translatedLines[dataIndex].GetDataArray(translationData);
				outFile.WriteData(translationData.data(), translationData.size() * sizeof(short));
#endif		
			}

			++dataIndex;
			++translationIndex;

			if( dataIndex >= numDataSegments && translationIndex >= numInsertedLines )
			{
				break;
			}
		}

		if( numSingleBytesWritten > 0 && numSingleBytesWritten % 2 != 0)
		{
			char dummy = 0;
			outFile.WriteData(&dummy, sizeof(dummy));
		}

		//Write footer
		{
			const unsigned char char2E   = CharTimingStartID;
			const unsigned char char6E   = 0x6e;
			const unsigned char  charEnd = 0;

#if FIX_TIMING_DATA
			const size_t numTranslatedLines = translatedLines.size();
			for(size_t translatedLinedIndex = 0; translatedLinedIndex < numTranslatedLines; ++translatedLinedIndex)
			{
				const SakuraString& translatedString = translatedLines[translatedLinedIndex];
				if( translatedString.mChars[0].mIndex == 0 )
				{
					continue;
				}

				if( bIsMESFile )
				{
					const size_t numChars = translatedString.mChars.size() - 1; //.size includes null terminator
					int numProcessed = 0;
					for(size_t c = 2; c < numChars; ++c)
					{
						if( !translatedString.mChars[c].IsNewLine() )
						{
							outFile.WriteData(&char6E, sizeof(char6E));
							outFile.WriteData(&char2E, sizeof(char2E));

							++numProcessed;
						}
					}
					outFile.WriteData(&charEnd, sizeof(charEnd));

					if( !bIsMESFile )
					{
						outFile.WriteData(&charEnd, sizeof(charEnd));
					}
				}
				else //if ( bIsMESFile )
				{
					//Todo: Probably don't need to write out timing data for lines that don't have timing data in the original file

					const size_t numOriginalCharactersInLine = sakuraFile.mLineTimingData[translatedLinedIndex].mTimingForLine.size();
					const size_t numNewCharactersInLines     = (size_t)translatedString.GetNumberOfActualCharacters();
					if( 1 ) //numNewCharactersInLines >= numOriginalCharactersInLine )
					{
						const size_t numTimingBytes_NoKeyValue = sakuraFile.mLineTimingData[translatedLinedIndex].GetNumBytesInLine_NoKeyValues();
						const size_t numTimingBytes            = sakuraFile.mLineTimingData[translatedLinedIndex].GetNumBytesInLine();
						if( numTimingBytes_NoKeyValue + numOriginalCharactersInLine != numTimingBytes )
						{
							printf("Timing data doesn't have correct amount of data in %s. \n", sakuraFile.mFileNameInfo.mFileName.c_str());
							return false;
						}

						bool bCalculateTimingByteInfo        = true;
						int numTimingValuesPerCharacter = 0;
						int numCharactersWithOneTimingValue  = 0;
						int numTimingBytesWritten            = 0;

						//Write out original data
						char* pTimingDataByteStream = sakuraFile.mLineTimingData[translatedLinedIndex].GetByteStream();
						char* pDataWithNoStartID = new char[numTimingBytes];
						memset(pDataWithNoStartID, 0x6e, numTimingBytes);

						for(size_t t = 0; t < numTimingBytes; )
						{
							//TODO: Fix expected count in this scenario
							//Check for timing data that precedes the first character entry.  Don't divide up those bytes.
							if( t == 0 && pTimingDataByteStream[t] != CharTimingStartID )
							{
								//Write out the timing data
								while( t < numTimingBytes && pTimingDataByteStream[t] != CharTimingStartID )
								{
									outFile.WriteData(&pTimingDataByteStream[t], sizeof(char));
									++t;
									++numTimingBytesWritten;
								}

								continue;
							}

							//Figure out how many character will have one byte of timing data assigned to them, and how many will have two bytes
							if( bCalculateTimingByteInfo )
							{
								bCalculateTimingByteInfo = false;

								size_t numBytesToSplit = 0;
								int n = 0;
								for(size_t k = t; k < numTimingBytes; ++k)
								{
									if( pTimingDataByteStream[k] != CharTimingStartID )
									{
										++numBytesToSplit;
										pDataWithNoStartID[n] = pTimingDataByteStream[k];
										++n;
									}
								}

								//Always gaurantee that number of timing bytes to split is atleast as big as the number of characters in the translated text
								if( numBytesToSplit < numNewCharactersInLines )
								{
									numBytesToSplit += (numNewCharactersInLines - numBytesToSplit);
								
									//Pad with 0x6e
									if( numTimingBytes < numBytesToSplit )
									{
										char* pNewBuffer = new char[numBytesToSplit];
										memset(pNewBuffer, 0x6e, numBytesToSplit);
										memcpy_s(pNewBuffer, numTimingBytes, pDataWithNoStartID, numTimingBytes);

										delete[] pDataWithNoStartID;
										pDataWithNoStartID = pNewBuffer;
									}
								}

								numTimingValuesPerCharacter     = numBytesToSplit/numNewCharactersInLines;
								numCharactersWithOneTimingValue = numBytesToSplit - numTimingValuesPerCharacter*numNewCharactersInLines;
							}

							int numSingleEntriesWritten = 0;
							int timingIndex = 0;
							for(size_t characterIndex = 0; characterIndex < numNewCharactersInLines; characterIndex++)
							{
								outFile.WriteData(&CharTimingStartID, sizeof(CharTimingStartID));
								++numTimingBytesWritten;

								for(int innerIndex = 0; innerIndex < numTimingValuesPerCharacter; ++innerIndex)
								{
									outFile.WriteData(&pDataWithNoStartID[timingIndex], sizeof(char));
									++timingIndex;
									++numTimingBytesWritten;
								}
								
								if( numSingleEntriesWritten < numCharactersWithOneTimingValue )
								{
									outFile.WriteData(&pDataWithNoStartID[timingIndex], sizeof(char));
									++numSingleEntriesWritten;
									++timingIndex;
									++numTimingBytesWritten;
								}
							}
							break;
						}

						delete[] pDataWithNoStartID;
						delete[] pTimingDataByteStream;

						//Make it two byte aligned by adding in an extra 0x6e
						if( !bIsMESFile && numTimingBytesWritten%2 != 0 )
						{
							outFile.WriteData(&char6E, sizeof(char6E));
							++numTimingBytesWritten;
						}

						numTimingBytesWritten += 2;
						const int expectedTimingCount = lineTimingMap[translatedLinedIndex];
						if( expectedTimingCount != numTimingBytesWritten )
						{
							printf("Invalid timing count for line %i in %s. Expected %i, wrote out %i\n", translatedLinedIndex, sakuraFile.mFileNameInfo.mFileName.c_str(), expectedTimingCount, numTimingBytesWritten);
						}
					}

					outFile.WriteData(&charEnd, sizeof(charEnd));
					outFile.WriteData(&charEnd, sizeof(charEnd));
				}
			}
#else
			for(const SakuraString& translatedString : translatedLines)
			{
				if( translatedString.mChars[0].mIndex == 0 )
				{
					continue;
				}

				const size_t numChars = translatedString.mChars.size() - 1; //.size includes null terminator
				int numProcessed = 0;
				for(size_t c = 2; c < numChars; ++c)
				{
					if( !translatedString.mChars[c].IsNewLine() )
					{
						outFile.WriteData(&char6E, sizeof(char6E));
						outFile.WriteData(&char2E, sizeof(char2E));

						++numProcessed;
					}
				}
				outFile.WriteData(&charEnd, sizeof(charEnd));

				if( !bIsMESFile )
				{
					outFile.WriteData(&charEnd, sizeof(charEnd));
				}
			}
#endif
		}

		if( outFile.GetFileSize() > MaxTBLFileSize )
		{
			printf("WARNING: TBL file %s is too big: %lu.\n", sakuraFile.mFileNameInfo.mFileName.c_str(), outFile.GetFileSize());
		}
	}

	printf("InsertText Succeeded\n");

	return true;
}

bool DumpTranslationFilesWithoutUnusedLines(const string& rootSakuraTaisenDirectory, const string& translatedTextDirectory, const string& outDirectory)
{

	//Find all translated text files
	vector<FileNameContainer> translatedTextFiles;
	FindAllFilesWithinDirectory(string(translatedTextDirectory), translatedTextFiles);

	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(rootSakuraTaisenDirectory, allFiles);

	//Get all files containing dialog
	vector<FileNameContainer> textFiles;
	GetAllFilesOfType(allFiles, "TBL.BIN", textFiles);
	GetAllFilesOfType(allFiles, "MES.BIN", textFiles);

	//Extract the text
	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(textFiles, sakuraTextFiles);

	//Find dialog order
	map<string, DialogOrder> dialogOrder;
	FindDialogOrder(rootSakuraTaisenDirectory, dialogOrder);

	const string UntranslatedEnglishString("Untranslated");

	//Insert text
	for(const SakuraTextFile& sakuraFile : sakuraTextFiles)
	{
		printf("Dumping text for: %s\n", sakuraFile.mFileNameInfo.mFileName.c_str());

		const bool bIsMESFile = sakuraFile.mFileNameInfo.mFileName.find("MES.BIN", 0, 6) != string::npos;

		//Create output file
		string outFileName = outDirectory + sakuraFile.mFileNameInfo.mNoExtension + ".txt";
		TextFileWriter outFile;
		if( !outFile.OpenFileForWrite(outFileName) )
		{
			printf("Unable to open the output file %s.\n", outFileName.c_str());

			return false;
		}

		//Search for the corresponding translated file name
		const FileNameContainer* pMatchingTranslatedFileName = nullptr;
		for(const FileNameContainer& translatedFileName : translatedTextFiles)
		{
			//if( translatedFileName.mNoExtension == sakuraFile.mFileNameInfo.mNoExtension )
			if( translatedFileName.mNoExtension.find(sakuraFile.mFileNameInfo.mNoExtension) != string::npos )
			{
				pMatchingTranslatedFileName = &translatedFileName;
				break;
			}
		}

		//Find the dialog order for this sakura file
		const size_t lastIndex                                   = sakuraFile.mFileNameInfo.mNoExtension.find_first_of("TBL");
		const string infoFileName                                = sakuraFile.mFileNameInfo.mNoExtension.substr(0, lastIndex);
		map<string, DialogOrder>::const_iterator dialogOrderIter = dialogOrder.find(infoFileName);
		const bool bDialogOrderExists                            = dialogOrderIter != dialogOrder.end();

		vector<SakuraString> translatedLines;
		if( pMatchingTranslatedFileName )
		{
			//Open translated text file
			TextFileData translatedFile(*pMatchingTranslatedFileName);
			if( !translatedFile.InitializeTextFile(false) )
			{
				printf("Unable to open the translation file %s.\n", pMatchingTranslatedFileName->mFullPath.c_str());
				return false;
			}

			//Make sure we have the correct amount of lines
			if( sakuraFile.mLines.size() < translatedFile.mLines.size() )
			{
				printf("Unable to translate file: %s because the translation has too many lines.\n", pMatchingTranslatedFileName->mNoExtension.c_str());
				return false;
			}

			//Get converted lines of text
			const size_t numTranslatedLines = translatedFile.mLines.size();
			for(size_t translatedLineIndex = 0; translatedLineIndex < numTranslatedLines; ++translatedLineIndex)
			{	
				const TextFileData::TextLine& textLine = translatedFile.mLines[translatedLineIndex];
				const size_t numWords = textLine.mWords.size();
				SakuraString translatedString;

				//Figure out if this is a lips entry
				const size_t currSakuraStringIndex = translatedLineIndex;
				const unsigned short id            = sakuraFile.mStringInfoArray[currSakuraStringIndex].mStringId;
				const vector<int>* pOrder          = bDialogOrderExists && dialogOrderIter->second.idAndOrder.find(id) != dialogOrderIter->second.idAndOrder.end() ? &dialogOrderIter->second.idAndOrder.find(id)->second : nullptr;
				const bool bHasLipsTag             = textLine.mWords.size() > 0 && textLine.mWords[0] == LipsWord;
				const bool bIsLipsEntry            = bHasLipsTag ? true : pOrder ? dialogOrderIter->second.idAndLips.find(id)->second : false;
				const bool bIsUnused               = bIsMESFile ? false : pOrder || (id >= 0x9c41 && id < 0x9cff) ? false : true;

				//If untranslated, then write out the file and line number
				if( bIsUnused ||
					textLine.mWords.size() == 0 )
				{
					static const string unusedString = "Unused\n";
					outFile.WriteString(unusedString);
					continue;
				}
				else
				{
					outFile.WriteString(textLine.mFullLine);
					outFile.AddNewLine();
				}
			}

		}//if(pMatchingTranslatedFileName)
	}

	return true;
}

void FindDuplicateText(const string& dialogImageDirectory, const string& outFileName)
{
	TextFileWriter outFile;
	if( !outFile.OpenFileForWrite(outFileName) )
	{
		return;
	}

	//Find all translated text files
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(dialogImageDirectory, allFiles);
	if( !allFiles.size() )
	{
		return;
	}

	vector<FileNameContainer> imageFiles;
	GetAllFilesOfType(allFiles, ".png", imageFiles);

	//Seperate all files into their own directories
	printf("Calculating CRCs...\n");
	map<string, vector<const FileNameContainer*>> directoryMap;
	map<const FileNameContainer*, unsigned long> crcMap;
	for(const FileNameContainer& imageFileNameInfo : imageFiles)
	{
		const size_t lastOf = imageFileNameInfo.mPathOnly.find_last_of('\\');
		const string leaf = imageFileNameInfo.mPathOnly.substr(lastOf+1);
		if( lastOf != string::npos && leaf.size() > 0 )
		{
			directoryMap[leaf].push_back(&imageFileNameInfo);

			//Create CRC entry
			FileData fileData;
			if( !fileData.InitializeFileData( imageFileNameInfo ) )
			{
				continue;
			}

			crcMap[&imageFileNameInfo] = fileData.GetCRC();
		}
	}	

	printf("Finding Duplicates\n");
	map<const FileNameContainer*, vector<string>> duplicatesMap; //FileName, Vector<Duplicate FileNames>
	const size_t numImageFiles = imageFiles.size();
	for(size_t fileIndex = 0; fileIndex < numImageFiles; ++fileIndex)
	{
		const FileNameContainer& firstImageName = imageFiles[fileIndex];

		//Make sure we haven't already found a match for this file
		bool bExistingDupFound = false;
		for(map<const FileNameContainer*, vector<string>>::const_iterator mapIter = duplicatesMap.begin(); mapIter != duplicatesMap.end(); ++mapIter)
		{
			const size_t numDups = mapIter->second.size();
			for(size_t dupIndex = 0; dupIndex < numDups; ++dupIndex)
			{
				if( mapIter->second[dupIndex] == firstImageName.mNoExtension )
				{
					bExistingDupFound = true;
					break;
				}
			}

			if( bExistingDupFound )
			{
				break;
			}
		}

		if( bExistingDupFound )
		{
			continue;
		}

		if( crcMap.find(&firstImageName) == crcMap.end() )
		{
			printf("Crc data not found.  Unable to generate web files.\n");
			return;
		}

		const unsigned long imageCrc = crcMap[&firstImageName];

		//Check all other files
		for(size_t secondFileIndex = fileIndex + 1; secondFileIndex < numImageFiles; ++secondFileIndex)
		{
			const FileNameContainer* pSecondFile = &imageFiles[secondFileIndex];
			if( crcMap.find(pSecondFile) == crcMap.end() )
			{
				printf("Crc data not found.  Unable to generate web files.\n");
				return;
			}

			if( crcMap[pSecondFile] == imageCrc )
			{
				duplicatesMap[&firstImageName].push_back( imageFiles[secondFileIndex].mFullPath );
			}
		}
	}

	//Print out duplicate info
	const string newLine("\n");
	for(map<const FileNameContainer*, vector<string>>::const_iterator mapIter = duplicatesMap.begin(); mapIter != duplicatesMap.end(); ++mapIter)
	{
		fprintf(outFile.GetFileHandle(), "%s %lu\n", mapIter->first->mFullPath.c_str(), crcMap[mapIter->first]);

		for(const string& dupFileName : mapIter->second)
		{
			fprintf(outFile.GetFileHandle(), "%s %lu\n", dupFileName.c_str(), crcMap[mapIter->first]);
		}
	}
}

struct CompressedImagePatcher
{
	bool Patch(const unsigned long inOffset, const unsigned long inOriginalCompressedSize, const string& inTranslatedDataDirectory, const char* pTranlsatedImageName, const char* pOutBuffer, 
			   const unsigned long inBufferSize)
	{
		const string patchedOptionsSubmenuImagePath = inTranslatedDataDirectory + pTranlsatedImageName;
		BmpToSaturnConverter patchedOptionsSubmenuImage;
		if( !patchedOptionsSubmenuImage.ConvertBmpToSakuraFormat(patchedOptionsSubmenuImagePath, true, BmpToSaturnConverter::CYAN) )
		{
			printf("CompressedImagePatcher: Couldn't convert image: %s.\n", patchedOptionsSubmenuImagePath.c_str());
			return false;
		}

		SakuraCompressedData translatedOptionsSubmenuData;
		translatedOptionsSubmenuData.PatchDataInMemory(patchedOptionsSubmenuImage.GetImageData(), patchedOptionsSubmenuImage.GetImageDataSize(), true, false, inOriginalCompressedSize);
		if( translatedOptionsSubmenuData.mDataSize != inOriginalCompressedSize )
		{
			printf("CompressedImagePatcher failed. Compressed data for OptionsSubmenu image size[%i] is larger than the original[%i\n", translatedOptionsSubmenuData.mDataSize, inOriginalCompressedSize);
			return false;
		}

		errno_t errorValue = memcpy_s( (void*)(pOutBuffer + inOffset), inBufferSize, translatedOptionsSubmenuData.mpCompressedData, inOriginalCompressedSize);

		return errorValue == 0;
	}
};

bool FixupSakura(const string& rootDir, const string& inTranslatedOptionsBmp, const string& patchedPalettePath, const string& inTranslatedDataDirectory)
{
	const string filePath = rootDir + string("SAKURA");	
	FileData origSakuraData;
	if( !origSakuraData.InitializeFileData("SAKURA", filePath.c_str()) )
	{
		printf("FixupSakura: Unable to open SAKURA file.\n");
		return false;
	}
	
	//Load new palette data
	FileData newPaletteData;
	if( !newPaletteData.InitializeFileData(patchedPalettePath.c_str(), patchedPalettePath.c_str()) )
	{
		printf("Unable to patch palettes because new palette not found.\n");
		return false;
	}

	//***Patch Text Drawing Code***
	//const int offsetTileDim     = 0x0001040A;
	const int offsetReadCharWhenScrolling1  = 0x000104b6;
	const int offsetReadCharWhenScrolling2  = 0x000104c6;
	const int offsetReadChar                = 0x00010722;
	const int offsetTileSpacingX            = 0x000104e5;
	const int offsetTileSpacingY            = 0x000104D7;
	const int offsetTileSpacingX2           = 0x00010747;
	const int offsetTileSpacingY2           = 0x00010733;
	const int offsetTileSpacingLIPSX        = 0x0001066B;
	const int offsetLIPSX_Formatting1       = 0x00010612;
	const int offsetLIPSX_Formatting2       = 0x00010646;
	const int offsetLipsStartLocationY      = 0x000105FB; //Change the cpommand from MOV 0xCC(-52), r11 to 0xCE(-50)
	const int offsetLipsStartLocationY2     = 0x00010603; //Change the cpommand from MOV 0xC4, r11 to 0xC6
	
	const unsigned short lipsFormattingCode = 0x0041; //0x4100 in big endian.  This is equivalent to SHLL R1
	const unsigned char lipsStartLocY       = 0xCE; //When there are 2 options
	const unsigned char lipsStartLocY2      = 0xC6; //When there are 3 options

	memcpy_s((void*)(origSakuraData.GetData() + offsetTileSpacingX),      origSakuraData.GetDataSize(), (void*)&OutTileSpacingX,    sizeof(OutTileSpacingX));
	memcpy_s((void*)(origSakuraData.GetData() + offsetTileSpacingY),      origSakuraData.GetDataSize(), (void*)&OutTileSpacingY,    sizeof(OutTileSpacingY));
	memcpy_s((void*)(origSakuraData.GetData() + offsetTileSpacingX2),     origSakuraData.GetDataSize(), (void*)&OutTileSpacingX,    sizeof(OutTileSpacingX));
	memcpy_s((void*)(origSakuraData.GetData() + offsetTileSpacingY2),     origSakuraData.GetDataSize(), (void*)&OutTileSpacingY,    sizeof(OutTileSpacingY));
	memcpy_s((void*)(origSakuraData.GetData() + offsetTileSpacingLIPSX),  origSakuraData.GetDataSize(), (void*)&OutTileSpacingX,    sizeof(OutTileSpacingX));
	memcpy_s((void*)(origSakuraData.GetData() + offsetLIPSX_Formatting1), origSakuraData.GetDataSize(), (void*)&lipsFormattingCode, sizeof(lipsFormattingCode));
	memcpy_s((void*)(origSakuraData.GetData() + offsetLIPSX_Formatting2), origSakuraData.GetDataSize(), (void*)&lipsFormattingCode, sizeof(lipsFormattingCode));
	memcpy_s((void*)(origSakuraData.GetData() + offsetLipsStartLocationY),origSakuraData.GetDataSize(), (void*)&lipsStartLocY,      sizeof(lipsStartLocY));
	memcpy_s((void*)(origSakuraData.GetData() + offsetLipsStartLocationY2),origSakuraData.GetDataSize(), (void*)&lipsStartLocY2,    sizeof(lipsStartLocY2));

#if USE_SINGLE_BYTE_LOOKUPS
#define SakAdd(x) (x - sakuraLoadAddress)
	const unsigned int sakuraLoadAddress = 0x06004000;

	const unsigned short readCharWhenScrolling1 = 0xb061; //61b0 in big endian. mov.b instead of mov.w
	const unsigned short readCharWhenScrolling2 = 0xbc01; //01bd in big endian. mov.b instead of mov.w
	const unsigned short readChar               = 0xdc01; //01dc in big endian. mov.b instead of mov.w
	const unsigned short getStringAddress       = 0x0078; //7800 in big endian
	const unsigned short newLineCharacter       = 0x0d00; //00d0 in big endian.  Since we only output the second byte of 0x0a0d, the game code should just check for 0x0d as a newline character
	const unsigned short add_0_r0               = 0x0070; //7000 in big endian.  add 0x00, r0
	const unsigned short extu_b_r1_r1           = 0x1c61; //611c in big endian. extu.b r1, r1 instead of extu.w
	const unsigned short extu_b_r1_r2           = 0x1c62; //621c in big endian. extu.b r1, r1 instead of extu.w
	const unsigned short extu_b_r1_r6           = 0x1c66; //661c in big endian. extu.b r1, r6 instead of extu.w
	const unsigned short add_0_r4               = 0x0074; //7400 in big endian. 
	const unsigned short mov_b_atR0_r1_r1       = 0x1c01; //011c in big endian.  move.b @(r0, r1), r1
	const unsigned short mov_b_atR0_r10_r1      = 0xac01; //01ac in big endian.  move.b @(r0, r10), r1
	const unsigned short mov_b_atR0_r11_r1      = 0xbc01; //01bc in big endian.
	const unsigned short mov_b_r1_atR11         = 0x102b; //2b10 in big endian.  move.b r1, @r11
	const unsigned short mov_b_atR10_r1         = 0xa061; //61a0 in big endian

	memcpy_s((void*)(origSakuraData.GetData() + offsetReadCharWhenScrolling1), origSakuraData.GetDataSize(), (void*)&readCharWhenScrolling1, sizeof(readCharWhenScrolling1)); //060144B6
	memcpy_s((void*)(origSakuraData.GetData() + offsetReadCharWhenScrolling2), origSakuraData.GetDataSize(), (void*)&readCharWhenScrolling2, sizeof(readCharWhenScrolling2)); //060144C6
	memcpy_s((void*)(origSakuraData.GetData() + 0x000107fC),                   origSakuraData.GetDataSize(), (void*)&getStringAddress,       sizeof(getStringAddress));       //060147FC
	memcpy_s((void*)(origSakuraData.GetData() + 0x0001076c),                   origSakuraData.GetDataSize(), (void*)&newLineCharacter,       sizeof(newLineCharacter)); //0601476C
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010510),                   origSakuraData.GetDataSize(), (void*)&newLineCharacter,       sizeof(newLineCharacter)); //06014510
	memcpy_s((void*)(origSakuraData.GetData() + 0x000104c4),                   origSakuraData.GetDataSize(), (void*)&add_0_r0,               sizeof(add_0_r0));         //060144C4
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010720),                   origSakuraData.GetDataSize(), (void*)&add_0_r0,               sizeof(add_0_r0));         //06014720
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010722),                   origSakuraData.GetDataSize(), (void*)&readChar,               sizeof(readChar));         //06014722
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010724),                   origSakuraData.GetDataSize(), (void*)&extu_b_r1_r6,           sizeof(extu_b_r1_r6));     //06014724
	memcpy_s((void*)(origSakuraData.GetData() + 0x000104f0),                   origSakuraData.GetDataSize(), (void*)&add_0_r0,               sizeof(add_0_r0));         //060144F0
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010a4c),                   origSakuraData.GetDataSize(), (void*)&add_0_r0,               sizeof(add_0_r0));         //06014A4C
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010bdc),                   origSakuraData.GetDataSize(), (void*)&add_0_r0,               sizeof(add_0_r0));         //06014BDC

	memcpy_s((void*)(origSakuraData.GetData() + 0x000104b8),                   origSakuraData.GetDataSize(), (void*)&extu_b_r1_r1,           sizeof(extu_b_r1_r1));     //060144B8
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010bde),                   origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r1_r1,       sizeof(mov_b_atR0_r1_r1)); //06014BDE
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010be0),                   origSakuraData.GetDataSize(), (void*)&extu_b_r1_r1,           sizeof(extu_b_r1_r1));     //06014BE0
	memcpy_s((void*)(origSakuraData.GetData() + 0x000104c8),                   origSakuraData.GetDataSize(), (void*)&extu_b_r1_r6,           sizeof(extu_b_r1_r6));     //060144C8

	//For LIPS
	memcpy_s((void*)(origSakuraData.GetData() + 0x000106a4),                   origSakuraData.GetDataSize(), (void*)&newLineCharacter,       sizeof(newLineCharacter)); //060146A4
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010bf4),                   origSakuraData.GetDataSize(), (void*)&newLineCharacter,       sizeof(newLineCharacter)); //06014BF4

	//060145a4 mov.w @(r0, r10), r1
	//060145c8 add r0, r0
	//060145ca mov.w @(r0, r10), r1
	//060145cc extu.w r1, r1
	memcpy_s((void*)(origSakuraData.GetData() + 0x000105a4),                   origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r10_r1,      sizeof(mov_b_atR0_r10_r1)); //060145A4
	memcpy_s((void*)(origSakuraData.GetData() + 0x000105c8),                   origSakuraData.GetDataSize(), (void*)&add_0_r0,               sizeof(add_0_r0));          //060145C8
	memcpy_s((void*)(origSakuraData.GetData() + 0x000105ca),                   origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r10_r1,      sizeof(mov_b_atR0_r10_r1)); //060145CA
	memcpy_s((void*)(origSakuraData.GetData() + 0x000105cc),                   origSakuraData.GetDataSize(), (void*)&extu_b_r1_r1,           sizeof(extu_b_r1_r1));      //060145CC

	//Fixes around 06014594
	memcpy_s((void*)(origSakuraData.GetData() + 0x000106d6),                   origSakuraData.GetDataSize(), (void*)&add_0_r4,               sizeof(add_0_r4));          //060146D6

	//Fixes around 060145a4
	memcpy_s((void*)(origSakuraData.GetData() + 0x000105a2),                   origSakuraData.GetDataSize(), (void*)&add_0_r0,               sizeof(add_0_r0));          //060145A2
	memcpy_s((void*)(origSakuraData.GetData() + 0x000105a6),                   origSakuraData.GetDataSize(), (void*)&extu_b_r1_r1,           sizeof(extu_b_r1_r1));      //060145A6

	//Fixes around 0601461a
	memcpy_s((void*)(origSakuraData.GetData() + 0x0001061c),                   origSakuraData.GetDataSize(), (void*)&extu_b_r1_r1,           sizeof(extu_b_r1_r1));      //060145A6

	//Fixes around 0601462a
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010628),                   origSakuraData.GetDataSize(), (void*)&add_0_r0,               sizeof(add_0_r0));          //0601461C
	memcpy_s((void*)(origSakuraData.GetData() + 0x0001062c),                   origSakuraData.GetDataSize(), (void*)&extu_b_r1_r6,           sizeof(extu_b_r1_r6));      //0601462C

	//Fixes around 06014676
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010676),                   origSakuraData.GetDataSize(), (void*)&add_0_r0,               sizeof(add_0_r0));          //06014676
	memcpy_s((void*)(origSakuraData.GetData() + 0x0001067a),                   origSakuraData.GetDataSize(), (void*)&extu_b_r1_r1,           sizeof(extu_b_r1_r1));      //0601467A

	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x06014a40)),           origSakuraData.GetDataSize(), (void*)&add_0_r0,               sizeof(add_0_r0));          //06014a40
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x06014a42)),           origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r1_r1,       sizeof(mov_b_atR0_r1_r1));  //06014a42
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x06014a44)),           origSakuraData.GetDataSize(), (void*)&mov_b_r1_atR11,         sizeof(mov_b_r1_atR11));    //06014a44
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x06014a4e)),           origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r1_r1,       sizeof(mov_b_atR0_r1_r1));  //06014a4e
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x06014a60)),           origSakuraData.GetDataSize(), (void*)&extu_b_r1_r2,           sizeof(extu_b_r1_r2));      //06014a50
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x060144f2)),           origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r11_r1,      sizeof(mov_b_atR0_r11_r1)); //060144f2
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x060144f4)),           origSakuraData.GetDataSize(), (void*)&extu_b_r1_r1,           sizeof(extu_b_r1_r1));      //060144f2

	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x06014594)),           origSakuraData.GetDataSize(), (void*)&mov_b_atR10_r1,         sizeof(mov_b_atR10_r1));    //06014594
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x06014596)),           origSakuraData.GetDataSize(), (void*)&extu_b_r1_r1,           sizeof(extu_b_r1_r1));      //06014596
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x060145a4)),           origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r10_r1,      sizeof(mov_b_atR0_r10_r1)); //060145a4
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x0601461a)),           origSakuraData.GetDataSize(), (void*)&mov_b_atR10_r1,         sizeof(mov_b_atR10_r1));    //0601461a
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x0601462a)),           origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r10_r1,      sizeof(mov_b_atR0_r10_r1)); //0601462a
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x06014678)),           origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r10_r1,      sizeof(mov_b_atR0_r10_r1)); //06014678

	//Fixes around 0601463e
//	memcpy_s((void*)(origSakuraData.GetData() + 0x0001063e),                   origSakuraData.GetDataSize(), (void*)&add_0_r0,               sizeof(add_0_r0));          //601463E
//	memcpy_s((void*)(origSakuraData.GetData() + 0x00010642),                   origSakuraData.GetDataSize(), (void*)&extu_b_r1_r1,           sizeof(extu_b_r1_r1));      //6014642
	
#endif

#if USE_4_BYTE_OFFSETS
	const unsigned short mov_l_atR1plus_r2 = 0x1662;
	const unsigned short mov_r2_r2         = 0x2362;
	const unsigned short mov_l_atR8_r1     = 0x8261; //6182
	const unsigned short no_op             = 0x0900;
	const unsigned short add_4_r8          = 0x0478;
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x060147dc)),           origSakuraData.GetDataSize(), (void*)&mov_l_atR1plus_r2,       sizeof(mov_l_atR1plus_r2));    //060147dc
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x060147e0)),           origSakuraData.GetDataSize(), (void*)&mov_r2_r2,               sizeof(mov_r2_r2));            //060147e0
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x060147ea)),           origSakuraData.GetDataSize(), (void*)&mov_l_atR8_r1,           sizeof(mov_l_atR8_r1));        //060147ea
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x060147ec)),           origSakuraData.GetDataSize(), (void*)&no_op,                   sizeof(no_op));                //060147ec
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x060147f4)),           origSakuraData.GetDataSize(), (void*)&add_4_r8,                sizeof(add_4_r8));             //060147f4


	/*
	060147f6 mov.w @r8, r1 0x000107f0
	060147fa extu.w r1, r8 0x000107fa
	060147e8 add 4, r8     0x000107e8  //should be add 6
	060147e2 mov.w @r1, r1 0x000107e2
	060147e6 extu.w r1,r9  0x000107e6
	06014806 add 4, r8
	06014808 add 0xfc, r8
	*/

	const unsigned short mov_r1_r8      = 0x1368; //6113 basically do nothing, because mov_l_atR8_r1 is all we want   
	const unsigned short add_8_r8       = 0x0878; //7806
	const unsigned short move_r1_r9     = 0x1369; //6913
	const unsigned short add_f8_r8      = 0xf878; //78f8
	const unsigned short mov_l_atR1_r1  = 0x1261; //6112

	memcpy_s((void*)(origSakuraData.GetData() + 0x000107f6), origSakuraData.GetDataSize(), (void*)&mov_l_atR8_r1,  sizeof(mov_l_atR8_r1));   //060147f6
	memcpy_s((void*)(origSakuraData.GetData() + 0x000107fa), origSakuraData.GetDataSize(), (void*)&mov_r1_r8,      sizeof(mov_r1_r8));       //060147fa
	memcpy_s((void*)(origSakuraData.GetData() + 0x000107e8), origSakuraData.GetDataSize(), (void*)&add_8_r8,       sizeof(add_8_r8));        //060147e8
	memcpy_s((void*)(origSakuraData.GetData() + 0x000107e2), origSakuraData.GetDataSize(), (void*)&mov_l_atR1_r1,  sizeof(mov_l_atR1_r1));   //060147e2
	memcpy_s((void*)(origSakuraData.GetData() + 0x000107e6), origSakuraData.GetDataSize(), (void*)&move_r1_r9,     sizeof(move_r1_r9));      //060147e6
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010806), origSakuraData.GetDataSize(), (void*)&add_8_r8,       sizeof(add_8_r8));        //06014806
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010808), origSakuraData.GetDataSize(), (void*)&add_f8_r8,      sizeof(add_f8_r8));       //06014808

	//LIPS
	/*
	060146c2 add 4, r4
	060146d0 mov.w @r4, r1
	060146e0 add 4, r4
	060146e2 add 0xfc, r4
	060146d4 extu.w r1, r4
	06014594 mov @r10, r1             0x00010594
	0601461a mov @r10, r1             0x0001061a
	0601462a mov mov.w @(r0, r10), r1 0x0001062a
	06014678 mov mov.w @(r0, r10), r1 0x00010678
	06014640 mov.w @(r0, r14), r1     0x00010640
	*/
	
	const unsigned short add_8_r4      = 0x0874; //7406
	const unsigned short mov_l_r4_r1   = 0x4261; //6142
	const unsigned short add_f8_r4     = 0xf874; //74fa
	const unsigned short mov_r1_r4     = 0x1364; //6413
	const unsigned short mov_b_atR0_r1 = 0xa161; //01ac
	const unsigned short mov_b_atR0_r14_r1 = 0xec01; //01ec
	const unsigned short mov_l_atR4_r1     = 0x4261;
	const unsigned short mov_r1_r3         = 0x1363;
	const unsigned short add_4_r4          = 0x0474;

	memcpy_s((void*)(origSakuraData.GetData() + 0x000106c2),         origSakuraData.GetDataSize(), (void*)&add_8_r4,           sizeof(add_8_r4));
	memcpy_s((void*)(origSakuraData.GetData() + 0x000106D0),         origSakuraData.GetDataSize(), (void*)&mov_l_r4_r1,        sizeof(mov_l_r4_r1));            //060146D0
	memcpy_s((void*)(origSakuraData.GetData() + 0x000106e0),         origSakuraData.GetDataSize(), (void*)&add_8_r4,           sizeof(add_8_r4));
	memcpy_s((void*)(origSakuraData.GetData() + 0x000106e2),         origSakuraData.GetDataSize(), (void*)&add_f8_r4,          sizeof(add_f8_r4));
	memcpy_s((void*)(origSakuraData.GetData() + 0x000106d4),         origSakuraData.GetDataSize(), (void*)&mov_r1_r4,          sizeof(mov_r1_r4));
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010594),         origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r1,      sizeof(mov_b_atR0_r1));
	memcpy_s((void*)(origSakuraData.GetData() + 0x0001061a),         origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r1,      sizeof(mov_b_atR0_r1));
	memcpy_s((void*)(origSakuraData.GetData() + 0x0001062a),         origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r10_r1,  sizeof(mov_b_atR0_r10_r1));
	memcpy_s((void*)(origSakuraData.GetData() + 0x00010678),         origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r10_r1,  sizeof(mov_b_atR0_r10_r1));
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x060146ba)), origSakuraData.GetDataSize(), (void*)&mov_l_atR4_r1,      sizeof(mov_l_atR4_r1));            //060146ba
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x060146be)), origSakuraData.GetDataSize(), (void*)&mov_r1_r3,          sizeof(mov_r1_r3));                //060146be
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x060146c4)), origSakuraData.GetDataSize(), (void*)&mov_l_atR4_r1,      sizeof(mov_l_atR4_r1));            //060146c4
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x060146c6)), origSakuraData.GetDataSize(), (void*)&no_op,              sizeof(no_op));                    //060146c6
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x060146ce)), origSakuraData.GetDataSize(), (void*)&add_4_r4,           sizeof(add_4_r4));                 //060146ce

//	memcpy_s((void*)(origSakuraData.GetData() + 0x00010640), origSakuraData.GetDataSize(), (void*)&mov_b_atR0_r14_r1,  sizeof(mov_b_atR0_r14_r1));

	//Menu items
	/*
	06014530 add 4, r6     //offsetting from tbl header to get to the first string id.  Needs to be add 6
	0601454e add 4, r6     //adding 4 to get to the next string id
	06014550 add 0xfc, r6  //move back by 4, should be 0xfa
	0601453e mov.w @r6, r1 //read the offset to the string
	06014542 extu.w r1, r6 //extu the offset should be mov r1, r6, 6613
	06014544 add r6, r6    //multiply offset by 2, should be add 0, r6 7600
	06014528 mov.w @r6, r1 //mov 00224000 to r1
	*/
	const unsigned short add_8_r6      = 0x0876;
	const unsigned short add_f8_r6     = 0xf876;
	const unsigned short mov_l_atR6_r1 = 0x6261;
	const unsigned short mov_r1_r6     = 0x1366;
	const unsigned short add_0_r6      = 0x0076;
	const unsigned short add_4_r6      = 0x0476;
	memcpy_s((void*)(origSakuraData.GetData() + (0x06014530 - sakuraLoadAddress)), origSakuraData.GetDataSize(), (void*)&add_8_r6,      sizeof(add_8_r6));
	memcpy_s((void*)(origSakuraData.GetData() + (0x0601454e - sakuraLoadAddress)), origSakuraData.GetDataSize(), (void*)&add_8_r6,      sizeof(add_8_r6));
	memcpy_s((void*)(origSakuraData.GetData() + (0x06014550 - sakuraLoadAddress)), origSakuraData.GetDataSize(), (void*)&add_f8_r6,     sizeof(add_f8_r6));
	memcpy_s((void*)(origSakuraData.GetData() + (0x0601453e - sakuraLoadAddress)), origSakuraData.GetDataSize(), (void*)&mov_l_atR6_r1, sizeof(mov_l_atR6_r1));
	memcpy_s((void*)(origSakuraData.GetData() + (0x06014542 - sakuraLoadAddress)), origSakuraData.GetDataSize(), (void*)&mov_r1_r6,     sizeof(mov_r1_r6));
	memcpy_s((void*)(origSakuraData.GetData() + (0x06014544 - sakuraLoadAddress)), origSakuraData.GetDataSize(), (void*)&add_0_r6,      sizeof(add_0_r6));
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x06014528)),               origSakuraData.GetDataSize(), (void*)&mov_l_atR6_r1, sizeof(mov_l_atR6_r1));   //06014528
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x0601452c)),               origSakuraData.GetDataSize(), (void*)&mov_r1_r3,     sizeof(mov_r1_r3));       //0601452c
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x06014532)),               origSakuraData.GetDataSize(), (void*)&mov_l_atR6_r1, sizeof(mov_l_atR6_r1));   //06014532
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x06014534)),               origSakuraData.GetDataSize(), (void*)&no_op,         sizeof(no_op));           //06014534
	memcpy_s((void*)(origSakuraData.GetData() + SakAdd(0x0601453c)),               origSakuraData.GetDataSize(), (void*)&add_4_r6,      sizeof(add_4_r6));        //0601453c
	
#endif
	//***Done Patching Text Drawing Code***

	//****Patch Palettes****
	const int offsetPalette1    = GIsDisc2 ? 0x00053a8c : 0x00053954;
//	const int offsetPalette2    = 0x0005ADB8;
	const int offsetLIPSPalette = GIsDisc2 ? 0x00053aac : 0x00053974;
	memcpy_s((void*)(origSakuraData.GetData() + offsetPalette1), origSakuraData.GetDataSize(), newPaletteData.GetData(), newPaletteData.GetDataSize());
//	memcpy_s((void*)(origSakuraData.GetData() + offsetPalette2), origSakuraData.GetDataSize(), newPaletteData.GetData(), newPaletteData.GetDataSize());
	memcpy_s((void*)(origSakuraData.GetData() + offsetLIPSPalette), origSakuraData.GetDataSize(), newPaletteData.GetData(), newPaletteData.GetDataSize());
	//***Done Patching Palettes***

	//****Patch Options Image****
	//Compress bgnd image
	BmpToSaturnConverter patchedOptionsImage;
	if( !patchedOptionsImage.ConvertBmpToSakuraFormat(inTranslatedOptionsBmp, true, BmpToSaturnConverter::CYAN) )
	{
		printf("FixupSakura: Couldn't convert image: %s.\n", inTranslatedOptionsBmp.c_str());
		return false;
	}

	//Read in original options image
	const unsigned long origOptionsCompressedSize  = 1256;
	const unsigned int  origOptionsOffset = GIsDisc2 ? 0x0005AF70 : 0x0005AE38;
	char* pOriginalOptionsImageData = new char[origOptionsCompressedSize];
	memcpy_s(pOriginalOptionsImageData, origOptionsCompressedSize, (void*)(origSakuraData.GetData() + origOptionsOffset), origOptionsCompressedSize);
	
	//Uncompress the original data
	PRSDecompressor uncompressedBgndImage;
	uncompressedBgndImage.UncompressData((void*)pOriginalOptionsImageData, origOptionsCompressedSize);
	delete[] pOriginalOptionsImageData;
	//Done reading in original image

	//Convert patched image into saturn format and compress it
	const unsigned int origImageDataSize = 144*288/2;
	const unsigned int patchedImageSize  = patchedOptionsImage.mTileExtractor.mTiles[0].mTileSize;
	const unsigned int newImageDataSize  = patchedImageSize + (uncompressedBgndImage.mUncompressedDataSize - origImageDataSize);
	if( newImageDataSize != uncompressedBgndImage.mUncompressedDataSize )
	{
		printf("FixupSakura failed. Data size mismatch");
		return false;
	}
	char* pNewImageData = new char[newImageDataSize];
	//memcpy_s(pNewImageData, newImageDataSize, uncompressedBgndImage.mpUncompressedData, uncompressedBgndImage.mUncompressedDataSize);
	
	memset(pNewImageData, 0, newImageDataSize);
	memcpy_s(pNewImageData, newImageDataSize, patchedOptionsImage.mTileExtractor.mTiles[0].mpTile, patchedImageSize);
	memcpy_s(pNewImageData + patchedImageSize, newImageDataSize - patchedImageSize, uncompressedBgndImage.mpUncompressedData + origImageDataSize, uncompressedBgndImage.mUncompressedDataSize - origImageDataSize);
	
	SakuraCompressedData translatedOptionsData;
	translatedOptionsData.PatchDataInMemory(pNewImageData, newImageDataSize, true, false, origOptionsCompressedSize);
	delete[] pNewImageData;

	//Create new sakura image data
	const unsigned int optionsSizeDiff = origOptionsCompressedSize < translatedOptionsData.mDataSize ? translatedOptionsData.mDataSize - origOptionsCompressedSize : 0;
	const unsigned int newSakuraLength = origSakuraData.GetDataSize() + optionsSizeDiff;
	char* pNewSakuraData = new char[newSakuraLength];

	if( optionsSizeDiff > 0 )
	{
		printf("FixupSakura failed. Compressed data is larger than original\n");
		return false;
	}
	//***Copy over the original data upto the options image***
	//Copy everything up to the options data
	memcpy_s(pNewSakuraData, newSakuraLength, origSakuraData.GetData(), origOptionsOffset);

	//copy new options image data
	memcpy_s(pNewSakuraData + origOptionsOffset, newSakuraLength - origOptionsOffset, translatedOptionsData.mpCompressedData, translatedOptionsData.mDataSize);

	//copy everything after original options data
	memcpy_s(pNewSakuraData + origOptionsOffset + translatedOptionsData.mDataSize, newSakuraLength - origOptionsOffset - translatedOptionsData.mDataSize, origSakuraData.GetData() + origOptionsOffset + origOptionsCompressedSize, origSakuraData.GetDataSize() - origOptionsOffset - origOptionsCompressedSize);
	//**done copying original data to new data

	//***Options Submenu Image***
	{
		const int offsetToImage = GIsDisc2 ? 0x00059544 : 0x0005940c;
		CompressedImagePatcher imagePatcher;
		if( !imagePatcher.Patch(offsetToImage, 432, inTranslatedDataDirectory, "OptionsSubmenu.bmp", pNewSakuraData, newSakuraLength) )
		{
			return false;
		}
	}
	//***Done with OptionsSubmenu***

	//***Systems Submenu Image***
	{
		const int offsetToImage = GIsDisc2 ? 0x000596f4 : 0x000595bc; //Disc1 and Disc2 have the same offset
		CompressedImagePatcher imagePatcher;
		if( !imagePatcher.Patch(offsetToImage, 324, inTranslatedDataDirectory, "SystemMenu.bmp", pNewSakuraData, newSakuraLength) )
		{
			return false;
		}
	}
	//***Done with Systems Submenu***

	//***Systems Submenu Image***
	{
		//This one has a header compressed with it which we want to retain, so uncompress original data, patch new data, copy it into the original data, and then compress that
		const unsigned int origOffset = GIsDisc2 ? 0x0005e2c8 : 0x0005e190;//0x0005e2bf : 0x0005e187;
		const unsigned int origCompressedSize = 2292;
		PRSDecompressor uncompressedData;
		uncompressedData.UncompressData((void*)(origSakuraData.GetData() + origOffset), origCompressedSize);
		
		const string translatedImagePath = inTranslatedDataDirectory + "LoadMenu.bmp";
		BmpToSaturnConverter translatedImage;
		if( !translatedImage.ConvertBmpToSakuraFormat(translatedImagePath, true, BmpToSaturnConverter::CYAN) )
		{
			printf("FixupSakura: Couldn't convert image: %s.\n", translatedImagePath.c_str());
			return false;
		}

		//Backup ram
		const string cartRamImagePath = inTranslatedDataDirectory + "CartridgeRam.bmp";
		BmpToSaturnConverter cartRamImage;
		if( !cartRamImage.ConvertBmpToSakuraFormat(cartRamImagePath, true, BmpToSaturnConverter::CYAN) )
		{
			printf("FixupSakura: Couldn't convert image: %s.\n", cartRamImagePath.c_str());
			return false;
		}

		//Backup ram
		const string copyFileImagePath = inTranslatedDataDirectory + "CopyFile.bmp";
		BmpToSaturnConverter copyFileImage;
		if( !copyFileImage.ConvertBmpToSakuraFormat(copyFileImagePath, true, BmpToSaturnConverter::CYAN) )
		{
			printf("FixupSakura: Couldn't convert image: %s.\n", copyFileImagePath.c_str());
			return false;
		}

		//Maing bgnd
		if( memcpy_s(uncompressedData.mpUncompressedData + 0, uncompressedData.mUncompressedDataSize - 0, translatedImage.GetImageData(), translatedImage.GetImageDataSize()) != 0 )
		{
			printf("FixupSakura: Unable to copy translated LoadMenu.bmp into original data buffer.\n");
			return false;
		}

		//Cartridge ram bgnd
		if( memcpy_s(uncompressedData.mpUncompressedData + 0x9780, uncompressedData.mUncompressedDataSize - 0x9780, cartRamImage.GetImageData(), cartRamImage.GetImageDataSize()) != 0 )
		{
			printf("FixupSakura: Unable to copy translated CartridgeRam.bmp into original data buffer.\n");
			return false;
		}

		//Copy file image
		if( memcpy_s(uncompressedData.mpUncompressedData + 0xa280, uncompressedData.mUncompressedDataSize - 0xa280, copyFileImage.GetImageData(), copyFileImage.GetImageDataSize()) != 0 )
		{
			printf("FixupSakura: Unable to copy translated CartridgeRam.bmp into original data buffer.\n");
			return false;
		}

		SakuraCompressedData compressedData;
		compressedData.PatchDataInMemory(uncompressedData.mpUncompressedData, uncompressedData.mUncompressedDataSize, true, false, origCompressedSize);
		if( compressedData.mDataSize != origCompressedSize )
		{
			printf("CompressedImagePatcher failed. Compressed data for LoadMenu image size[%i] is larger than the original[%i\n", compressedData.mDataSize, origCompressedSize);
			return false;
		}

		memcpy_s(pNewSakuraData + origOffset, newSakuraLength - origOffset, compressedData.mpCompressedData, compressedData.mDataSize);
	}
	//***Done with Systems Submenu***

	//Patch palette
	//Options image palette
	//const int optionsPaletteOffset = GIsDisc2 ? 0x0005aed0 : 0x0005AD98;
	//memcpy_s(pNewSakuraData + optionsPaletteOffset + optionsSizeDiff, newSakuraLength - optionsPaletteOffset + optionsSizeDiff, patchedOptionsImage.mPalette.GetData(), patchedOptionsImage.mPalette.GetSize());

	//const unsigned short tileSize  = (OutTileSpacingY << 8) + (OutTileSpacingX/8);

	//fseek(pFile, offsetTileDim, SEEK_SET);
	//fwrite(&tileSize, sizeof(tileSize), 1, pFile);

	//***Patch Dialog Options Menu***
	{
		const unsigned long origDialogOptionsCompressedSize = 376;
		const unsigned int origDialogOptionsOffset = GIsDisc2 ? 0x00059838 : 0x00059700;

		string translatedPausedOptionsImagePath = inTranslatedDataDirectory + string("PauseOptions.bmp");
		BmpToSaturnConverter patchedPausedOptionsImage;
		if( !patchedPausedOptionsImage.ConvertBmpToSakuraFormat(translatedPausedOptionsImagePath, false, BmpToSaturnConverter::WHITE) )
		{
			printf("FixupSakura: Couldn't convert image: %s.\n", translatedPausedOptionsImagePath.c_str());
			return false;
		}

		SakuraCompressedData translatedPausedOptionsData;
		translatedPausedOptionsData.PatchDataInMemory(patchedPausedOptionsImage.GetImageData(), patchedPausedOptionsImage.GetImageDataSize(), true, false, origDialogOptionsCompressedSize);

		memcpy_s(pNewSakuraData + origDialogOptionsOffset, origDialogOptionsCompressedSize, translatedPausedOptionsData.mpCompressedData, translatedPausedOptionsData.mDataSize);
	}
	//***Done Patching Dialog Options Menu***

	//***Game options during gameplay***
	{
		const unsigned long origGameOptionsCompressedSize = 601;
		const unsigned int origGameOptionsOffset = GIsDisc2 ? 0x000599b0 : 0x00059878;

		string translatedImagePath = inTranslatedDataDirectory + string("GameOptions.bmp");
		BmpToSaturnConverter patchedImage;
		const unsigned int gameOptionTileDim = 16;
		if( !patchedImage.ConvertBmpToSakuraFormat(translatedImagePath, false, BmpToSaturnConverter::WHITE, &gameOptionTileDim, &gameOptionTileDim) )
		{
			printf("FixupSakura: Couldn't convert image: %s.\n", translatedImagePath.c_str());
			return false;
		}

		patchedImage.PackTiles();

		SakuraCompressedData translatedCompressedData;
		translatedCompressedData.PatchDataInMemory(patchedImage.mpPackedTiles, patchedImage.mPackedTileSize, true, false, origGameOptionsCompressedSize);//patchedImage.GetImageData(), patchedImage.GetImageDataSize(), true, false, origGameOptionsCompressedSize);

		memcpy_s(pNewSakuraData + origGameOptionsOffset, origGameOptionsCompressedSize, translatedCompressedData.mpCompressedData, translatedCompressedData.mDataSize);
	}
	//***Done Patching Game Options Menu***

	//settings
	//Found: 0005b320 00000680 Size: 128 CompressedSize: 3782

	FileWriter newSakuraFile;
	if( !newSakuraFile.OpenFileForWrite(filePath) )
	{
		printf("Failed patching: Unable to open SAKURA file for write.\n");
		return false;
	}
	newSakuraFile.WriteData(pNewSakuraData, newSakuraLength);

	delete[] pNewSakuraData;
	printf("FixupSakura Succeeded.\n");

	return true;
}

void OutputDialogOrder(const string& rootSakuraTaisenDirectory, const string& outputDirectory)
{
	map<string, DialogOrder> dialogOrder;
	FindDialogOrder(rootSakuraTaisenDirectory, dialogOrder);

	for(map<string, DialogOrder>::const_iterator iter = dialogOrder.begin(); iter != dialogOrder.end(); ++iter)
	{
		const string outFileName = outputDirectory + iter->first + string("_DialogOrder.txt");
		TextFileWriter outFile;
		if( !outFile.OpenFileForWrite(outFileName) )
		{
			continue;
		}

		for(map<int, unsigned short>::const_iterator secondIter = iter->second.orderAndId.begin(); secondIter != iter->second.orderAndId.end(); ++secondIter)
		{
			if(secondIter->second >> 8 == 0x9C)
			{
				continue;
			}

			const unsigned short id                                   = secondIter->second;
			DialogOrder::IdAndImageMap::const_iterator idAndImageIter = iter->second.idAndImage.find(id);
			const unsigned short imageIndex                           = idAndImageIter != iter->second.idAndImage.end() ? idAndImageIter->second : 0;
			fprintf(outFile.GetFileHandle(), "ID: %02x, Image: %02x\n", secondIter->second, imageIndex);
		}
	}	
}

bool CreateTMapSpSpreadsheet(const string& imageDirectory)
{
	TextFileWriter htmlFile;
	const string htmlFileName = imageDirectory + string("..\\..\\Translation\\TMapSP.php");
	if( !htmlFile.OpenFileForWrite(htmlFileName) )
	{
		printf("Unable to create an html file: %s", htmlFileName.c_str());
		return false;
	}

	htmlFile.WriteString("<html>\n");
	htmlFile.WriteString("	<head><style>textarea {width: 100%;top: 0; left: 0; right: 0; bottom: 0; position: absolute; resize: none;-webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;} table {border-collapse: collapse;} table, th, td { position: relative; border: 1px solid black;}#myProgress {width: 100%;	background-color: #ddd;} #myBar {width: 1%;height: 30px; background-color: #4CAF50;}</style>\n");
	htmlFile.WriteString("	<div id=\"FileName\" style=\"display: none;\">TMapSP</div>\n");
	htmlFile.WriteString("	<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.1/jquery.min.js\">\n");
	htmlFile.WriteString("		$( window ).on( \"load\", function()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			OnStartup();\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("	</script>\n\n");
	htmlFile.WriteString("	<script type=\"text/javascript\">\n");
	htmlFile.WriteString("		function SaveData(inDialogImageName, inDivID)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			var translatedText = document.getElementById(inDivID).value;\n");
	htmlFile.WriteString("			var fileName = document.getElementById(\"FileName\").innerHTML;\n");
	htmlFile.WriteString("			$.ajax({\n");
	htmlFile.WriteString("				type: \"POST\",\n");
	htmlFile.WriteString("				url: \"UpdateTranslationTest.php\",\n");
	htmlFile.WriteString("				data: { inTBLFileName: fileName, inImageName:inDialogImageName, inTranslation:translatedText, inDivId:inDivID, inCrc:0 },\n");
	htmlFile.WriteString("				success: function(result)\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					var trId = \"tr_\" + inDivID;\n");
	htmlFile.WriteString("					if( translatedText != \"Untranslated\" && translatedText != \"<div>Untranslated</div>\")\n");
	htmlFile.WriteString("					{\n");
	htmlFile.WriteString("						if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
	htmlFile.WriteString("						{\n");
	htmlFile.WriteString("							document.getElementById(trId).bgColor = \"#e3fec8\";\n");
	htmlFile.WriteString("						}\n");
	htmlFile.WriteString("					}\n");
	htmlFile.WriteString("				else\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					document.getElementById(trId).bgColor = \"#fefec8\";\n");
	htmlFile.WriteString("				}\n");
	htmlFile.WriteString("			}\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("}\n");
	htmlFile.WriteString("		function SaveEdits(inDialogImageName, inDivID)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			SaveData(inDialogImageName, inDivID);\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("		function ExportData()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			$(\"textarea\").each ( function ()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				var thisText = $(this).text();\n");
	htmlFile.WriteString("				thisText = thisText.replace(/<br>/g, '&ltbr&gt');\n");
	htmlFile.WriteString("				thisText = thisText.replace(/<sp>/g, '&ltsp&gt');\n");
	htmlFile.WriteString("				document.write(thisText + \"<br>\");\n");
	htmlFile.WriteString("			});\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	function LoadData()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		var fileName = document.getElementById(\"FileName\").innerHTML;\n");
	htmlFile.WriteString("		$.ajax({\n");
	htmlFile.WriteString("		type: \"POST\",\n");
	htmlFile.WriteString("			  url: \"GetTranslationData.php\",\n");
	htmlFile.WriteString("				data: { inTBLFileName: fileName},\n");
	htmlFile.WriteString("				success: function(result)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			var json = $.parseJSON(result);\n");
	htmlFile.WriteString("			var i;\n");
	htmlFile.WriteString("			for (i = 0; i < json.length; i++)\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				var jsonEntry = json[i];\n");
	htmlFile.WriteString("				var english   = jsonEntry.English.replace(/\\\\/g, \'\');\n");
	htmlFile.WriteString("				var divId     = \"#\" + jsonEntry.DivId;\n");
	htmlFile.WriteString("					var trId      = \"tr_\" + jsonEntry.DivId;\n");
	htmlFile.WriteString("					if( english != \"Untranslated\" && english != \"<div>Untranslated</div>\")\n");
	htmlFile.WriteString("					{\n");
	htmlFile.WriteString("						if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
	htmlFile.WriteString("						{\n");
	htmlFile.WriteString("						       document.getElementById(trId).bgColor = \"#e3fec8\";\n");
	htmlFile.WriteString("						}\n");
	htmlFile.WriteString("						$(divId).html(english);\n");
	htmlFile.WriteString("					}\n");
	htmlFile.WriteString("			}\n");
	htmlFile.WriteString("		},\n");
	htmlFile.WriteString("		error: function()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			alert('Unable to load data');\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	function FixOnChangeEditableElements()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		var tags = document.querySelectorAll('[contenteditable=true][onChange]');\n");
	htmlFile.WriteString("		for (var i=tags.length-1; i>=0; i--) if (typeof(tags[i].onblur)!='function')\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			tags[i].onfocus = function()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				this.data_orig=this.innerHTML;\n");
	htmlFile.WriteString("			};\n");
	htmlFile.WriteString("			tags[i].onblur = function()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				if( this.innerHTML != this.data_orig)\n");
	htmlFile.WriteString("					this.onchange();\n");
	htmlFile.WriteString("				delete this.data_orig;\n");
	htmlFile.WriteString("			};\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	function OnStartup()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		FixOnChangeEditableElements();\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	</script>\n");
	htmlFile.WriteString("	</head>\n");
	htmlFile.WriteString("	\n");
	htmlFile.WriteString("	<body>\n");
	htmlFile.WriteString("	<?php include 'GetUserPermissions.php';\n");
	htmlFile.WriteString("	$bPermissionFound = false;\n");
	htmlFile.WriteString("	foreach ($allowedFiles as $value)\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		if( $value == \"TMapSP\" )\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			$bPermissionFound = true;\n");
	htmlFile.WriteString("			break;\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	if( $bPermissionFound )\n\t{\n?>");
	htmlFile.WriteString("		<article><header align=\"center\"><h1>File: TMapSP</h1></header></article>\n");
	htmlFile.WriteString("			<br>\n");
	htmlFile.WriteString("			<b>Instructions:</b><br>\n");
	htmlFile.WriteString("			-This page is best displayed using Chrome.  Otherwise some of the table borders are missing for some reason.<br>\n");
	htmlFile.WriteString("			-Your changes are automatically saved.<br>\n");
	htmlFile.WriteString("			-Press the Load Data button when you come back to the page to load your changes.<br>\n");
	htmlFile.WriteString("			<b>Naming Conventions:</n><br>\n");
	htmlFile.WriteString("			<a href=\"https://docs.google.com/spreadsheets/d/1rgafQe78vML_xbxnYuOSlO8P5C8nuhgLjMJOExUQsm0/edit?usp=sharing\" target=\"_blank\">Click here to view the naming conventions for Characters, Locations, and Terms</a> <br>\n");
	htmlFile.WriteString("			<?php\n");
	htmlFile.WriteString("				$currUser = $_SERVER['PHP_AUTH_USER'];\n");
	htmlFile.WriteString("				if( $currUser == \"swtranslator\" )\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					echo \"<input align=\\\"center\\\" type=\\\"button\\\" value=\\\"Export Data\\\" onclick=\\\"ExportData()\\\"/>\";\n");
	htmlFile.WriteString("				}\n");
	htmlFile.WriteString("			?>\n\n");
	htmlFile.WriteString("			<table align=\"center\">\n");
	htmlFile.WriteString("				<tr>\n");
	htmlFile.WriteString("					<td>\n");
	htmlFile.WriteString("						<input align=\"center\" type=\"button\" value=\"Load Data\" onclick=\"LoadData()\"/>\n");
	htmlFile.WriteString("					</td>\n");
	htmlFile.WriteString("				</tr>\n");
	htmlFile.WriteString("			</table><br>\n\n");
	htmlFile.WriteString("			<table>\n");
	htmlFile.WriteString("				<tr bgcolor=\"#c8c8fe\">\n");
	htmlFile.WriteString("					<th>#</th>\n");
	htmlFile.WriteString("					<th>Japanese</th>\n");
	htmlFile.WriteString("					<th>English</th>\n");
	htmlFile.WriteString("				</tr>\n");

	const int firstImage = 51; 
	const int lastImage  = 92;
	for(int i = firstImage; i <= lastImage; ++i)
	{
		fprintf(htmlFile.GetFileHandle(), "				<tr id=\"tr_edit_%i\" bgcolor=\"#fefec8\">\n", i);
		fprintf(htmlFile.GetFileHandle(), "					<td align=\"center\" width=\"20\">%i</td><td width=\"88\"><img src=\"..\\ExtractedData\\TMapSP\\%i.png\"></td><td width=\"480\"><textarea id=\"edit_%i\" contenteditable=true onchange=\"SaveEdits('%i.bmp', 'edit_%i')\" style=\"border: none; width: 100%%; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;\">Untranslated</textarea></td>\n", i, i, i, i, i);
		fprintf(htmlFile.GetFileHandle(), "				</tr>\n");
	}

	htmlFile.WriteString("			</table><br>\n");
	htmlFile.WriteString("			<?php\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	?></body>\n");
	htmlFile.WriteString("	</html>\n");

	return true;

}

struct EVTFileData
{
	struct DialogOrder
	{
		unsigned short stringId = 0;
		unsigned short faceId   = 0;
	};

	int GetFaceImageNumber(unsigned short stringId) const
	{
		for(const DialogOrder& order : mDialogOrder)
		{
			if( order.stringId == stringId )
			{
				assert(order.faceId < LutSize);

				return mFaceLookupTable[order.faceId] - 1;
			}
		}

		return -1;
	}

	static const int    LutSize = 512;
	unsigned char       mFaceLookupTable[LutSize];
	vector<DialogOrder> mDialogOrder;
	string              mFileName;
};

bool ParseEvtFiles(const string& sakuraRootDirectory, vector<EVTFileData>& outEvtData)
{
	const string sakura2Directory = sakuraRootDirectory + string("SAKURA2\\");
	vector<FileNameContainer> allSakura2Files;
	FindAllFilesWithinDirectory(sakura2Directory, allSakura2Files);

	//Find all dialog files (MES.BIN)
	vector<FileNameContainer> mesFiles;
	GetAllFilesOfType(allSakura2Files, "MES.BIN", mesFiles);

	//Parse the MES files
	vector<SakuraTextFile> mesTextFiles;
	FindAllSakuraText(mesFiles, mesTextFiles);

	//All EVT files
	vector<FileNameContainer> allEvtFiles;
	GetAllFilesOfType(allSakura2Files, "EVT0", allEvtFiles);
	GetAllFilesOfType(allSakura2Files, "EVT1", allEvtFiles);
	GetAllFilesOfType(allSakura2Files, "EVT2", allEvtFiles);

	//Remove EVT13 from the list because it has no corresponding mes file
	const string evt13("EVT13");
	vector<FileNameContainer> evtFiles;
	for(const FileNameContainer& evtFileName : allEvtFiles)
	{
		if( evtFileName.mNoExtension != evt13 )
		{
			evtFiles.push_back(evtFileName);
		}
	}

	if( mesTextFiles.size() != evtFiles.size() )
	{
		printf("Could not parse EVT Files.  Did not find all of the MES.BIN files.\n");
		return false;
	}

	//Parse all evt files
	const size_t numEvtFiles = evtFiles.size();
	for(size_t i = 0; i < numEvtFiles; ++i)
	{
		EVTFileData evtData;
		const FileNameContainer& evtFileName = evtFiles[i];

		FileData evtFileData;
		if( !evtFileData.InitializeFileData(evtFileName) )
		{
			return false;
		}

		evtData.mFileName = evtFileName.mNoExtension;

		//Find the start of the lookup table
		const char lutStartBytes[]  = {0x01, 0x02, 0x04};
		unsigned long lutIndex = 0;		
		if( !evtFileData.FindDataIndex(lutStartBytes, 3, lutIndex) )
		{
			return false;
		}
		lutIndex -= 11;

		if( lutIndex + evtData.LutSize >= evtFileData.GetDataSize() )
		{
			printf("No valid LUT found for : %s\n", evtFileName.mFileName.c_str());
			return false;
		}
		
		//copy the start of the face lookup table
		memcpy_s(&evtData.mFaceLookupTable, evtData.LutSize, evtFileData.GetData() + lutIndex, evtData.LutSize);

		//Now find the dialog order
		const SakuraTextFile& mesTextFile    = mesTextFiles[i];
		const char* pDialogOrderStart        = evtFileData.GetData();// + lutIndex + evtData.LutSize;
		const unsigned long numBytesRemaning = evtFileData.GetDataSize();// - (lutIndex + evtData.LutSize);
		const unsigned int indexAfterLut     = lutIndex + evtData.LutSize;
		for(unsigned int evtIndex = 0; evtIndex + 1 < numBytesRemaning; ++evtIndex)
		{
			unsigned short inId = 0;
			memcpy_s(&inId, sizeof(inId), &pDialogOrderStart[evtIndex], sizeof(inId));

			if( inId == 0xffff )
			{
				continue;
			}

			inId = SwapByteOrder(inId);

			if( mesTextFile.DoesIdExist(inId) )
			{
				EVTFileData::DialogOrder newEntry;
			
				newEntry.stringId = inId;

				if( evtIndex >= indexAfterLut )
				{
					memcpy_s(&newEntry.faceId,   sizeof(newEntry.faceId),   &pDialogOrderStart[evtIndex - 2], sizeof(newEntry.faceId));
				}
				else
				{
					memcpy_s(&newEntry.faceId,   sizeof(newEntry.faceId),   &pDialogOrderStart[evtIndex + 2], sizeof(newEntry.faceId));
				}
					
				newEntry.faceId = SwapByteOrder(newEntry.faceId);

				if( newEntry.faceId > 0 && newEntry.faceId < evtData.LutSize && evtData.mFaceLookupTable[newEntry.faceId] != 0)
				{
					evtData.mDialogOrder.push_back( newEntry );
				}
			}
		}

		outEvtData.push_back(evtData);
	}

	return true;
}

bool CreateMesSpreadSheets(const string& dialogImageDirectory, const string& sakuraRootDirectory)
{
	//EVT files contain info on which face image appears for each line of dialog
	vector<EVTFileData> evtData;
	if( !ParseEvtFiles(sakuraRootDirectory, evtData) )
	{
		return false;
	}

	const string sakura2Directory = sakuraRootDirectory + string("SAKURA2\\");

	vector<FileNameContainer> allFilesInImageDirectory;
	FindAllFilesWithinDirectory(dialogImageDirectory, allFilesInImageDirectory);

	vector<FileNameContainer> imageFiles;
	GetAllFilesOfType(allFilesInImageDirectory, ".png", imageFiles);

	vector<FileNameContainer> allSakura2Files;
	FindAllFilesWithinDirectory(sakura2Directory, allSakura2Files);

	//Find all dialog files
	vector<FileNameContainer> mesFiles;
	GetAllFilesOfType(allSakura2Files, "MES.BIN", mesFiles);

	//Parse the MES files
	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(mesFiles, sakuraTextFiles);

	//Create map of table file name to the file (M01MES => M01MES.BIN file)
	map<string, const SakuraTextFile*> sakuraTableFileMap;
	for(const SakuraTextFile& sakuraFile : sakuraTextFiles)
	{
		sakuraTableFileMap[sakuraFile.mFileNameInfo.mNoExtension] = &sakuraFile;
	}

	//Seperate all files into their own directories
	const string mesPrefix("MES");
	map<string, vector<FileNameContainer>> directoryMap;
	for(const FileNameContainer& imageFileNameInfo : imageFiles)
	{
		const size_t lastOf = imageFileNameInfo.mPathOnly.find_last_of('\\');
		const string leaf   = imageFileNameInfo.mPathOnly.substr(lastOf+1);
		if( leaf.find(mesPrefix) == std::string::npos )
		{
			continue;
		}

		if( lastOf != string::npos && leaf.size() > 0 )
		{
			directoryMap[leaf].push_back(imageFileNameInfo);
		}
	}

	//Sort png files from smallest to greatest (001.png -> 999.png)
	for(map<string, vector<FileNameContainer>>::iterator iter = directoryMap.begin(); iter != directoryMap.end(); ++iter)
	{
		std::sort(iter->second.begin(), iter->second.end());
	}

	const string htmlHeader1("<html>\n<head><style>textarea {width: 100%;top: 0; left: 0; right: 0; bottom: 0; position: absolute; resize: none;-webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;} table {border-collapse: collapse;} table, th, td { position: relative; border: 1px solid black;}");
	const string htmlHeader2("#myProgress {width: 100%;	background-color: #ddd;} #myBar {width: 1%;height: 30px; background-color: #4CAF50;}</style>");
	const string htmlHeader = htmlHeader1 + htmlHeader2;

	//Create html files for each directory
	static char buffer[2048];
	const string outputDirectory = dialogImageDirectory + string("..\\..\\Translation\\");
	if( !CreateDirectoryHelper(outputDirectory) )
	{
		printf("Unable to create translation directory %s.  Error: (%d)\n", outputDirectory.c_str(), GetLastError());
		return false;
	}

	for(map<string, vector<FileNameContainer>>::const_iterator iter = directoryMap.begin(); iter != directoryMap.end(); ++iter)
	{
		printf("Creating html file for %s\n", iter->first.c_str());

		//Find sakura file for this dialog
		const string tblFileName = iter->first;
		map<string, const SakuraTextFile*>::const_iterator sakuraFileIter = sakuraTableFileMap.find(tblFileName);
		if( sakuraFileIter == sakuraTableFileMap.end() )
		{
			printf("Unable to find corresponding sakura file for: %s", tblFileName.c_str());
			continue;
		}

		if( iter->second.size() != sakuraFileIter->second->mStringInfoArray.size() )
		{
			printf("Unable to create html file for %s.  StringInfo to dialog file count mismatch.", tblFileName.c_str());
			continue;
		}

		//Find matching evt file data
		const char mesNumberBuffer[3]  = {iter->first.c_str()[1], iter->first.c_str()[2], 0};
		const string mesNumber   = string(mesNumberBuffer);
		const string evtFileName = "EVT" + mesNumber;
		int evtIndex              = -1;
		for(size_t e = 0; e < evtData.size(); ++e)
		{
			if( evtData[e].mFileName == evtFileName )
			{
				evtIndex = e;
				break;
			}
		}

		if( evtIndex == -1 )
		{
			printf("Unable to create an html file: %s because EVT Data not found", iter->first.c_str());
			continue;
		}
		const EVTFileData& evtFileData = evtData[evtIndex];

		const string htmlFileName = outputDirectory + iter->first + string(".php");
		TextFileWriter htmlFile;
		if( !htmlFile.OpenFileForWrite(htmlFileName) )
		{
			printf("Unable to create an html file: %s", htmlFileName.c_str());
			continue;
		}

		const size_t numImageFiles = iter->second.size();

		//Common header stuff
		htmlFile.WriteString(htmlHeader);
		fprintf(htmlFile.GetFileHandle(), "\n<div id=\"FileName\" style=\"display: none;\">%s</div>\n", tblFileName.c_str());
		fprintf(htmlFile.GetFileHandle(), "\n<div id=\"LastImageIndex\" style=\"display: none;\">%i</div>\n", numImageFiles);

		htmlFile.WriteString("<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.1/jquery.min.js\">\n\n");

		//Load Data on startup
		htmlFile.WriteString("\t$( window ).on( \"load\", function()\n {\n\t\tOnStartup();\n});\n\n");
		htmlFile.WriteString("</script>\n\n");

		//Begin functions
		htmlFile.WriteString("<script type=\"text/javascript\">\n");

		//SaveData function
		htmlFile.WriteString("function SaveData(inDialogImageName, inDivID, inCRC)\n");
		htmlFile.WriteString("{\n");
		htmlFile.WriteString("     var translatedText = document.getElementById(inDivID).value;\n");
		htmlFile.WriteString("     var fileName = document.getElementById(\"FileName\").innerHTML;\n");
		htmlFile.WriteString("     $.ajax({\n");
		htmlFile.WriteString("          type: \"POST\",\n");
		htmlFile.WriteString("          url: \"UpdateTranslationTest.php\",\n");
		htmlFile.WriteString("          data: { inTBLFileName: fileName, inImageName:inDialogImageName, inTranslation:translatedText, inDivId:inDivID, inCrc:inCRC },\n");
		htmlFile.WriteString("          success: function(result)\n");
		htmlFile.WriteString("          {\n");
		htmlFile.WriteString("                var trId = \"tr_\" + inDivID;\n");
		htmlFile.WriteString("                if( translatedText != \"Untranslated\" && translatedText != \"<div>Untranslated</div>\")\n");
		htmlFile.WriteString("                {\n");
		htmlFile.WriteString("                     if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
		htmlFile.WriteString("                     {\n");
		htmlFile.WriteString("                          document.getElementById(trId).bgColor = \"#e3fec8\";\n");
		htmlFile.WriteString("                     }\n");
		htmlFile.WriteString("                }\n");
		htmlFile.WriteString("                else\n");
		htmlFile.WriteString("                {\n");
		htmlFile.WriteString("                     document.getElementById(trId).bgColor = \"#fefec8\";\n");
		htmlFile.WriteString("                }\n");
		htmlFile.WriteString("         }\n");
		htmlFile.WriteString("    });\n");
		htmlFile.WriteString("}\n");

		//SaveEdits function
		htmlFile.WriteString("function SaveEdits(inDialogImageName, inDivID, inCRC)\n");
		htmlFile.WriteString("{\n");
		htmlFile.WriteString("     SaveData(inDialogImageName, inDivID, inCRC);\n\n");		
		htmlFile.WriteString("}\n");

		//Export data function
		htmlFile.WriteString("function ExportData()\n");
		htmlFile.WriteString("{\n");
		htmlFile.WriteString("     $(\"textarea\").each ( function ()\n");
		htmlFile.WriteString("     {\n");
		htmlFile.WriteString("          var thisText = $(this).text();\n");
		htmlFile.WriteString("          thisText = thisText.replace(/<br>/g, '&ltbr&gt');\n");
		htmlFile.WriteString("          thisText = thisText.replace(/<sp>/g, '&ltsp&gt');\n");
		htmlFile.WriteString("          document.write(thisText + \"<br>\");\n");
		htmlFile.WriteString("     });\n");
		htmlFile.WriteString("}\n");

		//LoadData function
		htmlFile.WriteString("function LoadData(){\n");
		htmlFile.WriteString("     var fileName = document.getElementById(\"FileName\").innerHTML;\n");
		htmlFile.WriteString("     $.ajax({\n");
		htmlFile.WriteString("          type: \"POST\",\n");
		htmlFile.WriteString("          url: \"GetTranslationData.php\",\n");
		htmlFile.WriteString("          data: { inTBLFileName: fileName},\n");
		htmlFile.WriteString("          success: function(result)\n");
		htmlFile.WriteString("			{\n");
		htmlFile.WriteString("				var json = $.parseJSON(result);\n");
		htmlFile.WriteString("				var i;\n");
		htmlFile.WriteString("				for (i = 0; i < json.length; i++)\n");
		htmlFile.WriteString("				{\n");
		htmlFile.WriteString("					var jsonEntry = json[i];\n");
		htmlFile.WriteString("					var english   = jsonEntry.English.replace(/\\\\/g, \'\');\n");
		htmlFile.WriteString("					var divId     = \"#\" + jsonEntry.DivId;\n");
		htmlFile.WriteString("						var trId      = \"tr_\" + jsonEntry.DivId;\n");
		htmlFile.WriteString("						if( english != \"Untranslated\" && english != \"<div>Untranslated</div>\")\n");
		htmlFile.WriteString("						{\n");
		htmlFile.WriteString("							if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
		htmlFile.WriteString("							{\n");
		htmlFile.WriteString("							       document.getElementById(trId).bgColor = \"#e3fec8\";\n");
		htmlFile.WriteString("							}\n");
		htmlFile.WriteString("							$(divId).html(english);\n");
		htmlFile.WriteString("						}\n");
		htmlFile.WriteString("				}\n");
		htmlFile.WriteString("			},\n");;
		htmlFile.WriteString("          error: function()\n");
		htmlFile.WriteString("          {\n");
		htmlFile.WriteString("               alert('Unable to load data');\n");
		htmlFile.WriteString("          }\n");
		htmlFile.WriteString("     });\n");
		htmlFile.WriteString("}\n");


		//FixOnChangeEditableElements - A function that saves the data whenever input happens
		htmlFile.WriteString("function FixOnChangeEditableElements()\n{\n");
		htmlFile.WriteString("\tvar tags = document.querySelectorAll('[contenteditable=true][onChange]');//(requires FF 3.1+, Safari 3.1+, IE8+)\n");
		htmlFile.WriteString("\tfor (var i=tags.length-1; i>=0; i--) if (typeof(tags[i].onblur)!='function')\n{\n");
		htmlFile.WriteString("\t\ttags[i].onfocus = function()\n{\n");
		htmlFile.WriteString("\t\tthis.data_orig=this.innerHTML;\n};\n");
		htmlFile.WriteString("\t\ttags[i].onblur = function()\n{\n");
		htmlFile.WriteString("\t\tif( this.innerHTML != this.data_orig)\n");
		htmlFile.WriteString("\t\t\tthis.onchange();\n");
		htmlFile.WriteString("\tdelete this.data_orig;\n};\n}\n}\n");

		//Startup function
		htmlFile.WriteString("function OnStartup()\n{\n");
		htmlFile.WriteString("\tFixOnChangeEditableElements();\n}\n");

		//End scripts
		htmlFile.WriteString("</script>\n\n");
		htmlFile.WriteString("</head>\n\n");

		//Call startup function
		htmlFile.WriteString("<body>\n");

		htmlFile.WriteString("<?php include 'GetUserPermissions.php';\n");
		htmlFile.WriteString("\t$bPermissionFound = false;\n");
		htmlFile.WriteString("\tforeach ($allowedFiles as $value)\n{");
		fprintf(htmlFile.GetFileHandle(), "\t\tif( $value == \"%s\" )\n\t\t{\n", tblFileName.c_str());
		htmlFile.WriteString("\t\t\t$bPermissionFound = true;\n");
		htmlFile.WriteString("\t\t\tbreak;\n\t\t}\n\t}");
		htmlFile.WriteString("if( $bPermissionFound )\n{\n?>");

		fprintf(htmlFile.GetFileHandle(), "<article><header align=\"center\"><h1>Dialog For %s</h1></header></article>\n", iter->first.c_str());

		htmlFile.WriteString("<br>\n");
		htmlFile.WriteString("<b>Instructions:</b><br>\n");
		htmlFile.WriteString("-Please let me know once the file is complete by emailing me @ sakurataisentranslation@gmail.com<br>\n");
		htmlFile.WriteString("-This page is best displayed using Chrome.  Otherwise some of the table borders are missing for some reason.<br>\n");
		htmlFile.WriteString("-Your changes are automatically saved.<br>\n");
		htmlFile.WriteString("-Press the Load Data button when you come back to the page to load your changes.<br><br>\n");		
		
		htmlFile.WriteString("<b>Style:</b><br>\n");
		htmlFile.WriteString("-Use only a single space after a period.<br>\n");
		htmlFile.WriteString("-You do not need to worry about line breaks.  Just translate the text as one line.  The text insertion tool will automatically add newlines as needed.<br>\n");
		htmlFile.WriteString("-There is a 160 character limit for each dialog entry.<br><br>\n\n");

		htmlFile.WriteString("<b>Naming Conventions:</n><br>\n");
		htmlFile.WriteString("<a href=\"https://docs.google.com/spreadsheets/d/1rgafQe78vML_xbxnYuOSlO8P5C8nuhgLjMJOExUQsm0/edit?usp=sharing\" target=\"_blank\">Click here to view the naming conventions for Characters, Locations, and Terms</a> </b><br><br>\n\n");
		
		htmlFile.WriteString("<b>Notes about MES files:</b><br>\n");
		htmlFile.WriteString("-It's possible that some of the faces are incorrect, so if the line sounds strange for the character, it's probably the wrong face.<br>\n");
		htmlFile.WriteString("-Unlike the TBL files, I have not been able to figure out how to identify LIPS lines in the MES files.<br><br>\n\n");
		
		htmlFile.WriteString("<?php\n");
		htmlFile.WriteString("$currUser = $_SERVER['PHP_AUTH_USER'];\n");
		htmlFile.WriteString("if( $currUser == \"swtranslator\" )\n");
		htmlFile.WriteString("{\n");
		htmlFile.WriteString("echo \"<input align=\\\"center\\\" type=\\\"button\\\" value=\\\"Export Data\\\" onclick=\\\"ExportData()\\\"/>\";\n");
		htmlFile.WriteString("}\n");
		htmlFile.WriteString("?>\n\n");

		//Load Data button
		htmlFile.WriteString("<table align=\"center\">\n");
		htmlFile.WriteString("     <tr>\n");
		htmlFile.WriteString("          <td>\n");
		htmlFile.WriteString("               <input align=\"center\" type=\"button\" value=\"Load Data\" onclick=\"LoadData()\"/>\n");
		htmlFile.WriteString("          </td>\n");
		htmlFile.WriteString("     </tr>\n");
		htmlFile.WriteString("</table><br>\n\n");

		//Write table
		htmlFile.WriteString("<table>\n");
		htmlFile.WriteString("\t<tr bgcolor=\"#c8c8fe\">\n");
		htmlFile.WriteString("\t<th>#</th>\n");
		htmlFile.WriteString("\t<th>Speaker</th>\n");
		htmlFile.WriteString("\t<th>Japanese</th>\n");
		htmlFile.WriteString("\t<th>English</th>\n");
		htmlFile.WriteString("\t<th>ID</th>\n");
		htmlFile.WriteString("\t</tr>\n");

		//Create entries for all images
		int num = 0;
		for(const FileNameContainer& fileNameInfo : iter->second)
		{
			const unsigned short id   = sakuraFileIter->second->mStringInfoArray[num].mStringId;
			const char* bgColor       = "fefec8";

			const char* pVarSuffix = fileNameInfo.mNoExtension.c_str();
			fprintf(htmlFile.GetFileHandle(), "<tr id=\"tr_edit_%i\" bgcolor=\"#%s\">\n", num + 1, bgColor);
			snprintf(buffer, 2048, "<td align=\"center\" width=\"20\">%i</td>", num + 1);
			htmlFile.WriteString(string(buffer));

			int faceImageId = evtFileData.GetFaceImageNumber(id);
			if( faceImageId != -1 )
			{
				snprintf(buffer, 2048, "<td width=\"48\"><img src=\"..\\ExtractedData\\Faces\\FACE%s\\%i.png\"></td>", mesNumber.c_str(), faceImageId);
				//snprintf(buffer, 2048, "<td width=\"48\"><img src=\"..\\FACE\\FACE%s\\%i.png\"></td>", mesNumber.c_str(), faceImageId);
			}
			else
			{
				snprintf(buffer, 2048, "<td width=\"48\"><img src=\"..\\ExtractedData\\Faces\\UnknownFace.png\"></td>");
				//snprintf(buffer, 2048, "<td width=\"48\"><img src=\"..\\FACE\\UnknownFace.png\"></td>");
			}
			htmlFile.WriteString(string(buffer));

			snprintf(buffer, 2048, "<td width=\"240\"><img src=\"..\\ExtractedData\\Dialog\\MES\\M%sMES\\%s\"></td>", mesNumber.c_str(), fileNameInfo.mFileName.c_str());
			//snprintf(buffer, 2048, "<td width=\"240\"><img src=\"..\\Dialog\\M%sMES\\%s\"></td>", mesNumber.c_str(), fileNameInfo.mFileName.c_str());
			htmlFile.WriteString(string(buffer));

			snprintf(buffer, 2048, "<td width=\"480\"><textarea id=\"edit_%s\" contenteditable=true onchange=\"SaveEdits('%i.bmp', 'edit_%i')\" style=\"border: none; width: 100%%; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;\">Untranslated</textarea></td>", pVarSuffix, num + 1, num + 1);
			htmlFile.WriteString(string(buffer));

			snprintf(buffer, 2048, "<td align=\"center\" width=\"120\">%02x (%i)</td>", id, id);
			htmlFile.WriteString(string(buffer));

			htmlFile.WriteString("</tr>\n");

			++num;
		}
		htmlFile.WriteString("</table><br>\n");

		htmlFile.WriteString("<?php\n}\n?>");

		//End file
		htmlFile.WriteString("</body>\n");
		htmlFile.WriteString("</html>\n");
	}

	return true;
}

bool CreateTBLSpreadsheets(const string& dialogImageDirectory, const string& duplicatesFileName, const string& sakura1Directory, const string& translatedTextDirectory, bool bForRelease)
{
	printf("Parsing duplicates file\n");

	//Load duplicate info
	FileNameContainer dupFileNameContainer(duplicatesFileName.c_str());
	TextFileData duplicatesFile(dupFileNameContainer);
	if( !duplicatesFile.InitializeTextFile() )
	{
		printf("Unable to open duplicates file.\n");
		return false;
	}

	map<unsigned long, vector<string>> dupCrcMap; //crc, fileNames
	map<string, unsigned long> dupFilenameCrcMap; //fileName, crc
	const size_t numDupLines = duplicatesFile.mLines.size();
	for(size_t i = 0; i < numDupLines; ++i)
	{
		if( duplicatesFile.mLines[i].mWords.size() != 2 )
		{
			printf("Invalid duplicate file format");
			return false;
		}

		const unsigned long crc = strtoul(duplicatesFile.mLines[i].mWords[1].c_str(), nullptr, 0); 
		dupCrcMap[crc].push_back( duplicatesFile.mLines[i].mWords[0] );
		dupFilenameCrcMap[duplicatesFile.mLines[i].mWords[0]] = crc;
	}

	vector<FileNameContainer> allFilesInImageDirectory;
	FindAllFilesWithinDirectory(dialogImageDirectory, allFilesInImageDirectory);

	vector<FileNameContainer> imageFiles;
	GetAllFilesOfType(allFilesInImageDirectory, ".png", imageFiles);

	vector<FileNameContainer> allSakura1Files;
	FindAllFilesWithinDirectory(sakura1Directory, allSakura1Files);

	//Find all dialog files
	vector<FileNameContainer> tableFiles;
	GetAllFilesOfType(allSakura1Files, "TBL.BIN", tableFiles);

	//Parse the TBL files
	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(tableFiles, sakuraTextFiles);

	vector<FileNameContainer> translatedTextFiles;
	FindAllFilesWithinDirectory(translatedTextDirectory, translatedTextFiles);

	//Create map of table file name to the file (0100TBL => 0100TBL file)
	map<string, const SakuraTextFile*> sakuraTableFileMap;
	for(const SakuraTextFile& sakuraFile : sakuraTextFiles)
	{
		sakuraTableFileMap[sakuraFile.mFileNameInfo.mNoExtension] = &sakuraFile;
	}

	//Seperate all files into their own directories
	map<string, vector<FileNameContainer>> directoryMap;
	for(const FileNameContainer& imageFileNameInfo : imageFiles)
	{
		if( imageFileNameInfo.mPathOnly.find_first_of("TBL") != string::npos )
		{
			const size_t lastOf = imageFileNameInfo.mPathOnly.find_last_of('\\');
			const string leaf = imageFileNameInfo.mPathOnly.substr(lastOf + 1);
			if (lastOf != string::npos && leaf.size() > 0)
			{
				directoryMap[leaf].push_back(imageFileNameInfo);
			}
		}
	}

	const string htmlHeader1("<html>\n<head><style>textarea {width: 100%;top: 0; left: 0; right: 0; bottom: 0; position: absolute; resize: none;-webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;} table {border-collapse: collapse;} table, th, td { position: relative; border: 1px solid black;}");
	const string htmlHeader2("#myProgress {width: 100%;	background-color: #ddd;} #myBar {width: 1%;height: 30px; background-color: #4CAF50;}</style>");
	const string htmlHeader = htmlHeader1 + htmlHeader2;

	//Create html files for each directory
	static char buffer[2048];
	const string outputDirectory = dialogImageDirectory + string("..\\..\\Translation\\");
	if( !CreateDirectoryHelper(outputDirectory) )
	{
		printf("Unable to create translation directory %s.  Error: (%d)\n", outputDirectory.c_str(), GetLastError());
		return false;
	}

	//Sort bmp files from smallest to greatest (001.bmp -> 999.bmp)
	for(map<string, vector<FileNameContainer>>::iterator iter = directoryMap.begin(); iter != directoryMap.end(); ++iter)
	{
		std::sort(iter->second.begin(), iter->second.end());
	}

	//Find dialog order info
	map<string, DialogOrder> dialogOrder;
	if( !FindDialogOrder(sakura1Directory, dialogOrder) )
	{
		printf("Unable to find dialog files(0100.BIN, 0101.BIN, etc.) in directory: %s", sakura1Directory.c_str());
		return false;
	}

	for(map<string, vector<FileNameContainer>>::const_iterator iter = directoryMap.begin(); iter != directoryMap.end(); ++iter)
	{
		printf("Creating html file for %s\n", iter->first.c_str());

		//Find sakura file for this dialog
		const string tblFileName = iter->first;
		map<string, const SakuraTextFile*>::const_iterator sakuraFileIter = sakuraTableFileMap.find(tblFileName);
		if( sakuraFileIter == sakuraTableFileMap.end() )
		{
			printf("Unable to find corresponding sakura file for: %s", tblFileName.c_str());
			continue;
		}

		if( iter->second.size() != sakuraFileIter->second->mStringInfoArray.size() )
		{
			printf("Unable to create html file for %s.  StringInfo to dialog file count mismatch.", tblFileName.c_str());
			continue;
		}

		//Store all crcs based on FileNameContainer*
		const size_t numImageFiles = iter->second.size();
		map<const FileNameContainer*, unsigned long> crcMap;
		for(size_t fileIndex = 0; fileIndex < numImageFiles; ++fileIndex)
		{
			const FileNameContainer* pFileContainer = &iter->second[fileIndex];
			FileData fileData;
			if( !fileData.InitializeFileData(*pFileContainer) )
			{
				printf("Unable to open file for crc generation");
				break;
			}

			crcMap[pFileContainer] = fileData.GetCRC();
		}

		//Find all duplicates within this file. That is, duplicates that are internal to only this file.
		printf("Finding Duplicates for %s \n", iter->first.c_str());
		int numberOfDuplicatesFound = 0;
		map<string, vector<string>> duplicatesMap; //FileName, Vector<Duplicate FileNames>
		for(size_t fileIndex = 0; fileIndex < numImageFiles; ++fileIndex)
		{
			const FileNameContainer& firstImageName = iter->second[fileIndex];

			if( crcMap.find(&firstImageName) == crcMap.end() )
			{
				printf("Crc data not found.  Unable to generate web files.\n");
				return false;
			}

			const unsigned long imageCrc = crcMap[&firstImageName];

			//See if there is a duplicate in any file
			map<unsigned long, vector<string>>::iterator dupCrcMapIter = dupCrcMap.find(imageCrc);
			if( dupCrcMapIter != dupCrcMap.end() )
			{
				const size_t numCrcMatchesForFile = dupCrcMapIter->second.size();
				if( numCrcMatchesForFile == 0 )
				{
					printf("No crc matches found for file.  Something went wrong.\n");
					return false;
				}
				numberOfDuplicatesFound++;
			}

			//Make sure we haven't already found a match for this file
			bool bExistingDupFound = false;
			for(map<string, vector<string>>::const_iterator mapIter = duplicatesMap.begin(); mapIter != duplicatesMap.end(); ++mapIter)
			{
				const size_t numDups = mapIter->second.size();
				for(size_t dupIndex = 0; dupIndex < numDups; ++dupIndex)
				{
					if( mapIter->second[dupIndex] == firstImageName.mNoExtension )
					{
						bExistingDupFound = true;
						break;
					}
				}

				if( bExistingDupFound )
				{
					break;
				}
			}

			if( bExistingDupFound )
			{
				continue;
			}

			//Check all other files
			for(size_t secondFileIndex = fileIndex + 1; secondFileIndex < numImageFiles; ++secondFileIndex)
			{
				const FileNameContainer* pSecondFile = &iter->second[secondFileIndex];
				if( crcMap.find(pSecondFile) == crcMap.end() )
				{
					printf("Crc data not found.  Unable to generate web files.\n");
					return false;
				}

				if( crcMap[pSecondFile] == imageCrc )
				{
					duplicatesMap[firstImageName.mNoExtension].push_back( iter->second[secondFileIndex].mNoExtension );
				}
			}
		}

		const string htmlFileName = outputDirectory + iter->first + string(".php");

		TextFileWriter htmlFile;
		if( !htmlFile.OpenFileForWrite(htmlFileName) )
		{
			printf("Unable to create an html file: %s", htmlFileName.c_str());
			continue;
		}

		//Common header stuff
		htmlFile.WriteString(htmlHeader);

		if( !bForRelease )
		{
			fprintf(htmlFile.GetFileHandle(), "\n<div id=\"FileName\" style=\"display: none;\">%s</div>\n", tblFileName.c_str());
			fprintf(htmlFile.GetFileHandle(), "\n<div id=\"LastImageIndex\" style=\"display: none;\">%i</div>\n", numImageFiles);
			fprintf(htmlFile.GetFileHandle(), "\n<div id=\"NumberOfDuplicates\" style=\"display: none;\">%i</div>\n", numberOfDuplicatesFound);

			htmlFile.WriteString("<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.1/jquery.min.js\">\n\n");

			//Load Data on startup
			htmlFile.WriteString("\t$( window ).on( \"load\", function()\n {\n\t\tOnStartup();\n});\n\n");
			htmlFile.WriteString("</script>\n\n");

			//Begin functions
			htmlFile.WriteString("<script type=\"text/javascript\">\n");

			//SaveDuplicateData function
			htmlFile.WriteString("function SaveDuplicateData(inDialogImageName, inEnglish, inDivID, inCRC)\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("     var fileName = document.getElementById(\"FileName\").innerHTML;\n");
			htmlFile.WriteString("     	$.ajax({\n");
			htmlFile.WriteString("          type: \"POST\",\n");
			htmlFile.WriteString("          url: \"UpdateTranslationTest.php\",\n");
			htmlFile.WriteString("          data: { inTBLFileName: fileName, inImageName:inDialogImageName, inTranslation:inEnglish, inDivId:inDivID, inCrc:inCRC },\n");
			htmlFile.WriteString("          success: function(result)\n");
			htmlFile.WriteString("          {\n");
			htmlFile.WriteString("               var trId = \"tr_\" + inDivID;\n");
			htmlFile.WriteString("               if( inEnglish != \"Untranslated\" && inEnglish != \"<div>Untranslated</div>\")\n");
			htmlFile.WriteString("               {\n");
			htmlFile.WriteString("                    if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
			htmlFile.WriteString("                    {\n");
			htmlFile.WriteString("                         document.getElementById(trId).bgColor = \"#e3fec8\";\n");
			htmlFile.WriteString("                    }\n");
			htmlFile.WriteString("               }\n");
			htmlFile.WriteString("               else\n");
			htmlFile.WriteString("               {\n");
			htmlFile.WriteString("                    document.getElementById(trId).bgColor = \"#fefec8\";\n");
			htmlFile.WriteString("               }\n");
			htmlFile.WriteString("          }\n");
			htmlFile.WriteString("     });\n");
			htmlFile.WriteString("}\n\n");

			//SaveData function
			htmlFile.WriteString("function SaveData(inDialogImageName, inDivID, inCRC)\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("     var translatedText = document.getElementById(inDivID).value;\n");
			htmlFile.WriteString("     var fileName = document.getElementById(\"FileName\").innerHTML;\n");
			htmlFile.WriteString("     $.ajax({\n");
			htmlFile.WriteString("          type: \"POST\",\n");
			htmlFile.WriteString("          url: \"UpdateTranslationTest.php\",\n");
			htmlFile.WriteString("          data: { inTBLFileName: fileName, inImageName:inDialogImageName, inTranslation:translatedText, inDivId:inDivID, inCrc:inCRC },\n");
			htmlFile.WriteString("          success: function(result)\n");
			htmlFile.WriteString("          {\n");
			htmlFile.WriteString("                var trId = \"tr_\" + inDivID;\n");
			htmlFile.WriteString("                if( translatedText != \"Untranslated\" && translatedText != \"<div>Untranslated</div>\")\n");
			htmlFile.WriteString("                {\n");
			htmlFile.WriteString("                     if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
			htmlFile.WriteString("                     {\n");
			htmlFile.WriteString("                          document.getElementById(trId).bgColor = \"#e3fec8\";\n");
			htmlFile.WriteString("                     }\n");
			htmlFile.WriteString("                }\n");
			htmlFile.WriteString("                else\n");
			htmlFile.WriteString("                {\n");
			htmlFile.WriteString("                     document.getElementById(trId).bgColor = \"#fefec8\";\n");
			htmlFile.WriteString("                }\n");
			htmlFile.WriteString("         }\n");
			htmlFile.WriteString("    });\n");
			htmlFile.WriteString("}\n");

			//SaveEdits function
			htmlFile.WriteString("function SaveEdits(inDialogImageName, inDivID, inCRC)\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("     SaveData(inDialogImageName, inDivID, inCRC);\n\n");
			htmlFile.WriteString("     var translatedText = document.getElementById(inDivID).value;\n");

			//Print out array containing all available duplicate
			if (duplicatesMap.size())
			{
				htmlFile.WriteString("     var allDups = [\n");
			}
			else
			{
				htmlFile.WriteString("     var allDups = [];\n");
			}
		
			bool firstDupPrinted = false;
			for(map<string, vector<string>>::const_iterator mapIter = duplicatesMap.begin(); mapIter != duplicatesMap.end(); ++mapIter)
			{
				if( firstDupPrinted )
				{
					fprintf(htmlFile.GetFileHandle(), ",\n");
				}

				fprintf(htmlFile.GetFileHandle(), "                    \"edit_%s\"", mapIter->first.c_str());
			
				//Print dups of this entry (contained in mapIter->first)
				const size_t numDups = mapIter->second.size();
				for(size_t dupIndex = 0; dupIndex < numDups; ++dupIndex)
				{
					fprintf(htmlFile.GetFileHandle(), ", \"edit_%s\"", mapIter->second[dupIndex].c_str());
				}

				firstDupPrinted = true;

			}
			if( duplicatesMap.size() )
			{
				htmlFile.WriteString("];\n\n");
			}

			for(map<string, vector<string>>::const_iterator mapIter = duplicatesMap.begin(); mapIter != duplicatesMap.end(); ++mapIter)
			{
				fprintf(htmlFile.GetFileHandle(), "     var edit_%s_duplicates = [", mapIter->first.c_str());

				const size_t numDups = mapIter->second.size();
				for(size_t dupIndex = 0; dupIndex < numDups; ++dupIndex)
				{
					fprintf(htmlFile.GetFileHandle(), "\"%s\"", mapIter->second[dupIndex].c_str());
					if(dupIndex + 1 < numDups)
					{
						fprintf(htmlFile.GetFileHandle(), ",");
					}
				}
				fprintf(htmlFile.GetFileHandle(), "];\n");

				//Now print vars for all of the dups
				for(size_t dupIndex = 0; dupIndex < numDups; ++dupIndex)
				{
					fprintf(htmlFile.GetFileHandle(), "     var edit_%s_duplicates = [\"%s\"", mapIter->second[dupIndex].c_str(), mapIter->first.c_str());

					if( numDups > 1 )
					{
						fprintf(htmlFile.GetFileHandle(), ",");
					}

					size_t numDupsPrinted = 0;
					for(size_t dupIndex2 = 0; dupIndex2 < numDups; ++dupIndex2)
					{
						if( dupIndex == dupIndex2 )
						{
							continue;
						}

						fprintf(htmlFile.GetFileHandle(), "\"%s\"", mapIter->second[dupIndex2].c_str());
						if(numDupsPrinted + 1 < numDups - 1)
						{
							fprintf(htmlFile.GetFileHandle(), ",");
						}

						++numDupsPrinted;
					}
					fprintf(htmlFile.GetFileHandle(), "];\n");
				}
			}

			htmlFile.WriteString("     var dynName = inDivID + \"_duplicates\";\n\n");
			htmlFile.WriteString("     if( allDups.includes(inDivID) )\n");
			htmlFile.WriteString("     {\n");
			htmlFile.WriteString("          var dupArray = eval(dynName);\n");
			htmlFile.WriteString("          for(var i = 0; i < dupArray.length; i++)\n");
			htmlFile.WriteString("          {\n");
			htmlFile.WriteString("               var lookupName = \"#edit_\" + dupArray[i];\n");
			htmlFile.WriteString("               $(lookupName).html(translatedText)\n");
			htmlFile.WriteString("               var dupImageName = dupArray[i] + \".bmp\";\n");
			htmlFile.WriteString("               var dupDivID     = \"edit_\" + dupArray[i];\n\n");
			htmlFile.WriteString("               SaveData(dupImageName, dupDivID, 0)\n");
			htmlFile.WriteString("          }\n");
			htmlFile.WriteString("     }\n");
			htmlFile.WriteString("}\n");

			//Export data function
			htmlFile.WriteString("function ExportData()\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("     $(\"textarea\").each ( function ()\n");
			htmlFile.WriteString("     {\n");
			htmlFile.WriteString("          var thisText = $(this).text();\n");
			htmlFile.WriteString("          thisText = thisText.replace(/<br>/g, '&ltbr&gt');\n");
			htmlFile.WriteString("          thisText = thisText.replace(/<sp>/g, '&ltsp&gt');\n");
			htmlFile.WriteString("          document.write(thisText + \"<br>\");\n");
			htmlFile.WriteString("     });\n");
			htmlFile.WriteString("}\n");

			//UpdateLoadingBar function
			htmlFile.WriteString("function UpdateLoadingBar(inLoadPercentage)\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("     var elem            = document.getElementById(\"myBar\");\n");
			htmlFile.WriteString("     var loadPercentElem = document.getElementById(\"LoadPercent\");\n");
			htmlFile.WriteString("     inLoadPercentage    = Math.floor( inLoadPercentage*100 + 0.5);\n");
			htmlFile.WriteString("     elem.style.width = inLoadPercentage + '%';\n");
			htmlFile.WriteString("     loadPercentElem.innerHTML = (inLoadPercentage).toString() + \"%\";\n");
			htmlFile.WriteString("}\n");

			//AttemptToLoadDuplicateData function
			htmlFile.WriteString("function AttemptToLoadDuplicateData(inDivID, inCRC, inTrID, inPercentComplete)\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("     $.ajax({\n");
			htmlFile.WriteString("     type: \"POST\",\n");
			htmlFile.WriteString("     url: \"GetTranslationFromCRC.php\",\n");
			htmlFile.WriteString("     data: { inCrc: inCRC},\n");
			htmlFile.WriteString("     success: function(result)\n");
			htmlFile.WriteString("     {\n");
			htmlFile.WriteString("          UpdateLoadingBar(inPercentComplete);\n\n");
			htmlFile.WriteString("          var json = $.parseJSON(result);\n");
			htmlFile.WriteString("          var i;\n");
			htmlFile.WriteString("          for (i = 0; i < json.length; i++)\n");
			htmlFile.WriteString("          {\n");
			htmlFile.WriteString("               var jsonEntry = json[i];\n");
			htmlFile.WriteString("               var english   = jsonEntry.English.replace(/\\\\/g, \'\');\n");
			htmlFile.WriteString("               if( english != \"Untranslated\" )\n");
			htmlFile.WriteString("               {\n");
			htmlFile.WriteString("                    if( document.getElementById(inTrID).bgColor != \"#fec8c8\" )\n");
			htmlFile.WriteString("                    {\n");
			htmlFile.WriteString("                         document.getElementById(inTrID).bgColor = \"#e3fec8\";\n");
			htmlFile.WriteString("                    }\n");
			htmlFile.WriteString("                    $(inDivID).html(english);\n");
			htmlFile.WriteString("                    var divID = inDivID.replace(\"#\", \"\");\n");
			htmlFile.WriteString("                    var imageName = divID.replace(\"edit_\", \"\") + \".bmp\";\n");
			htmlFile.WriteString("                    SaveDuplicateData(imageName, english, divID, inCRC);\n");
			htmlFile.WriteString("                    return;\n");
			htmlFile.WriteString("               }\n");
			htmlFile.WriteString("          }\n");
			htmlFile.WriteString("     },\n");
			htmlFile.WriteString("     error: function()\n");
			htmlFile.WriteString("     {\n");
			htmlFile.WriteString("     }\n");
			htmlFile.WriteString("   });\n");
			htmlFile.WriteString("}\n");

			//LoadData function
			htmlFile.WriteString("function LoadData(){\n");
			htmlFile.WriteString("     var fileName = document.getElementById(\"FileName\").innerHTML;\n");
			htmlFile.WriteString("     $.ajax({\n");
			htmlFile.WriteString("          type: \"POST\",\n");
			htmlFile.WriteString("          url: \"GetTranslationData.php\",\n");
			htmlFile.WriteString("          data: { inTBLFileName: fileName},\n");
			htmlFile.WriteString("          success: function(result)\n");
			htmlFile.WriteString("          {\n");
			htmlFile.WriteString("               var numDupsPendingLoad = document.getElementById(\"NumberOfDuplicates\").innerHTML;\n");
			htmlFile.WriteString("               var json = $.parseJSON(result);\n");
			htmlFile.WriteString("               var i;\n");
			htmlFile.WriteString("               for (i = 0; i < json.length; i++)\n");
			htmlFile.WriteString("               {\n");
			htmlFile.WriteString("                    var jsonEntry = json[i];\n");
			htmlFile.WriteString("                    var english   = jsonEntry.English.replace(/\\\\/g, \'\');\n");
			htmlFile.WriteString("                    var divId     = \"#\" + jsonEntry.DivId;\n");
			htmlFile.WriteString("                    var trId      = \"tr_\" + jsonEntry.DivId;\n");
			htmlFile.WriteString("                    if( english != \"Untranslated\" && english != \"<div>Untranslated</div>\")\n");
			htmlFile.WriteString("                    {\n");
			htmlFile.WriteString("                         if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
			htmlFile.WriteString("                         {\n");
			htmlFile.WriteString("                              document.getElementById(trId).bgColor = \"#e3fec8\";\n");
			htmlFile.WriteString("                         }  \n");
			htmlFile.WriteString("                         $(divId).html(english);\n\n");
			htmlFile.WriteString("                         var idNum     = jsonEntry.DivId.replace(\"edit_\", \"\");\n");
			htmlFile.WriteString("                         var dupId     = \"dup_\" + idNum;\n");
			htmlFile.WriteString("                         var dupValue  = document.getElementById(dupId).innerHTML;\n");
			htmlFile.WriteString("                         if( dupValue == \"true\" )\n");
			htmlFile.WriteString("                              numDupsPendingLoad--;\n");
			htmlFile.WriteString("                    }\n");
			htmlFile.WriteString("               }\n\n");
			htmlFile.WriteString("               ////Load duplicates\n");
			htmlFile.WriteString("               if( numDupsPendingLoad <= 0 )\n");
			htmlFile.WriteString("               {\n");
			htmlFile.WriteString("                    UpdateLoadingBar(1);\n\n");
			htmlFile.WriteString("               }\n");
			htmlFile.WriteString("               else\n");
			htmlFile.WriteString("               {\n");
			htmlFile.WriteString("                    var lastImageIndex  = document.getElementById(\"LastImageIndex\").innerHTML;\n");
			htmlFile.WriteString("                    var numDupProcessed = 0;\n");
			htmlFile.WriteString("                    for(i = 1; i < lastImageIndex; ++i)\n");
			htmlFile.WriteString("                    {\n");
			htmlFile.WriteString("                         var dupId    = \"dup_\" + i;\n");
			htmlFile.WriteString("                         var dupValue = document.getElementById(dupId).innerHTML;\n");
			htmlFile.WriteString("                         if( dupValue == \"false\" )\n");
			htmlFile.WriteString("                         {\n");
			htmlFile.WriteString("                              continue;\n");
			htmlFile.WriteString("                         }\n");
			htmlFile.WriteString("                         var divId    = \"#edit_\" + i;\n");
			htmlFile.WriteString("                         var trId     = \"tr_edit_\" + i;\n");
			htmlFile.WriteString("                         var crcId    = \"crc_\" + i;\n");
			htmlFile.WriteString("                         var crcValue = document.getElementById(crcId).innerHTML;\n");
			htmlFile.WriteString("                         var translatedText = document.getElementById(\"edit_\" + i).value;\n");
			htmlFile.WriteString("                         crcValue     = parseInt(crcValue, 16)\n");
			htmlFile.WriteString("                         if( translatedText == \"Untranslated\" )\n");
			htmlFile.WriteString("                         {\n");
			htmlFile.WriteString("                              var percentComplete = (numDupProcessed+1)/numDupsPendingLoad;\n");
			htmlFile.WriteString("                              numDupProcessed = numDupProcessed + 1;\n");
			htmlFile.WriteString("                              AttemptToLoadDuplicateData(divId, crcValue, trId, percentComplete);\n");
			htmlFile.WriteString("                         }\n");
			htmlFile.WriteString("                    }\n");
			htmlFile.WriteString("               }\n");
			htmlFile.WriteString("          },\n");
			htmlFile.WriteString("          error: function()\n");
			htmlFile.WriteString("          {\n");
			htmlFile.WriteString("               alert('Unable to load data');\n");
			htmlFile.WriteString("          }\n");
			htmlFile.WriteString("     });\n");
			htmlFile.WriteString("}\n");


			//FixOnChangeEditableElements - A function that saves the data whenever input happens
			htmlFile.WriteString("function FixOnChangeEditableElements()\n{\n");
			htmlFile.WriteString("\tvar tags = document.querySelectorAll('[contenteditable=true][onChange]');//(requires FF 3.1+, Safari 3.1+, IE8+)\n");
			htmlFile.WriteString("\tfor (var i=tags.length-1; i>=0; i--) if (typeof(tags[i].onblur)!='function')\n{\n");
			htmlFile.WriteString("\t\ttags[i].onfocus = function()\n{\n");
			htmlFile.WriteString("\t\tthis.data_orig=this.innerHTML;\n};\n");
			htmlFile.WriteString("\t\ttags[i].onblur = function()\n{\n");
			htmlFile.WriteString("\t\tif( this.innerHTML != this.data_orig)\n");
			htmlFile.WriteString("\t\t\tthis.onchange();\n");
			htmlFile.WriteString("\tdelete this.data_orig;\n};\n}\n}\n");

			//Startup function
			htmlFile.WriteString("function OnStartup()\n{\n");
			htmlFile.WriteString("\tFixOnChangeEditableElements();\n}\n");

			//End scripts
			htmlFile.WriteString("</script>\n\n");
			htmlFile.WriteString("</head>\n\n");

			//Call startup function
			htmlFile.WriteString("<body>\n");

			htmlFile.WriteString("<?php include 'GetUserPermissions.php';\n");
			htmlFile.WriteString("\t$bPermissionFound = false;\n");
			htmlFile.WriteString("\tforeach ($allowedFiles as $value)\n{");
			fprintf(htmlFile.GetFileHandle(), "\t\tif( $value == \"%s\" )\n\t\t{\n", tblFileName.c_str());
			htmlFile.WriteString("\t\t\t$bPermissionFound = true;\n");
			htmlFile.WriteString("\t\t\tbreak;\n\t\t}\n\t}");
			htmlFile.WriteString("if( $bPermissionFound )\n{\n?>");

			fprintf(htmlFile.GetFileHandle(), "<article><header align=\"center\"><h1>Dialog For %s</h1></header></article>\n", iter->first.c_str());

			htmlFile.WriteString("<br>\n");
			htmlFile.WriteString("<b>Instructions:</b><br>\n");
			htmlFile.WriteString("-Please let me know once the file is complete by emailing me @ sakurataisentranslation@gmail.com<br>\n");
			htmlFile.WriteString("-This page is best displayed using Chrome.  Otherwise some of the table borders are missing for some reason.<br>\n");
			htmlFile.WriteString("-Skip any row that is grayed out.<br>\n");
			htmlFile.WriteString("-Your changes are automatically saved.<br>\n");
			htmlFile.WriteString("-Press the Load Data button when you come back to the page to load your changes.<br>\n");
			htmlFile.WriteString("-Please wait for the Load Bar to complete.  It's a bit slow, but as more of the file is translated, it will speed up.  If it gets stuck in the 90's, that's fine, consider it done. I'll fix this bug soon.<br><br>\n");

			htmlFile.WriteString("<b>Style:</b><br>\n");
			htmlFile.WriteString("-Use only a single space after a period.<br>\n");
			htmlFile.WriteString("-You do not need to worry about line breaks.  Just translate the text as one line.  The text insertion tool will automatically add newlines as needed.<br>\n");
			htmlFile.WriteString("-There is a 160 character limit for each dialog entry.<br><br>\n\n");

			htmlFile.WriteString("<b>LIPS Events</b><br>\n");
			htmlFile.WriteString("-Pink rows are LIPS events where the user has to pick which line to say.  When translating these, insert a \" &lt;br&gt; \" without the quotes to signigy the next option.<br><br>\n");
			htmlFile.WriteString("For example: The following line has two options.<br>\n");
			htmlFile.WriteString("<img src=\"..\\ExtractedData\\Dialog\\0100TBL\\67.png\"><br>\n");
			htmlFile.WriteString("It would be translated as: <br>\n");
			htmlFile.WriteString("I'm on duty! &lt;br&gt; First I'd like to know more about you.<br><br>\n\n");

			htmlFile.WriteString("<b>Naming Conventions:</n><br>\n");
			htmlFile.WriteString("<a href=\"https://docs.google.com/spreadsheets/d/1rgafQe78vML_xbxnYuOSlO8P5C8nuhgLjMJOExUQsm0/edit?usp=sharing\" target=\"_blank\">Click here to view the naming conventions for Characters, Locations, and Terms</a> <br>\n");

			htmlFile.WriteString("<?php\n");
			htmlFile.WriteString("$currUser = $_SERVER['PHP_AUTH_USER'];\n");
			htmlFile.WriteString("if( $currUser == \"swtranslator\" )\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("echo \"<input align=\\\"center\\\" type=\\\"button\\\" value=\\\"Export Data\\\" onclick=\\\"ExportData()\\\"/>\";\n");
			htmlFile.WriteString("}\n");
			htmlFile.WriteString("?>\n\n");

			//Load Data button
			htmlFile.WriteString("<table align=\"center\">\n");
			htmlFile.WriteString("     <tr>\n");
			htmlFile.WriteString("          <td>\n");
			htmlFile.WriteString("               <input align=\"center\" type=\"button\" value=\"Load Data\" onclick=\"LoadData()\"/>\n");
			htmlFile.WriteString("          </td>\n");
			htmlFile.WriteString("     </tr>\n");
			htmlFile.WriteString("     <tr>\n");
			htmlFile.WriteString("          <td>\n");
			htmlFile.WriteString("               Duplicate Load Progress\n");
			htmlFile.WriteString("          </td>\n");
			htmlFile.WriteString("          <td width=\"400\">\n");
			htmlFile.WriteString("               <div id=\"myProgress\">\n");
			htmlFile.WriteString("               <div id=\"myBar\"></div>\n");
			htmlFile.WriteString("          </div>\n");
			htmlFile.WriteString("          </td>\n");
			htmlFile.WriteString("          <td>\n");
			htmlFile.WriteString("               <div id=\"LoadPercent\">0</div>\n");
			htmlFile.WriteString("          </td>\n");
			htmlFile.WriteString("     </tr>\n");
			htmlFile.WriteString("</table><br>\n\n");

		}//if( !bForRelease )

		//Write table
		htmlFile.WriteString("<table>\n");
		htmlFile.WriteString("\t<tr bgcolor=\"#c8c8fe\">\n");
		htmlFile.WriteString("\t<th>#</th>\n");
		htmlFile.WriteString("\t<th>Speaker</th>\n");
		htmlFile.WriteString("\t<th>Japanese</th>\n");
		htmlFile.WriteString("\t<th>English</th>\n");
		htmlFile.WriteString("\t<th>ID</th>\n");
		htmlFile.WriteString("\t<th>Appearance Order</th>\n");
		htmlFile.WriteString("\t<th>CRC</th>\n");
		htmlFile.WriteString("\t<th>Has a Duplicate</th>\n");
		htmlFile.WriteString("\t</tr>\n");

		//Get name of info file (0100.BIN, etc.)
		const size_t lastIndex    = iter->first.find_first_of("TBL");
		const string infoFileName = iter->first.substr(0, lastIndex);
		map<string, DialogOrder>::const_iterator dialogOrderIter = dialogOrder.find(infoFileName);
		const bool bDialogOrderExists = dialogOrderIter != dialogOrder.end();

		//****Find Translated Text File****
		//Search for the corresponding translated file name
		const FileNameContainer* pMatchingTranslatedFileName = nullptr;
		for(const FileNameContainer& translatedFileName : translatedTextFiles)
		{
			if( translatedFileName.mNoExtension.find(iter->first) != string::npos )
			{
				pMatchingTranslatedFileName = &translatedFileName;
				break;
			}
		}


		//Open translated text file
		bool bCanUseTranslatedFile = true;
		TextFileData translatedFile(*pMatchingTranslatedFileName);
		if( !translatedFile.InitializeTextFile(true, false) )
		{
			printf("Unable to open the translation file %s.\n", pMatchingTranslatedFileName->mFullPath.c_str());
			bCanUseTranslatedFile = false;
		}

		//Make sure we have the correct amount of lines
		if( bCanUseTranslatedFile && iter->second.size() < translatedFile.mLines.size() )
		{
			printf("Unable to use tranlsted file: %s because the translation line count is incorrect.\n", pMatchingTranslatedFileName->mNoExtension.c_str());
			bCanUseTranslatedFile = false;
		}
		//****Done Finding Translated Text File****

		//Create entries for all images
		int num = 0;
		for(const FileNameContainer& fileNameInfo : iter->second)
		{
			const unsigned long crc   = crcMap[&iter->second[num]];
			const bool bIsDuplicate   = dupCrcMap.find(crc) != dupCrcMap.end();
			const unsigned short id   = sakuraFileIter->second->mStringInfoArray[num].mStringId;
			const vector<int>* pOrder = bDialogOrderExists && dialogOrderIter->second.idAndOrder.find(id) != dialogOrderIter->second.idAndOrder.end() ? &dialogOrderIter->second.idAndOrder.find(id)->second : nullptr;
			const bool bIsLipsEntry   = pOrder ? dialogOrderIter->second.idAndLips.find(id)->second : false;
			const char* bgColor       = bForRelease ? "e3fec8" : "fefec8";
			if( bIsLipsEntry )
			{
				bgColor = "fec8c8"; //LIPS event are highlighted in pink
			}
			else if( !pOrder )
			{
				bgColor = "B9B9B9"; //Gray out rows that don't need translations
			}

			const char* pVarSuffix = fileNameInfo.mNoExtension.c_str();
			fprintf(htmlFile.GetFileHandle(), "<tr id=\"tr_edit_%i\" bgcolor=\"#%s\">\n", num + 1, bgColor);
			snprintf(buffer, 2048, "<td align=\"center\" width=\"20\">%i</td>", num + 1);
			htmlFile.WriteString(string(buffer));

			int faceImageId = 0;
			if( bDialogOrderExists )
			{
				DialogOrder::IdAndImageMap::const_iterator imageIdIter = dialogOrderIter->second.idAndImage.find(id);
				faceImageId = imageIdIter != dialogOrderIter->second.idAndImage.end() ? imageIdIter->second : 0;
			}
			if( faceImageId != 0 )
			{
				snprintf(buffer, 2048, "<td width=\"48\"><img src=\"..\\ExtractedData\\Faces\\%sFCE\\%04x.png\"></td>", infoFileName.c_str(), faceImageId);
			}
			else
			{
				snprintf(buffer, 2048, "<td width=\"48\"><img src=\"..\\ExtractedData\\Faces\\UnknownFace.png\"></td>");
			}
			htmlFile.WriteString(string(buffer));

			snprintf(buffer, 2048, "<td width=\"240\"><img src=\"..\\ExtractedData\\Dialog\\%sTBL\\%s\"></td>", infoFileName.c_str(), fileNameInfo.mFileName.c_str());
			htmlFile.WriteString(string(buffer));

			//snprintf(buffer, 2048, "<td width=480><div id=\"edit_%s\" contenteditable=\"true\" onChange=\"SaveEdits('%i.bmp', 'edit_%i')\">Untranslated</div></td>", pVarSuffix, num + 1, num + 1);
			const char *pEnglishText = bCanUseTranslatedFile ? translatedFile.mLines[num].mFullLine.c_str() : "Untranslated";
			snprintf(buffer, 2048, "<td width=\"480\"><textarea id=\"edit_%s\" contenteditable=true onchange=\"SaveEdits('%i.bmp', 'edit_%i', '%lu')\" style=\"border: none; width: 100%%; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;\">%s</textarea></td>", pVarSuffix, num + 1, num + 1, crc, pEnglishText);
			htmlFile.WriteString(string(buffer));

			snprintf(buffer, 2048, "<td align=\"center\" width=\"120\">%02x (%i)</td>", id, id);
			htmlFile.WriteString(string(buffer));

			if( pOrder )
			{
				htmlFile.WriteString("<td align=\"center\" width=\"120\">");

				for(size_t orderIndex = 0; orderIndex < pOrder->size(); ++orderIndex)
				{
					snprintf(buffer, 2048, "Order: %i", (*pOrder)[orderIndex]);
					htmlFile.WriteString(string(buffer));
					if( orderIndex + 1 < pOrder->size() )
					{
						htmlFile.WriteString(", ");
					}
				}
				htmlFile.WriteString("</td>");
			}
			else
			{
				snprintf(buffer, 2048, "<td align=\"center\" width=\"120\">Order: -1</td>");
				htmlFile.WriteString(string(buffer));
			}

			snprintf(buffer, 2048, "<td id=\"crc_%i\" align=\"center\" width=\"20\">%08x</td>", num + 1, crc);
			htmlFile.WriteString(string(buffer));

			snprintf(buffer, 2048, "<td id=\"dup_%i\" align=\"center\" width=\"20\">%s</td>", num + 1, bIsDuplicate ? "true" : "false");
			htmlFile.WriteString(string(buffer));

			htmlFile.WriteString("</tr>\n");

			++num;
		}
		htmlFile.WriteString("</table><br>\n");

		htmlFile.WriteString("<?php\n}\n?>");

		//End file
		htmlFile.WriteString("</body>\n");
		htmlFile.WriteString("</html>\n");
	}

	return true;
}

bool CreateWKLSpreadSheets(const string& dialogImageDirectory, const string& duplicatesFileName, const string& sakuraDirectory)
{
	printf("Parsing duplicates file\n");

	//Load duplicate info
	FileNameContainer dupFileNameContainer(duplicatesFileName.c_str());
	TextFileData duplicatesFile(dupFileNameContainer);
	if( !duplicatesFile.InitializeTextFile() )
	{
		printf("Unable to open duplicates file.\n");
		return false;
	}

	map<unsigned long, vector<string>> dupCrcMap; //crc, fileNames
	map<string, unsigned long> dupFilenameCrcMap; //fileName, crc
	const size_t numDupLines = duplicatesFile.mLines.size();
	for(size_t i = 0; i < numDupLines; ++i)
	{
		if( duplicatesFile.mLines[i].mWords.size() != 2 )
		{
			printf("Invalid duplicate file format");
			return false;
		}

		const unsigned long crc = strtoul(duplicatesFile.mLines[i].mWords[1].c_str(), nullptr, 0); 
		dupCrcMap[crc].push_back( duplicatesFile.mLines[i].mWords[0] );
		dupFilenameCrcMap[duplicatesFile.mLines[i].mWords[0]] = crc;
	}

	vector<FileNameContainer> allFilesInImageDirectory;
	FindAllFilesWithinDirectory(dialogImageDirectory, allFilesInImageDirectory);

	vector<FileNameContainer> imageFiles;
	GetAllFilesOfType(allFilesInImageDirectory, ".png", imageFiles);

	vector<FileNameContainer> allSakura1Files;
	FindAllFilesWithinDirectory(sakuraDirectory, allSakura1Files);

	//Find all dialog files
	vector<FileNameContainer> tableFiles;
	GetAllFilesOfType(allSakura1Files, "WKL", tableFiles);

	//Seperate all files into their own directories
	map<string, vector<FileNameContainer>> directoryMap;
	for(const FileNameContainer& imageFileNameInfo : imageFiles)
	{
		const size_t lastOf = imageFileNameInfo.mPathOnly.find_last_of('\\');
		const string leaf   = imageFileNameInfo.mPathOnly.substr(lastOf+1);
		if( lastOf != string::npos && leaf.size() > 0 )
		{
			directoryMap[leaf].push_back(imageFileNameInfo);
		}
	}

	const string htmlHeader1("<html>\n<head><style>textarea {width: 100%;top: 0; left: 0; right: 0; bottom: 0; position: absolute; resize: none;-webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;} table {border-collapse: collapse;} table, th, td { position: relative; border: 1px solid black;}");
	const string htmlHeader2("#myProgress {width: 100%;	background-color: #ddd;} #myBar {width: 1%;height: 30px; background-color: #4CAF50;}</style>");
	const string htmlHeader = htmlHeader1 + htmlHeader2;

	//Create html files for each directory
	static char buffer[2048];
	const string outputDirectory = dialogImageDirectory + string("..\\..\\Translation\\");
	if( !CreateDirectoryHelper(outputDirectory) )
	{
		printf("Unable to create translation directory %s.  Error: (%d)\n", outputDirectory.c_str(), GetLastError());
		return false;
	}

	//Sort bmp files from smallest to greatest (001.bmp -> 999.bmp)
	for(map<string, vector<FileNameContainer>>::iterator iter = directoryMap.begin(); iter != directoryMap.end(); ++iter)
	{
		std::sort(iter->second.begin(), iter->second.end());
	}

	for(map<string, vector<FileNameContainer>>::const_iterator iter = directoryMap.begin(); iter != directoryMap.end(); ++iter)
	{
		printf("Creating html file for %s\n", iter->first.c_str());

		//Store all crcs based on FileNameContainer*
		const size_t numImageFiles = iter->second.size();
		map<const FileNameContainer*, unsigned long> crcMap;
		for(size_t fileIndex = 0; fileIndex < numImageFiles; ++fileIndex)
		{
			const FileNameContainer* pFileContainer = &iter->second[fileIndex];
			FileData fileData;
			if( !fileData.InitializeFileData(*pFileContainer) )
			{
				printf("Unable to open file for crc generation");
				break;
			}

			crcMap[pFileContainer] = fileData.GetCRC();
		}

		//Find all duplicates within this file. That is, duplicates that are internal to only this file.
		printf("Finding Duplicates for %s \n", iter->first.c_str());
		int numberOfDuplicatesFound = 0;
		map<string, vector<string>> duplicatesMap; //FileName, Vector<Duplicate FileNames>
		for(size_t fileIndex = 0; fileIndex < numImageFiles; ++fileIndex)
		{
			const FileNameContainer& firstImageName = iter->second[fileIndex];

			if( crcMap.find(&firstImageName) == crcMap.end() )
			{
				printf("Crc data not found.  Unable to generate web files.\n");
				return false;
			}

			const unsigned long imageCrc = crcMap[&firstImageName];

			//See if there is a duplicate in any file
			map<unsigned long, vector<string>>::iterator dupCrcMapIter = dupCrcMap.find(imageCrc);
			if( dupCrcMapIter != dupCrcMap.end() )
			{
				const size_t numCrcMatchesForFile = dupCrcMapIter->second.size();
				if( numCrcMatchesForFile == 0 )
				{
					printf("No crc matches found for file.  Something went wrong.\n");
					return false;
				}
				numberOfDuplicatesFound++;
			}

			//Make sure we haven't already found a match for this file
			bool bExistingDupFound = false;
			for(map<string, vector<string>>::const_iterator mapIter = duplicatesMap.begin(); mapIter != duplicatesMap.end(); ++mapIter)
			{
				const size_t numDups = mapIter->second.size();
				for(size_t dupIndex = 0; dupIndex < numDups; ++dupIndex)
				{
					if( mapIter->second[dupIndex] == firstImageName.mNoExtension )
					{
						bExistingDupFound = true;
						break;
					}
				}

				if( bExistingDupFound )
				{
					break;
				}
			}

			if( bExistingDupFound )
			{
				continue;
			}

			//Check all other files
			for(size_t secondFileIndex = fileIndex + 1; secondFileIndex < numImageFiles; ++secondFileIndex)
			{
				const FileNameContainer* pSecondFile = &iter->second[secondFileIndex];
				if( crcMap.find(pSecondFile) == crcMap.end() )
				{
					printf("Crc data not found.  Unable to generate web files.\n");
					return false;
				}

				if( crcMap[pSecondFile] == imageCrc )
				{
					duplicatesMap[firstImageName.mNoExtension].push_back( iter->second[secondFileIndex].mNoExtension );
				}
			}
		}

		map<string, bool> validMiscFileNames;
		validMiscFileNames["Misc_0_1a"] = true;
		validMiscFileNames["Misc_0_1b"] = true;
		validMiscFileNames["Misc_0_1c"] = true;
		validMiscFileNames["Misc_0_1d"] = true;
		validMiscFileNames["Misc_0_1e"] = true;
		validMiscFileNames["Misc_0_1f"] = true;
		validMiscFileNames["Misc_0_5"]  = true;
		validMiscFileNames["Misc_0_6"]  = true;
		validMiscFileNames["Misc_0_7"]  = true;
		validMiscFileNames["Misc_0_8"]  = true;
		validMiscFileNames["Misc_0_9"]  = true;
		validMiscFileNames["Misc_0_10"] = true;
		validMiscFileNames["Misc_0_11"] = true;
		validMiscFileNames["Misc_0_12"] = true;
		validMiscFileNames["Misc_0_13"] = true;
		validMiscFileNames["Misc_0_14"] = true;
		validMiscFileNames["Misc_0_15"] = true;
		validMiscFileNames["Misc_0_16"] = true;
		validMiscFileNames["Misc_0_17"] = true;
		validMiscFileNames["Misc_0_18"] = true;
		validMiscFileNames["Misc_0_19"] = true;
		validMiscFileNames["Misc_0_20"] = true;
		validMiscFileNames["Misc_0_a"]  = true;
		validMiscFileNames["Misc_0_b"]  = true;
		validMiscFileNames["Misc_0_c"]  = true;
		validMiscFileNames["Misc_0_d"]  = true;
		validMiscFileNames["Misc_0_e"]  = true;
		validMiscFileNames["Misc_0_f"]  = true;
		validMiscFileNames["Misc_3_3"]  = true;
		validMiscFileNames["Misc_3_4"]  = true;
		validMiscFileNames["Misc_3_15"] = true;
		validMiscFileNames["Misc_8_4"]  = true;
		validMiscFileNames["Misc_13_0"] = true;
		validMiscFileNames["Misc_13_1"] = true;
		validMiscFileNames["Misc_14_1"] = true;
		validMiscFileNames["Misc_14_2"] = true;
		validMiscFileNames["Misc_14_3"] = true;
		validMiscFileNames["Misc_14_4"] = true;
		validMiscFileNames["Misc_15_1"] = true;
		validMiscFileNames["Misc_15_2"] = true;
		
		const string MiscPrefix("Misc_");

		//Order the filename list so that Misc files appear last
		vector<const FileNameContainer*> imagesToProcess;
		for(const FileNameContainer& fileNameInfo : iter->second)
		{
			if( fileNameInfo.mNoExtension.compare(0, MiscPrefix.length(), MiscPrefix) == 0 )
			{
				continue;
			}
			imagesToProcess.push_back(&fileNameInfo);
		}
		for(const FileNameContainer& fileNameInfo : iter->second)
		{
			if( fileNameInfo.mNoExtension.compare(0, MiscPrefix.length(), MiscPrefix) == 0 )
			{
				imagesToProcess.push_back(&fileNameInfo);
			}
		}

		const string htmlFileName = outputDirectory + iter->first + string(".php");

		TextFileWriter htmlFile;
		if( !htmlFile.OpenFileForWrite(htmlFileName) )
		{
			printf("Unable to create an html file: %s", htmlFileName.c_str());
			continue;
		}

		//Common header stuff
		htmlFile.WriteString(htmlHeader);
		fprintf(htmlFile.GetFileHandle(), "\n<div id=\"FileName\" style=\"display: none;\">%s</div>\n", iter->first.c_str());
		fprintf(htmlFile.GetFileHandle(), "\n<div id=\"LastImageIndex\" style=\"display: none;\">%i</div>\n", numImageFiles);
		fprintf(htmlFile.GetFileHandle(), "\n<div id=\"NumberOfDuplicates\" style=\"display: none;\">%i</div>\n", numberOfDuplicatesFound);

		htmlFile.WriteString("<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.1/jquery.min.js\">\n\n");

		//Load Data on startup
		htmlFile.WriteString("\t$( window ).on( \"load\", function()\n {\n\t\tOnStartup();\n});\n\n");
		htmlFile.WriteString("</script>\n\n");

		//Begin functions
		htmlFile.WriteString("<script type=\"text/javascript\">\n");

		//SaveDuplicateData function
		htmlFile.WriteString("function SaveDuplicateData(inDialogImageName, inEnglish, inDivID, inCRC)\n");
		htmlFile.WriteString("{\n");
		htmlFile.WriteString("     var fileName = document.getElementById(\"FileName\").innerHTML;\n");
		htmlFile.WriteString("     	$.ajax({\n");
		htmlFile.WriteString("          type: \"POST\",\n");
		htmlFile.WriteString("          url: \"UpdateTranslationTest.php\",\n");
		htmlFile.WriteString("          data: { inTBLFileName: fileName, inImageName:inDialogImageName, inTranslation:inEnglish, inDivId:inDivID, inCrc:inCRC },\n");
		htmlFile.WriteString("          success: function(result)\n");
		htmlFile.WriteString("          {\n");
		htmlFile.WriteString("               var trId = \"tr_\" + inDivID;\n");
		htmlFile.WriteString("               if( inEnglish != \"Untranslated\" && inEnglish != \"<div>Untranslated</div>\")\n");
		htmlFile.WriteString("               {\n");
		htmlFile.WriteString("                    if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
		htmlFile.WriteString("                    {\n");
		htmlFile.WriteString("                         document.getElementById(trId).bgColor = \"#e3fec8\";\n");
		htmlFile.WriteString("                    }\n");
		htmlFile.WriteString("               }\n");
		htmlFile.WriteString("               else\n");
		htmlFile.WriteString("               {\n");
		htmlFile.WriteString("                    document.getElementById(trId).bgColor = \"#fefec8\";\n");
		htmlFile.WriteString("               }\n");
		htmlFile.WriteString("          }\n");
		htmlFile.WriteString("     });\n");
		htmlFile.WriteString("}\n\n");

		//SaveData function
		htmlFile.WriteString("function SaveData(inDialogImageName, inDivID, inCRC)\n");
		htmlFile.WriteString("{\n");
		htmlFile.WriteString("     var translatedText = document.getElementById(inDivID).value;\n");
		htmlFile.WriteString("     var fileName = document.getElementById(\"FileName\").innerHTML;\n");
		htmlFile.WriteString("     $.ajax({\n");
		htmlFile.WriteString("          type: \"POST\",\n");
		htmlFile.WriteString("          url: \"UpdateWKLTranslation.php\",\n");
		htmlFile.WriteString("          data: { inTBLFileName: fileName, inImageName:inDialogImageName, inTranslation:translatedText, inDivId:inDivID, inCrc:inCRC },\n");
		htmlFile.WriteString("          success: function(result)\n");
		htmlFile.WriteString("          {\n");
		htmlFile.WriteString("                var trId = \"tr_\" + inDivID;\n");
		htmlFile.WriteString("                if( translatedText != \"Untranslated\" && translatedText != \"<div>Untranslated</div>\")\n");
		htmlFile.WriteString("                {\n");
		htmlFile.WriteString("                     if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
		htmlFile.WriteString("                     {\n");
		htmlFile.WriteString("                          document.getElementById(trId).bgColor = \"#e3fec8\";\n");
		htmlFile.WriteString("                     }\n");
		htmlFile.WriteString("                }\n");
		htmlFile.WriteString("                else\n");
		htmlFile.WriteString("                {\n");
		htmlFile.WriteString("                     document.getElementById(trId).bgColor = \"#fefec8\";\n");
		htmlFile.WriteString("                }\n");
		htmlFile.WriteString("         }\n");
		htmlFile.WriteString("    });\n");
		htmlFile.WriteString("}\n");

		//SaveEdits function
		htmlFile.WriteString("function SaveEdits(inDialogImageName, inDivID, inCRC)\n");
		htmlFile.WriteString("{\n");
		htmlFile.WriteString("     SaveData(inDialogImageName, inDivID, inCRC);\n\n");
		htmlFile.WriteString("}\n");

		//Export data function
		htmlFile.WriteString("function ExportData()\n");
		htmlFile.WriteString("{\n");
		htmlFile.WriteString("     $(\"textarea\").each ( function ()\n");
		htmlFile.WriteString("     {\n");
		htmlFile.WriteString("          var thisText = $(this).text();\n");
		htmlFile.WriteString("          thisText = thisText.replace(/<br>/g, '&ltbr&gt');\n");
		htmlFile.WriteString("          thisText = thisText.replace(/<sp>/g, '&ltsp&gt');\n");
		htmlFile.WriteString("          document.write(thisText + \"<br>\");\n");
		htmlFile.WriteString("     });\n");
		htmlFile.WriteString("}\n");

		//UpdateLoadingBar function
		htmlFile.WriteString("function UpdateLoadingBar(inLoadPercentage)\n");
		htmlFile.WriteString("{\n");
		htmlFile.WriteString("     var elem            = document.getElementById(\"myBar\");\n");
		htmlFile.WriteString("     var loadPercentElem = document.getElementById(\"LoadPercent\");\n");
		htmlFile.WriteString("     inLoadPercentage    = Math.floor( inLoadPercentage*100 + 0.5);\n");
		htmlFile.WriteString("     elem.style.width = inLoadPercentage + '%';\n");
		htmlFile.WriteString("     loadPercentElem.innerHTML = (inLoadPercentage).toString() + \"%\";\n");
		htmlFile.WriteString("}\n");

		//AttemptToLoadDuplicateData function
		htmlFile.WriteString("function AttemptToLoadDuplicateData(inDivID, inCRC, inTrID, inPercentComplete)\n");
		htmlFile.WriteString("{\n");
		htmlFile.WriteString("     $.ajax({\n");
		htmlFile.WriteString("     type: \"POST\",\n");
		htmlFile.WriteString("     url: \"GetTranslationFromCRC.php\",\n");
		htmlFile.WriteString("     data: { inCrc: inCRC},\n");
		htmlFile.WriteString("     success: function(result)\n");
		htmlFile.WriteString("     {\n");
		htmlFile.WriteString("          UpdateLoadingBar(inPercentComplete);\n\n");
		htmlFile.WriteString("          var json = $.parseJSON(result);\n");
		htmlFile.WriteString("          var i;\n");
		htmlFile.WriteString("          for (i = 0; i < json.length; i++)\n");
		htmlFile.WriteString("          {\n");
		htmlFile.WriteString("               var jsonEntry = json[i];\n");
		htmlFile.WriteString("               var english   = jsonEntry.English.replace(/\\\\/g, \'\');\n");
		htmlFile.WriteString("               if( english != \"Untranslated\" )\n");
		htmlFile.WriteString("               {\n");
		htmlFile.WriteString("                    if( document.getElementById(inTrID).bgColor != \"#fec8c8\" )\n");
		htmlFile.WriteString("                    {\n");
		htmlFile.WriteString("                         document.getElementById(inTrID).bgColor = \"#e3fec8\";\n");
		htmlFile.WriteString("                    }\n");
		htmlFile.WriteString("                    $(inDivID).html(english);\n");
		htmlFile.WriteString("                    var divID = inDivID.replace(\"#\", \"\");\n");
		htmlFile.WriteString("                    var imageName = divID.replace(\"edit_\", \"\") + \".bmp\";\n");
		htmlFile.WriteString("                    SaveDuplicateData(imageName, english, divID, inCRC);\n");
		htmlFile.WriteString("                    return;\n");
		htmlFile.WriteString("               }\n");
		htmlFile.WriteString("          }\n");
		htmlFile.WriteString("     },\n");
		htmlFile.WriteString("     error: function()\n");
		htmlFile.WriteString("     {\n");
		htmlFile.WriteString("     }\n");
		htmlFile.WriteString("   });\n");
		htmlFile.WriteString("}\n");

		//LoadData function
		htmlFile.WriteString("function LoadData()\n{\n");
		int imageNum = 0;
		for(const FileNameContainer* pFileNameInfo : imagesToProcess)
		{
			const FileNameContainer& fileNameInfo = *pFileNameInfo;

			//Only output certain of the Misc files
			if( fileNameInfo.mNoExtension.compare(0, MiscPrefix.length(), MiscPrefix) == 0 )
			{
				if( validMiscFileNames.find(fileNameInfo.mNoExtension) == validMiscFileNames.end() )
				{
					continue;
				}
			}

			const unsigned long crc   = crcMap[&fileNameInfo];
			snprintf(buffer, 2048, "     AttemptToLoadDuplicateData('#edit_%i', %lu, 'tr_edit_%i', 1);\n", imageNum + 1, crc, imageNum + 1);
			htmlFile.WriteString(string(buffer));
			++imageNum;
		}
		htmlFile.WriteString("}\n");


		//FixOnChangeEditableElements - A function that saves the data whenever input happens
		htmlFile.WriteString("function FixOnChangeEditableElements()\n{\n");
		htmlFile.WriteString("\tvar tags = document.querySelectorAll('[contenteditable=true][onChange]');//(requires FF 3.1+, Safari 3.1+, IE8+)\n");
		htmlFile.WriteString("\tfor (var i=tags.length-1; i>=0; i--) if (typeof(tags[i].onblur)!='function')\n{\n");
		htmlFile.WriteString("\t\ttags[i].onfocus = function()\n{\n");
		htmlFile.WriteString("\t\tthis.data_orig=this.innerHTML;\n};\n");
		htmlFile.WriteString("\t\ttags[i].onblur = function()\n{\n");
		htmlFile.WriteString("\t\tif( this.innerHTML != this.data_orig)\n");
		htmlFile.WriteString("\t\t\tthis.onchange();\n");
		htmlFile.WriteString("\tdelete this.data_orig;\n};\n}\n}\n");

		//Startup function
		htmlFile.WriteString("function OnStartup()\n{\n");
		htmlFile.WriteString("\tFixOnChangeEditableElements();\n}\n");

		//End scripts
		htmlFile.WriteString("</script>\n\n");
		htmlFile.WriteString("</head>\n\n");

		//Call startup function
		htmlFile.WriteString("<body>\n");

		htmlFile.WriteString("<?php include 'GetUserPermissions.php';\n");
		htmlFile.WriteString("\t$bPermissionFound = false;\n");
		htmlFile.WriteString("\tforeach ($allowedFiles as $value)\n{");
		fprintf(htmlFile.GetFileHandle(), "\t\tif( $value == \"%s\" )\n\t\t{\n", iter->first.c_str());
		htmlFile.WriteString("\t\t\t$bPermissionFound = true;\n");
		htmlFile.WriteString("\t\t\tbreak;\n\t\t}\n\t}");
		htmlFile.WriteString("if( $bPermissionFound )\n{\n?>");

		fprintf(htmlFile.GetFileHandle(), "<article><header align=\"center\"><h1>Dialog For %s</h1></header></article>\n", iter->first.c_str());

		htmlFile.WriteString("<br>\n");
		htmlFile.WriteString("<b>Instructions:</b><br>\n");
		htmlFile.WriteString("-Please let me know once the file is complete by emailing me @ sakurataisentranslation@gmail.com<br>\n");
		htmlFile.WriteString("-This page is best displayed using Chrome.  Otherwise some of the table borders are missing for some reason.<br>\n");
		htmlFile.WriteString("-Your changes are automatically saved.<br>\n");
		htmlFile.WriteString("-Press the Load Data button when you come back to the page to load your changes.<br>\n");
		htmlFile.WriteString("-Please wait for the Load Bar to complete.  It's a bit slow, but as more of the file is translated, it will speed up.  If it gets stuck in the 90's, that's fine, consider it done. I'll fix this bug soon.<br><br>\n");
		
		htmlFile.WriteString("<b>Naming Conventions:</n><br>\n");
		htmlFile.WriteString("<a href=\"https://docs.google.com/spreadsheets/d/1rgafQe78vML_xbxnYuOSlO8P5C8nuhgLjMJOExUQsm0/edit?usp=sharing\" target=\"_blank\">Click here to view the naming conventions for Characters, Locations, and Terms</a> <br>\n");

		htmlFile.WriteString("<?php\n");
		htmlFile.WriteString("$currUser = $_SERVER['PHP_AUTH_USER'];\n");
		htmlFile.WriteString("if( $currUser == \"swtranslator\" )\n");
		htmlFile.WriteString("{\n");
		htmlFile.WriteString("echo \"<input align=\\\"center\\\" type=\\\"button\\\" value=\\\"Export Data\\\" onclick=\\\"ExportData()\\\"/>\";\n");
		htmlFile.WriteString("}\n");
		htmlFile.WriteString("?>\n\n");

		//Load Data button
		htmlFile.WriteString("<table align=\"center\">\n");
		htmlFile.WriteString("     <tr>\n");
		htmlFile.WriteString("          <td>\n");
		htmlFile.WriteString("               <input align=\"center\" type=\"button\" value=\"Load Data\" onclick=\"LoadData()\"/>\n");
		htmlFile.WriteString("          </td>\n");
		htmlFile.WriteString("     </tr>\n");
		htmlFile.WriteString("     <tr>\n");
		htmlFile.WriteString("          <td>\n");
		htmlFile.WriteString("               Duplicate Load Progress\n");
		htmlFile.WriteString("          </td>\n");
		htmlFile.WriteString("          <td width=\"400\">\n");
		htmlFile.WriteString("               <div id=\"myProgress\">\n");
		htmlFile.WriteString("               <div id=\"myBar\"></div>\n");
		htmlFile.WriteString("          </div>\n");
		htmlFile.WriteString("          </td>\n");
		htmlFile.WriteString("          <td>\n");
		htmlFile.WriteString("               <div id=\"LoadPercent\">0</div>\n");
		htmlFile.WriteString("          </td>\n");
		htmlFile.WriteString("     </tr>\n");
		htmlFile.WriteString("</table><br>\n\n");


		//Write table
		htmlFile.WriteString("<table>\n");
		htmlFile.WriteString("\t<tr bgcolor=\"#c8c8fe\">\n");
		htmlFile.WriteString("\t<th>#</th>\n");
		htmlFile.WriteString("\t<th>Japanese</th>\n");
		htmlFile.WriteString("\t<th>English</th>\n");
		htmlFile.WriteString("\t<th>ID</th>\n");
		htmlFile.WriteString("\t<th>CRC</th>\n");
		htmlFile.WriteString("\t<th>Has a Duplicate</th>\n");
		htmlFile.WriteString("\t</tr>\n");

		//Get name of info file (0100.BIN, etc.)
		const string infoFileName = iter->first;

		//Create entries for all images
		int num = 0;
		for(const FileNameContainer* pFileNameInfo : imagesToProcess)
		{
			const FileNameContainer& fileNameInfo = *pFileNameInfo;

			//Only output certain of the Misc files
			if( fileNameInfo.mNoExtension.compare(0, MiscPrefix.length(), MiscPrefix) == 0 )
			{
				if( validMiscFileNames.find(fileNameInfo.mNoExtension) == validMiscFileNames.end() )
				{
					continue;
				}
			}

			const unsigned long crc   = crcMap[&fileNameInfo];
			const bool bIsDuplicate   = dupCrcMap.find(crc) != dupCrcMap.end();
			const char* bgColor       = "fefec8";
			
			const char* pVarSuffix = fileNameInfo.mNoExtension.c_str();
			fprintf(htmlFile.GetFileHandle(), "<tr id=\"tr_edit_%i\" bgcolor=\"#%s\">\n", num + 1, bgColor);
			snprintf(buffer, 2048, "<td align=\"center\" width=\"20\">%i</td>", num + 1);
			htmlFile.WriteString(string(buffer));

			//japanese
			snprintf(buffer, 2048, "<td width=\"150\"><img src=\"..\\ExtractedData\\WKL\\%s\\%s\"></td>", infoFileName.c_str(), fileNameInfo.mFileName.c_str());
			htmlFile.WriteString(string(buffer));

			//english
			snprintf(buffer, 2048, "<td width=\"480\"><textarea id=\"edit_%i\" contenteditable=true onchange=\"SaveEdits('%s.bmp', 'edit_%i', '%lu')\" style=\"border: none; width: 100%%; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;\">Untranslated</textarea></td>", num + 1, pVarSuffix, num + 1, crc);
			htmlFile.WriteString(string(buffer));
			
			//ID
			snprintf(buffer, 2048, "<td id=\"id_%i\" align=\"center\" width=\"20\">%s</td>", num + 1, fileNameInfo.mNoExtension.c_str());
			htmlFile.WriteString(string(buffer));

			//crc
			snprintf(buffer, 2048, "<td id=\"crc_%i\" align=\"center\" width=\"20\">%08x</td>", num + 1, crc);
			htmlFile.WriteString(string(buffer));

			//has a duplicate
			snprintf(buffer, 2048, "<td id=\"dup_%i\" align=\"center\" width=\"20\">%s</td>", num + 1, bIsDuplicate ? "true" : "false");
			htmlFile.WriteString(string(buffer));

			htmlFile.WriteString("</tr>\n");

			++num;
		}
		htmlFile.WriteString("</table><br>\n");

		htmlFile.WriteString("<?php\n}\n?>");

		//End file
		htmlFile.WriteString("</body>\n");
		htmlFile.WriteString("</html>\n");
	}

	return true;
}

void ExtractPalettedImage(const string& fileName, const string& paletteFileName, const int offset, const int width, const int height, const int numTilesPerRow, const int numColors, const bool bForceBitmap, 
	                      const string& outDirectory)
{
	FileNameContainer imageFileNameInfo(fileName.c_str());
	FileNameContainer paletteFileNameInfo(paletteFileName.c_str());
	FileData paletteFile;
	if( !paletteFile.InitializeFileData(paletteFileNameInfo) )
	{
		return;
	}

	const string outFileName = outDirectory + imageFileNameInfo.mNoExtension + string(".bmp");

	ExtractImage(imageFileNameInfo, outFileName, paletteFile, width, height, numTilesPerRow, numColors, offset, true, bForceBitmap);
}

//FCE files contain faces that appear during the story dialog
void ExtractFCEFiles(const string& sakuraDirectory, const string& paletteFileName, const string& outDirectory)
{
	//Find all translated text files
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(sakuraDirectory, allFiles);
	if( !allFiles.size() )
	{
		return;
	}

	vector<FileNameContainer> faceFiles;
	GetAllFilesOfType(allFiles, "FCE.BIN", faceFiles);

	FileNameContainer paletteFileNameInfo(paletteFileName.c_str());
	FileData paletteData;
	if( !paletteData.InitializeFileData(paletteFileNameInfo) )
	{
		return;
	}

	for(const FileNameContainer& fileNameInfo : faceFiles)
	{
		const string subDirName = outDirectory + fileNameInfo.mNoExtension + Seperators;
		if( !CreateDirectoryHelper(subDirName) )
		{
			printf("Unable to create directory: %s", subDirName.c_str());
			return;
		}

		//Extract the whole sheet
		const string wholeSheetName = subDirName + fileNameInfo.mNoExtension + string(".bmp");
		ExtractImage(fileNameInfo, wholeSheetName, paletteData, 40, 48, 1, 256, 0xA0, true);

		//**Now extract the individual faces**
		FileData faceFile;
		if( !faceFile.InitializeFileData(fileNameInfo) )
		{
			return;
		}

		//Read in the head for the face file
		const unsigned short faceHeaderSize = 0xA0;
		unsigned char faceFileHeader[faceHeaderSize];
		memcpy_s(faceFileHeader, faceHeaderSize, faceFile.GetData(), faceHeaderSize);

		//Figure out how many entries there are
		int numFacesInFile =  0;
		for(unsigned short s = 0; s < faceHeaderSize/2; ++s)
		{
			if( !((unsigned short*)faceFileHeader)[s] )
			{
				break;
			}

			++numFacesInFile;
		}

		const int faceImageSize = 40*48 + 40*16 + 40*16;
		const unsigned int numExpectedBytes = numFacesInFile*faceImageSize;
		if(faceFile.GetDataSize() - faceHeaderSize != numExpectedBytes)
		{
			printf("FaceFile %s has an unexpected file size.\n", fileNameInfo.mFileName.c_str());
			return;
		}
		faceFile.Close();

		//Output individuals faces
		static const int faceFileNameBufferSize = 50;
		char faceFileNameBuffer[faceFileNameBufferSize];
		for(int i = 0; i < numFacesInFile; ++i)
		{
			sprintf_s(faceFileNameBuffer, faceFileNameBufferSize, "%02x%02x.bmp", faceFileHeader[i*2], faceFileHeader[i*2+1]);

			const string outFaceFileName = subDirName + faceFileNameBuffer;
			ExtractImage(fileNameInfo, outFaceFileName, paletteData, 40, 48, 1, 256, i*faceImageSize + faceHeaderSize, false);
		}
	}
}

struct SakuraPalette
{
	char* mpPaletteData = nullptr;
	int   mPaletteSize  = 0;

	SakuraPalette(const char* pInData, int paletteSize)
	{
		mPaletteSize = paletteSize;

		//Create palette data
		//Colors in the palette are in a 15 bit (5:5:5) format.  So we need to & every value by 0x7fff.
		static const unsigned int numBytesInPalette = mPaletteSize;
		const char* pOriginalPaletteData            = pInData;
		mpPaletteData                               = new char[numBytesInPalette];
		memset(mpPaletteData, 0, numBytesInPalette);
		memcpy_s(mpPaletteData, numBytesInPalette, pOriginalPaletteData, numBytesInPalette);

		for(unsigned int p = 0; p < numBytesInPalette; p += 2)
		{
			unsigned short* pPaletteValue = (unsigned short*)(mpPaletteData + p);
			*pPaletteValue = SwapByteOrder(*pPaletteValue);
			*pPaletteValue = (*pPaletteValue) & (unsigned short)(0x7fff);
			*pPaletteValue = SwapByteOrder(*pPaletteValue);
		}
	}

	~SakuraPalette()
	{
		delete[] mpPaletteData;
		mpPaletteData = nullptr;
	}
};

struct FaceHeaderInfo
{
	struct Entry
	{
		unsigned long  offsetToData;
		unsigned short unknown1;
		unsigned short unknown2;
	};

	Entry* mpData = nullptr;
	unsigned int mNumEntries;

	bool Initialize(const char* pFileData)
	{
		const char* pFaceData       = pFileData;
		unsigned long headerSize    = SwapByteOrder( *((unsigned long*)(pFaceData)) );

		//The header size should be divisible by 8			
		const unsigned int headerEntrySize = sizeof(Entry);
		if( headerSize == 0 || (headerSize % headerEntrySize) != 0 )
		{
			printf("Unable to figure out the size of the FACE file header.\n");
			return false;
		}

		mNumEntries = headerSize/headerEntrySize;
		mpData      = new Entry[mNumEntries];
		memcpy_s(mpData, headerEntrySize*mNumEntries, pFileData, headerEntrySize*mNumEntries);

		for(unsigned int i = 0; i < mNumEntries; ++i)
		{
			mpData[i].offsetToData = SwapByteOrder(mpData[i].offsetToData);
			mpData[i].unknown1     = SwapByteOrder(mpData[i].unknown1);
			mpData[i].unknown2     = SwapByteOrder(mpData[i].unknown2);
		}
		return true;
	}

	~FaceHeaderInfo()
	{
		delete[] mpData;
		mpData = nullptr;
	}
};

//FACE files contain faces that appear during the battle dialog
void ExtractFACEFiles(const string& sakuraDirectory, const string& outDirectory)
{
	//Find all translated text files
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(sakuraDirectory, allFiles);
	if( !allFiles.size() )
	{
		return;
	}

	vector<FileNameContainer> faceFiles;
	GetAllFilesOfType(allFiles, "FACE", faceFiles);

	for(const FileNameContainer& fileNameInfo : faceFiles)
	{
		printf("Extracting %s\n", fileNameInfo.mFileName.c_str());

		const string subDirName = outDirectory + fileNameInfo.mNoExtension + Seperators;
		if( !CreateDirectoryHelper(subDirName) )
		{
			printf("Unable to create directory: %s", subDirName.c_str());
			return;
		}

		FileData faceFile;
		if( !faceFile.InitializeFileData(fileNameInfo) )
		{
			return;
		}

		//Figure out how many faces images are in this file
		FaceHeaderInfo faceFileHeader;
		if( !faceFileHeader.Initialize( faceFile.GetData() ) )
		{
			printf("Could not extract %s\n", fileNameInfo.mFileName.c_str());
			return;
		}

		//Go through every image
		for(unsigned int i = 0; i < faceFileHeader.mNumEntries; ++i)
		{
			const unsigned int offsetToData        = faceFileHeader.mpData[i].offsetToData;
			const unsigned int compressedDataStart = offsetToData + 16;
			const unsigned int offsetToPalette     = SwapByteOrder( *((unsigned short*)(faceFile.GetData() + offsetToData + 10)) );

			if( compressedDataStart >= faceFile.GetDataSize() )
			{
				printf("Invalid data start position for file %i in %s\n", i, fileNameInfo.mFileName.c_str());
				continue;
				//return;
			}

			PRSDecompressor uncompressedImage;
			uncompressedImage.UncompressData( (void*)(faceFile.GetData() + compressedDataStart), faceFile.GetDataSize() - compressedDataStart );

			//Create palette data
			//Colors in the palette are in a 15 bit (5:5:5) format.  So we need to & every value by 0x7fff.
			static const unsigned int numBytesInPalette = 128;
			const char* pOriginalPaletteData             = faceFile.GetData() + offsetToData + offsetToPalette;
			char* pPaletteData                           = new char[512];//[numBytesInPalette];
			memset(pPaletteData, 0, 512);//numBytesInPalette);
			memcpy_s(pPaletteData, numBytesInPalette, pOriginalPaletteData, numBytesInPalette);
			for(int p = 0; p < numBytesInPalette; p += 2)
			{
				unsigned short* pPaletteValue = (unsigned short*)(pPaletteData + p);
				*pPaletteValue = SwapByteOrder(*pPaletteValue);
				*pPaletteValue = (*pPaletteValue) & (unsigned short)(0x7fff);
				*pPaletteValue = SwapByteOrder(*pPaletteValue);
			}
			
			//Create image from uncompressed data
			const string outFileName = subDirName + std::to_string(i) + string(".bmp");
			const unsigned char offsetToColorData = 0x40;
			//ExtractImageFromData(uncompressedImage.mpUncompressedData + offsetToColorData, uncompressedImage.mUncompressedDataSize - offsetToColorData, outFileName, pPaletteData, 128, 40, 48, 1, 128, 0, false, false);
			ExtractImageFromData(uncompressedImage.mpUncompressedData + offsetToColorData, uncompressedImage.mUncompressedDataSize - offsetToColorData, outFileName, pPaletteData, 512, 40, 80, 1, 128, 0, false, false);
			ExtractImageFromData(uncompressedImage.mpUncompressedData + offsetToColorData, uncompressedImage.mUncompressedDataSize - offsetToColorData, outFileName, pPaletteData, 512, 40, 80, 1, 128, 0, false, true);
			//ExtractImageFromData(uncompressedImage.mpUncompressedData + offsetToColorData, uncompressedImage.mUncompressedDataSize - offsetToColorData, outFileName, pPaletteData, numBytesInPalette, 40, 48, 1, 256, 0, false, true);
			//ExtractImageFromData(uncompressedImage.mpUncompressedData + offsetToColorData, uncompressedImage.mUncompressedDataSize - offsetToColorData, outFileName, pPaletteData, 128, 40, 48, 1, 128, 0, false);
			delete[] pPaletteData;
		}
	}
}

bool PatchFACEFiles(const string& rootSakuraDirectory, const string& rootTranslatedSakuraDirectory, const string& dataDirectory)
{
	printf("Patching FACE files\n");

	//Original Obstacle image 
	FileData face02File;
	const string face02FilePath = dataDirectory + Seperators + "FACE02.BIN";
	if( !face02File.InitializeFileData( FileNameContainer( face02FilePath.c_str() ) ) )
	{
		return false;
	}

	//Figure out how many faces images are in this file
	FaceHeaderInfo face02FileHeader;
	if( !face02FileHeader.Initialize( face02File.GetData() ) )
	{
		printf("Could not extract %s\n", face02FilePath.c_str());
		return false;
	}

	PRSDecompressor origObstacleImage;
	{
		const unsigned int offsetToData        = face02FileHeader.mpData[52].offsetToData;
		const unsigned int compressedDataStart = offsetToData + 16;

		origObstacleImage.UncompressData( (void*)(face02File.GetData() + compressedDataStart), face02File.GetDataSize() - compressedDataStart );
	}
	
	//Patched obstacle image
	BmpToSaturnConverter patchedObstacleImage;
	if( !patchedObstacleImage.ConvertBmpToSakuraFormat(dataDirectory + Seperators + "Obstacle.bmp", false) )
	{
		return false;
	}

	//Offset palette because original data uses only 128 colors
	for(unsigned int i = 0; i < patchedObstacleImage.GetImageDataSize(); ++i)
	{
		patchedObstacleImage.mTileExtractor.mTiles[0].mpTile[i] += 128;
	}

	//Find all files
	vector<FileNameContainer> allFiles;
	const string sakura2Directory = rootSakuraDirectory + "\\SAKURA2\\";
	FindAllFilesWithinDirectory(sakura2Directory, allFiles);
	if( !allFiles.size() )
	{
		return false;
	}

	vector<FileNameContainer> faceFiles;
	GetAllFilesOfType(allFiles, "FACE", faceFiles);

	const string sakura2PatchedDirectory = rootTranslatedSakuraDirectory + "\\SAKURA2\\";
	for(const FileNameContainer& fileNameInfo : faceFiles)
	{	
		FileData faceFile;
		if( !faceFile.InitializeFileData(fileNameInfo) )
		{
			return false;
		}

		//Figure out how many faces images are in this file
		FaceHeaderInfo faceFileHeader;
		if( !faceFileHeader.Initialize( faceFile.GetData() ) )
		{
			printf("Could not extract %s\n", fileNameInfo.mFileName.c_str());
			return false;
		}

		//Go through every image
		for(unsigned int i = 0; i < faceFileHeader.mNumEntries; ++i)
		{
			const unsigned int offsetToData        = faceFileHeader.mpData[i].offsetToData;
			const unsigned int compressedDataStart = offsetToData + 16;
			//const unsigned int offsetToPalette     = SwapByteOrder( *((unsigned short*)(faceFile.GetData() + offsetToData + 10)) );

			if( compressedDataStart >= faceFile.GetDataSize() )
			{
				continue;
			}

			PRSDecompressor uncompressedImage;
			uncompressedImage.UncompressData( (void*)(faceFile.GetData() + compressedDataStart), faceFile.GetDataSize() - compressedDataStart );

			//Check to see if this is the obstacle image
			if( uncompressedImage.mUncompressedDataSize == origObstacleImage.mUncompressedDataSize && 
				FileData::IsDataTheSame(uncompressedImage.mpUncompressedData, origObstacleImage.mpUncompressedData, uncompressedImage.mUncompressedDataSize) )
			{
				
				const string patchedFileName = sakura2PatchedDirectory + fileNameInfo.mFileName;
				FileReadWriter patchedFile;
				if( !patchedFile.OpenFile(patchedFileName) )
				{
					return false;
				}

				const int offsetToImageData = 0x40;
				char* pDataBuffer           = new char[uncompressedImage.mUncompressedDataSize];

				//Copy original data over which consists of a header, the face image, and eye open/close image
				memcpy_s(pDataBuffer, uncompressedImage.mUncompressedDataSize, uncompressedImage.mpUncompressedData, uncompressedImage.mUncompressedDataSize);

				//Now copy over the patched image
				memcpy_s(pDataBuffer + offsetToImageData, uncompressedImage.mUncompressedDataSize - offsetToImageData, patchedObstacleImage.GetImageData(), patchedObstacleImage.GetImageDataSize());

				//Compress the data
				SakuraCompressedData patchedObstacleData;
				patchedObstacleData.PatchDataInMemory(pDataBuffer, uncompressedImage.mUncompressedDataSize, true, false, origObstacleImage.mCompressedSize);

				//Verify the size fits
				if( patchedObstacleData.mDataSize > uncompressedImage.mCompressedSize )
				{
					printf("Patched obstacle image data is too big when compressed. Needs to be <= %i.  Is %i \n", uncompressedImage.mCompressedSize, patchedObstacleData.mDataSize);
					return false;
				}

				//Write it out
				patchedFile.WriteData(compressedDataStart, patchedObstacleData.mpCompressedData, patchedObstacleData.mDataSize, false);

				printf("     Patched Face: %i in %s\n", i, fileNameInfo.mNoExtension.c_str());
				break;
			}
		}
	}

	printf("Patch FACE files succeeded!\n");

	return true;
}

struct WklUncompressedData
{
	struct ImageInfo
	{
		unsigned short width;
		unsigned short height;
		unsigned short unknown1;
		unsigned short unknown2;
		unsigned short unknown3;
		unsigned short offsetBytesFromStart;
		unsigned short unknown4;
		unsigned short numBytes;

		void SwapEndianness()
		{
			width                = SwapByteOrder(width);
			height               = SwapByteOrder(height);
			offsetBytesFromStart = SwapByteOrder(offsetBytesFromStart);
			numBytes             = SwapByteOrder(numBytes);
		}
	};

	struct TranslatedImageData
	{
		TranslatedImageData(char* pInColorData, unsigned short inDataSize) : pColorData(pInColorData), dataSize(inDataSize){}

		char*          pColorData = nullptr;
		unsigned short dataSize   = 0;
	};

	unsigned short              mNumImages     = 0;
	unsigned long               mOffsetToImage = 0;
	ImageInfo*                  mpImageInfos   = nullptr;
	const char*                 mpFileData     = nullptr;
	vector<TranslatedImageData> mImageData;
	string                      mBlockPrefix;

	~WklUncompressedData()
	{
		delete[] mpImageInfos;
		mpImageInfos = nullptr;

		for(size_t i = 0; i < mImageData.size(); ++i)
		{
			delete[] mImageData[i].pColorData;
		}
		mImageData.clear();
	}

	void Initialize(const char* pInPrefix, unsigned long offsetToImage)
	{
		mBlockPrefix   = pInPrefix;
		mOffsetToImage = offsetToImage;
	}

	bool ReadInImages(const char* pInData)
	{
		mpFileData = pInData;

		memcpy_s(&mNumImages, sizeof(mNumImages), pInData, sizeof(mNumImages));
		mNumImages = SwapByteOrder(mNumImages);
		if( mNumImages == 0 )
		{
			return true;
		}
			
		mpImageInfos = new ImageInfo[mNumImages];
		memcpy_s(mpImageInfos, sizeof(ImageInfo)*mNumImages, pInData + 0x10, sizeof(ImageInfo)*mNumImages);

		for(unsigned short i = 0; i < mNumImages; ++i)
		{
			mpImageInfos[i].SwapEndianness();
		}

		return true;
	}

	bool DumpImages(const char* pInPaletteData, const unsigned int inPaletteSize, const string& inPrefix, const string& outDirectory) const
	{
		const unsigned int headerOffset = 0x10*(mNumImages + 1);
		const string bmpExtension(".bmp");

		for(unsigned short i = 0; i < mNumImages; ++i)
		{
			const string outFileName = outDirectory + inPrefix + string("_") + std::to_string(i) + bmpExtension;
			const char* pImageData = &mpFileData[ mpImageInfos[i].offsetBytesFromStart + headerOffset];
			ExtractImageFromData(pImageData, mpImageInfos[i].numBytes, outFileName, pInPaletteData, inPaletteSize, mpImageInfos[i].width, mpImageInfos[i].height, 1, 16, 0, false, true);
		}

		return true;
	}
};

struct WklCompressedInfo
{
	unsigned int compressedInfoOffset;
	unsigned int size;
};

struct WklHeader
{
	struct Entry
	{
		unsigned int wklHeaderEntryOffset = 0;
		unsigned int size = 0;
	};

	static const int NumEntries = 98;
	Entry mEntries[NumEntries];

	void SwapByteOrder()
	{
		for(int i = 0; i < NumEntries; ++i)
		{
			mEntries[i].wklHeaderEntryOffset = ::SwapByteOrder(mEntries[i].wklHeaderEntryOffset);
			mEntries[i].size                 = ::SwapByteOrder(mEntries[i].size);
		}
	}
};

struct WklBattleMenuImageBlockInfo
{
	unsigned int battleMenuImageOffset;
	unsigned int size;

	void SwapByteOrder()
	{
		battleMenuImageOffset = ::SwapByteOrder(battleMenuImageOffset);
		size                  = ::SwapByteOrder(size);
	}
};

struct WklBattleMenuImageHeader
{
	unsigned int   offsetFromHeader = 0;
	unsigned int   offsetFromPrevImage = 0;
	unsigned short width = 0;
	unsigned short height = 0;
	unsigned int   unknown = 0;

	void SwapByteOrder()
	{
		offsetFromHeader    = ::SwapByteOrder(offsetFromHeader);
		offsetFromPrevImage = ::SwapByteOrder(offsetFromPrevImage);
		width               = ::SwapByteOrder(width);
		height              = ::SwapByteOrder(height);
		unknown             = ::SwapByteOrder(unknown);
	}

	unsigned int GetImageByteCount() const
	{
		return (width*height)/2;
	}
};

struct WklBattleMenuExtractor
{
	struct NamesAndSpecialMoves
	{
		WklCompressedInfo            mCompressedInfo[64];
		unsigned long                mImageDataAddress = 0;
		vector<WklUncompressedData*> mUncompressedData;

		~NamesAndSpecialMoves()
		{
			for(const WklUncompressedData* pUnCompressedData : mUncompressedData)
			{
				delete pUnCompressedData;
			}
			mUncompressedData.clear();
		}

		void DumpImages(const string& outDirectory, const SakuraPalette& inSakuraPalette) const
		{
			for(const WklUncompressedData* pUnCompressedData : mUncompressedData)
			{
				pUnCompressedData->DumpImages(inSakuraPalette.mpPaletteData, inSakuraPalette.mPaletteSize, pUnCompressedData->mBlockPrefix, outDirectory);
			}
		}
	};

	struct BattleMenu
	{
		struct ImageBlock
		{
			struct BattleImage
			{
				unsigned int             mAddress = 0;
				string                   mPrefix;
				FileData                 mImageFileData;
				WklBattleMenuImageHeader mImageHeader;
				
				void SetPrefix(const char* pInPrefix)
				{
					mPrefix = pInPrefix;
				}

				void SetAddress(unsigned int inAddress)
				{
					mAddress = inAddress;
				}

				void DumpImage(const string& outDirectory, const char* pInWklData, const SakuraPalette& inSakuraPalette) const
				{
					const string outFileName = outDirectory + mPrefix + BMPExtension;
					const char* pImageData   = &pInWklData[mAddress];
					ExtractImageFromData(pImageData, (mImageHeader.width*mImageHeader.height)/2, outFileName, inSakuraPalette.mpPaletteData, inSakuraPalette.mPaletteSize, mImageHeader.width, mImageHeader.height, 1, 16, 0, false, true);
				}

				bool LoadImage(const char* pInWklData, const SakuraPalette& inSakuraPalette, const string& tempDir)
				{
					const char* pImageData   = &pInWklData[mAddress];
					const string outFileName = tempDir + string("\\battleImage.bmp");
					ExtractImageFromData(pImageData, (mImageHeader.width*mImageHeader.height)/2, outFileName, inSakuraPalette.mpPaletteData, inSakuraPalette.mPaletteSize, mImageHeader.width, mImageHeader.height, 
						                 1, 16, 0, false, true);

					return mImageFileData.InitializeFileData( FileNameContainer(outFileName.c_str()) );
				}
			};

			WklBattleMenuImageHeader*  mpImagesInBlock = nullptr;
			unsigned short       mNumImages      = 0;
			vector<BattleImage*> mBattleImages;

			~ImageBlock()
			{
				delete[] mpImagesInBlock;
				mpImagesInBlock = nullptr;

				mNumImages = 0;

				for(const BattleImage* pBattleImage : mBattleImages)
				{
					delete pBattleImage;
				}
				mBattleImages.clear();
			}

			void DumpImages(const string& outDirectory, const char* pInWklData, const SakuraPalette& inSakuraPalette) const
			{
				for(const BattleImage* pBattleImage : mBattleImages)
				{
					pBattleImage->DumpImage(outDirectory, pInWklData, inSakuraPalette);
				}
			}
		};

		~BattleMenu()
		{
			delete[] mpImageBlocksInfo;
			mpImageBlocksInfo = nullptr;
			mNumBlocks = 0;

			for(const ImageBlock* pImageBlock : mImageBlocks)
			{
				delete pImageBlock;
			}
			mImageBlocks.clear();
		}

		void DumpImages(const string& outDirectory, const char* pInWklData, const SakuraPalette& inSakuraPalette) const
		{
			for(const ImageBlock* pImageBlock : mImageBlocks)
			{
				pImageBlock->DumpImages(outDirectory, pInWklData, inSakuraPalette);
			}
		}

		void ReverseByteOrder()
		{
			for(unsigned int entryIndex = 0; entryIndex < mNumBlocks; ++entryIndex)
			{	
				mpImageBlocksInfo[entryIndex].SwapByteOrder();
			}

			for(const ImageBlock* pImageBlock : mImageBlocks)
			{
				for(unsigned short imageIndex = 0; imageIndex < pImageBlock->mNumImages; ++imageIndex)
				{
					WklBattleMenuImageHeader& imageHeader = pImageBlock->mpImagesInBlock[imageIndex];
					imageHeader.SwapByteOrder();
				}
			}
		}

		unsigned long       mImageDataAddress = 0;
		WklBattleMenuImageBlockInfo*      mpImageBlocksInfo = nullptr;
		unsigned int        mNumBlocks        = 0;
		vector<ImageBlock*> mImageBlocks;
	};

	NamesAndSpecialMoves mNamesAndSpecialMoves;
	BattleMenu           mBattleMenu;
	FileData             mWklFile;
public:

	void Initialize(const FileNameContainer& inFileNameInfo)
	{
		if( !mWklFile.InitializeFileData(inFileNameInfo) )
		{
			return;
		}

		const char* pWklData = mWklFile.GetData();

		//***Character Nmes & Special Moves****
		//Read in the address at which the compressed image data lives
		unsigned long offsetToImageData = 0x100;
		memcpy_s(&mNamesAndSpecialMoves.mImageDataAddress, sizeof(mNamesAndSpecialMoves.mImageDataAddress), &pWklData[offsetToImageData], sizeof(mNamesAndSpecialMoves.mImageDataAddress));
		mNamesAndSpecialMoves.mImageDataAddress = SwapByteOrder(mNamesAndSpecialMoves.mImageDataAddress);

		//Read in offset to compressed data
		memcpy_s(&mNamesAndSpecialMoves.mCompressedInfo, sizeof(mNamesAndSpecialMoves.mCompressedInfo), &pWklData[mNamesAndSpecialMoves.mImageDataAddress], sizeof(mNamesAndSpecialMoves.mCompressedInfo));

		char tempBuffer[512];
		for(int i = 0; i < 64; ++i)
		{
			if( !mNamesAndSpecialMoves.mCompressedInfo[i].size )
			{
				continue;
			}

			const unsigned int imageOffset            = SwapByteOrder(mNamesAndSpecialMoves.mCompressedInfo[i].compressedInfoOffset);
			const unsigned int offsetToCompressedData = imageOffset + mNamesAndSpecialMoves.mImageDataAddress;

			//Uncompress image data
			PRSDecompressor uncompressedImages;
			uncompressedImages.UncompressData( (void*)(pWklData + offsetToCompressedData), mWklFile.GetDataSize() - (offsetToCompressedData));
			if( !uncompressedImages.mCompressedSize )
			{
				continue;
			}

			//Create prefix
			sprintf_s(tempBuffer, 512, "%i_%0x", i, imageOffset);

			WklUncompressedData* pUncompressedImageData = new WklUncompressedData();
			pUncompressedImageData->Initialize(tempBuffer, imageOffset);
			pUncompressedImageData->ReadInImages(uncompressedImages.mpUncompressedData);
			mNamesAndSpecialMoves.mUncompressedData.push_back(pUncompressedImageData);
		}
		//***Done with Character Names & Special Moves

		//***Battle Menu Images***
		unsigned long offsetToImageData2 = 0xE8;
		memcpy_s(&mBattleMenu.mImageDataAddress, sizeof(mBattleMenu.mImageDataAddress), &pWklData[offsetToImageData2], sizeof(mBattleMenu.mImageDataAddress));
		mBattleMenu.mImageDataAddress = SwapByteOrder(mBattleMenu.mImageDataAddress);

		//Read the first entry so we can now how big the header is
		WklBattleMenuImageBlockInfo firstEntry;
		memcpy_s(&firstEntry, sizeof(firstEntry), &pWklData[mBattleMenu.mImageDataAddress], sizeof(firstEntry));
		firstEntry.SwapByteOrder();

		mBattleMenu.mNumBlocks        = firstEntry.battleMenuImageOffset/sizeof(WklBattleMenuImageBlockInfo);
		mBattleMenu.mpImageBlocksInfo = new WklBattleMenuImageBlockInfo[mBattleMenu.mNumBlocks];
		memcpy_s(mBattleMenu.mpImageBlocksInfo, sizeof(WklBattleMenuImageBlockInfo)*mBattleMenu.mNumBlocks, &pWklData[mBattleMenu.mImageDataAddress], sizeof(WklBattleMenuImageBlockInfo)*mBattleMenu.mNumBlocks);
		for(unsigned int entryIndex = 0; entryIndex < mBattleMenu.mNumBlocks; ++entryIndex)
		{	
			mBattleMenu.mpImageBlocksInfo[entryIndex].SwapByteOrder();

			BattleMenu::ImageBlock* pNewImageBlock = new BattleMenu::ImageBlock();
			mBattleMenu.mImageBlocks.push_back(pNewImageBlock);

			memcpy_s(&pNewImageBlock->mNumImages, sizeof(pNewImageBlock->mNumImages), &pWklData[mBattleMenu.mImageDataAddress + mBattleMenu.mpImageBlocksInfo[entryIndex].battleMenuImageOffset], sizeof(pNewImageBlock->mNumImages));
			pNewImageBlock->mNumImages = SwapByteOrder(pNewImageBlock->mNumImages);

			pNewImageBlock->mpImagesInBlock       = new WklBattleMenuImageHeader[pNewImageBlock->mNumImages];
			const unsigned int imageHeaderEnd     = mBattleMenu.mImageDataAddress + mBattleMenu.mpImageBlocksInfo[entryIndex].battleMenuImageOffset + 8;
			const unsigned int miscImageHeaderEnd = imageHeaderEnd + sizeof(WklBattleMenuImageHeader)*(pNewImageBlock->mNumImages) + 8; //Last 8 bytes is some uknown thing
			memcpy_s(pNewImageBlock->mpImagesInBlock, sizeof(WklBattleMenuImageHeader)*pNewImageBlock->mNumImages, &pWklData[imageHeaderEnd], sizeof(WklBattleMenuImageHeader)*pNewImageBlock->mNumImages);
			for(unsigned short imageIndex = 0; imageIndex < pNewImageBlock->mNumImages; ++imageIndex)
			{
				WklBattleMenuImageHeader& imageHeader = pNewImageBlock->mpImagesInBlock[imageIndex];
				imageHeader.SwapByteOrder();

				//Dump image
				sprintf_s(tempBuffer, 512, "Misc_%i_%0x", entryIndex, imageIndex);

				BattleMenu::ImageBlock::BattleImage* pNewImage = new BattleMenu::ImageBlock::BattleImage();
				pNewImage->SetPrefix(tempBuffer);
				pNewImage->SetAddress(miscImageHeaderEnd + imageHeader.offsetFromHeader + imageHeader.offsetFromPrevImage);
				pNewImage->mImageHeader = imageHeader;
				pNewImageBlock->mBattleImages.push_back(pNewImage);
			}
		}
		//***Done with Battle Menu Images
	}

	void DumpImages(const string& outDirectory, const SakuraPalette& inSakuraPalette)
	{
		mNamesAndSpecialMoves.DumpImages(outDirectory, inSakuraPalette);
		mBattleMenu.DumpImages(outDirectory, mWklFile.GetData(), inSakuraPalette);
	}
};

//Also modifies SLG files
bool PatchWKLFiles(const string& sakuraDirectory, const string& inPatchedDirectory, const string& inTranslatedDirectory, const string& inTempDir, const string& extractedWklDir)
{
	const unsigned int maxCompressedSize = 0x1BA4;
	const string outDirectory              = inPatchedDirectory + Seperators + string("SAKURA2\\");
	const string translatedWKLDirectory    = inTranslatedDirectory + string("WKL\\");
	const string translatedSharedDirectory = translatedWKLDirectory + string("Shared\\");
	const string patchedStatsImagePath     = translatedWKLDirectory + string("StatsMenuPatched.bmp");

	//Create map of where the key is the crc of the original wkl image and the value is the path to the translated image
	map<unsigned long, FileNameContainer> patchedWklFileCrcMap;

	vector<string> wklSubDirs;
	FindAllDirectoriesWithinDirectory(translatedWKLDirectory, wklSubDirs);
	for(const string& wklSubDir : wklSubDirs)
	{
		//Get all images in the translated directory
		const string wklSubDirPath = translatedWKLDirectory + wklSubDir;
		vector<FileNameContainer> allPatchedWklImages;
		FindAllFilesWithinDirectory(wklSubDirPath, allPatchedWklImages);

		//For every translated image, get the crc of the original image
		for(const FileNameContainer& patchedWklImageName : allPatchedWklImages)
		{
			FileNameContainer originalWklImagePath( (extractedWklDir + string("\\") + wklSubDir + string("\\") + patchedWklImageName.mFileName).c_str() );
			FileData originalWkImage;
			if( !originalWkImage.InitializeFileData(originalWklImagePath) )
			{
				printf("PatchWKL failed.  Unable to open %s\n", originalWklImagePath.mFullPath.c_str());
				return false;
			}

			//It is assumed that the patched wkl directory has no duplicate images
			patchedWklFileCrcMap[originalWkImage.GetCRC()] = patchedWklImageName;
		}

	}
	//Done creating map

	//Load patched image data
	BmpToSaturnConverter patchedStatsMenu;
	if( !patchedStatsMenu.ConvertBmpToSakuraFormat(patchedStatsImagePath, false) )
	{
		return false;
	}

	//Find all files
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(sakuraDirectory, allFiles);
	if( !allFiles.size() )
	{
		printf("PatchWKL failed.  No files found in %s\n", sakuraDirectory.c_str());
		return false;
	}

	//Find all WKL files
	vector<FileNameContainer> wklFiles;
	GetAllFilesOfType(allFiles, "WKL", wklFiles);

	//Get palette for battle images from slg.bin
	FileData slgFileData;
	const FileNameContainer slgPath( (sakuraDirectory + "\\SAKURA2\\SLG.BIN").c_str() );
	if( !slgFileData.InitializeFileData(slgPath) )
	{
		printf("PatchWKL failed.  Unable to get slg data\n");
		return false;
	}
	SakuraPalette battleImagePalette(&slgFileData.GetData()[0x00011B78], 32);

	const int numCompressedEntries = 64;
	WklCompressedInfo compressedInfo[numCompressedEntries];
	WklCompressedInfo origCompressedInfo[numCompressedEntries];
	
	int battleMenuDelta         = 0;
	bool bCalculatedBattleDelta = false;
	for(const FileNameContainer& originalWklFileName : wklFiles)
	{
		//const string translatedDirectory = translatedWKLDirectory + originalWklFileName.mNoExtension + Seperators;
		const string extractedDirectory = extractedWklDir + originalWklFileName.mNoExtension + Seperators;
		if( !DoesDirectoryExist(extractedDirectory) )
		{
			continue;
		}

		printf("Patching %s\n", originalWklFileName.mFileName.c_str());

		FileData originalWKLFile;
		if( !originalWKLFile.InitializeFileData(originalWklFileName) )
		{
			printf("PatchWKL failed.  Unable to open %s", originalWklFileName.mFullPath.c_str());
			return false;
		}

		//Open patched file
		FileNameContainer outFileName(originalWklFileName.mFileName.c_str(), outDirectory);
		FileWriter outFile;
		if( !outFile.OpenFileForWrite(outFileName.mFullPath) )
		{
			printf("PatchWKL failed.  Unable to open %s for write\n", outFileName.mFullPath.c_str());
			return false;
		}

		//Copy over original contents
		const char* pOriginalWklData = originalWKLFile.GetData();
		
		//Read in the address at which the compressed image data lives
		unsigned long offsetToImageData     = 0x100;
		unsigned long origCompressedDataAddress = 0;
		memcpy_s(&origCompressedDataAddress, sizeof(origCompressedDataAddress), &pOriginalWklData[offsetToImageData], sizeof(origCompressedDataAddress));
		origCompressedDataAddress = SwapByteOrder(origCompressedDataAddress);

		//Read in offset to compressed data
		memset(&compressedInfo, 0, sizeof(compressedInfo));
		memcpy_s(&origCompressedInfo, sizeof(origCompressedInfo), &pOriginalWklData[origCompressedDataAddress], sizeof(origCompressedInfo));
		
		MemoryBlocks newWklCompressedData;
		MemoryBlocks newWklData;
		MemoryBlocks patchedBattleMenuImageData;
		MemoryBlocks patchedBattleMenuBlocksHeader;
		MemoryBlocks patchedBattleMenuBlocks;
		const unsigned int compressedImageHeaderSize         = 512;
		const unsigned int compressedDataLastOffset_original = SwapByteOrder(origCompressedInfo[numCompressedEntries - 1].compressedInfoOffset);
		
		//Gather data from original wkl file
		WklBattleMenuExtractor wklExtractor;
		wklExtractor.Initialize(originalWklFileName);

		const unsigned int startOfBattleImages = wklExtractor.mBattleMenu.mImageDataAddress;
		const unsigned int endOfBattleImages   = wklExtractor.mBattleMenu.mImageBlocks.back()->mBattleImages.back()->mAddress + wklExtractor.mBattleMenu.mImageBlocks.back()->mBattleImages.back()->mImageHeader.GetImageByteCount();

		if( origCompressedDataAddress < endOfBattleImages )
		{
			printf("PatchWKL failed.  In %s, compressedData is coming before menu images.\n", outFileName.mFullPath.c_str());
			return false;
		}

		//create data blocks
		//newWklData.AddBlock(pOriginalWklData, 0, compressedDataAddress);
		const int memoryBlock_HeaderIndex           = 0;
		const int memoryBlock_PostBattleImagesIndex = 1;
		const int memoryBlock_FooterIndex           = 2;
		newWklData.AddBlock(pOriginalWklData, 0, startOfBattleImages);
		newWklData.AddBlock(pOriginalWklData, endOfBattleImages, origCompressedDataAddress - endOfBattleImages);
		newWklData.AddBlock(pOriginalWklData, origCompressedDataAddress + compressedDataLastOffset_original, originalWKLFile.GetDataSize() - (origCompressedDataAddress + compressedDataLastOffset_original));

		//**Copy over battle menu**
		//Block header
		wklExtractor.mBattleMenu.ReverseByteOrder();
		//patchedBattleMenuImageData.AddBlock((const char*)wklExtractor.mBattleMenu.mpImageBlocksInfo, 0, sizeof(WklBattleMenuImageBlockInfo)*wklExtractor.mBattleMenu.mNumBlocks);

		unsigned int offsetFromBlockHeader = wklExtractor.mBattleMenu.mNumBlocks*sizeof(WklBattleMenuImageBlockInfo);
		unsigned int imageBlockSize        = 0;
		for(const WklBattleMenuExtractor::BattleMenu::ImageBlock* pImageBlock : wklExtractor.mBattleMenu.mImageBlocks)
		{	
			imageBlockSize = 0;

			char blockData[8];
			memset(blockData, 0, sizeof(blockData));
			memcpy_s(blockData, 8, &pImageBlock->mNumImages, sizeof(pImageBlock->mNumImages));
			SwapByteOrderInPlace(blockData, 2);
			patchedBattleMenuImageData.AddBlock(blockData, 0, sizeof(blockData));
			
			//Create image block header
			int currImage                    = 0;
			unsigned int offsetFromHeader    = 0;
			unsigned int offsetFromPrevImage = 0;
			for(WklBattleMenuExtractor::BattleMenu::ImageBlock::BattleImage* pBattleImage : pImageBlock->mBattleImages)
			{
				//Load original battle image
				if( !pBattleImage->LoadImage(originalWKLFile.GetData(), battleImagePalette, inTempDir) )
				{
					return false;
				}
				//Find the translated image based on the crc of this battle image
				map<unsigned long, FileNameContainer>::iterator pathToTranslatedImage = patchedWklFileCrcMap.find(pBattleImage->mImageFileData.GetCRC());
				if( pathToTranslatedImage == patchedWklFileCrcMap.end() )
				{
					printf("PatchWKL failed.  Could not find a translated image for battle image %s in %s\n", pBattleImage->mPrefix.c_str(), originalWklFileName.mFileName.c_str());
					return false;
				}

				const string patchedBattleImagePath = pathToTranslatedImage->second.mFullPath;
				BitmapReader bmpReader;
				if( !bmpReader.ReadBitmap(patchedBattleImagePath) )
				{
					return false;
				}

				WklBattleMenuImageHeader translatedImageInfo;
				translatedImageInfo.offsetFromHeader    = SwapByteOrder(offsetFromHeader);
				translatedImageInfo.offsetFromPrevImage = SwapByteOrder(offsetFromPrevImage);
				translatedImageInfo.width               = SwapByteOrder(static_cast<unsigned short>(bmpReader.mBitmapData.mInfoHeader.mImageWidth));
				translatedImageInfo.height              = SwapByteOrder(static_cast<unsigned short>(abs(bmpReader.mBitmapData.mInfoHeader.mImageHeight)));
				translatedImageInfo.unknown             = pImageBlock->mpImagesInBlock[currImage].unknown;

				/*
				if( bmpReader.mBitmapData.mInfoHeader.mImageWidth != pBattleImage->mImageHeader.width || abs(bmpReader.mBitmapData.mInfoHeader.mImageHeight) != pBattleImage->mImageHeader.height)
				{
					printf("WKL Stat: Image dims don't match. Width(%i -> %i), Height(%i -> %i) %s\n", pBattleImage->mImageHeader.width, bmpReader.mBitmapData.mInfoHeader.mImageWidth,
						                                                                                  pBattleImage->mImageHeader.height, bmpReader.mBitmapData.mInfoHeader.mImageHeight,
						                                                                                  pathToTranslatedImage->second.mFullPath.c_str());
				}*/

				offsetFromHeader   += offsetFromPrevImage;
				offsetFromPrevImage = bmpReader.mBitmapData.mColorData.mSizeInBytes;
				imageBlockSize     += bmpReader.mBitmapData.mColorData.mSizeInBytes + sizeof(translatedImageInfo);
				++currImage;

				patchedBattleMenuImageData.AddBlock((char*)&translatedImageInfo, 0, sizeof(translatedImageInfo));
			}

			//Last 8 bytes of block header
			patchedBattleMenuImageData.AddBlock((char*)&offsetFromHeader, 0, sizeof(offsetFromHeader), true);
			patchedBattleMenuImageData.AddBlock((char*)&offsetFromPrevImage, 0, sizeof(offsetFromPrevImage), true);
			imageBlockSize += 16;

			//Copy image data
			for(WklBattleMenuExtractor::BattleMenu::ImageBlock::BattleImage* pBattleImage : pImageBlock->mBattleImages)
			{
				//Find the translated image based on the crc of this battle image
				map<unsigned long, FileNameContainer>::iterator pathToTranslatedImage = patchedWklFileCrcMap.find(pBattleImage->mImageFileData.GetCRC());
				if( pathToTranslatedImage == patchedWklFileCrcMap.end() )
				{
					printf("PatchWKL failed.  Could not find a translated image for battle image %s in %s\n", pBattleImage->mPrefix.c_str(), originalWklFileName.mFileName.c_str());
					return false;
				}

				const string patchedBattleImagePath = pathToTranslatedImage->second.mFullPath;
				//const string patchedBattleImagePath = translatedSharedDirectory + pBattleImage->mPrefix + BMPExtension;
				
				//Load patched image data
				BmpToSaturnConverter patchedBattleImage;
				if( !patchedBattleImage.ConvertBmpToSakuraFormat(patchedBattleImagePath, false) )
				{
					return false;
				}

				patchedBattleMenuImageData.AddBlock(patchedBattleImage.GetImageData(), 0, patchedBattleImage.GetImageDataSize());
			}

			//Main block header
			patchedBattleMenuBlocksHeader.AddBlock((char*)&offsetFromBlockHeader, 0, sizeof(offsetFromBlockHeader), true);
			patchedBattleMenuBlocksHeader.AddBlock((char*)&imageBlockSize, 0, sizeof(imageBlockSize), true);
			offsetFromBlockHeader += imageBlockSize;
		}

		patchedBattleMenuBlocks.AddBlock(patchedBattleMenuBlocksHeader);
		patchedBattleMenuBlocks.AddBlock(patchedBattleMenuImageData);
		//***Done with battle menu***

		//**Compressed images**
		char tempBuffer[512];
		unsigned int offsetToImageBlock = sizeof(compressedInfo);
		for(int i = 0; i < 64; ++i)
		{	
			compressedInfo[i].compressedInfoOffset = offsetToImageBlock;

			if( !origCompressedInfo[i].size )
			{
				compressedInfo[i].size   = 0;
				compressedInfo[i].compressedInfoOffset = SwapByteOrder(offsetToImageBlock);
				continue;
			}

			const unsigned int origImageOffset        = SwapByteOrder(origCompressedInfo[i].compressedInfoOffset);
			const unsigned int offsetToCompressedData = origImageOffset + origCompressedDataAddress;
			
			//Uncompress image data
			PRSDecompressor uncompressedImages;
			uncompressedImages.UncompressData( (void*)(pOriginalWklData + offsetToCompressedData), originalWKLFile.GetDataSize() - (offsetToCompressedData));
			if( !uncompressedImages.mCompressedSize )
			{
				continue;
			}

			//Create prefix
			sprintf_s(tempBuffer, 512, "%i_%0x", i, origImageOffset);
			const string prefix = string(tempBuffer);

			//Read in original data
			WklUncompressedData uncompressedImageData;
			uncompressedImageData.ReadInImages(uncompressedImages.mpUncompressedData);

			WklUncompressedData translatedWklData;
			translatedWklData.mNumImages   = uncompressedImageData.mNumImages;
			translatedWklData.mpImageInfos = new WklUncompressedData::ImageInfo[uncompressedImageData.mNumImages];

			//Find translated files
			unsigned short offsetFromStart    = 0;
			unsigned int sizeOfDataToCompress = 0;
			for(unsigned short imageIndex = 0; imageIndex < uncompressedImageData.mNumImages; ++imageIndex)
			{
				const string originalFileName   = prefix + string("_") + std::to_string(imageIndex);
				//const string translatedFileName = translatedDirectory + originalFileName + BMPExtension;
				const FileNameContainer originalFilePath( (extractedDirectory + originalFileName + BMPExtension).c_str() );
				FileData originalWklImageData;
				if( !originalWklImageData.InitializeFileData(originalFilePath) )
				{
					printf("PatchWKL failed.  Unable to open %s", originalFilePath.mFullPath.c_str());
					return false;
				}

				//Find the translated image based on the crc of this battle image
				map<unsigned long, FileNameContainer>::iterator pathToTranslatedImage = patchedWklFileCrcMap.find(originalWklImageData.GetCRC());
				if( pathToTranslatedImage == patchedWklFileCrcMap.end() )
				{
					printf("PatchWKL failed.  Could not find translated image %s", originalFilePath.mFullPath.c_str());
					return false;
				}

				const string patchedBattleImagePath = pathToTranslatedImage->second.mFullPath;

				//Read in original data
				BitmapReader originalImage;
				if( !originalImage.ReadBitmap(originalFilePath.mFullPath) )
				{
					printf("PatchWKL failed.  Unable to open %s\n", originalFilePath.mFullPath.c_str());
					return false;
				}

				//Read in translated data
				BitmapReader translatedImage;
				if( !translatedImage.ReadBitmap(patchedBattleImagePath.c_str()) )
				{
					printf("PatchWKL failed.  Unable to open %s\n", patchedBattleImagePath.c_str());
					return false;
				}

				const int imageWidth  = abs(translatedImage.mBitmapData.mInfoHeader.mImageWidth);
				const int imageHeight = abs(translatedImage.mBitmapData.mInfoHeader.mImageHeight);

				if( imageWidth != originalImage.mBitmapData.mInfoHeader.mImageWidth || imageHeight != abs(originalImage.mBitmapData.mInfoHeader.mImageHeight) )
				{
					printf("WKL Warning: Image dims don't match. Width(%i -> %i), Height(%i -> %i) %s\n", originalImage.mBitmapData.mInfoHeader.mImageWidth, imageWidth, 
						                                                                                  abs(originalImage.mBitmapData.mInfoHeader.mImageHeight), imageHeight,
						                                                                                  pathToTranslatedImage->second.mFullPath.c_str());
				}

				if( imageWidth%8 != 0 )
				{
					printf("PatchWKL failed.  %s has a width of %i which is not divisible by 8\n", patchedBattleImagePath.c_str(), imageWidth);
					return false;
				}

				TileExtractor tileExtractor;
				if( !tileExtractor.ExtractTiles(imageWidth, imageHeight, imageWidth, imageHeight, translatedImage) )
				{
					return false;
				}

				if( tileExtractor.mTiles.size() != 1 || 
					tileExtractor.mTiles[0].mTileSize != static_cast<unsigned int>(imageWidth*imageHeight/2) )
				{
					printf("PatchWkl: Invalid patched image: %s\n", patchedBattleImagePath.c_str());
					return false;
				}

				translatedImage.mBitmapData.mInfoHeader.mImageWidth  = imageWidth;// abs(translatedImage.mBitmapData.mInfoHeader.mImageWidth);
				translatedImage.mBitmapData.mInfoHeader.mImageHeight = imageHeight;//abs(translatedImage.mBitmapData.mInfoHeader.mImageHeight);

				//Uncompressed image header for translated data
				translatedWklData.mpImageInfos[imageIndex]                      = uncompressedImageData.mpImageInfos[imageIndex];
				translatedWklData.mpImageInfos[imageIndex].width                = ((unsigned short)translatedImage.mBitmapData.mInfoHeader.mImageWidth);
				translatedWklData.mpImageInfos[imageIndex].height               = ((unsigned short)translatedImage.mBitmapData.mInfoHeader.mImageHeight);
				translatedWklData.mpImageInfos[imageIndex].offsetBytesFromStart = ((unsigned short)offsetFromStart);
				translatedWklData.mpImageInfos[imageIndex].numBytes             = ((unsigned short)translatedImage.mBitmapData.mColorData.mSizeInBytes);
				translatedWklData.mpImageInfos[imageIndex].SwapEndianness();

				assert(tileExtractor.mTiles[0].mTileSize <= 0xffff);
				const unsigned short colorBytes = static_cast<unsigned short>(tileExtractor.mTiles[0].mTileSize);//static_cast<unsigned short>(translatedImage.mBitmapData.mColorData.mSizeInBytes);
				offsetFromStart      += colorBytes;
				sizeOfDataToCompress += colorBytes;

				char* pImageData = new char[colorBytes];
				memcpy_s(pImageData, colorBytes, tileExtractor.mTiles[0].mpTile, colorBytes);
				translatedWklData.mImageData.push_back( WklUncompressedData::TranslatedImageData(pImageData, colorBytes) );
			}
			
			//Put all of the data that needs to be compressed into a single contiguous block of memory
			const unsigned short numImagesBigEndian = SwapByteOrder(translatedWklData.mNumImages);
			sizeOfDataToCompress += sizeof(WklUncompressedData::ImageInfo)*(uncompressedImageData.mNumImages + 1); //+1 for the initial 16 bytes which just contain the number of images
			char* pDataToCompress = new char[sizeOfDataToCompress];
			memset(pDataToCompress, 0, sizeOfDataToCompress);
			memcpy_s(pDataToCompress, sizeOfDataToCompress, &numImagesBigEndian, sizeof(numImagesBigEndian));
			memcpy_s(pDataToCompress + 0x10, sizeOfDataToCompress - 0x10, translatedWklData.mpImageInfos, sizeof(WklUncompressedData::ImageInfo)*uncompressedImageData.mNumImages);

			//Copy over image data
			unsigned short baseOffset = sizeof(WklUncompressedData::ImageInfo)*uncompressedImageData.mNumImages + 0x10;
			for(unsigned short imageIndex = 0; imageIndex < (unsigned short)translatedWklData.mImageData.size(); ++imageIndex)
			{
				memcpy_s(pDataToCompress + baseOffset, sizeOfDataToCompress - baseOffset, translatedWklData.mImageData[imageIndex].pColorData, translatedWklData.mImageData[imageIndex].dataSize);

				baseOffset += translatedWklData.mImageData[imageIndex].dataSize;
			}

			//Compress the data
			PRSCompressor compressedTranslatedData;
			compressedTranslatedData.CompressData(pDataToCompress, sizeOfDataToCompress, PRSCompressor::kCompressOption_FourByteAlign);
			
			//Fixup header info
			compressedInfo[i].size = SwapByteOrder(compressedTranslatedData.mCompressedSize);
			offsetToImageBlock    += compressedTranslatedData.mCompressedSize;

			//Write out the compressed data
			newWklCompressedData.AddBlock(compressedTranslatedData.mpCompressedData, 0, compressedTranslatedData.mCompressedSize);

			//Now swap the byte order
			compressedInfo[i].compressedInfoOffset = SwapByteOrder(compressedInfo[i].compressedInfoOffset);

			//Cleanup
			delete[] pDataToCompress;
		}

		//***Write out the new file***
		const MemoryBlocks::Block& newWklHeader           = newWklData.GetBlock(memoryBlock_HeaderIndex);
		const MemoryBlocks::Block& newWklPostBattleImages = newWklData.GetBlock(memoryBlock_PostBattleImagesIndex);
		const MemoryBlocks::Block& newWklFooter           = newWklData.GetBlock(memoryBlock_FooterIndex);

		//fixup header pointers
		WklHeader wklHeader_patched;
		memcpy_s(&wklHeader_patched, sizeof(wklHeader_patched), pOriginalWklData, sizeof(wklHeader_patched));
		wklHeader_patched.SwapByteOrder();

		const unsigned int compressedDataLastOffset_patched = SwapByteOrder(compressedInfo[numCompressedEntries - 1].compressedInfoOffset);
		const int newDataSizeDelta                          = compressedDataLastOffset_patched - compressedDataLastOffset_original;
		for(int headerEntry = 0; headerEntry < WklHeader::NumEntries; ++headerEntry)
		{
			if( wklHeader_patched.mEntries[headerEntry].wklHeaderEntryOffset > origCompressedDataAddress )
			{
				wklHeader_patched.mEntries[headerEntry].wklHeaderEntryOffset += newDataSizeDelta;
			}
			else if( wklHeader_patched.mEntries[headerEntry].wklHeaderEntryOffset == origCompressedDataAddress )
			{
				if( wklHeader_patched.mEntries[headerEntry].size > 0 )
				{
					wklHeader_patched.mEntries[headerEntry].size = compressedDataLastOffset_patched;
				}
			}
		}

		//Get size of the patched battle menu
		const unsigned int newBattleMenuSize   = offsetFromBlockHeader;
		const unsigned int origBattleMenuSize  = SwapByteOrder(*(unsigned int*)(wklExtractor.mWklFile.GetData() + 0xEC));
		const int newBattleMenuDelta           = newBattleMenuSize - origBattleMenuSize;
		if( newBattleMenuDelta%4 != 0 )
		{
			printf("PatchWKL: Battle menu data is not at a four byte boundary for the file %s\n", originalWklFileName.mFileName.c_str());
		}

		if( bCalculatedBattleDelta && USE_LARGE_BATTLE_MENU )
		{
			assert(newBattleMenuDelta == battleMenuDelta);
		}
		battleMenuDelta        = newBattleMenuDelta;
		bCalculatedBattleDelta = true;
		for(int headerEntry = 0; headerEntry < WklHeader::NumEntries; ++headerEntry)
		{
			if( wklHeader_patched.mEntries[headerEntry].wklHeaderEntryOffset > wklExtractor.mBattleMenu.mImageDataAddress )
			{
				wklHeader_patched.mEntries[headerEntry].wklHeaderEntryOffset += battleMenuDelta;
			}
			else if( wklHeader_patched.mEntries[headerEntry].wklHeaderEntryOffset == wklExtractor.mBattleMenu.mImageDataAddress )
			{
				if( wklHeader_patched.mEntries[headerEntry].size > 0 )
				{
					wklHeader_patched.mEntries[headerEntry].size = newBattleMenuSize;
				}
			}
		}

		wklHeader_patched.SwapByteOrder();
		memcpy_s(newWklHeader.pData, sizeof(wklHeader_patched.mEntries), wklHeader_patched.mEntries, sizeof(wklHeader_patched.mEntries));
		//done fixing header pointers
		
		//write header
		outFile.WriteData(newWklHeader.pData, newWklHeader.blockSize);
		
		//Write out patched battle images
		unsigned int totalBattleMenuSize = 0;
		for(unsigned int b = 0; b < patchedBattleMenuBlocks.GetNumberOfBlocks(); ++b)
		{
			outFile.WriteData(patchedBattleMenuBlocks.GetBlock(b).pData, patchedBattleMenuBlocks.GetBlock(b).blockSize);

			totalBattleMenuSize += patchedBattleMenuBlocks.GetBlock(b).blockSize;
		}

		outFile.WriteData(newWklPostBattleImages.pData, newWklPostBattleImages.blockSize);

		//**write compressed image data**
		outFile.WriteData(compressedInfo, sizeof(compressedInfo));

		unsigned int sizeWritten = 0;
		for(unsigned int blockIndex = 0; blockIndex < newWklCompressedData.GetNumberOfBlocks(); ++blockIndex)
		{
			outFile.WriteData(newWklCompressedData.GetBlock(blockIndex).pData, newWklCompressedData.GetBlock(blockIndex).blockSize);

			sizeWritten += newWklCompressedData.GetBlock(blockIndex).blockSize;
		}
		//**Done writing compressed image data**
		
		//write footer
		outFile.WriteData(newWklFooter.pData, newWklFooter.blockSize);
		//***Done writing new file***
	}

	//***Fix pointers in SLG files***
	vector<string> slgFiles;
	slgFiles.push_back("0SLG.BIN");
	slgFiles.push_back("SLG.BIN");

	FileData battleMenuFormattingData;
	FileNameContainer battleMenuFormattingDataPath("BattleMenuFormatting.bin", inTranslatedDirectory);
	if( !battleMenuFormattingData.InitializeFileData(battleMenuFormattingDataPath) )
	{
		printf("PatchWKL failed. Unable to open %s\\BattleMenuFormatting.bin\n", inTranslatedDirectory.c_str());
		return false;
	}
	
	//Calculate text offset
	const int maxMultiplier        = (240/(OutTileSpacingX));
	const int maxCharactersPerLine = maxMultiplier;
	const int textBytes            = maxCharactersPerLine*MaxLines*((16*16)/2);
	const int origTextBytes        = ((240/16))*3*((16*16)/2);
	const int textDelta            = FIX_SLG_FONT_DRAWING_SIZE ? 0 : (textBytes - origTextBytes);
	
	//Clipping for the battle menu, change 0x0030 to 0xffa0 (48 to -96)
	unsigned short newBattleMenuClippingValue = 0xA0FF;//Big Endian: 0xFFA0;
	
	std::unordered_map<unsigned long, unsigned long> addressesFixedInSlg;        //originalValue, newValue
	std::unordered_map<unsigned short, unsigned short> packedVDP1AddressesToFix; //originalValue, newValue
	bool bEvtFilesFixed = false;

	for(const string& slgFileName : slgFiles)
	{
		//Open the original file
		const string filePath = outDirectory + slgFileName;

		printf("File: %s\n", slgFileName.c_str());

		//DEBUG 
		FileData slgData;
		slgData.InitializeFileData("SLG.BIN", filePath.c_str());
		//debug
		if (!bEvtFilesFixed && 0)
		{
			FileData slgData;
			slgData.InitializeFileData("SLG.BIN", filePath.c_str());

			std::map<unsigned long, unsigned long> slgVdp1Addresses;
			std::map<unsigned long, unsigned long> slgVdp1AddressesUnique;
			for (unsigned long s = 0; s < slgData.GetDataSize(); s += 4)
			{
				unsigned long sData;
				slgData.ReadData(s, (char*)&sData, 4, true);
				if ((sData & 0xfff00000) == 0x25c00000 && sData >= 0x25C15100 )//0x25c17b00)//0x25c2b600)
				{
					slgVdp1Addresses[s] = sData;
					slgVdp1AddressesUnique[sData] = sData;
				}
			}

			for (unsigned long s = 0; s < slgData.GetDataSize(); s += 4)
			{
				unsigned long sData;
				slgData.ReadData(s, (char*)&sData, 4, true);
				
				for(std::map<unsigned long, unsigned long>::iterator iter = slgVdp1AddressesUnique.begin(); iter != slgVdp1AddressesUnique.end(); ++iter)
				{
					if( (iter->second & 0x000fffff) == sData )
					{
						printf("   SLG Packed VDP1: 0x%x at 0x%x\n", iter->second, s);
					}
				}
			}

			for (std::map<unsigned long, unsigned long>::iterator sIter = slgVdp1Addresses.begin(); sIter != slgVdp1Addresses.end(); ++sIter)
			{
				printf("   SLG VDP1: 0x%x at 0x%x\n", sIter->second, sIter->first);
			}
		}
		//end debug

		FileReadWriter slgFile;
		if( !slgFile.OpenFile(filePath.c_str()) )
		{
			printf("PatchWKL: Unable to open %s file.\n", filePath.c_str());
			return false;
		}

		addressesFixedInSlg.clear();
		packedVDP1AddressesToFix.clear();

		//Fixup 4 byte offsets
		unsigned long origVDP1Value = 0;
		unsigned long newVPD1Value  = 0;

		//IF THE DISC2 FIXES BELOW ARE ENABLED, THEN THIS BLOCK NEEDS TO BE DISABLED
#if 1
		//Cursor image VDP1 offset
		slgFile.ReadData(0x14058, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta;
		slgFile.WriteData(0x14058, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

		//Cursor image VDP1 offset
		slgFile.ReadData(0x140f0, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta;
		slgFile.WriteData(0x140f0, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

		//Cursor image VDP1 offset
		slgFile.ReadData(0x14528, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta;
		slgFile.WriteData(0x14528, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

		//Dialog text VDP1 address [05C16480]
		slgFile.ReadData(0x21590, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta;
		slgFile.WriteData(0x21590, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

		//Dialog text VDP1 address [05C16480]
		slgFile.ReadData(0x219C4, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta;
		slgFile.WriteData(0x219C4, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

		//Battle Sprites VDP1 address[05C17B00]
		slgFile.ReadData(0x10f8c, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta + textDelta;
		slgFile.WriteData(0x10f8c, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

		//Battle sprites VDP1 read address[05C17B00]
		slgFile.ReadData(0xFF68, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta + textDelta;
		slgFile.WriteData(0xFF68, (char*)&newVPD1Value, sizeof(newVPD1Value), true);
#endif

		//Battle reaction sprites
		if( 0 && GIsDisc2 )
		{
			auto FixSLGAddress = [&slgFile, &newVPD1Value, &origVDP1Value, &battleMenuDelta, &addressesFixedInSlg, &packedVDP1AddressesToFix, &textDelta](unsigned long InAddress)
			{
				slgFile.ReadData(InAddress, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
				
				/*
				if( origVDP1Value == 0x25C49100 ||
					((origVDP1Value & 0xfff00000) == 0 && origVDP1Value == 0x49100)
					)
				{
					return;
				}*/
				
				newVPD1Value = origVDP1Value >= 0x25C17B00 ? (origVDP1Value + battleMenuDelta + textDelta) : (origVDP1Value + battleMenuDelta);
				slgFile.WriteData(InAddress, (char*)&newVPD1Value, sizeof(newVPD1Value), true);
				addressesFixedInSlg[origVDP1Value] = newVPD1Value;
				if( (origVDP1Value & 0xfff00000) == 0x25c00000 )
				{
					packedVDP1AddressesToFix[ (unsigned short)((origVDP1Value & 0x000fffff) / 8)] = (unsigned short)((newVPD1Value & 0x000fffff) / 8);
				}
				printf("FixSLGAddress Original Address: 0x%x to 0x%x at 0x%x\n", origVDP1Value, newVPD1Value, InAddress);
			};

			auto SetSLGAddress = [&slgFile, &origVDP1Value, &addressesFixedInSlg, &packedVDP1AddressesToFix, &textDelta](unsigned long InAddress, unsigned int InNewValue)
			{
				slgFile.ReadData(InAddress, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
				slgFile.WriteData(InAddress, (char*)&InNewValue, sizeof(InNewValue), true);
				addressesFixedInSlg[origVDP1Value] = InNewValue;
				if ((origVDP1Value & 0xfff00000) == 0x25c00000)
				{
					packedVDP1AddressesToFix[(unsigned short)((origVDP1Value & 0x000fffff) / 8)] = (unsigned short)((InNewValue & 0x000fffff) / 8);
				}
				printf("SetSLGAddress Original Address: %x\n", origVDP1Value);
			};

#define SetValueAtSLGAddress(InAddress, InNewValue, InType) {const InType newValue = (InType)InNewValue; \
                                                               slgFile.WriteData(InAddress, (char*)&newValue, sizeof(InType), true);}
		
			FixSLGAddress(0x1044);
			FixSLGAddress(0x1070);
			FixSLGAddress(0x43E8);
			FixSLGAddress(0x88AC);
			FixSLGAddress(0x8C84);
			FixSLGAddress(0x8F5C);
			FixSLGAddress(0x9360);
			FixSLGAddress(0xCCCC);
			FixSLGAddress(0xCF7C);
			FixSLGAddress(0xE390);
			FixSLGAddress(0xF720);
			FixSLGAddress(0xF724);
			FixSLGAddress(0xFF48);
			FixSLGAddress(0x10F38);
			FixSLGAddress(0x10F8C);
			FixSLGAddress(0x10FE4);
			FixSLGAddress(0x11094);
			FixSLGAddress(0x1109C);
			FixSLGAddress(0x127F0);
			FixSLGAddress(0x127FC);
			FixSLGAddress(0x12804);
			FixSLGAddress(0x1280C);
			FixSLGAddress(0x12814);
			FixSLGAddress(0x13C64);
			FixSLGAddress(0x13C7C);
			FixSLGAddress(0x13D04);
			FixSLGAddress(0x14058);
			FixSLGAddress(0x14528);
		//	FixSLGAddress(0x15924);
		//	FixSLGAddress(0x15994);
		//	FixSLGAddress(0x159A0);
			FixSLGAddress(0x15F8C);
			FixSLGAddress(0x169B0);
			FixSLGAddress(0x1ED10);
			FixSLGAddress(0x1EDD8);
			FixSLGAddress(0x21590);
		//	FixSLGAddress(0x216A8);
		//	FixSLGAddress(0x217E0);
		//	FixSLGAddress(0x2183C);
		//	FixSLGAddress(0x21928);
			FixSLGAddress(0x219C4);
			FixSLGAddress(0x33438);
			FixSLGAddress(0x34CD0);
			
			//Packed values
			FixSLGAddress(0x10A4);
			FixSLGAddress(0x889C);
			FixSLGAddress(0x8C74);
			FixSLGAddress(0x8F4C);
			FixSLGAddress(0x9350);
			FixSLGAddress(0xFF68);
			FixSLGAddress(0xFF70);
			FixSLGAddress(0xFF78);
			FixSLGAddress(0xFF80);
			FixSLGAddress(0x140F0);
			FixSLGAddress(0x16920);
		//	FixSLGAddress(0x43D70);
		//	FixSLGAddress(0x43D78);
		//	FixSLGAddress(0x43D88);
		//	FixSLGAddress(0x43D90);
		//	FixSLGAddress(0x43DA0);
		//	FixSLGAddress(0x43DA8);
		//	FixSLGAddress(0x43DB8);
		//	FixSLGAddress(0x43DC8);
		//	FixSLGAddress(0x43DD0);
		//	FixSLGAddress(0x49114);

			/*
			FixSLGAddress(0x1044);
			FixSLGAddress(0x1070);
			FixSLGAddress(0x43E8); //*
			FixSLGAddress(0x88AC);
			FixSLGAddress(0x8C84);
			FixSLGAddress(0x8F5C);
			FixSLGAddress(0x9350); //*
			FixSLGAddress(0x9360); //*
			FixSLGAddress(0xCCCC);
			FixSLGAddress(0xE390); //*
			FixSLGAddress(0xF720); //
			FixSLGAddress(0xF724); //
			FixSLGAddress(0xFF48); //
			FixSLGAddress(0xFF68);
			FixSLGAddress(0x10F38); //
			FixSLGAddress(0x10F8C);
			FixSLGAddress(0x10FE4);
			FixSLGAddress(0x11094);
			FixSLGAddress(0x1109C);
			FixSLGAddress(0x169B0);
			FixSLGAddress(0x1ED10); //*
			FixSLGAddress(0x1EDD8); //*
			FixSLGAddress(0x33438);
		//	FixSLGAddress(0x34CD0); //

			//Packed addresses (fullAddress/8) with format 0x00XXXXXXXX
			FixSLGAddress(0x10A4);
			FixSLGAddress(0x889C);
			FixSLGAddress(0x8C74);
			FixSLGAddress(0x8F4C);
			FixSLGAddress(0xFF70);
			FixSLGAddress(0xFF78);
			FixSLGAddress(0xFF80);
			FixSLGAddress(0x16920);*/

			//Fixes battle map
			SetValueAtSLGAddress(0xC7AC, 0x91229142, unsigned long);
			SetValueAtSLGAddress(0xC84A, 0x87F2, unsigned short);
			SetValueAtSLGAddress(0xCF24, 0x8822, unsigned short);

			/*
			//Hard coded value here.  We just need to shift this pointer[0x25C49100] to a free block of memory.
			const unsigned int freeVDP1RamOffset = 0x25C4A500;
			const unsigned int freeRamOffset = 0x0004A500;
			SetSLGAddress(0x43E8,  freeVDP1RamOffset);
			SetSLGAddress(0x9350,  freeRamOffset);
			SetSLGAddress(0x9360,  freeVDP1RamOffset);
			SetSLGAddress(0xE390,  freeVDP1RamOffset);
			SetSLGAddress(0x1ED10, freeVDP1RamOffset);
			SetSLGAddress(0x1EDD8, freeVDP1RamOffset);*/

			//debug
			if (!bEvtFilesFixed && 0)
			{
				std::map<unsigned long, unsigned long> slgVdp1Addresses;
				for (unsigned long s = 0; s < slgData.GetDataSize(); s += 4)
				{
					unsigned long sData;
					slgData.ReadData(s, (char*)&sData, 4, true);
					if ((sData & 0xfff00000) == 0 )
					{
						const unsigned long searchVal = 0x25c00000 + sData;
						if(addressesFixedInSlg.find(searchVal) != addressesFixedInSlg.end())
						{
							slgVdp1Addresses[s] = sData;
						}
					}
				}

				for (std::map<unsigned long, unsigned long>::iterator sIter = slgVdp1Addresses.begin(); sIter != slgVdp1Addresses.end(); ++sIter)
				{
					printf("   SLG VDP1 PACKED: %x at %x\n", sIter->second, sIter->first);
				}
			}
			//end debug
		}
		else if( !GIsDisc2 )
		{
			//All of these are for the pointer 0002B600
			slgFile.ReadData(0x0000FF78, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
			newVPD1Value = origVDP1Value + battleMenuDelta + textDelta;
			slgFile.WriteData(0x0000FF78, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

			slgFile.ReadData(0x00001070, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
			newVPD1Value = origVDP1Value + battleMenuDelta + textDelta;
			slgFile.WriteData(0x00001070, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

			slgFile.ReadData(0x00010FE4, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
			newVPD1Value = origVDP1Value + battleMenuDelta + textDelta;
			slgFile.WriteData(0x00010FE4, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

			slgFile.ReadData(0x000010A4, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
			newVPD1Value = origVDP1Value + battleMenuDelta + textDelta;
			slgFile.WriteData(0x000010A4, (char*)&newVPD1Value, sizeof(newVPD1Value), true);
		}

		//Fixup 2 byte offsets
		unsigned short origVDP1Offset = 0;
		unsigned short newVDP1Offset  = 0;

		//Face images
		//Blink image written to 05c15B00 [2B60 when divided by 8] and 05c15880 [2B10 when divided by 8]
		{
			slgFile.ReadData(0x000142A6, (char*)&origVDP1Offset, sizeof(origVDP1Offset), true);
			newVDP1Offset = ((origVDP1Offset<<3) + (unsigned short)battleMenuDelta) >> 3;
			slgFile.WriteData(0x000142A6, (char*)&newVDP1Offset, sizeof(newVDP1Offset), true);

			slgFile.ReadData(0x00014434, (char*)&origVDP1Offset, sizeof(origVDP1Offset), true);
			newVDP1Offset = ((origVDP1Offset<<3) + (unsigned short)battleMenuDelta) >> 3;
			slgFile.WriteData(0x00014434, (char*)&newVDP1Offset, sizeof(newVDP1Offset), true);

			slgFile.ReadData(0x0001444C, (char*)&origVDP1Offset, sizeof(origVDP1Offset), true);
			newVDP1Offset = ((origVDP1Offset<<3) + (unsigned short)battleMenuDelta) >> 3;
			slgFile.WriteData(0x0001444C, (char*)&newVDP1Offset, sizeof(newVDP1Offset), true);
		}

		if( USE_LARGE_BATTLE_MENU )
		{
			//Clipping for the battle menu, change 0x0030 to 0xffb8 (48 to -72)
			slgFile.WriteData(0x000253DC, (char*)&newBattleMenuClippingValue, sizeof(newBattleMenuClippingValue), false);

			//Battle menu formatting
			slgFile.WriteData(0x00046c60, battleMenuFormattingData.GetData(), battleMenuFormattingData.GetDataSize());
		}
	}
	//***Done with SLG files***

	//***Fixup addresses in EVT files***
	if( !bEvtFilesFixed && FIX_EVT_FILES )
	{
		struct EVTAddressesToFix
		{
			union EVTAddressValue
			{
				EVTAddressValue(unsigned long f) : full(f){}
				EVTAddressValue(unsigned short p) : packed(p) {}

				unsigned long  full;
				unsigned short packed;
			};

			EVTAddressesToFix(unsigned long inOriginal, unsigned long inNew, unsigned long inOffset) : 
			originalValue(inOriginal), newValue(inNew), offsetInFile(inOffset), bIsPacked(false) {}

			EVTAddressesToFix(unsigned short inOriginal, unsigned short inNew, unsigned long inOffset) :
				originalValue(inOriginal), newValue(inNew), offsetInFile(inOffset), bIsPacked(true) {}

			EVTAddressValue originalValue;
			EVTAddressValue newValue;
			unsigned long   offsetInFile;
			bool            bIsPacked;
		};

		//Fixup evt files
		{
			std::string Sakura2("SAKURA2\\");
			std::vector<FileNameContainer> allFiles;
			std::vector<FileNameContainer> evtFiles;
			FindAllFilesWithinDirectory(inPatchedDirectory, allFiles);
			GetAllFilesOfType(allFiles, "EVT", evtFiles);
			for (const FileNameContainer& evtFileNameContainer : evtFiles)
			{
				FileData evtFileData;
				if (!evtFileData.InitializeFileData(evtFileNameContainer))
				{
					return false;
				}

				//Find addresses in evt file that need to be fixed
				std::vector< EVTAddressesToFix > addressesToFix;
				for (unsigned long a = 0; a < evtFileData.GetDataSize() - 2; ++a)
				{
					unsigned long evtValue;
					evtFileData.ReadData(a, (char*)&evtValue, sizeof(evtValue), true);

					if( evtValue == 0x25c40000 || evtValue == 0x25c4f100 )
					{
						addressesToFix.push_back(EVTAddressesToFix(evtValue, evtValue + battleMenuDelta + textDelta, a));
						continue;
					}

					//VDP1 address are stored as shorts where their full address as been divided by 8
					if ((evtValue >> 16) == 0xf610)
					{
						unsigned short packedAddress;
						evtFileData.ReadData(a + 3, (char*)&packedAddress, sizeof(packedAddress), true);

						if (packedVDP1AddressesToFix.find(packedAddress) != packedVDP1AddressesToFix.end())
						{
							addressesToFix.push_back(EVTAddressesToFix(packedAddress, packedVDP1AddressesToFix[packedAddress], a + 3));
						}
					}

					//Addresses at 4byte boundaries can contain the 4byte values stores in addressesFixedInSlg
					if (a % 4 == 0 && ((evtValue & 0xfff00000) == 0x25c00000))
					{
						if (addressesFixedInSlg.find(evtValue) != addressesFixedInSlg.end())
						{
							addressesToFix.push_back(EVTAddressesToFix(evtValue, addressesFixedInSlg[evtValue], a));
						}
					}
				}

				FileReadWriter evtFile;
				if (!evtFile.OpenFile(evtFileNameContainer.mFullPath))
				{
					return false;
				}

				for (const EVTAddressesToFix& addressToFix : addressesToFix)
				{
					if (addressToFix.bIsPacked)
					{
						evtFile.WriteData(addressToFix.offsetInFile, (char*)&addressToFix.newValue.packed, sizeof(addressToFix.newValue.packed), true);

						printf("Fixing address at 0x%0x from 0x%0x to 0x%0x in %s\n", addressToFix.offsetInFile, addressToFix.originalValue.packed, addressToFix.newValue.packed, evtFileNameContainer.mNoExtension.c_str());
					}
					else
					{
						evtFile.WriteData(addressToFix.offsetInFile, (char*)&addressToFix.newValue.full, sizeof(addressToFix.newValue.full), true);

						printf("Fixing address at 0x%0x from 0x%0x to 0x%0x in %s\n", addressToFix.offsetInFile, addressToFix.originalValue.full, addressToFix.newValue.full, evtFileNameContainer.mNoExtension.c_str());
					}
				}
			}
		}
	}
	//***Done Fixing Addresses in EVT Files***
	
	//***Fix battle 1 tutorial code in Evt01***
	if( !bEvtFilesFixed && USE_LARGE_BATTLE_MENU )
	{
		const string evtFilePath     = outDirectory + "EVT01.BIN";
		const string evtOrigFilePath = sakuraDirectory + "SAKURA2\\EVT01.BIN";
		FileWriter evtFile;
		FileData origEvtFile;
		if( !evtFile.OpenFileForWrite(evtFilePath.c_str()) )
		{
			printf("PatchWKL: Unable to open %s file.\n", evtFilePath.c_str());
			return false;
		}
		if( !origEvtFile.InitializeFileData(evtOrigFilePath) )
		{
			printf("PatchWKL: Unable to open %s file.\n", evtOrigFilePath.c_str());
			return false;
		}

		const string tutorialPatchPath = inTranslatedDirectory + "EVT01_Tutorial.bin";
		FileData evt01TutorialPatch;
		if( !evt01TutorialPatch.InitializeFileData(tutorialPatchPath) )
		{
			printf("PatchWKL: Unable to open %s file.\n", tutorialPatchPath.c_str());
			return false;
		}
	

		evtFile.WriteData(origEvtFile.GetData(), origEvtFile.GetDataSize());
		evtFile.WriteDataAtOffset(evt01TutorialPatch.GetData(), evt01TutorialPatch.GetDataSize(), 0x000120F0);
		evtFile.WriteDataAtOffset(&newBattleMenuClippingValue, sizeof(newBattleMenuClippingValue), 0x000004F8, false);
	}
	//***Done Fixing Tutorial Code***

	bEvtFilesFixed = true;

	return true;
}

/*
Misc Images in WKLFiles: Contain battle menu images
-Data block is pointed to at index 0xE8 in the wkl header
-Data block header is [uint offset, uint size]
-The index to the next block is at index 3
-This block starts with a 2 byte entry saying how many images
-8 bytes later the image data starts
-Image Data[uint offset, uint size, ushort width, ushort height, uint unknown]
*/
void ExtractWKLFiles(const string& sakuraDirectory, const string& outDirectory)
{
	//Find all files
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(sakuraDirectory, allFiles);
	if( !allFiles.size() )
	{
		return;
	}

	const string slgFileName = sakuraDirectory + string("0SLG.BIN");
	FileData slgFile;
	if( !slgFile.InitializeFileData("0SLG.BIN", slgFileName.c_str()) )
	{
		printf("Unable to open %s", slgFileName.c_str());
		return;
	}

	SakuraPalette paletteData(&slgFile.GetData()[0x00011B78], 32);

	vector<FileNameContainer> wklFiles;
	GetAllFilesOfType(allFiles, "WKL", wklFiles);

	WklCompressedInfo compressedInfo[64];

	/*
	for(const FileNameContainer& fileNameInfo : wklFiles)
	{
		printf("Extracting %s\n", fileNameInfo.mFileName.c_str());

		const string outSubDirName = outDirectory + fileNameInfo.mNoExtension + Seperators;
		if( !CreateDirectoryHelper(outSubDirName) )
		{
			printf("Unable to create directory: %s", outSubDirName.c_str());
			return;
		}


		WklBattleMenuExtractor wklExtractor;
		wklExtractor.Initialize(fileNameInfo);
		wklExtractor.DumpImages(outSubDirName, paletteData);
	}*/
	
	
	for(const FileNameContainer& fileNameInfo : wklFiles)
	{
		printf("Extracting %s\n", fileNameInfo.mFileName.c_str());

		const string outSubDirName = outDirectory + fileNameInfo.mNoExtension + Seperators;
		if( !CreateDirectoryHelper(outSubDirName) )
		{
			printf("Unable to create directory: %s", outSubDirName.c_str());
			return;
		}

		FileData wklFile;
		if( !wklFile.InitializeFileData(fileNameInfo) )
		{
			return;
		}

		const char* pWklData = wklFile.GetData();

		//***Character Nmes & Special Moves****
		//Read in the address at which the compressed image data lives
		unsigned long offsetToImageData = 0x100;
		unsigned long imageDataAddress  = 0;
		memcpy_s(&imageDataAddress, sizeof(imageDataAddress), &pWklData[offsetToImageData], sizeof(imageDataAddress));
		imageDataAddress = SwapByteOrder(imageDataAddress);

		//Read in offset to compressed data
		memcpy_s(&compressedInfo, sizeof(compressedInfo), &pWklData[imageDataAddress], sizeof(compressedInfo));

		char tempBuffer[512];
		for(int i = 0; i < 64; ++i)
		{
			if( !compressedInfo[i].size )
			{
				continue;
			}

			const unsigned int imageOffset = SwapByteOrder(compressedInfo[i].compressedInfoOffset);
			const unsigned int offsetToCompressedData = imageOffset + imageDataAddress;
			
			//Uncompress image data
			PRSDecompressor uncompressedImages;
			uncompressedImages.UncompressData( (void*)(pWklData + offsetToCompressedData), wklFile.GetDataSize() - (offsetToCompressedData));
			if( !uncompressedImages.mCompressedSize )
			{
				continue;
			}

			//Create prefix
			sprintf_s(tempBuffer, 512, "%i_%0x", i, imageOffset);
			const string prefix = string(tempBuffer);

			WklUncompressedData uncompressedImageData;
			uncompressedImageData.ReadInImages(uncompressedImages.mpUncompressedData);
			uncompressedImageData.DumpImages(paletteData.mpPaletteData, paletteData.mPaletteSize, prefix, outSubDirName);
		}
		//***Done with Character Names & Special Moves

		//***Other images***
		unsigned long offsetToImageData2 = 0xE8;
		unsigned long imageDataAddress2  = 0;
		memcpy_s(&imageDataAddress2, sizeof(imageDataAddress2), &pWklData[offsetToImageData2], sizeof(imageDataAddress));
		imageDataAddress2 = SwapByteOrder(imageDataAddress2);

		//Read the first entry so we can now how big the header is
		WklBattleMenuImageBlockInfo firstEntry;
		memcpy_s(&firstEntry, sizeof(firstEntry), &pWklData[imageDataAddress2], sizeof(firstEntry));
		firstEntry.SwapByteOrder();

		const unsigned int numEntries = firstEntry.battleMenuImageOffset/sizeof(WklBattleMenuImageBlockInfo);
		WklBattleMenuImageBlockInfo* pMiscHeaders   = new WklBattleMenuImageBlockInfo[numEntries];
		memcpy_s(pMiscHeaders, sizeof(WklBattleMenuImageBlockInfo)*numEntries, &pWklData[imageDataAddress2], sizeof(WklBattleMenuImageBlockInfo)*numEntries);
		for(unsigned int entryIndex = 0; entryIndex < numEntries; ++entryIndex)
		{
			pMiscHeaders[entryIndex].SwapByteOrder();

			unsigned short numMiscImages = 0;
			memcpy_s(&numMiscImages, sizeof(numMiscImages), &pWklData[imageDataAddress2 + pMiscHeaders[entryIndex].battleMenuImageOffset], sizeof(numMiscImages));
			numMiscImages = SwapByteOrder(numMiscImages);

			WklBattleMenuImageHeader* pImageHeaders     = new WklBattleMenuImageHeader[numMiscImages];
			const unsigned int imageHeaderEnd     = imageDataAddress2 + pMiscHeaders[entryIndex].battleMenuImageOffset + 8;
			const unsigned int miscImageHeaderEnd = imageHeaderEnd + sizeof(WklBattleMenuImageHeader)*(numMiscImages) + 8; //Last 8 bytes is some uknown thing
			memcpy_s(pImageHeaders, sizeof(WklBattleMenuImageHeader)*numMiscImages, &pWklData[imageHeaderEnd], sizeof(WklBattleMenuImageHeader)*numMiscImages);
			for(unsigned short imageIndex = 0; imageIndex < numMiscImages; ++imageIndex)
			{	
				WklBattleMenuImageHeader& imageHeader = pImageHeaders[imageIndex];
				imageHeader.SwapByteOrder();

				//Dump iamge
				sprintf_s(tempBuffer, 512, "Misc_%i_%0x", entryIndex, imageIndex);
				const string prefix = string(tempBuffer);

				const string outFileName = outSubDirName + prefix + BMPExtension;
				const char* pImageData   = &pWklData[miscImageHeaderEnd + imageHeader.offsetFromHeader + imageHeader.offsetFromPrevImage];
				ExtractImageFromData(pImageData, (imageHeader.width*imageHeader.height)/2, outFileName, paletteData.mpPaletteData, paletteData.mPaletteSize, imageHeader.width, imageHeader.height, 1, 16, 0, false, true);
				
			}
			delete[] pImageHeaders;
		}
		delete[] pMiscHeaders;
		//***Done with Other Images
	}
}

struct SakuraLut
{
	unsigned short addressInTmapSP;
	unsigned char  reserved1;
	unsigned char  reserved2;
	unsigned char  width;
	unsigned char  height;
	unsigned char  reserved3;
	unsigned char  reserved4;
};
void ExtractTMapSP(const string& sakuraDirectory, const string& paletteFileName, const string& outDirectory)
{
	const string tmapFilePath = sakuraDirectory + string("SAKURA1\\TMAPSP.BIN");
	FileNameContainer tmapFileNameInfo(tmapFilePath.c_str());

	const string sakuraFilePath = sakuraDirectory + string("SAKURA");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());

	FileNameContainer paletteFileNameInfo(paletteFileName.c_str());
	FileData paletteFile;
	if( !paletteFile.InitializeFileData(paletteFileNameInfo) )
	{
		return;
	}

	FileData sakuraFileData;
	if( !sakuraFileData.InitializeFileData(sakuraFileNameInfo) )
	{
		return;
	}

	FileData tmapFileData;
	if( !tmapFileData.InitializeFileData(tmapFileNameInfo) )
	{
		return;
	}

	const string fukagawaPath = sakuraDirectory + string("SAKURA1\\FUKAGAWA.BIN");
	FileData fukagawaData;
	if( !fukagawaData.InitializeFileData( FileNameContainer(fukagawaPath.c_str()) ) )
	{
		return;
	}

	PaletteData paletteData;
	if( !paletteData.CreateFrom15BitData(paletteFile.GetData(), paletteFile.GetDataSize()) )
	{
		printf("Unable to create palette.\n");
		return;
	}

	//TMAPSP
	{
		const int numLutEntries = 93;
		SakuraLut lookupTable[numLutEntries];
		const unsigned int offsetToData = 0x0001EBF4;
		memcpy_s(lookupTable, sizeof(lookupTable), sakuraFileData.GetData() + offsetToData, sizeof(lookupTable));

		for(int i = 0; i < numLutEntries; ++i)
		{
			lookupTable[i].addressInTmapSP = SwapByteOrder(lookupTable[i].addressInTmapSP);

			const string outFileName = outDirectory + std::to_string(i);

			BitmapWriter outBmp;
			outBmp.CreateBitmap(outFileName, lookupTable[i].width*8, lookupTable[i].height, 4, tmapFileData.GetData() + lookupTable[i].addressInTmapSP*8, (lookupTable[i].width*lookupTable[i].height)/2, paletteData.GetData(), paletteData.GetSize(), false);
		}
	}

	//FUKAGAWA
	{
		const int numLutEntries = 67;
		SakuraLut lookupTable[numLutEntries];
		const unsigned int offsetToData = 0x00020760;
		memcpy_s(lookupTable, sizeof(lookupTable), sakuraFileData.GetData() + offsetToData, sizeof(lookupTable));

		const string fukagawaDirectory = outDirectory + string("FUKAGAWA\\");
		CreateDirectoryHelper(fukagawaDirectory);
		for(int i = 0; i < numLutEntries; ++i)
		{
			lookupTable[i].addressInTmapSP = SwapByteOrder(lookupTable[i].addressInTmapSP);

			const string outFileName = fukagawaDirectory + std::to_string(i) + string(".bmp");

			ExtractImageFromData(fukagawaData.GetData() + lookupTable[i].addressInTmapSP*8, 
								 (lookupTable[i].width*lookupTable[i].height)/2, 
								 outFileName, 
								 paletteFile.GetData(), 
								 paletteFile.GetDataSize(), 
								 lookupTable[i].width*8, 
								 lookupTable[i].height, 
								 1,
								 256, 
								 0,
								 false, 
								 true);
		}
	}

	printf("Success\n");
}

bool PatchTheaterView(const string& sakuraDirectory, const string& patchDataPath, const int inNumLutEntries, const int inOffsetToLookupTable, const char* inTheaterFilePath, 
					  const unsigned int inMaxSize)
{	
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(patchDataPath, allFiles);

	vector<FileNameContainer> patchedImages;
	GetAllFilesOfType(allFiles, ".bmp", patchedImages);

	if( patchedImages.size() == 0 )
	{
		return false;
	}

	const string tmapFilePath = sakuraDirectory + string(inTheaterFilePath);
	FileNameContainer tmapFileNameInfo(tmapFilePath.c_str());

	const string sakuraFilePath = sakuraDirectory + string("SAKURA");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());

	//Open TMapFile
	FILE* pTMapSpFile = nullptr;
	errno_t errorValue = fopen_s(&pTMapSpFile, tmapFilePath.c_str(), "r+b");
	if( errorValue )
	{
		printf("Unable to open TMapSP file.  Error: %i\n", errorValue);
		return false;
	}

	//Open SAKURA file
	FILE* pSakuraFile = nullptr;
	errorValue = fopen_s(&pSakuraFile, sakuraFilePath.c_str(), "r+b");
	if( errorValue )
	{
		printf("Unable to open SAKURA file.  Error: %i\n", errorValue);
		fclose(pTMapSpFile);
		return false;
	}

	//Read in lookup table to know where the images reside in TMapSP
	SakuraLut* lookupTable        = new SakuraLut[inNumLutEntries];
	SakuraLut* patchedLookupTable = new SakuraLut[inNumLutEntries];
	fseek(pSakuraFile, inOffsetToLookupTable, SEEK_SET);
	fread(lookupTable, sizeof(SakuraLut)*inNumLutEntries, 1, pSakuraFile);

	//Create a copy for the patched version of the table.  We'll modify that below.
	memcpy_s(patchedLookupTable, sizeof(SakuraLut)*inNumLutEntries, lookupTable, sizeof(SakuraLut)*inNumLutEntries);
	

	//**Create the palette from one of the images**
	//Read in first image
	BitmapReader firstImage;
	if( !firstImage.ReadBitmap(patchedImages[0].mFullPath) )
	{
		return false;
	}

	//Address of first image in TMapSP
	const int firstPatchedImageNum = atoi(patchedImages[0].mNoExtension.c_str());
	unsigned short imageAddress    = SwapByteOrder(lookupTable[firstPatchedImageNum].addressInTmapSP)*8;

	//Copy everything from original TMapSp file to the new version of it
	unsigned char* pTMapSpBuffer = new unsigned char[imageAddress];
	if( !fread_s(pTMapSpBuffer, imageAddress, imageAddress, 1, pTMapSpFile) )
	{
		printf("PatchTheaterView: Unable to copy over original TMapSP data.\n");
		fclose(pSakuraFile);
		fclose(pTMapSpFile);
		return false;
	}
	fclose(pTMapSpFile);

	//Create new TMapSPFile
	FileWriter patchedTMapSP;
	if( !patchedTMapSP.OpenFileForWrite(tmapFileNameInfo.mFullPath) )
	{
		printf("PatchTheaterView: Unable to create patched TMapSP file.\n");
		fclose(pSakuraFile);
		return false;
	}

	//Write out the initial unpatched images
	if( !patchedTMapSP.WriteData(pTMapSpBuffer, imageAddress) )
	{
		printf("PatchTheaterView: Unable to copy buffer to patched TMapSP file.\n");
		fclose(pSakuraFile);
		return false;
	}
	delete[] pTMapSpBuffer;

	//Write out all the patched files into the new TMapSP file
	unsigned int newImageSize = 0;
	const size_t numPatchedImages = patchedImages.size();
	const string emptyRoom("EmptyRoom");
	for(size_t imageIndex = 0; imageIndex < numPatchedImages; ++imageIndex)
	{
		//Read in translated font sheet
		BitmapReader origTranslatedBmp;
		if( !origTranslatedBmp.ReadBitmap(patchedImages[imageIndex].mFullPath) )
		{
			fclose(pSakuraFile);
			return false;
		}

		const unsigned int newImageWidth  = origTranslatedBmp.mBitmapData.mInfoHeader.mImageWidth;
		const unsigned int newImageHeight = origTranslatedBmp.mBitmapData.mInfoHeader.mImageHeight;
		newImageSize                     += newImageWidth*newImageHeight/2;

		if( newImageWidth/8 > 0xff )
		{
			printf("PatchTheaterView failed.  Image width for %s is invalid.  Max width is %i", patchedImages[imageIndex].mFileName.c_str(), 0xff*8);
			return false;
		}

		if( newImageHeight > 0xff )
		{
			printf("PatchTheaterView failed.  Image height for %s is invalid.  Max height is %i", patchedImages[imageIndex].mFileName.c_str(), 0xff);
			return false;
		}

		bool bIsEmptyRoomImage = patchedImages[imageIndex].mNoExtension == emptyRoom;
		if( bIsEmptyRoomImage )
		{
			if( newImageWidth != 56 && newImageHeight != 24 )
			{
				printf("PatchTheaterView failed.  EmptyRoom image must be 56x24 4bpp.");
				return false;
			}
		}
		else
		{
			const int lutIndex = atoi(patchedImages[imageIndex].mNoExtension.c_str());
			if( lutIndex >= inNumLutEntries || lutIndex < 0 )
			{
				printf("Invalid image number: %i.  Only % images allowed.\n", lutIndex, inNumLutEntries);
				fclose(pSakuraFile);
				return false;
			}

			if( lutIndex == 92 )
			{
				//Modify the patched lookup table
				patchedLookupTable[lutIndex].width           = patchedLookupTable[64].width;
				patchedLookupTable[lutIndex].height          = patchedLookupTable[64].height;
				patchedLookupTable[lutIndex].addressInTmapSP = patchedLookupTable[64].addressInTmapSP;
				continue;
			}

			//Modify the patched lookup table
			patchedLookupTable[lutIndex].width  = (unsigned char)(newImageWidth/8);
			patchedLookupTable[lutIndex].height = (unsigned char)newImageHeight;
			patchedLookupTable[lutIndex].addressInTmapSP = SwapByteOrder( (unsigned short)(imageAddress/8) );
		}

		TileExtractor tileExtractor;
		if( !tileExtractor.ExtractTiles(newImageWidth, newImageHeight, newImageWidth, newImageHeight, origTranslatedBmp) )
		{
			fclose(pSakuraFile);
			return false;
		}

		if( tileExtractor.mTiles.size() != 1 || 
			tileExtractor.mTiles[0].mTileSize != (newImageWidth*newImageHeight/2) )
		{
			fclose(pSakuraFile);
			printf("PatchTheaterView: Invalid patched image: %s\n", patchedImages[imageIndex].mNoExtension.c_str());
		}

		//Write out the SakuraTaisen format image
	//	tileExtractor.FixupIndexOfAlphaColor((unsigned short)indexOfAlphaColor, true);
		for(TileExtractor::Tile& tile : tileExtractor.mTiles)
		{
			//Empty room image goes into sakura file
			if( bIsEmptyRoomImage )
			{
				const unsigned int emptyMapImageAddress = GIsDisc2 ? 0x00058e48 : 0x00058d10;

				//Save the patched lookup table in the SAKURA file
				fseek(pSakuraFile, emptyMapImageAddress, SEEK_SET);
				if( fwrite(tile.mpTile, tile.mTileSize, 1, pSakuraFile) != 1 )
				{
					printf("PatchTheaterView: Unable to write out EmptyRoom image to SAKURA.\n");
					fclose(pSakuraFile);
					return false;
				}
			}
			else
			{
				if( !patchedTMapSP.WriteData(tile.mpTile, tile.mTileSize) )
				{
					printf("PatchTheaterView: Unable to write out patched image %s.\n", patchedImages[imageIndex].mFileName.c_str());
					fclose(pSakuraFile);
					return false;
				}
			}

		}

		imageAddress += (unsigned short)(newImageWidth*newImageHeight/2);
	}

	if( patchedTMapSP.GetFileSize() > inMaxSize )
	{
		printf("PatchTheaterView: Patched TMapSP file is too big.  Max size is 0xD000, current size is %lu.\n", patchedTMapSP.GetFileSize());
		fclose(pSakuraFile);
		return false;
	}

	//Save the patched lookup table in the SAKURA file
	fseek(pSakuraFile, inOffsetToLookupTable, SEEK_SET);
	if( fwrite(patchedLookupTable, sizeof(SakuraLut)*inNumLutEntries, 1, pSakuraFile) != 1 )
	{
		printf("PatchTheaterView: Unable to write out patched lookup table.\n");
		fclose(pSakuraFile);
		return false;
	}

	fclose(pSakuraFile);

	delete[] lookupTable;
	delete[] patchedLookupTable;

	return true;
}

bool PatchTMapSP(const string& sakuraDirectory, const string& patchDataPath)
{
	const string patchedTMapDir     = patchDataPath + string("\\TMapSP\\");
	const string patchedFukagawaDir = patchDataPath + string("\\Fukagawa\\");

	if( !PatchTheaterView(sakuraDirectory, patchedTMapDir,     93, GIsDisc2 ? 0x0001EC0C : 0x0001EBF4, "SAKURA1\\TMAPSP.BIN", 0xd000) ) //0x0001ecc disc 2
	{
		return false;
	}

	if( !PatchTheaterView(sakuraDirectory, patchedFukagawaDir, 67, GIsDisc2 ? 0x00020778 : 0x00020760, "SAKURA1\\FUKAGAWA.BIN", 0x6d3f) ) //0x00020778 disc2
	{
		return false; 
	}

	return true;
}

void FindCMPData(const string& /*inCompressedFilePath*/, const string& /*inUncompressedFilePath*/, const string& /*outDirectory*/)
{
#if 0
	FileData inFileData;
	if( !inFileData.InitializeFileData( FileNameContainer(inCompressedFilePath.c_str()) ) )
	{
		return;
	}

	FileData searchData;
	if( !searchData.InitializeFileData( FileNameContainer(inUncompressedFilePath.c_str()) ) )
	{
		return;
	}

	char buffer[MAX_PATH];
	for(unsigned long i = 0; i < inFileData.GetDataSize(); ++i)
	{
		int fileOffset = (int)i;
		int dataSizeBytes = 0;
		int cmprType = 0;

		if(analyzeHeader(inCompressedFilePath.c_str(), &fileOffset, &dataSizeBytes, &cmprType) < 0 )
		{
			continue;
		}

		snprintf(buffer, MAX_PATH, "%s\\%i.bin", outDirectory.c_str(), i);
		const string outFile(buffer);
		if( cmp_decompress(inCompressedFilePath.c_str(), outFile.c_str(), fileOffset, dataSizeBytes, cmprType) == 0 )
		{
			FileData newFile;
			if( !newFile.InitializeFileData( FileNameContainer(outFile.c_str()) ) )
			{
				continue;
			}

			if( newFile.GetDataSize() < searchData.GetDataSize() )
			{
				continue;
			}

			if( FileData::IsDataTheSame(newFile.GetData(), searchData.GetData(), searchData.GetDataSize()) )
			{
				printf("Found at: %i\n", i);
				break;
			}
		}
	}

	printf("Not found\n");
#endif
}

void FindCompressedData(const string& inCompressedFilePath, const string& inUncompressedFilePath, const string& outDirectory)
{
	FileNameContainer compressedFileName(inCompressedFilePath.c_str());
	FileData compresedFile;
	if( !compresedFile.InitializeFileData(compressedFileName) )
	{
		return;
	}

	FileNameContainer uncompressedFileName(inUncompressedFilePath.c_str());
	FileData uncompressedFile;
	if( !uncompressedFile.InitializeFileData(uncompressedFileName) )
	{
		return;
	}

	struct FoundData
	{
		FoundData(unsigned long a, unsigned long b, unsigned long c, unsigned long d) : uncompressionStart(a), offestInUncompressedData(b), dataSize(c), compressedSize(d){}
		unsigned long uncompressionStart;
		unsigned long offestInUncompressedData;
		unsigned long dataSize;
		unsigned long compressedSize;
	};

	//Uncompress the compressed data starting from the first byte and going all the way to the end.  Each time, check to see if it contains the uncompressed data we are searching for.
	vector<FoundData> foundIndices;
	const unsigned long fileSize = compresedFile.GetDataSize();
	for(unsigned long index = 0; index < fileSize; ++index)
	{
		//printf("Progress: %lu/%lu\n", index, fileSize);

		//Uncompress
		PRSDecompressor decompressor;
		if( !decompressor.UncompressData( (void*)(compresedFile.GetData() + index), compresedFile.GetDataSize() - index ) )
		{
			continue;
		}

		//Check to see if the search data is found
		unsigned long foundIndex = 0;
		if( FindDataWithinBuffer(decompressor.mpUncompressedData, decompressor.mUncompressedDataSize, uncompressedFile.GetData(), uncompressedFile.GetDataSize(), foundIndex) )
		{
			//Find size of original compressed data
			//PRSCompressor compressedInfo;
			//compressedInfo.CompressData((void*)decompressor.mpUncompressedData, decompressor.mUncompressedDataSize);
			
			foundIndices.push_back( FoundData(index, foundIndex, uncompressedFile.GetDataSize(), decompressor.mCompressedSize) );
			printf("Found at: %lu\n", index);

			char numBuffer[50];
			sprintf_s(numBuffer, 50, "%08x", index);

			//Output uncompressed data
			const string outFileName = outDirectory + string(numBuffer) + string(".bin");
			FileWriter outFile;
			if( !outFile.OpenFileForWrite(outFileName) )
			{
				printf("Unable to open file for output: %s", outFileName.c_str());
			}
			else
			{
				outFile.WriteData(decompressor.mpUncompressedData, decompressor.mUncompressedDataSize);
			}
		}
	}
	
	if( foundIndices.size() )
	{
		printf("Results:\n");
		
		for(const FoundData& foundResult : foundIndices)
		{
			printf("Found: At %08x OffsetWithinUncompressedData: %08x Size: %lu CompressedSize: %lu\n", foundResult.uncompressionStart, foundResult.offestInUncompressedData, foundResult.dataSize, foundResult.compressedSize);
		}
	}
	else
	{
		printf("Data not found:\n");
	}
}

int GetBytesInSequence(unsigned char inByte)
{
	unsigned int sequenceSize = inByte;
	if( (inByte >> 4) == 0x0f )
	{
		sequenceSize = 0xffffff00 | inByte;
	}

	int count = 0;
	while(sequenceSize)
	{
		sequenceSize = sequenceSize >> 1;
		++count;
	}

	return count;
}

void DecompressionTest()
{
	/*
	FileData testData;
	//if( !testData.InitializeFileData("FACE01.BIN", "D:\\Rizwan\\SakuraWars\\Disc1\\SAKURA2\\FACE01.BIN") )
	if( !testData.InitializeFileData("Uncompressed.bin", "A:\\SakuraTaisen\\Uncompressed.bin") )
	{
		return;
	}

	vector<unsigned char> output;
	const unsigned char* compressedData = (const unsigned char*)testData.GetData();//0xFDD7;//0xFD09;//0xFDF8;
	unsigned long destSize = prs_decompress_size((void*)compressedData);
	unsigned char* dest = new unsigned char[destSize];
	prs_decompress((void *)compressedData, dest, destSize);

	FILE* pOutFile = nullptr;
	fopen_s(&pOutFile, "A:\\SakuraTaisen\\UncompressedDataResults\\UncompressedFace.bin", "wb");
	if( pOutFile )
	{
		fwrite(dest, 1, destSize, pOutFile);

		fclose(pOutFile);
	}
	delete[] dest;*/
}

void CompressFile(const string& filePath, const string& outPath)
{
	FileData testData;
	FileNameContainer inFileName(filePath.c_str());
	if( !testData.InitializeFileData(inFileName) )
	{
		return;
	}

	FileWriter outFile;
	if( !outFile.OpenFileForWrite(outPath) )
	{
		return;
	}

	PRSCompressor compressedFile;
	compressedFile.CompressData((void*)testData.GetData(), testData.GetDataSize(), PRSCompressor::kCompressOption_None);

	outFile.WriteData(compressedFile.mpCompressedData, compressedFile.mCompressedSize);
}

struct MainMenuText
{
	unsigned short x;
	unsigned short y;
	unsigned short width;
	unsigned short height;
	unsigned short characterOffset; //multiple of 16
		

	void SetCharacter(char c, short xPosition)
	{	
		SetCharacter(c);
			
		x = (unsigned short)xPosition;
		SwapByteOrderInPlace((char*)&x, sizeof(x));
	}

	void SetCharacterWithOffset(char c, short xOffset)
	{
		SetCharacter(c);

		SwapByteOrderInPlace((char*)&x, sizeof(x));
		x += xOffset;
		SwapByteOrderInPlace((char*)&x, sizeof(x));
	}

	void SetCharacter(char c)
	{
		characterOffset = c*16;
		SwapByteOrderInPlace((char*)&characterOffset, sizeof(characterOffset));
	}

	void SwapEndianness()
	{
		SwapByteOrderInPlace((char*)&width, sizeof(width));
		SwapByteOrderInPlace((char*)&height, sizeof(height));
		SwapByteOrderInPlace((char*)&characterOffset, sizeof(characterOffset));
		SwapByteOrderInPlace((char*)&x, sizeof(x));
		SwapByteOrderInPlace((char*)&y, sizeof(y));
	}
};

struct OptionsSettingsMenu
{
	void CreateTranslatedMenu(FileReadWriter& sakuraFileData, bool bIsSLG)
	{
		const int Disc2Offset = GIsDisc2 ? 0x138 : 0;

		//SAKURA layout code
		const unsigned long optionsCursorSpeedTextOffset = bIsSLG ? 0x0004808c : 0x0005aa50 + Disc2Offset; //5ab88
		const unsigned long optionsSoundTextOffset       = bIsSLG ? 0x00047fb0 : 0x0005a974 + Disc2Offset;
		const unsigned long optionsVoiceTextOffset       = bIsSLG ? 0x00047f04 : 0x0005a8c8 + Disc2Offset;
		const unsigned long optionsControlsTextOffset    = bIsSLG ? 0x00047e3c : 0x0005a800 + Disc2Offset;
		const unsigned long optionsExitTextOffset        = bIsSLG ? 0x00047dd2 : 0x0005a796 + Disc2Offset;
		MainMenuText cursorSpeedText[11];
		MainMenuText soundText[12];
		MainMenuText voiceText[7];
		MainMenuText controlsText[10];
		MainMenuText exitText[4];
		sakuraFileData.ReadData(optionsCursorSpeedTextOffset, (char*)cursorSpeedText, sizeof(cursorSpeedText));
		sakuraFileData.ReadData(optionsSoundTextOffset,       (char*)soundText,       sizeof(soundText));
		sakuraFileData.ReadData(optionsVoiceTextOffset,       (char*)voiceText,       sizeof(voiceText));
		sakuraFileData.ReadData(optionsControlsTextOffset,    (char*)controlsText,    sizeof(controlsText));
		sakuraFileData.ReadData(optionsExitTextOffset,        (char*)exitText,        sizeof(exitText));
	
		const INT emptyOptionsChar = 60;
		cursorSpeedText[0].SetCharacter(0); //0-5 is "Cursor Speed"
		cursorSpeedText[1].SetCharacter(1);
		cursorSpeedText[2].SetCharacter(2);
		cursorSpeedText[3].SetCharacter(3);
		cursorSpeedText[4].SetCharacter(4);
		cursorSpeedText[5].SetCharacter(emptyOptionsChar);
		cursorSpeedText[6].SetCharacter(5);  //slow
		cursorSpeedText[7].SetCharacter(7);  //dot
		cursorSpeedText[8].SetCharacter(7);  //dot
		cursorSpeedText[9].SetCharacter(7);  //dot
		cursorSpeedText[10].SetCharacter(6); //fast

		//Sound
		soundText[0].SetCharacter(8); 
		soundText[1].SetCharacter(9);
		soundText[2].SetCharacter(10);
		soundText[3].SetCharacter(emptyOptionsChar);

		//Stereo
		short xOffset = 15;
		soundText[4].SetCharacterWithOffset(11, xOffset);
		soundText[5].SetCharacterWithOffset(12, xOffset);
		soundText[6].SetCharacterWithOffset(13, xOffset);
		soundText[7].SetCharacterWithOffset(emptyOptionsChar, xOffset);

		//Mono
		xOffset = 16;
		soundText[8].SetCharacterWithOffset(14, xOffset);
		soundText[9].SetCharacterWithOffset(15, xOffset);
		soundText[10].SetCharacterWithOffset(16, xOffset);
		soundText[11].SetCharacterWithOffset(emptyOptionsChar, xOffset);

		//Voices
		voiceText[0].SetCharacter(17);
		voiceText[1].SetCharacter(18);

		//On
		voiceText[2].SetCharacter(19);
		voiceText[3].SetCharacter(20);

		//Off
		xOffset = 8;
		voiceText[4].SetCharacterWithOffset(21, xOffset);
		voiceText[5].SetCharacterWithOffset(22, xOffset);
		voiceText[6].SetCharacterWithOffset(emptyOptionsChar, xOffset);

		//Controls
		controlsText[0].SetCharacter(23);
		controlsText[1].SetCharacter(24);
		controlsText[2].SetCharacter(25);
		controlsText[3].SetCharacter(emptyOptionsChar);

		//Set
		controlsText[4].SetCharacter(26);
		controlsText[5].SetCharacter(27);

		//Accept
		xOffset = 13;
		controlsText[6].SetCharacterWithOffset(28, xOffset);
		controlsText[7].SetCharacterWithOffset(29, xOffset);
		controlsText[8].SetCharacterWithOffset(30, xOffset);
		controlsText[9].SetCharacterWithOffset(emptyOptionsChar, xOffset);

		//Exit
		xOffset = 16;
		exitText[0].SetCharacterWithOffset(31, xOffset);
		exitText[1].SetCharacterWithOffset(32, xOffset);
		exitText[2].SetCharacterWithOffset(emptyOptionsChar, xOffset);
		exitText[3].SetCharacterWithOffset(emptyOptionsChar, xOffset);

		//Ok
		const unsigned long controlsOkTextOffset = bIsSLG ? 0x00047dbe : 0x0005a782 + Disc2Offset;
		MainMenuText controlsOk[2];
		sakuraFileData.ReadData(controlsOkTextOffset, (char*)controlsOk, sizeof(controlsOk));
		controlsOk[0].SetCharacter(33);
		controlsOk[1].SetCharacter(34);

		//Cancel
		const unsigned long controlsCancelTextOffset = bIsSLG ? 0x00047d96 : 0x0005a75a + Disc2Offset;
		MainMenuText controlsCancel[4];
		sakuraFileData.ReadData(controlsCancelTextOffset, (char*)controlsCancel, sizeof(controlsCancel));
		controlsCancel[0].SetCharacter(35);
		controlsCancel[1].SetCharacter(36);
		controlsCancel[2].SetCharacter(37);
		controlsCancel[3].SetCharacter(emptyOptionsChar);

		//Move
		const unsigned long controlsMoveTextOffset = bIsSLG ? 0x00047ce2 : 0x0005a6a6 + Disc2Offset;
		MainMenuText controlsMove[2];
		sakuraFileData.ReadData(controlsMoveTextOffset, (char*)controlsMove, sizeof(controlsMove));
		controlsMove[0].SetCharacter(38);
		controlsMove[1].SetCharacter(39);

		//Attack
		const unsigned long controlsAttackTextOffset = bIsSLG ? 0x00047cf6 : 0x0005a6ba + Disc2Offset;
		MainMenuText controlsAttack[4];
		sakuraFileData.ReadData(controlsAttackTextOffset, (char*)controlsAttack, sizeof(controlsAttack));
		controlsAttack[0].SetCharacter(40);
		controlsAttack[1].SetCharacter(41);
		controlsAttack[2].SetCharacter(42);
		controlsAttack[3].SetCharacter(emptyOptionsChar);

		//Defend
		const unsigned long controlsDefendTextOffset = bIsSLG ? 0x00047d32 : 0x0005a6f6 + Disc2Offset;
		MainMenuText controlsDefendText[2];
		sakuraFileData.ReadData(controlsDefendTextOffset, (char*)controlsDefendText, sizeof(controlsDefendText));
		controlsDefendText[0].SetCharacter(43);
		controlsDefendText[1].SetCharacter(44);

		//Charge
		const unsigned long controlsChargeTextOffset = bIsSLG ? 0x00047d1e : 0x0005a6e2 + Disc2Offset;
		MainMenuText controlsChargeText[2];
		sakuraFileData.ReadData(controlsChargeTextOffset, (char*)controlsChargeText, sizeof(controlsChargeText));
		controlsChargeText[0].SetCharacter(45);
		controlsChargeText[1].SetCharacter(46);

		//View All
		const unsigned long controlsViewAllTextOffset = bIsSLG ? 0x00047d5a : 0x0005a71e + Disc2Offset;
		MainMenuText controlsViewAllText[2];
		sakuraFileData.ReadData(controlsViewAllTextOffset, (char*)controlsViewAllText, sizeof(controlsViewAllText));
		controlsViewAllText[0].SetCharacter(47);
		controlsViewAllText[1].SetCharacter(48);

		//Advice
		const unsigned long controlsAdviceTextOffset = bIsSLG ? 0x00047d46 : 0x0005a70a + Disc2Offset;
		MainMenuText controlsAdviceText[2];
		sakuraFileData.ReadData(controlsAdviceTextOffset, (char*)controlsAdviceText, sizeof(controlsAdviceText));
		controlsAdviceText[0].SetCharacter(49);
		controlsAdviceText[1].SetCharacter(50);

		//Ok
		const unsigned long controlsOk2TextOffset = bIsSLG ? 0x00047dbe : 0x0005a782 + Disc2Offset;
		MainMenuText controlsOk2Text[2];
		sakuraFileData.ReadData(controlsOk2TextOffset, (char*)controlsOk2Text, sizeof(controlsOk2Text));
		controlsOk2Text[0].SetCharacter(33);
		controlsOk2Text[1].SetCharacter(34);

		//NoSetting
		const unsigned long controlsNoSettingTextOffset = bIsSLG ? 0x00047cba : 0x0005a67e + Disc2Offset;
		MainMenuText controlsNoSetting[4];
		sakuraFileData.ReadData(controlsNoSettingTextOffset, (char*)controlsNoSetting, sizeof(controlsNoSetting));
		controlsNoSetting[0].SetCharacter(51);
		controlsNoSetting[1].SetCharacter(52);
		controlsNoSetting[2].SetCharacter(53);
		controlsNoSetting[3].SetCharacter(54);

		//Default
		const unsigned long controlsDefaultTextOffset = bIsSLG ? 0x00047c92 : 0x0005a656 + Disc2Offset;
		MainMenuText controlsDefault[4];
		sakuraFileData.ReadData(controlsDefaultTextOffset, (char*)controlsDefault, sizeof(controlsDefault));
		controlsDefault[0].SetCharacter(55);
		controlsDefault[1].SetCharacter(56);
		controlsDefault[2].SetCharacter(57);
		controlsDefault[3].SetCharacter(emptyOptionsChar);
		//X
		const unsigned long controlsXOffset = bIsSLG ? 0x0004830a : 0x0005acce + Disc2Offset;
		MainMenuText controlsXText[1];
		sakuraFileData.ReadData(controlsXOffset, (char*)controlsXText, sizeof(controlsXText));
		controlsXText[0].SetCharacter(58);

		//X for accept
		const unsigned long controlsXForAcceptOffset = bIsSLG ? 0x00047c38 : 0x0005a5fc + Disc2Offset;
		MainMenuText controlsXForAcceptText[1];
		sakuraFileData.ReadData(controlsXForAcceptOffset, (char*)controlsXForAcceptText, sizeof(controlsXForAcceptText));
		controlsXForAcceptText[0].SetCharacter(58);

		//Exit
		const unsigned long controlsExitOffset = bIsSLG ? 0x00047c6a : 0x0005a62e + Disc2Offset;
		MainMenuText controlsExit[4];
		sakuraFileData.ReadData(controlsExitOffset, (char*)controlsExit, sizeof(controlsExit));
		controlsExit[0].SetCharacter(31);
		controlsExit[1].SetCharacter(32);
		controlsExit[2].SetCharacter(emptyOptionsChar);
		controlsExit[3].SetCharacter(emptyOptionsChar);
		//**Done creating text for the main menu**

		sakuraFileData.WriteData(optionsCursorSpeedTextOffset, (char*)cursorSpeedText, sizeof(cursorSpeedText));
		sakuraFileData.WriteData(optionsSoundTextOffset,       (char*)soundText,       sizeof(soundText));
		sakuraFileData.WriteData(optionsVoiceTextOffset,       (char*)voiceText,       sizeof(voiceText));
		sakuraFileData.WriteData(optionsControlsTextOffset,    (char*)controlsText,    sizeof(controlsText));
		sakuraFileData.WriteData(optionsExitTextOffset,        (char*)exitText,        sizeof(exitText));

		sakuraFileData.WriteData(controlsOkTextOffset,        (char*)controlsOk,				sizeof(controlsOk));
		sakuraFileData.WriteData(controlsCancelTextOffset,    (char*)controlsCancel,			sizeof(controlsCancel));
		sakuraFileData.WriteData(controlsMoveTextOffset,      (char*)controlsMove,				sizeof(controlsMove));
		sakuraFileData.WriteData(controlsAttackTextOffset,    (char*)controlsAttack,			sizeof(controlsAttack));
		sakuraFileData.WriteData(controlsDefendTextOffset,    (char*)controlsDefendText,		sizeof(controlsDefendText));
		sakuraFileData.WriteData(controlsChargeTextOffset,    (char*)controlsChargeText,		sizeof(controlsChargeText));
		sakuraFileData.WriteData(controlsViewAllTextOffset,   (char*)controlsViewAllText,		sizeof(controlsViewAllText));
		sakuraFileData.WriteData(controlsAdviceTextOffset,    (char*)controlsAdviceText,		sizeof(controlsAdviceText));
		sakuraFileData.WriteData(controlsOk2TextOffset,       (char*)controlsOk2Text,			sizeof(controlsOk2Text));
		sakuraFileData.WriteData(controlsNoSettingTextOffset, (char*)controlsNoSetting,			sizeof(controlsNoSetting));
		sakuraFileData.WriteData(controlsDefaultTextOffset,   (char*)controlsDefault,			sizeof(controlsDefault));
		sakuraFileData.WriteData(controlsXOffset,             (char*)controlsXText,				sizeof(controlsXText));
		sakuraFileData.WriteData(controlsXForAcceptOffset,    (char*)controlsXForAcceptText,    sizeof(controlsXForAcceptText));
		sakuraFileData.WriteData(controlsExitOffset,          (char*)controlsExit,				sizeof(controlsExit));
	}
};

bool PatchMainMenu(const string& inPatchedSakuraRootDirectory, const string& inTranslatedMainMenuFontSheet, const string& outMainMenuFontSheetPath, const string& inMainMenuTranslatedBgnd, 
				   const string& inTranslatedDataDirectory, const string& inTempDir)
{
	//LOGO.SH2
	const FileNameContainer logoFileName( (inPatchedSakuraRootDirectory + "SAKURA1\\LOGO.SH2").c_str() );
	FileData logoFileData;
	if( !logoFileData.InitializeFileData(logoFileName) )
	{
		printf("PatchMainMenu: Unable to open LOGO.SH2: %s\n", logoFileName.mFullPath.c_str());
		return false;
	}

	//SAKURA
	const FileNameContainer sakuraFileName( (inPatchedSakuraRootDirectory + "SAKURA").c_str() );
	FileReadWriter sakuraFileData;
	if( !sakuraFileData.OpenFile(sakuraFileName.mFullPath) )
	{
		printf("PatchMainMenu: Unable to open SAKURA: %s\n", logoFileName.mFullPath.c_str());
		return false;
	}

	//Create Sakura format MainMenu font sheet
	if( !CreateTranslatedFontSheet(inTranslatedMainMenuFontSheet, outMainMenuFontSheetPath, false) )
	{
		printf("PatchMainMenu: Unable to create translated main menu font sheet\n");
		return false;
	}

	//Options font sheet
	const string translatedOptionsFontSheetPath = inTranslatedDataDirectory + string("OptionsSettings.bmp");
	string optionsFontSheetSakuraFormatPath = inTempDir + string("OptionsSettingsFontSheet");
	if( !CreateTranslatedFontSheet(translatedOptionsFontSheetPath, optionsFontSheetSakuraFormatPath, false) )
	{
		printf("PatchMainMenu: Unable to create translated options font sheet\n");
		return false;
	}

	//Load MainMenu font sheet
	const string fontSheetPath = outMainMenuFontSheetPath + ".bin";
	const FileNameContainer translatedFileName(fontSheetPath.c_str());
	FileData translatedData;
	if( !translatedData.InitializeFileData(translatedFileName) )
	{
		printf("PatchMainMenu: Unable to open converted main menu font sheet %s\n", fontSheetPath.c_str());
		return false;
	}

	//Load Options font sheet
	const string optionsFontSheetPath = optionsFontSheetSakuraFormatPath + ".bin";
	const FileNameContainer translatedOptionsFileName(optionsFontSheetPath.c_str());
	FileData translatedOptionsData;
	if( !translatedOptionsData.InitializeFileData(translatedOptionsFileName) )
	{
		printf("PatchMainMenu: Unable to open converted options font sheet %s\n", optionsFontSheetPath.c_str());
		return false;
	}

	//Compress font sheet
	int numCharactersInFontSheet = 65;
	PRSCompressor compressor;
	compressor.CompressData((void*)translatedData.GetData(), (numCharactersInFontSheet*16*8), PRSCompressor::kCompressOption_FourByteAlign);//translatedData.GetDataSize() - (numCharactersInFontSheet*16*8));
	
	//Compress options font sheet
	const unsigned long optionsFontSheetOffset        = GIsDisc2 ? 0x0005b458 : 0x0005b320;
	const unsigned long originalOptionsCompressedSize = 3782;
	SakuraCompressedData optionsCompressedData;
	optionsCompressedData.PatchDataInMemory(translatedOptionsData.GetData(), translatedOptionsData.GetDataSize(), true, false, originalOptionsCompressedSize);
	if( optionsCompressedData.mDataSize > originalOptionsCompressedSize )
	{
		printf("PatchMainMenu: Compressed options font sheet is larger than the original %ul instead of %ul \n", optionsCompressedData.mDataSize, originalOptionsCompressedSize);
		return false;
	}
	sakuraFileData.WriteData(optionsFontSheetOffset, optionsCompressedData.mpCompressedData, optionsCompressedData.mDataSize);

	//Pad compressed data is it is divisible by 2
	const unsigned long patchedFontSheetSize = compressor.mCompressedSize + (compressor.mCompressedSize%2);
	char* pCompressedFontSheet               = new char[patchedFontSheetSize];
	memset(pCompressedFontSheet, 0, patchedFontSheetSize);
	memcpy_s(pCompressedFontSheet, patchedFontSheetSize, compressor.mpCompressedData, compressor.mCompressedSize);

	//Compress bgnd image
	BmpToSaturnConverter patchedBgndImage;
	if( !patchedBgndImage.ConvertBmpToSakuraFormat(inMainMenuTranslatedBgnd, true) )
	{
		printf("PatchMainMenu: Couldn't convert image: %s.\n", inMainMenuTranslatedBgnd.c_str());
		return false;
	}

	SakuraCompressedData translatedBgndData;
	translatedBgndData.PatchDataInMemory(patchedBgndImage.mTileExtractor.mTiles[0].mpTile, patchedBgndImage.mTileExtractor.mTiles[0].mTileSize, true);

	//Intermediate Screen
	const string intermediateScreenPath = inTranslatedDataDirectory + "IntermediateScreen.bmp";
	BmpToSaturnConverter patchedIntermediateScreen;
	unsigned int intermediateScreenTileDim = 16;
	if( !patchedIntermediateScreen.ConvertBmpToSakuraFormat(intermediateScreenPath, false, 0, &intermediateScreenTileDim, &intermediateScreenTileDim) )
	{
		printf("PatchMainMenu: Couldn't convert image: %s.\n", inMainMenuTranslatedBgnd.c_str());
		return false;
	}

	patchedIntermediateScreen.PackTiles();

	const unsigned long originalIntermediateSreenCompressedSize = 966;
	SakuraCompressedData translatedIntermediateScreenData;
	translatedIntermediateScreenData.PatchDataInMemory(patchedIntermediateScreen.mpPackedTiles, patchedIntermediateScreen.mPackedTileSize, true, false, originalIntermediateSreenCompressedSize);

	if( translatedIntermediateScreenData.mDataSize > originalIntermediateSreenCompressedSize )
	{
		printf("PatchMainMenu: Compressed size for the IntermediateScreen is too big.  Should be less thatn %ul, is %ul", originalIntermediateSreenCompressedSize, translatedIntermediateScreenData.mDataSize);
		return false;
	}

	const FileNameContainer icatallFileName( (inPatchedSakuraRootDirectory + "SAKURA1\\ICATALL.DAT").c_str() );
	FileReadWriter icatallFileData;
	if( !icatallFileData.OpenFile(icatallFileName.mFullPath) )
	{
		printf("PatchMainMenu: Unable to open ICATALL: %s\n", icatallFileName.mFullPath.c_str());
		return false;
	}
	icatallFileData.WriteData(0x00002800, translatedIntermediateScreenData.mpCompressedData, translatedIntermediateScreenData.mDataSize);

	const unsigned long intermediateTextIndiceOffset = GIsDisc2 ? 0x0005f700 : 0x0005f5c8;
	const char intermediateTextIndices[36] = {0, 1, 0, 2, 0, 13, 0, 13, 0, 13, 0, 0, 
											  0, 3, 0, 4, 0, 5, 0, 13, 0, 13, 0, 0, 
											  0, 6, 0, 7, 0, 8, 0, 9, 0, 10, 0, 11};
	//

	//Create text for the main menu
	const unsigned long mainMenuTextOffset           = 0x000061E4;
	const unsigned long mainMenuOptionTextOffset     = 0x000060D6;
	const unsigned long mainMenuContinueTextOffset   = 0x00006194;
	const unsigned long mainMenuTheaterTextOffset    = 0x00006108;
	MainMenuText newGameText[8];
	MainMenuText continueText[8];
	MainMenuText optionText[5];
	MainMenuText theaterText[7];
	memcpy_s((void*)newGameText,  sizeof(newGameText),  logoFileData.GetData() + mainMenuTextOffset,         sizeof(newGameText));
	memcpy_s((void*)continueText, sizeof(continueText), logoFileData.GetData() + mainMenuContinueTextOffset, sizeof(continueText));
	memcpy_s((void*)optionText,   sizeof(optionText),   logoFileData.GetData() + mainMenuOptionTextOffset,   sizeof(optionText));
	memcpy_s((void*)theaterText,  sizeof(theaterText),  logoFileData.GetData() + mainMenuTheaterTextOffset,  sizeof(theaterText));
	
	//Options menu layout
	OptionsSettingsMenu sakuraOptionsMenu;
	sakuraOptionsMenu.CreateTranslatedMenu(sakuraFileData, false);

	//SAKURA layout code for main menu
	short startX = -1 * (74/2);
	newGameText[0].SetCharacter(0, startX);
	newGameText[1].SetCharacter(1, startX + 16);
	newGameText[2].SetCharacter(2, startX + 16*2);
	newGameText[3].SetCharacter(3, startX + 16*3);
	newGameText[4].SetCharacter(4, startX + 16*4);
	newGameText[5].SetCharacter(25, startX + 16*5);
	newGameText[6].SetCharacter(25, startX + 16*6);
	newGameText[7].SetCharacter(25, startX + 16*7);
	
	startX = -1 * (52/2);
	continueText[0].SetCharacter(5, startX);
	continueText[1].SetCharacter(6, startX + 16);
	continueText[2].SetCharacter(7, startX + 16*2);
	continueText[3].SetCharacter(8, startX + 16*3);
	continueText[4].SetCharacter(25, startX + 16*4);
	continueText[5].SetCharacter(25, startX + 16*5);
	continueText[6].SetCharacter(25, startX + 16*6);
	continueText[7].SetCharacter(25, startX + 16*7);

	startX = -1 * (44/2);
	optionText[0].SetCharacter(9,  startX);
	optionText[1].SetCharacter(10, startX + 16);
	optionText[2].SetCharacter(11, startX + 16*2);
	optionText[3].SetCharacter(12, startX + 16*3);
	optionText[4].SetCharacter(25, startX + 16*4);

	startX = -1 * (68/2);
	theaterText[0].SetCharacter(16, startX);
	theaterText[1].SetCharacter(17, startX + 16);
	theaterText[2].SetCharacter(18, startX + 16*2);
	theaterText[3].SetCharacter(19, startX + 16*3);
	theaterText[4].SetCharacter(20, startX + 16*4);
	theaterText[5].SetCharacter(25, startX + 16*5);
	theaterText[6].SetCharacter(25, startX + 16*6);

	//Create new data
	const unsigned long origCompressedFontSheetSize = 2280;
	const unsigned long origCompressedBgndImgSize   = 3942;//4260;
	const unsigned long origCompressedSize          = origCompressedFontSheetSize + origCompressedBgndImgSize;
	const unsigned long originalDataSize            = logoFileData.GetDataSize() - origCompressedSize;  //Original size of all the non-compressed data in LOGO.SH2
	unsigned long newCompressedSize                 = patchedFontSheetSize + translatedBgndData.mDataSize;
	long compressedDiff                             = newCompressedSize - origCompressedSize;
	if( compressedDiff > 0 )
	{
		printf("PatchMainMenu: Compressed data is too large \n");
		return false;
	}

	const unsigned long newDataSize                 = origCompressedSize + originalDataSize;//newCompressedSize + originalDataSize;
	char* pNewData                                  = new char[newDataSize];
	memset(pNewData, 0, newDataSize);

	//Patch font sheet
	//mainMenuTextOffset                   = 0x000061E8;
	const unsigned long fontSheetOffset    = 0x0000ff20;
	const unsigned long bgndImageOffset    = 0x00010808; //Bgnd image appears directly after the font sheet
	unsigned long       memCpyOffset       = 0;

	//Copy everything from the start of the file to the font sheet
	memcpy_s(pNewData + memCpyOffset, newDataSize - memCpyOffset, logoFileData.GetData(), fontSheetOffset);

	//Copy over new main menu text
	memcpy_s(pNewData + memCpyOffset + mainMenuTextOffset,         newDataSize - memCpyOffset, &newGameText, sizeof(newGameText));
	memcpy_s(pNewData + memCpyOffset + mainMenuContinueTextOffset, newDataSize - memCpyOffset, &continueText, sizeof(continueText));
	memcpy_s(pNewData + memCpyOffset + mainMenuOptionTextOffset,   newDataSize - memCpyOffset, &optionText, sizeof(optionText));
	memcpy_s(pNewData + memCpyOffset + mainMenuTheaterTextOffset,  newDataSize - memCpyOffset, &theaterText, sizeof(theaterText));

	//SAKURA file: write layout code for menus
	sakuraFileData.WriteData(intermediateTextIndiceOffset, intermediateTextIndices, sizeof(intermediateTextIndices));

	//Copy patched font sheet
	memCpyOffset += fontSheetOffset;
	memcpy_s(pNewData + memCpyOffset, newDataSize - memCpyOffset, pCompressedFontSheet, patchedFontSheetSize);

	//Copy patched bgnd image
	if( patchedFontSheetSize > origCompressedFontSheetSize )
	{
		memCpyOffset += patchedFontSheetSize;
	}
	else
	{
		memCpyOffset += origCompressedFontSheetSize; //If the new font sheet size is smaller, pad with 0s
		
		newCompressedSize = origCompressedFontSheetSize + translatedBgndData.mDataSize;
		compressedDiff    = newCompressedSize - origCompressedSize;

		if( compressedDiff > 0 )
		{
			printf("PatchMainMenu: Compressed data is too large \n");
			return false;
		}
	}
	memcpy_s(pNewData + memCpyOffset, newDataSize - memCpyOffset, translatedBgndData.mpCompressedData, translatedBgndData.mDataSize);

	//Copy the rest of the LOGO.SH2 file
	memCpyOffset += (translatedBgndData.mDataSize - compressedDiff); //compressedDiff should be negative
	memcpy_s(pNewData + memCpyOffset, newDataSize - memCpyOffset, logoFileData.GetData() + fontSheetOffset + origCompressedSize, logoFileData.GetDataSize() - (fontSheetOffset + origCompressedSize));

	//Fix bgnd palette
	const unsigned long newPaletteAddress = 0x00029324;// + compressedDiff;
	memcpy_s(pNewData + newPaletteAddress, newDataSize - newPaletteAddress, patchedBgndImage.mPalette.GetData(), patchedBgndImage.mPalette.GetSize());

	//Pointer to the bgnd image data.  This has to be treated seperatly because the fixup happening above is after both the font sheet and the bgnd image
	const unsigned int fontSizeDiff = patchedFontSheetSize > origCompressedFontSheetSize ? patchedFontSheetSize - origCompressedFontSheetSize : 0;
	const unsigned int newAddress = 0x214808 + fontSizeDiff;
	*(unsigned int*)(&pNewData[0x38A0]) = SwapByteOrder(newAddress);
		
	FileWriter outFile;
	if( !outFile.OpenFileForWrite(logoFileName.mFullPath) )
	{
		printf("PatchMainMenu: Unable to open LOGO.SH2 for write\n");
		return false;
	}
	outFile.WriteData(pNewData, newDataSize);

	delete[] pNewData;
	delete[] pCompressedFontSheet;

	return true;
}

void ExtractLoadScreen(const string& inRootSakuraDirectory, const string& inTranslatedDataDirectory, const string& inOutputDirectory)
{
	CreateDirectoryHelper(inOutputDirectory);

	const string sakuraFilePath = inRootSakuraDirectory + "SAKURA";
	FileData sakuraFileData;
	if( !sakuraFileData.InitializeFileData( FileNameContainer(sakuraFilePath.c_str()) ) )
	{
		return;
	}

	{
		//Create font sheet
		SakuraFontSheet sakuraFontSheet;
		PRSDecompressor decompressor;
		decompressor.UncompressData((void*)(sakuraFileData.GetData() + 0x0005c654), (sakuraFileData.GetDataSize() - 0x0005c654));

		if( !sakuraFontSheet.CreateFontSheetFromData(decompressor.mpUncompressedData, decompressor.mUncompressedDataSize) )
		{
			printf("Unable to create font sheet.\n");
			return;
		}

		const string fontFilePath = inTranslatedDataDirectory + "Palette.bin";
		FileData fontFile;
		if( !fontFile.InitializeFileData(FileNameContainer(fontFilePath.c_str()) ) )
		{
			return;
		}

		PaletteData fontPaletteData;
		fontPaletteData.CreateFrom15BitData(fontFile.GetData(), fontFile.GetDataSize());

		MiniGameSakuraText sakuraText;
		sakuraText.ReadInStrings(sakuraFileData.GetData(), 0x0005a09e, 128);
		sakuraText.DumpTextImages(sakuraFontSheet, fontPaletteData, inOutputDirectory);
	}

	printf("LoadScreen Extracted\n");
}

bool FixupSLG(const string& rootDir, const string& outDir, const string& inTranslatedDirectory, const string& newFontPaletteFile, const string& inTempDir)
{
	//We need the palette from the patched stats menu image
	const string translatedWKLDirectory  = inTranslatedDirectory + string("WKL\\");
	const string patchedStatsImagePath   = translatedWKLDirectory + string("StatsMenuPatched.bmp");

	BmpToSaturnConverter statsImage;
	if( !statsImage.ConvertBmpToSakuraFormat(patchedStatsImagePath, true, BmpToSaturnConverter::BLACK) )
	{
		printf("FixupSLG: Unable to convert stats image %s to sakura format.\n", patchedStatsImagePath.c_str());
		return false;
	}
	if( statsImage.mPalette.GetSize() != 32 )
	{
		printf("FixupSLG: Stats image %s is of an invalid size.  It needs to be a 16 color 32 byte palette.\n", patchedStatsImagePath.c_str());
	}

	FileData newPaletteData;
	if( !newPaletteData.InitializeFileData(newFontPaletteFile.c_str(), newFontPaletteFile.c_str()) )
	{
		printf("FixupSLG: Unable to patch palettes because new palette not found.\n");
		return false;
	}
	
	const int paletteSize = 32;
	if( newPaletteData.GetDataSize() != paletteSize )
	{
		printf("FixupSLG: Palette size should be %i.  NewPaletteFile %s has a size of %ul instead.\n", paletteSize, newFontPaletteFile.c_str(), newPaletteData.GetDataSize());
		return false;
	}

	//**Create options settings font sheet
	//Options font sheet
	const string translatedOptionsFontSheetPath = inTranslatedDirectory + string("OptionsSettings.bmp");
	string optionsFontSheetSakuraFormatPath     = inTempDir + string("OptionsSettingsFontSheet");
	if( !CreateTranslatedFontSheet(translatedOptionsFontSheetPath, optionsFontSheetSakuraFormatPath, false) )
	{
		printf("FixupSLG: Unable to create translated options font sheet\n");
		return false;
	}

	//Load Options font sheet
	const string optionsFontSheetPath = optionsFontSheetSakuraFormatPath + ".bin";
	const FileNameContainer translatedOptionsFileName(optionsFontSheetPath.c_str());
	FileData translatedOptionsData;
	if( !translatedOptionsData.InitializeFileData(translatedOptionsFileName) )
	{
		printf("FixupSLG: Unable to open converted options font sheet %s\n", optionsFontSheetPath.c_str());
		return false;
	}

	//Compress options font sheet
	const unsigned long optionsFontSheetOffset        = 0x0005b320;
	const unsigned long originalOptionsCompressedSize = 3782;
	SakuraCompressedData optionsCompressedData;
	optionsCompressedData.PatchDataInMemory(translatedOptionsData.GetData(), translatedOptionsData.GetDataSize(), true, false, originalOptionsCompressedSize);
	if( optionsCompressedData.mDataSize > originalOptionsCompressedSize )
	{
		printf("PatchMainMenu: Compressed options font sheet is larger than the original %ul instead of %ul \n", optionsCompressedData.mDataSize, originalOptionsCompressedSize);
		return false;
	}
	//**Done with settings font sheet

	vector<string> slgFiles;
	slgFiles.push_back("0SLG.BIN");
	slgFiles.push_back("SLG.BIN");
	
	for(const string& slgFileName : slgFiles)
	{
		//Open the original file
		const string filePath = rootDir + string("SAKURA2\\\\") + slgFileName;
		FileData origSlgData;
		if( !origSlgData.InitializeFileData(slgFileName.c_str(), filePath.c_str()) )
		{
			printf("FixupSLG: Unable to open %s file.\n", filePath.c_str());
			return false;
		}

		//***Fix StatsMenu Palette***
	//	const int offsetToStatsMenuPalette = 0x00011b78;
	//	memcpy_s((void*)(origSlgData.GetData() + offsetToStatsMenuPalette), origSlgData.GetDataSize(), statsImage.mPalette.GetData(), statsImage.mPalette.GetSize());
		//***Done fixing StatsMenu palette

		//***Read in original options image***
		//The bgnd image is part of a sprite sheet that has another image in it as well.  So in order to patch it, we need to uncompress the original data to get the original sprite sheet, 
		//then copy over the new bgnd image into it, then compress that data again and save that out.
		const unsigned int bgndImageOriginalCompressedSize = 1255;
		const int ofsetBgndImage                           = 0x0004a258;
		char* pOriginalOptionsImageData                    = new char[bgndImageOriginalCompressedSize];

		memcpy_s(pOriginalOptionsImageData, bgndImageOriginalCompressedSize, (void*)(origSlgData.GetData() + ofsetBgndImage), bgndImageOriginalCompressedSize);
	
		//Uncompress the original data
		PRSDecompressor uncompressedBgndImage;
		uncompressedBgndImage.UncompressData((void*)pOriginalOptionsImageData, bgndImageOriginalCompressedSize);
		delete[] pOriginalOptionsImageData;

		//Convert patched options bgnd image to sakura format
		const string mainMenuTranslatedBgnd = inTranslatedDirectory + "OptionsBgndPatched.bmp";
		BmpToSaturnConverter patchedBgndImage;
		if( !patchedBgndImage.ConvertBmpToSakuraFormat(mainMenuTranslatedBgnd, false) )
		{
			printf("PatchMainMenu: Couldn't convert image: %s.\n", mainMenuTranslatedBgnd.c_str());
			return false;
		}

		//Copy the translated image data into the original sprite sheet
		memcpy_s(uncompressedBgndImage.mpUncompressedData, uncompressedBgndImage.mUncompressedDataSize, patchedBgndImage.GetImageData(), patchedBgndImage.GetImageDataSize());

		//Compress the data
		SakuraCompressedData translatedBgndData;
		translatedBgndData.PatchDataInMemory(uncompressedBgndImage.mpUncompressedData, uncompressedBgndImage.mUncompressedDataSize, true, false, bgndImageOriginalCompressedSize);
		if( translatedBgndData.mDataSize > bgndImageOriginalCompressedSize )
		{
			printf("PatchMainMenu: Translated bgnd image is too big when compressed. Max is %u, this is %u.\n", bgndImageOriginalCompressedSize, translatedBgndData.mDataSize);
			return false;
		}
		//**Done compressing bgnd image
		//***Done reading in original image***

		//***Fix the character spacing***
		const int offsetTileSpacingX  = 0x0002167B; //Used when all text is displayed
		const int offsetTileSpacingY  = 0x0002165D; //Used when all text is displayed
		const int offsetTileSpacingX2 = 0x00021903; //Used while text is scrolling
		const int offsetTileSpacingY2 = 0x000218E1; //Used while text is scrolling

		memcpy_s((void*)(origSlgData.GetData() + offsetTileSpacingX),     origSlgData.GetDataSize(), (void*)&OutTileSpacingX, sizeof(OutTileSpacingX));
		memcpy_s((void*)(origSlgData.GetData() + offsetTileSpacingY),     origSlgData.GetDataSize(), (void*)&OutTileSpacingY, sizeof(OutTileSpacingY));
		memcpy_s((void*)(origSlgData.GetData() + offsetTileSpacingX2),    origSlgData.GetDataSize(), (void*)&OutTileSpacingX, sizeof(OutTileSpacingX));
		memcpy_s((void*)(origSlgData.GetData() + offsetTileSpacingY2),    origSlgData.GetDataSize(), (void*)&OutTileSpacingY, sizeof(OutTileSpacingY));
		//**Done fixing character spacing***

		//***Fix Max Character Lengths***
		const int offsetMaxCharsWhenScrolling  = 0x000218EB;
		const int offsetMaxSpacingScrolling1   = 0x00021963;
		const int offsetMaxSpacingScrolling2   = 0x0002196B;
		const int offsetMaxMultiplierScrolling = 0x00021981;
		const int offsetMaxMultiplier          = 0x00021655;
		const int offsetMaxLines_1             = 0x0002196D;
		const int offsetMaxLines_2             = 0x00021975;
		const int offsetLipsCharWidthOffset    = 0x00021809;
		const int offsetLipsShiftLeft_1        = 0x00021765; //Change the command from SHLL2(4108) to SHLL1(4100)
		const int offsetLipsShiftLeft_2        = 0x00021799; //Change the command from SHLL2(4108) to SHLL1(4100)
		const int offsetLipsMaxCharsOffset_1   = 0x00021755; //Change the command from MOV 0x0F(E20F) to SHLL1(E21E)
		const int offsetLipsMaxCharsOffset_2   = 0x0002178B; //Change the command from MOV 0x0F(E20F) to SHLL1(E21E)
		const int offsetLipsNumCharsPerLine_1  = 0x000217B9;
		const int offsetLipsNumCharsPerLine_2  = 0x000217BF;
		const int offsetLipsStartLocationY     = 0x0002173D; //Change the command from MOV 0xCC(-52), r11 to 0xCE(-50)
		const int offsetItemCountXOffset       = 0x0002512B;
		const int offsetOptionsFontSheet       = 0x0004a740;
	//	const int offsetBgndImagePalette       = 0x0004a1b8;
		const unsigned char maxMultiplier      = (240/(OutTileSpacingX));
		const unsigned char maxCharacters      = maxMultiplier - 1;
		const unsigned char maxLines           = MaxLines - 1;
		const unsigned char lipsXOffset        = 0;
		const unsigned char shiftLeftValue     = 0;
		const unsigned char maxCharsPerLipsLine= maxMultiplier;
		const unsigned char lipsStartLocY      = 0xCE;
		const unsigned char itemCountXOffset   = 0x30; //This is the text that appears next to a battle menu option which has a number associated with it (ex: Cover x8)

		memcpy_s((void*)(origSlgData.GetData() + offsetMaxSpacingScrolling1),     origSlgData.GetDataSize(), (void*)&maxCharacters,							sizeof(maxCharacters));
		memcpy_s((void*)(origSlgData.GetData() + offsetMaxSpacingScrolling2),     origSlgData.GetDataSize(), (void*)&maxCharacters,							sizeof(maxCharacters));
		memcpy_s((void*)(origSlgData.GetData() + offsetMaxMultiplierScrolling),   origSlgData.GetDataSize(), (void*)&maxMultiplier,							sizeof(maxMultiplier));
		memcpy_s((void*)(origSlgData.GetData() + offsetMaxMultiplier),            origSlgData.GetDataSize(), (void*)&maxMultiplier,							sizeof(maxMultiplier));
		memcpy_s((void*)(origSlgData.GetData() + offsetMaxCharsWhenScrolling),    origSlgData.GetDataSize(), (void*)&maxMultiplier,							sizeof(maxMultiplier));
		memcpy_s((void*)(origSlgData.GetData() + offsetMaxLines_1),               origSlgData.GetDataSize(), (void*)&maxLines,								sizeof(maxLines));
		memcpy_s((void*)(origSlgData.GetData() + offsetMaxLines_2),               origSlgData.GetDataSize(), (void*)&maxLines,								sizeof(maxLines));
		memcpy_s((void*)(origSlgData.GetData() + offsetLipsCharWidthOffset),      origSlgData.GetDataSize(), (void*)&OutTileSpacingX,						sizeof(OutTileSpacingX));
		memcpy_s((void*)(origSlgData.GetData() + offsetLipsShiftLeft_1),		  origSlgData.GetDataSize(), (void*)&shiftLeftValue,						sizeof(shiftLeftValue));
		memcpy_s((void*)(origSlgData.GetData() + offsetLipsShiftLeft_2),		  origSlgData.GetDataSize(), (void*)&shiftLeftValue,						sizeof(shiftLeftValue));
		memcpy_s((void*)(origSlgData.GetData() + offsetLipsMaxCharsOffset_1),	  origSlgData.GetDataSize(), (void*)&maxCharsPerLipsLine,					sizeof(maxCharsPerLipsLine));
		memcpy_s((void*)(origSlgData.GetData() + offsetLipsMaxCharsOffset_2),	  origSlgData.GetDataSize(), (void*)&maxCharsPerLipsLine,					sizeof(maxCharsPerLipsLine));
		memcpy_s((void*)(origSlgData.GetData() + offsetLipsNumCharsPerLine_1),	  origSlgData.GetDataSize(), (void*)&maxCharsPerLipsLine,					sizeof(maxCharsPerLipsLine));
		memcpy_s((void*)(origSlgData.GetData() + offsetLipsNumCharsPerLine_2),	  origSlgData.GetDataSize(), (void*)&maxCharsPerLipsLine,					sizeof(maxCharsPerLipsLine));
		memcpy_s((void*)(origSlgData.GetData() + offsetLipsStartLocationY),	      origSlgData.GetDataSize(), (void*)&lipsStartLocY,							sizeof(lipsStartLocY));
		memcpy_s((void*)(origSlgData.GetData() + offsetOptionsFontSheet),		  origSlgData.GetDataSize(), (void*)optionsCompressedData.mpCompressedData, optionsCompressedData.mDataSize);
		memcpy_s((void*)(origSlgData.GetData() + ofsetBgndImage),		          origSlgData.GetDataSize(), (void*)translatedBgndData.mpCompressedData,    translatedBgndData.mDataSize);
	
	#if !FIX_SLG_FONT_DRAWING_SIZE
		memcpy_s((void*)(origSlgData.GetData() + offsetItemCountXOffset), origSlgData.GetDataSize(), (void*)&itemCountXOffset, sizeof(itemCountXOffset));
	#endif
		//memcpy_s((void*)(origSlgData.GetData() + offsetBgndImagePalette),		  origSlgData.GetDataSize(), (void*)patchedBgndImage.mPalette.GetData(),    patchedBgndImage.mPalette.GetSize());
		//***Done Fixing Max Character Lengths***

		//Assembly to change the size of the text tiles from 16x16 to 8x12 - Done by Trekkies
		if( FIX_SLG_FONT_DRAWING_SIZE )
		{
			/*
			basically you're making changes to how the game calculates the VRAM addresses for the sprites
			at 16x16 each letter was 0x80 bytes
			but at 8x12 they're 0x30 bytes
			so those changes will make it calculate things correctly
			*/
			const int offsetCMDSize          = 0x0002158A; //Change the command 0x210 to 0x010C.  This changes the drawing size of the text tiles
			const int offsetToTextResizeCmd1 = 0x000214F4;
			const int offsetToTextResizeCmd2 = offsetToTextResizeCmd1 + sizeof(short);
			const int offsetToTextResizeCmd3 = offsetToTextResizeCmd2 + sizeof(short);
			const int offsetToTextResizeCmd4 = offsetToTextResizeCmd3 + sizeof(short);
			const int offsetToTextResizeCmd5 = offsetToTextResizeCmd4 + sizeof(short);
			const int offsetToTextResizeCmd6 = 0x000219B6;

			const unsigned short tileSizeCmd    = SwapByteOrder<unsigned short>(0x010C); //Changing from 0x0210 to 0x010C.  Original 02 = width of 2 tiles, 0x10 = height of 16 pixels
			const unsigned short textResizeCmd1 = SwapByteOrder<unsigned short>(0x6163);
			const unsigned short textResizeCmd2 = SwapByteOrder<unsigned short>(0xE630);
			const unsigned short textResizeCmd3 = SwapByteOrder<unsigned short>(0x0167);
			const unsigned short textResizeCmd4 = SwapByteOrder<unsigned short>(0x061A);
			const unsigned short textResizeCmd5 = SwapByteOrder<unsigned short>(0x0009);
			const unsigned short textResizeCmd6 = SwapByteOrder<unsigned short>(0x0030);

			memcpy_s((void*)(origSlgData.GetData() + offsetCMDSize),          origSlgData.GetDataSize(), (void*)&tileSizeCmd,    sizeof(tileSizeCmd));
			memcpy_s((void*)(origSlgData.GetData() + offsetToTextResizeCmd1), origSlgData.GetDataSize(), (void*)&textResizeCmd1, sizeof(textResizeCmd1));
			memcpy_s((void*)(origSlgData.GetData() + offsetToTextResizeCmd2), origSlgData.GetDataSize(), (void*)&textResizeCmd2, sizeof(textResizeCmd2));
			memcpy_s((void*)(origSlgData.GetData() + offsetToTextResizeCmd3), origSlgData.GetDataSize(), (void*)&textResizeCmd3, sizeof(textResizeCmd3));
			memcpy_s((void*)(origSlgData.GetData() + offsetToTextResizeCmd4), origSlgData.GetDataSize(), (void*)&textResizeCmd4, sizeof(textResizeCmd4));
			memcpy_s((void*)(origSlgData.GetData() + offsetToTextResizeCmd5), origSlgData.GetDataSize(), (void*)&textResizeCmd5, sizeof(textResizeCmd5));
			memcpy_s((void*)(origSlgData.GetData() + offsetToTextResizeCmd6), origSlgData.GetDataSize(), (void*)&textResizeCmd6, sizeof(textResizeCmd6));
		}

		//***Fix the palette***
		const int paletteAddress1 = 0x000158A4;
		const int paletteAddress2 = 0x000158C4;//0x0004A1D8;
		char* pNewPalette         = new char[paletteSize];

		//Store palette as 15bit color
		for(int i = 0; i < paletteSize; ++i)
		{
			pNewPalette[i] = newPaletteData.GetData()[i] & 0x7f;
		}

		//copy over the palette
		memcpy_s((void*)(origSlgData.GetData() + paletteAddress1), origSlgData.GetDataSize(), pNewPalette, paletteSize);
		memcpy_s((void*)(origSlgData.GetData() + paletteAddress2), origSlgData.GetDataSize(), pNewPalette, paletteSize);
		delete[] pNewPalette;
		//***Done fixing the palette***

		//Output patched file
		const string outFilePath = outDir + string("SAKURA2\\\\") + slgFileName;
		FileWriter patchedFile;
		if( !patchedFile.OpenFileForWrite(outFilePath) )
		{
			printf("Failed patching: Unable to open %s file for write.\n", outFilePath.c_str());
			return false;
		}

		patchedFile.WriteData(origSlgData.GetData(), origSlgData.GetDataSize());
		patchedFile.Close();

		//Options menu layout
		OptionsSettingsMenu slgOptionsMenu;
		FileReadWriter finalSLGFile;
		if( !finalSLGFile.OpenFile(outFilePath) )
		{
			printf("Failed patching: Unable to open %s file for read/write.\n", outFilePath.c_str());
			return false;
		}
		slgOptionsMenu.CreateTranslatedMenu(finalSLGFile, true);
	}

	printf("FixupSLG Succeeded.\n");
	return true;
}

bool CopyOriginalFiles(const string& rootSakuraTaisenDirectory, const string& patchedSakuraTaisenDirectory)
{
#define CopyOriginalFile(fileName)\
	{\
		const string originalSakuraFile = rootSakuraTaisenDirectory + fileName;\
		const string newSakuraFile      = patchedSakuraTaisenDirectory + fileName;\
		if( !CopyFile(originalSakuraFile.c_str(), newSakuraFile.c_str(), FALSE) )\
		{\
			printf("Unable to copy %s to %s\n", originalSakuraFile.c_str(), newSakuraFile.c_str());\
			return false;\
		}\
	}

	
	CopyOriginalFile("SAKURA");
	CopyOriginalFile("SAKURA1\\LOGO.SH2");
	CopyOriginalFile("SAKURA1\\ICATALL.DAT");
	CopyOriginalFile("SAKURA3\\HANAMAIN.BIN");
	CopyOriginalFile("SAKURA1\\TMapSP.BIN");
	CopyOriginalFile("SAKURA1\\FUKAGAWA.BIN");
	CopyOriginalFile("SAKURA1\\AI_CG.BIN");
	CopyOriginalFile("SAKURA1\\KA_CG.BIN");
	CopyOriginalFile("SAKURA1\\MA_CG.BIN");
	CopyOriginalFile("SAKURA1\\SA_CG.BIN");
	CopyOriginalFile("SAKURA1\\SU_CG.BIN");
	CopyOriginalFile("SAKURA2\\SLG.BIN");
	CopyOriginalFile("SAKURA2\\0SLG.BIN");

	std::string Sakura2("SAKURA2\\");
	std::vector<FileNameContainer> allFiles;
	std::vector<FileNameContainer> evtFiles;
	FindAllFilesWithinDirectory(rootSakuraTaisenDirectory, allFiles);
	GetAllFilesOfType(allFiles, "EVT", evtFiles);
	for(const FileNameContainer& evtFile : evtFiles)
	{
		std::string evtFileName = Sakura2 + evtFile.mFileName;
		
		CopyOriginalFile(evtFileName);
	}

	return true;
}

void FindDiff()
{
	FileData file1;
	FileData file2;
	FileData file3;

	if( !file1.InitializeFileData("BattleDialog_1.bin", "D:\\Rizwan\\SakuraWars\\MemoryDumps\\BattleDialog_1.bin") )
	{
		return;
	}

	if( !file2.InitializeFileData("BattleDialog_2.bin", "D:\\Rizwan\\SakuraWars\\MemoryDumps\\BattleDialog_2.bin") )
	{
		return;
	}

	if( !file3.InitializeFileData("BattleDialog_3.bin", "D:\\Rizwan\\SakuraWars\\MemoryDumps\\BattleDialog_3.bin") )
	{
		return;
	}

	if( file1.GetDataSize() != file2.GetDataSize() )
	{
		return;
	}

	for(unsigned int i = 0; i < file1.GetDataSize(); ++i)
	{
		if( file1.GetData()[i] == 0x05 && file2.GetData()[i] == 0x0B && file3.GetData()[i] == 0x11 )
		{
			printf("Diff Found at 0x%08x\n", i);
		}
	}
}

void CopySharedWklImages(const string& inSourceDirectory, const string& inWklOutputDirectory)
{
	map<unsigned long, bool> wklCrcs;
	
	vector<string> wklDirectories;
	FindAllDirectoriesWithinDirectory(inSourceDirectory, wklDirectories);
	for(const string& wklDir : wklDirectories)
	{
		printf("Processing %s\n", wklDir.c_str());

		//Find all files within this wkl directory
		const string sourceDir = inSourceDirectory + string("\\") + wklDir;
		vector<FileNameContainer> wklSourceFiles;
		FindAllFilesWithinDirectory(sourceDir, wklSourceFiles);

		//Create output directory
		const string outWklDir = inWklOutputDirectory + string("\\") + wklDir;
		CreateDirectoryHelper(outWklDir);

		//Copy over unique files
		for(const FileNameContainer& sourceFileName : wklSourceFiles)
		{
			FileData wklData;
			if( !wklData.InitializeFileData(sourceFileName) )
			{
				printf("Unable to open file %s\n", sourceFileName.mFullPath.c_str());
				return;
			}

			const unsigned long wklCrc = wklData.GetCRC();
			if( wklCrcs.find(wklCrc) == wklCrcs.end() )
			{
				wklCrcs[wklCrc] = true;

				const string outFileName = outWklDir + string("\\") + sourceFileName.mFileName;
				CopyFile(sourceFileName.mFullPath.c_str(), outFileName.c_str(), true);
			}
		}
	}
}

void SwapEndiannessForArrayOfShorts(short* pArray, const int sizeInBytes)
{
	const int numEntries = sizeInBytes / 2;

	for(int i = 0; i < numEntries; ++i)
	{
		SwapByteOrderInPlace( (char*)&pArray[i], sizeof(short) );
	}
}

struct MiniGameTextIndiceFinder
{
	vector<unsigned int> indiceAddresses;
	vector<unsigned int> numCharsPerLine;
	vector< vector<short> > indices;

	void FindIndices(const char* pFileData, unsigned int startAddress, int numCharsInFontSheet)
	{	
		unsigned int currAddress = startAddress;
		bool bNextValueIsAddressStart = true;
		int numCharsInLine = 1;
		vector<short> currIndices;
		while(1)
		{
			const char* pCurrPointer = pFileData + currAddress;
			unsigned short readValue = SwapByteOrder(*((short*)pCurrPointer));
			if( readValue == 0 && bNextValueIsAddressStart )
			{
				break;
			}

			if( readValue > numCharsInFontSheet && readValue != 0x0a0d )
			{
				break;
			}

			if( bNextValueIsAddressStart )
			{
				indiceAddresses.push_back(currAddress);
				bNextValueIsAddressStart = false;
			}
			
			if( readValue == 0 )
			{
				bNextValueIsAddressStart = true;
				numCharsPerLine.push_back(numCharsInLine);

				indices.push_back(currIndices);
				currIndices.clear();
			}
			else
			{
				currIndices.push_back(readValue);
				++numCharsInLine;
			}

			currAddress += sizeof(short);
		}
	}

	void DumpIndices(char* pOutFileName)
	{
		TextFileWriter outFile;
		outFile.OpenFileForWrite(pOutFileName);

		for(size_t i = 0; i < indices.size(); ++i)
		{
			for(size_t k = 0; k < indices[i].size(); ++k)
			{
				fprintf(outFile.GetFileHandle(), "%hi, ", indices[i][k] );
			}
			outFile.AddNewLine();
		}
	}

	bool ValidateNewIndices(int index, unsigned int inDataSize)
	{
		const size_t numIndices = inDataSize / sizeof(short);

		if( (size_t)index >= numCharsPerLine.size() ||
			numIndices > numCharsPerLine[index] )
		{
			printf("Invalid minigame index: %i", index);
			return false;
		}

		return true;
	}
};

bool PatchMiniCook(const string& patchedSakuraDirectory, const string& inTranslatedDataDirectory, const string& originalSakuraDirectory)
{
	const string patchedMinigameFileName  = patchedSakuraDirectory + string("\\SAKURA3\\MINICOOK.BIN");
	const string originalMinigameFileName = originalSakuraDirectory + string("\\SAKURA3\\MINICOOK.BIN");

	FileReadWriter miniGameFile;
	if (!miniGameFile.OpenFile(patchedMinigameFileName))
	{
		return false;
	}

	FileNameContainer originalMinigameFileNameContainer(originalMinigameFileName.c_str());
	FileData miniGameOriginalFile;
	if( !miniGameOriginalFile.InitializeFileData(originalMinigameFileNameContainer) )
	{
		return false;
	}

	//Open translated logo
	const string logoFileName = inTranslatedDataDirectory + "MiniCookLogo.bmp";
	BmpToSaturnConverter patchedLogo;
	if( !patchedLogo.ConvertBmpToSakuraFormat(logoFileName, false) )
	{
		printf("PatchMiniCook: Couldn't convert image: %s.\n", logoFileName.c_str());
		return false;
	}
	if( patchedLogo.mPalette.GetNumColors() > 16 )
	{
		printf("PatchMiniCook: Patched logo has more than 16 colors\n");
		return false;
	}

	//Patch logo
	miniGameFile.WriteData(0x00093568 + (42*16*8), patchedLogo.GetImageData(), patchedLogo.GetImageDataSize());

#if !USE_TREKKIS_MINIGAME_DATA
	//Open translated fontsheet
	const string fontSheetFileName = inTranslatedDataDirectory + "MiniCookFontSheet.bmp";
	BmpToSaturnConverter patchedFontSheet;
	const unsigned int tileDim = 16;
	if( !patchedFontSheet.ConvertBmpToSakuraFormat(fontSheetFileName, false, 0, &tileDim, &tileDim) )
	{
		printf("PatchMiniCook: Couldn't convert image: %s.\n", fontSheetFileName.c_str());
		return false;
	}

	patchedFontSheet.PackTiles();

	//Patch fontsheet
	miniGameFile.WriteData(0x00093568, patchedFontSheet.mpPackedTiles, patchedFontSheet.mPackedTileSize);

	MiniGameTextIndiceFinder indiceFinder;
	indiceFinder.FindIndices(miniGameOriginalFile.GetData(), 0x000aece4, 42);
	if( indiceFinder.indiceAddresses.size() != 3 )
	{
		printf("PatchMiniCook: Couldn't find text indices\n");
		return false;
	}

	//Patch text lookups
	short textIndices1[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0x0a0d, 12, 13, 14, 15, 16, 17, 18, 19, 0x0a0d, 21, 22, 23, 24, 25, 0 };
	short textIndices2[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0x0a0d, 12, 13, 14, 15, 16, 17, 18, 19, 0x0a0d, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 0 };
	short textIndices3[] = { 0x0a0d, 37, 38, 0x0a0d, 39, 40, 0x0a0d, 41, 42, 0 };

	SwapEndiannessForArrayOfShorts(textIndices1, sizeof(textIndices1));
	SwapEndiannessForArrayOfShorts(textIndices2, sizeof(textIndices2));
	SwapEndiannessForArrayOfShorts(textIndices3, sizeof(textIndices3));
	
	if( !indiceFinder.ValidateNewIndices(0, sizeof(textIndices1)) ) return false;
	if( !indiceFinder.ValidateNewIndices(1, sizeof(textIndices2)) ) return false;
	if( !indiceFinder.ValidateNewIndices(2, sizeof(textIndices3)) ) return false;
	
	miniGameFile.WriteData(indiceFinder.indiceAddresses[0], (char*)textIndices1, sizeof(textIndices1));
	miniGameFile.WriteData(indiceFinder.indiceAddresses[1], (char*)textIndices2, sizeof(textIndices2));
	miniGameFile.WriteData(indiceFinder.indiceAddresses[2], (char*)textIndices3, sizeof(textIndices3));
#endif

	return true;
}

bool PatchMiniMaig(const string& patchedSakuraDirectory, const string& inTranslatedDataDirectory, const string& originalSakuraDirectory)
{
	const string patchedMinigameFileName  = patchedSakuraDirectory + string("\\SAKURA3\\MINIMAIG.BIN");
	const string originalMinigameFileName = originalSakuraDirectory + string("\\SAKURA3\\MINIMAIG.BIN");

	FileReadWriter miniGameFile;
	if (!miniGameFile.OpenFile(patchedMinigameFileName))
	{
		return false;
	}

	FileNameContainer originalMinigameFileNameContainer(originalMinigameFileName.c_str());
	FileData miniGameOriginalFile;
	if( !miniGameOriginalFile.InitializeFileData(originalMinigameFileNameContainer) )
	{
		return false;
	}

	//Open translated logo
	const string logoFileName = inTranslatedDataDirectory + "MiniMaigLogo.bmp";
	BmpToSaturnConverter patchedLogo;
	if( !patchedLogo.ConvertBmpToSakuraFormat(logoFileName, false) )
	{
		printf("PatchMiniMaig: Couldn't convert image: %s.\n", logoFileName.c_str());
		return false;
	}
	if( patchedLogo.mPalette.GetNumColors() > 16 )
	{
		printf("PatchMiniMaig: Patched logo has more than 16 colors\n");
		return false;
	}

	//Patch logo
	miniGameFile.WriteData(0x000504B4 + (43*16*8), patchedLogo.GetImageData(), patchedLogo.GetImageDataSize());

#if !USE_TREKKIS_MINIGAME_DATA
	//Open translated fontsheet
	const string fontSheetFileName = inTranslatedDataDirectory + "MiniMaigFontSheet.bmp";
	BmpToSaturnConverter patchedFontSheet;
	const unsigned int tileDim = 16;
	if( !patchedFontSheet.ConvertBmpToSakuraFormat(fontSheetFileName, false, 0, &tileDim, &tileDim) )
	{
		printf("PatchMiniMaig: Couldn't convert image: %s.\n", fontSheetFileName.c_str());
		return false;
	}

	patchedFontSheet.PackTiles();

	//Patch fontsheet
	miniGameFile.WriteData(0x000504B4, patchedFontSheet.mpPackedTiles, patchedFontSheet.mPackedTileSize);

	MiniGameTextIndiceFinder indiceFinder;
	indiceFinder.FindIndices(miniGameOriginalFile.GetData(), 0x00061e90, 43);
	if( indiceFinder.indiceAddresses.size() != 3 )
	{
		printf("PatchMiniMaig: Couldn't find text indices\n");
		return false;
	}

	//Patch text lookups
	short textIndices1[] = { 1, 2, 3, 4, 5, 6, 7, 8, 0x0a0d, 10, 11, 12, 13, 14, 15, 0x0a0d, 16, 17, 18, 19, 20, 21, 22, 23, 0 };
	short textIndices2[] = { 1, 2, 3, 4, 5, 6, 7, 8, 0x0a0d, 10, 11, 12, 13, 14, 15, 0x0a0d, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 0 };
	short textIndices3[] = { 37, 38, 39, 40, 0x0a0d, 41, 42, 0x0a0d, 41, 42, 0x0a0d, 41, 42, 0};

	SwapEndiannessForArrayOfShorts(textIndices1, sizeof(textIndices1));
	SwapEndiannessForArrayOfShorts(textIndices2, sizeof(textIndices2));
	SwapEndiannessForArrayOfShorts(textIndices3, sizeof(textIndices3));

	if( !indiceFinder.ValidateNewIndices(0, sizeof(textIndices1)) ) return false;
	if( !indiceFinder.ValidateNewIndices(1, sizeof(textIndices2)) ) return false;
	if( !indiceFinder.ValidateNewIndices(2, sizeof(textIndices3)) ) return false;

	miniGameFile.WriteData(indiceFinder.indiceAddresses[0], (char*)textIndices1, sizeof(textIndices1));
	miniGameFile.WriteData(indiceFinder.indiceAddresses[1], (char*)textIndices2, sizeof(textIndices2));
	miniGameFile.WriteData(indiceFinder.indiceAddresses[2], (char*)textIndices3, sizeof(textIndices3));
#endif

	return true;
}

bool PatchMiniShot(const string& patchedSakuraDirectory, const string& inTranslatedDataDirectory, const string& originalSakuraDirectory)
{
	const string patchedMinigameFileName = patchedSakuraDirectory + string("\\SAKURA3\\MINISHOT.BIN");
	const string originalMinigameFileName = originalSakuraDirectory + string("\\SAKURA3\\MINISHOT.BIN");

	FileReadWriter miniGameFile;
	if (!miniGameFile.OpenFile(patchedMinigameFileName))
	{
		return false;
	}

	FileNameContainer originalMinigameFileNameContainer(originalMinigameFileName.c_str());
	FileData miniGameOriginalFile;
	if (!miniGameOriginalFile.InitializeFileData(originalMinigameFileNameContainer))
	{
		return false;
	}

	//Open translated logo
	const string logoFileName = inTranslatedDataDirectory + "MiniShotLogo.bmp";
	BmpToSaturnConverter patchedLogo;
	if (!patchedLogo.ConvertBmpToSakuraFormat(logoFileName, false))
	{
		printf("PatchMiniShot: Couldn't convert image: %s.\n", logoFileName.c_str());
		return false;
	}
	if( patchedLogo.mPalette.GetNumColors() > 16 )
	{
		printf("PatchMiniShot: Patched logo has more than 16 colors\n");
		return false;
	}

	//Patch logo
	miniGameFile.WriteData(0x00055510 + (43 * 16 * 8), patchedLogo.GetImageData(), patchedLogo.GetImageDataSize());

#if !USE_TREKKIS_MINIGAME_DATA
	//Open translated fontsheet
	const string fontSheetFileName = inTranslatedDataDirectory + "MiniShotFontSheet.bmp";
	BmpToSaturnConverter patchedFontSheet;
	const unsigned int tileDim = 16;
	if (!patchedFontSheet.ConvertBmpToSakuraFormat(fontSheetFileName, false, 0, &tileDim, &tileDim))
	{
		printf("PatchMiniShot: Couldn't convert image: %s.\n", fontSheetFileName.c_str());
		return false;
	}

	patchedFontSheet.PackTiles();

	//Patch fontsheet
	miniGameFile.WriteData(0x00055510, patchedFontSheet.mpPackedTiles, patchedFontSheet.mPackedTileSize);

	MiniGameTextIndiceFinder indiceFinder;
	indiceFinder.FindIndices(miniGameOriginalFile.GetData(), 0x00080D94, 43);
	if (indiceFinder.indiceAddresses.size() != 3)
	{
		printf("PatchMiniShot: Couldn't find text indices\n");
		return false;
	}

	//Patch text lookups
	short textIndices1[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0x0a0d, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0x0a0d, 20, 21, 22, 23, 24, 25, 26, 27, 0 };
	short textIndices2[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0x0a0d, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0x0a0d, 29, 30, 31, 32, 33, 34, 35, 0 };
	short textIndices3[] = { 36, 37, 0x0a0d, 38, 39, 0x0a0d, 38, 39, 0x0a0d, 38, 39, 0 };

	SwapEndiannessForArrayOfShorts(textIndices1, sizeof(textIndices1));
	SwapEndiannessForArrayOfShorts(textIndices2, sizeof(textIndices2));
	SwapEndiannessForArrayOfShorts(textIndices3, sizeof(textIndices3));

	if (!indiceFinder.ValidateNewIndices(0, sizeof(textIndices1))) return false;
	if (!indiceFinder.ValidateNewIndices(1, sizeof(textIndices2))) return false;
	if (!indiceFinder.ValidateNewIndices(2, sizeof(textIndices3))) return false;

	miniGameFile.WriteData(indiceFinder.indiceAddresses[0], (char*)textIndices1, sizeof(textIndices1));
	miniGameFile.WriteData(indiceFinder.indiceAddresses[1], (char*)textIndices2, sizeof(textIndices2));
	miniGameFile.WriteData(indiceFinder.indiceAddresses[2], (char*)textIndices3, sizeof(textIndices3));
#endif

	return true;
}

bool PatchMiniSlot(const string& patchedSakuraDirectory, const string& inTranslatedDataDirectory, const string& originalSakuraDirectory)
{
	const string patchedMinigameFileName = patchedSakuraDirectory + string("\\SAKURA3\\MINISLOT.BIN");
	const string originalMinigameFileName = originalSakuraDirectory + string("\\SAKURA3\\MINISLOT.BIN");

	FileReadWriter miniGameFile;
	if (!miniGameFile.OpenFile(patchedMinigameFileName))
	{
		return false;
	}

	FileNameContainer originalMinigameFileNameContainer(originalMinigameFileName.c_str());
	FileData miniGameOriginalFile;
	if (!miniGameOriginalFile.InitializeFileData(originalMinigameFileNameContainer))
	{
		return false;
	}

	//Open translated logo
	const string logoFileName = inTranslatedDataDirectory + "MiniSlotLogo.bmp";
	BmpToSaturnConverter patchedLogo;
	if (!patchedLogo.ConvertBmpToSakuraFormat(logoFileName, false))
	{
		printf("PatchMiniSlot: Couldn't convert image: %s.\n", logoFileName.c_str());
		return false;
	}
	if( patchedLogo.mPalette.GetNumColors() > 16 )
	{
		printf("PatchMiniSlot: Patched logo has more than 16 colors\n");
		return false;
	}

	//Patch logo
	miniGameFile.WriteData(0x0006149C + (37 * 16 * 8), patchedLogo.GetImageData(), patchedLogo.GetImageDataSize());

#if !USE_TREKKIS_MINIGAME_DATA
	//Open translated fontsheet
	const string fontSheetFileName = inTranslatedDataDirectory + "MiniSlotFontSheet.bmp";
	BmpToSaturnConverter patchedFontSheet;
	const unsigned int tileDim = 16;
	if (!patchedFontSheet.ConvertBmpToSakuraFormat(fontSheetFileName, false, 0, &tileDim, &tileDim))
	{
		printf("PatchMiniSlot: Couldn't convert image: %s.\n", fontSheetFileName.c_str());
		return false;
	}

	patchedFontSheet.PackTiles();

	//Patch fontsheet
	miniGameFile.WriteData(0x0006149C, patchedFontSheet.mpPackedTiles, patchedFontSheet.mPackedTileSize);

	MiniGameTextIndiceFinder indiceFinder;
	indiceFinder.FindIndices(miniGameOriginalFile.GetData(), 0x0007e650, 37);
	if (indiceFinder.indiceAddresses.size() != 3)
	{
		printf("PatchMiniSlot: Couldn't find text indices\n");
		return false;
	}

	//Patch text lookups
	short textIndices1[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0x0a0d, 17, 18, 19, 20, 21, 22, 23, 24, 0 };
	short textIndices2[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0x0a0d, 13, 14, 15, 16, 21, 22, 23, 24, 0 };
	short textIndices3[] = { 25, 26, 0x0a0d, 27, 28, 0x0a0d, 29, 30, 0x0a0d, 31, 32, 0 };

	SwapEndiannessForArrayOfShorts(textIndices1, sizeof(textIndices1));
	SwapEndiannessForArrayOfShorts(textIndices2, sizeof(textIndices2));
	SwapEndiannessForArrayOfShorts(textIndices3, sizeof(textIndices3));

	if (!indiceFinder.ValidateNewIndices(0, sizeof(textIndices1))) return false;
	if (!indiceFinder.ValidateNewIndices(1, sizeof(textIndices2))) return false;
	if (!indiceFinder.ValidateNewIndices(2, sizeof(textIndices3))) return false;

	miniGameFile.WriteData(indiceFinder.indiceAddresses[0], (char*)textIndices1, sizeof(textIndices1));
	miniGameFile.WriteData(indiceFinder.indiceAddresses[1], (char*)textIndices2, sizeof(textIndices2));
	miniGameFile.WriteData(indiceFinder.indiceAddresses[2], (char*)textIndices3, sizeof(textIndices3));
#endif

	return true;
}

bool PatchMiniSoji(const string& patchedSakuraDirectory, const string& inTranslatedDataDirectory, const string& originalSakuraDirectory)
{
	const string patchedMinigameFileName = patchedSakuraDirectory + string("\\SAKURA3\\MINISOJI.BIN");
	const string originalMinigameFileName = originalSakuraDirectory + string("\\SAKURA3\\MINISOJI.BIN");

	FileReadWriter miniGameFile;
	if (!miniGameFile.OpenFile(patchedMinigameFileName))
	{
		return false;
	}

	FileNameContainer originalMinigameFileNameContainer(originalMinigameFileName.c_str());
	FileData miniGameOriginalFile;
	if (!miniGameOriginalFile.InitializeFileData(originalMinigameFileNameContainer))
	{
		return false;
	}

	//Open translated logo
	const string logoFileName = inTranslatedDataDirectory + "MiniSojiLogo.bmp";
	BmpToSaturnConverter patchedLogo;
	if (!patchedLogo.ConvertBmpToSakuraFormat(logoFileName, false))
	{
		printf("PatchMiniSoji: Couldn't convert image: %s.\n", logoFileName.c_str());
		return false;
	}
	if( patchedLogo.mPalette.GetNumColors() > 16 )
	{
		printf("PatchMiniSoji: Patched logo has more than 16 colors\n");
		return false;
	}

	//Patch logo
	miniGameFile.WriteData(0x00064810 + (35 * 16 * 8), patchedLogo.GetImageData(), patchedLogo.GetImageDataSize());

#if !USE_TREKKIS_MINIGAME_DATA
	//Open translated fontsheet
	const string fontSheetFileName = inTranslatedDataDirectory + "MiniSojiFontSheet.bmp";
	BmpToSaturnConverter patchedFontSheet;
	const unsigned int tileDim = 16;
	if (!patchedFontSheet.ConvertBmpToSakuraFormat(fontSheetFileName, false, 0, &tileDim, &tileDim))
	{
		printf("PatchMiniSoji: Couldn't convert image: %s.\n", fontSheetFileName.c_str());
		return false;
	}

	patchedFontSheet.PackTiles();

	//Patch fontsheet
	miniGameFile.WriteData(0x00064810, patchedFontSheet.mpPackedTiles, patchedFontSheet.mPackedTileSize);

	MiniGameTextIndiceFinder indiceFinder;
	indiceFinder.FindIndices(miniGameOriginalFile.GetData(), 0x00074cbc, 35);
	if (indiceFinder.indiceAddresses.size() != 3)
	{
		printf("PatchMiniSoji: Couldn't find text indices\n");
		return false;
	}

	//Patch text lookups
	short textIndices1[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0x0a0d, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 0 };
	short textIndices2[] = { 25, 26, 27, 28, 0x0a0d, 0x0a0d, 0x0a0d, 0 };
	short textIndices3[] = { 0x0a0d, 30, 31, 32, 0x0a0d, 30, 31, 34, 0 };

	SwapEndiannessForArrayOfShorts(textIndices1, sizeof(textIndices1));
	SwapEndiannessForArrayOfShorts(textIndices2, sizeof(textIndices2));
	SwapEndiannessForArrayOfShorts(textIndices3, sizeof(textIndices3));

	if (!indiceFinder.ValidateNewIndices(0, sizeof(textIndices1))) return false;
	if (!indiceFinder.ValidateNewIndices(1, sizeof(textIndices2))) return false;
	if (!indiceFinder.ValidateNewIndices(2, sizeof(textIndices3))) return false;

	miniGameFile.WriteData(indiceFinder.indiceAddresses[0], (char*)textIndices1, sizeof(textIndices1));
	miniGameFile.WriteData(indiceFinder.indiceAddresses[1], (char*)textIndices2, sizeof(textIndices2));
	miniGameFile.WriteData(indiceFinder.indiceAddresses[2], (char*)textIndices3, sizeof(textIndices3));
#endif

	return true;
}

bool PatchMiniHana(const string& patchedSakuraDirectory, const string& inTranslatedDataDirectory, const string& originalSakuraDirectory)
{
	//Compressed FontSheet:
	//Found: 000a8178 00000000 Size: 384 CompressedSize: 12625

	const string patchedMinigameFileName  = patchedSakuraDirectory + string("\\SAKURA3\\MINIHANA.BIN");
	const string originalMinigameFileName = originalSakuraDirectory + string("\\SAKURA3\\MINIHANA.BIN");
	const string patchedHanaMainFileName  = patchedSakuraDirectory + string("\\SAKURA3\\HANAMAIN.BIN");

	FileReadWriter miniGameFile;
	if (!miniGameFile.OpenFile(patchedMinigameFileName))
	{
		return false;
	}

	FileNameContainer originalMinigameFileNameContainer(originalMinigameFileName.c_str());
	FileData miniGameOriginalFile;
	if( !miniGameOriginalFile.InitializeFileData(originalMinigameFileNameContainer) )
	{
		return false;
	}

	FileReadWriter hanaMainFile;
	if( !hanaMainFile.OpenFile(patchedHanaMainFileName) )
	{
		return false;
	}

	//Open translated Points image
	//Found a match in HANAMAIN.BIN @0x000261e8
	//Found a match in MINIHANA.BIN @0x00044e30
	const string pointsImageFileName = inTranslatedDataDirectory + "MiniGamePoints.bmp";
	BmpToSaturnConverter pointsImage;
	if( !pointsImage.ConvertBmpToSakuraFormat(pointsImageFileName, false) )
	{
		printf("PatchMiniHana: Couldn't convert image: %s.\n", pointsImageFileName.c_str());
		return false;
	}
	miniGameFile.WriteData(0x00044e30, pointsImage.GetImageData(), pointsImage.GetImageDataSize());
	hanaMainFile.WriteData(0x000261e8, pointsImage.GetImageData(), pointsImage.GetImageDataSize());

	//Open translated Dealer image
	//Found a match in HANAMAIN.BIN @0x000267e8
	//Found a match in MINIHANA.BIN @0x00045430
	const string dealerImageFileName = inTranslatedDataDirectory + "MiniGameDealer.bmp";
	BmpToSaturnConverter dealerImage;
	if( !dealerImage.ConvertBmpToSakuraFormat(dealerImageFileName, false) )
	{
		printf("PatchMiniHana: Couldn't convert image: %s.\n", dealerImageFileName.c_str());
		return false;
	}
	miniGameFile.WriteData(0x00045430, dealerImage.GetImageData(), dealerImage.GetImageDataSize());
	hanaMainFile.WriteData(0x000267e8, dealerImage.GetImageData(), dealerImage.GetImageDataSize());

	//Open translated Round image
	//Found a match in HANAMAIN.BIN @0x00026568
	//Found a match in MINIHANA.BIN @0x000451b0
	const string roundImageFileName = inTranslatedDataDirectory + "MiniGameRound.bmp";
	BmpToSaturnConverter roundsImage;
	if( !roundsImage.ConvertBmpToSakuraFormat(roundImageFileName, false) )
	{
		printf("PatchMiniHana: Couldn't convert image: %s.\n", roundImageFileName.c_str());
		return false;
	}
	miniGameFile.WriteData(0x000451b0, roundsImage.GetImageData(), roundsImage.GetImageDataSize());
	hanaMainFile.WriteData(0x00026568, roundsImage.GetImageData(), roundsImage.GetImageDataSize());

	//Open translated logo
	const string logoFileName = inTranslatedDataDirectory + "MiniHanaLogo.bmp";
	BmpToSaturnConverter patchedLogo;
	if( !patchedLogo.ConvertBmpToSakuraFormat(logoFileName, false) )
	{
		printf("PatchMiniHana: Couldn't convert image: %s.\n", logoFileName.c_str());
		return false;
	}
	if( patchedLogo.mPalette.GetNumColors() > 16 )
	{
		printf("PatchMiniHana: Patched logo has more than 16 colors\n");
		return false;
	}

	//Patch logo
	miniGameFile.WriteData(0x00095990, patchedLogo.GetImageData(), patchedLogo.GetImageDataSize());

#if !USE_TREKKIS_MINIGAME_DATA
	//Open translated fontsheet
	{
		const string fontSheetFileName = inTranslatedDataDirectory + "MiniHanaFontSheet.bmp";
		BmpToSaturnConverter patchedFontSheet;
		const unsigned int tileDim = 16;
		if( !patchedFontSheet.ConvertBmpToSakuraFormat(fontSheetFileName, false, 0, &tileDim, &tileDim) )
		{
			printf("PatchMiniHana: Couldn't convert image: %s.\n", logoFileName.c_str());
			return false;
		}

		patchedFontSheet.PackTiles();

		//Patch fontsheet
		//miniGameDumper.Dump("MINIHANA", 111, 0x00092210, 0x000a6b84);
		miniGameFile.WriteData(0x00092210, patchedFontSheet.mpPackedTiles, patchedFontSheet.mPackedTileSize);
	}

	//Text
	{
		MiniGameTextIndiceFinder indiceFinder;
		indiceFinder.FindIndices(miniGameOriginalFile.GetData(), 0x000a6b84, 111);
		if( indiceFinder.indiceAddresses.size() != 16 )
		{
			printf("PatchMiniHana: Couldn't find text indices\n");
			return false;
		}

		//Patch text lookups
		short textIndices1[] = { 1, 2, 3, 4, 5, 6, 0x0a0d, 7, 8, 9, 0x0a0d, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 0 };
		short textIndices2[] = { 23, 24, 25, 26, 27, 28, 29, 39, 31, 32, 33, 0x0a0d, 34, 35, 36, 37, 38, 39, 40, 41, 0 };
		short textIndices3[] = { 42, 43, 44, 45, 0x0a0d, 46, 0x0a0d, 0x0a0d, 46, 0 };
		short textIndices4[] = { 47, 48, 49, 0x0a0d, 50, 51, 52, 0x0a0d, 53, 54, 55, 56, 0x0a0d, 57, 58, 59, 0 };
		short textIndices5[] = { 60, 61, 62, 63, 64, 65, 66, 0 };
		short textIndices6[] = { 67, 68, 69, 70, 71, 72, 73, 0 };
		short textIndices7[] = { 74, 75, 76, 77, 78, 79, 80, 0 };
		short textIndices8[] = { 81, 82, 83, 70, 71, 72, 73, 0 };
		short textIndices9[] = { 84, 85, 86, 87, 0 };
		short textIndices10[] = { 88, 89, 90, 91, 92, 0 };
		short textIndices11[] = { 93, 94, 95, 0 };
		short textIndices12[] = { 96, 97, 98, 99, 100, 101, 102, 0 };
		short textIndices13[] = { 103, 104, 0 };
		short textIndices14[] = { 105, 106, 0 };
		short textIndices15[] = { 107, 108, 0 };
		short textIndices16[] = { 109, 110, 111, 0 };

		SwapEndiannessForArrayOfShorts(textIndices1, sizeof(textIndices1));
		SwapEndiannessForArrayOfShorts(textIndices2, sizeof(textIndices2));
		SwapEndiannessForArrayOfShorts(textIndices3, sizeof(textIndices3));
		SwapEndiannessForArrayOfShorts(textIndices4, sizeof(textIndices4));
		SwapEndiannessForArrayOfShorts(textIndices5, sizeof(textIndices5));
		SwapEndiannessForArrayOfShorts(textIndices6, sizeof(textIndices6));
		SwapEndiannessForArrayOfShorts(textIndices7, sizeof(textIndices7));
		SwapEndiannessForArrayOfShorts(textIndices8, sizeof(textIndices8));
		SwapEndiannessForArrayOfShorts(textIndices9, sizeof(textIndices9));
		SwapEndiannessForArrayOfShorts(textIndices10, sizeof(textIndices10));
		SwapEndiannessForArrayOfShorts(textIndices11, sizeof(textIndices11));
		SwapEndiannessForArrayOfShorts(textIndices12, sizeof(textIndices12));
		SwapEndiannessForArrayOfShorts(textIndices13, sizeof(textIndices13));
		SwapEndiannessForArrayOfShorts(textIndices14, sizeof(textIndices14));
		SwapEndiannessForArrayOfShorts(textIndices15, sizeof(textIndices15));
		SwapEndiannessForArrayOfShorts(textIndices16, sizeof(textIndices16));

		if( !indiceFinder.ValidateNewIndices(0, sizeof(textIndices1)) ) return false;
		if( !indiceFinder.ValidateNewIndices(1, sizeof(textIndices2)) ) return false;
		if( !indiceFinder.ValidateNewIndices(2, sizeof(textIndices3)) ) return false;
		if( !indiceFinder.ValidateNewIndices(3, sizeof(textIndices4)) ) return false;
		if( !indiceFinder.ValidateNewIndices(4, sizeof(textIndices5)) ) return false;
		if( !indiceFinder.ValidateNewIndices(5, sizeof(textIndices6)) ) return false;
		if( !indiceFinder.ValidateNewIndices(6, sizeof(textIndices7)) ) return false;
		if( !indiceFinder.ValidateNewIndices(7, sizeof(textIndices8)) ) return false;
		if( !indiceFinder.ValidateNewIndices(8, sizeof(textIndices9)) ) return false;
		if( !indiceFinder.ValidateNewIndices(9,  sizeof(textIndices10)) ) return false;
		if( !indiceFinder.ValidateNewIndices(10, sizeof(textIndices11)) ) return false;
		if( !indiceFinder.ValidateNewIndices(11, sizeof(textIndices12)) ) return false;
		if( !indiceFinder.ValidateNewIndices(12, sizeof(textIndices13)) ) return false;
		if( !indiceFinder.ValidateNewIndices(13, sizeof(textIndices14)) ) return false;
		if( !indiceFinder.ValidateNewIndices(14, sizeof(textIndices15)) ) return false;
		if( !indiceFinder.ValidateNewIndices(15, sizeof(textIndices16)) ) return false;

		miniGameFile.WriteData(indiceFinder.indiceAddresses[0], (char*)textIndices1, sizeof(textIndices1));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[1], (char*)textIndices2, sizeof(textIndices2));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[2], (char*)textIndices3, sizeof(textIndices3));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[3], (char*)textIndices4, sizeof(textIndices4));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[4], (char*)textIndices5, sizeof(textIndices5));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[5], (char*)textIndices6, sizeof(textIndices6));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[6], (char*)textIndices7, sizeof(textIndices7));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[7], (char*)textIndices8, sizeof(textIndices8));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[8], (char*)textIndices9, sizeof(textIndices9));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[9], (char*)textIndices10, sizeof(textIndices10));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[10], (char*)textIndices11, sizeof(textIndices11));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[11], (char*)textIndices12, sizeof(textIndices12));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[12], (char*)textIndices13, sizeof(textIndices13));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[13], (char*)textIndices14, sizeof(textIndices14));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[14], (char*)textIndices15, sizeof(textIndices15));
		miniGameFile.WriteData(indiceFinder.indiceAddresses[15], (char*)textIndices16, sizeof(textIndices16));
	}
#endif

	//Fixup # Round to be Round #
	{
		unsigned short roundLocation = 0xe0ff; //ffe0 in big endian
		unsigned short numberLocation = 0x60e4; //e460 in big endian
		
		miniGameFile.WriteData(0x000422bc, (char*)&roundLocation, sizeof(roundLocation));
		miniGameFile.WriteData(0x0001aa12, (char*)&numberLocation, sizeof(numberLocation));
	}

#if !USE_TREKKIS_MINIGAME_DATA
	//Tutorial fontsheet
	{
		const string fontSheetFileName = inTranslatedDataDirectory + "MiniHana_TutorialFontSheet.bmp";
		BmpToSaturnConverter patchedFontSheet;
		const unsigned int tileDim = 16;
		if( !patchedFontSheet.ConvertBmpToSakuraFormat(fontSheetFileName, false, 0, &tileDim, &tileDim) )
		{
			printf("PatchMiniHana: Couldn't convert image: %s.\n", logoFileName.c_str());
			return false;
		}
		patchedFontSheet.PackTiles();

		SakuraCompressedData compressedData;
		compressedData.PatchDataInMemory(patchedFontSheet.mpPackedTiles, patchedFontSheet.mPackedTileSize, true, false, 384);
		if( compressedData.mDataSize != 384 )
		{
			printf("PatchMiniHana: Patched data is too big for: %s.\n", fontSheetFileName.c_str());
		}
		miniGameFile.WriteData(0x000a8178, compressedData.mpCompressedData, compressedData.mDataSize);
	}
	
	//Tutorial text
	{
		MiniGameTextIndiceFinder indiceFinder;
		indiceFinder.FindIndices(miniGameOriginalFile.GetData(), 0x000a6f1c, 230);
		if( indiceFinder.indiceAddresses.size() != 70 )
		{
			printf("PatchMiniHana: Couldn't find text indices for tutorial\n");
			return false;
		}

		short textIndices1[] = {1, 2, 0};
		short textIndices2[] = {3, 4, 5, 6, 7, 8, 0};
		short textIndices3[] = {9, 10, 11, 0};
		short textIndices4[] = {12, 13, 14, 15, 16, 17, 0};
		short textIndices5[] = {18, 19, 20, 21, 22, 0};
		short textIndices6[] = {23, 24, 25, 0};
		short textIndices7[] = {26, 27, 28, 29, 0};
		short textIndices8[] = {30, 31, 32, 0};
		short textIndices9[] = {33, 34, 35, 36, 0};
		short textIndices10[] = {37, 38, 39, 40, 41, 0};
		short textIndices11[] = {42, 43, 44, 0};
		short textIndices12[] = {45, 46, 47, 0};
		short textIndices13[] = {48, 49, 50, 51, 52, 0};
		short textIndices14[] = {53, 54, 55, 56, 57, 0};
		short textIndices15[] = {58, 59, 60, 61, 0};
		short textIndices16[] = {62, 63, 0};
		short textIndices17[] = {64, 65, 0};
		short textIndices18[] = {66, 67, 68, 69, 0};
		short textIndices19[] = {70, 71, 72, 0};
		short textIndices20[] = {73, 74, 75, 0};
		short textIndices21[] = {76, 77, 78, 79, 0};
		short textIndices22[] = {80, 81, 82, 0};
		short textIndices23[] = {83, 84, 85, 86, 0};
		short textIndices24[] = {87, 88, 89, 0};
		short textIndices25[] = {90, 91, 92, 93, 94, 0};
		short textIndices26[] = {95, 96, 97, 0};
		short textIndices27[] = {98, 99, 100, 0};
		short textIndices28[] = {101, 102, 103, 0};
		short textIndices29[] = {104, 105, 106, 107, 0};
		short textIndices30[] = {108, 109, 0};
		short textIndices31[] = {110, 111, 0};
		short textIndices32[] = {112, 113, 114, 115, 0};
		short textIndices33[] = {116, 117, 118, 0};
		short textIndices34[] = {119, 120, 121, 122, 0};
		short textIndices35[] = {123, 124, 125, 0};
		short textIndices36[] = {126, 127, 0};
		short textIndices37[] = {128, 129, 130, 131, 0};
		short textIndices38[] = {132, 133, 0};
		short textIndices39[] = {134, 135, 136, 0};
		short textIndices40[] = {137, 138, 139, 140, 141, 0};
		short textIndices41[] = {142, 143, 144, 145, 146, 0};
		short textIndices42[] = {147, 148, 149, 0};
		short textIndices43[] = {150, 151, 152, 153, 0};
		short textIndices44[] = {154, 155, 156, 157, 158, 159, 0};
		short textIndices45[] = {160, 161, 162, 0};
		short textIndices46[] = {163, 164, 165, 166, 167, 0};
		short textIndices47[] = {168, 169, 170, 171, 172, 0};
		short textIndices48[] = {173, 174, 175, 176, 0};
		short textIndices49[] = {177, 178, 179, 180, 0};
		short textIndices50[] = {181, 182, 183, 184, 0};
		short textIndices51[] = {185, 186, 187, 188, 0};
		short textIndices52[] = {189, 190, 191, 192, 193, 194, 0};
		short textIndices53[] = {195, 196, 197, 198, 0};
		short textIndices54[] = {199, 200, 201, 202, 0};
		short textIndices55[] = {203, 204, 205, 206, 207, 0};
		short textIndices56[] = {208, 209, 210, 211, 0};
		short textIndices57[] = {212, 213, 214, 215, 216, 217, 0};
		short textIndices58[] = {218, 219, 220, 221, 222, 223, 0};
		short textIndices59[] = {222, 226, 0};
		short textIndices60[] = {227, 228, 0};
		short textIndices61[] = {227, 228, 0};
		short textIndices62[] = {227, 228, 0};
		short textIndices63[] = {227, 228, 0};
		short textIndices64[] = {227, 228, 0};
		short textIndices65[] = {227, 228, 0};
		short textIndices66[] = {227, 228, 0};
		short textIndices67[] = {227, 228, 0};
		short textIndices68[] = {227, 228, 0};
		short textIndices69[] = {227, 228, 0};
		short textIndices70[] = {227, 228, 0};

#define PatchTextIndices(indice) SwapEndiannessForArrayOfShorts(indice, sizeof(indice));\
                                 if( !indiceFinder.ValidateNewIndices(indiceNumber, sizeof(indice)) ) return false;\
                                 miniGameFile.WriteData(indiceFinder.indiceAddresses[indiceNumber], (char*)indice, sizeof(indice));\
                                 ++indiceNumber;

		int indiceNumber = 0;
		PatchTextIndices(textIndices1);
		PatchTextIndices(textIndices2);
		PatchTextIndices(textIndices3);
		PatchTextIndices(textIndices4);
		PatchTextIndices(textIndices5);
		PatchTextIndices(textIndices6);
		PatchTextIndices(textIndices7);
		PatchTextIndices(textIndices8);
		PatchTextIndices(textIndices9);
		PatchTextIndices(textIndices10);
		PatchTextIndices(textIndices11);
		PatchTextIndices(textIndices12);
		PatchTextIndices(textIndices13);
		PatchTextIndices(textIndices14);
		PatchTextIndices(textIndices15);
		PatchTextIndices(textIndices16);
		PatchTextIndices(textIndices17);
		PatchTextIndices(textIndices18);
		PatchTextIndices(textIndices19);
		PatchTextIndices(textIndices20);
		PatchTextIndices(textIndices21);
		PatchTextIndices(textIndices22);
		PatchTextIndices(textIndices23);
		PatchTextIndices(textIndices24);
		PatchTextIndices(textIndices25);
		PatchTextIndices(textIndices26);
		PatchTextIndices(textIndices27);
		PatchTextIndices(textIndices28);
		PatchTextIndices(textIndices29);
		PatchTextIndices(textIndices30);
		PatchTextIndices(textIndices31);
		PatchTextIndices(textIndices32);
		PatchTextIndices(textIndices33);
		PatchTextIndices(textIndices34);
		PatchTextIndices(textIndices35);
		PatchTextIndices(textIndices36);
		PatchTextIndices(textIndices37);
		PatchTextIndices(textIndices38);
		PatchTextIndices(textIndices39);
		PatchTextIndices(textIndices40);
		PatchTextIndices(textIndices41);
		PatchTextIndices(textIndices42);
		PatchTextIndices(textIndices43);
		PatchTextIndices(textIndices44);
		PatchTextIndices(textIndices45);
		PatchTextIndices(textIndices46);
		PatchTextIndices(textIndices47);
		PatchTextIndices(textIndices48);
		PatchTextIndices(textIndices49);
		PatchTextIndices(textIndices50);
		PatchTextIndices(textIndices51);
		PatchTextIndices(textIndices52);
		PatchTextIndices(textIndices53);
		PatchTextIndices(textIndices54);
		PatchTextIndices(textIndices55);
		PatchTextIndices(textIndices56);
		PatchTextIndices(textIndices57);
		PatchTextIndices(textIndices58);
		PatchTextIndices(textIndices59);
		PatchTextIndices(textIndices60);
		PatchTextIndices(textIndices61);
		PatchTextIndices(textIndices62);
		PatchTextIndices(textIndices63);
		PatchTextIndices(textIndices64);
		PatchTextIndices(textIndices65);
		PatchTextIndices(textIndices66);
		PatchTextIndices(textIndices67);
		PatchTextIndices(textIndices68);
		PatchTextIndices(textIndices69);
		PatchTextIndices(textIndices70);
	}
#endif

	return true;
}

bool PatchMiniSwim(const string& patchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	const string patchedMinigameFileName = patchedSakuraDirectory + string("\\SAKURA3\\MINISWIM.BIN");

	FileReadWriter miniGameFile;
	if( !miniGameFile.OpenFile(patchedMinigameFileName) )
	{
		return false;
	}

	//Open translated logo
	const string logoFileName = inTranslatedDataDirectory + "SwimLogo.bmp";
	BmpToSaturnConverter patchedLogo;
	if( !patchedLogo.ConvertBmpToSakuraFormat(logoFileName, false) )
	{
		printf("PatchMiniSwim: Couldn't convert image: %s.\n", logoFileName.c_str());
		return false;
	}
	if( patchedLogo.mPalette.GetNumColors() > 16 )
	{
		printf("PatchMiniSwim: Patched logo has more than 16 colors\n");
		return false;
	}

	//Patch logo
	miniGameFile.WriteData(0x0003cf28, patchedLogo.GetImageData(), patchedLogo.GetImageDataSize());

#if !USE_TREKKIS_MINIGAME_DATA
	//Open translated fontsheet
	const string fontSheetFileName = inTranslatedDataDirectory + "MiniSwimFontsheet.bmp";
	BmpToSaturnConverter patchedFontSheet;
	const unsigned int tileDim = 16;
	if( !patchedFontSheet.ConvertBmpToSakuraFormat(fontSheetFileName, false, 0, &tileDim, &tileDim) )
	{
		printf("PatchMiniSwim: Couldn't convert image: %s.\n", logoFileName.c_str());
		return false;
	}

	patchedFontSheet.PackTiles();

	//Patch fontsheet
	miniGameFile.WriteData(0x0003b1a8, patchedFontSheet.mpPackedTiles, patchedFontSheet.mPackedTileSize);

	//Patch text lookups
	short mainText[]    = {1, 2, 3, 4, 5, 0x0a0d, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 0x0a0d, 19, 20, 21, 22, 23, 24, 25, 26, 27, 0};
	short buttonText[]  = {28, 29, 0x0a0d, 30, 31, 0x0a0d, 30, 31, 0x0a0d, 30, 31, 0};
	short mainText2[]   = {32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 0x0a0d, 48, 49, 50, 51, 52, 53, 54, 55, 0};
	short buttonText2[] = {56, 57, 0x0a0d, 28, 29, 0x0a0d, 58, 59, 0};

	SwapEndiannessForArrayOfShorts(mainText, sizeof(mainText));
	SwapEndiannessForArrayOfShorts(buttonText, sizeof(buttonText));
	SwapEndiannessForArrayOfShorts(mainText2, sizeof(mainText2));
	SwapEndiannessForArrayOfShorts(buttonText2, sizeof(buttonText2));

	miniGameFile.WriteData(0x00056b28, (char*)mainText, sizeof(mainText));
	miniGameFile.WriteData(0x00056bc2, (char*)buttonText, sizeof(buttonText));
	miniGameFile.WriteData(0x00056b78, (char*)mainText2, sizeof(mainText2));
	miniGameFile.WriteData(0x00056be0, (char*)buttonText2, sizeof(buttonText2));
#endif

	return true;
}

bool PatchMiniGames(const string& rootSakuraDirectory, const string& patchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	printf("Patching Minigames\n");

	//Open translated option 1 (start)
	const string imageBatch1FileName = inTranslatedDataDirectory + "MiniGameImages1.bmp";
	BmpToSaturnConverter patchedImageBatch1;
	if( !patchedImageBatch1.ConvertBmpToSakuraFormat(imageBatch1FileName, false) )
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", imageBatch1FileName.c_str());
		return false;
	}

	//Open translated time image
	const string timeImageFileName = inTranslatedDataDirectory + "MiniGameTime.bmp";
	BmpToSaturnConverter patchedTimeImage;
	if( !patchedTimeImage.ConvertBmpToSakuraFormat(timeImageFileName, false) )
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", timeImageFileName.c_str());
		return false;
	}

	//Open translated seconds image
	const string secondsImageFileName = inTranslatedDataDirectory + "MiniGameSeconds.bmp";
	BmpToSaturnConverter patchedSecondsImage;
	if( !patchedSecondsImage.ConvertBmpToSakuraFormat(secondsImageFileName, false) )
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", secondsImageFileName.c_str());
		return false;
	}

	//Open translated seconds image
	const string minutesImageFileName = inTranslatedDataDirectory + "MiniGameMinutes.bmp";
	BmpToSaturnConverter patchedMinutesImage;
	if( !patchedMinutesImage.ConvertBmpToSakuraFormat(minutesImageFileName, false) )
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", minutesImageFileName.c_str());
		return false;
	}

	//Open translated N\A image
	const string naImageFileName = inTranslatedDataDirectory + "MiniGameNAPatched.bmp";
	BmpToSaturnConverter naImage;
	if (!naImage.ConvertBmpToSakuraFormat(naImageFileName, false))
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", naImageFileName.c_str());
		return false;
	}

	//Open translated Practicing image
	const string scrollingTextImageFileName = inTranslatedDataDirectory + "MiniGameScrollingText.bmp";
	BmpToSaturnConverter scrollingTextImage;
	if( !scrollingTextImage.ConvertBmpToSakuraFormat(scrollingTextImageFileName, false) )
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", scrollingTextImageFileName.c_str());
		return false;
	}

	//Open translated RankingText image
	const string rankingTextImageFileName = inTranslatedDataDirectory + "MiniGameRankingText.bmp";
	BmpToSaturnConverter rankingTextImage;
	if( !rankingTextImage.ConvertBmpToSakuraFormat(rankingTextImageFileName, false) )
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", rankingTextImageFileName.c_str());
		return false;
	}

	//Open translated RankingLogo image
	const string rankingLogoImageFileName = inTranslatedDataDirectory + "MiniGameRankingLogo.bmp";
	BmpToSaturnConverter rankingLogoImage;
	if( !rankingLogoImage.ConvertBmpToSakuraFormat(rankingLogoImageFileName, false) )
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", rankingLogoImageFileName.c_str());
		return false;
	}

	//Open translated RankingLogo image
	const string miniHanaPopupText = inTranslatedDataDirectory + "MiniHanaPopupText.bmp";
	BmpToSaturnConverter miniHanaImage;
	if( !miniHanaImage.ConvertBmpToSakuraFormat(miniHanaPopupText, false) )
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", miniHanaPopupText.c_str());
		return false;
	}

	//Open translated Minihana colored images
	const string miniHanaColoredImagesPath = inTranslatedDataDirectory + "MiniHanaColoredImages.bmp";
	BmpToSaturnConverter miniHanaColoredImages;
	if( !miniHanaColoredImages.ConvertBmpToSakuraFormat(miniHanaColoredImagesPath, false) )
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", miniHanaColoredImagesPath.c_str());
		return false;
	}

	struct MiniGameFileOffsets
	{
		string       fileName;
		unsigned int imageBatchOffset;
		unsigned int timeImageOffset;
		unsigned int secondsImageOffset;
		unsigned int minutesImageOffset;
		unsigned int notAvailableImageOffset;
		unsigned int scrollingTextOffset;
		unsigned int rankingTextOffset;
		unsigned int rankingLogoOffset;
		unsigned int hanaPopupTextOffset;
		unsigned int hanaColoredImagesOffset;
	};

	const int numMiniGameFiles = 8;
	MiniGameFileOffsets miniGameOption1Offsets[numMiniGameFiles] = 
	{
		"HANAMAIN.BIN", 0x00013c08, 0x00016628, 0x000165a8, 0x00016528, 0x00015508, 0x00016728, 0x00014d88, 0x00015608, 0x00023768, 0x00026868,
		"MINICOOK.BIN", 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001435c, 0x0001333c, 0x0001455c, 0x00012bbc, 0x0001343c, 0,          0,
		"MINIHANA.BIN", 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001435c, 0x0001333c, 0x0001455c, 0x00012bbc, 0x0001343c, 0x000423b0, 0x000454b0,
		"MINIMAIG.BIN", 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001435c, 0x0001333c, 0x0001455c, 0x00012bbc, 0x0001343c, 0,          0,
		"MINISHOT.BIN", 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001435c, 0x0001333c, 0x0001455c, 0x00012bbc, 0x0001343c, 0,          0,
		"MINISLOT.BIN", 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001435c, 0x0001333c, 0x0001455c, 0x00012bbc, 0x0001343c, 0,          0,
		"MINISOJI.BIN", 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001435c, 0x0001333c, 0x0001455c, 0x00012bbc, 0x0001343c, 0,          0,
		"MINISWIM.BIN", 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001435c, 0x0001333c, 0x0001455c, 0x00012bbc, 0x0001343c, 0,          0,
	};

	//Patch common data among all mini games
	for(int miniGameIndex = 0; miniGameIndex < numMiniGameFiles; ++miniGameIndex)
	{
		//Create a copy of the original file in the patched directory to stomp any modifications from previous patch runs
		const string originalMinigameFileName = rootSakuraDirectory + string("\\SAKURA3\\") + miniGameOption1Offsets[miniGameIndex].fileName;
		const string patchedMinigameFileName  = patchedSakuraDirectory + string("\\SAKURA3\\") + miniGameOption1Offsets[miniGameIndex].fileName;
		CopyFile(originalMinigameFileName.c_str(), patchedMinigameFileName.c_str(), false);

		//Open patched MiniSwim file
		FileReadWriter miniGameFile;
		if( !miniGameFile.OpenFile(patchedMinigameFileName.c_str()) )
		{
			return false;
		}

		miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].imageBatchOffset,        patchedImageBatch1.GetImageData(), patchedImageBatch1.GetImageDataSize());
		miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].timeImageOffset,         patchedTimeImage.GetImageData(),    patchedTimeImage.GetImageDataSize());
		miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].secondsImageOffset,      patchedSecondsImage.GetImageData(), patchedSecondsImage.GetImageDataSize());
		miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].minutesImageOffset,      patchedMinutesImage.GetImageData(), patchedMinutesImage.GetImageDataSize());
		miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].notAvailableImageOffset, naImage.GetImageData(),             naImage.GetImageDataSize());
		miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].scrollingTextOffset,     scrollingTextImage.GetImageData(),  scrollingTextImage.GetImageDataSize());
		miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].rankingTextOffset,		  rankingTextImage.GetImageData(),    rankingTextImage.GetImageDataSize());
		miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].rankingLogoOffset,		  rankingLogoImage.GetImageData(),    rankingLogoImage.GetImageDataSize());

		if( miniGameOption1Offsets[miniGameIndex].hanaPopupTextOffset )
		{
			miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].hanaPopupTextOffset, miniHanaImage.GetImageData(),		  miniHanaImage.GetImageDataSize());
		}

		if( miniGameOption1Offsets[miniGameIndex].hanaColoredImagesOffset )
		{
			miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].hanaColoredImagesOffset, miniHanaColoredImages.GetImageData(),	  miniHanaColoredImages.GetImageDataSize());
		}
	}

	bool bResult = PatchMiniSwim(patchedSakuraDirectory, inTranslatedDataDirectory);
	bResult      = bResult && PatchMiniHana(patchedSakuraDirectory, inTranslatedDataDirectory, rootSakuraDirectory);
	bResult      = bResult && PatchMiniCook(patchedSakuraDirectory, inTranslatedDataDirectory, rootSakuraDirectory);
	bResult      = bResult && PatchMiniMaig(patchedSakuraDirectory, inTranslatedDataDirectory, rootSakuraDirectory);
	bResult      = bResult && PatchMiniShot(patchedSakuraDirectory, inTranslatedDataDirectory, rootSakuraDirectory);
	bResult      = bResult && PatchMiniSlot(patchedSakuraDirectory, inTranslatedDataDirectory, rootSakuraDirectory);
	bResult      = bResult && PatchMiniSoji(patchedSakuraDirectory, inTranslatedDataDirectory, rootSakuraDirectory);

	printf("PatchMinigames Succeeded.\n");

	return bResult;
}

bool PatchTheEndLogo(const string& patchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	//Open translated fontsheet
	const string patchedImagePath = inTranslatedDataDirectory + "\\TheEndImage.bmp";
	BmpToSaturnConverter patchedImage;
	const unsigned int tileDims = 8;
	if( !patchedImage.ConvertBmpToSakuraFormat(patchedImagePath, false, 0, &tileDims, &tileDims) )
	{
		printf("PatchStatusScreen: Couldn't convert image: %s.\n", patchedImagePath.c_str());
		return false;
	}

	patchedImage.PackTiles();

	if( patchedImage.mPackedTileSize > 64*64 )
	{
		printf("Compressed TheEndImage is too big.  Should be %i, is %i", 64*64, patchedImage.mPackedTileSize);
		return false;
	}

	const int NumFiles = 5;
	const char* pFileNames[NumFiles] = {"AI_CG.BIN", "KA_CG.BIN", "MA_CG.BIN", "SA_CG.BIN", "SU_CG.BIN"};
	
	for(int i = 0; i < NumFiles; ++i)
	{
		const string gameFilePath = patchedSakuraDirectory + Seperators + "SAKURA1" + Seperators + pFileNames[i];

		FileReadWriter gameFile;
		if( !gameFile.OpenFile(gameFilePath) )
		{
			return false;
		}

		gameFile.WriteData(0x000552a4, patchedImage.mpPackedTiles, patchedImage.mPackedTileSize);
	}

	return true;
}

bool PatchLoadScreen(const string& patchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	printf("Patch Load Screen\n");

	const string sakuraFilePath = patchedSakuraDirectory + Seperators + "SAKURA";
	FileData sakuraFileData;
	if( !sakuraFileData.InitializeFileData( FileNameContainer(sakuraFilePath.c_str())) )
	{
		return false;
	}

	FileReadWriter sakuraFile;
	if( !sakuraFile.OpenFile(sakuraFilePath ) )
	{
		return false;
	}

	const string fileDataPath = inTranslatedDataDirectory + Seperators + "LoadScreenFormatting.bin";
	FileData formattingData;
	if( !formattingData.InitializeFileData( FileNameContainer(fileDataPath.c_str())) )
	{
		return false;
	}

	//Patch font sheet
	{
#if 1
		//Open translated fontsheet
		const string fontSheetFileName = inTranslatedDataDirectory + "\\LoadSaveScreen.bmp";
		BmpToSaturnConverter patchedFontSheet;
		const unsigned int tileDim = 16;
		if( !patchedFontSheet.ConvertBmpToSakuraFormat(fontSheetFileName, false, 0, &tileDim, &tileDim) )
		{
			printf("PatchStatusScreen: Couldn't convert image: %s.\n", fontSheetFileName.c_str());
			return false;
		}

		patchedFontSheet.PackTiles();

		SakuraCompressedData fontSheetCompressedData;
		fontSheetCompressedData.PatchDataInMemory(patchedFontSheet.mpPackedTiles, patchedFontSheet.mPackedTileSize, true, false, 6970);
		if( fontSheetCompressedData.mDataSize > 6970 )
		{
			printf("Compressed LoadSaveScreen font sheet is too big");
			return false;
		}

		const unsigned int fontSheetAddress = GIsDisc2 ? 0x0005c78c : 0x0005c654;
		sakuraFile.WriteData(fontSheetAddress, fontSheetCompressedData.mpCompressedData, fontSheetCompressedData.mDataSize);
#else

		const string precompressedFontSheetName = inTranslatedDataDirectory + "\\LoadSaveScreenCompressed.bin";
		FileData precompressedData;
		precompressedData.InitializeFileData( FileNameContainer(precompressedFontSheetName.c_str()));

		//Patch fontsheet
		const unsigned int fontSheetAddress = GIsDisc2 ? 0x0005c78c : 0x0005c654;
		sakuraFile.WriteData(fontSheetAddress, precompressedData.GetData(), precompressedData.GetDataSize());
#endif
	}

	//Patch lookup table
	if( 1 )
	{
		MiniGameTextIndiceFinder indiceFinder;
		const unsigned int indiceOffset = GIsDisc2 ? 0x0005a1d6 : 0x0005a09e;
		indiceFinder.FindIndices(sakuraFileData.GetData(), indiceOffset, 144);
	//	indiceFinder.DumpIndices("d:\\rizwan\\indices.txt");
		if( indiceFinder.indiceAddresses.size() < 45 )
		{
			printf("PatchLoadScreen: Couldn't find text indices for load screen\n");
			return false;
		}

		short textIndices0[]  = {126, 68, 7, 95, 95, 0};                                       //Episode 1
		short textIndices1[]  = {93, 63, 136, 134, 30, 17, 67, 39, 88, 40, 47, 132, 15, 0};    //The Capital's Floral Assault Troop!
		short textIndices2[]  = {126, 68, 35, 95, 95, 0};                                      //Episode 2
		short textIndices3[]  = {18, 113, 56, 80, 100, 44, 117, 67, 0};                        //The Enemy Is...The Hive of Darkness!
		short textIndices4[]  = {126, 68, 54, 95, 95, 0};                                      //Episode 3
		short textIndices5[]  = {119, 121, 31, 97, 19, 63, 82, 2, 28, 91, 0};                  //I'm Not Fit To Be Captain?!
		short textIndices6[]  = {126, 68, 71, 95, 95, 0};                                      //Episode 4
		short textIndices7[]  = {5, 98, 31, 57, 103, 26, 64, 0};                               //Chaos! Chaos! The Great Rampage!!
		short textIndices8[]  = {126, 68, 89, 95, 95, 0};                                      //Episode 5
		short textIndices9[]  = {142, 116, 12, 109, 32, 42, 21, 87, 137, 111, 15, 0};          //Bloom Like A Flower! A Maiden's Willpower!
		short textIndices10[] = {126, 68, 110, 95, 95, 0};                                     //Episode 6
		short textIndices11[] = {93, 63, 82, 139, 73, 45, 6, 90, 83, 36, 59, 0};               //The Capital's Great Collapse!?
		short textIndices12[] = {126, 68, 124, 95, 95, 0};                                     //Episode 7
		short textIndices13[] = {93, 63, 101, 99, 31, 128, 33, 14, 64, 0};                     //Decisive Battle - The Limit of Life!
		short textIndices14[] = {126, 68, 138, 95, 95, 0};                                     //Episode 8
		short textIndices15[] = {100, 11, 37, 80, 10, 20, 63, 0};                              // A Date In Days Of Peace
		short textIndices16[] = {126, 68, 3, 95, 95, 0};                                       //Episode 9
		short textIndices17[] = {93, 63, 118, 77, 94, 75, 42, 98, 143, 76, 0};                 //The Ultimate Weapon Revealed
		short textIndices18[] = {126, 68, 16, 95, 95, 0};                                      //Episode 10
		short textIndices19[] = {135, 115, 41, 70, 85, 108, 109, 31, 0};                       //The Final Judgement
		short textIndices20[] = {100, 73, 61, 92, 72, 4, 0};                                   //Data to be copied
		short textIndices21[] = {90, 4, 61, 50, 95, 95, 95, 95, 0};                            //Copy which data
		short textIndices22[] = {100, 73, 61, 92, 86, 22, 0};                                  //Data to be loaded
		short textIndices23[] = {100, 73, 61, 92, 55, 113, 0};                                 //Data to be saved
		short textIndices24[] = {100, 73, 61, 92, 84, 78, 75, 0};                              //Data to be deleted
		short textIndices25[] = {133, 27, 21, 112, 0};                                         //No records
		short textIndices26[] = {102, 78, 75, 120, 0};                                         //Delete?
		short textIndices27[] = {34, 6, 0};                                                    //Yes
		short textIndices28[] = {133, 0};                                                      //No
		short textIndices29[] = {133, 24, 38, 130, 0};                                         //No save data
		short textIndices30[] = {13, 98, 59, 52, 122, 111, 31, 105, 58, 9, 0};                 //Please Insert Disc 2
		short textIndices31[] = {13, 98, 59, 52, 122, 111, 31, 105, 58, 8, 0};                 //Please Insert Disc 1
		short textIndices32[] = {135, 115, 96, 95, 95, 95, 0};                                 //Last Chapter
		short textIndices33[] = {93, 63, 125, 46, 0};                                          //The End
		short textIndices34[] = {66, 79, 31, 48, 60, 98, 53, 0};                               //could not be read.
		short textIndices35[] = {97, 49, 131, 77, 123, 63, 140, 43, 25, 106, 0};               //In order to continue playing
		short textIndices36[] = {74, 113, 23, 129, 140, 114, 64, 0};                           //Finished saving.
		short textIndices37[] = {13, 98, 59, 52, 122, 111, 31, 105, 58, 9, 0};                 //Please Insert Disc 2
		short textIndices38[] = {81, 49, 131, 77, 123, 63, 140, 43, 25, 107, 0};               //to continue playing.
		short textIndices39[] = {90, 4, 120, 0};                                               //Copy?
		short textIndices40[] = {74, 113, 120, 0};                                             //Save?
		short textIndices41[] = {104, 22, 120, 0};                                             //Load?
		short textIndices42[] = {74, 113, 23, 129, 140, 114, 64, 0};                           //Finished saving.
		short textIndices43[] = {102, 78, 75, 23, 129, 140, 114, 64, 0};                       //Finished deleting.
		short textIndices44[] = {90, 4, 23, 129, 140, 114, 64, 0};                             //Finished copying.

		const int pointerTableStartInRAM = GIsDisc2 ? 0x0605e1d6 : 0x0605e09e;
		int currentRAMAddress = pointerTableStartInRAM;
		int currentAddress    = indiceOffset;
		map<int, int> adjustedPointerMap;
#define PatchLoadScreenIndices(indice) SwapEndiannessForArrayOfShorts(indice, sizeof(indice));\
                                sakuraFile.WriteData(currentAddress, (char*)indice, sizeof(indice));\
								adjustedPointerMap[(indiceFinder.indiceAddresses[indiceNumber] - indiceOffset) + pointerTableStartInRAM] = currentRAMAddress;\
								currentRAMAddress += sizeof(indice);\
								currentAddress    += sizeof(indice);\
								++indiceNumber;

		int indiceNumber = 0;
		PatchLoadScreenIndices(textIndices0);
		PatchLoadScreenIndices(textIndices1);
		PatchLoadScreenIndices(textIndices2);
		PatchLoadScreenIndices(textIndices3);
		PatchLoadScreenIndices(textIndices4);
		PatchLoadScreenIndices(textIndices5);
		PatchLoadScreenIndices(textIndices6);
		PatchLoadScreenIndices(textIndices7);
		PatchLoadScreenIndices(textIndices8);
		PatchLoadScreenIndices(textIndices9);
		PatchLoadScreenIndices(textIndices10);
		PatchLoadScreenIndices(textIndices11);
		PatchLoadScreenIndices(textIndices12);
		PatchLoadScreenIndices(textIndices13);
		PatchLoadScreenIndices(textIndices14);
		PatchLoadScreenIndices(textIndices15);
		PatchLoadScreenIndices(textIndices16);
		PatchLoadScreenIndices(textIndices17);
		PatchLoadScreenIndices(textIndices18);
		PatchLoadScreenIndices(textIndices19);
		PatchLoadScreenIndices(textIndices20);
		PatchLoadScreenIndices(textIndices21);
		PatchLoadScreenIndices(textIndices22);
		PatchLoadScreenIndices(textIndices23);
		PatchLoadScreenIndices(textIndices24);
		PatchLoadScreenIndices(textIndices25);
		PatchLoadScreenIndices(textIndices26);
		PatchLoadScreenIndices(textIndices27);
		PatchLoadScreenIndices(textIndices28);
		PatchLoadScreenIndices(textIndices29);
		PatchLoadScreenIndices(textIndices30);
		PatchLoadScreenIndices(textIndices31);
		PatchLoadScreenIndices(textIndices32);
		PatchLoadScreenIndices(textIndices33);
		PatchLoadScreenIndices(textIndices34);
		PatchLoadScreenIndices(textIndices35);
		PatchLoadScreenIndices(textIndices36);
		PatchLoadScreenIndices(textIndices37);
		PatchLoadScreenIndices(textIndices38);
		PatchLoadScreenIndices(textIndices39);
		PatchLoadScreenIndices(textIndices40);
		PatchLoadScreenIndices(textIndices41);
		PatchLoadScreenIndices(textIndices42);
		PatchLoadScreenIndices(textIndices43);
		PatchLoadScreenIndices(textIndices44);

		//Write out new pointer table
		printf("Fixing LoadScreen Pointer\n");
		int numFixed = 0;
		for(unsigned long i = 0; i < sakuraFileData.GetDataSize(); i += 4)
		{
			int origValue;
			sakuraFileData.ReadData(i, (char*)&origValue, sizeof(origValue), true);

			if( adjustedPointerMap.find(origValue) != adjustedPointerMap.end() )
			{
				const int newValue = adjustedPointerMap[ origValue ];
				sakuraFile.WriteData(i, (char*)&newValue, sizeof(newValue), true);

			//	printf("     %0x to %0x at %0x\n", origValue, SwapByteOrder(newValue), i);
				numFixed++;
			}
		}

		//printf("Num Fixed: %i\n", numFixed);
	}

	//Patch button formatting
	const unsigned int formattingAddress = GIsDisc2 ? 0x00059da8 : 0x00059c70;
	sakuraFile.WriteData(formattingAddress, formattingData.GetData(), formattingData.GetDataSize());

	unsigned char arrowFormatting = 0xec;
	const unsigned int formattingAddress2 = GIsDisc2 ? 0x0005a193 : 0x0005a05b;
	sakuraFile.WriteData(formattingAddress2, (char*)&arrowFormatting, sizeof(arrowFormatting));

	return true;
}

bool PatchStatusScreen(const string& patchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	printf("Patch Status Screen\n");

	const string sakuraFilePath = patchedSakuraDirectory + Seperators + "SAKURA";
	FileData sakuraFileData;
	if( !sakuraFileData.InitializeFileData( FileNameContainer(sakuraFilePath.c_str())) )
	{
		return false;
	}

	FileReadWriter sakuraFile;
	if( !sakuraFile.OpenFile(sakuraFilePath ) )
	{
		return false;
	}

	const string icatallFilePath = patchedSakuraDirectory + Seperators + "SAKURA1\\ICATALL.DAT";
	FileReadWriter icatallFile;
	if( !icatallFile.OpenFile(icatallFilePath ) )
	{
		return false;
	}

	//Patch background image
	{
		//Open translated fontsheet
		const string imagePath = inTranslatedDataDirectory + "\\StatusScreen.bmp";
		BmpToSaturnConverter patchedImage;
		const unsigned int tileWidth  = 144;
		const unsigned int tileHeight = 64;
		if( !patchedImage.ConvertBmpToSakuraFormat(imagePath, false, 0, &tileWidth, &tileHeight) )
		{
			printf("PatchStatusScreen: Couldn't convert image: %s.\n", imagePath.c_str());
			return false;
		}

		patchedImage.PackTiles();

		SakuraCompressedData compressedData;
		compressedData.PatchDataInMemory(patchedImage.mpPackedTiles, patchedImage.mPackedTileSize, true, false, 21967);
		if( compressedData.mDataSize > 21967 )
		{
			printf("Compressed StatusScreen is too big");
			return false;
		}

		icatallFile.WriteData(0x00008800, compressedData.mpCompressedData, compressedData.mDataSize);
	}//Done patching background image

	//Open translated fontsheet
	const string fontSheetFileName = inTranslatedDataDirectory + "\\StatusScreenFontSheet.bmp";
	BmpToSaturnConverter patchedFontSheet;
	const unsigned int tileDim = 16;
	if( !patchedFontSheet.ConvertBmpToSakuraFormat(fontSheetFileName, false, 0, &tileDim, &tileDim) )
	{
		printf("PatchStatusScreen: Couldn't convert image: %s.\n", fontSheetFileName.c_str());
		return false;
	}

	patchedFontSheet.PackTiles();

	SakuraCompressedData fontSheetCompressedData;
	fontSheetCompressedData.PatchDataInMemory(patchedFontSheet.mpPackedTiles, patchedFontSheet.mPackedTileSize, true, false, 2522);
	if( fontSheetCompressedData.mDataSize > 2522 )
	{
		printf("Compressed font sheet is too big");
		return false;
	}

	//Patch fontsheet
	icatallFile.WriteData(0x00013800, fontSheetCompressedData.mpCompressedData, fontSheetCompressedData.mDataSize);

	//Read pointer table
	static const int NumPointers = 12;
	const int PointerTableOffset = GIsDisc2 ? 0x0005f5ec : 0x0005f4b4;
	int pointerTable[NumPointers];
	sakuraFile.ReadData(PointerTableOffset, (char*)pointerTable, sizeof(pointerTable), false);
	
	map<int, int> originalPointerMap;
	for(int i = 0; i < NumPointers; ++i) 
	{
		SwapByteOrderInPlace((char*)&pointerTable[i], sizeof(int));

		originalPointerMap[pointerTable[i]] = i;
	}

	const int lookupTableStart = GIsDisc2 ? 0x0005f560 : 0x0005f428;
	MiniGameTextIndiceFinder indiceFinder;
	indiceFinder.FindIndices(sakuraFileData.GetData(), lookupTableStart, 48);
	if( indiceFinder.indiceAddresses.size() != 18 )
	{
		printf("PatchStatusScreen: Couldn't find text indices\n");
		return false;
	}

	short textIndices1[] = {1, 2, 3, 0};      //Sakura
	short textIndices2[] = {4, 5, 6, 0};      //Sumire
	short textIndices3[] = {7, 8, 9, 0};      //Kohran
	short textIndices4[] = {10, 11, 12, 0};   //Kanna
	short textIndices5[] = {13, 14, 15, 0};   //Maria
	short textIndices6[] = {16, 17, 0};       //Iris
	short textIndices7[] = {18, 19, 0};       //High
	short textIndices8[] = {21, 22, 0};       //Spirits
	short textIndices9[] = {23, 24, 25, 0};   //Normal
	short textIndices10[] = {26, 27, 0};      //Mood
	short textIndices11[] = {28, 29, 30, 0};  //Terrible
	short textIndices12[] = {26, 27, 0};      //Mood
	short textIndices13[] = {31, 32, 0};      //Attack
	short textIndices14[] = {33, 34, 0};      //Defense
	short textIndices15[] = {35, 36, 0};      //Movement

	//Clear out out lookup values
	static const int EmptyDataSize = 110;
	char emptyData[EmptyDataSize];
	memset(emptyData, 0, EmptyDataSize);
	sakuraFile.WriteData(lookupTableStart, emptyData, EmptyDataSize);

	const int pointerTableStartInRAM = GIsDisc2 ? 0x06063560 : 0x06063428;
	int currentRAMAddress = pointerTableStartInRAM;
	int currentAddress    = lookupTableStart;
	map<int, int> adjustedPointerMap;
#define PatchTextIndices(indice) SwapEndiannessForArrayOfShorts(indice, sizeof(indice));\
                                if( !indiceFinder.ValidateNewIndices(indiceNumber, sizeof(indice)) ) return false;\
                                sakuraFile.WriteData(currentAddress, (char*)indice, sizeof(indice));\
								adjustedPointerMap[(indiceFinder.indiceAddresses[indiceNumber] - lookupTableStart) + pointerTableStartInRAM] = currentRAMAddress;\
								currentRAMAddress += sizeof(indice);\
								currentAddress    += sizeof(indice);\
								++indiceNumber;

	
	int indiceNumber = 0;
	int statsPointers[3];
	PatchTextIndices(textIndices1);
	PatchTextIndices(textIndices2);
	PatchTextIndices(textIndices3);
	PatchTextIndices(textIndices4);
	PatchTextIndices(textIndices5);
	PatchTextIndices(textIndices6);
	PatchTextIndices(textIndices7);
	PatchTextIndices(textIndices8);
	PatchTextIndices(textIndices9);
	PatchTextIndices(textIndices10);
	PatchTextIndices(textIndices11);
	PatchTextIndices(textIndices12);

	currentRAMAddress += currentRAMAddress%4;
	currentAddress += currentAddress%4;
	statsPointers[0] = SwapByteOrder(currentRAMAddress); //Address for Atk
	PatchTextIndices(textIndices13);

	currentRAMAddress += currentRAMAddress%4;
	currentAddress += currentAddress%4;
	statsPointers[1] = SwapByteOrder(currentRAMAddress); //Address for Def
	PatchTextIndices(textIndices14);

	currentRAMAddress += currentRAMAddress%4;
	currentAddress += currentAddress%4;
	statsPointers[2] = SwapByteOrder(currentRAMAddress); //Address for Mov
	PatchTextIndices(textIndices15);

	//Write out new pointer table
	int newPointerTable[NumPointers];
	for(int i = 0; i < NumPointers; ++i)
	{
		newPointerTable[i] = SwapByteOrder( adjustedPointerMap[ pointerTable[i] ] );
	}
	sakuraFile.WriteData(PointerTableOffset, (char*)newPointerTable, sizeof(int)*NumPointers);

	const int statsPointersOffset = GIsDisc2 ? 0x0003e470 : 0x0003e458;
	sakuraFile.WriteData(statsPointersOffset, (char*)statsPointers, 3*sizeof(int));

	//Modify the code that reads the Atk, Def, Mov strings to read 4 bytes because our data takes up two tiles.
	{
		//Stats codes
		const long disc1Offset = GIsDisc2 ? 0 : -24;

		//Disc 1 
		unsigned short mov_l_atR1_R1 = 0x1261;
		unsigned short mov_l_r1_atR6 = 0x1226;

		#define SakuraAdd(x) ( (x - sakuraLoadAddress) + disc1Offset)
		const unsigned int sakuraLoadAddress = 0x06004000;
		sakuraFile.WriteData( SakuraAdd(0x0604240A), (char*)&mov_l_atR1_R1, sizeof(short) );
		sakuraFile.WriteData( SakuraAdd(0x06042410), (char*)&mov_l_atR1_R1, sizeof(short) );
		sakuraFile.WriteData( SakuraAdd(0x06042414), (char*)&mov_l_atR1_R1, sizeof(short) );
		sakuraFile.WriteData( SakuraAdd(0x06042416), (char*)&mov_l_r1_atR6, sizeof(short) );
	}

	return true;
}

void CreateFontSheetEntries(const string& inFileName, bool bEveryCombination)
{
	FILE* pFile = nullptr;
	const errno_t errorValue = fopen_s(&pFile, inFileName.c_str(), "r");
	if( errorValue )
	{
		printf("Unable to open file: %s.  Error code: %i \n", inFileName.c_str(), errorValue);
		return;
	}

	map<string, int> fontSheetEntries;
	while( 1 )
	{
		string newEntry;

		//First char
		int result = fgetc(pFile);
		char c = (char)result;
		if( result == EOF )
		{
			break;
		}
		if( c == '\n' )
		{
			continue;
		}

		newEntry += c;

		//Second char
		result = fgetc(pFile);
		char c2 = (char)result;
		if( result == EOF )
		{
			if( fontSheetEntries.find(newEntry) == fontSheetEntries.end() )
			{
				fontSheetEntries[newEntry] = 1;
			}
			else
			{
				fontSheetEntries[newEntry] = fontSheetEntries[newEntry] + 1;
			}
			break;
		}
		if( c2 == '\n' )
		{
			if( fontSheetEntries.find(newEntry) == fontSheetEntries.end() )
			{
				fontSheetEntries[newEntry] = 1;
			}
			else
			{
				fontSheetEntries[newEntry] = fontSheetEntries[newEntry] + 1;
			}
			continue;
		}
		newEntry += c2;

		if( fontSheetEntries.find(newEntry) == fontSheetEntries.end() )
		{
			fontSheetEntries[newEntry] = 1;
		}
		else
		{
			fontSheetEntries[newEntry] = fontSheetEntries[newEntry] + 1;
		}

		if( bEveryCombination )
		{
			fpos_t currPos = 0;
			fgetpos(pFile, &currPos);
			fseek(pFile, long(currPos - 1), SEEK_SET);
		}
	}

	for(map<string, int>::const_iterator iter = fontSheetEntries.begin(); iter != fontSheetEntries.end(); ++iter)
	{
		printf("%s %i\n", iter->first.c_str(), iter->second);
	}
}

void ExtractSubtitles(const string& rootSakuraDirectory, const string& outDirectory)
{
	CreateDirectoryHelper(outDirectory);

	//SAKURA file
	FileNameContainer sakuraFilePath((rootSakuraDirectory + "SAKURA").c_str());
	FileData sakuraData;
	if( !sakuraData.InitializeFileData(sakuraFilePath) )
	{
		return;
	}

	//LOGO.SH2 file
	FileNameContainer logoFilePath((rootSakuraDirectory + "\\SAKURA1\\LOGO.SH2").c_str());
	FileData logoData;
	if( !logoData.InitializeFileData(logoFilePath) )
	{
		return;
	}

	//Read in info for each subtitle image
	struct SubtitleInfo
	{
		unsigned int   unknown;
		unsigned char  width;
		unsigned char  height;
		unsigned short unknown2;

		void PostLoadFixup()
		{
			width  = SwapByteOrder(width) * 8;
			height = SwapByteOrder(height);
		}
	};

	const int numSubtitles = 17;
	SubtitleInfo subtitles[17];
	memcpy_s(subtitles, sizeof(subtitles), sakuraData.GetData() + 0x000a20c, sizeof(subtitles));

	for (int i = 0; i < numSubtitles; ++i)
	{
		subtitles[i].PostLoadFixup();
	}

	//Uncompress subtitle image block
	PRSDecompressor uncompressedData;
	uncompressedData.UncompressData((void*)(logoData.GetData() + 0x000196d8), logoData.GetDataSize() - 0x000196d8);

	char buffer[10];
	int imageOffset = 0;
	for(int i = 0; i < numSubtitles; ++i)
	{
		sprintf_s(buffer, 10, "%i.bmp", i);
		const string outFileName = outDirectory + buffer;

		const int imageSize = subtitles[i].width*subtitles[i].height / 2;
		ExtractImageFromData(uncompressedData.mpUncompressedData + imageOffset, imageSize, outFileName, logoData.GetData() + 0x00019578, 32, subtitles[i].width, subtitles[i].height, 1,
			                 256, 0, false, false);

		imageOffset += imageSize;
	}
}

bool PatchSubtitles(const string& /*rootSakuraTaisenDirectory*/, const string& patchedSakuraTaisenDirectory, const string& patchedDataDirectory)
{
	//SAKURA file
	FileNameContainer sakuraFilePath((patchedSakuraTaisenDirectory + "SAKURA").c_str());
	FileReadWriter sakuraData;
	if( !sakuraData.OpenFile(sakuraFilePath.mFullPath) )
	{
		return false;
	}

	//LOGO.SH2 file
	FileNameContainer logoFilePath((patchedSakuraTaisenDirectory + "\\SAKURA1\\LOGO.SH2").c_str());
	FileReadWriter logoData;
	if( !logoData.OpenFile(logoFilePath.mFullPath) )
	{
		return false;
	}

	//Read in info for each subtitle image
	struct SubtitleInfo
	{
		unsigned short offset;
		unsigned short unknown1;
		unsigned char  width;
		unsigned char  height;
		unsigned short unknown2;

		void PostLoadFixup()
		{
			width  = width * 8;
			offset = SwapByteOrder(offset);
		}
	};

	const int numSubtitles = 17;
	SubtitleInfo subtitles[17];
	sakuraData.ReadData(0x000a20c, (char*)subtitles, sizeof(subtitles), false);
	//memcpy_s(subtitles, sizeof(subtitles), sakuraData.ReadData + 0x000a20c, sizeof(subtitles));

	for(int i = 0; i < numSubtitles; ++i)
	{
		subtitles[i].PostLoadFixup();
	}

	//Create saturn image data for all subtitle images
	char buffer[255];
	int imageDataSize = 0;
	unsigned int offsetFromStart = 0;
	BmpToSaturnConverter patchedSubtitles[numSubtitles];
	for(int i = 0; i < numSubtitles; ++i)
	{
		snprintf(buffer, 255, "\\Subtitles\\%i.bmp", i);

		const string subtitleImage = patchedDataDirectory + buffer;
		if( !patchedSubtitles[i].ConvertBmpToSakuraFormat(subtitleImage, false) )
		{
			printf("PatchSubtitles: Couldn't convert image: %s.\n", subtitleImage.c_str());
			return false;
		}

		subtitles[i].width  = (unsigned char)((patchedSubtitles[i].mTileExtractor.mImageWidth/8));
		subtitles[i].height = (unsigned char)((patchedSubtitles[i].mTileExtractor.mImageHeight));
		subtitles[i].offset = (unsigned short)offsetFromStart;
		subtitles[i].offset = SwapByteOrder(subtitles[i].offset);

		imageDataSize   += patchedSubtitles[i].GetImageDataSize();
		offsetFromStart += subtitles[i].width*subtitles[i].height/2;

		if( offsetFromStart > 0xffff )
		{
			printf("PatchSubtitles: Image data is too big\n");
			return false;
		}
	}

	//Pack all images into a single block of data
	char* pSubtitleImageBlock = new char[imageDataSize];
	int imageOffset = 0;
	for(int i = 0; i < numSubtitles; ++i)
	{
		memcpy_s(pSubtitleImageBlock + imageOffset, imageDataSize - imageOffset, patchedSubtitles[i].GetImageData(), patchedSubtitles[i].GetImageDataSize());

		imageOffset += patchedSubtitles[i].GetImageDataSize();
	}

	//Compress the patched data
	const int originalCompressedSize = 12673;
	SakuraCompressedData compressor;
	compressor.PatchDataInMemory(pSubtitleImageBlock, imageDataSize, true, false, originalCompressedSize);
	if( compressor.mDataSize > originalCompressedSize )
	{
		printf("PatchSubtitles: Compressed data size[%i] is too big. Needs to be smaller than %i.\n", compressor.mDataSize, originalCompressedSize);
		return false;
	}

	//Clear memory
	delete[] pSubtitleImageBlock;

	//Patch subtitle image info table
	sakuraData.WriteData(0x000a20c, (char*)subtitles, sizeof(subtitles), false);

	//Copy over the patched data
	logoData.WriteData(0x000196d8, compressor.mpCompressedData, compressor.mDataSize);

	printf("PatchSubtitles Succeeded.\n");

	return true;
}

bool PatchScreens(const string& rootSakuraTaisenDirectory, const string& patchedSakuraTaisenDirectory, const string& patchedDataDirectory)
{
	char buffer[MAX_PATH];

	struct ScreenFileInfo
	{
		enum EType
		{
			kType_Load,
			kType_BG,
		};

		const char* pName;
		int   offset;
		EType type;
	};

	ScreenFileInfo filesToPatch[] = 
	{
		"TITLE1", 0x13c0, ScreenFileInfo::kType_Load,
		"TITLE2", 0x13c0, ScreenFileInfo::kType_Load,
		"TITLE3", 0x13c0, ScreenFileInfo::kType_Load,
		"TITLE4", 0x13c0, ScreenFileInfo::kType_Load,
		"TITLE5", 0x13c0, ScreenFileInfo::kType_Load,
		"TITLE6", 0x13c0, ScreenFileInfo::kType_Load,
		"TITLE7", 0x13c0, ScreenFileInfo::kType_Load,
		"TITLE8", 0x13c0, ScreenFileInfo::kType_Load,
		"TITLE9", 0x13c0, ScreenFileInfo::kType_Load,
		"TITLE10",0x13c0, ScreenFileInfo::kType_Load,
		"LOAD",   0x1380, ScreenFileInfo::kType_Load,
		"BG1211", 0x50,   ScreenFileInfo::kType_BG,
		"BG1212", 0x50,   ScreenFileInfo::kType_BG,
		"BG1213", 0x50,   ScreenFileInfo::kType_BG
	};

	const int numTitleFiles = sizeof(filesToPatch)/sizeof(ScreenFileInfo);
	for(int i = 0; i < numTitleFiles; ++i )
	{
		snprintf(buffer, MAX_PATH, "%s\\SAKURA1\\%s.bin", rootSakuraTaisenDirectory.c_str(), filesToPatch[i].pName);
		FileData originalFile;
		if( !originalFile.InitializeFileData(FileNameContainer(buffer)) )
		{
			return false;
		}

		BmpToSaturnConverter patchedScreen;
		snprintf(buffer, MAX_PATH, "%s\\Screens\\%s.bmp", patchedDataDirectory.c_str(), filesToPatch[i].pName);

		const unsigned int tileDim = 8;
		if( !patchedScreen.ConvertBmpToSakuraFormat(string(buffer), false, 0, &tileDim, &tileDim) )
		{
			printf("PatchScreens: Couldn't convert image: %s.\n", buffer);
			return false;
		}

		const int expectedPaletteSize = 512;
		if( patchedScreen.mPalette.GetSize() != expectedPaletteSize )
		{
			printf("PatchScreens: Image needs to be in 8bpp format[256 Colors]: %s.\n", buffer);
			return false;
		}
		patchedScreen.PackTiles();

		snprintf(buffer, MAX_PATH, "%s\\SAKURA1\\%s.bin", patchedSakuraTaisenDirectory.c_str(), filesToPatch[i].pName);
		FileWriter outFile;
		if( !outFile.OpenFileForWrite(buffer) )
		{
			return false;
		}
		
		switch(filesToPatch[i].type)
		{
			case ScreenFileInfo::kType_Load:
			{
				const int offsetToImageData = filesToPatch[i].offset;
				outFile.WriteData(patchedScreen.mPalette.GetData(), patchedScreen.mPalette.GetSize());
				outFile.WriteData(originalFile.GetData() + expectedPaletteSize, offsetToImageData - expectedPaletteSize);
				outFile.WriteData(patchedScreen.mpPackedTiles, patchedScreen.mPackedTileSize);
			}break;

			case ScreenFileInfo::kType_BG:
			{
				outFile.WriteData(originalFile.GetData(), filesToPatch[i].offset);
				outFile.WriteData(patchedScreen.mpPackedTiles, patchedScreen.mPackedTileSize);
				outFile.WriteData(patchedScreen.mPalette.GetData(), patchedScreen.mPalette.GetSize());

				const long sizeOfPatchedData = patchedScreen.mPackedTileSize + patchedScreen.mPalette.GetSize();
				outFile.WriteData(originalFile.GetData() + sizeOfPatchedData, originalFile.GetDataSize() - (sizeOfPatchedData + filesToPatch[i].offset));
			}break;
		};
		

		if( outFile.GetFileSize() != originalFile.GetDataSize() )
		{
			printf("PatchScreens: Expected file size: %i, got %i.  For %s\n", originalFile.GetDataSize(), outFile.GetFileSize(), filesToPatch[i].pName);
			return false;
		}
	}

	printf("PatchScreen Successful!\n");

	return true;
}

void ExtractTiledImage(const char* pFileName, const char* pOutFileName, int dataOffset)
{
	const FileNameContainer inFileName(pFileName);
	const string outFileName(pOutFileName);

	FileData inFileData;
	if( !inFileData.InitializeFileData(inFileName) )
	{
		printf("ExtractTiledImage %s failed\n", pFileName);
		return;
	}

	ExtractImageFromData(inFileData.GetData(), 8*8*(320/8)*(224/8) + dataOffset, outFileName, inFileData.GetData(), 512, 8, 8, 320/8, 256, dataOffset, true, true);
}

void ExtractTiledImages(const string& rootSakuraDir, const string& outDir)
{
	const string outTitleDirectory = outDir + string("TitleFiles\\");
	const string outLoadDirectory  = outDir + string("LoadFiles\\");
	const string outMiscDirectory  = outDir + string("MiscFiles\\");
	const string outBGDirectory  = outDir + string("BGFiles\\");
	CreateDirectoryHelper(outDir);
	CreateDirectoryHelper(outTitleDirectory);
	CreateDirectoryHelper(outLoadDirectory);
	CreateDirectoryHelper(outMiscDirectory);
	CreateDirectoryHelper(outBGDirectory);
 
	//Extract title files
	char buffer0[1024];
	char buffer1[1024];
	for(int i = 1; i <= 10; ++i)
	{
		sprintf_s(buffer0, 1024, "%sSAKURA1\\TITLE%i.bin", rootSakuraDir.c_str(), i);
		sprintf_s(buffer1, 1024, "%sTITLE%i.bmp", outTitleDirectory.c_str(), i);
	
		ExtractTiledImage(buffer0, buffer1, 0x13c0);
		/*
		FileData inFileData;
		if( !inFileData.InitializeFileData(inFileName) )
		{
			printf("ExtractTiledImage failed\n");
			return;
		}
 
		const int dataOffset = 0x1380;
		ExtractImageFromData(inFileData.GetData(), 8*8*(320/8)*(224/8) + dataOffset, outFileName, inFileData.GetData(), 512, 8, 8, 320/8, 256, dataOffset, true, true);
		*/
	}
 
	//Extract LOAD.BIN
	{
		sprintf_s(buffer0, 1024, "%sSAKURA1\\LOAD.bin", rootSakuraDir.c_str());
		sprintf_s(buffer1, 1024, "%sLOAD.bmp", outLoadDirectory.c_str());
		ExtractTiledImage(buffer0, buffer1, 0x1380);
 
		sprintf_s(buffer0, 1024, "%sSAKURA1\\LOAD01.bin", rootSakuraDir.c_str());
		sprintf_s(buffer1, 1024, "%sLOAD01.bmp", outLoadDirectory.c_str());
		ExtractTiledImage(buffer0, buffer1, 0x13c0);
	}
 
	//Extract misc files
	{
		const char* fileNames[] = 
		{
			"OMAKE01",
			"OMAKE02",
			"OMAKE03",
			"OMAKE11",
			"OMAKE12",
			"OMAKE13",
			"WALL1",
			"IC01",
			"IC02",
			"IC03",
			"IC04",
			"IC06",
			"IC07",
			"IC08",
			"IC09",
			"IC1001",
			"IC1002",
			"IC1003",
			"IC1004",
			"IC1005",
			"IC1006",
			"KABE01",
			"KABE10",
			"KABE11",
			"KABE20",
			"KABE21",
			"KABE30",
			"KABE31",
			"KABE40",
			"KABE41",
			"KABE50",
			"KABE51",
			"KABE60",
			"KABE61",
			"KABE70",
			"KABE71",
			"KABE80",
			"KABE81",
			"KABE90",
			"KABE91",
			"WALL1"
		};
 
		const int numFiles = sizeof(fileNames)/sizeof(char*);
		for(int i = 0; i < numFiles; ++i)
		{
			sprintf_s(buffer0, 1024, "%sSAKURA1\\%s.bin", rootSakuraDir.c_str(), fileNames[i]);
			sprintf_s(buffer1, 1024, "%s%s.bmp", outMiscDirectory.c_str(), fileNames[i]);
			ExtractTiledImage(buffer0, buffer1, 0x1380);
		}
	}
 
	//BG Files
	{
		const string searchDir = rootSakuraDir + "SAKURA1\\";
		vector<FileNameContainer> allFiles;
		FindAllFilesWithinDirectory(searchDir, allFiles);
 
		vector<FileNameContainer> bmpFiles;
		GetAllFilesOfType(allFiles, "BG", bmpFiles);
 
		const size_t numFiles = bmpFiles.size();
		for(size_t i = 0; i < numFiles; ++i)
		{
			const FileNameContainer inFileName(bmpFiles[i].mFullPath.c_str());
			const string outFileName((outBGDirectory + bmpFiles[i].mNoExtension + ".bmp"));
 
			FileData inFileData;
			if( !inFileData.InitializeFileData(inFileName) )
			{
				printf("ExtractTiledImage %s failed\n", bmpFiles[i].mFullPath.c_str());
				return;
			}
 
			ExtractImageFromData(inFileData.GetData(), 8*8*(288/8)*(144/8) + 0x50, outFileName, inFileData.GetData() + 0x50 + (288*144), 512, 8, 8, 288/8, 256, 0x50, true, false);
		}
	}
}

void ConvertYabauseSaveToMednafen(const string& yabauseFileName, const string& outFileName)
{
	FileData yabauseFile;
	if( !yabauseFile.InitializeFileData( FileNameContainer(yabauseFileName.c_str()) ) )
	{
		printf("Unable to open %s\n", yabauseFileName.c_str());
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

	printf("Success!\n");
}

void AddShadowsToWKLText(const string& wklDirectory)
{
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(wklDirectory, allFiles);

	vector<FileNameContainer> bmpFiles;
	GetAllFilesOfType(allFiles, ".bmp", bmpFiles);

	for(const FileNameContainer& fileName : bmpFiles)
	{
		BitmapReader bmpData;
		if( !bmpData.ReadBitmap(fileName.mFullPath) )
		{
			printf("Failed to read %s\n", fileName.mFullPath.c_str());
			return;
		}

		const int numBytes        = bmpData.mBitmapData.mColorData.mSizeInBytes;
		char* pModifiedData       = new char[numBytes];
		const char* pOriginalData = bmpData.mBitmapData.mColorData.mpRGBA;
		memcpy_s(pModifiedData, numBytes, pOriginalData, numBytes);

		//Flip image
		const int imageHeight = abs(bmpData.mBitmapData.mInfoHeader.mImageHeight);
		for(int y = 0; y < imageHeight; ++y)
		{
			for(int x = 0; x < bmpData.mBitmapData.mInfoHeader.mImageWidth/2; ++x)
			{
				const int currentPixel = y*bmpData.mBitmapData.mInfoHeader.mImageWidth/2 + x;
				const int outPixel     = ((imageHeight - 1) - y)*bmpData.mBitmapData.mInfoHeader.mImageWidth/2 + x;
				pModifiedData[outPixel]  = pOriginalData[currentPixel];
			}
		}

		const char grayColor = 15;
		const char grayColor2 = 0xd;
		const char shadowColor = 2;
		//Add shadows
		for(int y = 0; y < imageHeight ; ++y)
		{	
			for(int x = 0; x < bmpData.mBitmapData.mInfoHeader.mImageWidth/2; ++x)
			{
				const int currentPixel = y*bmpData.mBitmapData.mInfoHeader.mImageWidth/2 + x;
				const int upperPixel   = y > 0 ?               (y-1)*bmpData.mBitmapData.mInfoHeader.mImageWidth/2 + x : -1;
				const int lowerPixel   = y + 1 < imageHeight ? (y+1)*bmpData.mBitmapData.mInfoHeader.mImageWidth/2 + x : -1;
				const char colorValue1 = (pModifiedData[currentPixel] & 0xf0) >> 4;
				if( colorValue1 == grayColor || colorValue1 == grayColor2)
				{
					//left
					if( x > 0 )
					{
						const char leftValue = pModifiedData[currentPixel - 1] & 0x0f;
						if( leftValue != grayColor && leftValue != grayColor2)
						{
							pModifiedData[currentPixel-1] = (pModifiedData[currentPixel-1]&0xf0) + shadowColor;
						}
					}

					//right
					const char rightValue = (pModifiedData[currentPixel] & 0x0f);
					if( rightValue != grayColor && rightValue != grayColor2)
					{
						pModifiedData[currentPixel] = (pModifiedData[currentPixel] & 0xf0) + shadowColor;
					}

					//up
					if( upperPixel > -1 )
					{
						const char upValue = (pModifiedData[upperPixel] & 0xf0) >> 4;
						if( upValue != grayColor && upValue != grayColor2)
						{
							pModifiedData[upperPixel] = (pModifiedData[upperPixel] & 0x0f) + (shadowColor << 4);
						}
					}

					//down
					if( lowerPixel > -1 )
					{
						const char downValue = (pModifiedData[lowerPixel] & 0xf0) >> 4;
						if( downValue != grayColor && downValue != grayColor2)
						{
							pModifiedData[lowerPixel] = (pModifiedData[lowerPixel] & 0x0f) + (shadowColor << 4);
						}
					}

				}//if( colorValue1 == 1)

				char colorValue2 = pModifiedData[currentPixel] & 0x0f;
				if( colorValue2 == grayColor || colorValue2 == grayColor2 )
				{
					//left
					const char leftValue = (pModifiedData[currentPixel] & 0xf0) >> 4;
					if( leftValue != grayColor && leftValue != grayColor2 )
					{
						pModifiedData[currentPixel] = (pModifiedData[currentPixel]&0x0f) + (shadowColor << 4);
					}

					//check right
					if( x + 1 < bmpData.mBitmapData.mInfoHeader.mImageWidth/2 )
					{
						const char rightValue = (pModifiedData[currentPixel + 1] & 0xf0) >> 4;
						if( rightValue != grayColor && rightValue != grayColor2 )
						{
							pModifiedData[currentPixel + 1] = (pModifiedData[currentPixel + 1] & 0x0f) + (shadowColor<<4);
						}
					}

					//up
					if( upperPixel > -1 )
					{
						const char upValue = (pModifiedData[upperPixel] & 0x0f);
						if( upValue != grayColor && upValue != grayColor2 )
						{
							pModifiedData[upperPixel] = (pModifiedData[upperPixel] & 0xf0) + shadowColor;
						}
					}

					//down
					if( lowerPixel > -1 )
					{
						const char downValue = (pModifiedData[lowerPixel] & 0x0f);
						if( downValue != grayColor && downValue != grayColor2)
						{
							pModifiedData[lowerPixel] = (pModifiedData[lowerPixel] & 0xf0) + shadowColor;
						}
					}
				}//if(colorValue2 == 1)

			} //for x
		}

		BitmapWriter outBitmap;
		outBitmap.CreateBitmap(fileName.mFullPath, bmpData.mBitmapData.mInfoHeader.mImageWidth, -abs(imageHeight), 4, pModifiedData, numBytes, 
							   bmpData.mBitmapData.mPaletteData.mpRGBA, bmpData.mBitmapData.mPaletteData.mSizeInBytes, true);
	}

	printf("Success\n");
}

void BoldWKLText(const string& wklDirectory)
{
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(wklDirectory, allFiles);

	vector<FileNameContainer> bmpFiles;
	GetAllFilesOfType(allFiles, ".bmp", bmpFiles);

	for (const FileNameContainer& fileName : bmpFiles)
	{
		BitmapReader bmpData;
		if (!bmpData.ReadBitmap(fileName.mFullPath))
		{
			printf("Failed to read %s\n", fileName.mFullPath.c_str());
			return;
		}

		const int numBytes = bmpData.mBitmapData.mColorData.mSizeInBytes;
		char* pModifiedData = new char[numBytes];
		const char* pOriginalData = bmpData.mBitmapData.mColorData.mpRGBA;
		memcpy_s(pModifiedData, numBytes, pOriginalData, numBytes);

		//Flip image
		const int imageHeight = abs(bmpData.mBitmapData.mInfoHeader.mImageHeight);
		for (int y = 0; y < imageHeight; ++y)
		{
			for (int x = 0; x < bmpData.mBitmapData.mInfoHeader.mImageWidth / 2; ++x)
			{
				const int currentPixel = y * bmpData.mBitmapData.mInfoHeader.mImageWidth / 2 + x;
				const int outPixel = ((imageHeight - 1) - y) * bmpData.mBitmapData.mInfoHeader.mImageWidth / 2 + x;
				pModifiedData[outPixel] = pOriginalData[currentPixel];
			}
		}

		const char grayColor = 1;
		const char boldedGray = 0xC;

		//Bold colors
		for (int y = 0; y < imageHeight; ++y)
		{
			if(y == 3)
			{
				int k = 0;
				++k;
			}

			for(int x = 0; x < bmpData.mBitmapData.mInfoHeader.mImageWidth / 2; ++x)
			{
				const int currentPixel = y * bmpData.mBitmapData.mInfoHeader.mImageWidth / 2 + x;
				
				const char colorValue1 = (pModifiedData[currentPixel] & 0xf0) >> 4;
				if( colorValue1 == grayColor )
				{
					pModifiedData[currentPixel] = (pModifiedData[currentPixel] & 0x0f) + (boldedGray << 4);
				
				}//if( colorValue1 == 1)

				char colorValue2 = pModifiedData[currentPixel] & 0x0f;
				if( colorValue2 == grayColor )
				{
					pModifiedData[currentPixel] = (pModifiedData[currentPixel] & 0xf0) + boldedGray;
				}

			} //for x
		}

		BitmapWriter outBitmap;
		outBitmap.CreateBitmap(fileName.mFullPath, bmpData.mBitmapData.mInfoHeader.mImageWidth, -abs(imageHeight), 4, pModifiedData, numBytes,
							   bmpData.mBitmapData.mPaletteData.mpRGBA, bmpData.mBitmapData.mPaletteData.mSizeInBytes, true);
	}

	printf("Success\n");
}

bool PatchTiledData(FileReadWriter& outFile, const string& fontSheetPath, const string& lookupTablePath, unsigned int fontSheetAddress, unsigned int lutAddress, 
					unsigned int fontCompressedSize, unsigned int lutCompressedSize)
{		
	//Create image data
	const unsigned int tileDim = 8;
	BmpToSaturnConverter fontSheet;
	if( !fontSheet.ConvertBmpToSakuraFormat(fontSheetPath, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim) )
	{
		return false;
	}

	fontSheet.PackTiles();

	SakuraCompressedData fontSheetCompressedData;
	fontSheetCompressedData.PatchDataInMemory(fontSheet.mpPackedTiles, fontSheet.mPackedTileSize, true, false, fontCompressedSize);
	if( fontSheetCompressedData.mDataSize > fontCompressedSize )
	{
		printf("Compressed credits image is too big");
		return false;
	}
	//Done with image data

	//Write out new image data
	outFile.WriteData(fontSheetAddress, fontSheetCompressedData.mpCompressedData, fontSheetCompressedData.mDataSize, false);

	//Open tile map containing screen layout
	FILE* tileFile = nullptr;
	errno_t errorValue = fopen_s(&tileFile, lookupTablePath.c_str(), "r");
	if( errorValue )
	{
		printf("Unable to open %s\n", lookupTablePath.c_str());
		return false;
	}

	std::vector<int> tileEntries;
	while( feof(tileFile) != EOF )
	{
		char comma;
		int tileIndex;

		if( fscanf_s(tileFile, "%i%c", &tileIndex, &comma, 2) == EOF )
		{
			break;
		}

		tileEntries.push_back( SwapByteOrder(tileIndex) );
	}

	fclose(tileFile);

	if( tileEntries.size() != (320 / 8)*(224 / 8) )
	{
		printf("IntroCredits.csv does not have the expected number of tiles [40 * 28].  Found %zu instead.\n", tileEntries.size());
		return false;
	}

	SakuraCompressedData lutCompressed;
	lutCompressed.PatchDataInMemory((char*)tileEntries.data(), tileEntries.size()*sizeof(int), true, false, lutCompressedSize);
	if( lutCompressed.mDataSize != lutCompressedSize )
	{
		printf("Lookup table is too big.  Should be 916, is %ul\n", lutCompressed.mDataSize);
		return false;
	}

	//Write out compressed lut table
	outFile.WriteData(lutAddress, lutCompressed.mpCompressedData, lutCompressed.mDataSize, false);
	
	return true;
}

bool PatchIntroCredits(const string& patchedSakuraTaisenDirectory, const string& patchedDataDirectory)
{
	printf("Patching Intro Credits\n");

	const string logoFilePath = patchedSakuraTaisenDirectory + "\\SAKURA1\\LOGO.SH2";
	FileReadWriter logoFile;
	if( !logoFile.OpenFile(logoFilePath) )
	{
		return false;
	}

	const string creditsPath = patchedDataDirectory + "\\VDP2\\IntroCredits.bmp";
	const string creditsTileMapPath = patchedDataDirectory + "\\VDP2\\IntroCredits.csv";
	if( !PatchTiledData(logoFile, creditsPath, creditsTileMapPath, 0x00032edc, 0x000349c8, 6889, 916) )
	{
		return false;
	}

	const string sakuraWarsLogo = patchedDataDirectory + "\\VDP2\\SakuraWarsLogo.bmp";
	const string sakuraWarsLogoTileMapPath = patchedDataDirectory + "\\VDP2\\SakuraWarsLogo.csv";
	if( !PatchTiledData(logoFile, sakuraWarsLogo, sakuraWarsLogoTileMapPath, 0x00035924, 0x00035c30, 777, 208) )
	{
		return false;
	}
	
	printf("Patching Intro Credits Succeeded!\n");

	return true;
}

bool PatchIntroLogo(const string& patchedSakuraDirectory, const string& patchedDataDirectory)
{
	printf("Patch Intro Logo Screen\n");

	const string logoFilePath = patchedSakuraDirectory + "\\SAKURA1\\LOGO.SH2";
	FileData logoFileData;
	if( !logoFileData.InitializeFileData( FileNameContainer(logoFilePath.c_str()) ) )
	{
		return false;
	}

	FileReadWriter logoFile;
	if( !logoFile.OpenFile(logoFilePath) )
	{
		return false;
	}

#if 0
	const int originalCompressedSize = 15123;
	const int offsetToImage = 0x00025410;
	PRSDecompressor uncompressedData;
	uncompressedData.UncompressData( (void*)(logoFileData.GetData() + offsetToImage), logoFileData.GetDataSize() - offsetToImage);

	const string sakuraImagePath = patchedDataDirectory + "\\SakuraLogo_Sakura.bmp";
	const string warsImagePath = patchedDataDirectory + "\\SakuraLogo_Wars.bmp";

	//Create image data
	BmpToSakuraConverter sakuraImage;
	if( !sakuraImage.ConvertBmpToSakuraFormat(sakuraImagePath, false, BmpToSakuraConverter::CYAN) )
	{
		return false;
	}

	//Wars
	BmpToSakuraConverter warsImage;
	if( !warsImage.ConvertBmpToSakuraFormat(warsImagePath, false, BmpToSakuraConverter::CYAN) )
	{
		return false;
	}


	memcpy_s(uncompressedData.mpUncompressedData + 0x0000a200, sakuraImage.GetImageDataSize(), sakuraImage.GetImageData(), sakuraImage.GetImageDataSize());
	memcpy_s(uncompressedData.mpUncompressedData + 0x0000e740, warsImage.GetImageDataSize(), warsImage.GetImageData(), warsImage.GetImageDataSize());

	FileWriter f;
	f.OpenFileForWrite("d:\\rizwan\\precompressed.bin");
	f.WriteData(uncompressedData.mpUncompressedData, uncompressedData.mUncompressedDataSize);

	SakuraCompressedData fontSheetCompressedData;
	fontSheetCompressedData.PatchDataInMemory(uncompressedData.mpUncompressedData, uncompressedData.mUncompressedDataSize, true, false, originalCompressedSize);
	if( fontSheetCompressedData.mDataSize > originalCompressedSize )
	{
		printf("Compressed credits image is too big");
		return false;
	}
	
	//Write out new image data
	logoFile.WriteData(offsetToImage, fontSheetCompressedData.mpCompressedData, fontSheetCompressedData.mDataSize, false);
#else
	const string patchDataPath = patchedDataDirectory + "\\PrecompressedIntroLogo.bin";
	FileData patchData;
	if( !patchData.InitializeFileData( FileNameContainer(patchDataPath.c_str()) ) )
	{
		return false;
	}

	const int originalCompressedSize = 15123;
	const int offsetToImage = 0x00025410;

	if( patchData.GetDataSize() > originalCompressedSize )
	{
		printf("Compressed credits image is too big");
		return false;
	}
	logoFile.WriteData(offsetToImage, (char*)patchData.GetData(), patchData.GetDataSize(), false);
#endif
	//Done with image data

	
	//Fix formatting
	unsigned char sakuraFormatting[] = {0xff, 0x80, 
										0xff, 0xd0, 
										0x00, 0x98, 
										0x00, 0x38};
	logoFile.WriteData(0x628a, (char*)sakuraFormatting, sizeof(sakuraFormatting), false);

	//Move the "Tai" off screen
	unsigned char taiFormatting[] = {0x00, 0x00,
									0x00, 0xff};

	logoFile.WriteData(0x62b4, (char*)taiFormatting, sizeof(sakuraFormatting), false);
	
	//Wars
	unsigned char warsFormatting[] = {0xff, 0xf0,
									0xff, 0xe0};
	logoFile.WriteData(0x62de, (char*)warsFormatting, sizeof(warsFormatting), false);

	//Patch tiled image
	if( 0 )
	{
		const string fontSheetPath = patchedDataDirectory + "\\SakuraLogo_Small.bmp";

		//Create image data
		const unsigned int tileDim = 8;
		BmpToSaturnConverter fontSheet;
		if( !fontSheet.ConvertBmpToSakuraFormat(fontSheetPath, false, BmpToSaturnConverter::CYAN, &tileDim, &tileDim) )
		{
			return false;
		}

		fontSheet.PackTiles();

		SakuraCompressedData fontSheetCompressedData;
		fontSheetCompressedData.PatchDataInMemory(fontSheet.mpPackedTiles, fontSheet.mPackedTileSize, true, false, 1644);
		if( fontSheetCompressedData.mDataSize > 1644 )
		{
			printf("Compressed swirl sakura image is too big");
			return false;
		}
		//Done with image data

		//Write out new image data
		logoFile.WriteData(0x00024b8c, fontSheetCompressedData.mpCompressedData, fontSheetCompressedData.mDataSize, false);

		//Open tile map containing screen layout
		const string lookupTablePath = patchedDataDirectory + "\\VDP2\\IntroLogo.csv";
		FILE* tileFile = nullptr;
		errno_t errorValue = fopen_s(&tileFile, lookupTablePath.c_str(), "r");
		if( errorValue )
		{
			printf("Unable to open %s\n", lookupTablePath.c_str());
			return false;
		}

		std::vector<int> tileEntries;
		while( feof(tileFile) != EOF )
		{
			char comma;
			int tileIndex;

			if( fscanf_s(tileFile, "%i%c", &tileIndex, &comma, 2) == EOF )
			{
				break;
			}

			tileEntries.push_back( SwapByteOrder(tileIndex) );
		}

		fclose(tileFile);

		/*
		if( tileEntries.size() != (320 / 8)*(224 / 8) )
		{
			printf("IntroCredits.csv does not have the expected number of tiles [40 * 28].  Found %u instead.\n", tileEntries.size());
			return false;
		}*/

		//Write out compressed lut table
		const int lutAddress = 0x000251f8;
		logoFile.WriteData(lutAddress, (char*)tileEntries.data(), tileEntries.size()*sizeof(int), false);
	}
	return true;
}

bool CopyChangesFromTrekkies(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	printf("CopyChangesFromTrekkies\n");

	const string sakuraFilePath = inPatchedSakuraDirectory + "\\SAKURA";
	FileReadWriter sakuraFile;
	if( !sakuraFile.OpenFile(sakuraFilePath) )
	{
		return false;
	}

	const string trekkiesFilePath = inTranslatedDataDirectory + string("\\FromTrekkies") + (GIsDisc2 ? string("\\Disc2") : string("\\Disc1")) + "\\SAKURA";
	FileData trekkiesFile;
	if( !trekkiesFile.InitializeFileData("SAKURA", trekkiesFilePath.c_str()) )
	{
		return false;
	}

	{
		unsigned long offset = GIsDisc2 ? 0x51D20 : 0x51BF0;
		unsigned long size = 0x2FC;
		const char* pSourceData = trekkiesFile.GetData() + offset;
		sakuraFile.WriteData(offset, pSourceData, size, false);
	}

	{
		unsigned long offset = GIsDisc2 ? 0x521DC : 0x5209F;
		unsigned long size = 0x3A3;
		const char* pSourceData = trekkiesFile.GetData() + offset;
		sakuraFile.WriteData(offset, pSourceData, size, false);
	}

	return true;
}

bool DumpBitmap(const string& inputFilePath, const string& outDir)
{
	BmpToSaturnConverter inputImage;
	if( !inputImage.ConvertBmpToSakuraFormat(inputFilePath, false) )
	{
		return false;
	}

	FileNameContainer inFileName(inputFilePath.c_str());
	
	//Output image data
	{
		std::string outfileName = outDir + inFileName.mNoExtension + "_Image.bin";
		FileWriter outFile;
		if( !outFile.OpenFileForWrite(outfileName) )
		{
			return false;
		}

		outFile.WriteData(inputImage.GetImageData(), inputImage.GetImageDataSize());
	}

	//Output palette data
	{
		std::string outfileName = outDir + inFileName.mNoExtension + "_Palette.bin";
		FileWriter outFile;
		if (!outFile.OpenFileForWrite(outfileName))
		{
			return false;
		}

		outFile.WriteData(inputImage.mPalette.GetData(), inputImage.mPalette.GetSize());
	}

	return true;
}

bool PatchGame(const string& rootSakuraTaisenDirectory, 
			   const string& patchedSakuraTaisenDirectory, 
			   const string& translatedTextDirectory, 
			   const string& fontSheetFileName, 
			   const string& ,//originalPaletteFileName, 
			   const string& inMainMainFontSheetPath, 
			   const string& inMainMenuTranslatedBgnd, 
			   const string& inPatchedOptionsImage, 
			   const string& inTranslatedDataDirectory, 
			   const string& inExtractedWklDir)
{
	//Create temp work directory
	string tempDir;
	if( !CreateTemporaryDirectory(tempDir) )
	{
		printf("Cannot patch game.  Could not create temp work directory.  Error: (%d)\n", GetLastError());
		return false;
	}

	//Step 0
	if( !CopyOriginalFiles(rootSakuraTaisenDirectory, patchedSakuraTaisenDirectory) )
	{
		printf("CopyOriginalFiles failed.  Patch unsuccessful.\n");
		return false;
	}

//	PatchIntroLogo(patchedSakuraTaisenDirectory, inTranslatedDataDirectory);

	//Step 1
	if( !CreateTranslatedFontSheet(fontSheetFileName, tempDir) )
	{
		printf("CreateTranslatedFontSheet failed.  Patch unsuccessful.\n");
		return false;
	}

	const string slgFontFileName = inTranslatedDataDirectory + string("8x12_SLG.bmp");
	if (!CreateTranslatedFontSheet(slgFontFileName, tempDir, true, true))
	{
		printf("CreateTranslatedFontSheet failed.  Patch unsuccessful.\n");
		return false;
	}

	//Step2
	const string newPaletteFileName = tempDir + PatchedPaletteName; //Created by CreateTranslatedFontSheet
	/**
	if( !PatchPalettes(rootSakuraTaisenDirectory, originalPaletteFileName, newPaletteFileName, patchedSakuraTaisenDirectory, true) )
	{
		printf("PatchPalettes failed.  Patch unsuccessful.\n");
		return false;
	}*/

	//Step 3
	if( !FixupSakura(patchedSakuraTaisenDirectory, inPatchedOptionsImage, newPaletteFileName, inTranslatedDataDirectory) )
	{
		printf("FixupSakura failed.  Patch unsuccessful.\n");
		return false;
	}

	//Step 4
	const string slgFontPaletteFileName = FIX_SLG_FONT_DRAWING_SIZE ? newPaletteFileName + SlgFontFileSuffix : newPaletteFileName;
	if( !FixupSLG(rootSakuraTaisenDirectory, patchedSakuraTaisenDirectory, inTranslatedDataDirectory, slgFontPaletteFileName, tempDir) )
	{
		printf("FixupSLG failed.  Patch unsuccessful.\n");
		return false;
	}

	//Step 5
	if( !PatchWKLFiles(rootSakuraTaisenDirectory, patchedSakuraTaisenDirectory, inTranslatedDataDirectory, tempDir, inExtractedWklDir) )
	{
		printf("PatchWKLFiles failed. Patch unsuccessful.\n");
		return false;
	}

	//Step 6
	if( !PatchTMapSP(patchedSakuraTaisenDirectory, inTranslatedDataDirectory) )
	{
		printf("PatchTMapSP failed.  Patch unsuccessful.\n");
		return false;
	}

	//Step 7
	const string translatedKNJPath = tempDir + PatchedKNJName; //Created by CreateTranslatedFontSheet
	const string patchedKNJPath    = patchedSakuraTaisenDirectory;
	if( !PatchKNJ(rootSakuraTaisenDirectory, translatedKNJPath, patchedKNJPath) )
	{
		printf("PatchKNJ failed.  Patch unsuccessful.\n");
		return false;
	}

	//Step 8
	if( !InsertText(rootSakuraTaisenDirectory, translatedTextDirectory, patchedSakuraTaisenDirectory, true) )
	{
		printf("Insert Text failed.  Patch unsuccessful.\n");
		return false;
	}

	//Step 7
	const string translatedMainMenuFontSheetPath = tempDir + "TranslatedMainMenuFontSheet";
	if( !PatchMainMenu(patchedSakuraTaisenDirectory, inMainMainFontSheetPath, translatedMainMenuFontSheetPath, inMainMenuTranslatedBgnd, inTranslatedDataDirectory, tempDir) )
	{
		printf("Patching MainMenu LOGO.SH2 failed.\n");
		return false;
	}

	//Step 8
	if( !PatchMiniGames(rootSakuraTaisenDirectory, patchedSakuraTaisenDirectory, inTranslatedDataDirectory) )
	{
		printf("Patching MiniGames failed.\n");
		return false;
	}

	//Step 9
	if( !PatchSubtitles(rootSakuraTaisenDirectory, patchedSakuraTaisenDirectory, inTranslatedDataDirectory) )
	{
		printf("Patching Subtitles failed.\n");
		return false;
	}

	//Step 10
	if( !PatchScreens(rootSakuraTaisenDirectory, patchedSakuraTaisenDirectory, inTranslatedDataDirectory) )
	{
		printf("Patching Screens failed.\n");
		return false;
	}

	//Step 11
	if( !PatchFACEFiles(rootSakuraTaisenDirectory, patchedSakuraTaisenDirectory, inTranslatedDataDirectory) )
	{
		printf("Patching FACEs failed.\n");
		return false;
	}

	//Step 12
	if( !PatchIntroCredits(patchedSakuraTaisenDirectory, inTranslatedDataDirectory) )
	{
		printf("Patching Intro Credits failed.\n");
		return false;
	}

	//Step 13
	if( !PatchStatusScreen(patchedSakuraTaisenDirectory, inTranslatedDataDirectory) )
	{
		printf("Patching Status Screen Failed.\n");
		return false;
	}

	//Step 14
	if( !PatchLoadScreen(patchedSakuraTaisenDirectory, inTranslatedDataDirectory) )
	{
		printf("Patching LoadSave Screen Failed.\n");
		return false;
	}

	//Step 15
	if( !PatchTheEndLogo(patchedSakuraTaisenDirectory, inTranslatedDataDirectory) )
	{
		printf("Patching End Logo Failed.\n");
		return false;
	}

	//Step 16
	if( !CopyChangesFromTrekkies(patchedSakuraTaisenDirectory, inTranslatedDataDirectory) )
	{
		printf("Unabled to copy changes from Trekkies\n");
		return false;
	}
	//Step 14
	/*if( !PatchIntroLogo(patchedSakuraTaisenDirectory, inTranslatedDataDirectory) )
	{
		printf("Patching IntroLogo Failed.\n");
		return false;
	}*/
	
	printf("Patching Successful!\n");
	return true;
}

void PrintHelp()
{
	printf("usage: SakuraTaisen [command]\n");
	printf("Commands:\n");
	printf("ExtractRawText rootSakuraTaisenDirectory outDirectory\n");
	printf("ExtractFontSheets rootSakuraTaisenDirectory paletteFileName outDirectory\n");
	printf("ExtractText rootSakuraTaisenDirectory paletteFileName outDirectory\n");
	printf("CreateTranslatedFontSheets translatedFontSheet outDirectory\n");
	printf("ConvertTranslatedText translatedTextDir outDirectory\n");
	printf("PatchGameKNJ rootSakuraTaisenDirectory newKNJ outDirectory\n");
	printf("PatchPalettes rootSakuraTaisenDirectory originalPalette newPalette outDirectory\n");
	printf("InsertText rootSakuraTaisenDirectory translatedText outDirectory\n");
	printf("FindDuplicateText dialogDirectory outFile\n");
	printf("FindDialogOrder rootSakuraTaisenDirectory outDirectory\n");
	printf("ParseEVTFiles rootSakuraTaisenDirectory\n");
	printf("CreateTBLSpreadsheets dialogImageDirectory duplicatesFile sakura1Directory translatedTextDirectory forRelease\n");
	printf("CreateMesSpreadsheets dialogImageDirectory rootSakuraTaisenDirectory\n");
	printf("CreateWKLSpreadsheets dialogImageDirectory duplicatesFile rootSakuraTaisenDirectory\n");
	printf("CreateTMapSpSpreadsheet imageDirectory\n");
	printf("ExtractImages fileName paletteFile width height outDirectory\n");
	printf("ExtractPalettedImage fileName paletteFile offset width height numTilesPerRow numColors[256, 128] pngOrBmp[0 png, 1 bmp] outDirectory\n");
	printf("ExtractFCEFiles rootSakuraTaisenDirectory paletteFile outDirectory\n");
	printf("ExtractFACEFiles rootSakuraTaisenDirectory outDirectory\n");
	printf("ExtractWKLFiles rootSakuraTaisenDirectory outDirectory\n");
	printf("ExtractTMapSP rootSakuraTaisenDirectory paletteFile outDirectory\n");
	printf("PatchTMapSP sakuraDirectory patchDataPath\n");
	printf("PrintPaletteColors fileName\n");
	printf("CompressFile inFilePath outFilePath\n");
	printf("FindCompressedData compressedFile uncompressedFile outDirectory\n");
	printf("FindCompressedDataInDir inDirectory uncompressedFile outDirectory\n");
	printf("ExtractMiniGameData rootSakuraDirectory patchedDataDirectory outDirectory\n");
	printf("ExtractStatusScreen rootSakuraDirectory patchedDataDirectory outDirectory\n");
	printf("YabauseToMednafen yabauseFilePath outFile\n");
	printf("DumpBitmap inputFilePath outDirectory\n");
	printf("PatchGame isDisc2 rootSakuraTaisenDirectory patchedSakuraTaisenDirectory translatedTextDirectory fontSheet originalPalette patchedTMapSpDataPath mainMenuFontSheetPath mainMenuBgndPatchedImage optionsImagePatched translatedDataDirectory extractedWklDir\n");
}

void SizeTest(const string& wklDir1, const string& wklDir2)
{
	vector<FileNameContainer> allFiles1;
	FindAllFilesWithinDirectory(wklDir1, allFiles1);

	vector<FileNameContainer> bmpFiles1;
	GetAllFilesOfType(allFiles1, ".bmp", bmpFiles1);

	vector<FileNameContainer> allFiles2;
	FindAllFilesWithinDirectory(wklDir2, allFiles2);

	vector<FileNameContainer> bmpFiles2;
	GetAllFilesOfType(allFiles2, ".bmp", bmpFiles2);

	for(FileNameContainer& file1 : bmpFiles1)
	{
		FileData fileData1;
		fileData1.InitializeFileData(file1);

		for (FileNameContainer& file2 : bmpFiles2)
		{
			if(file2.mFileName == file1.mFileName)
			{
				FileData fileData2;
				if( fileData2.InitializeFileData(file2) )
				{
					BitmapReader bmpData1;
					if( !bmpData1.ReadBitmap(file1.mFullPath) )
					{
						break;
					}

					BitmapReader bmpData2;
					if (!bmpData2.ReadBitmap(file2.mFullPath))
					{
						break;
					}

					if( bmpData1.mBitmapData.mInfoHeader.mImageWidth != bmpData2.mBitmapData.mInfoHeader.mImageWidth || 
					    abs(bmpData1.mBitmapData.mInfoHeader.mImageHeight) != abs(bmpData2.mBitmapData.mInfoHeader.mImageHeight) )
					{
						printf("%s\n", file1.mFileName.c_str());
					}
				}
			}
		}
	}
}

int main(int argc, char *argv[])
{
#if 0

#else
	if(argc == 1)
	{
		PrintHelp();
		return 1;
	}

	const string command(argv[1]);
	if( command == string("ExtractRawText") && argc == 4 )
	{
		const char*  pSearchDirectory = argv[2];
		const string outDirectory     = string(argv[3]) + Seperators;

		//Find all files within the requested directory
		vector<FileNameContainer> allFiles;
		FindAllFilesWithinDirectory(string(pSearchDirectory), allFiles);

		DumpSakuraText(allFiles, outDirectory);
	}
	else if( command == string("ExtractFontSheets") && argc == 5 )
	{
		const char*  pSearchDirectory = argv[2];
		const string paletteFileName  = string(argv[3]);
		const string outDirectory     = string(argv[4]) + Seperators;

		//Find all files within the requested directory
		vector<FileNameContainer> allFiles;
		FindAllFilesWithinDirectory(string(pSearchDirectory), allFiles);

		ExtractAllFontSheets(allFiles, paletteFileName, outDirectory);
	}
	else if( command == string("ExtractImages") )
	{
		const string fileName         = string(argv[2]);
		const string paletteFileName  = string(argv[3]);
		const string width            = string(argv[4]);
		const string height           = string(argv[5]);
		const string outDirectory     = string(argv[6]) + Seperators;

		ExtractImages(fileName, paletteFileName, std::atoi(width.c_str()), std::atoi(height.c_str()), outDirectory);
	}
	else if( command == string("ExtractText" ) && argc == 5 )
	{
		const string searchDirectory   = string(argv[2]);
		const string paletteFileName   = string(argv[3]);
		const string outDirectory      = string(argv[4]) + Seperators;

		ExtractText(searchDirectory, paletteFileName, outDirectory);
	}
	else if (command == "CreateTranslatedFontSheets" && argc == 4 )
	{
		const string translatedFontSheet = string(argv[2]);
		const string outDirectory        = string(argv[3]) + Seperators;

		CreateTranslatedFontSheet(translatedFontSheet, outDirectory);
	}
	else if (command == "ConvertTranslatedText" && argc == 4 )
	{
		const string translatedTextDir = string(argv[2]);
		const string outDirectory      = string(argv[3]) + Seperators;

		ConvertTranslatedText(translatedTextDir, outDirectory);
	}
	else if( command == "PatchGameKNJ" && argc == 5 )
	{
		const string searchDirectory = string(argv[2]);
		const string newKNJ          = string(argv[3]);
		const string outDir          = string(argv[4]) + Seperators;

		PatchKNJ(searchDirectory, newKNJ, outDir);
	}
	else if( command == "PatchPalettes" && argc == 6 )
	{
		const string searchDirectory = string(argv[2]);
		const string origPalette     = string(argv[3]);
		const string newPalette      = string(argv[4]);
		const string outDir          = string(argv[5]) + Seperators;

		PatchPalettes(searchDirectory, origPalette, newPalette, outDir);

		//FixupSakura(outDir);
	}
	else if( command == "InsertText" && argc == 5 )
	{
		const string searchDirectory = string(argv[2]);
		const string translatedText  = string(argv[3]);
		const string outDir          = string(argv[4]) + Seperators;

		InsertText(searchDirectory, translatedText, outDir);
	}
	else if( command == "FindDuplicateText"  && argc == 4 )
	{
		const string searchDirectory = string(argv[2]);
		const string outputFile      = string(argv[3]);

		FindDuplicateText(searchDirectory, outputFile);
	}
	else if( command == "FindDuplicateWKLText"  && argc == 4 )
	{
		const string searchDirectory = string(argv[2]);
		const string outputFile      = string(argv[3]);

		FindDuplicateText(searchDirectory, outputFile);
	}
	else if( command == "FindDialogOrder" && argc == 4 )
	{
		const string searchDirectory = string(argv[2]);
		const string outputFile      = string(argv[3]) + Seperators;

		OutputDialogOrder(searchDirectory, outputFile);
	}
	else if( command == "PatchGame" && argc == 13 )
	{
		GIsDisc2                                  = string(argv[2]) == "1" ? true : false;
		const string rootSakuraTaisenDirectory    = string(argv[3]) + Seperators;
		const string patchedSakuraTaisenDirectory = string(argv[4]) + Seperators;
		const string translatedTextDirectory      = string(argv[5]);
		const string fontSheet                    = string(argv[6]);
		const string originalPalette              = string(argv[7]);
		const string mainMenuFontSheetPath        = string(argv[8]);
		const string mainMenuTranslatedBgnd       = string(argv[9]);
		const string patchedOptionsImage          = string(argv[10]);
		const string translatedDataDirectory      = string(argv[11]) + Seperators;
		const string extractedWklDirectory        = string(argv[12]) + Seperators;

		PatchGame(rootSakuraTaisenDirectory, patchedSakuraTaisenDirectory, translatedTextDirectory, fontSheet, originalPalette, mainMenuFontSheetPath, mainMenuTranslatedBgnd, patchedOptionsImage, 
				  translatedDataDirectory, extractedWklDirectory);
	}
	else if(command == "CreateTBLSpreadsheets" && argc == 7 )
	{
		const string dialogImageDirectory = string(argv[2]);
		const string duplicatesFile       = string(argv[3]);
		const string sakura1Directory     = string(argv[4]) + Seperators;
		const string translatedDirectory  = string(argv[5]) + Seperators;
		const bool bForRelease            = atoi(argv[6]) != 0;

		CreateTBLSpreadsheets(dialogImageDirectory, duplicatesFile, sakura1Directory, translatedDirectory, bForRelease);
	}
	else if(command == "CreateMesSpreadsheets" && argc == 4 )
	{
		const string dialogImageDirectory = string(argv[2]);
		const string sakuraRootDirectory  = string(argv[3]) + Seperators;

		CreateMesSpreadSheets(dialogImageDirectory, sakuraRootDirectory);
	}
	else if(command == "CreateWKLSpreadsheets" && argc == 5 )
	{
		const string dialogImageDirectory = string(argv[2]);
		const string duplicatesFile       = string(argv[3]);
		const string sakuraDirectory      = string(argv[4]) + Seperators;

		CreateWKLSpreadSheets(dialogImageDirectory, duplicatesFile, sakuraDirectory);
	}
	else if(command == "CreateTMapSpSpreadsheet" && argc == 3 )
	{
		const string imageDirectory = string(argv[2]);

		CreateTMapSpSpreadsheet(imageDirectory);
	}
	else if(command == "ExtractPalettedImage" && argc == 11 )
	{
		const string fileName       = string(argv[2]);
		const string paletteFile    = string(argv[3]);
		const int    offset         = atoi(argv[4]);
		const int    width          = atoi(argv[5]);
		const int    height         = atoi(argv[6]);
		const int    numTilesPerRow = atoi(argv[7]);
		const int    numColors      = atoi(argv[8]);
		const int    pngOrBmp       = atoi(argv[9]);
		const string outDirectory   = string(argv[10]) + Seperators;

		ExtractPalettedImage(fileName, paletteFile, offset, width, height, numTilesPerRow, numColors, pngOrBmp == 1, outDirectory);
	}
	else if(command == "ExtractFCEFiles" && argc == 5 )
	{
		const string rootSakuraTaisenDirectory = string(argv[2]) + Seperators;
		const string paletteFile               = string(argv[3]);
		const string outDirectory              = string(argv[4]) + Seperators;

		ExtractFCEFiles(rootSakuraTaisenDirectory, paletteFile, outDirectory);
	}
	else if(command == "ExtractFACEFiles" && argc == 4 )
	{
		const string rootSakuraTaisenDirectory = string(argv[2]) + Seperators + string("SAKURA2\\");
		const string outDirectory              = string(argv[3]) + Seperators;

		ExtractFACEFiles(rootSakuraTaisenDirectory, outDirectory);
	}
	else if(command == "ExtractWKLFiles" && argc == 4 )
	{
		const string rootSakuraTaisenDirectory = string(argv[2]) + Seperators + string("SAKURA2\\");
		const string outDirectory              = string(argv[3]) + Seperators;

		ExtractWKLFiles(rootSakuraTaisenDirectory, outDirectory);
	}
	else if(command == "ExtractTMapSP" && argc == 5 )
	{
		const string rootSakuraTaisenDirectory = string(argv[2]) + Seperators;
		const string paletteFile               = string(argv[3]);
		const string outDirectory              = string(argv[4]) + Seperators;

		ExtractTMapSP(rootSakuraTaisenDirectory, paletteFile, outDirectory);
	}
	else if(command == "PatchTMapSP" && argc == 4 )
	{
		const string sakuraDirectory = string(argv[2]) + Seperators;
		const string patchDataPath   = string(argv[3]) + Seperators;

		PatchTMapSP(sakuraDirectory, patchDataPath);
	}
	else if(command == "ParseEVTFiles" && argc == 3)
	{
		const string sakuraDirectory = string(argv[2]) + Seperators;
	
		std::vector<EVTFileData> outData;
		ParseEvtFiles(sakuraDirectory, outData);
	}
	else if(command == "CompressFile" && argc == 4)
	{
		const string inFile(argv[2]);
		const string outFile(argv[3]);

		CompressFile(inFile, outFile);
	}
	else if(command == "FindCMPData" && argc == 5 )
	{
		const string compressedFile(argv[2]);
		const string uncompressedFile(argv[3]);
		const string outDirectory = string(argv[4]) + Seperators;

		FindCMPData(compressedFile, uncompressedFile, outDirectory);
		
		return -1;
	}
	else if(command == "FindCompressedData" && argc == 5 )
	{
		const string compressedFile(argv[2]);
		const string uncompressedFile(argv[3]);
		const string outDirectory = string(argv[4]) + Seperators;

		FindCompressedData(compressedFile, uncompressedFile, outDirectory);
	}
	else if(command == "FindCompressedDataInDir" && argc == 5 )
	{
		const string inDirectory(argv[2]);
		const string uncompressedFile(argv[3]);
		const string outDirectory = string(argv[4]) + Seperators;

		vector<FileNameContainer> fileNames;
		FindAllFilesWithinDirectory( inDirectory, fileNames);

		for(FileNameContainer& fc : fileNames)
		{	
			if( fc.mFileName == "SAKURA4" || fc.mFileName == "VCEADV" || fc.mFileName == "OTOMEADP.DAT" || fc.mFileName == "SLG1ADP.DAT" || fc.mFileName == "MINIADP.DAT" ||
			    fc.mFileName.find_first_of("ADV") != std::string::npos)
			{
				continue;
			}
			printf("Searching %s\n", fc.mFullPath.c_str());
			FindCompressedData(fc.mFullPath, uncompressedFile, outDirectory);
		}
	}
	else if(command == "PrintPaletteColors" && argc == 3 )
	{
		const string inFile(argv[2]);
		
		PrintPaletteColors(inFile);
	}
	else if(command == "DumpTranslationFilesWithoutUnusedLines" && argc == 5)
	{
		const string rootSakuraTaisenDirectory(argv[2]);
		const string translatedTextDirectory(argv[3]);
		const string outDirectory = string(argv[4]) + Seperators;

		DumpTranslationFilesWithoutUnusedLines(rootSakuraTaisenDirectory, translatedTextDirectory, outDirectory);
	}
	else if(command == "CopySharedWklImages" && argc == 4)
	{
		const string sourceDir(argv[2]);
		const string outDir(argv[3]);

		CopySharedWklImages(sourceDir, outDir);
	}
	else if(command == "ExtractMiniGameData" && argc == 5)
	{
		const string rootSakuraDir     = string(argv[2]) + Seperators;
		const string translatedDataDir = string(argv[3]) + Seperators;
		const string outDir            = string(argv[4]) + Seperators;

		ExtractMinigameData(rootSakuraDir, translatedDataDir, outDir);
	}
	else if( command == "CreateFontSheetEntries" && argc == 3 )
	{
		const string textFile = string(argv[2]);

		CreateFontSheetEntries(textFile, false);
	}
	else if( command == "ExtractSubtitles" && argc == 4 )
	{ 
		const string rootSakuraDir = string(argv[2]) + Seperators;
		const string outDir        = string(argv[3]) + Seperators;

		ExtractSubtitles(rootSakuraDir, outDir);
	}
	else if( command == "YabauseToMednafen" && argc == 4 )
	{
		const string yabauseFile = string(argv[2]);
		const string outFile     = string(argv[3]);

		ConvertYabauseSaveToMednafen(yabauseFile, outFile);
	}
	else if( command == "ExtractTiledImages" && argc == 4 )
	{
		const string rootSakuraDir = string(argv[2]) + Seperators;
		const string outDir        = string(argv[3]) + Seperators;

		ExtractTiledImages(rootSakuraDir, outDir);
	}
	else if( command == "AddShadowsToWKLText" && argc == 3 ) 
	{
		const string wklDir = string(argv[2]) + Seperators;

		AddShadowsToWKLText(wklDir);
	}
	else if(command == "BoldWKLText" && argc == 3)
	{
		const string wklDir = string(argv[2]) + Seperators;

		BoldWKLText(wklDir);
	}
	else if( command == "PatchFACEFiles" && argc == 5 )
	{
		const string rootSakuraDir     = string(argv[2]) + Seperators;
		const string outDir            = string(argv[3]) + Seperators;
		const string translatedDataDir = string(argv[4]) + Seperators;

		PatchFACEFiles(rootSakuraDir, outDir, translatedDataDir);
	}
	else if( command == "ExtractStatusScreen" && argc == 5 )
	{
		const string rootSakuraDir     = string(argv[2]) + Seperators;
		const string translatedDataDir = string(argv[3]) + Seperators;
		const string outDir            = string(argv[4]) + Seperators;

		ExtractStatusScreen(rootSakuraDir, translatedDataDir, outDir);
	}
	else if( command == "ExtractLoadScreen" && argc == 5 )
	{
		const string rootSakuraDir     = string(argv[2]) + Seperators;
		const string translatedDataDir = string(argv[3]) + Seperators;
		const string outDir            = string(argv[4]) + Seperators;

		ExtractLoadScreen(rootSakuraDir, translatedDataDir, outDir);
	}
	else if( command == "DumpBitmap" && argc == 4 )
	{
		const string inputFilePath = string(argv[2]);
		const string outDir        = string(argv[3]) + Seperators;

		DumpBitmap(inputFilePath, outDir);
	}
	else
	{
		PrintHelp();
	}
#endif
	return 1;
}
