#pragma once

bool PatchBattleTextDrawingCode(const string& inPatchedDirectory, const string& inDataDirectory)
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

	const string lipPatchPath = inDataDirectory + Seperators + "BattleLipsPatching.bin";
	FileData lipsPatchData;
	if(!lipsPatchData.InitializeFileData(lipPatchPath))
	{
		return false;
	}

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

		//0x060893b0 multiplying by 16 (height spacing) We need to multiply by 12
		"M00PRG.BIN",  0x0002ffb0 - 4, 0xeb0c, //mov 0x0c, r11
		"M00PRG.BIN",  0x0002ffb0 - 2, 0x2abe, //mulu.w, r11, r10
		"M00PRG.BIN",  0x0002ffb0,   0x0b1a, //sts macl. r11

		"M01PRG.BIN",  0x00029050 - 4, 0xeb0c,
		"M01PRG.BIN",  0x00029050 - 2, 0x2abe,
		"M01PRG.BIN",  0x00029050, 0x0b1a,

		"M02PRG.BIN",  0x0002d7f0 - 4, 0xeb0c,
		"M02PRG.BIN",  0x0002d7f0 - 2, 0x2abe,
		"M02PRG.BIN",  0x0002d7f0, 0x0b1a,

		"M03PRG.BIN",  0x0002e7d4 - 4, 0xeb0c,
		"M03PRG.BIN",  0x0002e7d4 - 2, 0x2abe,
		"M03PRG.BIN",  0x0002e7d4, 0x0b1a,

		"M04PRG.BIN",  0x0002ef84 - 4, 0xeb0c,
		"M04PRG.BIN",  0x0002ef84 - 2, 0x2abe,
		"M04PRG.BIN",  0x0002ef84, 0x0b1a,

		"M05PRG.BIN",  0x0002daf8 - 4, 0xeb0c,
		"M05PRG.BIN",  0x0002daf8 - 2, 0x2abe,
		"M05PRG.BIN",  0x0002daf8, 0x0b1a,

		"M26PRG.BIN",  0x0002f8fc - 4, 0xeb0c,
		"M26PRG.BIN",  0x0002f8fc - 2, 0x2abe,
		"M26PRG.BIN",  0x0002f8fc, 0x0b1a,

		"M27PRG.BIN",  0x0002d6ac - 4, 0xeb0c,
		"M27PRG.BIN",  0x0002d6ac - 2, 0x2abe,
		"M27PRG.BIN",  0x0002d6ac, 0x0b1a,

		"M29PRG.BIN",  0x000274e8 - 4, 0xeb0c,
		"M29PRG.BIN",  0x000274e8 - 2, 0x2abe,
		"M29PRG.BIN",  0x000274e8, 0x0b1a,

		"M31PRG.BIN",  0x0002d930 - 4, 0xeb0c,
		"M31PRG.BIN",  0x0002d930 - 2, 0x2abe,
		"M31PRG.BIN",  0x0002d930, 0x0b1a,

		"M33PRG.BIN",  0x0002e4a0 - 4, 0xeb0c,
		"M33PRG.BIN",  0x0002e4a0 - 2, 0x2abe,
		"M33PRG.BIN",  0x0002e4a0, 0x0b1a,

		"M34PRG.BIN",  0x0002d57c - 4, 0xeb0c,
		"M34PRG.BIN",  0x0002d57c - 2, 0x2abe,
		"M34PRG.BIN",  0x0002d57c, 0x0b1a,

		"SLGGOVER.BIN", 0x0001bf20 - 4, 0xeb0c,
		"SLGGOVER.BIN", 0x0001bf20 - 2, 0x2abe,
		"SLGGOVER.BIN", 0x0001bf20, 0x0b1a,

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

		//0x06089450 multiplies by 0xe to get to next line write buffer, change to 0x1c
		"M00PRG.BIN", 0x00030050, 0x1c,
		"M01PRG.BIN", 0x000290f0, 0x1c,
		"M02PRG.BIN", 0x0002d890, 0x1c,
		"M03PRG.BIN", 0x0002e874, 0x1c,
		"M04PRG.BIN", 0x0002f024, 0x1c,
		"M05PRG.BIN", 0x0002db98, 0x1c,
		"M26PRG.BIN", 0x0002f99c, 0x1c,
		"M27PRG.BIN", 0x0002d74c, 0x1c,
		"M29PRG.BIN", 0x00027588, 0x1c,
		"M31PRG.BIN", 0x0002d9d0, 0x1c,
		"M33PRG.BIN", 0x0002e540, 0x1c,
		"M34PRG.BIN", 0x0002d61c, 0x1c,
		"SLGGOVER.BIN", 0x0001bfc0, 0x1c,

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

		//060892a2 change to 0x1a
		"M00PRG.BIN", 0x0002fc5c, 0x1a,
		"M00PRG.BIN", 0x0002fd76, 0x1a,
		"M00PRG.BIN", 0x0002fea2, 0x1a,
		"M01PRG.BIN", 0x00028cfc, 0x1a,
		"M01PRG.BIN", 0x00028e16, 0x1a,
		"M01PRG.BIN", 0x00028f42, 0x1a,
		"M02PRG.BIN", 0x0002d49c, 0x1a,
		"M02PRG.BIN", 0x0002d5b6, 0x1a,
		"M02PRG.BIN", 0x0002d6e2, 0x1a,
		"M03PRG.BIN", 0x0002e480, 0x1a,
		"M03PRG.BIN", 0x0002e59a, 0x1a,
		"M03PRG.BIN", 0x0002e6c6, 0x1a,
		"M04PRG.BIN", 0x0002ec30, 0x1a,
		"M04PRG.BIN", 0x0002ed4a, 0x1a,
		"M04PRG.BIN", 0x0002ee76, 0x1a,
		"M05PRG.BIN", 0x0002d7a4, 0x1a,
		"M05PRG.BIN", 0x0002d8be, 0x1a,
		"M05PRG.BIN", 0x0002d9ea, 0x1a,
		"M26PRG.BIN", 0x0002f5a8, 0x1a,
		"M26PRG.BIN", 0x0002f6c2, 0x1a,
		"M26PRG.BIN", 0x0002f7ee, 0x1a,
		"M27PRG.BIN", 0x0002d358, 0x1a,
		"M27PRG.BIN", 0x0002d472, 0x1a,
		"M27PRG.BIN", 0x0002d59e, 0x1a,
		"M29PRG.BIN", 0x00027194, 0x1a,
		"M29PRG.BIN", 0x000272ae, 0x1a,
		"M29PRG.BIN", 0x000273da, 0x1a,
		"M31PRG.BIN", 0x0002d5dc, 0x1a,
		"M31PRG.BIN", 0x0002d6f6, 0x1a,
		"M31PRG.BIN", 0x0002d822, 0x1a,
		"M33PRG.BIN", 0x0002e14c, 0x1a,
		"M33PRG.BIN", 0x0002e266, 0x1a,
		"M33PRG.BIN", 0x0002e392, 0x1a,
		"M34PRG.BIN", 0x0002d228, 0x1a,
		"M34PRG.BIN", 0x0002d342, 0x1a,
		"M34PRG.BIN", 0x0002d46e, 0x1a,
		"SLGGOVER.BIN", 0x0001bbcc, 0x1a,
		"SLGGOVER.BIN", 0x0001bce6, 0x1a,
		"SLGGOVER.BIN", 0x0001be12, 0x1a,

		//0602063e will write up to 0xf characters per line(mov 0x0f, r3)
		"0.SLG", 0x0001b63e, 0x1c,

		//0608905c within giant function
		"M00PRG.BIN", 0x0002fc5c, 0x1b,
		"M00PRG.BIN", 0x0002fd76, 0x1b,
		"M00PRG.BIN", 0x0002fea2, 0x1b,
		"M01PRG.BIN", 0x00028cfc, 0x1b,
		"M01PRG.BIN", 0x00028e16, 0x1b,
		"M01PRG.BIN", 0x00028f42, 0x1b,
		"M02PRG.BIN", 0x0002d49c, 0x1b,
		"M02PRG.BIN", 0x0002d5b6, 0x1b,
		"M02PRG.BIN", 0x0002d6e2, 0x1b,
		"M03PRG.BIN", 0x0002e480, 0x1b,
		"M03PRG.BIN", 0x0002e59a, 0x1b,
		"M03PRG.BIN", 0x0002e6c6, 0x1b,
		"M04PRG.BIN", 0x0002ec30, 0x1b,
		"M04PRG.BIN", 0x0002ed4a, 0x1b,
		"M04PRG.BIN", 0x0002ee76, 0x1b,
		"M05PRG.BIN", 0x0002d7a4, 0x1b,
		"M05PRG.BIN", 0x0002d8be, 0x1b,
		"M05PRG.BIN", 0x0002d9ea, 0x1b,
		"M26PRG.BIN", 0x0002f5a8, 0x1b,
		"M26PRG.BIN", 0x0002f6c2, 0x1b,
		"M26PRG.BIN", 0x0002f7ee, 0x1b,
		"M27PRG.BIN", 0x0002d358, 0x1b,
		"M27PRG.BIN", 0x0002d472, 0x1b,
		"M27PRG.BIN", 0x0002d59e, 0x1b,
		"M29PRG.BIN", 0x00027194, 0x1b,
		"M29PRG.BIN", 0x000272ae, 0x1b,
		"M29PRG.BIN", 0x000273da, 0x1b,
		"M31PRG.BIN", 0x0002d5dc, 0x1b,
		"M31PRG.BIN", 0x0002d6f6, 0x1b,
		"M31PRG.BIN", 0x0002d822, 0x1b,
		"M33PRG.BIN", 0x0002e14c, 0x1b,
		"M33PRG.BIN", 0x0002e266, 0x1b,
		"M33PRG.BIN", 0x0002e392, 0x1b,
		"M34PRG.BIN", 0x0002d228, 0x1b,
		"M34PRG.BIN", 0x0002d342, 0x1b,
		"M34PRG.BIN", 0x0002d46e, 0x1b,
		"SLGGOVER.BIN", 0x0001bbcc, 0x1b,
		"SLGGOVER.BIN", 0x0001bce6, 0x1b,
		"SLGGOVER.BIN", 0x0001be12, 0x1b,

		//0608912e More within giant function
		"M00PRG.BIN", 0x0002fc1e, 0x1c,
		"M00PRG.BIN", 0x0002fd2e, 0x1c,
		"M00PRG.BIN", 0x0002fe64, 0x1c,
		"M01PRG.BIN", 0x00028cbe, 0x1c,
		"M01PRG.BIN", 0x00028dce, 0x1c,
		"M01PRG.BIN", 0x00028f04, 0x1c,
		"M02PRG.BIN", 0x0002d45e, 0x1c,
		"M02PRG.BIN", 0x0002d56e, 0x1c,
		"M02PRG.BIN", 0x0002d6a4, 0x1c,
		"M03PRG.BIN", 0x0002e442, 0x1c,
		"M03PRG.BIN", 0x0002e552, 0x1c,
		"M03PRG.BIN", 0x0002e688, 0x1c,
		"M04PRG.BIN", 0x0002ebf2, 0x1c,
		"M04PRG.BIN", 0x0002ed02, 0x1c,
		"M04PRG.BIN", 0x0002ee38, 0x1c,
		"M05PRG.BIN", 0x0002d766, 0x1c,
		"M05PRG.BIN", 0x0002d876, 0x1c,
		"M05PRG.BIN", 0x0002d9ac, 0x1c,
		"M26PRG.BIN", 0x0002f56a, 0x1c,
		"M26PRG.BIN", 0x0002f67a, 0x1c,
		"M26PRG.BIN", 0x0002f7b0, 0x1c,
		"M27PRG.BIN", 0x0002d31a, 0x1c,
		"M27PRG.BIN", 0x0002d42a, 0x1c,
		"M27PRG.BIN", 0x0002d560, 0x1c,
		"M29PRG.BIN", 0x00027156, 0x1c,
		"M29PRG.BIN", 0x00027266, 0x1c,
		"M29PRG.BIN", 0x0002739c, 0x1c,
		"M31PRG.BIN", 0x0002d59e, 0x1c,
		"M31PRG.BIN", 0x0002d6ae, 0x1c,
		"M31PRG.BIN", 0x0002d7e4, 0x1c,
		"M33PRG.BIN", 0x0002e10e, 0x1c,
		"M33PRG.BIN", 0x0002e21e, 0x1c,
		"M33PRG.BIN", 0x0002e354, 0x1c,
		"M34PRG.BIN", 0x0002d1ea, 0x1c,
		"M34PRG.BIN", 0x0002d2fa, 0x1c,
		"M34PRG.BIN", 0x0002d430, 0x1c,
		"SLGGOVER.BIN", 0x0001bb8e, 0x1c,
		"SLGGOVER.BIN", 0x0001bc9e, 0x1c,
		"SLGGOVER.BIN", 0x0001bdd4, 0x1c,

		//0608903c and 06089282 change to 0x03 (number of lines)
		"M00PRG.BIN", 0x0002fc3c, 0x03,
		"M00PRG.BIN", 0x0002fe82, 0x03,
		"M01PRG.BIN", 0x00028cdc, 0x03,
		"M01PRG.BIN", 0x00028f22, 0x03,
		"M02PRG.BIN", 0x0002d47c, 0x03,
		"M02PRG.BIN", 0x0002d6c2, 0x03,
		"M03PRG.BIN", 0x0002e460, 0x03,
		"M03PRG.BIN", 0x0002e6a6, 0x03,
		"M04PRG.BIN", 0x0002ec10, 0x03,
		"M04PRG.BIN", 0x0002ee56, 0x03,
		"M05PRG.BIN", 0x0002d784, 0x03,
		"M05PRG.BIN", 0x0002d9ca, 0x03,
		"M26PRG.BIN", 0x0002f588, 0x03,
		"M26PRG.BIN", 0x0002f7ce, 0x03,
		"M27PRG.BIN", 0x0002d338, 0x03,
		"M27PRG.BIN", 0x0002d57e, 0x03,
		"M29PRG.BIN", 0x00027174, 0x03,
		"M29PRG.BIN", 0x000273ba, 0x03,
		"M31PRG.BIN", 0x0002d5bc, 0x03,
		"M31PRG.BIN", 0x0002d802, 0x03,
		"M33PRG.BIN", 0x0002e12c, 0x03,
		"M33PRG.BIN", 0x0002e372, 0x03,
		"M34PRG.BIN", 0x0002d208, 0x03,
		"M34PRG.BIN", 0x0002d44e, 0x03,
		"SLGGOVER.BIN", 0x0001bbac, 0x03,
		"SLGGOVER.BIN", 0x0001bdf2, 0x03,

		// 0608914c
		"M00PRG.BIN", 0x0002fd4c, 0x03,
		"M01PRG.BIN", 0x00028dec, 0x03,
		"M02PRG.BIN", 0x0002d58c, 0x03,
		"M03PRG.BIN", 0x0002e570, 0x03,
		"M04PRG.BIN", 0x0002ed20, 0x03,
		"M05PRG.BIN", 0x0002d894, 0x03,
		"M26PRG.BIN", 0x0002f698, 0x03,
		"M27PRG.BIN", 0x0002d448, 0x03,
		"M29PRG.BIN", 0x00027284, 0x03,
		"M31PRG.BIN", 0x0002d6cc, 0x03,
		"M33PRG.BIN", 0x0002e23c, 0x03,
		"M34PRG.BIN", 0x0002d318, 0x03,
		"SLGGOVER.BIN", 0x0001bcbc, 0x03,

		/*Making space for a bigger text buffer
		06034760 (3 instances need to be changed to 06033f50 in 0.slg)
		- at 0600739c, 06007400, 060074e0[@0x0000239c, 0x2400, 0x24e0]
		0600735a need to change 0x3f to 6f(edited)[0.SLG @0x0000235a]*/
		"0.SLG", 0x0000239e, 0x3f50,
		"0.SLG", 0x00002402, 0x3f50,
		"0.SLG", 0x000024e2, 0x3f50,
		"0.SLG", 0x0000235a, 0x6f,

		/*Lips spacing
		060d1503 is X spacing.Set this to 0x08
		060d146c is calculation to center text.Set this to 4200 (shll r2)
		060d16bf is another horizontal offset.Set this to f8
		060d14ef is vertical offset.Change to 0x93*/
		"EV00001.BIN", 0x000071f0, 0xffff,
		"EV00002.BIN", 0x00005af4, 0xffff,
		"EV00050.BIN", 0x00005af4, 0xffff,
		"EV00051.BIN", 0x00005af4, 0xffff,
		"EV00052.BIN", 0x00005af4, 0xffff,
		"EV00054.BIN", 0x00005af4, 0xffff,
		"EV00055.BIN", 0x00005af4, 0xffff,
		"EV00060.BIN", 0x00005af4, 0xffff,
		"EV01001.BIN", 0x00005c00, 0xffff,
		"EV01002.BIN", 0x00005af4, 0xffff,
		"EV01020.BIN", 0x00006118, 0xffff,
		"EV01021.BIN", 0x00005f2c, 0xffff,
		"EV01022.BIN", 0x00005af4, 0xffff,
		"EV01023.BIN", 0x00005af4, 0xffff,
		"EV01030.BIN", 0x00005af4, 0xffff,
		"EV01050.BIN", 0x00005af4, 0xffff,
		"EV01054.BIN", 0x00005af4, 0xffff,
		"EV01055.BIN", 0x00005af4, 0xffff,
		"EV02001.BIN", 0x00005af4, 0xffff,
		"EV02002.BIN", 0x00005af4, 0xffff,
		"EV02010.BIN", 0x00005af4, 0xffff,
		"EV02021.BIN", 0x00005af4, 0xffff,
		"EV02025.BIN", 0x00005af4, 0xffff,
		"EV02050.BIN", 0x00005af4, 0xffff,
		"EV02051.BIN", 0x00005af4, 0xffff,
		"EV02052.BIN", 0x00005af4, 0xffff,
		"EV02053.BIN", 0x00005af4, 0xffff,
		"EV02054.BIN", 0x00005af4, 0xffff,
		"EV03001.BIN", 0x00005cc0, 0xffff,
		"EV03002.BIN", 0x00005b68, 0xffff,
		"EV03005.BIN", 0x00005b20, 0xffff,
		"EV03010.BIN", 0x00005b04, 0xffff,
		"EV03020.BIN", 0x00005b04, 0xffff,
		"EV03021.BIN", 0x00005b04, 0xffff,
		"EV03023.BIN", 0x00005b1c, 0xffff,
		"EV03024.BIN", 0x00005b1c, 0xffff,
		"EV03025.BIN", 0x00005b30, 0xffff,
		"EV03050.BIN", 0x00005f34, 0xffff,
		"EV03051.BIN", 0x00005b58, 0xffff,
		"EV03052.BIN", 0x00005bc4, 0xffff,
		"EV03053.BIN", 0x00005b80, 0xffff,
		"EV04001.BIN", 0x000069ac, 0xffff,
		"EV04002.BIN", 0x00005b70, 0xffff,
		"EV04003.BIN", 0x00005ca4, 0xffff,
		"EV04005.BIN", 0x00005af4, 0xffff,
		"EV04010.BIN", 0x00005b68, 0xffff,
		"EV04020.BIN", 0x00005af4, 0xffff,
		"EV04022.BIN", 0x00005afc, 0xffff,
		"EV04050.BIN", 0x00005b9c, 0xffff,
		"EV04053.BIN", 0x00005af4, 0xffff,
		"EV04055.BIN", 0x00005b68, 0xffff,
		"EV05001.BIN", 0x00005b0c, 0xffff,
		"EV05002.BIN", 0x00005af4, 0xffff,
		"EV05003.BIN", 0x00005cd0, 0xffff,
		"EV05004.BIN", 0x00005af4, 0xffff,
		"EV05005.BIN", 0x00005af4, 0xffff,
		"EV05007.BIN", 0x00005af4, 0xffff,
		"EV05010.BIN", 0x00005af4, 0xffff,
		"EV05011.BIN", 0x00005af4, 0xffff,
		"EV05018.BIN", 0x00005af4, 0xffff,
		"EV05019.BIN", 0x00005b1c, 0xffff,
		"EV05020.BIN", 0x00005af4, 0xffff,
		"EV05021.BIN", 0x00005af4, 0xffff,
		"EV05022.BIN", 0x00005af4, 0xffff,
		"EV05023.BIN", 0x00005af4, 0xffff,
		"EV05025.BIN", 0x00005af4, 0xffff,
		"EV05026.BIN", 0x00005af4, 0xffff,
		"EV05027.BIN", 0x00005af4, 0xffff,
		"EV05051.BIN", 0x00005af4, 0xffff,
		"EV05052.BIN", 0x00005af4, 0xffff,
		"EV05053.BIN", 0x00005af4, 0xffff,
		"EV05054.BIN", 0x00005af4, 0xffff,
		"EV05060.BIN", 0x00005af4, 0xffff,
		"EV26001.BIN", 0x00006900, 0xffff,
		"EV26002.BIN", 0x00005e9c, 0xffff,
		"EV26020.BIN", 0x00005af4, 0xffff,
		"EV26021.BIN", 0x00005af4, 0xffff,
		"EV27001.BIN", 0x00005b00, 0xffff,
		"EV27002.BIN", 0x00005ba8, 0xffff,
		"EV27054.BIN", 0x00005af4, 0xffff,
		"EV28001.BIN", 0x00005af4, 0xffff,
		"EV28025.BIN", 0x00005af4, 0xffff,
		"EV28050.BIN", 0x00005af4, 0xffff,
		"EV28051.BIN", 0x00005af4, 0xffff,
		"EV29001.BIN", 0x00005b18, 0xffff,
		"EV29002.BIN", 0x00005b18, 0xffff,
		"EV29021.BIN", 0x00005af4, 0xffff,
		"EV29050.BIN", 0x00005af4, 0xffff,
		"EV30003.BIN", 0x00006b3c, 0xffff,
		"EV30020.BIN", 0x00005b04, 0xffff,
		"EV30026.BIN", 0x00005b04, 0xffff,
		"EV30027.BIN", 0x00005b04, 0xffff,
		"EV31001.BIN", 0x00005d9c, 0xffff,
		"EV31002.BIN", 0x00005cf4, 0xffff,
		"EV31023.BIN", 0x00005b1c, 0xffff,
		"EV31024.BIN", 0x00005b1c, 0xffff,
		"EV32002.BIN", 0x00005b28, 0xffff,
		"EV32020.BIN", 0x00005af4, 0xffff,
		"EV32055.BIN", 0x00005b68, 0xffff,
		"EV33001.BIN", 0x00006048, 0xffff,
		"EV33002.BIN", 0x00005db0, 0xffff,
		"EV33022.BIN", 0x00005afc, 0xffff,
		"EV33054.BIN", 0x00005af4, 0xffff,
		"EV34001.BIN", 0x00005b18, 0xffff,
		"EV34002.BIN", 0x00005b30, 0xffff,
		"EV34003.BIN", 0x00005b18, 0xffff,
		"EV34004.BIN", 0x00005b70, 0xffff,
		"EV34005.BIN", 0x00005b70, 0xffff,
		"EV34006.BIN", 0x00005ba8, 0xffff,
		"EV34007.BIN", 0x00005b18, 0xffff,
		"EV34008.BIN", 0x00005ba8, 0xffff,
		"EV34020.BIN", 0x00005af4, 0xffff,
		"EV34030.BIN", 0x00005af4, 0xffff,
		"EV34040.BIN", 0x00005af4, 0xffff,
		"EV34041.BIN", 0x00005af4, 0xffff,
		"EV34042.BIN", 0x00005af4, 0xffff,
		"EV34043.BIN", 0x00005af4, 0xffff,
		"EV34044.BIN", 0x00005af4, 0xffff,
		"M00PRG.BIN", 0x000795f0, 0xffff,
		"M26PRG.BIN", 0x00078d00, 0xffff,
		"TUTORI0.BIN", 0x00005c6c, 0xffff,
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

		//Lips Patching
		if(patchingInfo[i].data.command.word == 0xffff)
		{
			sakuraBin.WriteData(patchingInfo[i].data.address, lipsPatchData.GetData(), lipsPatchData.GetDataSize(), false);
			continue;
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