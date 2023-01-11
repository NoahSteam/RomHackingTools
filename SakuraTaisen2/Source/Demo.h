#pragma once

void ExtractDemoWPALL(const std::string& inDemoDirectory, const std::string& inOutputDirectory)
{
	CreateDirectoryHelper(inOutputDirectory);

	//Coming soon
	if(0)
	{
		BmpToSaturnConverter patchedImageData;
		unsigned int tileDim = 8;
		if (!patchedImageData.ConvertBmpToSakuraFormat(inDemoDirectory + "ComingSoon.bmp", false, 0, &tileDim, &tileDim) )
		{
			return;
		}
		patchedImageData.PackTiles();
		FileWriter outData;
		outData.OpenFileForWrite((inOutputDirectory + "ComingSoon.bin"));
		outData.WriteData(patchedImageData.mpPackedTiles, patchedImageData.mPackedTileSize, false);
	}

	//N_Load
	{
		const std::string nLoadPath = inDemoDirectory + "N_LOAD.ALL";
		FileData nLoadData;
		if( !nLoadData.InitializeFileData(nLoadPath) )
		{
			return;
		}

		const std::string wpallPath = inDemoDirectory + "WPALL.ALL";
		FileData wpallData;
		if( !wpallData.InitializeFileData(wpallPath) )
		{
			return;
		}

		struct WPAllInfo
		{
			bool bIsWpall;
			unsigned int colorOffset;
			unsigned int colorSize;
			unsigned int tileOffset;
			unsigned int paletteOffset;
		};

		const WPAllInfo entries[6] = 
		{
			{true, 0x40, 0xfc00, 0x10000, 0x11800},
			{true, 0x12040, 0x11480, 0x23800, 0x25000},
			{true, 0x25840, 0xf500, 0x35000, 0x36800},
			{false, 0x40, 0xa680, 0xa800, 0xc000},
			{false, 0xc840, 0xA840, 0x17800, 0x19000},
			{false, 0x19840, 0xaac0, 0x24800, 0x26000}
		};

		for( int i = 0; i < 6; ++i )
		{
			FileData* pFileData = entries[i].bIsWpall ? &wpallData : &nLoadData;

			SakuraFontSheet tileSheet;
			if( !tileSheet.CreateFontSheetFromData(pFileData->GetData() + entries[i].colorOffset, entries[i].colorSize) )
			{
				return;
			}

			//Create 32bit palette data
			PaletteData paletteData;
			paletteData.CreateFrom15BitData(pFileData->GetData() + entries[i].paletteOffset, 512);

			BitmapSurface sakuraStringBmp;
			sakuraStringBmp.CreateSurface(320, 224, BitmapSurface::EBitsPerPixel::kBPP_8, paletteData.GetData(), paletteData.GetSize());

			unsigned int tiles[1120];
			memcpy_s(tiles, sizeof(tiles), pFileData->GetData() + entries[i].tileOffset, sizeof(tiles));
			int x = 0;
			int y = 0;
			for( int t = 0; t < 1120; ++t )
			{
				const int tileValue = SwapByteOrder(tiles[t]);
				SakuraString::SakuraChar sakuraChar;
				sakuraChar.mIndex = (tileValue/2) - 1;

				const char* pTileData = tileSheet.GetCharacterTile(sakuraChar);
				sakuraStringBmp.AddTile(pTileData, 64, x, y, 8, 8);

				x += 8;
				if( x == 320 )
				{
					x = 0;
					y += 8;
				}
			}

			std::string outFile;
			if( entries[i].bIsWpall )
			{
				outFile = inOutputDirectory + "wpall_" + std::to_string(i) + ".png";
			}
			else
			{
				outFile = inOutputDirectory + "nload_" + std::to_string(i) + ".png";
			}
			
			sakuraStringBmp.WriteToFile(outFile, false);
		}
	}
}