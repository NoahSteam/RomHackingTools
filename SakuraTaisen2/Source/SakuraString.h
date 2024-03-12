#pragma once

class SakuraTranslationTable
{
public:
	const unsigned short GetIndex(unsigned char inChar) const
	{
		if (inChar == '@')
		{
			return 'u' + 16;// + 1;//'…' + 1;
		}

		if (inChar == '\n')
		{
			return 0xfffe;
		}

		if (inChar == 0xA0)
		{
			return ' ';// + 1;
		}

		return inChar;// + 1;
	}
};
const SakuraTranslationTable GTranslationLookupTable;

struct SakuraString
{
	struct SakuraChar
	{
		SakuraChar() : mRow(0), mColumn(0) {}
		SakuraChar(unsigned char inRow, unsigned char inColumn) : mRow(inRow), mColumn(inColumn)
		{
			mIndex = (inRow << 8) + inColumn;
			assert(mIndex > 0);
		}

		SakuraChar(unsigned short inIndex)
		{
			mRow = (inIndex & 0xff00) >> 8;
			mColumn = (inIndex & 0x00ff);
			mIndex = inIndex;
		}

		bool IsNewLine() const
		{
			return mIndex == NewLine;
		}

		unsigned char    mRow;
		unsigned char    mColumn;
		unsigned short   mIndex;
		static const int NewLine = 0xFFFE;
	};

	vector<SakuraChar> mChars;
	static const int   MaxCharsPerLine = 15;
	unsigned short     mOffsetToStringData = 0;

	void Clear()
	{
		mChars.clear();
	}

	bool AddChar(unsigned short index, bool bPushFront = false)
	{
		if (bPushFront)
		{
			mChars.insert(mChars.begin(), std::move(SakuraChar(index)));
		}
		else
		{
			mChars.push_back(std::move(SakuraChar(index)));
		}

		return true;
	}

	bool AddChar(char row, char column, bool bPushFront)
	{
		//End of entry is 0xffff
		if (row == 0xff && column == 0xff)
		{
			return false;
		}

		if (bPushFront)
		{
			mChars.insert(mChars.begin(), std::move(SakuraChar(row, column)));
		}
		else
		{
			mChars.push_back(std::move(SakuraChar(row, column)));
		}

		return true;
	}

	void AddPlainString(const string& inString, bool bAddPadByte)
	{
		for (string::const_iterator iter = inString.begin(); iter != inString.end(); ++iter)
		{
			const unsigned short value = GTranslationLookupTable.GetIndex(*iter);
			mChars.push_back(std::move(SakuraChar(value)));
		}

		if (bAddPadByte && mChars.size() % 2 != 0)
		{
#if USE_SINGLE_BYTE_LOOKUPS
			mChars.push_back(std::move(SakuraChar(' ')));
#endif
		}

		//Not needed in SW2
	//	mChars.push_back(std::move(SakuraChar(0)));

		mOffsetToStringData = 0;
	}

	void AddString(const string& inString, unsigned short specialValue, unsigned short numCharsPrinted, bool bAddPadByte)
	{
		//SW2 doesn't have ID's with each dialog entry
		/*
		if (specialValue)
		{
			mChars.push_back(std::move(SakuraChar(specialValue)));
			mChars.push_back(std::move(SakuraChar(numCharsPrinted)));
		}
		else
		{
			mChars.push_back(std::move(SakuraChar(0)));
			mChars.push_back(std::move(SakuraChar(0)));
		}
		*/
		AddPlainString(inString, bAddPadByte);

		//SW2 doesn't have ID's with each dialog entry
		//mOffsetToStringData = 2;
	}

	int GetNumberOfLines() const
	{
		int numLines = 1;
		for (const SakuraChar& sakuraChar : mChars)
		{
			if (sakuraChar.mIndex == SakuraChar::NewLine)
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
		for (size_t i = 2; i < charCount; ++i)
		{
			if (mChars[i].mIndex && mChars[i].mIndex != SakuraChar::NewLine)
			{
				++returnValue;
			}
		}

		return returnValue;
	}

	int GetNumberOfPrintedCharacters() const
	{
		int numChars = 0;
		for (const SakuraChar& sakuraChar : mChars)
		{
			if (sakuraChar.mIndex != SakuraChar::NewLine)
			{
				++numChars;
			}
		}

		return numChars;
	}

	int GetMaxCharactersInLine() const
	{
		int maxCharacters = 0;
		int currCount = 0;
		for (const SakuraChar& sakuraChar : mChars)
		{
			if (sakuraChar.mIndex == SakuraChar::NewLine)
			{
				if (currCount > maxCharacters)
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

		if (currCount > maxCharacters)
		{
			maxCharacters = currCount;
		}

		return maxCharacters;
	}

	void GetDataArray(vector<unsigned short>& outData) const
	{
		outData.clear();

		for (const SakuraChar& sakuraChar : mChars)
		{
			//Change endianness so that it gets written in big endian order
			const short valueBE = ((sakuraChar.mIndex & 0xff00) >> 8) + ((sakuraChar.mIndex & 0x00ff) << 8);

			outData.push_back(valueBE);
		}
	}

	int GetNumBytes() const
	{
		return mChars.size() * 2; //2 bytes per entry
	}

	unsigned short GetSingleByteDataSize() const
	{
		if (mChars.size() > 3)
		{
			const unsigned short returnSize = (unsigned short)(mChars.size()) + 3;
			return returnSize;
		}

		return (unsigned short)mChars.size() * sizeof(short);
	}

	void GetSingleByteDataArray(vector<unsigned char>& outData, bool bIsSysFile = false) const
	{
		outData.clear();

		int count = 0;
		for (const SakuraChar& sakuraChar : mChars)
		{
			if (bIsSysFile && count < 2)
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
				if (value1 == 0 && value2 == 0)
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
		if (mChars.size() > 0 && mChars[0].mIndex != 0)
		{
			return true;
		}

		return false;
	}
};

class SakuraStringLookUpValue
{
public:
	vector<short> mValuesForEachChar;

	SakuraStringLookUpValue(const string& inString)
	{
		for (string::const_iterator iter = inString.begin(); iter != inString.end(); ++iter)
		{
			if (*iter == '\n')
			{
				continue;
			}

			mValuesForEachChar.push_back(GTranslationLookupTable.GetIndex(*iter));
		}
	}
};
