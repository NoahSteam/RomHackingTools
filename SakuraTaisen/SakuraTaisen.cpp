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
#include "..\Utils\Utils.h"

using std::vector;
using std::string;
using std::list;

struct SakuraString
{
	struct SakuraChar
	{
		SakuraChar() : mRow(0), mColumn(0){}
		SakuraChar(unsigned char inRow, unsigned char inColumn) : mRow(inRow), mColumn(inColumn){}

		unsigned char mRow;
		unsigned char mColumn;
	};

	vector<SakuraChar> mChars;

	void AddChar(char row, char column)
	{
		mChars.push_back( std::move(SakuraChar(row, column)) );
	}
};

struct SakuraTextFile
{
	FileNameContainer             mFileName;
	vector<SakuraString> mLines;

private:
	long  mFileSize;
	FILE* mpFile;	
	char* mpBuffer;

public:
	SakuraTextFile(const FileNameContainer& fileName) : mFileName(fileName), mFileSize(0), mpFile(nullptr), mpBuffer(nullptr){}

	~SakuraTextFile()
	{
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

	bool FindNextSakuraString(long &inOutLocation)
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

			++inOutLocation;
		}

		return false;
	}

	void ReadInText()
	{
		long currentLocation = 0;
		bool bNextStringFound = FindNextSakuraString(currentLocation);

		while( bNextStringFound && currentLocation < mFileSize )
		{
			long nextStringLocation = currentLocation;
			bNextStringFound        = FindNextSakuraString(nextStringLocation);
			const long endOfString  = bNextStringFound ? nextStringLocation - 6 : mFileSize;

			SakuraString newLineOfText;
			bool bWasValidString = false;
			while(currentLocation + 1 < endOfString )
			{
				newLineOfText.AddChar( mpBuffer[currentLocation], mpBuffer[currentLocation+1] );
				currentLocation += 2;
				bWasValidString = true;

				if(currentLocation > nextStringLocation)
				{
					break;
				}
			}

			currentLocation = nextStringLocation;

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
		mpFile  = nullptr;
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
		sakuraFile.Close();

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

bool ExtractFontSheetAsBitmap(const FileNameContainer& inFileNameContainer, const string& outFileName, const FileData& paletteData)
{
	FileData fontSheet;
	if( !fontSheet.InitializeFileData(inFileNameContainer) )
	{
		return false;
	}

	printf("Extracting: %s", inFileNameContainer.mFileName.c_str());

	const int tileDimX          = 16;
	const int tileDimY          = 16;
	const int tileBytes         = (tileDimX*tileDimY)/2; //4bits per pixel, so only half the amount of bytes as pixels
	const int tilesPerRow       = 256;
	const int bytesPerTile      = 128;
	const int bytesPerTileRow   = bytesPerTile*tilesPerRow;
	const int numRows           = (int)ceil( fontSheet.GetDataSize() / (float)bytesPerTileRow);
	const int numColumns        = numRows > 0 ? tilesPerRow : fontSheet.GetDataSize()/bytesPerTileRow;
	const int imageHeight       = -numRows*tileDimY;
	const int imageWidth        = numColumns*tileDimX;
	
	//Create 32bit palette from the 16 bit(5:5:5 bgr) palette in SakuraTaisen
	const int paletteSize              = paletteData.GetDataSize();
	const int numColorInPalette        = paletteSize/3;
	const int numBytesIn32BitPalette   = numColorInPalette*4;
	char* p32BitPalette                = new char[numBytesIn32BitPalette];
	const char* pPaletteData           = paletteData.GetData();
	const float full5BitValue          = (float)0x1f;
	for(int i = 0, origIdx = 0; i < numBytesIn32BitPalette; i += 4, origIdx += 2)
	{
		unsigned short color = ((pPaletteData[origIdx] << 8) + (unsigned char)pPaletteData[origIdx+1]);
		
		//Ugly conversion of 5bit values to 8bit.  Probably a better way to do this.
		//Masking the r,g,b components and then bringing the result into a [0,255] range.
		p32BitPalette[i]   = (char)floor( ( ((color & 0x001f)/full5BitValue) * 255.f) + 0.5f);
		p32BitPalette[i+1] = (char)floor( ( ( ((color & 0x03E0) >> 5)/full5BitValue) * 255.f) + 0.5f);
		p32BitPalette[i+2] = (char)floor( ( ( ((color & 0x7C00) >> 10)/full5BitValue) * 255.f) + 0.5f);
		p32BitPalette[i+3] = 0;
	}

	//Allocate space for tiled data
	int numTiles                     = fontSheet.GetDataSize()/tileBytes;
	int currTileRow                  = 0;
	int currTileCol                  = 0;
	const int bytesInEachTilesWidth  = 8;
	const int numTiledBytes          = (numRows*bytesPerTileRow) + numColumns*bytesInEachTilesWidth;
	char* pOutTiledData              = new char[numTiledBytes];
	const int bytesPerHorizontalLine = bytesInEachTilesWidth*numColumns;

	memset(pOutTiledData, 0, numTiledBytes);

	//File in data
	for(int tileIndex = 0; tileIndex < numTiles; ++tileIndex)
	{
		const char* pTile = fontSheet.GetData() + tileIndex*tileBytes;
		char* pOutTile    = pOutTiledData + currTileRow*bytesPerHorizontalLine*tileDimY + currTileCol*bytesInEachTilesWidth;
		int tilePixel = 0;
		for(int r = 0; r < tileDimY; ++r)
		{
			for(int c = 0; c < bytesInEachTilesWidth; ++c)
			{
				pOutTile[r*bytesPerHorizontalLine + c] = pTile[tilePixel++];
			}
		}

		++currTileCol;
		if( currTileCol >= numColumns )
		{
			currTileCol = 0;
			++currTileRow;
		}
	}

	BitmapWriter fontBitmap;
	fontBitmap.CreateBitmap(outFileName, imageWidth, imageHeight, 4, pOutTiledData, numTiledBytes, p32BitPalette, numBytesIn32BitPalette);

	delete[] p32BitPalette;
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

void PrintHelp()
{
	printf("usage: SakuraTaisen [command]\n");
	printf("Commands:\n");
	printf("ExtractRawText dataFileDirectory outDirectory\n");
	printf("ExtractFontSheets dataFileDirectory paletteFileName outDirectory");
	printf("ExtractText dataFileDirectory fontSheetDirectory outDirectory");
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
	else if( command == string("ExtractText" ))
	{

	}
	else
	{
		PrintHelp();
	}

	return 1;
}
