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
#include "..\Utils\Utils.h"
#include <assert.h>

using std::vector;
using std::string;
using std::list;
using std::map;


class SakuraTranslationTable
{
public:
	const unsigned short GetIndex(char inChar) const
	{
		if( inChar == '@' )
		{
			return 134;
		}
		
		if( inChar == '\n' )
		{
			return 0x0a0d;
		}
		
		return inChar + 1;
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

	bool AddChar(unsigned short index)
	{
		mChars.push_back( std::move(SakuraChar(index)) );

		return true;
	}

	bool AddChar(char row, char column)
	{
		if( row == 0 && column == 0 )
		{
			return false; 
		}

		mChars.push_back( std::move(SakuraChar(row, column)) );

		return true;
	}

	void AddString(const string& inString)
	{
		mChars.push_back( std::move(SakuraChar(0)) );
		mChars.push_back( std::move(SakuraChar(0)) );

		for(string::const_iterator iter = inString.begin(); iter != inString.end(); ++iter)
		{
			const unsigned short value = GTranslationLookupTable.GetIndex(*iter);
			mChars.push_back( std::move(SakuraChar(value)) );
		}

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

	const char* GetCharacterTile(const SakuraString::SakuraChar& sakuraChar)
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
		unsigned short mUnknown;
		unsigned short mOffsetFromPrevString;

		SakuraStringInfo(unsigned short inFirst, unsigned short inSecond) : mUnknown(inFirst), mOffsetFromPrevString(inSecond)
		{
			mFullValue = (inFirst << 16) + inSecond;
		}
	};

	FileNameContainer         mFileName;
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
	SakuraTextFile(const FileNameContainer& fileName) : mFileName(fileName), mFileSize(0), mpFile(nullptr), mpBuffer(nullptr){}

	SakuraTextFile(SakuraTextFile&& rhs) : mFileName(std::move(rhs.mFileName)), mLines(std::move(rhs.mLines)), mHeader(std::move(rhs.mHeader)), mFooter(std::move(rhs.mFooter)), 
		                                   mDataSegments(std::move(rhs.mDataSegments)), mStringInfoArray(std::move(rhs.mStringInfoArray)), mFileSize(rhs.mFileSize),
		                                   mpFile(rhs.mpFile), mpBuffer(std::move(rhs.mpBuffer))
	{
		rhs.mpBuffer = nullptr;
		rhs.mpFile   = nullptr;
	}

	SakuraTextFile& operator=(SakuraTextFile&& rhs)
	{
		mFileName        = std::move(rhs.mFileName);
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

	void ReadInText()
	{
		ParseHeader();
		ParseStrings();
		ParseFooter();

		assert( mStringInfoArray.size() == mLines.size() );
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

		for(size_t i = 0; i < numStrings; ++i)
		{
			const unsigned int offsetToString = mStringInfoArray[i].mOffsetFromPrevString*2 + mHeader.dataSize;
			assert(offsetToString < mFileSize);

			SakuraString newLineOfText;
			unsigned short* pWordBuffer = (unsigned short*)&mpBuffer[offsetToString];
			bool bNonZeroValueFound     = false;
			int currentIndex            = 0;
			unsigned short offsetToStringData = 0;
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

				newLineOfText.AddChar(currValue);

				if( currValue == 0 && bNonZeroValueFound )
				{
					break;
				}
			}

			newLineOfText.mOffsetToStringData = mStringInfoArray[i].mUnknown >= 0xC351 ? 2 : offsetToStringData;
			mLines.push_back(newLineOfText);
		}
	}

	void ParseStringsOld()
	{
		const unsigned long startTextBlock = SwapByteOrder( *((unsigned short*)mpBuffer) ) * 2;
		const unsigned long endTextBlock   = SwapByteOrder( *((unsigned short*)(mpBuffer + 2)) ) * 2;
		unsigned short*      pWordBuffer   = (unsigned short*)(mpBuffer + startTextBlock);
		const unsigned long numEntries     = (endTextBlock - startTextBlock)/2;

		assert(endTextBlock > startTextBlock);
		assert( ((endTextBlock - startTextBlock) % 2) == 0 );

		unsigned long currentIndex = 0;
		while(currentIndex < numEntries)
		{
			//Leading 0s
			unsigned long startIndex = currentIndex;
			unsigned short currValue  = 0;
			while( 1 )
			{
				currValue = pWordBuffer[currentIndex];

				if(currValue != 0 && currValue != 0xffff )
				{
					break;
				}
				++currentIndex;
			}
			if( currentIndex > startIndex )
			{
				const unsigned long numLeadingZeros = currentIndex - startIndex;
				mDataSegments.push_back( std::move(SakuraDataSegment((char*)&pWordBuffer[startIndex], numLeadingZeros*sizeof(short))) );
			}

			//String
			SakuraString newLineOfText;
			while(1)
			{
				currValue = pWordBuffer[currentIndex];
				if( currValue == 0 )
				{
					break;
				}

				//Convert to big endian
				currValue = SwapByteOrder(currValue);

				newLineOfText.AddChar((currValue&0xff00) >> 8, currValue&0x00ff);
				++currentIndex;
			}
			mLines.push_back(newLineOfText);

			//End zeros
			startIndex = currentIndex;
			while( 1 )
			{
				currValue = pWordBuffer[currentIndex];
				if(currValue != 0 && currValue != 0xffff )
				{
					break;
				}
				++currentIndex;

				if( currentIndex == numEntries )
				{
					break;
				}
			}

			const unsigned long numEndingZeroes = currentIndex - startIndex;
			assert( (long)numEndingZeroes );
			mDataSegments.push_back( std::move(SakuraDataSegment((char*)&pWordBuffer[startIndex], numEndingZeroes*sizeof(short))) );
		}
	}
};

struct SakuraTextFileFixedHeader
{
	unsigned short mOffsetToTable;
	unsigned short mTableEnd;
	vector<unsigned int> mStringInfo;

