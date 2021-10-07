#pragma once

class DragonForceTextInserter
{
public:
	bool PatchTextInFile(const string& inCsvFilePath, const string& inPatchedFilePath, const uint32 inOffset)
	{
		if( !mCsvReader.InitializeFile(inCsvFilePath) )
		{
			return false;
		}
		
		if( !mTextFinder.FindText(inPatchedFilePath, inOffset) )
		{
			return false;
		}
	
		if( !mFileData.InitializeFileData(FileNameContainer(inPatchedFilePath.c_str())) )
		{
			return false;
		}

		mFileToPatch.OpenFile(inPatchedFilePath);

		for( const DragonForceCSVReader::LineEntry& csvEntry : mCsvReader.mLines )
		{	
			if( csvEntry.bShouldPatch && mTextFinder.DoesDataExistForLine(csvEntry.japanese) )
			{
				const string lineToInsert = csvEntry.english.empty() ? csvEntry.google : csvEntry.english;

				InsertLine(csvEntry, lineToInsert);
			}
		}

		//Copy data over
		mFileToPatch.WriteData(0, mFileData.GetData(), mFileData.GetDataSize());

		return true;
	}

private:
	void InsertLineOld(const DragonForceCSVReader::LineEntry& inCsvEntry, const string& inLineToInsert)
	{
		const vector<const DragonForceTextFinder::LineData*>& japaneseLineData = mTextFinder.GetLineDataFromString(inCsvEntry.japanese);
		
		const char zero = 0;
		for( const DragonForceTextFinder::LineData* pLineData : japaneseLineData )
		{
			const uint32 numAvailableBytes = pLineData->NumBytes + pLineData->NumZerosAfterLine;
			if( numAvailableBytes < inLineToInsert.length() + 1 )
			{
				printf("Warning:%s [%s] as is too long.  Expected %i, got %zi\n", inLineToInsert.c_str(), pLineData->Line.c_str(), numAvailableBytes, inLineToInsert.length() + 1);
			
				mFileToPatch.WriteData(pLineData->Offset, inLineToInsert.c_str(), numAvailableBytes - 1);

				//Null terminate entry
				mFileToPatch.WriteData(pLineData->Offset + inLineToInsert.length(), &zero, 1);
				continue;
			}

			mFileToPatch.WriteData(pLineData->Offset, inLineToInsert.c_str(), (unsigned long)inLineToInsert.length());

			//Null terminate entry
			mFileToPatch.WriteData(pLineData->Offset + inLineToInsert.length(), &zero, 1);
		}
	}

	void InsertLine(const DragonForceCSVReader::LineEntry& inCsvEntry, const string& inLineToInsert)
	{
		const vector<const DragonForceTextFinder::LineData*>& japaneseLineData = mTextFinder.GetLineDataFromString(inCsvEntry.japanese);
		
		const char zero = 0;
		char* pFileData = const_cast<char*>(mFileData.GetData());

		for( const DragonForceTextFinder::LineData* pLineData : japaneseLineData )
		{	
			//See if we can move this back to get more space
			uint32 newOffset = pLineData->Offset;
			if( pLineData->Offset >= 0x7aa20 && pLineData->Offset <= 0x7b480)//pLineData->Offset != 0x70e10 && pLineData->Offset != 0x7a728 )
			{
				while( pFileData[newOffset - 2] == 0 )
				{
			//		--newOffset;
				}
			}

			const uint32 numExtraBytes = pLineData->Offset - newOffset;
			const uint32 numOriginalBytes = pLineData->NumBytes + pLineData->NumZerosAfterLine;
			const uint32 numAvailableBytes = numOriginalBytes + numExtraBytes;
			
			//Null terminate entry
			memcpy_s(&pFileData[pLineData->Offset], numOriginalBytes, &mZeros, numOriginalBytes);

			//Make sure we have enough space
			if( numAvailableBytes < inLineToInsert.length() + 1 )
			{
				printf("Warning:%s [%s] as is too long.  Expected %i, got %zi\n", inLineToInsert.c_str(), pLineData->Line.c_str(), numAvailableBytes, inLineToInsert.length() + 1);
			
				memcpy_s(&pFileData[newOffset], numAvailableBytes - 1, inLineToInsert.c_str(), numAvailableBytes - 1);
				
				continue;
			}

			memcpy_s(&pFileData[newOffset], (unsigned long)inLineToInsert.length(), inLineToInsert.c_str(), (unsigned long)inLineToInsert.length());
		}
	}

private:
	DragonForceTextFinder mTextFinder;
	DragonForceCSVReader  mCsvReader;
	FileReadWriter        mFileToPatch;
	FileData              mFileData;
	static const char     mZeros[2048];
};
const char DragonForceTextInserter::mZeros[2048] = {0};
