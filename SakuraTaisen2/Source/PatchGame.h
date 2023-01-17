#pragma once

static bool BringOverOriginalFiles(const string& inRootSakuraDirectory, const string& inPatchedDirectory)
{
	printf("BringOverOriginalFiles\n");

	vector<FileNameContainer> allFiles;
	FindAllFilesWithinDirectory(inRootSakuraDirectory, allFiles);

	//Find all the scenario text files
	vector<FileNameContainer> originalFiles;
	GetAllFilesOfType(allFiles, "SK0", originalFiles);
	GetAllFilesOfType(allFiles, "SK1", originalFiles);
	GetAllFilesOfType(allFiles, "SKC", originalFiles);
	GetAllFilesOfType(allFiles, "WPALL", originalFiles);
	GetAllFilesOfType(allFiles, "PBOOK_FL", originalFiles);
	GetAllFilesOfType(allFiles, "VDP1.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "GOVER", originalFiles);
	GetAllFilesOfType(allFiles, "CESALOGO.ALL", originalFiles);
	GetAllFilesOfType(allFiles, "TITLE.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "TTL2CGB.BIN", originalFiles);	
	GetAllFilesOfType(allFiles, "M_NAME", originalFiles);

	//Bring over scenario files
	const string outputDirectory = inPatchedDirectory + Seperators;
	for(const FileNameContainer& scenarioFile : originalFiles)
	{
		string subDirectory = "";
		const size_t lastIndex = scenarioFile.mPathOnly.find_last_of("\\");
		if(lastIndex != std::string::npos)
		{
			subDirectory = scenarioFile.mPathOnly.substr(lastIndex, scenarioFile.mPathOnly.size()) + Seperators;
		}

		const string outputFile = outputDirectory + subDirectory + scenarioFile.mFileName;
		if (!CopyFile(scenarioFile.mFullPath.c_str(), outputFile.c_str(), FALSE))\
		{
			printf("Unable to copy over %s\n", scenarioFile.mFullPath.c_str());
			return false;
		}
	}

#define CopySingleFile(InFileName)\
	{\
		const string sourceFile = inRootSakuraDirectory + Seperators + InFileName;\
		const string destFile = inPatchedDirectory + Seperators + InFileName;\
		if (!CopyFile(sourceFile.c_str(), destFile.c_str(), FALSE))\
		{\
			printf("Unable to copy over %s to %s\n", sourceFile.c_str(), destFile.c_str());\
			return false;\
		}\
	}
	
	CopySingleFile("SAKURADA");
	CopySingleFile("SAKURA.BIN");

	return true;
}

bool PatchTextDrawingCode(const string& inSourceGameDirectory, const string& inPatchedDirectory)
{
	uint32 writeAddress = 0;
	uint32 newLong = 0;
	uint16 newCommand = 0;
	uint8 newCommand1Byte = 0;
	#define WriteLong(address, cmd)    {writeAddress = address; newLong = cmd;         sakuraBin.WriteData(writeAddress, (char*)&newLong, sizeof(newLong), true);}
	#define WriteCommand(address, cmd) {writeAddress = address; newCommand = cmd;      sakuraBin.WriteData(writeAddress, (char*)&newCommand, sizeof(newCommand), true);}
	#define WriteByte(address, cmd)    {writeAddress = address; newCommand1Byte = cmd; sakuraBin.WriteData(writeAddress, (char*)&newCommand1Byte, sizeof(newCommand1Byte), false);}

	//SAKURA.BIN
	{
		const string sakuraBinFilePath = inPatchedDirectory + Seperators + string("SAKURA.BIN");
		FileReadWriter sakuraBin;
		if (!sakuraBin.OpenFile(sakuraBinFilePath))
		{
			return false;
		}

		//Fix spacing for text drawing
		{
			const uint32 StartAddresses[] = { 0xdde8, 0x4ca20 };
			for (const uint32 startAddress : StartAddresses)
			{
				//Vertical Spacing : starting from 0x06012de8 in memory
				WriteCommand(startAddress + 0, 0xea0c); //mov 0x0c, r10 (ea0c)
				WriteCommand(startAddress + 2, 0x29af); //mul r10, r9 (29af)
				WriteCommand(startAddress + 4, 0x0a1a); //sts MACL, R10 (0a1a)
				WriteCommand(startAddress + 6, 0x7a30); //add 0x30, r10 (7a30)

				//Horizontal Spacing : 06012e26 = > change this to 4100 (SHLL instead of SHLL2)
				WriteCommand(startAddress + 0x3e, 0x4100); //add 0x30, r10 (7a30)
			}
		}//Done fixing spacing

		//Fix max characters per line
		{
			const uint32 startAddressInFile = 0xd795;
			const uint32 modifyAddresses[] = {0xd795, 0xd84b, 0xd945, 0xd9e3, 0xdb63, 0xdc17, 0xddf1,0xde9b};//, 0x4b5f9, 0x4b641};

			//starting 06012794 in memory
			for (const uint32 addressToModify : modifyAddresses)
			{
				WriteByte(addressToModify, 0x1c);
			}

			//change 0x06012e44 0xE11A, change 0x06012E4A 0xE11C
			WriteCommand(0xde44, 0xe11a);
			WriteCommand(0xde4a, 0xe11c);

			//Change number of lines to 4 (03 but it's 0 based)
			WriteCommand(0xde52, 0xe103); //0x06012E53
			WriteByte(0xd7b5, 0x03); //0x060127b5
			WriteByte(0xdb83, 0x03); //0x06012b83 (untested)
			
			//Printing number of empty lines
			{
				//Need a bigger buffer for empty lines, so start writing from 060a5100 instead of 060a5180 
				WriteCommand(0xde7a, 0x5100);
				WriteCommand(0xdeb6, 0x5100);

				//06012ece
				WriteByte(0xdecf, 0x1b); //num chars per line
				WriteByte(0xdeed, 0x03); //num lines (0 based)

				//Another place for line counting 06012962
				WriteByte(0xd963, 0x03);
			}
		}

		//Fix regular lips spacing
		{
			WriteByte(0xdff3, 0x08); //horizontal spacing increment by 10, from 7110 to 7108 in memory at 06012ff2
			WriteByte(0xdfdf, 0x91); //vertical spacing offset by 0x92, from 7190 to 7192 in memory at 06012fde
		}

		//Fix special lips (where text changes midway) code to allow for more characters
		{
			const unsigned short offsetToLipsCharSpriteData = 80 + MaxCharsPerLine*32;
			WriteCommand(0xe704, offsetToLipsCharSpriteData + 0); //from 0x01d0 in memory at 06013704
			WriteCommand(0xe706, offsetToLipsCharSpriteData + 2); //from 0x01d2 in memory at 06013706
			WriteCommand(0xe482, offsetToLipsCharSpriteData + 4); //from 0x01d4 in memory at 06013482

			WriteCommand(0xe5ce, offsetToLipsCharSpriteData + 0); //from 0x01d0 in memory at 060135ce
			WriteCommand(0xe5d0, offsetToLipsCharSpriteData + 2); //from 0x01d2 in memory at 060135d0

			//Fix special lips horizontal spacing
			WriteCommand(0xdf56, 0x4200); //from SHLL2 to SHLL in memory at 06012F56
			WriteByte(0xe4f5, 0x08); //from add 0x10,r1 to add 0x08,r1
		}

		//Fix item drawing code
		{
			//Reading Code :
			WriteByte(0x4ca29, 0x1c);//06051a28 mov 0xe, r1 needs to be 0x1c
			WriteByte(0x4ca7d, 0x1b);//06051a7c mov 0xd, r1 needs to be 0x1b
			WriteByte(0x4ca83, 0x1c);//06051a82 mov 0xe, r1 needs to be 0x1c
			WriteByte(0x4ca8b, 0x03);//06051a8a mov 0x2, r1 needs to be 0x3
			
			//Writing Code :
			WriteByte(0x4B641, 0x1C);//06050640 mov 0xe, r2 needs to be 0x1c (stride)
			WriteByte(0x4cad3, 0x1C);//06051ad2 mov 0xe, r1 needs to be 0x1c (stride)
			WriteByte(0x4cb07, 0x1b);//06051b06 mov 0xd, r10 needs to be 0x1b (char count)
			WriteByte(0x4cb25, 0x03);//06051b24 mov 0x2, r1 needs to be 0x3 (num lines)
			
			//Buffer address :
			WriteLong(0x4cab0, 0x060a6700);//Read:  06051ab0 needs to be 060a6700
			WriteLong(0x4caec, 0x060a6700);//Write : 06051aec needs to be 060a6700
		}
	}

	//SAKURADA
	{
		const string sakuradaFilePath = inPatchedDirectory + Seperators + string("SAKURADA");
		FileReadWriter sakuraBin;
		if (!sakuraBin.OpenFile(sakuradaFilePath))
		{
			return false;
		}

		//Fix spacing for text drawing
		{
			const uint32 StartAddresses[] = 
			{
				0x0000ca20, 0x0004ca20, 0x0008ca20, 0x000cca20, 0x0010ca20, 0x0014ca20, 0x0018ca20, 0x001cca20, 0x0020ca20, 0x0024ca20, 0x0028ca20,
				0x002cca20, 0x0030ca20
			};

			for (const uint32 startAddress : StartAddresses)
			{
				//Vertical Spacing : starting from 0x06012de8 in memory
				WriteCommand(startAddress + 0, 0xea0c); //mov 0x0c, r10 (ea0c)
				WriteCommand(startAddress + 2, 0x29af); //mul r10, r9 (29af)
				WriteCommand(startAddress + 4, 0x0a1a); //sts MACL, R10 (0a1a)
				WriteCommand(startAddress + 6, 0x7a30); //add 0x30, r10 (7a30)

				//Horizontal Spacing : 06012e26 = > change this to 4100 (SHLL instead of SHLL2)
				WriteCommand(startAddress + 0x3e, 0x4100); //add 0x30, r10 (7a30)
			}
		}//Done fixing spacing

	}

	return true;
}

bool PatchGame(const string& inSourceGameDirectory, const string& inTranslatedDirectory, const string& inPatchedDirectory)
{
	if( !BringOverOriginalFiles(inSourceGameDirectory, inPatchedDirectory) )
	{
		printf("Unable to copy original files\n");
		return false;
	}

	if( !PatchMNameCG(inPatchedDirectory, inTranslatedDirectory) )
	{
		return false;
	}

	if(!PatchIntroScreens(inPatchedDirectory, inTranslatedDirectory))
	{
		printf("Unable to patch Title Screens\n");
		return false;
	}

	if(!PatchTextDrawingCode(inSourceGameDirectory, inPatchedDirectory))
	{
		printf("Unable to patch drawing code\n");
		return false;
	}

	if(!PatchMainMenu(inPatchedDirectory, inTranslatedDirectory))
	{
		printf("Unable to patch main menu\n");
		return false;
	}

	if(!PatchBattleMenu(inPatchedDirectory, inTranslatedDirectory))
	{
		printf("Unable to patch battle menu\n");
		return false;
	}

	//Create translated font sheet
	const string translatedFontSheetPath = inTranslatedDirectory + "8x12.bmp";
	TileExtractor translatedFontSheet;
	PaletteData translatedFontSheetPalette;
	if( !CreateTranslatedFontSheet(translatedFontSheetPath, translatedFontSheet, translatedFontSheetPalette) )
	{
		printf("Unable to create font sheet\n");
		return false;
	}

	//Insert scenario text
	if( !InsertText(inSourceGameDirectory, inTranslatedDirectory, inPatchedDirectory, translatedFontSheet, false) )
	{
		printf("Text insertion failed\n");
		return false;
	}

	//Patch title screens
	const string wpallDirectory = inTranslatedDirectory + "Disc\\1\\WPALL1\\";
	if(!PatchTitleScreens(inPatchedDirectory, wpallDirectory))
	{
		printf("PatchTitleScreens failed\n");
		return false;
	}

	//Patch splash screen
	if(!PatchSplashScreen(inPatchedDirectory, inTranslatedDirectory))
	{
		printf("PatchSplashScreen failed\n");
		return false;
	}

	printf("Patching Successfull!\n");
	return true;
}
