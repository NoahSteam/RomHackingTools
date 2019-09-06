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
#include <algorithm>
#include "..\Utils\Utils.h"
#include <assert.h>

using std::vector;
using std::string;
using std::list;
using std::map;

#define OPTIMIZE_INSERTION_DEBUGGING 0

const unsigned char OutTileSpacingX = 8;
const unsigned char OutTileSpacingY = 12;
const unsigned long MaxTBLFileSize  = 0x20000;
const unsigned long MaxMESFileSize  = 0x1A800;
const unsigned long MaxWKLFileSize  = 0x82E00;
const char          MaxLines        = 4;

const string PatchedPaletteName("PatchedPalette.BIN");
const string PatchedKNJName("PatchedKNJ.BIN");
const string Disc1("\\Disc1");
const string Disc2("\\Disc2");
const string Seperators("\\");
const string NewLineWord("<br>");
const string LipsWord("<LIPS>");
const string SpaceWord("<sp>");
const string BMPExtension(".bmp");
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

struct BmpToSakuraConverter
{
	TileExtractor mTileExtractor;
	PaletteData   mPalette;
	char*         mpPackedTiles = nullptr;
	unsigned int  mPackedTileSize = 0;

	static const unsigned short CYAN  = 0xe07f; //In little endian order
	static const unsigned short WHITE = 0xff7f;
	static const unsigned short BLACK = 0;

	~BmpToSakuraConverter()
	{
		delete mpPackedTiles;
		mpPackedTiles   = nullptr;
		mPackedTileSize = 0;
	}

	bool ConvertBmpToSakuraFormat(const string& inBmpPath, bool bFixupAlphaColor, const unsigned short inAlphaColor = CYAN, const unsigned int* pTileWidth = 0, const unsigned int* pTileHeight = 0)
	{
		//Read in translated font sheet
		BitmapReader origBmp;
		if(!origBmp.ReadBitmap(inBmpPath))
		{
			return false;
		}

		const unsigned int imageWidth = pTileWidth ? *pTileWidth : origBmp.mBitmapData.mInfoHeader.mImageWidth;
		const int imageHeight         = pTileHeight ? *pTileHeight : origBmp.mBitmapData.mInfoHeader.mImageHeight;

		if(!mTileExtractor.ExtractTiles(imageWidth, imageHeight, imageWidth, abs(imageHeight), origBmp))
		{
			return false;
		}

		//Convert it to the SakuraTaisen format
		if( !mPalette.CreateFrom32BitData(origBmp.mBitmapData.mPaletteData.mpRGBA, origBmp.mBitmapData.mPaletteData.mSizeInBytes, false) )
		{
			return false;
		}

		//Fix up palette
		if( bFixupAlphaColor )
		{	
			//First index needs to have the transparent color
			int indexOfAlphaColor = -1;
			for(int i = 0; i < mPalette.GetNumColors(); ++i)
			{
				assert(i * 2 < mPalette.GetSize());

				const unsigned short color = *((short*)(mPalette.GetData() + i * 2));
				if(color == inAlphaColor)
				{
					const unsigned short oldColor0 = *((unsigned short*)mPalette.GetData());
					mPalette.SetValue(0, inAlphaColor);
					mPalette.SetValue(i, oldColor0);
					indexOfAlphaColor = i;
					break;
				}
			}

			if(indexOfAlphaColor == -1)
			{
				printf("Alpha Color not found.  Palette will not be correct. \n");
				indexOfAlphaColor = 0;
			}

			//Fix up color data now that the palette has been modified
			if( indexOfAlphaColor != -1 )
			{
				mTileExtractor.FixupIndexOfAlphaColor((unsigned short)indexOfAlphaColor, origBmp.mBitmapData.mInfoHeader.mBitCount == 4);
			}
		}

		return true;
	}

	void PackTiles()
	{
		if( mTileExtractor.mTiles.size() )
		{
			mPackedTileSize = mTileExtractor.mTiles.size() * mTileExtractor.mTiles[0].mTileSize;
			mpPackedTiles = new char[mPackedTileSize];

			int packedTileOffset = 0;
			for(const TileExtractor::Tile& tile : mTileExtractor.mTiles)
			{
				memcpy_s(mpPackedTiles + packedTileOffset, mPackedTileSize - packedTileOffset, tile.mpTile, tile.mTileSize);
				
				packedTileOffset += tile.mTileSize;
			}
		}
	}

	const char* GetImageData() const
	{
		return mTileExtractor.mTiles[0].mpTile;
	}

	unsigned int GetImageDataSize() const
	{
		return mTileExtractor.mTiles[0].mTileSize;
	}
};

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
		compressor.CompressData((void*)pInData, inDataSize);

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
				mDataSize = compressor.mCompressedSize + (compressor.mCompressedSize % 2);
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
		if( inChar == '@' )
		{
			return 'u' + 16;// + 1;//'…' + 1;
		}

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

	void AddString(const string& inString, unsigned short specialValue, unsigned short numCharsPrinted)
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

	FileNameContainer         mFileNameInfo;
	vector<SakuraString>      mLines;
	SakuraDataSegment         mHeader;
	SakuraDataSegment         mFooter;
	vector<SakuraDataSegment> mDataSegments;
	vector<SakuraStringInfo>  mStringInfoArray;

private:
	unsigned long       mFileSize;
	FILE*               mpFile;
	char*               mpBuffer;

public:
	SakuraTextFile(const FileNameContainer& fileName) : mFileNameInfo(fileName), mFileSize(0), mpFile(nullptr), mpBuffer(nullptr){}

	SakuraTextFile(SakuraTextFile&& rhs) : mFileNameInfo(std::move(rhs.mFileNameInfo)), mLines(std::move(rhs.mLines)), mHeader(std::move(rhs.mHeader)), mFooter(std::move(rhs.mFooter)), 
		mDataSegments(std::move(rhs.mDataSegments)), mStringInfoArray(std::move(rhs.mStringInfoArray)), mFileSize(rhs.mFileSize),
		mpFile(rhs.mpFile), mpBuffer(std::move(rhs.mpBuffer))
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
		mFooter          = std::move(rhs.mFooter);
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
		const unsigned short dataSize = SwapByteOrder( *((unsigned short*)(mpBuffer + 2)) ) * 2;
		mFooter = std::move(SakuraDataSegment(mpBuffer + dataSize, mFileSize - dataSize));
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
	unsigned short mOffsetToTable;
	unsigned short mTableEnd;
	vector<unsigned int> mStringInfo;

	bool CreateFixedHeader(const vector<SakuraTextFile::SakuraStringInfo>& inInfo, const SakuraTextFile& inSakuraFile, const vector<SakuraString>& inStrings)
	{
		//All TBL files start with this entries
		mStringInfo.push_back( SwapByteOrder(inInfo[0].mFullValue) );

		unsigned short prevValue = 0;
		const size_t numEntries  = inInfo.size() - 1;
		for(size_t i = 0; i < numEntries; ++i)
		{
			//const unsigned short trailingZeroes = (unsigned short)(inSakuraFile.mDataSegments[i+1].dataSize)/2;
			const unsigned short newSecondValue = (unsigned short)inStrings[i].mChars.size() + prevValue; //+ trailingZeros;
			const unsigned int   newValue       = ((unsigned int)(inInfo[i+1].mStringId) << 16) + (unsigned int)newSecondValue;
			prevValue                           = newSecondValue;

			mStringInfo.push_back( SwapByteOrder(newValue) );
		}

		//Figure out text table size
		unsigned long stringTableSize = 0;
		for(const SakuraTextFile::SakuraDataSegment& segment : inSakuraFile.mDataSegments)
		{
			stringTableSize += segment.dataSize;
		}

		for(const SakuraString& sakuraString : inStrings)
		{
			stringTableSize += sakuraString.mChars.size()*2;
		}
		//Done figuring out table size

		mOffsetToTable = SwapByteOrder( ((unsigned short*)inSakuraFile.mHeader.pData)[0] );

		unsigned long timingDataL = ((mOffsetToTable<<1) + stringTableSize) >> 1;
		if(timingDataL/2 > 0xffff)
		{
			const int sizeToReduce = (timingDataL/2) - 0xffff;
			printf("\nERROR:Translated file %s is too big. Timing data is too far down.  Needs to be reduced by %i bytes (%i characters)\n", inSakuraFile.mFileNameInfo.mFileName.c_str(), sizeToReduce, sizeToReduce/2);
			return false;
		}
		mTableEnd = (unsigned short)timingDataL;//(unsigned short)((mOffsetToTable*2 + (unsigned short)stringTableSize) / 2);

		if(stringTableSize/2 > 0xffff)
		{
			const int sizeToReduce = (stringTableSize/2) - 0xffff;
			printf("\nERROR:Translated file %s is too big.  Needs to be reduced by %i bytes (%i characters)\n", inSakuraFile.mFileNameInfo.mFileName.c_str(), sizeToReduce, sizeToReduce/2);
			return false;
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

void GetAllFilesOfType(const vector<FileNameContainer>& allFiles, const char* pInFileType, vector<FileNameContainer>& outFiles)
{
	const string validTextFile(pInFileType);

	for(const FileNameContainer& fileName : allFiles)
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
	const int inNumTexturesPerRow, const int inNumColors = 256, const int inDataOffset = 0, bool bInFillEmptyData = true, bool bForceBitmapFormat = false)
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
	paletteData.CreateFrom15BitData(pInPaletteData, inPaletteDataSize);

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
	fontBitmap.CreateBitmap(outFileName, imageWidth, -imageHeight, inPaletteDataSize == 32 ? 4 : 8, pOutTiledData, numTiledBytes, paletteData.GetData(), paletteData.GetSize(), bForceBitmapFormat);

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

	void ReadInStrings(const char* pFileData, unsigned int offsetToTextData)
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

			const string bitmapName = outputDirectory + std::to_string(stringIndex + 1) + extension;
			sakuraStringBmp.WriteToFile(bitmapName, true);

			++stringIndex;
		}
	}
};

