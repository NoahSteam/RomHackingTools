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
	map<char, short> mTranslationTable;

public:
	SakuraTranslationTable()
	{
		mTranslationTable['A']  = 1;
		mTranslationTable['B']  = 2;
		mTranslationTable['C']  = 3;
		mTranslationTable['D']  = 4;
		mTranslationTable['E']  = 5;
		mTranslationTable['F']  = 6;
		mTranslationTable['G']  = 7;
		mTranslationTable['H']  = 8;
		mTranslationTable['I']  = 9;
		mTranslationTable['J']  = 10;
		mTranslationTable['K']  = 11;
		mTranslationTable['L']  = 12;
		mTranslationTable['M']  = 13;
		mTranslationTable['N']  = 14;
		mTranslationTable['O']  = 15;
		mTranslationTable['P']  = 16;
		mTranslationTable['Q']  = 17;
		mTranslationTable['R']  = 18;
		mTranslationTable['S']  = 19;
		mTranslationTable['T']  = 20;
		mTranslationTable['U']  = 21;
		mTranslationTable['V']  = 22;
		mTranslationTable['W']  = 23;
		mTranslationTable['X']  = 24;
		mTranslationTable['Y']  = 25;
		mTranslationTable['Z']  = 26;
		mTranslationTable['a']  = 27;
		mTranslationTable['b']  = 28;
		mTranslationTable['c']  = 29;
		mTranslationTable['d']  = 30;
		mTranslationTable['e']  = 31;
		mTranslationTable['f']  = 32;
		mTranslationTable['g']  = 33;
		mTranslationTable['h']  = 34;
		mTranslationTable['i']  = 35;
		mTranslationTable['j']  = 36;
		mTranslationTable['k']  = 37;
		mTranslationTable['l']  = 38;
		mTranslationTable['m']  = 39;
		mTranslationTable['n']  = 40;
		mTranslationTable['o']  = 41;
		mTranslationTable['p']  = 42;
		mTranslationTable['q']  = 43;
		mTranslationTable['r']  = 44;
		mTranslationTable['s']  = 45;
		mTranslationTable['t']  = 46;
		mTranslationTable['u']  = 47;
		mTranslationTable['v']  = 48;
		mTranslationTable['w']  = 49;
		mTranslationTable['x']  = 50;
		mTranslationTable['y']  = 51;
		mTranslationTable['z']  = 52;
		mTranslationTable['.']  = 53;
		mTranslationTable['@']  = 54;
		mTranslationTable['!']  = 55;
		mTranslationTable['?']  = 56;
		mTranslationTable[',']  = 57;
		mTranslationTable['\''] = 58;
		mTranslationTable[' ']  = 59;
		mTranslationTable['-']  = 59; //Todo, add this into the table
		mTranslationTable['\n'] = 0x0a0d;
	}

	const short GetIndex(char inChar) const
	{
		assert( mTranslationTable.find(inChar) != mTranslationTable.end() );
		
		return mTranslationTable.find(inChar)->second;
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
	static const int   MaxCharsPerLine = 15;

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
		for(string::const_iterator iter = inString.begin(); iter != inString.end(); ++iter)
		{
			const unsigned short value = GTranslationLookupTable.GetIndex(*iter);
			mChars.push_back( std::move(SakuraChar(value)) );
		}
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
		const int tileIndex = sakuraChar.mIndex - 1;
		assert(tileIndex >= 0);
		assert(tileIndex < (int)mCharacterTiles.size());

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

		SakuraDataSegment(char* pInData, size_t inSize) : pData(pInData), dataSize(inSize) {}

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

	FileNameContainer         mFileName;
	vector<SakuraString>      mLines;
	vector<SakuraDataSegment> mDataSegments;

private:
	unsigned long       mFileSize;
	FILE*               mpFile;
	char*               mpBuffer;

public:
	SakuraTextFile(const FileNameContainer& fileName) : mFileName(fileName), mFileSize(0), mpFile(nullptr), mpBuffer(nullptr){}

	SakuraTextFile(SakuraTextFile&& rhs) : mFileName(std::move(rhs.mFileName)), mLines(std::move(rhs.mLines)), mDataSegments(std::move(rhs.mDataSegments)), mFileSize(rhs.mFileSize), 
		                                   mpFile(rhs.mpFile), mpBuffer(std::move(rhs.mpBuffer))
	{
		rhs.mpBuffer = nullptr;
		rhs.mpFile   = nullptr;
	}

	SakuraTextFile& operator=(SakuraTextFile&& rhs)
	{
		mFileName     = std::move(rhs.mFileName);
		mLines        = std::move(rhs.mLines);
		mDataSegments = std::move(rhs.mDataSegments);
		mFileSize     = rhs.mFileSize;
		mpFile        = rhs.mpFile;
		mpBuffer      = std::move(rhs.mpBuffer);

		rhs.mpFile    = nullptr;
		rhs.mpBuffer  = nullptr;
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

	bool FindNextSakuraString(unsigned long &inOutLocation)
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

			/*
			if( mpBuffer[inOutLocation + 0] == 0 && 
				mpBuffer[inOutLocation + 1] == 0 && 
				mpBuffer[inOutLocation + 2] != 0 )
			{
				return false;
			}
			*/
			++inOutLocation;
		}

		return false;
	}

	void ReadInText()
	{
		unsigned long currentLocation   = 0;
		unsigned long dataStartLocation = 0;
		bool bNextStringFound = FindNextSakuraString(currentLocation);
		bool bLastStringFound = false;

		while( bNextStringFound && !bLastStringFound && currentLocation < mFileSize )
		{
			unsigned long nextStringLocation   = currentLocation;
			bNextStringFound                   = FindNextSakuraString(nextStringLocation);
			const unsigned long endOfString    = bNextStringFound ? nextStringLocation - 6 : mFileSize;
			const unsigned long stringStartLoc = currentLocation;

			SakuraString newLineOfText;
			bool bWasValidString  = false;
			while(currentLocation + 1 < endOfString )
			{
				if( !newLineOfText.AddChar(mpBuffer[currentLocation], mpBuffer[currentLocation + 1]) )
				{
					break;
				}
				currentLocation += 2;
				bWasValidString = true;

				if(currentLocation > nextStringLocation)
				{
					break;
				}
			}

			//Add data segment
			assert(stringStartLoc > dataStartLocation);
			const unsigned long dataSize = stringStartLoc - dataStartLocation;
			char* pDataSegment           = new char[dataSize];
			memcpy(pDataSegment, mpBuffer + dataStartLocation, dataSize);
			mDataSegments.push_back( std::move(SakuraDataSegment(pDataSegment, dataSize)) );

			dataStartLocation = currentLocation;
			currentLocation   = nextStringLocation;			

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
		mpFile   = nullptr;
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

	for(const SakuraTextFile& textFile : inSakuraTextFiles)
	{
		const string outFileName = outDirectory + textFile.mFileName.mNoExtension + txt;

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

bool ExtractFontSheetAsBitmap(const FileNameContainer& inFileNameContainer, const string& outFileName, const FileData& inPaletteFile)
{
	FileData fontSheet;
	if( !fontSheet.InitializeFileData(inFileNameContainer) )
	{
		return false;
	}

	printf("Extracting: %s\n", inFileNameContainer.mFileName.c_str());
	
	const int tileDimX          = 16;
	const int tileDimY          = 16;
	const int tileBytes         = (tileDimX*tileDimY)/2; //4bits per pixel, so only half the amount of bytes as pixels
	const int tilesPerRow       = 255;
	const int bytesPerTile      = 128;
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
	const int bytesInEachTilesWidth  = 8;
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

		//Create font sheet
		SakuraFontSheet sakuraFontSheet;
		sakuraFontSheet.CreateFontSheet(fontSheetName);

		//Dump out the dialog for each line
		int stringIndex   = 0;
		const int tileDim = 16;
		for(const SakuraString& sakuraString : sakuraText.mLines)
		{	
			if( sakuraString.mChars.size() > 255 )
			{
				continue;
			}

			const int numSakuraLines = sakuraString.GetNumberOfLines();			

			BitmapSurface sakuraStringBmp;
			sakuraStringBmp.CreateSurface( SakuraString::MaxCharsPerLine*tileDim, tileDim*numSakuraLines, BitmapSurface::EBitsPerPixel::kBPP_4, paletteData.GetData(), paletteData.GetSize());
	
			int charIndex = 0;
			int currRow = 0;
			for(const SakuraString::SakuraChar& sakuraChar : sakuraString.mChars)
			{
				if( sakuraChar.IsNewLine() )
				{
					++currRow;
					charIndex = 0;
					continue;
				}

				const char* pData = sakuraFontSheet.GetCharacterTile(sakuraChar);

				sakuraStringBmp.AddTile(pData, sakuraFontSheet.GetTileSizeInBytes(), charIndex*16, currRow*16, tileDim, tileDim);

				++charIndex;
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
	if( !tileExtractor.ExtractTiles(16, 16, origTranslatedBmp) )
	{
		return;
	}

	//Convert it to the SakuraTaisen format
	PaletteData sakuraPalette;
	sakuraPalette.CreateFrom32BitData(origTranslatedBmp.mBitmapData.mPaletteData.mpRGBA, origTranslatedBmp.mBitmapData.mPaletteData.mSizeInBytes);

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
		for(unsigned int i = 0; i < tile.mTileSize; i+=2)
		{
			const int pixel1 = (tile.mpTile[i] & 0xF0) >> 4; 
			const int pixel2 = (tile.mpTile[i+1] & 0x0F);
			
			const unsigned short color1 = *((short*)(sakuraPalette.GetData() + pixel1*2));
			const unsigned short color2 = *((short*)(sakuraPalette.GetData() + pixel2*2));

			if( color1 == 0xFF7F )
			{
		//		tile.mpTile[i] = 0;
			}
			if( color2 == 0xFF7F )
			{
			//	tile.mpTile[i+1] = 0;
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
			sakuraFileData.DoesThisFileContain(origData, offsets, true);

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
				vector<SakuraString> translatedLines;
				for(const TextFileData::TextLine& textLine : translatedFile.mLines)
				{
					SakuraString translatedString;
					for(const string& word : textLine.mWords)
					{
						translatedString.AddString(word);
					}

					translatedLines.push_back( std::move(translatedString) );
				}

				//Fill out everything else with "Untranslated"
				SakuraString translatedSakuraString;
				translatedSakuraString.AddString( string("Untranslated") );
				const size_t untranslatedCount = sakuraFile.mLines.size() - translatedFile.mLines.size();
				for(size_t i = 0; i < untranslatedCount; ++i)
				{
					translatedLines.push_back( translatedSakuraString );
				}

				//Output data
				const size_t numDataSegments    = sakuraFile.mDataSegments.size();
				const size_t numTranslatedLines = translatedLines.size();
				size_t dataIndex                = 0;
				size_t translationIndex         = 0;
				while(1)
				{
					if( dataIndex < numDataSegments )
					{						
						outFile.WriteData(sakuraFile.mDataSegments[dataIndex].pData, sakuraFile.mDataSegments[dataIndex].dataSize);
					}

					if( translationIndex < numTranslatedLines )
					{
						vector<unsigned short> translationData;
						translatedLines[dataIndex].GetDataArray(translationData);

						outFile.WriteData(translationData.data(), translationData.size()*sizeof(short));
					}

					++dataIndex;
					++translationIndex;

					if( dataIndex >= numDataSegments && translationIndex >= numTranslatedLines )
					{
						break;
					}
				}

				break;
			}
		}
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
	else
	{
		PrintHelp();
	}

	return 1;
}
