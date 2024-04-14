#pragma once

class SakuraFontTile
{
	char* mpData{nullptr};
	int mDataSize{0};

public:
	SakuraFontTile(const char* pInData, int dataSize)
	{
		mpData = new char[dataSize];
		memcpy(mpData, pInData, dataSize);

		mDataSize = dataSize;
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
		mDataSize = 0;
	}

	const char* GetFontTileData() const
	{
		return mpData;
	}
};

class SakuraFontSheet
{	
public:
	bool CreateFontSheetFromData(const char* pInData, unsigned int inDataSize, int inTileWidth, int inTileHeight, bool bInIs4Bit)
	{
		mBytesInTile = bInIs4Bit ? (inTileWidth*inTileHeight) / 2 : inTileWidth*inTileHeight;
		mTileWidth   = inTileWidth;
		mTileHeight  = inTileHeight;
		mPixelFormat = bInIs4Bit ? BitmapSurface::EBitsPerPixel::kBPP_4 : BitmapSurface::EBitsPerPixel::kBPP_8;

		if (inDataSize % mBytesInTile != 0) //128
		{
			printf("CreateFontSheet: Invalid size for data.  Should be multiple of 128, is %u", inDataSize);
			return false;
		}

		const int numTilesInFile = inDataSize / mBytesInTile;
		for (int currTile = 0; currTile < numTilesInFile; ++currTile)
		{
			mCharacterTiles.push_back(std::move(SakuraFontTile(&pInData[currTile * mBytesInTile], mBytesInTile)));
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

	const char* GetCharacterTile(int inTileIndex) const
	{
		assert(inTileIndex >= 0);
		//assert(tileIndex < (int)mCharacterTiles.size());
		if (inTileIndex >= (int)mCharacterTiles.size())
		{
			inTileIndex = 0;
		}

		return inTileIndex < (int)mCharacterTiles.size() ? mCharacterTiles[inTileIndex].GetFontTileData() : nullptr;
	}

	const char* GetCharacterTile(const SakuraString::SakuraChar& sakuraChar) const
	{
		return GetCharacterTile(sakuraChar.mIndex);
	}

	unsigned long GetTileSizeInBytes() const
	{
		return mBytesInTile;
	}

	int GetNumTiles() const {return mCharacterTiles.size();}

	int GetTileWidth() const {mTileWidth;}
	int GetTileHeight() const {mTileHeight;}
	BitmapSurface::EBitsPerPixel GetPixelFormat() const {return mPixelFormat;}

private:
	vector<SakuraFontTile> mCharacterTiles;
	unsigned long mBytesInTile{128};
	int mTileWidth{0};
	int mTileHeight{0};
	BitmapSurface::EBitsPerPixel mPixelFormat{BitmapSurface::EBitsPerPixel::kBPP_8};
};