	void CreateFixedHeader(const vector<SakuraTextFile::SakuraStringInfo>& inInfo, const SakuraTextFile& inSakuraFile, const vector<SakuraString>& inStrings)
	{
		//All TBL files start with this entries
		mStringInfo.push_back( SwapByteOrder(inInfo[0].mFullValue) );
		
		unsigned short prevValue = 0;
		const size_t numEntries  = inInfo.size() - 1;
		for(size_t i = 0; i < numEntries; ++i)
		{
			//const unsigned short trailingZeroes = (unsigned short)(inSakuraFile.mDataSegments[i+1].dataSize)/2;
			const unsigned short newSecondValue = (unsigned short)inStrings[i].mChars.size() + prevValue; //+ trailingZeros;
			const unsigned int   newValue       = ((unsigned int)(inInfo[i+1].mUnknown) << 16) + (unsigned int)newSecondValue;
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
		
		assert(stringTableSize <= 0xffff);
		mOffsetToTable = SwapByteOrder( ((unsigned short*)inSakuraFile.mHeader.pData)[0] );
		mTableEnd      = (unsigned short)((mOffsetToTable*2 + (unsigned short)stringTableSize) / 2);

		assert( (mOffsetToTable + (unsigned short)stringTableSize)%2 == 0 );
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

		sakuraFile.ReadInText();

		outText.push_back( std::move(sakuraFile) );
	}
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
		const string outFileName = outDirectory + textFile.mFileName.mNoExtension + txt;
		const string infoFileName = outDirectory + textFile.mFileName.mNoExtension + info + txt;

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

		printf("Dumping text for: %s\n", textFile.mFileName.mFileName.c_str());
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
			fprintf(pOutInfoFile, "%02x %02x %i DiffFromPrev: %i\n", stringInfo.mUnknown, stringInfo.mOffsetFromPrevString, stringInfo.mOffsetFromPrevString, diffFromPrev);
		}

		fclose(pOutFile);
		fclose(pOutInfoFile);
		++fileNum;
	}
}

