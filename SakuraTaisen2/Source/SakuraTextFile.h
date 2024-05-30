#pragma once
//SakuraTextFile is used to extract SK files (SK0101.bin, etc.)

#include "SakuraString.h"


/*
Dialog index : 0102 7FFE ####
Image index : 0103(or other code) 7FFE CAAA CBBB CDDD(idx from set)
AAA : Character index
BBB : Image set
CCC : Expression

Character Index 0 Note: Image Set is offset by 4.  So subtract 4 from BBB to get actual image set.
*/

const uint16 NumSetsPerCharacter[] =
{
	6, //Ogami
	10, //Sakura
	9, //Hairband girl
	6, //Maria
	7, //Kanna
	9, //Iris
	8, //Kohran
	9, //Orihime
	8, //Reni
	3, //Yoneda
	4, //Kayama
	3, //Lobby girl
	3, //Lobby girl 2
	3, //Lobby girl 3
	2, //Kaede (Image Char: 88)
	2, //(90, 91)
	1, //92
	2, //93
	1, //95
	1, //96
	1, //97
	1, //98
	1, //99
	3, //100
	1, //103
	1, //104
	1, //105
	2, //106
	1, //108
	1, //109
	1, //110
	1, //111
	1, //112
	1, //113
	1, //114
	1, //115
	1, //116
	1, //117
	1, //118
	1, //119
	1, //120
	1, //121
	1, //122
	1, //123
	1, //124
	1, //125
	1, //126
	1, //127
	1, //128
	1, //129
	1, //130
	1, //131
	1, //132
	1, //133
	1, //134
	1, //135
	1, //136
	2, //137
	1, //139
	1, //140
	1, //141
	1, //142
	1, //143
	1, //144
	1, //145
	1, //146
	1, //147
	1, //148
	1, //149
	1, //150
	1, //151
	1, //152
	1, //153
	1, //154
	1, //155
	1, //156
	1, //157
	1, //158
	1, //159
	1, //160
	1, //161
	1, //162
	1, //163
	1, //164
	1, //165
	1, //166
	1, //167
	1, //168
	1, //169
	1, //170
	1, //171
	1, //172
	1, //173
	1, //174
	1, //175
};

struct SakuraTextFile
{
	struct SakuraHeaderForTextEntries
	{
		char* pData;
		unsigned int dataSize;

		SakuraHeaderForTextEntries() : pData(nullptr), dataSize(0) {}

		SakuraHeaderForTextEntries(const char* pInData, unsigned int inSize)
		{
			dataSize = inSize;
			pData = new char[dataSize];

			memcpy_s(pData, dataSize, pInData, inSize);
		}

		SakuraHeaderForTextEntries(SakuraHeaderForTextEntries&& rhs) : pData(rhs.pData), dataSize(rhs.dataSize)
		{
			rhs.pData = nullptr;
		}

		SakuraHeaderForTextEntries& operator=(SakuraHeaderForTextEntries&& rhs)
		{
			if (this != &rhs)
			{
				delete[] pData;
				pData = rhs.pData;
				dataSize = rhs.dataSize;
				rhs.pData = nullptr;
			}

			return *this;
		}

		~SakuraHeaderForTextEntries()
		{
			delete[] pData;
			pData = nullptr;
		}
	};

	struct SakuraStringInfo
	{
		unsigned int   mFullValue;
		unsigned short mNumCharactersPrecedingThisString;