void ExtractMiniSwimText(const string& rootSakuraDirectory, const string& outputDirectory)
{
	//Create temp work directory
	string tempDir;
	if( !CreateTemporaryDirectory(tempDir) )
	{
		printf("Could not create temp work directory.  Error: (%d)\n", GetLastError());
		return;
	}

	CreateDirectoryHelper(outputDirectory);

	//Open MINISWIM.BIN
	const string sakura3Directory = rootSakuraDirectory + "\\SAKURA3\\";
	FileNameContainer miniSwimFileName( (sakura3Directory + "MINISWIM.BIN").c_str() );
	FileData miniSwimFile;
	if( !miniSwimFile.InitializeFileData(miniSwimFileName) )
	{
		printf("Unable to open %s", miniSwimFileName.mFullPath.c_str() );
		return;
	}

	//Font palette
	const int paletteSize = 32;
	const unsigned char rawPaletteData[paletteSize] = {(UCHAR)0x7f, (UCHAR)0xff, (UCHAR)0x08, (UCHAR)0x42, (UCHAR)0x10, (UCHAR)0x84, (UCHAR)0x18, (UCHAR)0xc6, (UCHAR)0x21, (UCHAR)0x08, (UCHAR)0x29, (UCHAR)0x4a, (UCHAR)0x31, (UCHAR)0x8c, 
													   (UCHAR)0x39, (UCHAR)0xce, (UCHAR)0x42, (UCHAR)0x10, (UCHAR)0x4a, (UCHAR)0x52, (UCHAR)0x52, (UCHAR)0x94, (UCHAR)0x5a, (UCHAR)0xd6, (UCHAR)0x63, (UCHAR)0x18, (UCHAR)0x6b, (UCHAR)0x5a, 
													   (UCHAR)0x73, (UCHAR)0x9c, (UCHAR)0x7f, (UCHAR)0xff};

	//Create 32bit palette data
	PaletteData paletteData;
	paletteData.CreateFrom15BitData((const char*)rawPaletteData, paletteSize);

	//Create font sheet
	const int numCharactersInFontSheet = 59;
	const int numBytesPerCharacter = 16*16/2;
	SakuraFontSheet sakuraFontSheet;
	if( !sakuraFontSheet.CreateFontSheetFromData( (miniSwimFile.GetData() + 0x0003b1a8), numBytesPerCharacter*numCharactersInFontSheet) )
	{
		return;
	}

	//Read in sakura text
	const unsigned int textDataOffset = 0x00056b28;
	MiniGameSakuraText sakuraText;
	sakuraText.ReadInStrings(miniSwimFile.GetData(), textDataOffset);
	sakuraText.DumpTextImages(sakuraFontSheet, paletteData, outputDirectory);
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

bool CreateTranslatedFontSheet(const string& inTranslatedFontSheet, const string& outPath, bool bAutoName = true)
{
	//Read in translated font sheet
	BitmapReader origTranslatedBmp;
	if( !origTranslatedBmp.ReadBitmap(inTranslatedFontSheet) )
	{
		return false;
	}

	TileExtractor tileExtractor;
	if( !tileExtractor.ExtractTiles(16, 16, 16, 16, origTranslatedBmp) )
	{
		return false;
	}

	//Convert it to the SakuraTaisen format
	PaletteData sakuraPalette;
	sakuraPalette.CreateFrom32BitData(origTranslatedBmp.mBitmapData.mPaletteData.mpRGBA, origTranslatedBmp.mBitmapData.mPaletteData.mSizeInBytes, false);

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

	const string outPaletteName = bAutoName ? outPath + PatchedPaletteName : outPath + "Palette.bin";
	const string outTableName   = bAutoName ? outPath + PatchedKNJName : outPath + ".bin";

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
	FileData newFileData;
	if( !newFileData.InitializeFileData(newKNJ.c_str(), newKNJ.c_str()) )
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
			patchedFile.WriteData(newFileData.GetData(), newFileData.GetDataSize());
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
		const size_t lastIndex = tableFileName.mFileName.find_first_of("TBL");
		const string infoFileName = tableFileName.mFileName.substr(0, lastIndex) + string(".BIN");
		const string infoFileFullPath = tableFileName.mPathOnly + Seperators + infoFileName;

		printf("Parsing %s\n", infoFileName.c_str());

		FileNameContainer infoFileNameInfo(infoFileName.c_str(), infoFileFullPath.c_str());
		FileData infoData;
		if (!infoData.InitializeFileData(infoFileNameInfo))
		{
			continue;
		}

		//Search for 228000
		const unsigned char* pData = (const unsigned char*)infoData.GetData();
		unsigned long index = 0;
		int appearance = 0;
		unsigned short imageId = 0;
		const unsigned long dataSize = infoData.GetDataSize();
		while (index + 5 < dataSize)
		{
			const bool bIsLipsEntry = (pData[index] == 0x2E && pData[index + 1] == 0x80 && pData[index + 2] == 0x00);

			if( bIsLipsEntry || 
				(pData[index] == 0x22 && pData[index + 1] == 0x80 && pData[index + 2] == 0x00)
				)
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
						(pData[lookAheadValue] == 0x2E && pData[lookAheadValue + 1] == 0x80 && pData[lookAheadValue + 2] == 0x00)
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
			printf("LIPS line is too long[D].  Needs to be a max of %i characters long. %s\n", maxCharsPerLine, textLine.mFullLine.c_str());\
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

	const string UntranslatedEnglishString("Untranslated");

	//Insert text
	for(const SakuraTextFile& sakuraFile : sakuraTextFiles)
	{
		printf("\nInserting text for: %s\n", sakuraFile.mFileNameInfo.mFileName.c_str());

		const bool bIsMESFile = sakuraFile.mFileNameInfo.mFileName.find("MES.BIN", 0, 6) != string::npos;

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
			if( translatedFileName.mNoExtension == sakuraFile.mFileNameInfo.mNoExtension )
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
					const unsigned short numCharsPrintedInLine = (unsigned short)inTranslatedString.GetNumberOfPrintedCharacters() - 3;//(static_cast<short>(translatedString.mChars.size()) - 3 - numLinesInString);
					if( (short)numCharsPrintedInLine < 0 )
					{
						printf("Invalid number of characters in string\n");
						return false;
					}
					numCharsPrintedForMES  += numCharsPrintedInLine*2 + 1;
					numCharsPrintedFortTBL += (numCharsPrintedInLine + 1)*2;
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
				const bool bIsUnused               = bIsMESFile ? false : pOrder ? false : true;

				if( bIsLipsEntry )
				{
					if( textLine.GetNumberOfLines() != sakuraFile.mLines[currSakuraStringIndex].GetNumberOfLines() )
					{
						printf("LIPS ERROR - Translated LIPS line does not have expected number of options. Expected: %i, Has: %i (File: %s Line: %u)\n", sakuraFile.mLines[currSakuraStringIndex].GetNumberOfLines(), textLine.GetNumberOfLines(), sakuraFile.mFileNameInfo.mFileName.c_str(), currSakuraStringIndex + 1);
					}
				}

				//If untranslated, then write out the file and line number
				if( bIsUnused ||
					textLine.mWords.size() == 0 ||
					(textLine.mWords.size() == 1 && textLine.mWords[0] == UntranslatedEnglishString )
					)
				{
					bool bUseShorthand = false;
					if( numTranslatedLines > 1200 )
					{
						bUseShorthand = true;
					}

					const string baseUntranslatedString = sakuraFile.mFileNameInfo.mNoExtension + string(":");
					const string untranslatedString = numTranslatedLines ? "U" : baseUntranslatedString + std::to_string(translatedLineIndex + 1); //Just print out a U for unused lines to save space

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
					translatedSakuraString.AddString( untranslatedString, sakuraFile.mLines[currSakuraStringIndex].mChars[0].mIndex, timingData );

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

				//String ends with 0000
				translatedString.AddChar(0);

				/*
				//If the first byte is non-zero, then this string has timing info associated with it
				if( sakuraFile.mLines[translatedLineIndex].mChars[0].mIndex != 0 )
				{
					const short numLinesInString = static_cast<short>(translatedString.GetNumberOfLines()) - 1;
					if( numLinesInString < 0 )
					{
						printf("Invalid number of lines in string\n");
						break;
					}

					//-3 because 2 for the initial bytes and one for the trailing zero
					const unsigned short numCharsPrintedInLine = (unsigned short)translatedString.GetNumberOfPrintedCharacters() - 3;//(static_cast<short>(translatedString.mChars.size()) - 3 - numLinesInString);
					if( (short)numCharsPrintedInLine < 0 )
					{
						printf("Invalid number of characters in string\n");
						break;
					}
					numCharsPrintedForMES += numCharsPrintedInLine*2 + 1;
					numCharsPrintedFortTBL += (numCharsPrintedInLine + 1)*2;
				}*/
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
				translatedSakuraString.AddString( untranslatedString, sakuraFile.mLines[currSakuraStringIndex].mChars[0].mIndex, timingData);
				
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
				translatedSakuraString.AddString( untranslatedString, sakuraFile.mLines[i].mChars[0].mIndex, 0);
			
				translatedLines.push_back( translatedSakuraString );
			}		
		}

		SakuraTextFileFixedHeader fixedHeader;
		fixedHeader.CreateFixedHeader(sakuraFile.mStringInfoArray, sakuraFile, translatedLines);

		//Write header
		const unsigned short bigEndianOffsetToTable = SwapByteOrder(fixedHeader.mOffsetToTable);
		const unsigned short bigEndianTableEnd      = SwapByteOrder(fixedHeader.mTableEnd);
		outFile.WriteData(&bigEndianOffsetToTable, sizeof(bigEndianOffsetToTable));
		outFile.WriteData(&bigEndianTableEnd, sizeof(bigEndianTableEnd));
		outFile.WriteData(&fixedHeader.mStringInfo[0], fixedHeader.mStringInfo.size()*sizeof(fixedHeader.mStringInfo[0]));
		//outFile.WriteData(sakuraFile.mHeader.pData, sakuraFile.mHeader.dataSize);

		//Output data
		const size_t numDataSegments    = sakuraFile.mDataSegments.size();
		size_t dataIndex                = 0;
		size_t translationIndex         = 0;
		const size_t numInsertedLines   = translatedLines.size();
		while(1)
		{
			if( dataIndex < numDataSegments )
			{
				outFile.WriteData(sakuraFile.mDataSegments[dataIndex].pData, sakuraFile.mDataSegments[dataIndex].dataSize);
			}

			if( translationIndex < numInsertedLines )
			{
				vector<unsigned short> translationData;
				translatedLines[dataIndex].GetDataArray(translationData);

				outFile.WriteData(translationData.data(), translationData.size()*sizeof(short));
			}

			++dataIndex;
			++translationIndex;

			if( dataIndex >= numDataSegments && translationIndex >= numInsertedLines )
			{
				break;
			}
		}

		//Write footer
		{
			const unsigned char char2E   = 0x2e;
			const unsigned char char6E   = 0x6e;
			const unsigned char  charEnd = 0;
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
		string outFileName = outDirectory + sakuraFile.mFileNameInfo.mNoExtension + "_NoUnusedText.txt";
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
			if( translatedFileName.mNoExtension == sakuraFile.mFileNameInfo.mNoExtension )
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
				const bool bIsUnused               = bIsMESFile ? false : pOrder ? false : true;

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
	memcpy_s((void*)(origSakuraData.GetData() + offsetLipsStartLocationY2),origSakuraData.GetDataSize(), (void*)&lipsStartLocY2,      sizeof(lipsStartLocY2));
	//***Done Patching Text Drawing Code***

	//****Patch Palettes****
	const int offsetPalette1    = 0x00053954;
//	const int offsetPalette2    = 0x0005ADB8;
	const int offsetLIPSPalette = 0x00053974;
	memcpy_s((void*)(origSakuraData.GetData() + offsetPalette1), origSakuraData.GetDataSize(), newPaletteData.GetData(), newPaletteData.GetDataSize());
//	memcpy_s((void*)(origSakuraData.GetData() + offsetPalette2), origSakuraData.GetDataSize(), newPaletteData.GetData(), newPaletteData.GetDataSize());
	memcpy_s((void*)(origSakuraData.GetData() + offsetLIPSPalette), origSakuraData.GetDataSize(), newPaletteData.GetData(), newPaletteData.GetDataSize());
	//***Done Patching Palettes***

	//****Patch Options Image****
	//Compress bgnd image
	BmpToSakuraConverter patchedOptionsImage;
	if( !patchedOptionsImage.ConvertBmpToSakuraFormat(inTranslatedOptionsBmp, true, BmpToSakuraConverter::CYAN) )
	{
		printf("FixupSakura: Couldn't convert image: %s.\n", inTranslatedOptionsBmp.c_str());
		return false;
	}

	//Read in original options image
	const unsigned long origOptionsCompressedSize  = 1256;
	const unsigned int  origOptionsOffset = 0x0005AE38;
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

	//***Copy over the original data upto the options image***
	//Copy everything up to the options data
	memcpy_s(pNewSakuraData, newSakuraLength, origSakuraData.GetData(), origOptionsOffset);

	//copy new options image data
	memcpy_s(pNewSakuraData + origOptionsOffset, newSakuraLength - origOptionsOffset, translatedOptionsData.mpCompressedData, translatedOptionsData.mDataSize);

	//copy everything after original options data
	memcpy_s(pNewSakuraData + origOptionsOffset + translatedOptionsData.mDataSize, newSakuraLength - origOptionsOffset - translatedOptionsData.mDataSize, origSakuraData.GetData() + origOptionsOffset + origOptionsCompressedSize, origSakuraData.GetDataSize() - origOptionsOffset - origOptionsCompressedSize);
	//**done copying original data to new data

	//Patch palette
	//Options image palette
	memcpy_s(pNewSakuraData + 0x0005AD98 + optionsSizeDiff, newSakuraLength - 0x0005AD98 + optionsSizeDiff, patchedOptionsImage.mPalette.GetData(), patchedOptionsImage.mPalette.GetSize());

	//Fixup pointers after the options image
	/*
	for(unsigned long k = 0x00035240; k < newSakuraLength; ++k)
	{
		const unsigned long origK = k - optionsSizeDiff;
		
		/*
		if( (origK >= 0x0005629C && origK <= 0x000562BC) || (origK >= 0x0005880A && origK <= 0x00058FBC) || (origK >= 0x0005ADAC && origK <= 0x0005b328) || (origK >= 0x0005F148 && origK <= 0x0005FB83) || 
			(origK >= 0x0005B329 && origK <= 0x0005FA19) || (origK >= 0x00052D2F && origK <= 0x000539AD) || (origK >- 0x000553DD && origK <= 0x0005540D) || (origK >= 0x0005873B && origK <= 0x0005900D) ||
			(origK >= 0x0005AD7D && origK <= 0x0005B329) )
		{
			continue;
		}*/

	/*
		unsigned int possiblePointer = *((unsigned int*)&pNewSakuraData[k]);
		possiblePointer = SwapByteOrder(possiblePointer);

		if( possiblePointer == 0x06060606 || possiblePointer == 0x06060607 || possiblePointer == 0x06060707 || possiblePointer == 0x06060601 || possiblePointer == 0x06060406 || possiblePointer == 0x06060106 || //0x06060106
			possiblePointer == 0x06060605 || possiblePointer == 0x06060506 || possiblePointer == 0x06060602 || possiblePointer == 0x06060206 || possiblePointer == 0x06060603 || possiblePointer == 0x06060306 ||
			possiblePointer == 0x06060600 || possiblePointer == 0x06060001 || possiblePointer == 0x06054D70)
		{
			continue;
		}

		if( !(possiblePointer > 0x0605EE38 && possiblePointer < 0x06063A2A) )
		{
//			if( possiblePointer != 0x060EC300 && possiblePointer != 0x060ef000 && possiblePointer != 0x060f3000 && possiblePointer != 0x060F4000 && possiblePointer != 0x060f5800 && possiblePointer != 0x060f6000 )
			continue;
		}

		unsigned int newAddress = possiblePointer + optionsSizeDiff;
		newAddress = SwapByteOrder(newAddress);
		memcpy_s(pNewSakuraData + k, newSakuraLength - k, &newAddress, sizeof(newAddress));

		printf("Fixed Sakura: Old: 0x%08x New: 0x%08x LogoIndex: 0x%08x\n", possiblePointer, SwapByteOrder(newAddress), k);
	}*/
	//****Done Patching Options Image****

	//const unsigned short tileSize  = (OutTileSpacingY << 8) + (OutTileSpacingX/8);

	//fseek(pFile, offsetTileDim, SEEK_SET);
	//fwrite(&tileSize, sizeof(tileSize), 1, pFile);

	//***Patch Dialog Options Menu***
	{
		const unsigned long origDialogOptionsCompressedSize = 376;
		const unsigned int origDialogOptionsOffset = 0x00059700;

		string translatedPausedOptionsImagePath = inTranslatedDataDirectory + string("PauseOptions.bmp");
		BmpToSakuraConverter patchedPausedOptionsImage;
		if( !patchedPausedOptionsImage.ConvertBmpToSakuraFormat(translatedPausedOptionsImagePath, false, BmpToSakuraConverter::WHITE) )
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
		const unsigned int origGameOptionsOffset = 0x00059878;

		string translatedImagePath = inTranslatedDataDirectory + string("GameOptions.bmp");
		BmpToSakuraConverter patchedImage;
		const unsigned int gameOptionTileDim = 16;
		if( !patchedImage.ConvertBmpToSakuraFormat(translatedImagePath, false, BmpToSakuraConverter::WHITE, &gameOptionTileDim, &gameOptionTileDim) )
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
	vector<FileNameContainer> evtFiles;	
	evtFiles.push_back( FileNameContainer("EVT01.BIN", sakura2Directory) );
	evtFiles.push_back( FileNameContainer("EVT02.BIN", sakura2Directory) );
	evtFiles.push_back( FileNameContainer("EVT03.BIN", sakura2Directory) );
	evtFiles.push_back( FileNameContainer("EVT04.BIN", sakura2Directory) );
	evtFiles.push_back( FileNameContainer("EVT05.BIN", sakura2Directory) );
	evtFiles.push_back( FileNameContainer("EVT06.BIN", sakura2Directory) );
	evtFiles.push_back( FileNameContainer("EVT07.BIN", sakura2Directory) );
	evtFiles.push_back( FileNameContainer("EVT08.BIN", sakura2Directory) );
	evtFiles.push_back( FileNameContainer("EVT09.BIN", sakura2Directory) );
	evtFiles.push_back( FileNameContainer("EVT10.BIN", sakura2Directory) );
	evtFiles.push_back( FileNameContainer("EVT11.BIN", sakura2Directory) );
	evtFiles.push_back( FileNameContainer("EVT21.BIN", sakura2Directory) );
	evtFiles.push_back( FileNameContainer("EVT22.BIN", sakura2Directory) );
//	evtFiles.push_back( FileNameContainer("EVT27.BIN", sakura2Directory) );

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

bool CreateTBLSpreadsheets(const string& dialogImageDirectory, const string& duplicatesFileName, const string& sakura1Directory)
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
		if( duplicatesMap.size() )
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

		//Create entries for all images
		int num = 0;
		for(const FileNameContainer& fileNameInfo : iter->second)
		{
			const unsigned long crc   = crcMap[&iter->second[num]];
			const bool bIsDuplicate   = dupCrcMap.find(crc) != dupCrcMap.end();
			const unsigned short id   = sakuraFileIter->second->mStringInfoArray[num].mStringId;
			const vector<int>* pOrder = bDialogOrderExists && dialogOrderIter->second.idAndOrder.find(id) != dialogOrderIter->second.idAndOrder.end() ? &dialogOrderIter->second.idAndOrder.find(id)->second : nullptr;
			const bool bIsLipsEntry   = pOrder ? dialogOrderIter->second.idAndLips.find(id)->second : false;
			const char* bgColor       = "fefec8";
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
			snprintf(buffer, 2048, "<td width=\"480\"><textarea id=\"edit_%s\" contenteditable=true onchange=\"SaveEdits('%i.bmp', 'edit_%i', '%lu')\" style=\"border: none; width: 100%%; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;\">Untranslated</textarea></td>", pVarSuffix, num + 1, num + 1, crc);
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

void Extract8BitImage(const string& fileName, const string& paletteFileName, const int offset, const int width, const int height, const int numColors, const bool bForceBitmap, const string& outDirectory)
{
	FileNameContainer imageFileNameInfo(fileName.c_str());
	FileNameContainer paletteFileNameInfo(paletteFileName.c_str());
	FileData paletteFile;
	if( !paletteFile.InitializeFileData(paletteFileNameInfo) )
	{
		return;
	}

	const string outFileName = outDirectory + imageFileNameInfo.mNoExtension + string(".bmp");

	ExtractImage(imageFileNameInfo, outFileName, paletteFile, width, height, 1, numColors, offset, true, bForceBitmap);
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

//FACE files contain faces that appear during the battle dialog
void ExtractFACEFiles(const string& sakuraDirectory, const string& outDirectory)
{
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
			static const unsigned int numBytesInPalette = 256;
			const char* pOriginalPaletteData             = faceFile.GetData() + offsetToData + offsetToPalette;
			char* pPaletteData                           = new char[numBytesInPalette];
			memset(pPaletteData, 0, numBytesInPalette);
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
			ExtractImageFromData(uncompressedImage.mpUncompressedData + offsetToColorData, uncompressedImage.mUncompressedDataSize - offsetToColorData, outFileName, pPaletteData, numBytesInPalette, 40, 48,
				                 1, 256, 0, false);

			delete[] pPaletteData;
		}
	}
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
			ExtractImageFromData(pImageData, mpImageInfos[i].numBytes, outFileName, pInPaletteData, inPaletteSize, mpImageInfos[i].width, mpImageInfos[i].height, 1, 16, 0, false);
		}

		return true;
	}
};

struct WklCompressedInfo
{
	unsigned int offset;
	unsigned int size;
};

struct WklHeader
{
	struct Entry
	{
		unsigned int offset = 0;
		unsigned int size = 0;
	};

	static const int NumEntries = 98;
	Entry mEntries[NumEntries];

	void SwapByteOrder()
	{
		for(int i = 0; i < NumEntries; ++i)
		{
			mEntries[i].offset = ::SwapByteOrder(mEntries[i].offset);
			mEntries[i].size   = ::SwapByteOrder(mEntries[i].size);
		}
	}
};

struct WklBattleMenuImageBlockInfo
{
	unsigned int offset;
	unsigned int size;

	void SwapByteOrder()
	{
		offset = ::SwapByteOrder(offset);
		size   = ::SwapByteOrder(size);
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
					ExtractImageFromData(pImageData, (mImageHeader.width*mImageHeader.height)/2, outFileName, inSakuraPalette.mpPaletteData, inSakuraPalette.mPaletteSize, mImageHeader.width, mImageHeader.height, 1, 16, 0, false);
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

			const unsigned int imageOffset            = SwapByteOrder(mNamesAndSpecialMoves.mCompressedInfo[i].offset);
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

		mBattleMenu.mNumBlocks        = firstEntry.offset/sizeof(WklBattleMenuImageBlockInfo);
		mBattleMenu.mpImageBlocksInfo = new WklBattleMenuImageBlockInfo[mBattleMenu.mNumBlocks];
		memcpy_s(mBattleMenu.mpImageBlocksInfo, sizeof(WklBattleMenuImageBlockInfo)*mBattleMenu.mNumBlocks, &pWklData[mBattleMenu.mImageDataAddress], sizeof(WklBattleMenuImageBlockInfo)*mBattleMenu.mNumBlocks);
		for(unsigned int entryIndex = 0; entryIndex < mBattleMenu.mNumBlocks; ++entryIndex)
		{	
			mBattleMenu.mpImageBlocksInfo[entryIndex].SwapByteOrder();

			BattleMenu::ImageBlock* pNewImageBlock = new BattleMenu::ImageBlock();
			mBattleMenu.mImageBlocks.push_back(pNewImageBlock);

			memcpy_s(&pNewImageBlock->mNumImages, sizeof(pNewImageBlock->mNumImages), &pWklData[mBattleMenu.mImageDataAddress + mBattleMenu.mpImageBlocksInfo[entryIndex].offset], sizeof(pNewImageBlock->mNumImages));
			pNewImageBlock->mNumImages = SwapByteOrder(pNewImageBlock->mNumImages);

			pNewImageBlock->mpImagesInBlock       = new WklBattleMenuImageHeader[pNewImageBlock->mNumImages];
			const unsigned int imageHeaderEnd     = mBattleMenu.mImageDataAddress + mBattleMenu.mpImageBlocksInfo[entryIndex].offset + 8;
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
				printf("PatchWKL failed.  Unable top open %s", patchedWklImageName.mFullPath.c_str());
				return false;
			}

			//It is assumed that the patched wkl directory has no duplicate images
			patchedWklFileCrcMap[originalWkImage.GetCRC()] = patchedWklImageName;
		}

	}
	//Done creating map

	//Load patched image data
	BmpToSakuraConverter patchedStatsMenu;
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
		unsigned long compressedDataAddress = 0;
		memcpy_s(&compressedDataAddress, sizeof(compressedDataAddress), &pOriginalWklData[offsetToImageData], sizeof(compressedDataAddress));
		compressedDataAddress = SwapByteOrder(compressedDataAddress);

		//Read in offset to compressed data
		memset(&compressedInfo, 0, sizeof(compressedInfo));
		memcpy_s(&origCompressedInfo, sizeof(origCompressedInfo), &pOriginalWklData[compressedDataAddress], sizeof(origCompressedInfo));
		
		MemoryBlocks newWklCompressedData;
		MemoryBlocks newWklData;
		MemoryBlocks patchedBattleMenuImageData;
		MemoryBlocks patchedBattleMenuBlocksHeader;
		MemoryBlocks patchedBattleMenuBlocks;
		const unsigned int compressedImageHeaderSize         = 512;
		const unsigned int compressedDataLastOffset_original = SwapByteOrder(origCompressedInfo[numCompressedEntries - 1].offset);
		
		//Gather data from original wkl file
		WklBattleMenuExtractor wklExtractor;
		wklExtractor.Initialize(originalWklFileName);

		const unsigned int startOfBattleImages = wklExtractor.mBattleMenu.mImageDataAddress;
		const unsigned int endOfBattleImages   = wklExtractor.mBattleMenu.mImageBlocks.back()->mBattleImages.back()->mAddress + wklExtractor.mBattleMenu.mImageBlocks.back()->mBattleImages.back()->mImageHeader.GetImageByteCount();

		if( compressedDataAddress < endOfBattleImages )
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
		newWklData.AddBlock(pOriginalWklData, endOfBattleImages, compressedDataAddress - endOfBattleImages);
		newWklData.AddBlock(pOriginalWklData, compressedDataAddress + compressedDataLastOffset_original, originalWKLFile.GetDataSize() - (compressedDataAddress + compressedDataLastOffset_original));

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
				BmpToSakuraConverter patchedBattleImage;
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
			compressedInfo[i].offset = offsetToImageBlock;

			if( !origCompressedInfo[i].size )
			{
				compressedInfo[i].size   = 0;
				compressedInfo[i].offset = SwapByteOrder(offsetToImageBlock);
				continue;
			}

			const unsigned int origImageOffset        = SwapByteOrder(origCompressedInfo[i].offset);
			const unsigned int offsetToCompressedData = origImageOffset + compressedDataAddress;
			
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

				//Read in translated data
				BitmapReader translatedImage;
				if( !translatedImage.ReadBitmap(patchedBattleImagePath.c_str()) )
				{
					printf("PatchWKL failed.  Unable to open %s\n", patchedBattleImagePath.c_str());
					return false;
				}

				const int imageWidth  = abs(translatedImage.mBitmapData.mInfoHeader.mImageWidth);
				const int imageHeight = abs(translatedImage.mBitmapData.mInfoHeader.mImageHeight);

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

				translatedImage.mBitmapData.mInfoHeader.mImageWidth = abs(translatedImage.mBitmapData.mInfoHeader.mImageWidth);
				translatedImage.mBitmapData.mInfoHeader.mImageHeight = abs(translatedImage.mBitmapData.mInfoHeader.mImageHeight);

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
			compressedTranslatedData.CompressData(pDataToCompress, sizeOfDataToCompress, true);
			
			//Fixup header info
			compressedInfo[i].size = SwapByteOrder(compressedTranslatedData.mCompressedSize);
			offsetToImageBlock    += compressedTranslatedData.mCompressedSize;

			//Write out the compressed data
			newWklCompressedData.AddBlock(compressedTranslatedData.mpCompressedData, 0, compressedTranslatedData.mCompressedSize);

			//Now swap the byte order
			compressedInfo[i].offset = SwapByteOrder(compressedInfo[i].offset);

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

		const unsigned int compressedDataLastOffset_patched = SwapByteOrder(compressedInfo[numCompressedEntries - 1].offset);
		const int newDataSizeDelta                          = compressedDataLastOffset_patched - compressedDataLastOffset_original;
		for(int headerEntry = 0; headerEntry < WklHeader::NumEntries; ++headerEntry)
		{
			if( wklHeader_patched.mEntries[headerEntry].offset > compressedDataAddress )
			{
				wklHeader_patched.mEntries[headerEntry].offset += newDataSizeDelta;
			}
			else if( wklHeader_patched.mEntries[headerEntry].offset == compressedDataAddress )
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
		if( bCalculatedBattleDelta )
		{
			assert(newBattleMenuDelta == battleMenuDelta);
		}
		battleMenuDelta        = newBattleMenuDelta;
		bCalculatedBattleDelta = true;
		for(int headerEntry = 0; headerEntry < WklHeader::NumEntries; ++headerEntry)
		{
			if( wklHeader_patched.mEntries[headerEntry].offset > wklExtractor.mBattleMenu.mImageDataAddress )
			{
				wklHeader_patched.mEntries[headerEntry].offset += battleMenuDelta;
			}
			else if( wklHeader_patched.mEntries[headerEntry].offset == wklExtractor.mBattleMenu.mImageDataAddress )
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
	const int textDelta            = (textBytes - origTextBytes);
	
	//Clipping for the battle menu, change 0x0030 to 0xffa0 (48 to -96)
	unsigned short newBattleMenuClippingValue = 0xFFA0;
	
	for(const string& slgFileName : slgFiles)
	{
		//Open the original file
		const string filePath = outDirectory + slgFileName;
		FileReadWriter slgFile;
		if( !slgFile.OpenFile(filePath.c_str()) )
		{
			printf("PatchWKL: Unable to open %s file.\n", filePath.c_str());
			return false;
		}

		//Fixup 4 byte offsets
		unsigned long origVDP1Value = 0;
		unsigned long newVPD1Value  = 0;

		//Cursor image VDP1 offset
		slgFile.ReadData(0x00014058, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta;
		slgFile.WriteData(0x00014058, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

		//Cursor image VDP1 offset
		slgFile.ReadData(0x000140f0, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta;
		slgFile.WriteData(0x000140f0, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

		//Cursor image VDP1 offset
		slgFile.ReadData(0x00014528, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta;
		slgFile.WriteData(0x00014528, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

		//Dialog text VDP1 address [05C16480]
		slgFile.ReadData(0x00021590, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta;
		slgFile.WriteData(0x00021590, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

		//Dialog text VDP1 address [05C16480]
		slgFile.ReadData(0x000219c4, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta;
		slgFile.WriteData(0x000219c4, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

		//Battle Sprites VDP1 address[05C17B00]
		slgFile.ReadData(0x00010f8c, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta + textDelta;
		slgFile.WriteData(0x00010f8c, (char*)&newVPD1Value, sizeof(newVPD1Value), true);

		//Battle sprites VDP1 read address[05C17B00]
		slgFile.ReadData(0x0000FF68, (char*)&origVDP1Value, sizeof(origVDP1Value), true);
		newVPD1Value = origVDP1Value + battleMenuDelta + textDelta;
		slgFile.WriteData(0x0000FF68, (char*)&newVPD1Value, sizeof(newVPD1Value), true);


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

		//Clipping for the battle menu, change 0x0030 to 0xffb8 (48 to -72)
		slgFile.WriteData(0x000253DC, (char*)&newBattleMenuClippingValue, sizeof(newBattleMenuClippingValue), true);

		//Battle menu formatting
		slgFile.WriteData(0x00046c60, battleMenuFormattingData.GetData(), battleMenuFormattingData.GetDataSize());
		/*
		
		*/
		//Value: 2000 
	//	slgFile.ReadData(0x00012d90, (char*)&origVDP1Offset, sizeof(origVDP1Offset), true);
	//	newVDP1Offset = ((origVDP1Offset<<3) + battleMenuDelta) >> 3;
	//	slgFile.WriteData(0x00012d90, (char*)&newVDP1Offset, sizeof(newVDP1Offset), true);

		//Value: 4000 
	//	slgFile.ReadData(0x00012d92, (char*)&origVDP1Offset, sizeof(origVDP1Offset), true);
	//	newVDP1Offset = ((origVDP1Offset<<3) + battleMenuDelta) >> 3;
	//	slgFile.WriteData(0x00012d92, (char*)&newVDP1Offset, sizeof(newVDP1Offset), true);
	}
	//***Done with SLG files***

	const string evtFilePath     = outDirectory + "EVT01.BIN";
	const string evtOrigFilePath = sakuraDirectory + "SAKURA2\\EVT01.BIN";
	FileWriter evtFile;
	FileData origEvtFile;
	if( !evtFile.OpenFileForWrite(evtFilePath.c_str()) )
	{
		printf("PatchWKL: Unable to open %s file.\n", evtFilePath.c_str());
		return false;
	}
	if( !origEvtFile.InitializeFileData(evtOrigFilePath.c_str()) )
	{
		printf("PatchWKL: Unable to open %s file.\n", evtOrigFilePath.c_str());
		return false;
	}

	const string tutorialPatchPath = inTranslatedDirectory + "EVT01_Tutorial.bin";
	FileData evt01TutorialPatch;
	if( !evt01TutorialPatch.InitializeFileData(tutorialPatchPath.c_str()) )
	{
		printf("PatchWKL: Unable to open %s file.\n", tutorialPatchPath.c_str());
		return false;
	}
	

	evtFile.WriteData(origEvtFile.GetData(), origEvtFile.GetDataSize());
	evtFile.WriteDataAtOffset(evt01TutorialPatch.GetData(), evt01TutorialPatch.GetDataSize(), 0x000120F0);
	evtFile.WriteDataAtOffset(&newBattleMenuClippingValue, sizeof(newBattleMenuClippingValue), 0x000004F8, true);

	//unsigned long origEvtVDP1WriteAddress = 0;
	//unsigned long newEvtVDP1WriteAddress  = 0;

	//VDP1 Write address
	{
		/*
		origEvtFile.ReadData(0x00006d9c, (char*)&origEvtVDP1WriteAddress, sizeof(origEvtVDP1WriteAddress), true);
		newEvtVDP1WriteAddress = (origEvtVDP1WriteAddress + (unsigned short)((battleMenuDelta)>>3));
		evtFile.WriteData(0x00006d9c, (char*)&newEvtVDP1WriteAddress, sizeof(newEvtVDP1WriteAddress), true);*/

		//25CF100
		//origEvtFile.ReadData(0x000077d4, (char*)&origEvtVDP1WriteAddress, sizeof(origEvtVDP1WriteAddress), true);
		//newEvtVDP1WriteAddress = (origEvtVDP1WriteAddress + (unsigned short)((battleMenuDelta + textDelta)));
		//evtFile.WriteData(0x000077d4, (char*)&newEvtVDP1WriteAddress, sizeof(newEvtVDP1WriteAddress), true);

		/*
		origEvtFile.ReadData(0x00000c1c, (char*)&origEvtVDP1WriteAddress, sizeof(origEvtVDP1WriteAddress), true);
		newEvtVDP1WriteAddress = (origEvtVDP1WriteAddress + (unsigned short)((battleMenuDelta + textDelta)>>3));
		evtFile.WriteData(0x00000c1c, (char*)&newEvtVDP1WriteAddress, sizeof(newEvtVDP1WriteAddress), true);

		origEvtFile.ReadData(0x00001128, (char*)&origEvtVDP1WriteAddress, sizeof(origEvtVDP1WriteAddress), true);
		newEvtVDP1WriteAddress = (origEvtVDP1WriteAddress + (unsigned short)((battleMenuDelta + textDelta)>>3));
		evtFile.WriteData(0x00001128, (char*)&newEvtVDP1WriteAddress, sizeof(newEvtVDP1WriteAddress), true);

		origEvtFile.ReadData(0x00001320, (char*)&origEvtVDP1WriteAddress, sizeof(origEvtVDP1WriteAddress), true);
		newEvtVDP1WriteAddress = (origEvtVDP1WriteAddress + (unsigned short)((battleMenuDelta + textDelta)>>3));
		evtFile.WriteData(0x00001320, (char*)&newEvtVDP1WriteAddress, sizeof(newEvtVDP1WriteAddress), true);*/
		/*
		Found a match in EVT01.BIN @0x00006d9a
		Found a match in EVT02.BIN @0x000087aa
		Found a match in EVT03.BIN @0x00007536
		Found a match in EVT04.BIN @0x00007aca
		Found a match in EVT05.BIN @0x000063ea
		Found a match in EVT06.BIN @0x00008472
		Found a match in EVT07.BIN @0x00008b12
		Found a match in EVT08.BIN @0x00008ee6
		Found a match in EVT08.BIN @0x0001172e
		Found a match in EVT09.BIN @0x0000325e
		Found a match in EVT10.BIN @0x00003b32
		Found a match in EVT11.BIN @0x00005306
		Found a match in EVT21.BIN @0x00003a96
		Found a match in EVT22.BIN @0x0000972e
		Found a match in EVT27.BIN @0x00009852
		*/
	}
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

			const unsigned int imageOffset = SwapByteOrder(compressedInfo[i].offset);
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

		const unsigned int numEntries = firstEntry.offset/sizeof(WklBattleMenuImageBlockInfo);
		WklBattleMenuImageBlockInfo* pMiscHeaders   = new WklBattleMenuImageBlockInfo[numEntries];
		memcpy_s(pMiscHeaders, sizeof(WklBattleMenuImageBlockInfo)*numEntries, &pWklData[imageDataAddress2], sizeof(WklBattleMenuImageBlockInfo)*numEntries);
		for(unsigned int entryIndex = 0; entryIndex < numEntries; ++entryIndex)
		{
			pMiscHeaders[entryIndex].SwapByteOrder();

			unsigned short numMiscImages = 0;
			memcpy_s(&numMiscImages, sizeof(numMiscImages), &pWklData[imageDataAddress2 + pMiscHeaders[entryIndex].offset], sizeof(numMiscImages));
			numMiscImages = SwapByteOrder(numMiscImages);

			WklBattleMenuImageHeader* pImageHeaders     = new WklBattleMenuImageHeader[numMiscImages];
			const unsigned int imageHeaderEnd     = imageDataAddress2 + pMiscHeaders[entryIndex].offset + 8;
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
				ExtractImageFromData(pImageData, (imageHeader.width*imageHeader.height)/2, outFileName, paletteData.mpPaletteData, paletteData.mPaletteSize, imageHeader.width, imageHeader.height, 1, 16, 0, false);
				
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

	PaletteData paletteData;
	if( !paletteData.CreateFrom15BitData(paletteFile.GetData(), paletteFile.GetDataSize()) )
	{
		printf("Unable to create palette.\n");
		return;
	}

	const int numLutEntries = 93;
	SakuraLut lookupTable[numLutEntries];
	const unsigned int offsetToData = 0x0001EBF4;
	memcpy_s(lookupTable, sizeof(lookupTable), sakuraFileData.GetData() + offsetToData, sizeof(lookupTable));

	for(int i = 0; i < numLutEntries; ++i)
	{
		lookupTable[i].addressInTmapSP = SwapByteOrder(lookupTable[i].addressInTmapSP);

		const string outFileName = outDirectory + std::to_string(i);

		BitmapWriter outBmp;
		outBmp.CreateBitmap(outFileName, lookupTable[i].width*8, lookupTable[i].height, 4, tmapFileData.GetData() + lookupTable[i].addressInTmapSP*8, (lookupTable[i].width*lookupTable[i].height)/2, paletteData.GetData(), paletteData.GetSize());
	}
}

bool PatchTMapSP(const string& sakuraDirectory, const string& patchDataPath)
{
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(patchDataPath, allFiles);

	vector<FileNameContainer> patchedImages;
	GetAllFilesOfType(allFiles, ".bmp", patchedImages);

	if( patchedImages.size() == 0 )
	{
		return false;
	}

	const string tmapFilePath = sakuraDirectory + string("SAKURA1\\TMAPSP.BIN");
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
	const int numLutEntries = 93;
	SakuraLut lookupTable[numLutEntries];
	SakuraLut patchedLookupTable[numLutEntries];
	const unsigned int offsetToLookupTable = 0x0001EBF4;
	fseek(pSakuraFile, offsetToLookupTable, SEEK_SET);
	fread(lookupTable, sizeof(lookupTable), 1, pSakuraFile);

	//Create a copy for the patched version of the table.  We'll modify that below.
	memcpy_s(patchedLookupTable, sizeof(patchedLookupTable), lookupTable, sizeof(lookupTable));

	//**Create the palette from one of the images**
	//Read in first image
	BitmapReader firstImage;
	if( !firstImage.ReadBitmap(patchedImages[0].mFullPath) )
	{
		return false;
	}

	//Convert it to the SakuraTaisen format
	PaletteData sakuraPalette;
	sakuraPalette.CreateFrom32BitData(firstImage.mBitmapData.mPaletteData.mpRGBA, firstImage.mBitmapData.mPaletteData.mSizeInBytes, false);

	//Fix up palette
	//First index needs to have the transparent color, in our case that's white
	int indexOfAlphaColor = -1;
	const unsigned short alphaColor = 0x4629; //In little endian order
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
		printf("Alpha Color not found.  Palette will not be correct. \n");
		indexOfAlphaColor = 0;
	}

	//Write out new palette
	const unsigned int tmapSpPaletteAddress = 0x00056b1c;
	fseek(pSakuraFile, tmapSpPaletteAddress, SEEK_SET);
	fwrite(sakuraPalette.GetData(), sakuraPalette.GetSize(), 1, pSakuraFile);
	//***Done Creating the palette***

	//Address of first image in TMapSP
	const int firstPatchedImageNum = atoi(patchedImages[0].mNoExtension.c_str());
	unsigned short imageAddress    = SwapByteOrder(lookupTable[firstPatchedImageNum].addressInTmapSP)*8;

	//Copy everything from original TMapSp file to the new version of it
	unsigned char* pTMapSpBuffer = new unsigned char[imageAddress];
	if( !fread_s(pTMapSpBuffer, imageAddress, imageAddress, 1, pTMapSpFile) )
	{
		printf("PatchTMapSp: Unable to copy over original TMapSP data.\n");
		fclose(pSakuraFile);
		fclose(pTMapSpFile);
		return false;
	}
	fclose(pTMapSpFile);

	//Create new TMapSPFile
	FileWriter patchedTMapSP;
	if( !patchedTMapSP.OpenFileForWrite(tmapFileNameInfo.mFullPath) )
	{
		printf("PatchTMapSp: Unable to create patched TMapSP file.\n");
		fclose(pSakuraFile);
		return false;
	}

	//Write out the initial unpatched images
	if( !patchedTMapSP.WriteData(pTMapSpBuffer, imageAddress) )
	{
		printf("PatchTMapSp: Unable to copy buffer to patched TMapSP file.\n");
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
			printf("PatchTMapSP failed.  Image width for %s is invalid.  Max width is %i", patchedImages[imageIndex].mFileName.c_str(), 0xff*8);
			return false;
		}

		if( newImageHeight > 0xff )
		{
			printf("PatchTMapSP failed.  Image height for %s is invalid.  Max height is %i", patchedImages[imageIndex].mFileName.c_str(), 0xff);
			return false;
		}

		bool bIsEmptyRoomImage = patchedImages[imageIndex].mNoExtension == emptyRoom;
		if( bIsEmptyRoomImage )		
		{
			if( newImageWidth != 56 && newImageHeight != 24 )
			{
				printf("PatchTMapSP failed.  EmptyRoom image must be 56x24 4bpp.");
				return false;
			}
		}
		else
		{
			const int lutIndex = atoi(patchedImages[imageIndex].mNoExtension.c_str());
			if( lutIndex >= numLutEntries || lutIndex < 0 )
			{
				printf("Invalid image number: %i.  Only % images allowed.\n", lutIndex, numLutEntries);
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
			printf("PatchTMapSP: Invalid patched image: %s\n", patchedImages[imageIndex].mNoExtension.c_str());
		}

		//Write out the SakuraTaisen format image
		tileExtractor.FixupIndexOfAlphaColor((unsigned short)indexOfAlphaColor, true);
		for(TileExtractor::Tile& tile : tileExtractor.mTiles)
		{
			//Empty room image goes into sakura file
			if( bIsEmptyRoomImage )
			{
				const unsigned int emptyMapImageAddress = 0x00058d10;

				//Save the patched lookup table in the SAKURA file
				fseek(pSakuraFile, emptyMapImageAddress, SEEK_SET);
				if( fwrite(tile.mpTile, tile.mTileSize, 1, pSakuraFile) != 1 )
				{
					printf("PatchTMapSP: Unable to write out EmptyRoom image to SAKURA.\n");
					fclose(pSakuraFile);
					return false;
				}
			}
			else
			{
				if( !patchedTMapSP.WriteData(tile.mpTile, tile.mTileSize) )
				{
					printf("PatchTMapSP: Unable to write out patched image %s.\n", patchedImages[imageIndex].mFileName.c_str());
					fclose(pSakuraFile);
					return false;
				}
			}

		}

		imageAddress += (unsigned short)(newImageWidth*newImageHeight/2);
	}

	if( patchedTMapSP.GetFileSize() > 0xD000 )
	{
		printf("PatchTMapSP: Patched TMapSP file is too big.  Max size is 0xD000, current size is %lu.\n", patchedTMapSP.GetFileSize());
		fclose(pSakuraFile);
		return false;
	}

	//Save the patched lookup table in the SAKURA file
	fseek(pSakuraFile, offsetToLookupTable, SEEK_SET);
	if( fwrite(patchedLookupTable, sizeof(patchedLookupTable), 1, pSakuraFile) != 1 )
	{
		printf("PatchTMapSP: Unable to write out patched lookup table.\n");
		fclose(pSakuraFile);
		return false;
	}

	fclose(pSakuraFile);

	return true;
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
			printf("Found: %08x %08x Size: %lu CompressedSize: %lu\n", foundResult.uncompressionStart, foundResult.offestInUncompressedData, foundResult.dataSize, foundResult.compressedSize);
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
	compressedFile.CompressData((void*)testData.GetData(), testData.GetDataSize());

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
		//SAKURA layout code
		const unsigned long optionsCursorSpeedTextOffset = bIsSLG ? 0x0004808c : 0x0005aa50;
		const unsigned long optionsSoundTextOffset       = bIsSLG ? 0x00047fb0 : 0x0005a974;
		const unsigned long optionsVoiceTextOffset       = bIsSLG ? 0x00047f04 : 0x0005a8c8;
		const unsigned long optionsControlsTextOffset    = bIsSLG ? 0x00047e3c : 0x0005a800;
		const unsigned long optionsExitTextOffset        = bIsSLG ? 0x00047dd2 : 0x0005a796;
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
		const unsigned long controlsOkTextOffset = bIsSLG ? 0x00047dbe : 0x0005a782;
		MainMenuText controlsOk[2];
		sakuraFileData.ReadData(controlsOkTextOffset, (char*)controlsOk, sizeof(controlsOk));
		controlsOk[0].SetCharacter(33);
		controlsOk[1].SetCharacter(34);

		//Cancel
		const unsigned long controlsCancelTextOffset = bIsSLG ? 0x00047d96 : 0x0005a75a;
		MainMenuText controlsCancel[4];
		sakuraFileData.ReadData(controlsCancelTextOffset, (char*)controlsCancel, sizeof(controlsCancel));
		controlsCancel[0].SetCharacter(35);
		controlsCancel[1].SetCharacter(36);
		controlsCancel[2].SetCharacter(37);
		controlsCancel[3].SetCharacter(emptyOptionsChar);

		//Move
		const unsigned long controlsMoveTextOffset = bIsSLG ? 0x00047ce2 : 0x0005a6a6;
		MainMenuText controlsMove[2];
		sakuraFileData.ReadData(controlsMoveTextOffset, (char*)controlsMove, sizeof(controlsMove));
		controlsMove[0].SetCharacter(38);
		controlsMove[1].SetCharacter(39);

		//Attack
		const unsigned long controlsAttackTextOffset = bIsSLG ? 0x00047cf6 : 0x0005a6ba;
		MainMenuText controlsAttack[4];
		sakuraFileData.ReadData(controlsAttackTextOffset, (char*)controlsAttack, sizeof(controlsAttack));
		controlsAttack[0].SetCharacter(40);
		controlsAttack[1].SetCharacter(41);
		controlsAttack[2].SetCharacter(42);
		controlsAttack[3].SetCharacter(emptyOptionsChar);

		//Defend
		const unsigned long controlsDefendTextOffset = bIsSLG ? 0x00047d32 : 0x0005a6f6;
		MainMenuText controlsDefendText[2];
		sakuraFileData.ReadData(controlsDefendTextOffset, (char*)controlsDefendText, sizeof(controlsDefendText));
		controlsDefendText[0].SetCharacter(43);
		controlsDefendText[1].SetCharacter(44);

		//Charge
		const unsigned long controlsChargeTextOffset = bIsSLG ? 0x00047d1e : 0x0005a6e2;
		MainMenuText controlsChargeText[2];
		sakuraFileData.ReadData(controlsChargeTextOffset, (char*)controlsChargeText, sizeof(controlsChargeText));
		controlsChargeText[0].SetCharacter(45);
		controlsChargeText[1].SetCharacter(46);

		//View All
		const unsigned long controlsViewAllTextOffset = bIsSLG ? 0x00047d5a : 0x0005a71e;
		MainMenuText controlsViewAllText[2];
		sakuraFileData.ReadData(controlsViewAllTextOffset, (char*)controlsViewAllText, sizeof(controlsViewAllText));
		controlsViewAllText[0].SetCharacter(47);
		controlsViewAllText[1].SetCharacter(48);

		//Advice
		const unsigned long controlsAdviceTextOffset = bIsSLG ? 0x00047d46 : 0x0005a70a;
		MainMenuText controlsAdviceText[2];
		sakuraFileData.ReadData(controlsAdviceTextOffset, (char*)controlsAdviceText, sizeof(controlsAdviceText));
		controlsAdviceText[0].SetCharacter(49);
		controlsAdviceText[1].SetCharacter(50);

		//Ok
		const unsigned long controlsOk2TextOffset = bIsSLG ? 0x00047dbe : 0x0005a782;
		MainMenuText controlsOk2Text[2];
		sakuraFileData.ReadData(controlsOk2TextOffset, (char*)controlsOk2Text, sizeof(controlsOk2Text));
		controlsOk2Text[0].SetCharacter(33);
		controlsOk2Text[1].SetCharacter(34);

		//NoSetting
		const unsigned long controlsNoSettingTextOffset = bIsSLG ? 0x00047cba : 0x0005a67e;
		MainMenuText controlsNoSetting[4];
		sakuraFileData.ReadData(controlsNoSettingTextOffset, (char*)controlsNoSetting, sizeof(controlsNoSetting));
		controlsNoSetting[0].SetCharacter(51);
		controlsNoSetting[1].SetCharacter(52);
		controlsNoSetting[2].SetCharacter(53);
		controlsNoSetting[3].SetCharacter(54);

		//Default
		const unsigned long controlsDefaultTextOffset = bIsSLG ? 0x00047c92 : 0x0005a656;
		MainMenuText controlsDefault[4];
		sakuraFileData.ReadData(controlsDefaultTextOffset, (char*)controlsDefault, sizeof(controlsDefault));
		controlsDefault[0].SetCharacter(55);
		controlsDefault[1].SetCharacter(56);
		controlsDefault[2].SetCharacter(57);
		controlsDefault[3].SetCharacter(emptyOptionsChar);
		//X
		const unsigned long controlsXOffset = bIsSLG ? 0x0004830a : 0x0005acce;
		MainMenuText controlsXText[1];
		sakuraFileData.ReadData(controlsXOffset, (char*)controlsXText, sizeof(controlsXText));
		controlsXText[0].SetCharacter(58);

		//X for accept
		const unsigned long controlsXForAcceptOffset = bIsSLG ? 0x00047c38 : 0x0005a5fc;
		MainMenuText controlsXForAcceptText[1];
		sakuraFileData.ReadData(controlsXForAcceptOffset, (char*)controlsXForAcceptText, sizeof(controlsXForAcceptText));
		controlsXForAcceptText[0].SetCharacter(58);

		//Exit
		const unsigned long controlsExitOffset = bIsSLG ? 0x00047c6a : 0x0005a62e;
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
	compressor.CompressData((void*)translatedData.GetData(), (numCharactersInFontSheet*16*8));//translatedData.GetDataSize() - (numCharactersInFontSheet*16*8));
	
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
	sakuraFileData.WriteData(optionsFontSheetOffset, optionsCompressedData.mpCompressedData, optionsCompressedData.mDataSize);

	//Pad compressed data is it is divisible by 2
	const unsigned long patchedFontSheetSize = compressor.mCompressedSize + (compressor.mCompressedSize%2);
	char* pCompressedFontSheet               = new char[patchedFontSheetSize];
	memset(pCompressedFontSheet, 0, patchedFontSheetSize);
	memcpy_s(pCompressedFontSheet, patchedFontSheetSize, compressor.mpCompressedData, compressor.mCompressedSize);

	//Compress bgnd image
	BmpToSakuraConverter patchedBgndImage;
	if( !patchedBgndImage.ConvertBmpToSakuraFormat(inMainMenuTranslatedBgnd, true) )
	{
		printf("PatchMainMenu: Couldn't convert image: %s.\n", inMainMenuTranslatedBgnd.c_str());
		return false;
	}

	SakuraCompressedData translatedBgndData;
	translatedBgndData.PatchDataInMemory(patchedBgndImage.mTileExtractor.mTiles[0].mpTile, patchedBgndImage.mTileExtractor.mTiles[0].mTileSize, true);

	//Intermediate Screen
	const string intermediateScreenPath = inTranslatedDataDirectory + "IntermediateScreen.bmp";
	BmpToSakuraConverter patchedIntermediateScreen;
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

	const unsigned long intermediateTextIndiceOffset = 0x0005f5c8;
	const char intermediateTextIndices[36] = {0, 1, 0, 2, 0, 13, 0, 13, 0, 13, 0, 0, 
											  0, 3, 0, 4, 0, 5, 0, 13, 0, 13, 0, 0, 
											  0, 6, 0, 7, 0, 8, 0, 9, 0, 10, 0, 11};
	//

	//Create text for the main menu
	const unsigned long mainMenuTextOffset           = 0x000061E4;
	const unsigned long mainMenuOptionTextOffset     = 0x000060D6;
	const unsigned long mainMenuContinueTextOffset   = 0x00006194;
	MainMenuText newGameText[8];
	MainMenuText continueText[8];
	MainMenuText optionText[5];
	memcpy_s((void*)newGameText, sizeof(newGameText), logoFileData.GetData() + mainMenuTextOffset, sizeof(newGameText));
	memcpy_s((void*)continueText, sizeof(continueText), logoFileData.GetData() + mainMenuContinueTextOffset, sizeof(continueText));
	memcpy_s((void*)optionText, sizeof(optionText), logoFileData.GetData() + mainMenuOptionTextOffset, sizeof(optionText));
	
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
	newGameText[5].SetCharacter(16, startX + 16*5);
	newGameText[6].SetCharacter(16, startX + 16*6);
	newGameText[7].SetCharacter(16, startX + 16*7);
	
	startX = -1 * (52/2);
	continueText[0].SetCharacter(5, startX);
	continueText[1].SetCharacter(6, startX + 16);
	continueText[2].SetCharacter(7, startX + 16*2);
	continueText[3].SetCharacter(8, startX + 16*3);
	continueText[4].SetCharacter(16, startX + 16*4);
	continueText[5].SetCharacter(16, startX + 16*5);
	continueText[6].SetCharacter(16, startX + 16*6);
	continueText[7].SetCharacter(16, startX + 16*7);

	startX = -1 * (44/2);
	optionText[0].SetCharacter(9,  startX);
	optionText[1].SetCharacter(10, startX + 16);
	optionText[2].SetCharacter(11, startX + 16*2);
	optionText[3].SetCharacter(16, startX + 16*3);
	optionText[4].SetCharacter(16, startX + 16*4);

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

bool FixupSLG(const string& rootDir, const string& outDir, const string& inTranslatedDirectory, const string& newFontPaletteFile, const string& inTempDir)
{
	//We need the palette from the patched stats menu image
	const string translatedWKLDirectory  = inTranslatedDirectory + string("WKL\\");
	const string patchedStatsImagePath   = translatedWKLDirectory + string("StatsMenuPatched.bmp");

	BmpToSakuraConverter statsImage;
	if( !statsImage.ConvertBmpToSakuraFormat(patchedStatsImagePath, true, BmpToSakuraConverter::BLACK) )
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
		BmpToSakuraConverter patchedBgndImage;
		if( !patchedBgndImage.ConvertBmpToSakuraFormat(mainMenuTranslatedBgnd, true) )
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
		const int offsetLipsStartLocationY     = 0x0002173D; //Change the cpommand from MOV 0xCC(-52), r11 to 0xCE(-50)
		const int offsetItemCountXOffset       = 0x0002512B;
		const int offsetOptionsFontSheet       = 0x0004a740;
		const int offsetBgndImagePalette       = 0x0004a1b8;
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
		memcpy_s((void*)(origSlgData.GetData() + offsetItemCountXOffset),	      origSlgData.GetDataSize(), (void*)&itemCountXOffset,						sizeof(itemCountXOffset));
		memcpy_s((void*)(origSlgData.GetData() + offsetOptionsFontSheet),		  origSlgData.GetDataSize(), (void*)optionsCompressedData.mpCompressedData, optionsCompressedData.mDataSize);
		memcpy_s((void*)(origSlgData.GetData() + ofsetBgndImage),		          origSlgData.GetDataSize(), (void*)translatedBgndData.mpCompressedData,    translatedBgndData.mDataSize);
		memcpy_s((void*)(origSlgData.GetData() + offsetBgndImagePalette),		  origSlgData.GetDataSize(), (void*)patchedBgndImage.mPalette.GetData(),    patchedBgndImage.mPalette.GetSize());
		//***Done Fixing Max Character Lengths***

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
	const string originalSakuraFile = rootSakuraTaisenDirectory + "SAKURA";
	const string newSakuraFile      = patchedSakuraTaisenDirectory + "SAKURA";
	if( !CopyFile(originalSakuraFile.c_str(), newSakuraFile.c_str(), FALSE) )
	{
		return false;
	}

	const string originalLogoFile = rootSakuraTaisenDirectory + "SAKURA1\\LOGO.SH2";
	const string newLogoFile      = patchedSakuraTaisenDirectory + "SAKURA1\\LOGO.SH2";
	if(!CopyFile(originalLogoFile.c_str(), newLogoFile.c_str(), FALSE))
	{
		return false;
	}

	const string originalIcatallFile = rootSakuraTaisenDirectory + "SAKURA1\\ICATALL.DAT";
	const string newIcatallFile      = patchedSakuraTaisenDirectory + "SAKURA1\\ICATALL.DAT";
	if(!CopyFile(originalIcatallFile.c_str(), newIcatallFile.c_str(), FALSE))
	{
		return false;
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
	BmpToSakuraConverter patchedLogo;
	if( !patchedLogo.ConvertBmpToSakuraFormat(logoFileName, false) )
	{
		printf("PatchMiniSwim: Couldn't convert image: %s.\n", logoFileName.c_str());
		return false;
	}

	//Patch logo
	miniGameFile.WriteData(0x0003cf28, patchedLogo.GetImageData(), patchedLogo.GetImageDataSize());

	//Open translated fontsheet
	const string fontSheetFileName = inTranslatedDataDirectory + "MiniSwimFontsheet.bmp";
	BmpToSakuraConverter patchedFontSheet;
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
	short mainText[]   = {1, 2, 3, 4, 5, 0x0a0d, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 0x0a0d, 19, 20, 21, 22, 23, 24, 25, 26, 27, 0};
	short buttonText[] = {28, 29, 0x0a0d, 30, 31, 0x0a0d, 30, 31, 0x0a0d, 30, 31, 0};
	
	SwapEndiannessForArrayOfShorts(mainText, sizeof(mainText));
	SwapEndiannessForArrayOfShorts(buttonText, sizeof(buttonText));

	miniGameFile.WriteData(0x00056b28, (char*)mainText, sizeof(mainText));
	miniGameFile.WriteData(0x00056bc2, (char*)buttonText, sizeof(buttonText));

	return true;
}

bool PatchMiniGames(const string& rootSakuraDirectory, const string& patchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	printf("Patching Minigames\n");

	//Open translated option 1 (start)
	const string option1FileName = inTranslatedDataDirectory + "MiniGameOption1.bmp";
	BmpToSakuraConverter patchedOption1;
	if( !patchedOption1.ConvertBmpToSakuraFormat(option1FileName, false) )
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", option1FileName.c_str());
		return false;
	}

	//Open translated option 2 (practice)
	const string option2FileName = inTranslatedDataDirectory + "MiniGameOption2.bmp";
	BmpToSakuraConverter patchedOption2;
	if( !patchedOption2.ConvertBmpToSakuraFormat(option2FileName, false) )
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", option2FileName.c_str());
		return false;
	}

	//Open translated time image
	const string timeImageFileName = inTranslatedDataDirectory + "MiniGameTime.bmp";
	BmpToSakuraConverter patchedTimeImage;
	if( !patchedTimeImage.ConvertBmpToSakuraFormat(timeImageFileName, false) )
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", timeImageFileName.c_str());
		return false;
	}

	//Open translated seconds image
	const string secondsImageFileName = inTranslatedDataDirectory + "MiniGameSeconds.bmp";
	BmpToSakuraConverter patchedSecondsImage;
	if( !patchedSecondsImage.ConvertBmpToSakuraFormat(secondsImageFileName, false) )
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", secondsImageFileName.c_str());
		return false;
	}

	//Open translated N\A image
	const string naImageFileName = inTranslatedDataDirectory + "MiniGameNAPatched.bmp";
	BmpToSakuraConverter naImage;
	if (!naImage.ConvertBmpToSakuraFormat(naImageFileName, false))
	{
		printf("PatchMiniGames: Couldn't convert image: %s.\n", naImageFileName.c_str());
		return false;
	}
	struct MiniGameFileOffsets
	{
		string       fileName;
		unsigned int option1Offset;
		unsigned int option2Offset;
		unsigned int timeImageOffset;
		unsigned int secondsImageOffset;
		unsigned int notAvailableImageOffset;
	};

	const int numMiniGameFiles = 8;
	MiniGameFileOffsets miniGameOption1Offsets[numMiniGameFiles] = 
	{
		"HANAMAIN.BIN", 0x00014688, 0x00013c08, 0x00016628, 0x000165a8, 0x00015508,
		"MINICOOK.BIN", 0x000124bc, 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001333c,
		"MINIHANA.BIN", 0x000124bc, 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001333c,
		"MINIMAIG.BIN", 0x000124bc, 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001333c,
		"MINISHOT.BIN", 0x000124bc, 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001333c,
		"MINISLOT.BIN", 0x000124bc, 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001333c,
		"MINISOJI.BIN", 0x000124bc, 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001333c,
		"MINISWIM.BIN", 0x000124bc, 0x00011a3c, 0x0001445c, 0x000143dc, 0x0001333c,
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

		miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].option1Offset,           patchedOption1.GetImageData(),      patchedOption1.GetImageDataSize());
		miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].option2Offset,           patchedOption2.GetImageData(),      patchedOption2.GetImageDataSize());
		miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].timeImageOffset,         patchedTimeImage.GetImageData(),    patchedTimeImage.GetImageDataSize());
		miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].secondsImageOffset,      patchedSecondsImage.GetImageData(), patchedSecondsImage.GetImageDataSize());
		miniGameFile.WriteData(miniGameOption1Offsets[miniGameIndex].notAvailableImageOffset, naImage.GetImageData(),             naImage.GetImageDataSize());
	}

	bool bResult = PatchMiniSwim(patchedSakuraDirectory, inTranslatedDataDirectory);

	return bResult;
}

bool PatchGame(const string& rootSakuraTaisenDirectory, const string& patchedSakuraTaisenDirectory, const string& translatedTextDirectory, const string& fontSheetFileName, const string& /*originalPaletteFileName*/, 
	const string &patchedTMapSPDataPath, const string& inMainMainFontSheetPath, const string& inMainMenuTranslatedBgnd, const string& inPatchedOptionsImage, const string& inTranslatedDataDirectory,
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

	//Step 1
	if( !CreateTranslatedFontSheet(fontSheetFileName, tempDir) )
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
	if( !FixupSLG(rootSakuraTaisenDirectory, patchedSakuraTaisenDirectory, inTranslatedDataDirectory, newPaletteFileName, tempDir) )
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
	if( !PatchTMapSP(patchedSakuraTaisenDirectory, patchedTMapSPDataPath) )
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
	printf("CreateTBLSpreadsheets dialogImageDirectory duplicatesFile sakura1Directory\n");
	printf("CreateMesSpreadsheets dialogImageDirectory rootSakuraTaisenDirectory\n");
	printf("CreateWKLSpreadsheets dialogImageDirectory duplicatesFile rootSakuraTaisenDirectory\n");
	printf("CreateTMapSpSpreadsheet imageDirectory\n");
	printf("ExtractImages fileName paletteFile width height outDirectory\n");
	printf("Extract8BitImage fileName paletteFile offset width height numColors[256, 128] pngOrBmp[0 png, 1 bmp] outDirectory\n");
	printf("ExtractFCEFiles rootSakuraTaisenDirectory paletteFile outDirectory\n");
	printf("ExtractFACEFiles rootSakuraTaisenDirectory outDirectory\n");
	printf("ExtractWKLFiles rootSakuraTaisenDirectory outDirectory\n");
	printf("ExtractTMapSP rootSakuraTaisenDirectory paletteFile outDirectory\n");
	printf("PatchTMapSP sakuraDirectory patchDataPath\n");
	printf("PrintPaletteColors fileName\n");
	printf("CompressFile inFilePath outFilePath\n");
	printf("FindCompressedData compressedFile uncompressedFile outDirectory\n");
	printf("FindCompressedDataInDir inDirectory uncompressedFile outDirectory\n");
	printf("ExtractMiniGameData rootSakuraDirectory outDirectory\n");
	printf("PatchGame rootSakuraTaisenDirectory patchedSakuraTaisenDirectory translatedTextDirectory fontSheet originalPalette patchedTMapSpDataPath mainMenuFontSheetPath mainMenuBgndPatchedImage optionsImagePatched translatedDataDirectory extractedWklDir\n");
}

int main(int argc, char *argv[])
{
#if 0
	FindDiff();

	if(argc == 2)
	{
		string command(argv[1]);
		command = "";

		return 1;
	}
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
		const string rootSakuraTaisenDirectory    = string(argv[2]) + Seperators;
		const string patchedSakuraTaisenDirectory = string(argv[3]) + Seperators;
		const string translatedTextDirectory      = string(argv[4]);
		const string fontSheet                    = string(argv[5]);
		const string originalPalette              = string(argv[6]);
		const string patchedTMapSpDataPath        = string(argv[7]) + Seperators;
		const string mainMenuFontSheetPath        = string(argv[8]);
		const string mainMenuTranslatedBgnd       = string(argv[9]);
		const string patchedOptionsImage          = string(argv[10]);
		const string translatedDataDirectory      = string(argv[11]) + Seperators;
		const string extractedWklDirectory        = string(argv[12]) + Seperators;

		PatchGame(rootSakuraTaisenDirectory, patchedSakuraTaisenDirectory, translatedTextDirectory, fontSheet, originalPalette, patchedTMapSpDataPath, mainMenuFontSheetPath, mainMenuTranslatedBgnd, patchedOptionsImage, 
				  translatedDataDirectory, extractedWklDirectory);
	}
	else if(command == "CreateTBLSpreadsheets" && argc == 5 )
	{
		const string dialogImageDirectory = string(argv[2]);
		const string duplicatesFile       = string(argv[3]);
		const string sakura1Directory     = string(argv[4]) + Seperators;

		CreateTBLSpreadsheets(dialogImageDirectory, duplicatesFile, sakura1Directory);
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
	else if(command == "Extract8BitImage" && argc == 10 )
	{
		const string fileName     = string(argv[2]);
		const string paletteFile  = string(argv[3]);
		const int    offset       = atoi(argv[4]);
		const int    width        = atoi(argv[5]);
		const int    height       = atoi(argv[6]);
		const int    numColors    = atoi(argv[7]);
		const int    pngOrBmp     = atoi(argv[8]);
		const string outDirectory = string(argv[9]) + Seperators;

		Extract8BitImage(fileName, paletteFile, offset, width, height, numColors, pngOrBmp == 1, outDirectory);
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
			if( fc.mFileName == "SAKURA4" || fc.mFileName == "VCEADV" || fc.mFileName == "OTOMEADP.DAT" || fc.mFileName == "SLG1ADP.DAT" || fc.mFileName == "MINIADP.DAT")
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
	else if(command == "ExtractMiniGameData" && argc == 4)
	{
		const string rootSakuraDir(argv[2]);
		const string outDir(argv[3]);

		ExtractMiniSwimText(rootSakuraDir, outDir);
	}
	else
	{
		PrintHelp();
	}
#endif
	return 1;
}