bool ExtractFontSheetAsBitmap(const FileNameContainer& inFileNameContainer, const string& outFileName, const FileData& inPaletteFile)
{
	FileData fontSheet;
	if( !fontSheet.InitializeFileData(inFileNameContainer) )
	{
		return false;
	}

	printf("Extracting: %s\n", inFileNameContainer.mFileName.c_str());
	
	const int tileDimX          = 8;//16;
	const int tileDimY          = 8;//16;
	const int tileBytes         = (tileDimX*tileDimY)/2; //4bits per pixel, so only half the amount of bytes as pixels
	const int tilesPerRow       = 255;
	const int bytesPerTile      = tileBytes;
	const int bytesPerTileRow   = bytesPerTile*tilesPerRow;
	const int numRows           = (int)ceil( fontSheet.GetDataSize() / (float)bytesPerTileRow);
	const int numColumns        = numRows > 0 ? tilesPerRow : fontSheet.GetDataSize()/bytesPerTileRow;
	const int imageHeight       = numRows*tileDimY;
	const int imageWidth        = numColumns*tileDimX;
	
	//Create 32bit palette from the 16 bit(5:5:5 bgr) palette in SakuraTaisen
	PaletteData paletteData;
	paletteData.CreateFrom15BitData(inPaletteFile.GetData(), inPaletteFile.GetDataSize());

	//Allocate space for tiled data
	int numTiles                     = fontSheet.GetDataSize()/tileBytes;
	int currTileRow                  = 0;
	int currTileCol                  = 0;
	const int bytesInEachTilesWidth  = tileDimX/2;
	const int numTiledBytes          = (numRows*bytesPerTileRow);// + numColumns*bytesInEachTilesWidth;
	char* pOutTiledData              = new char[numTiledBytes];
	const int bytesPerHorizontalLine = bytesInEachTilesWidth*numColumns;

	memset(pOutTiledData, 0, numTiledBytes);

	//Fill in data
	for(int tileIndex = 0; tileIndex < numTiles; ++tileIndex)
	{
		const char* pTile       = fontSheet.GetData() + tileIndex*tileBytes;
		const int outTileOffset = currTileRow*bytesPerHorizontalLine*tileDimY + currTileCol*bytesInEachTilesWidth;
		char* pOutTile          = pOutTiledData + outTileOffset;
		int tilePixel           = 0;
		for(int r = 0; r < tileDimY; ++r)
		{
			for(int c = 0; c < bytesInEachTilesWidth; ++c)
			{
				assert(outTileOffset + r*bytesPerHorizontalLine + c < numTiledBytes);
				pOutTile[r*bytesPerHorizontalLine + c] = pTile[tilePixel++];
			}
		}

		if( ++currTileCol >= numColumns )
		{
			currTileCol = 0;
			++currTileRow;
		}
	}

	BitmapWriter fontBitmap;
	fontBitmap.CreateBitmap(outFileName, imageWidth, -imageHeight, 4, pOutTiledData, numTiledBytes, paletteData.GetData(), paletteData.GetSize());	

	delete[] pOutTiledData;

	return true;
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

	const string bmpExtension(".bmp");
	string outFileName;
	for(const FileNameContainer& fileNameInfo : textFiles)
	{
		outFileName = outDir + fileNameInfo.mNoExtension + bmpExtension;
		ExtractFontSheetAsBitmap(fileNameInfo, outFileName, paletteFile);
	}
}

void DumpSakuraText(const vector<FileNameContainer>& inAllFiles, const string& inOutputDir)
{
	vector<FileNameContainer> textFiles;
	GetAllFilesOfType(inAllFiles, "TBL.BIN", textFiles);

	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(textFiles, sakuraTextFiles);

	DumpExtractedSakuraText(sakuraTextFiles, inOutputDir);
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

	//Find all the text files
	vector<FileNameContainer> textFiles;
	GetAllFilesOfType(allFiles, "TBL.BIN", textFiles);

	//There needs to be a font sheet for every text file 
	if( textFiles.size() != fontFiles.size() )
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
		const string sakuraFileNum   = sakuraText.mFileName.mNoExtension.substr(0, sakuraText.mFileName.mNoExtension.size() - 3);

		if( sakuraFileNum != fontSheetNumber )
		{
			printf("ExtractText: Font sheet and text file mistmatch. %s %s", fontSheetName.mNoExtension.c_str(), sakuraText.mFileName.mNoExtension.c_str());
			return;
		}
		
		//Create output directory for this file
		string fileOutputDir = inOutputDirectory + sakuraText.mFileName.mNoExtension + string("\\");
		if( !CreateDirectoryHelper(fileOutputDir) )
		{
			continue;
		}

		printf("Dumping dialog for: %s\n", sakuraText.mFileName.mNoExtension.c_str());

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
				if( sakuraChar.mIndex == 0 )
				{
					continue;
				}

				if( sakuraChar.IsNewLine() )
				{
					++currRow;
					currCol = 0;
					continue;
				}

				const char* pData = sakuraFontSheet.GetCharacterTile(sakuraChar);

				sakuraStringBmp.AddTile(pData, sakuraFontSheet.GetTileSizeInBytes(), currCol*16, currRow*16, tileDim, tileDim);

				++currCol;
			}
			
			const string bitmapName = fileOutputDir + std::to_string(stringIndex) + extension;
			sakuraStringBmp.WriteToFile(bitmapName);

			++stringIndex;
		}
	}
}