		SakuraStringInfo(unsigned int inValue) : mNumCharactersPrecedingThisString(inValue)
		{
			mFullValue = inValue;
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

				for (unsigned char value : mTimingBytes)
				{
					if (value != CharTimingStartID)
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
			for (const SakuraCharTimingData& lineData : mTimingForLine)
			{
				count += lineData.mTimingBytes.size();
			}

			return count;
		}

		size_t GetNumBytesInLine_NoKeyValues() const
		{
			size_t count = 0;
			for (const SakuraCharTimingData& lineData : mTimingForLine)
			{
				count += lineData.GetNumBytes_NoKeyValue();
			}

			return count;
		}

		size_t GetNumPrefixBytes() const
		{
			size_t numPrefixBytes = 0;

			if (mTimingForLine.size() && mTimingForLine[0].mTimingBytes.size())
			{
				if (mTimingForLine[0].mTimingBytes[0] != CharTimingStartID)
				{
					while (numPrefixBytes < mTimingForLine[0].mTimingBytes.size() && mTimingForLine[0].mTimingBytes[numPrefixBytes] != CharTimingStartID)
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
			char* pByteStream = new char[numBytes];
			size_t offset = 0;
			for (const SakuraCharTimingData& lineData : mTimingForLine)
			{
				if (offset >= numBytes)
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

	struct SakuraHeader
	{
		unsigned int OffsetToSequenceData; //1
		unsigned int OffsetToUnknown2;     //2
		unsigned int OffsetToTextHeader;   //3
		unsigned int OffsetToText;         //4
		unsigned int OffsetToFontSheet;    //5
		unsigned int FontSheetDataSize;    //6

		void InitializeFileHeader(const char* pInBuffer, const size_t InBufferSize)
		{
			memcpy_s(this, sizeof(SakuraHeader), pInBuffer, sizeof(SakuraHeader));

			//Convert to big endian
			SwapByteOrder();
		}

		void SwapByteOrder()
		{
			SwapByteOrderInPlace((char*)(&OffsetToSequenceData), sizeof(OffsetToSequenceData));
			SwapByteOrderInPlace((char*)(&OffsetToTextHeader),   sizeof(OffsetToTextHeader));
			SwapByteOrderInPlace((char*)(&OffsetToText),         sizeof(OffsetToText));
			SwapByteOrderInPlace((char*)(&OffsetToFontSheet),    sizeof(OffsetToFontSheet));
			SwapByteOrderInPlace((char*)(&FontSheetDataSize),    sizeof(FontSheetDataSize));
		}
	};

	struct FontSheetData
	{
		char* mpData{ nullptr };
		unsigned int mDataSize{ 0 };

		~FontSheetData()
		{
			delete[] mpData;
			mpData = nullptr;

			mDataSize = 0;
		}
	};

	struct SequenceEntry
	{
		uint16 mTextIndex{ 0xffff };
		uint16 mLipSyncId{0};
		uint16 mImageId_CharIndex{ 0 };
		uint16 mImageId_SetIndex{ 0 };
		uint16 mImageId_ExpressionIndex{ 0 };
		uint32 mAddress{0};
		bool   mbIsLips{ false };

		bool operator==(const SequenceEntry& rhs) const 
		{
			return 
			mTextIndex == rhs.mTextIndex && 
			mImageId_CharIndex == rhs.mImageId_CharIndex && 
			mImageId_SetIndex == rhs.mImageId_SetIndex && 
			mImageId_ExpressionIndex == rhs.mImageId_ExpressionIndex &&
			mbIsLips == rhs.mbIsLips;
		}
	};

	FileNameContainer          mFileNameInfo;
	SakuraHeader               mFileHeader;
	vector<SakuraString>       mLines;
	SakuraHeaderForTextEntries mHeaderForTextEntries;
	FontSheetData              mFontSheetData;
	vector<SakuraHeaderForTextEntries> mDataSegments;
	vector<SakuraStringInfo>  mStringInfoArray;
	vector<SakuraTimingData>  mLineTimingData;
	vector<SequenceEntry>     mSequenceEntries;
	unordered_map<uint16, SequenceEntry> mTextIdToSequenceEntryMap;
	map<uint16, SequenceEntry> mLipSyncIdToSequenceEntryMap;
	SW2SyncFile               mSyncFileData;
	char*                     mpHeaderToText{0};
	int                       mHeaderToTextSize{0};

private:
	unsigned long       mFileSize;
	FILE* mpFile;
	char* mpBuffer;

public:
	SakuraTextFile(const FileNameContainer& fileName) : mFileNameInfo(fileName), mFileSize(0), mpFile(nullptr), mpBuffer(nullptr) {}

	SakuraTextFile(SakuraTextFile&& rhs) : mFileNameInfo(std::move(rhs.mFileNameInfo)), mFileHeader(rhs.mFileHeader), mLines(std::move(rhs.mLines)), mHeaderForTextEntries(std::move(rhs.mHeaderForTextEntries)),
		mDataSegments(std::move(rhs.mDataSegments)), mStringInfoArray(std::move(rhs.mStringInfoArray)), mpHeaderToText(std::move(rhs.mpHeaderToText)),
		mHeaderToTextSize(rhs.mHeaderToTextSize), mFileSize(rhs.mFileSize),	mpFile(rhs.mpFile), mpBuffer(std::move(rhs.mpBuffer)), 
		mLineTimingData(std::move(rhs.mLineTimingData)), mFontSheetData(std::move(rhs.mFontSheetData)), mSequenceEntries(rhs.mSequenceEntries), 
		mTextIdToSequenceEntryMap(rhs.mTextIdToSequenceEntryMap), mLipSyncIdToSequenceEntryMap(rhs.mLipSyncIdToSequenceEntryMap), mSyncFileData(rhs.mSyncFileData)
	{
		rhs.mpBuffer              = nullptr;
		rhs.mpFile                = nullptr;
		rhs.mFontSheetData.mpData = nullptr;
		rhs.mpHeaderToText        = nullptr;
	}

	SakuraTextFile& operator=(SakuraTextFile&& rhs)
	{
		mFileNameInfo         = std::move(rhs.mFileNameInfo);
		mLines                = std::move(rhs.mLines);
		mFileHeader           = rhs.mFileHeader;
		mDataSegments         = std::move(rhs.mDataSegments);
		mStringInfoArray      = std::move(rhs.mStringInfoArray);
		mHeaderForTextEntries = std::move(rhs.mHeaderForTextEntries);
		mLineTimingData       = std::move(rhs.mLineTimingData);
		mFileSize             = rhs.mFileSize;
		mpFile                = rhs.mpFile;
		mpBuffer              = std::move(rhs.mpBuffer);
		mFontSheetData        = std::move(rhs.mFontSheetData);
		mSequenceEntries      = std::move(rhs.mSequenceEntries);
		mTextIdToSequenceEntryMap = std::move(rhs.mTextIdToSequenceEntryMap);
		mLipSyncIdToSequenceEntryMap = std::move(rhs.mLipSyncIdToSequenceEntryMap);
		mpHeaderToText        = std::move(rhs.mpHeaderToText);
		mHeaderToTextSize     = rhs.mHeaderToTextSize;
		mSyncFileData         = rhs.mSyncFileData;

		rhs.mpFile = nullptr;
		rhs.mpBuffer = nullptr;
		rhs.mpHeaderToText = nullptr;
	}

	~SakuraTextFile()
	{
		for (size_t i = 0; i < mDataSegments.size(); ++i)
		{
			delete[] mDataSegments[i].pData;

			mDataSegments[i].pData = nullptr;
		}
		mDataSegments.clear();

		delete[] mpHeaderToText;
		mpHeaderToText = nullptr;

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
		if (numBytesRead != mFileSize)
		{
			printf("Unable to read all of file: %s.\n", mFileNameInfo.mFileName.c_str());

			Close();
			return false;
		}

		return true;
	}

	void ParseSyncData()
	{
		std::string syncFileName("SYNC");
		if (isdigit(*(char*)(mFileNameInfo.mNoExtension.c_str() + 3)))
		{
			syncFileName += mFileNameInfo.mNoExtension.at(4);
			syncFileName += mFileNameInfo.mNoExtension.at(3);

			syncFileName = mFileNameInfo.mPathOnly + syncFileName + ".BIN";

			mSyncFileData.InitializeSyncFile(syncFileName);
		}
	}
	void ReadInText()
	{
		ParseSyncData();
		ParseHeader();
		ParseStrings();
		//	ParseFooter();
		ReadInFontSheetData();
		ReadInSequenceData();

		//Misc data between the header and start of the text header
		mHeaderToTextSize = mFileHeader.OffsetToTextHeader - sizeof(mFileHeader);
		mpHeaderToText = new char[mHeaderToTextSize];
		memcpy_s(mpHeaderToText, mHeaderToTextSize, mpBuffer + sizeof(mFileHeader), mHeaderToTextSize);

		assert(mStringInfoArray.size() == mLines.size());
	}

	void Close()
	{
		if (mpFile)
		{
			fclose(mpFile);
		}

		delete[] mpBuffer;

		mpBuffer = nullptr;
		mpFile = nullptr;
	}

	unsigned long GetFileSize() const
	{
		return mFileSize;
	}

	int GetVoicedLineStartIndex() const
	{
		const size_t numLines = mLines.size();
		for (size_t i = 0; i < numLines; ++i)
		{
			if (mLines[i].IsVoicedLine())
			{
				return i;
			}
		}

		return -1;
	}

	string GetFaceImageId(int InLineIndex) const
	{
		const int NarratorIndex = 141;

		for(const SequenceEntry& sequenceEntry : mSequenceEntries)
		{
			if( (sequenceEntry.mTextIndex) == (InLineIndex) )
			{
				const int characterSetIndex = GetCharacterImageSetIndexFromCharIndex(sequenceEntry.mImageId_CharIndex);
				int faceSetIndex            = sequenceEntry.mImageId_SetIndex;
				if( sequenceEntry.mImageId_CharIndex == 0 || sequenceEntry.mImageId_CharIndex > 8 )
				{
					faceSetIndex -= 4;
				}
				if (faceSetIndex < 0 && (sequenceEntry.mImageId_ExpressionIndex) != 0)
				{
					faceSetIndex = 4;
				}

				const int portraitIndex = faceSetIndex < 0 ? NarratorIndex : characterSetIndex + faceSetIndex;
				
				const string imageId = string("Char") + std::to_string(portraitIndex) + string("_") + std::to_string(sequenceEntry.mImageId_ExpressionIndex);

				return imageId;
			}
		}

		return "";
	}

	string GetFaceImageValues(int InLineIndex) const
	{
		const int NarratorIndex = 141;

		char buffer[50];
		for (const SequenceEntry& sequenceEntry : mSequenceEntries)
		{
			if ((sequenceEntry.mTextIndex) == (InLineIndex))
			{
				snprintf(buffer, 50, "%04x %04x %04x", sequenceEntry.mImageId_CharIndex | 0xC000, sequenceEntry.mImageId_SetIndex | 0xC000, sequenceEntry.mImageId_ExpressionIndex | 0xC000);

				return string(buffer);
			}
		}

		return "";
	}

private:
	void ParseHeader()
	{
		//Read in file header
		mFileHeader.InitializeFileHeader(mpBuffer, GetFileSize());

		//Get pointer to the text header
		const char* mpTextHeader = mpBuffer + mFileHeader.OffsetToTextHeader;

		//Parse header
		/*
			Text Header - Header for lines of dialog
			4 Bytes - Size of text header / 4
			Then for each entry - 4 byte number of characters preceeding this entry
		*/
		{
			//Get size of the text entry header
			const unsigned int textHeaderSize = SwapByteOrder(*((unsigned int*)mpTextHeader)) * 4;
			mHeaderForTextEntries = std::move(SakuraHeaderForTextEntries(mpTextHeader, textHeaderSize));

			assert(textHeaderSize % 4 == 0);

			const unsigned int* pDialogEntryHeader = (unsigned int*)(mpTextHeader)+1;
			const int numBytesPerEntry = 4;
			const int numStrings = (textHeaderSize / numBytesPerEntry) - 1; //First entry is just the 4 byte value saying how big the table is
			for (int i = 0; i < numStrings; ++i)
			{
				const unsigned int offsetToText = SwapByteOrder(pDialogEntryHeader[i]);

				mStringInfoArray.push_back(std::move(SakuraStringInfo(offsetToText)));
			}
		}
	}

	void ParseFooter()
	{
		//Second two bytes are the offset that will take us to the footer
		const unsigned int offsetToTimingData = SwapByteOrder(*((unsigned short*)(mpBuffer + 2))) * 2;
		char* pTimingData = mpBuffer + offsetToTimingData;
		int timingDataSize = mFileSize - offsetToTimingData;
		int index = 0;
		bool bSearchingForStart = false;

		SakuraTimingData timingForLine;
		SakuraTimingData::SakuraCharTimingData timingForCharacter;

		const bool bIsMESFile = mFileNameInfo.mFileName.find("MES.BIN", 0, 6) != string::npos;
		const size_t startLineIndex = bIsMESFile ? 0 : (size_t)GetVoicedLineStartIndex();
		size_t currentLineIndex = startLineIndex;

		if ((int)currentLineIndex == -1 && timingDataSize)
		{
			printf("No voiced lines found in %s", mFileNameInfo.mFileName.c_str());
			return;
		}

		if ((int)currentLineIndex == -1)
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
			if (timingChar == 0 &&
				timingDataSize > 0 &&
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
				if (!bIsMESFile && numTimingChararactersForLine % 2 == 0)
				{
					index += 2;
					timingDataSize -= 2;
				}
				else
				{
					index += 1;
					timingDataSize -= 1;
				}

				bSearchingForStart = false;

				if (timingDataSize <= 0)
				{
					break;
				}

				if (bIsMESFile)
				{
					const size_t numLinesInFile = mLines.size();
					while (mLines[currentLineIndex].mChars[0].mIndex == 0 && currentLineIndex < numLinesInFile)
					{
						++currentLineIndex;
					}
				}
				continue;
			}
			else if (!bSearchingForStart && timingChar == CharTimingStartID && timingForCharacter.mTimingBytes.size()) //When timing for next character is hit
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
				if (timingForCharacter.mTimingBytes.size() == 0 && timingChar != CharTimingStartID)
				{
					bSearchingForStart = true;
				}
				else if (bSearchingForStart && timingChar == CharTimingStartID)
				{
					bSearchingForStart = false;
				}

				timingForCharacter.mTimingBytes.push_back(timingChar);
			}

			--timingDataSize;
			++index;
		} while (timingDataSize > 0);

		if (startLineIndex != (size_t)-1 &&
			(mLineTimingData.size() != (mLines.size() - startLineIndex)))
		{
			//	printf("Line timing count[%u] doesn't match the number of lines[%u] in %s\n", mLineTimingData.size(), mLines.size(), mFileNameInfo.mFileName.c_str());
		}
	}

	void ParseStrings()
	{
		static const unsigned short EndOfLineCharacter = 0xffff;

		const unsigned int NumBytesPerCharacter = 2;

		const size_t numStrings = mStringInfoArray.size();
		const bool bIsMESFile = mFileNameInfo.mFileName.find("MES.BIN", 0, 6) != string::npos;
		const int TextDataSize = mFileHeader.OffsetToFontSheet - mFileHeader.OffsetToText; //Size of the text block can be calculated this way

		assert(TextDataSize >= 0);

		for (size_t i = 0; i < numStrings; ++i)
		{
			const unsigned int offsetToEntry = mStringInfoArray[i].mNumCharactersPrecedingThisString * NumBytesPerCharacter;
			const unsigned int offsetToString = mFileHeader.OffsetToText + offsetToEntry;
			assert(offsetToString < mFileSize);

			SakuraString newLineOfText;
			unsigned short* pWordBuffer = (unsigned short*)&mpBuffer[offsetToString];
			int currentIndex = 0;

			while (1)
			{
				const unsigned short currValue = SwapByteOrder(pWordBuffer[currentIndex++]);

				if (bIsMESFile)
				{
					if (currentIndex > 2 || (currentIndex == 1 && pWordBuffer[0] == 0))
					{
						newLineOfText.AddChar(currValue);
					}
				}
				else
				{
					newLineOfText.AddChar(currValue);
				}

				if (EndOfLineCharacter == currValue)
				{
					break;
				}

				if ((currentIndex + offsetToEntry) >= (unsigned int)TextDataSize)
				{
					break;
				}
			}

			newLineOfText.mOffsetToStringData = 0; //0 in SW2 because there is no ID to each dialog entry
			mLines.push_back(newLineOfText);

			if ((currentIndex + offsetToEntry) >= (unsigned int)TextDataSize)
			{
				break;
			}
		}
	}

	void ReadInFontSheetData()
	{
		mFontSheetData.mDataSize = mFileHeader.FontSheetDataSize;

		mFontSheetData.mpData = new char[mFontSheetData.mDataSize];
		memcpy_s(mFontSheetData.mpData, mFontSheetData.mDataSize, &mpBuffer[mFileHeader.OffsetToFontSheet], mFontSheetData.mDataSize);
	}

	void ReadInSequenceData()
	{
		const unsigned short* pSequenceData = (unsigned short*)(mpBuffer + mFileHeader.OffsetToSequenceData);
		const unsigned int SequenceDataNumEntries = (mFileHeader.OffsetToTextHeader - mFileHeader.OffsetToSequenceData) / sizeof(short);
		assert((int)SequenceDataNumEntries > 0);

		const uint16 breakTextId = 0xfffc;//0x48;

		const uint16 TextEntryId     = 0x0102;
		const uint16 SpecialEntryId  = 0x0103;
		const uint16 SpecialEntryId2 = 0xF04A;
		const uint16 DataEntryId     = 0x7FFE;
		const uint16 LipsId          = 0xC13F;
		const uint16 LipsId2         = 0xC180;

		size_t syncEntryIndex = 0;
		const size_t numSyncEntries = mSyncFileData.mSyncEntries.size();
		bool bFirstSyncEntryFound = false;

		unsigned int index = 1;

		std::map<uint16, int> mProcessedIdsToSequenceIndex;
		std::map<uint16, int> mProcessedIdsToSequenceIndex_ExtraEntries;
		
		auto AddLipSyncEntry = [this, &mProcessedIdsToSequenceIndex, &mProcessedIdsToSequenceIndex_ExtraEntries](const SequenceEntry& newEntry)
		{
			mLipSyncIdToSequenceEntryMap[newEntry.mLipSyncId] = newEntry;
			if (mProcessedIdsToSequenceIndex_ExtraEntries.find(newEntry.mTextIndex) != mProcessedIdsToSequenceIndex_ExtraEntries.end())
			{
				mSequenceEntries[mProcessedIdsToSequenceIndex_ExtraEntries[newEntry.mTextIndex]] = newEntry;
				mProcessedIdsToSequenceIndex_ExtraEntries.erase(mProcessedIdsToSequenceIndex_ExtraEntries.find(newEntry.mTextIndex));
			}
			else
			{
				bool bIsUnique = true;
				if (mTextIdToSequenceEntryMap.find(newEntry.mTextIndex) != mTextIdToSequenceEntryMap.end())
				{
					bIsUnique = false;
				}

				if (bIsUnique)
				{
					mSequenceEntries.push_back(newEntry);
					mTextIdToSequenceEntryMap[newEntry.mTextIndex] = newEntry;
				}
			}
			mProcessedIdsToSequenceIndex[newEntry.mTextIndex] = (int)(mSequenceEntries.size() - 1);
		};

		uint16 prevTextIndex = 1;

		//Go to the second to last entry as we require index + 1 to be valid in this loop
		while (index < (SequenceDataNumEntries - 1))
		{
			const unsigned short sequenceValue     = SwapByteOrder(pSequenceData[index]);
			const unsigned short prevValue         = SwapByteOrder(pSequenceData[index - 1]);
			const unsigned short nextValue         = SwapByteOrder(pSequenceData[index + 1]);
			const unsigned short nextNextValue     = SwapByteOrder(pSequenceData[index + 2]);
			const unsigned short nextNextNextValue = SwapByteOrder(pSequenceData[index + 3]);

			//if 7FFE detected
			if (sequenceValue == DataEntryId /*0x7FFE*/)
			{
				//if prev value was 0x0102, then the next value is the text id
				if (prevValue == 0x0102)
				{
					SequenceEntry newEntry;
					newEntry.mTextIndex               = 0xffff;//SwapByteOrder(pSequenceData[index + 1]) & 0x0fff;
					newEntry.mLipSyncId               = 0;
					newEntry.mImageId_CharIndex       = SwapByteOrder(pSequenceData[index - 4]) & 0x0fff;
					newEntry.mImageId_SetIndex        = SwapByteOrder(pSequenceData[index - 3]) & 0x0fff;
					newEntry.mImageId_ExpressionIndex = SwapByteOrder(pSequenceData[index - 2]) & 0x0fff;
					newEntry.mbIsLips                 = false;
					newEntry.mAddress                 = (index + 1) * 2 + mFileHeader.OffsetToSequenceData;
					
				//	prevTextIndex = newEntry.mTextIndex;

					//Rare case
					if(nextNextValue == 0x0112 && nextNextNextValue == 0x7ffe)
					{
						newEntry.mTextIndex = SwapByteOrder(pSequenceData[index + 4]) & 0x0fff;
						newEntry.mAddress = (index + 4) * 2 + mFileHeader.OffsetToSequenceData;

						index = index + 5;

						prevTextIndex = newEntry.mTextIndex;

						AddLipSyncEntry(newEntry);
						continue;
					}

					if (newEntry.mTextIndex == breakTextId)
					{
						newEntry.mTextIndex = breakTextId;
					}

					bool b103Found = false;

					//**Find lipsync id**
					unsigned int searchIndex = index + 1;
					while(searchIndex < mFileHeader.OffsetToTextHeader)
					{
						//Find next sequence
						uint16 searchValue     = SwapByteOrder(pSequenceData[searchIndex]);
						uint16 nextSearchValue = SwapByteOrder(pSequenceData[searchIndex+1]);

						if( !(searchValue == 0x0102 && (nextSearchValue == 0x7FFE || nextSearchValue == 0xF04A)))
						{
							//Should be 0103 and then behind that we can find the lip sync id
							if (searchValue == 0x0103 && nextSearchValue == 0x7FFE)
							{
								unsigned int index103 = searchIndex;
								const unsigned int endSearch = index;
								bool bSyncIdFound = false;

								while (--index103 != endSearch)
								{
									searchValue = SwapByteOrder(pSequenceData[index103]);
									const uint16 searchValueClean = (searchValue & 0xf000) == 0xc000 ? searchValue & 0x0fff : 0;

									//Sync entry
									if (!bSyncIdFound && (searchValue & 0xf000) == 0xc000)
									{
										newEntry.mLipSyncId = searchValue & 0x0fff;
										newEntry.mbIsLips   = false;
										bSyncIdFound        = true;

										//Find first sync entry from sync file
										if(!bFirstSyncEntryFound && mSyncFileData.mSyncIdToEntry.find(newEntry.mLipSyncId) != mSyncFileData.mSyncIdToEntry.end())
										{
											syncEntryIndex = mSyncFileData.mSyncIdToEntryIndex[newEntry.mLipSyncId];
											bFirstSyncEntryFound = true;
										}

										if(bFirstSyncEntryFound)
										{
											//Sanity check to see if this is a valid entry
											const bool bSyncEntriesExist = syncEntryIndex < numSyncEntries;
											const uint16 expectedSyncId = bSyncEntriesExist ? mSyncFileData.mSyncEntries[syncEntryIndex].syncEntry.syncID : 0;
											if (!bSyncEntriesExist || newEntry.mLipSyncId != expectedSyncId)
											{
												newEntry.mLipSyncId = 0;
											}
											else
											{
												++syncEntryIndex;

												if(syncEntryIndex < numSyncEntries && (mSyncFileData.mSyncEntries[syncEntryIndex].syncEntry.syncID & 0xf000) > 0x1000)
												{
													++syncEntryIndex;
												}
											}
										}
										else
										{
											newEntry.mLipSyncId = 0;
										}
									}
									else if(bSyncIdFound && (searchValueClean == prevTextIndex + 1 || (!b103Found && searchValueClean == prevTextIndex)))
									{
										newEntry.mTextIndex = searchValueClean;
										newEntry.mAddress   = index103 * 2 + mFileHeader.OffsetToSequenceData;
										
										const bool bShouldBreak = searchValueClean != prevTextIndex;
										prevTextIndex = newEntry.mTextIndex;

										b103Found = true;

										AddLipSyncEntry(newEntry);

										if (newEntry.mTextIndex == breakTextId)
										{
											newEntry.mTextIndex = breakTextId;
										}

										if(bShouldBreak)
										{
											break;
										}
									}
								}

								searchIndex += 2;
								continue;
							}

							//Lips
							else if(searchValue == 0x0001 && nextSearchValue == 0x7ffe)
							{
								//Find start of lips sequence
								const uint32 endIndex = searchIndex;
								uint32 lipsStartIndex = searchIndex;
								uint16 lipsValue = 0;
								do 
								{
									--lipsStartIndex;
									lipsValue = SwapByteOrder(pSequenceData[lipsStartIndex]);
									
								} while (lipsValue != 0x7ffe);

								
								//Find all lips ids
								uint32 lipsIndex = lipsStartIndex + 2; //Skip past junk value
								while(lipsIndex != endIndex)
								{
									lipsValue = SwapByteOrder(pSequenceData[lipsIndex]);
									if(((lipsValue & 0xf000) == 0xc000) && (lipsValue&0x0fff) == prevTextIndex + 1)
									{
										newEntry.mTextIndex = lipsValue & 0x0fff;
										newEntry.mbIsLips = true;
										newEntry.mLipSyncId = 0;
										newEntry.mAddress = lipsIndex * 2 + mFileHeader.OffsetToSequenceData;

										prevTextIndex = newEntry.mTextIndex;

										AddLipSyncEntry(newEntry);
									}
									++lipsIndex;
								}

								searchIndex += 2;
								continue;
							}

							++searchIndex;
							continue;
						}
						index = searchIndex - 1;
						break;
					}
				}//if (prevValue == 0x0102)
				index += 1;
			}
			else
			{
				++index;
			}
		}
	}

	int GetCharacterImageSetIndexFromCharIndex(uint16 InIndex) const
	{	
		uint16 numPrecedingFaceEntries = 0;
		for(uint16 i = 0; i < InIndex; ++i)
		{
			numPrecedingFaceEntries += NumSetsPerCharacter[i];
		}

		return numPrecedingFaceEntries;
	}
};

void FindAllSakuraText(const vector<FileNameContainer>& inFiles, vector<SakuraTextFile>& outText)
{
	const std::string SkipFileName("SK0000");

	for (const FileNameContainer& fileName : inFiles)
	{
		if (fileName.mNoExtension == SkipFileName)
		{
			continue;
		}

		printf("Extracting: %s\n", fileName.mFileName.c_str());

		SakuraTextFile sakuraFile(fileName);
		if (!sakuraFile.OpenFile())
		{
			continue;
		}

		if (sakuraFile.GetFileSize() == 0)
		{
			continue;
		}

		sakuraFile.ReadInText();

		outText.push_back(std::move(sakuraFile));
	}
}

void ExtractText(const string& inSearchDirectory, const string& inPaletteFileName, const string& inOutputDirectory, bool bInOnlyFontSheet = false)
{
	CreateDirectoryHelper(inOutputDirectory);

	//Get the palette
	FileData paletteFile;
	if (!paletteFile.InitializeFileData(inPaletteFileName.c_str(), inPaletteFileName.c_str()))
	{
		return;
	}

	//Create 32bit palette data
	PaletteData paletteData;
	paletteData.CreateFrom15BitData(paletteFile.GetData(), paletteFile.GetDataSize());

	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inSearchDirectory, allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> scenarioFiles;
	GetAllFilesOfType(allFiles, "SK0", scenarioFiles);
	GetAllFilesOfType(allFiles, "SK1", scenarioFiles);
	GetAllFilesOfType(allFiles, "SKC", scenarioFiles);

	//Extract the text
	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(scenarioFiles, sakuraTextFiles);

	const string extension(".bmp");
	
	//Write out bitmaps for all of the lines found in the sakura text files
	const size_t numFiles = sakuraTextFiles.size();
	for (size_t i = 0; i < numFiles; ++i)
	{
		const SakuraTextFile& sakuraText = sakuraTextFiles[i];

		//Create font sheet
		SakuraFontSheet sakuraFontSheet;
		if (!sakuraFontSheet.CreateFontSheetFromData(sakuraText.mFontSheetData.mpData, sakuraText.mFontSheetData.mDataSize, 16, 16, true))
		{
			continue;
		}

		if (bInOnlyFontSheet)
		{
			const string bitmapFileName = inOutputDirectory + sakuraText.mFileNameInfo.mNoExtension + extension;
			ExtractImageFromData(sakuraText.mFontSheetData.mpData, sakuraText.mFontSheetData.mDataSize, 
			  					bitmapFileName, nullptr, 0, true, 16, 16,
			 					255, 256, 0, true, true, &paletteData);
			continue;
		}

		//Create output directory for this file
		string fileOutputDir = inOutputDirectory + sakuraText.mFileNameInfo.mNoExtension + string("\\");
		if (!CreateDirectoryHelper(fileOutputDir))
		{
			continue;
		}

		printf("Dumping dialog for: %s\n", sakuraText.mFileNameInfo.mNoExtension.c_str());

		//Dump out the dialog for each line
		int stringIndex = 0;
		const int tileDim = 16;
		for (size_t lineIndex = 0; lineIndex < sakuraText.mLines.size(); ++lineIndex)
		{
			const SakuraString& sakuraString = sakuraText.mLines[lineIndex];
			if (sakuraString.mChars.size() > 255)
			{
				continue;
			}

			const int numSakuraLines = sakuraString.GetNumberOfLines();

			BitmapSurface sakuraStringBmp;
			sakuraStringBmp.CreateSurface(SakuraString::MaxCharsPerLine * tileDim, tileDim * numSakuraLines, BitmapSurface::EBitsPerPixel::kBPP_4, paletteData.GetData(), paletteData.GetSize());

			int currRow = 0;
			int currCol = 0;
			for (size_t charIndex = sakuraString.mOffsetToStringData; charIndex < sakuraString.mChars.size(); ++charIndex)
			{
				const SakuraString::SakuraChar& sakuraChar = sakuraString.mChars[charIndex];

				if (sakuraChar.IsNewLine())
				{
					++currRow;
					currCol = 0;
					continue;
				}

				if (sakuraChar.mIndex == 0xFFFF || sakuraChar.mIndex > 255 * 4)
				{
					continue;
				}

				const char* pData = sakuraFontSheet.GetCharacterTile(sakuraChar);

				sakuraStringBmp.AddTile(pData, sakuraFontSheet.GetTileSizeInBytes(), currCol * 16, currRow * 16, tileDim, tileDim);

				++currCol;
			}

			const string bitmapName = fileOutputDir + std::to_string(stringIndex + 1) + extension;
			sakuraStringBmp.WriteToFile(bitmapName);

			++stringIndex;
		}
	}
}

void ExtractTextCode(const string& inSearchDirectory, const string& inOutputDirectory)
{
	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inSearchDirectory, allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> scenarioFiles;
	GetAllFilesOfType(allFiles, "SK0", scenarioFiles);
	GetAllFilesOfType(allFiles, "SK1", scenarioFiles);
	GetAllFilesOfType(allFiles, "SKC", scenarioFiles);

	//Extract the text
	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(scenarioFiles, sakuraTextFiles);

	CreateDirectoryHelper(inOutputDirectory);

	//Write out hex code for all of the lines found in the sakura text files
	const string txt(".txt");
	const size_t numFiles = sakuraTextFiles.size();
	for (size_t i = 0; i < numFiles; ++i)
	{
		const SakuraTextFile& sakuraText = sakuraTextFiles[i];
		const string outTextCodeFileName = inOutputDirectory + sakuraText.mFileNameInfo.mNoExtension + txt;
		const string outTextSequenceFileName = inOutputDirectory + sakuraText.mFileNameInfo.mNoExtension + "_Seq" + txt;

		FILE* pOutTextCodeFile = nullptr;
		errno_t errorValue = fopen_s(&pOutTextCodeFile, outTextCodeFileName.c_str(), "w");
		if (errorValue)
		{
			printf("Unable to open out file: %s.  Error code: %i \n", outTextCodeFileName.c_str(), errorValue);
			continue;
		}

		FILE* pOutTextSequenceFile = nullptr;
		errorValue = fopen_s(&pOutTextSequenceFile, outTextSequenceFileName.c_str(), "w");
		if (errorValue)
		{
			printf("Unable to open out file: %s.  Error code: %i \n", outTextSequenceFileName.c_str(), errorValue);
			continue;
		}

		printf("Dumping dialog for: %s\n", sakuraText.mFileNameInfo.mNoExtension.c_str());
		
		//Dump out the dialog for each line
		int stringIndex = 0;
		const int tileDim = 16;
		for (size_t lineIndex = 0; lineIndex < sakuraText.mLines.size(); ++lineIndex)
		{
			const SakuraString& sakuraString = sakuraText.mLines[lineIndex];
			if (sakuraString.mChars.size() > 255)
			{
				continue;
			}
			
			for (const SakuraString::SakuraChar& sakuraChar : sakuraString.mChars)
			{
				fprintf(pOutTextCodeFile, "%02x%02x ", sakuraChar.mRow, sakuraChar.mColumn);
			}

			fprintf(pOutTextCodeFile, "\n");
		}

		for(const SakuraTextFile::SequenceEntry& seqEntry : sakuraText.mSequenceEntries)
		{
			fprintf(pOutTextSequenceFile, "%02x [%04x]\n", seqEntry.mTextIndex, seqEntry.mLipSyncId);
		}

		fclose(pOutTextCodeFile);
		fclose(pOutTextSequenceFile);
	}
}

bool ExtractFontSheets(const string& inSearchDirectory, const string& inPaletteFileName, const string& inOutputDirectory)
{
	ExtractText(inSearchDirectory, inPaletteFileName, inOutputDirectory, true);

	return true;
}

void ValidateSyncData(const string& inSakura1Directory, const string& inOutputDirectory)
{
	CreateDirectoryHelper(inOutputDirectory);

	//Find all files within the requested directory
	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inSakura1Directory, allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> scenarioFiles;
	GetAllFilesOfType(allFiles, "SK0", scenarioFiles);
	GetAllFilesOfType(allFiles, "SK1", scenarioFiles);
	GetAllFilesOfType(allFiles, "SKC", scenarioFiles);

	//Extract the text
	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(scenarioFiles, sakuraTextFiles);

	//Extract sync data
	vector<FileNameContainer> syncFileNames;
	vector<SW2SyncFile> syncFiles;
	GetAllFilesOfType(allFiles, "SYNC", syncFileNames);
	FindAllSyncData(syncFileNames, syncFiles);

	struct SyncDataInfo
	{
		const char* pFileName;
		uint16 syncId;
	};

	//Make sure all sync ids from the sakuraTextFiles exist within the syncFiles and vice-versa

	//Gather all sync data
	std::unordered_map<std::string, std::unordered_set<uint16>> syncIds;
	for(const SW2SyncFile& syncFile : syncFiles)
	{
		for(const SyncData& syncData : syncFile.mSyncEntries)
		{
			syncIds[syncFile.mFileName.mNoExtension].insert(syncData.syncEntry.syncID);
		}
	}

	typedef std::unordered_map<uint16, SyncDataInfo> SyncIDToSyncData;
	typedef std::unordered_map<std::string, SyncIDToSyncData> SyncFileToSyncData;
	
	SyncFileToSyncData syncDataForSyncFiles;

	//TODO: Once you find 0102, keep going till you find another 0102. In between if a 01013 is found, then textId and lipId preceeds it 
	
	//Find sync files for each SK file
	for(const SakuraTextFile& sakuraFile : sakuraTextFiles)
	{
		std::string syncFileName("SYNC");
		if(isdigit( *(char*)(sakuraFile.mFileNameInfo.mNoExtension.c_str() + 3)))
		{
			syncFileName += sakuraFile.mFileNameInfo.mNoExtension.at(4);
			syncFileName += sakuraFile.mFileNameInfo.mNoExtension.at(3);
		}
		else
		{
			continue;
		}

		int expectedTextId = 1;
		if(sakuraFile.mSequenceEntries.size())
		{
			for(const SakuraTextFile::SequenceEntry& seqEntry : sakuraFile.mSequenceEntries)
			{
				if(seqEntry.mTextIndex != expectedTextId)
				{
					expectedTextId = seqEntry.mTextIndex;
				}
				++expectedTextId;

				if(seqEntry.mLipSyncId)
				{	
					if(syncIds.find(syncFileName) != syncIds.end())
					{
						const auto& syncIdSet = syncIds[syncFileName];
						if(syncIdSet.find(seqEntry.mLipSyncId) == syncIdSet.end())
						{
							printf("%s: 0x%02x\n", syncFileName.c_str(), seqEntry.mLipSyncId);
						}
					}

					SyncDataInfo info;
					info.pFileName = sakuraFile.mFileNameInfo.mNoExtension.c_str();
					info.syncId = seqEntry.mLipSyncId;
					syncDataForSyncFiles[syncFileName][seqEntry.mLipSyncId] = info;
				}
			}
		}
		else
		{
			printf("No sync file for %s\n", sakuraFile.mFileNameInfo.mFileName.c_str());
		}
	}


	for (const SakuraTextFile& sakuraFile : sakuraTextFiles)
	{
		printf("%s: %i, TextLines: %i\n", sakuraFile.mFileNameInfo.mNoExtension.c_str(), (int)sakuraFile.mSequenceEntries.size(), (int)sakuraFile.mLines.size());

		const string outFileName = inOutputDirectory + sakuraFile.mFileNameInfo.mNoExtension + ".txt";
		TextFileWriter outFile;
		outFile.OpenFileForWrite(outFileName);

		for(const SakuraTextFile::SequenceEntry& seqEntry : sakuraFile.mSequenceEntries)
		{
			outFile.Printf("Entry: %02x Sync: %02x 0x%05x ", seqEntry.mTextIndex, seqEntry.mLipSyncId, seqEntry.mAddress);

			if(seqEntry.mTextIndex >= sakuraFile.mLines.size())
			{
				outFile.Printf("INVALID NUMBER OF LINES IN %s\n", sakuraFile.mFileNameInfo.mFileName.c_str());
			}
			else
			{
				const int numPrintedCharacters = sakuraFile.mLines[seqEntry.mTextIndex - 1].GetNumberOfActualCharacters();

				const uint32 lipSyncId = seqEntry.mLipSyncId;
				if(sakuraFile.mSyncFileData.mSyncIdToEntry.find(lipSyncId) != sakuraFile.mSyncFileData.mSyncIdToEntry.end())
				{
					const SyncData& syncData = sakuraFile.mSyncFileData.mSyncIdToEntry.at(lipSyncId);

					const int numTimingValues = syncData.GetNumTimingValues();
					if(abs(numTimingValues - numPrintedCharacters) > 1)
					{
						outFile.Printf("MISMATCH: ExpectedCharacters: %i, SK_Characters: %i Timing: ", numTimingValues, numPrintedCharacters);

						for(uint8 t : syncData.timingData)
						{
							outFile.Printf("%02x ", t);
						}

						outFile.Printf("\n");
					}
					else
					{
						outFile.Printf("ExpectedCharacters: %i, SK_Characters: %i\n", numTimingValues, numPrintedCharacters);
					}
				}
				else
				{
					outFile.Printf("\n");
				}
			}
		}
	}
}
