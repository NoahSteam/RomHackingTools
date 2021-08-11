#pragma once

#pragma pack(push, 1)
struct MiniGameImageInfo
{
	uint16 offsetDiv8;
	uint16 unknown;
	uint8  widthDiv8;
	uint8  height;
	uint16 zero;

};
#pragma pack(pop)

struct MiniGameFiles
{
	const char* pCGFileName;
	const char* pDataFileName;
	uint16      imageInfoTableOffset;
};

void ExtractMiniGames(const string& inSakuraDirectory, const string& inOutputDirectory)
{
	CreateDirectoryHelper(inOutputDirectory);

	FileData nbgFile;
	if( nbgFile.InitializeFileData("NBGFILE1.ALL", (inSakuraDirectory + string("SAKURA1\NBGFILE1.ALL"))) )
	{
		return;
	}

	const int NumMiniGames = 9;
	MiniGameFiles miniGameFiles[NumMiniGames] =
	{
		"INST_AL", "0000DAIF.BIN", 0xcf64,
		"INST_IR", "0000IRIS.BIN", 0xcd10,
		"INST_KN", "0000KANN.BIN", 0xc628,
		"INST_KR", "0000KORA.BIN", 0xcbc8,
		"INST_MR", "0000MARI.BIN", 0x8fc8,
		"INST_OH", "0000ORIH.BIN", 0xcbcc,
		"INST_RN", "0000RENI.BIN", 0xa3f4,
		"INST_SK", "0000SAKU.BIN", 0x8c00,
		"INST_SM", "0000SUMI.BIN", 0xcd20,
	};

	for(int i = 0; i < NumMiniGames; ++i)
	{
		const string miniGameOutputDir = inOutputDirectory + miniGameFiles[i].pCGFileName;
		
		PaletteData& palette = i < 78 ? paletteData1 : paletteData2;
		BitmapWriter outBmp;
		outBmp.CreateBitmap(outFileName, lookupTable[i].widthDiv8, -lookupTable[i].height, 4, fileData.GetData() + lookupTable[i].offsetDiv8, (lookupTable[i].widthDiv8 * lookupTable[i].height) / 2, palette.GetData(), palette.GetSize(), bBmp);
	}
}