void CreateTranslatedFontSheet(const string& inTranslatedFontSheet, const string& outPath)
{
	//Read in translated font sheet
	BitmapReader origTranslatedBmp;
	if( !origTranslatedBmp.ReadBitmap(inTranslatedFontSheet) )
	{
		return;
	}

	TileExtractor tileExtractor;
	if( !tileExtractor.ExtractTiles(8, 8, origTranslatedBmp) )
	{
		return;
	}

	//Convert it to the SakuraTaisen format
	PaletteData sakuraPalette;
	sakuraPalette.CreateFrom32BitData(origTranslatedBmp.mBitmapData.mPaletteData.mpRGBA, origTranslatedBmp.mBitmapData.mPaletteData.mSizeInBytes);

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
		printf("Alpha Color not found.  Palette will not be correct. \n");
		indexOfAlphaColor = 0;
	}


	const string outPaletteName = outPath + string("OutPalette.BIN");
	const string outTableName   = outPath + string("TranslatedKNJ.BIN");

	//Ouptut the palette
	FileWriter outPalette;
	if( !outPalette.OpenFileForWrite(outPaletteName) )
	{
		return;
	}
	outPalette.WriteData(sakuraPalette.GetData(), sakuraPalette.GetSize());

	//Write out the SakuraTaisen TBL file
	FileWriter outTable;
	outTable.OpenFileForWrite(outTableName);
	for(TileExtractor::Tile& tile : tileExtractor.mTiles)
	{
		for(unsigned int i = 0; i < tile.mTileSize; i++)
		{
			const unsigned short paletteIndex1 = (tile.mpTile[i] & 0xF0) >> 4; 
			const unsigned short paletteIndex2 = (tile.mpTile[i] & 0x0F);
			
			if( paletteIndex1 == 0 )
			{
				tile.mpTile[i] = (char)((indexOfAlphaColor << 4) + paletteIndex2);
			}
			else if( paletteIndex1 == indexOfAlphaColor )
			{
				tile.mpTile[i] = (char)paletteIndex2;
			}

			if( paletteIndex2 == 0 )
			{
				tile.mpTile[i] = (char)((tile.mpTile[i]&0xF0) + indexOfAlphaColor);
			}
			else if( paletteIndex2 == indexOfAlphaColor )
			{
				tile.mpTile[i] = tile.mpTile[i]&0xF0;
			}
		}

		outTable.WriteData(tile.mpTile, tile.mTileSize);
	}
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
		const int maxCharsPerLine = 15;
		const int maxLines        = 3;
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

void PatchKNJ(const string& rootDirectory, const string& newKNJ, const string& outDir)
{
	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(string(rootDirectory), allFiles);

	//Get all knj files
	vector<FileNameContainer> knjFiles;
	GetAllFilesOfType(allFiles, "KNJ.BIN", knjFiles);

	//Load patched file
	FileData newFileData;
	if( !newFileData.InitializeFileData(newKNJ.c_str(), newKNJ.c_str()) )
	{
		return;
	}

	//Patch original files
	string outFileName;
	for(const FileNameContainer& knj : knjFiles)
	{
		FileData fileToPatch;
		if( !fileToPatch.InitializeFileData(knj) )
		{
			continue;
		}

		printf("Patching %s \n", knj.mNoExtension.c_str());

		outFileName = outDir + knj.mFileName;

		FileWriter patchedFile;
		if( patchedFile.OpenFileForWrite(outFileName) )
		{
			patchedFile.WriteData(newFileData.GetData(), newFileData.GetDataSize());
		}
		else
		{
			printf("Unable to patch: %s \n", knj.mNoExtension.c_str());
		}
	}
}

