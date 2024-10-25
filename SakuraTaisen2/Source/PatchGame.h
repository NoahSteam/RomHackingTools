#pragma once

#include "Source/PatchBattleTextDrawCode.h"

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
	GetAllFilesOfType(allFiles, "PBOOK_BT", originalFiles);
	GetAllFilesOfType(allFiles, "PBOOK_EC", originalFiles);
	GetAllFilesOfType(allFiles, "CMD_WIN", originalFiles);
	GetAllFilesOfType(allFiles, "PB_EYE", originalFiles);
	GetAllFilesOfType(allFiles, "HANKO", originalFiles);
	GetAllFilesOfType(allFiles, "VDP1.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "GOVER", originalFiles);
	GetAllFilesOfType(allFiles, "CESALOGO.ALL", originalFiles);
	GetAllFilesOfType(allFiles, "TITLE.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "TTL2CGB.BIN", originalFiles);	
	GetAllFilesOfType(allFiles, "M_NAME", originalFiles);
	GetAllFilesOfType(allFiles, "INFONAME.BIN", originalFiles);
	GetAllFilesOfType(allFiles, ".MES", originalFiles);
	GetAllFilesOfType(allFiles, "TUTORI0.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "0.SLG", originalFiles);
	GetAllFilesOfType(allFiles, "EV0", originalFiles);
	GetAllFilesOfType(allFiles, "N_LOAD.ALL", originalFiles);
	GetAllFilesOfType(allFiles, "LOAD", originalFiles);
	GetAllFilesOfType(allFiles, "NBGFILE", originalFiles);
	GetAllFilesOfType(allFiles, "EYECATCH.ALL", originalFiles);
	GetAllFilesOfType(allFiles, "CACG.ALL", originalFiles);
	GetAllFilesOfType(allFiles, "ETCDATA.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "COMMFILE.ALL", originalFiles);
	GetAllFilesOfType(allFiles, "OMK", originalFiles);
	GetAllFilesOfType(allFiles, "CARD_DAT.ALL", originalFiles);
	GetAllFilesOfType(allFiles, "INST_", originalFiles);
	GetAllFilesOfType(allFiles, "MGMRDATA.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "MGRNDATA.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "MGSKDATA.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "MINIGAME.CG", originalFiles);
	GetAllFilesOfType(allFiles, "SYNC", originalFiles);
	GetAllFilesOfType(allFiles, "BG6", originalFiles);
	GetAllFilesOfType(allFiles, "SINRAISP.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "SINRAIP.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "OPBG_1.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "OPCPK.BG", originalFiles);

	GetAllFilesOfType(allFiles, "IRIS_DAT.ALL", originalFiles);
	GetAllFilesOfType(allFiles, "MGKR_DAT.ALL", originalFiles);
	GetAllFilesOfType(allFiles, "MGOH_DAT.ALL", originalFiles);
	GetAllFilesOfType(allFiles, "SUMI_DAT.ALL", originalFiles);
	GetAllFilesOfType(allFiles, "MG_FONT.CG", originalFiles);
	GetAllFilesOfType(allFiles, "0000", originalFiles);

	//Battle files
	GetAllFilesOfType(allFiles, "COL.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "LOW.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "PRG.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "VDP1.BIN", originalFiles);
	GetAllFilesOfType(allFiles, "VDP2.BIN", originalFiles);
	
	//Bring over scenario files
	const string outputDirectory = inPatchedDirectory + Seperators;
	if(!CopyFiles(originalFiles, outputDirectory))
	{
		return false;
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
	
//	CopySingleFile("SAKURADA");
	CopySingleFile("SAKURA.BIN");

	return true;
}

bool PatchTextDrawingCode(const string& inSourceGameDirectory, const string& inPatchedDirectory)
{
	printf("Patching Text Drawing Code\n");

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
				//Need a bigger buffer for empty lines, so start writing from 060a50f0 instead of 060a5180 
				WriteCommand(0xde7a, 0x50f0);
				WriteCommand(0xdeb6, 0x50f0);

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
			WriteByte(0xdfdf, 0x93); //vertical spacing offset by 0x92, from 7190 to 7192 in memory at 06012fde
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
			WriteCommand(0xe380, 0x72ff);//06013380 add 0xf0, r2 72f0  centering horizontally
			WriteByte(0xe4f5, 0x08); //from add 0x10,r1 to add 0x08,r1
			WriteCommand(0xe45e, 0x719a); //from add 0x98, r1, to add 0x9a, r1 at 0601345e 
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

			WriteCommand(0x3010, 0xe56c); //e538 to e56c Number of bytes within text buffer to clear when opening the item menu
		}

		//Fixes for Single Byte Encoding
		#if USE_SINGLE_BYTE_LOOKUPS
		{
			//Regular text reading
			WriteCommand(0xdb08,  0x0009); //No-op for add r0, r0
			WriteCommand(0xdb0a,  0x011c); //06012b0a Text Read at 06012b0a: mov.w @(r0, r1), r1  change to 011c
			WriteCommand(0xd8dc,  0x0009); //No-op for add r0, r0
			WriteCommand(0xd8de,  0x011c); //060128de Text Read at 060128de: mov.w @(r0, r1), r1  change to 011c //when dumping out all text
			WriteCommand(0x1631a, 0x0009); //No-op for add r0, r0
			WriteCommand(0x1631c, 0x011c); //Text Read at 0601B31c: mov.w @(r0, r1), r1  change to 011c //ogami's lines
		//	WriteCommand(0xd6fe,  0x0009); //No-op for add r0, r0
		//	WriteCommand(0xd700,  0x011c); //06012700: same change to 011c
			
			//Compare FFFF and FFFE changed to to FF and FE
			WriteCommand(0xd71a, 0x631c); // Compare value to fffa extu.w r1, r3 0601271a change to 631c
			WriteCommand(0xd78a, 0x00fa); // 06012788 fffa change to 00fa
			WriteCommand(0xde86, 0x666c); // Compare value to fffe extu.w r6, r6 06012e86 change to 666c
			WriteCommand(0xdeb2, 0x00fe); // 06012eb0 fffe change to 00fe
			
			//Lips
			WriteCommand(0xdf4a, 0x0009);
			WriteCommand(0xdf4c, 0x014c); //Lips Text Read at 06012f4c: mov.w @(r0, r4), r1 (add r0, r0 right above)      change to 014c
			WriteCommand(0xdf4e, 0x611c); //extu.w r1, r1 06012f4e (Reading High Bits)
			WriteCommand(0xdf66, 0x00ff); //ffff to 00ff 06012f66 (Reading High Bits)
			WriteCommand(0xdf96, 0x0009);
			WriteCommand(0xdf98, 0x02bc); //Lips Text Read at 06012f98: mov.w @(r0, r11), r2 (add r0, r0 right above)     change to 02bc
			WriteCommand(0xdf9c, 0x622c); //extu.w r2, r2 06012f9c (Reading Hight Bits)
			WriteCommand(0xdf84, 0x61b0); //Lips Text Read at 06012f84: mov.w @r11, r1                                    change to 61b0
			WriteCommand(0xdffe, 0x0009);
			WriteCommand(0xe000, 0x01bc); //Lips Text Read at 06013000: mov.w @(r0, r11), r1 (add r0, r0 right above)     change to 01bc
			WriteCommand(0xe004, 0x611c); //extu.w r1, r1 06013004 (Reading High Bits)
			WriteCommand(0xe036, 0x00ff); //ffff to ff 06013036(Reading High Bits)
			WriteCommand(0xe25c, 0x0009);
			WriteCommand(0xe25e, 0x014c); //Lips Text Read at 0601325e: mov.w @(r0, r4), r1   (add r0, r0 right above)    change to 014c
			WriteCommand(0xe274, 0x014c); //Lips Text Read at 06013274: mov.w @(r0, r4), r1                               change to 014c
			WriteCommand(0xe3dc, 0x61a0); //Lips Text Read at 060133dc: mov.w @r10, r1                                    change to 61a0
			WriteCommand(0xe3f6, 0x0009);
			WriteCommand(0xe428, 0x02ac); //Lips Text Read at 06013428: mov.w @(r0, r10), r2 (add r0, r0 060133f6)        change to 02ac
			WriteCommand(0xe4fa, 0x0009);
			WriteCommand(0xe4fc, 0x01ac); //Lips Text Read at 060134fc: mov.w @(r0, r10), r1 (add r0, r0 right above)     change to 01ac

			//Auto-Resume Commands (Used in sparring sequence and some kinematron events like in SK0206.BIN where we have fffa <code> at the end of the line
			WriteCommand(0xd6fe, 0x011c);	//060126fe nop                  /011d to 011c extu.b r1, r1
			WriteCommand(0xd700, 0x611c);	//6012700 mov.b @(r0, r1), r1  //300c to 611c

			WriteCommand(0xd7f6, 0x011c);	//060127f6 add r0, r0          //300c to 011c
			WriteCommand(0xd7f8, 0x611c);	//060127f8 mov.w @(r0, r1), r1 //011d to 611c extu.b r1, r1
			WriteCommand(0xd810,0x011c);	//06012810 add r0, r0          //300c to 0009
			WriteCommand(0xd812,0x611c);	//06012812 mov.w @(r0, r1), r1 //011d to 011c  Swap with above and change this to extu.b r1, r1 (611c)
			WriteCommand(0xdd2a,0x611c);	//06012d2a extu.w r1, r1       //611d to 611c
			WriteCommand(0x16316,0x601c);	//0601b316 extu.w r1, r0       //601d to 601c
			WriteCommand(0x1631e,0x611c);	//0601b31e exts.w r1, r1       //611d to 611c

			//More Lips commands along with fix for all lines flipping instead of just the newly appeared one
			WriteCommand(0xe262, 0x611c); //06013262  extu.w r1, r1      //611d to 611c
			WriteCommand(0xe26c, 0x015c); //0601326c mov.w @(r0, r5), r1 //015d
			WriteCommand(0xe26e, 0x621c); //0601326e extu.w r1, r2       //621d to 621c
			WriteCommand(0xe276, 0x611c); //06013276 extu.w r1, r1       //611d to 611c
			WriteCommand(0xe284, 0x0009); //06013284 add r0, r0          //300c to 0009 
			WriteCommand(0xe286, 0x014c); //06013286 mov.w @(r0, r4), r1 //014d  to 014c
			WriteCommand(0xe288, 0x611c); //06013288 extu.w r1, r1       //611d to 611c
			WriteCommand(0xe29a, 0x00ff); //0601329a ffff to ff
			WriteCommand(0xe3e0, 0x611c); //060133e0 extu.w r1, r1       //611d to 611c
			WriteCommand(0xe4ae, 0x00ff); //060134ae ffff to ff
			WriteCommand(0xe42e, 0x622c); //0601342e extu.w r2, r2       //622d to 622c			
			WriteCommand(0xe4fe, 0x611c); //060134fe extu.w r1, r1       //611d to 611c			
						
			//Dog Lines
			// 0602c580	=> 27580
			// 0602c582	=> 27582
			// 0602c584	=> 27584
			// 0602c58a	=> 2758A
			// 0602c5a4	=> 275A4
			// 0602c5a8	=> 275A8
			// 0602c5b0	=> 275B0
			// 0602c5b2	=> 275B2
			// 0602c5b4	=> 275B4
			// 0602c5b6	=> 275B6
			// 0602c5b8	=> 275B8
			// 0602c5be	=> 275BE
			// 0602c5ea	=> 275EA
			// 0602c5c2	=> 275C2

			WriteCommand(0x275A4, 0x012c); // 0602c5a4 mov.w @(r0, r2), r1 change to 012c
			WriteCommand(0x275B2, 0x6134); // 0602c5b2 mov.w @r3 + , r1   6134
			WriteCommand(0x275B6, 0x6130); // 0602c5b6 mov.w @r3, r1    6130			
			WriteCommand(0x275B4, 0x2710); // 0602c5b4 mov.w r1, @r7 2710
			WriteCommand(0x275C2, 0x2710); // 0602c5c2 mov.w r1, @r7 2710
			WriteCommand(0x275BE, 0x7701); // 0602c5be add 0x2, r7 //spacing change to 7701
			WriteCommand(0x27580, 0x6170); // 0602c580 mov.w @r7, r1 6170
			WriteCommand(0x27582, 0x6170); // 0602c582 mov.w @r7, r1 6170
			WriteCommand(0x2758A, 0x7701); // 0602c58a add 2, r7     7701			
			WriteCommand(0x275EA, 0x00ff); // 0602c5ea change to 00ff
			WriteCommand(0x27584, 0x611c); // 0602c584 extu.w r1, r1 change to 611c
			WriteCommand(0x275B8, 0x611c); // 0602c5b8 611c
			WriteCommand(0x275A8, 0x611c); // 0602c5a8 611c
			WriteCommand(0x275B0, 0x77ff); // 0602c5b0 add 0xfe, r1 change to 77ff

			//Item Menu
			WriteCommand(0x4B5B0, 0x011c); // 060505b0 add r0, r0 TO mov.b @(r0, r1), r1
			WriteCommand(0x4B5B2, 0x611c); // 060505b2 mov.w@(r0, r1, r1) TO extu.w r1, r1 //011d to 611c
			WriteCommand(0x4B5C8, 0x621c); // 060505c8 extu.w r1, r2  621d TO 621c
			WriteCommand(0x4B5f8, 0xe21c); // 060505f8 e20e to e21c  max chars per line
			WriteCommand(0x4B612, 0xe203); // 06050612 e202 to e203  max lines
			WriteCommand(0x4B7EA, 0x00fe); // 060507ea fffe = > 00fe
			WriteCommand(0x4B7EE, 0x00ff); // 060507ee ffff = > 00ff

			WriteCommand(0x4B660, 0x666c); // 06050660 exut.w r6, r6       //666d to 666c

			//Auto-resume commands during Chapter 10 and 9
			WriteCommand(0xdbf6, 0x00fa); // 06012bf4 fffa => 00ff
			WriteCommand(0xdb1c, 0x7105); // 06012b1c add 4, r1 //7104 to 7105
			WriteCommand(0xdb1e, 0x6110); // 06012b1e mov.w @r1, r1 //6111 to 6110
			WriteCommand(0xdb22, 0x631c); // 06012b22 extu.w r1, r3 //631d to 631c
			WriteCommand(0xdc38, 0x666c); // 06012c38 extu.w r6, r6 //666d to 666c

			//Auto-resume at the end of SK0808  (macro 66 in debug mode) (also used by Ogami lines)
			WriteCommand(0x24f4e, 0x6200);// 06029f4e mov.w @r0, r2 //mov.w @r0, r2 6201 to 6200
			WriteCommand(0x24f58, 0x7001);// 06029f58 add 0x2, r0  // add 0x1, r0 7002 to 7001
			WriteCommand(0x24f5a, 0x6200);// 06029f5a mov.w @r0, r2  //6201 to 6200
			WriteCommand(0x24f5c, 0x612c);// 06029f5c extu.w r2, r1  //612d to 612c
			WriteCommand(0x24f76, 0x00ff);// 06029f74 ffff => 00ff
		}
		#endif
	}

	//SAKURADA
	if(0)
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

bool PatchGame(const string& inSourceGameDirectory, const string& inTranslatedDataDirectory, const string& inPatchedDirectory, int inDiscNumber)
{
	if( !BringOverOriginalFiles(inSourceGameDirectory, inPatchedDirectory) )
	{
		printf("Unable to copy original files\n");
		return false;
	}
	
	if(inDiscNumber == 2)
	{
		if(!PatchBattleAnimViewer(inPatchedDirectory, inTranslatedDataDirectory))
		{
			printf("Unable to patch BattleAnimViewer\n");
			return false;
		}

		if (!PatchBattleSimulator(inPatchedDirectory, inTranslatedDataDirectory))
		{
			printf("Unable to patch BattleSimulator\n");
			return false;
		}
	}

	if(!PatchIntroSubtitles(inPatchedDirectory, inTranslatedDataDirectory, inDiscNumber))
	{
		printf("Unable to patch IntroSubtitles\n");
		return false;
	}

	if(inDiscNumber != 3 && !PatchMiniGames(inPatchedDirectory, inTranslatedDataDirectory))
	{
		printf("Unable to patch MiniGames\n");
		return false;
	}

	if (inDiscNumber != 3 && !PatchTycoon(inPatchedDirectory, inTranslatedDataDirectory))
	{
		printf("Unable to patch Tycoon\n");
		return false;
	}

	if (!PatchLongDay(inPatchedDirectory, inTranslatedDataDirectory))
	{
		printf("Unable to patch Long Day\n");
		return false;
	}

	if (!PatchKinematron(inPatchedDirectory, inTranslatedDataDirectory))
	{
		printf("Unable to patch Kinematron\n");
		return false;
	}

	if (!PatchWarningScreens(inPatchedDirectory, inTranslatedDataDirectory))
	{
		printf("Unable to patch Warning Screens\n");
		return false;
	}
	
	if(!PatchStatusScreen(inPatchedDirectory, inTranslatedDataDirectory))
	{
		printf("Unable to patch Status Screen\n");
		return false;
	}

	if(!PatchEyeCatch(inPatchedDirectory, inTranslatedDataDirectory))
	{
		printf("Unable to patch EyeCatch\n");
		return false;
	}

	if(!PatchNBGFile(inPatchedDirectory, inTranslatedDataDirectory, inDiscNumber))
	{
		printf("Unable to patch NBGFile\n");
		return false;
	}

	if(!PatchLoadScreens(inPatchedDirectory, inTranslatedDataDirectory))
	{
		printf("Unable to patch LoadScreens\n");
		return false;
	}

	if(!PatchBattleLoadScreens(inPatchedDirectory, inTranslatedDataDirectory, inDiscNumber))
	{
		printf("Unable to patch BattleLoadScreens\n");
		return false;
	}

	if (!PatchBGDatFiles(inPatchedDirectory, inTranslatedDataDirectory))
	{
		printf("Unable to patch BGDat files\n");
		return false;
	}

	if( !PatchInfoName(inPatchedDirectory, inTranslatedDataDirectory) )
	{
		printf("Unable to patch PatchInfoName\n");
		return false;
	}

	if( !PatchMNameCG(inPatchedDirectory, inTranslatedDataDirectory) )
	{
		printf("Unable to patch MNameCG\n");
		return false;
	}

	if(!PatchIntroScreens(inPatchedDirectory, inTranslatedDataDirectory, inDiscNumber))
	{
		printf("Unable to patch Title Screens\n");
		return false;
	}

	if(!PatchTextDrawingCode(inSourceGameDirectory, inPatchedDirectory))
	{
		printf("Unable to patch drawing code\n");
		return false;
	}

	if( !PatchBattleTextDrawingCode(inSourceGameDirectory, inPatchedDirectory, inTranslatedDataDirectory) )
	{
		printf("Unable to patch battle text drawing code\n");
		return false;
	}

	if(!PatchMainMenu(inPatchedDirectory, inTranslatedDataDirectory))
	{
		printf("Unable to patch main menu\n");
		return false;
	}

	if(!PatchBattleMenu(inPatchedDirectory, inTranslatedDataDirectory, inDiscNumber))
	{
		printf("Unable to patch battle menu\n");
		return false;
	}

	//Create translated font sheet
	const string translatedFontSheetPath = inTranslatedDataDirectory + "8x12.bmp";
	TileExtractor translatedFontSheet;
	PaletteData translatedFontSheetPalette;
	if( !CreateTranslatedFontSheet(translatedFontSheetPath, translatedFontSheet, translatedFontSheetPalette, 16, 16) )
	{
		printf("Unable to create font sheet\n");
		return false;
	}

	//Create translated font sheet for battles
#if FIX_SLG_FONT_DRAWING_SIZE
	const string translatedBattleFontSheetPath = inTranslatedDataDirectory + "8x12_SLG2.bmp";
	uint32 battleFontWidth = 8;
	uint32 battleFontHeight = 12;
#else
	const string translatedBattleFontSheetPath = translatedFontSheetPath;
	uint32 battleFontWidth = 16;
	uint32 battleFontHeight = 16;
#endif

	TileExtractor translatedBattleFontSheet;
	PaletteData translatedBattleFontSheetPalette;
	if (!CreateTranslatedFontSheet(translatedBattleFontSheetPath, translatedBattleFontSheet, translatedBattleFontSheetPalette, battleFontWidth, battleFontHeight))
	{
		printf("Unable to create battle font sheet\n");
		return false;
	}

	if (inDiscNumber != 3 && !PatchTycoonTutorial(inSourceGameDirectory, inPatchedDirectory, inTranslatedDataDirectory, translatedBattleFontSheet))
	{
		printf("PatchTycoonTutorial failed\n");
		return false;
	}

	//Insert scenario text
	if (!InsertSysFileText(inSourceGameDirectory, inTranslatedDataDirectory, inPatchedDirectory, translatedBattleFontSheet, false))
	{
		printf("Text insertion failed\n");
		return false;
	}

	//Insert scenario text
	if( !InsertText(inSourceGameDirectory, inTranslatedDataDirectory, inPatchedDirectory, translatedFontSheet, false) )
	{
		printf("Text insertion failed\n");
		return false;
	}

	if(!HackFixAutoResumeLines(inPatchedDirectory))
	{
		printf("Patching sync data failed\n");

		return false;
	}

	
	if(!VerifyAutoResumeLines(inPatchedDirectory))
	{

	}
	
	//Patch title screens
	if(!PatchTitleScreens(inPatchedDirectory, inTranslatedDataDirectory, inDiscNumber))
	{
		printf("PatchTitleScreens failed\n");
		return false;
	}

	//Patch splash screen
	if(!PatchSplashScreen(inPatchedDirectory, inTranslatedDataDirectory))
	{
		printf("PatchSplashScreen failed\n");
		return false;
	}

	printf("Patching Successfull!\n");
	return true;
}
