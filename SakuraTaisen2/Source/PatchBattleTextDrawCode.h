#pragma once

bool PatchBattleTextDrawingCode(const string& inPatchedDirectory)
{
	struct PatchingData
	{
		union Command
		{
			uint16 word;
			uint8 byte;
		};

		uint32 address;
		Command command;
	};

	struct PatchingEntry
	{
		const char* pFileName;
		PatchingData data;	
	};

	PatchingEntry patchingInfo[] = 
	{
		//0x06089264 checking for 0xe, change to 0x1b
		"M00PRG.BIN", 0x0002fc1e, 0x1b,
		"M00PRG.BIN", 0x0002fe64, 0x1b,
		"M01PRG.BIN", 0x00028cbe, 0x1b,
		"M01PRG.BIN", 0x00028f04, 0x1b,
		"M02PRG.BIN", 0x0002d45e, 0x1b,
		"M02PRG.BIN", 0x0002d6a4, 0x1b,
		"M03PRG.BIN", 0x0002e442, 0x1b,
		"M03PRG.BIN", 0x0002e688, 0x1b,
		"M04PRG.BIN", 0x0002ebf2, 0x1b,
		"M04PRG.BIN", 0x0002ee38, 0x1b,
		"M05PRG.BIN", 0x0002d766, 0x1b,
		"M05PRG.BIN", 0x0002d9ac, 0x1b,
		"M26PRG.BIN", 0x0002f56a, 0x1b,
		"M26PRG.BIN", 0x0002f7b0, 0x1b,
		"M27PRG.BIN", 0x0002d31a, 0x1b,
		"M27PRG.BIN", 0x0002d560, 0x1b,
		"M29PRG.BIN", 0x00027156, 0x1b,
		"M29PRG.BIN", 0x0002739c, 0x1b,
		"M31PRG.BIN", 0x0002d59e, 0x1b,
		"M31PRG.BIN", 0x0002d7e4, 0x1b,
		"M33PRG.BIN", 0x0002e10e, 0x1b,
		"M33PRG.BIN", 0x0002e354, 0x1b,
		"M34PRG.BIN", 0x0002d1ea, 0x1b,
		"M34PRG.BIN", 0x0002d430, 0x1b,
		"SLGGOVER.BIN", 0x0001bb8e, 0x1b,
		"SLGGOVER.BIN", 0x0001bdd4, 0x1b,

		//0x060893b0 multiplying by 16 (height spacing) (make this 4b00)
		"M00PRG.BIN",  0x0002ffb0, 0x00,
		"M01PRG.BIN",  0x00029050, 0x00,
		"M02PRG.BIN",  0x0002d7f0, 0x00,
		"M03PRG.BIN",  0x0002e7d4, 0x00,
		"M04PRG.BIN",  0x0002ef84, 0x00,
		"M05PRG.BIN",  0x0002daf8, 0x00,
		"M26PRG.BIN",  0x0002f8fc, 0x00,
		"M27PRG.BIN",  0x0002d6ac, 0x00,
		"M29PRG.BIN",  0x000274e8, 0x00,
		"M31PRG.BIN",  0x0002d930, 0x00,
		"M33PRG.BIN",  0x0002e4a0, 0x00,
		"M34PRG.BIN",  0x0002d57c, 0x00,
		"SLGGOVER.BIN", 0x0001bf20, 0x00,

		//0x060893e0 will multiply by 16 (width spacing) (make this 4100 shll1)
		"M00PRG.BIN", 0x0002ffe0, 0x00,
		"M01PRG.BIN", 0x00029080, 0x00,
		"M02PRG.BIN", 0x0002d820, 0x00,
		"M03PRG.BIN", 0x0002e804, 0x00,
		"M04PRG.BIN", 0x0002efb4, 0x00,
		"M05PRG.BIN", 0x0002db28, 0x00,
		"M26PRG.BIN", 0x0002f92c, 0x00,
		"M27PRG.BIN", 0x0002d6dc, 0x00,
		"M29PRG.BIN", 0x00027518, 0x00,
		"M31PRG.BIN", 0x0002d960, 0x00,
		"M33PRG.BIN", 0x0002e4d0, 0x00,
		"M34PRG.BIN", 0x0002d5ac, 0x00,
		"SLGGOVER.BIN", 0x0001bf50, 0x00,

		//0x060893fc is characters per line (make this E11B)
		"M00PRG.BIN", 0x0002fffc, 0x1b,
		"M01PRG.BIN", 0x0002909c, 0x1b,
		"M02PRG.BIN", 0x0002d83c, 0x1b,
		"M03PRG.BIN", 0x0002e820, 0x1b,
		"M04PRG.BIN", 0x0002efd0, 0x1b,
		"M05PRG.BIN", 0x0002db44, 0x1b,
		"M26PRG.BIN", 0x0002f948, 0x1b,
		"M27PRG.BIN", 0x0002d6f8, 0x1b,
		"M29PRG.BIN", 0x00027534, 0x1b,
		"M31PRG.BIN", 0x0002d97c, 0x1b,
		"M33PRG.BIN", 0x0002e4ec, 0x1b,
		"M34PRG.BIN", 0x0002d5c8, 0x1b,
		"SLGGOVER.BIN", 0x0001bf6c, 0x1b,

		//0x06089408 is num lines (make this e103)
		"M00PRG.BIN", 0x00030008, 0x03,
		"M01PRG.BIN", 0x000290a8, 0x03,
		"M02PRG.BIN", 0x0002d848, 0x03,
		"M03PRG.BIN", 0x0002e82c, 0x03,
		"M04PRG.BIN", 0x0002efdc, 0x03,
		"M05PRG.BIN", 0x0002db50, 0x03,
		"M26PRG.BIN", 0x0002f954, 0x03,
		"M27PRG.BIN", 0x0002d704, 0x03,
		"M29PRG.BIN", 0x00027540, 0x03,
		"M31PRG.BIN", 0x0002d988, 0x03,
		"M33PRG.BIN", 0x0002e4f8, 0x03,
		"M34PRG.BIN", 0x0002d5d4, 0x03,
		"SLGGOVER.BIN", 0x0001bf78, 0x03,

		//0x06089451 multiplies by 0xe to get to next line write buffer, change to 0x1c
		"M00PRG.BIN", 0x00030050, 0x1b,
		"M01PRG.BIN", 0x000290f0, 0x1b,
		"M02PRG.BIN", 0x0002d890, 0x1b,
		"M03PRG.BIN", 0x0002e874, 0x1b,
		"M04PRG.BIN", 0x0002f024, 0x1b,
		"M05PRG.BIN", 0x0002db98, 0x1b,
		"M26PRG.BIN", 0x0002f99c, 0x1b,
		"M27PRG.BIN", 0x0002d74c, 0x1b,
		"M29PRG.BIN", 0x00027588, 0x1b,
		"M31PRG.BIN", 0x0002d9d0, 0x1b,
		"M33PRG.BIN", 0x0002e540, 0x1b,
		"M34PRG.BIN", 0x0002d61c, 0x1b,
		"SLGGOVER.BIN", 0x0001bfc0, 0x1b,

		//060894b0 is 0xa7f.  This is the amount of bytes to clear in vdp1 when clearing the text box.
		//Change this to 27*4 * 64(bytes)
		"M00PRG.BIN", 0x000300b0, (27*4*66),
		"M01PRG.BIN", 0x00029150, (27*4*66),
		"M02PRG.BIN", 0x0002d8f0, (27*4*66),
		"M03PRG.BIN", 0x0002e8d4, (27*4*66),
		"M04PRG.BIN", 0x0002f084, (27*4*66),
		"M05PRG.BIN", 0x0002dbf8, (27*4*66),
		"M26PRG.BIN", 0x0002f9fc, (27*4*66),
		"M27PRG.BIN", 0x0002d7ac, (27*4*66),
		"M29PRG.BIN", 0x000275e8, (27*4*66),
		"M31PRG.BIN", 0x0002da30, (27*4*66),
		"M33PRG.BIN", 0x0002e5a0, (27*4*66),
		"M34PRG.BIN", 0x0002d67c, (27*4*66),
		"SLGGOVER.BIN", 0x0001c020, (27*4*66),
	};

	const int numEntries = sizeof(patchingInfo) / sizeof(patchingInfo[0]);
	for( int i = 0; i < numEntries; ++i )
	{		
		const string sakuraBinFilePath = inPatchedDirectory + Seperators + string("SAKURA2\\") + string(patchingInfo[i].pFileName);
		FileReadWriter sakuraBin;
		if (!sakuraBin.OpenFile(sakuraBinFilePath))
		{
			return false;
		}

		const bool bIsWord = (patchingInfo[i].data.command.word & 0xff00) != 0;
		if( bIsWord )
		{
			sakuraBin.WriteData(patchingInfo[i].data.address, (char*)&patchingInfo[i].data.command.word, 2, true);
		}
		else
		{
			sakuraBin.WriteData(patchingInfo[i].data.address + 1, (char*)&patchingInfo[i].data.command.byte, 1, false);
		}
	}

	return true;
}