void PatchPalettes(const string& rootDirectory, const string& originalPalette, const string& newPalette, const string& outDir)
{
	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(string(rootDirectory), allFiles);

	//Load original data
	FileData origData;
	if( !origData.InitializeFileData(originalPalette.c_str(), originalPalette.c_str()) )
	{
		printf("Unable to patch palettes because original palette not found.\n");
		return;
	}
	
	//Load new palette data
	FileData newPaletteData;
	if( !newPaletteData.InitializeFileData(newPalette.c_str(), newPalette.c_str()) )
	{
		printf("Unable to patch palettes because new palette not found.\n");
		return;
	}

	vector<FileNameContainer> foundFiles;

	//Go trhough all files in the SakuraTaisen directory searching for palette data
	for(const FileNameContainer& sakuraFile : allFiles)
	{
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

				const string& outFileName = outDir + sakuraFile.mFileName;
				FileWriter outFile;
				if( !outFile.OpenFileForWrite(outFileName) )
				{
					printf("Unable to create patched file: %s \n", outFileName.c_str());
					continue;
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
		printf("Patched: %s\n", patchedFile.mFileName.c_str());
	}
}

void InsertText(const string& rootSakuraTaisenDirectory, const string& translatedTextDirectory, const string& outDirectory)
{
#define IncrementLine()\
    ++currLine;\
    charCount = 0;\
    if( currLine > maxLines ) \
	{\
		printf("Unable to fully insert line because it is longer than %i characters: %s\n", maxLines*maxCharsPerLine, textLine.mFullLine.c_str());\
		bFailedToAddLine = true;\
		break;\
	}\
    translatedString.AddChar( GTranslationLookupTable.GetIndex('\n') );

	//Insert new line if needed
#define ConditionallyAddNewLine()\
	if( word.size() + charCount > maxCharsPerLine )\
	{\
		IncrementLine()\
	}

	//Find all translated text files
	vector<FileNameContainer> translatedTextFiles;
	FindAllFilesWithinDirectory(string(translatedTextDirectory), translatedTextFiles);
	if( !translatedTextFiles.size() )
	{
		return;
	}

	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(rootSakuraTaisenDirectory, allFiles);
	
	//Get all files containing dialog
	vector<FileNameContainer> textFiles;
	GetAllFilesOfType(allFiles, "TBL.BIN", textFiles);

	//Extract the text
	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(textFiles, sakuraTextFiles);

	//Insert text
	for(const SakuraTextFile& sakuraFile : sakuraTextFiles)
	{
		//Search for the corresponding translated file name
		for(const FileNameContainer& translatedFileName : translatedTextFiles)
		{
			if( translatedFileName.mNoExtension == sakuraFile.mFileName.mNoExtension )
			{
				//Open translated text file
				TextFileData translatedFile(translatedFileName);
				if( !translatedFile.InitializeTextFile() )
				{
					break;
				}

				//Create output file
				const string outFileName = outDirectory + sakuraFile.mFileName.mFileName;
				FileWriter outFile;
				if( !outFile.OpenFileForWrite(outFileName) )
				{
					break;
				}

				//Make sure we have the correct amount of lines
				if( sakuraFile.mLines.size() < translatedFile.mLines.size() )
				{
					printf("Unable to translate file: %s because the translation has too many lines.\n", translatedFileName.mNoExtension.c_str());
					break;
				}

				//Get converted lines of text
				const int maxCharsPerLine       = 15;
				const int maxLines              = 3;
				const size_t numTranslatedLines = translatedFile.mLines.size();
				vector<SakuraString> translatedLines;
				for(size_t translatedLineIndex = 0; translatedLineIndex < numTranslatedLines; ++translatedLineIndex)
				{	
					const TextFileData::TextLine& textLine = translatedFile.mLines[translatedLineIndex];
					int charCount         = 0;
					int currLine          = 1;
					const size_t numWords = textLine.mWords.size();
					SakuraString translatedString;

					for(unsigned short initialZeroes = 0; initialZeroes < sakuraFile.mLines[translatedLineIndex].mOffsetToStringData; ++initialZeroes)
					{
						translatedString.AddChar(0);
					}

					for(size_t wordIndex = 0; wordIndex < numWords; ++wordIndex)
					{
						const string& word = textLine.mWords[wordIndex];
						bool bFailedToAddLine = false;

						if( word.size() > maxCharsPerLine )
						{
							printf("Unable to insert word because it is longer than %i characters: %s\n", maxCharsPerLine, word.c_str());
							continue;
						}

						//Insert new line if needed
						ConditionallyAddNewLine();

						//Add the word
						const size_t numLettersInWord = word.size();
						if( word.size() + charCount > maxCharsPerLine )
						{
							IncrementLine();
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
							if( word.size() + charCount > maxCharsPerLine )
							{
								ConditionallyAddNewLine();
							}
							else //Otherwise add a space
							{
								translatedString.AddChar( GTranslationLookupTable.GetIndex(' ') );
								++charCount;

								ConditionallyAddNewLine();
							}
						}
					}

					translatedString.AddChar(0);
					translatedLines.push_back( std::move(translatedString) );
				}

				//Fill out everything else with "Untranslated"
				SakuraString translatedSakuraString;
				translatedSakuraString.AddChar(0);
				translatedSakuraString.AddChar(0);
				translatedSakuraString.AddString( string("Untranslated") );
				translatedSakuraString.AddChar(0);
				const size_t untranslatedCount = sakuraFile.mLines.size() - translatedFile.mLines.size();
				for(size_t i = 0; i < untranslatedCount; ++i)
				{
					translatedLines.push_back( translatedSakuraString );
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
				outFile.WriteData(sakuraFile.mFooter.pData, sakuraFile.mFooter.dataSize);

				break;
			}
		}
	}
}

void FindDuplicateText(const string& dialogDirectory, const string& outFileName)
{
	TextFileWriter outFile;
	if( !outFile.OpenFileForWrite(outFileName) )
	{
		return;
	}

	//Find all translated text files
	vector<FileNameContainer> dialogFiles;
	FindAllFilesWithinDirectory(dialogDirectory, dialogFiles);
	if( !dialogFiles.size() )
	{
		return;
	}

	struct MatchingInfo
	{
		vector<const FileNameContainer*> matches;
	};
	map<const FileNameContainer*, MatchingInfo> matchingInfo;

	const size_t numDialogFiles = dialogFiles.size();
	for(size_t currFileIndex = 0; currFileIndex < numDialogFiles; ++currFileIndex)
	{
		const FileNameContainer& currFileInfo = dialogFiles[currFileIndex];

		FileData currFileData;
		if( !currFileData.InitializeFileData(currFileInfo) )
		{
			continue;
		}

		printf("Searching through %s\n", currFileInfo.mFullPath.c_str());

		//Compare with every other file
		for(size_t secondaryFileIndex = currFileIndex + 1; secondaryFileIndex < numDialogFiles; ++secondaryFileIndex)
		{
			const FileNameContainer& secondFileInfo = dialogFiles[secondaryFileIndex];

			map<const FileNameContainer*, MatchingInfo>::const_iterator existingMatch = matchingInfo.find(&secondFileInfo);
			if( existingMatch != matchingInfo.end() )
			{
				bool bPairFound = false;
				const size_t numMatches = existingMatch->second.matches.size();
				for(size_t i = 0; i < numMatches; ++i)
				{
					if(existingMatch->second.matches[i] == &secondFileInfo )
					{
						bPairFound = true;
						break;
					}
				}

				if( bPairFound )
				{
					break;
				}
			}

			FILE* pSecondFile = nullptr;
			const errno_t errorValue = fopen_s(&pSecondFile, secondFileInfo.mFullPath.c_str(), "rb");
			if( errorValue )
			{
				continue;
			}

			//Figure out the file size by
			fseek(pSecondFile, 0, SEEK_END);
			const unsigned long secondFileSize = ftell(pSecondFile);
			fseek(pSecondFile, 0, SEEK_SET);
			
			//Close the file
			fclose(pSecondFile);

			//If the file sizes are the same, then compare the data
			if( secondFileSize == currFileData.GetDataSize() )
			{
				FileData secondFileData;
				if( secondFileData.InitializeFileData(secondFileInfo) && currFileData.DoesThisFileContain(secondFileData, nullptr, false) )
				{
					matchingInfo[&currFileInfo].matches.push_back(&secondFileInfo);
				}
			}
		}
	}


	for(map<const FileNameContainer*, MatchingInfo>::const_iterator iter = matchingInfo.begin(); iter != matchingInfo.end(); ++iter)
	{
		fprintf(outFile.GetFileHandle(), "Matches For: %s\n", iter->first->mFullPath.c_str());

		for(size_t i = 0; i < iter->second.matches.size(); ++i)
		{
			fprintf(outFile.GetFileHandle(), "%s\n", iter->second.matches[i]->mFullPath.c_str());
		}

		printf("----------------\n");
	}
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
}

int main(int argc, char *argv[])
{
	if(argc == 1)
	{
		PrintHelp();
		return 1;
	}

	const string command(argv[1]);
	if( command == string("ExtractRawText") && argc == 4 )
	{
		const char*  pSearchDirectory = argv[2];
		const string outDirectory     = string(argv[3]) + string("\\");

		//Find all files within the requested directory
		vector<FileNameContainer> allFiles;
		FindAllFilesWithinDirectory(string(pSearchDirectory), allFiles);

		DumpSakuraText(allFiles, outDirectory);
	}
	else if( command == string("ExtractFontSheets") && argc == 5 )
	{
		const char*  pSearchDirectory = argv[2];
		const string paletteFileName  = string(argv[3]);
		const string outDirectory     = string(argv[4]) + string("\\");

		//Find all files within the requested directory
		vector<FileNameContainer> allFiles;
		FindAllFilesWithinDirectory(string(pSearchDirectory), allFiles);

		ExtractAllFontSheets(allFiles, paletteFileName, outDirectory);
	}
	else if( command == string("ExtractText" ) && argc == 5 )
	{
		const string searchDirectory   = string(argv[2]);
		const string paletteFileName   = string(argv[3]);
		const string outDirectory      = string(argv[4]) + string("\\");

		ExtractText(searchDirectory, paletteFileName, outDirectory);
	}
	else if (command == "CreateTranslatedFontSheets" && argc == 4 )
	{
		const string translatedFontSheet = string(argv[2]);
		const string outDirectory        = string(argv[3]) + string("\\");

		CreateTranslatedFontSheet(translatedFontSheet, outDirectory);
	}
	else if (command == "ConvertTranslatedText" && argc == 4 )
	{
		const string translatedTextDir = string(argv[2]);
		const string outDirectory      = string(argv[3]) + string("\\");

		ConvertTranslatedText(translatedTextDir, outDirectory);
	}
	else if( command == "PatchGameKNJ" && argc == 5 )
	{
		const string searchDirectory = string(argv[2]);
		const string newKNJ          = string(argv[3]);
		const string outDir          = string(argv[4]) + string("\\");

		PatchKNJ(searchDirectory, newKNJ, outDir);
	}
	else if( command == "PatchPalettes" && argc == 6 )
	{
		const string searchDirectory = string(argv[2]);
		const string origPalette     = string(argv[3]);
		const string newPalette      = string(argv[4]);
		const string outDir          = string(argv[5]) + string("\\");

		PatchPalettes(searchDirectory, origPalette, newPalette, outDir);
	}
	else if( command == "InsertText" && argc == 5 )
	{
		const string searchDirectory = string(argv[2]);
		const string translatedText  = string(argv[3]);
		const string outDir          = string(argv[4]) + string("\\");

		InsertText(searchDirectory, translatedText, outDir);
	}
	else if( command == "FindDuplicateText"  && argc == 4 )
	{
		const string searchDirectory = string(argv[2]);
		const string outputFile      = string(argv[3]);

		FindDuplicateText(searchDirectory, outputFile);
	}
	else
	{
		PrintHelp();
	}

	return 1;
}
