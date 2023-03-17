#pragma once

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
			mpData = other.mpData;
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

class SakuraFontSheet
{
	vector<SakuraFontTile> mCharacterTiles;

public:
	bool CreateFontSheetFromData(const char* pInData, unsigned int inDataSize)
	{
		const int numBytesPerTile = 128;
		if (inDataSize % 128 != 0) //128
		{
			printf("CreateFontSheet: Invalid size for data.  Should be multiple of 128, is %u", inDataSize);
			return false;
		}

		const int numTilesInFile = inDataSize / numBytesPerTile;
		for (int currTile = 0; currTile < numTilesInFile; ++currTile)
		{
			mCharacterTiles.push_back(std::move(SakuraFontTile(&pInData[currTile * numBytesPerTile], numBytesPerTile)));
		}

		return true;
	}

	bool CreateFontSheet(const FileNameContainer& inFileNameInfo)
	{
		FileData fontFile;
		if (!fontFile.InitializeFileData(inFileNameInfo))
		{
			return false;
		}

		const int numBytesPerTile = 128;
		if (fontFile.GetDataSize() % 128 != 0)
		{
			printf("CreateFontSheet: Invalid size for file %s", inFileNameInfo.mFileName.c_str());
			return false;
		}

		const char* pFontSheetData = fontFile.GetData();
		const int numTilesInFile = fontFile.GetDataSize() / numBytesPerTile;
		for (int currTile = 0; currTile < numTilesInFile; ++currTile)
		{
			mCharacterTiles.push_back(std::move(SakuraFontTile(&pFontSheetData[currTile * numBytesPerTile], numBytesPerTile)));
		}

		return true;
	}

	const char* GetCharacterTile(const SakuraString::SakuraChar& sakuraChar) const
	{
		int tileIndex = sakuraChar.mIndex;// - 1;
		assert(tileIndex >= 0);
		//assert(tileIndex < (int)mCharacterTiles.size());
		if (tileIndex >= (int)mCharacterTiles.size())
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
