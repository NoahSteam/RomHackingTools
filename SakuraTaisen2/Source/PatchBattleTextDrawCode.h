#pragma once

//Convert tile size form 16x16 to 8x12
/*
//Calc offset in vdp1 ram at 060d139c
mov 0x30, r1 //e130
muls.w r2,r1 //212f
sts macl,r4  //021a
nop          //0009

//Calc offset from prev letter, write address starting at 060d13da
mov 0x30, r4 //e430
muls.w r5,r4 //245f
sts macl,r5  //051a
mov r9,r4    //6493

//Calc offset from prev letter, write address satrting at 060d13e4
mov 0x30, r1 //e130
muls.w r4,r1 //214f
sts macl,r4  //041a
nop          //0009

0x010c //sprite dims stored at 060d153a
*/
#define PatchCharacterDimensionsForLips(filename, address) \
	filename, address+0,  0xe130,\
	filename, address+2,  0x212f,\
	filename, address+4,  0x021a,\
	filename, address+6,  0x0009,\
	filename, address+62, 0xe430,\
	filename, address+64, 0x245f,\
	filename, address+66, 0x051a,\
	filename, address+68, 0x6493,\
	filename, address+72, 0xe130,\
	filename, address+74, 0x214f,\
	filename, address+76, 0x041a,\
	filename, address+78, 0x0009,\
	filename, address+414,0x010c\

#define PatchCharacterDimensionsForLipsInFiles() \
	PatchCharacterDimensionsForLips("EV00001.BIN", 0x00007120),\
	PatchCharacterDimensionsForLips("EV00002.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV00050.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV00051.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV00052.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV00054.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV00055.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV00060.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV01001.BIN", 0x00005b30),\
	PatchCharacterDimensionsForLips("EV01002.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV01020.BIN", 0x00006048),\
	PatchCharacterDimensionsForLips("EV01021.BIN", 0x00005e5c),\
	PatchCharacterDimensionsForLips("EV01022.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV01023.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV01030.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV01050.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV01054.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV01055.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV02001.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV02002.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV02010.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV02021.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV02025.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV02050.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV02051.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV02052.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV02053.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV02054.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV03000.BIN", 0x000056f8),\
	PatchCharacterDimensionsForLips("EV03001.BIN", 0x00005bf0),\
	PatchCharacterDimensionsForLips("EV03002.BIN", 0x00005a98),\
	PatchCharacterDimensionsForLips("EV03005.BIN", 0x00005a50),\
	PatchCharacterDimensionsForLips("EV03010.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV03020.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV03021.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV03023.BIN", 0x00005a4c),\
	PatchCharacterDimensionsForLips("EV03024.BIN", 0x00005a4c),\
	PatchCharacterDimensionsForLips("EV03025.BIN", 0x00005a60),\
	PatchCharacterDimensionsForLips("EV03050.BIN", 0x00005e64),\
	PatchCharacterDimensionsForLips("EV03051.BIN", 0x00005a88),\
	PatchCharacterDimensionsForLips("EV03052.BIN", 0x00005af4),\
	PatchCharacterDimensionsForLips("EV03053.BIN", 0x00005ab0),\
	PatchCharacterDimensionsForLips("EV04001.BIN", 0x000068dc),\
	PatchCharacterDimensionsForLips("EV04002.BIN", 0x00005aa0),\
	PatchCharacterDimensionsForLips("EV04003.BIN", 0x00005bd4),\
	PatchCharacterDimensionsForLips("EV04005.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV04010.BIN", 0x00005a98),\
	PatchCharacterDimensionsForLips("EV04020.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV04022.BIN", 0x00005a2c),\
	PatchCharacterDimensionsForLips("EV04050.BIN", 0x00005acc),\
	PatchCharacterDimensionsForLips("EV04053.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV04055.BIN", 0x00005a98),\
	PatchCharacterDimensionsForLips("EV05001.BIN", 0x00005a3c),\
	PatchCharacterDimensionsForLips("EV05002.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05003.BIN", 0x00005c00),\
	PatchCharacterDimensionsForLips("EV05004.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05005.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05007.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05010.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05011.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05018.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05019.BIN", 0x00005a4c),\
	PatchCharacterDimensionsForLips("EV05020.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05021.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05022.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05023.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05025.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05026.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05027.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05051.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05052.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05053.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05054.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV05060.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV26001.BIN", 0x00006830),\
	PatchCharacterDimensionsForLips("EV26002.BIN", 0x00005dcc),\
	PatchCharacterDimensionsForLips("EV26020.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV26021.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV27001.BIN", 0x00005a30),\
	PatchCharacterDimensionsForLips("EV27002.BIN", 0x00005ad8),\
	PatchCharacterDimensionsForLips("EV27054.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV28001.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV28025.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV28050.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV28051.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV29001.BIN", 0x00005a48),\
	PatchCharacterDimensionsForLips("EV29002.BIN", 0x00005a48),\
	PatchCharacterDimensionsForLips("EV29021.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV29050.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV30003.BIN", 0x00006a6c),\
	PatchCharacterDimensionsForLips("EV30020.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV30026.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV30027.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV31001.BIN", 0x00005ccc),\
	PatchCharacterDimensionsForLips("EV31002.BIN", 0x00005c24),\
	PatchCharacterDimensionsForLips("EV31023.BIN", 0x00005a4c),\
	PatchCharacterDimensionsForLips("EV31024.BIN", 0x00005a4c),\
	PatchCharacterDimensionsForLips("EV32002.BIN", 0x00005a58),\
	PatchCharacterDimensionsForLips("EV32020.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV32055.BIN", 0x00005a98),\
	PatchCharacterDimensionsForLips("EV33001.BIN", 0x00005f78),\
	PatchCharacterDimensionsForLips("EV33002.BIN", 0x00005ce0),\
	PatchCharacterDimensionsForLips("EV33022.BIN", 0x00005a2c),\
	PatchCharacterDimensionsForLips("EV33054.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV34001.BIN", 0x00005a48),\
	PatchCharacterDimensionsForLips("EV34002.BIN", 0x00005a60),\
	PatchCharacterDimensionsForLips("EV34003.BIN", 0x00005a48),\
	PatchCharacterDimensionsForLips("EV34004.BIN", 0x00005aa0),\
	PatchCharacterDimensionsForLips("EV34005.BIN", 0x00005aa0),\
	PatchCharacterDimensionsForLips("EV34006.BIN", 0x00005ad8),\
	PatchCharacterDimensionsForLips("EV34007.BIN", 0x00005a48),\
	PatchCharacterDimensionsForLips("EV34008.BIN", 0x00005ad8),\
	PatchCharacterDimensionsForLips("EV34020.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV34030.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV34040.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV34041.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV34042.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV34043.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV34044.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("M00PRG.BIN", 0x00079520),\
	PatchCharacterDimensionsForLips("M26PRG.BIN", 0x00078c30),\
	PatchCharacterDimensionsForLips("TUTORI0.BIN", 0x00005b9c)

//Convert tile size form 16x16 to 8x12
/*
add 0x10,r1 //7106  controls spacing in vdp1 ram  060893d0
0x0210      //010c  image dimensions  0608942c
mov 0x40,r6 //e618  bytes to copy from from file for each character 06089458

//Calc offset from prev letter, write address starting at 0608945e
//Multiply by character index by 0x30 and store into r5
mov 0x30, r1 //e130
muls.w r5,r1 //215f
sts macl,r5  //051a
nop          //0009

//Multiply character index by 0x30 and store into r4 (0608946a)
mov 0x30, r1 //e130
muls.w r4,r1 //214f
sts macl,r4  //041a
nop          //0009
*/
#define PatchCharacterDimensions(filename, address) \
	filename, address,     0x7106,\
	filename, address+92,  0x010c,\
	filename, address+136, 0xe618,\
	filename, address+142, 0xe130,\
	filename, address+144, 0x215f,\
	filename, address+146, 0x051a,\
	filename, address+148, 0x0009,\
	filename, address+154, 0xe130,\
	filename, address+156, 0x214f,\
	filename, address+158, 0x041a,\
	filename, address+160, 0x0009

#define PatchCharacterDimensionsForFiles() \
	PatchCharacterDimensions("M00PRG.BIN",   0x0002ffd0),\
	PatchCharacterDimensions("M01PRG.BIN",   0x00029070),\
	PatchCharacterDimensions("M02PRG.BIN",   0x0002d810),\
	PatchCharacterDimensions("M03PRG.BIN",   0x0002e7f4),\
	PatchCharacterDimensions("M04PRG.BIN",   0x0002efa4),\
	PatchCharacterDimensions("M05PRG.BIN",   0x0002db18),\
	PatchCharacterDimensions("M26PRG.BIN",   0x0002f91c),\
	PatchCharacterDimensions("M27PRG.BIN",   0x0002d6cc),\
	PatchCharacterDimensions("M29PRG.BIN",   0x00027508),\
	PatchCharacterDimensions("M31PRG.BIN",   0x0002d950),\
	PatchCharacterDimensions("M33PRG.BIN",   0x0002e4c0),\
	PatchCharacterDimensions("M34PRG.BIN",   0x0002d59c),\
	PatchCharacterDimensions("SLGGOVER.BIN", 0x0001bf40)

//PatchLipsSingleByteForFile
/*
	060d145e add r0, r0           //0009
	060d1460 mov.w @(r0, r4), r1  //014c
	060d1498 mov.w @r11, r1       //61b0
	060d14aa add r0, r0           //0009
	060d14ac mov.w @(r0, r11), r4 //04bc
	060d150e add r0, r0           //0009
	060d1510 mov.w @(r0, r11), r1 //01bc
*/
#define PatchLipsSingleByteForFile(filename, address) \
	filename, address,     0x009,\
	filename, address+2,   0x014c,\
	filename, address+58,  0x61b0,\
	filename, address+76,  0x009,\
	filename, address+78,  0x04bc,\
	filename, address+176, 0x009,\
	filename, address+178, 0x01bc

#define LipsSingleBytePatching() \
	PatchLipsSingleByteForFile("EV00001.BIN", 0x000071e2),\
	PatchLipsSingleByteForFile("EV00002.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV00050.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV00051.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV00052.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV00054.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV00055.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV00060.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV01001.BIN", 0x00005bf2),\
	PatchLipsSingleByteForFile("EV01002.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV01020.BIN", 0x0000610a),\
	PatchLipsSingleByteForFile("EV01021.BIN", 0x00005f1e),\
	PatchLipsSingleByteForFile("EV01022.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV01023.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV01030.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV01050.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV01054.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV01055.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV02001.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV02002.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV02010.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV02021.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV02025.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV02050.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV02051.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV02052.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV02053.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV02054.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV03001.BIN", 0x00005cb2),\
	PatchLipsSingleByteForFile("EV03002.BIN", 0x00005b5a),\
	PatchLipsSingleByteForFile("EV03005.BIN", 0x00005b12),\
	PatchLipsSingleByteForFile("EV03010.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV03020.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV03021.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV03023.BIN", 0x00005b0e),\
	PatchLipsSingleByteForFile("EV03024.BIN", 0x00005b0e),\
	PatchLipsSingleByteForFile("EV03025.BIN", 0x00005b22),\
	PatchLipsSingleByteForFile("EV03050.BIN", 0x00005f26),\
	PatchLipsSingleByteForFile("EV03051.BIN", 0x00005b4a),\
	PatchLipsSingleByteForFile("EV03052.BIN", 0x00005bb6),\
	PatchLipsSingleByteForFile("EV03053.BIN", 0x00005b72),\
	PatchLipsSingleByteForFile("EV04001.BIN", 0x0000699e),\
	PatchLipsSingleByteForFile("EV04002.BIN", 0x00005b62),\
	PatchLipsSingleByteForFile("EV04003.BIN", 0x00005c96),\
	PatchLipsSingleByteForFile("EV04005.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV04010.BIN", 0x00005b5a),\
	PatchLipsSingleByteForFile("EV04020.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV04022.BIN", 0x00005aee),\
	PatchLipsSingleByteForFile("EV04050.BIN", 0x00005b8e),\
	PatchLipsSingleByteForFile("EV04053.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV04055.BIN", 0x00005b5a),\
	PatchLipsSingleByteForFile("EV05001.BIN", 0x00005afe),\
	PatchLipsSingleByteForFile("EV05002.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05003.BIN", 0x00005cc2),\
	PatchLipsSingleByteForFile("EV05004.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05005.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05007.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05010.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05011.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05018.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05019.BIN", 0x00005b0e),\
	PatchLipsSingleByteForFile("EV05020.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05021.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05022.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05023.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05025.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05026.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05027.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05051.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05052.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05053.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05054.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV05060.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV26001.BIN", 0x000068f2),\
	PatchLipsSingleByteForFile("EV26002.BIN", 0x00005e8e),\
	PatchLipsSingleByteForFile("EV26020.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV26021.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV27001.BIN", 0x00005af2),\
	PatchLipsSingleByteForFile("EV27002.BIN", 0x00005b9a),\
	PatchLipsSingleByteForFile("EV27054.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV28001.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV28025.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV28050.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV28051.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV29001.BIN", 0x00005b0a),\
	PatchLipsSingleByteForFile("EV29002.BIN", 0x00005b0a),\
	PatchLipsSingleByteForFile("EV29021.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV29050.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV30003.BIN", 0x00006b2e),\
	PatchLipsSingleByteForFile("EV30020.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV30026.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV30027.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV31001.BIN", 0x00005d8e),\
	PatchLipsSingleByteForFile("EV31002.BIN", 0x00005ce6),\
	PatchLipsSingleByteForFile("EV31023.BIN", 0x00005b0e),\
	PatchLipsSingleByteForFile("EV31024.BIN", 0x00005b0e),\
	PatchLipsSingleByteForFile("EV32002.BIN", 0x00005b1a),\
	PatchLipsSingleByteForFile("EV32020.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV32055.BIN", 0x00005b5a),\
	PatchLipsSingleByteForFile("EV33001.BIN", 0x0000603a),\
	PatchLipsSingleByteForFile("EV33002.BIN", 0x00005da2),\
	PatchLipsSingleByteForFile("EV33022.BIN", 0x00005aee),\
	PatchLipsSingleByteForFile("EV33054.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV34001.BIN", 0x00005b0a),\
	PatchLipsSingleByteForFile("EV34002.BIN", 0x00005b22),\
	PatchLipsSingleByteForFile("EV34003.BIN", 0x00005b0a),\
	PatchLipsSingleByteForFile("EV34004.BIN", 0x00005b62),\
	PatchLipsSingleByteForFile("EV34005.BIN", 0x00005b62),\
	PatchLipsSingleByteForFile("EV34006.BIN", 0x00005b9a),\
	PatchLipsSingleByteForFile("EV34007.BIN", 0x00005b0a),\
	PatchLipsSingleByteForFile("EV34008.BIN", 0x00005b9a),\
	PatchLipsSingleByteForFile("EV34020.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV34030.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV34040.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV34041.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV34042.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV34043.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV34044.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("M00PRG.BIN",  0x000795e2),\
	PatchLipsSingleByteForFile("M26PRG.BIN",  0x00078cf2),\
	PatchLipsSingleByteForFile("TUTORI0.BIN", 0x00005c5e)

/*
//Memory from second battle	
	060d212c mov.w @r11, r1       //61b0
*/
#define PatchLipsSingleByteForFile2(filename, address) \
	filename, address,     0x61b0

#define LipsSingleBytePatching2() \
	PatchLipsSingleByteForFile2("EV00001.BIN", 0x0000721c),\
	PatchLipsSingleByteForFile2("EV00002.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV00050.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV00051.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV00052.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV00054.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV00055.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV00060.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV01001.BIN", 0x00005c2c),\
	PatchLipsSingleByteForFile2("EV01002.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV01020.BIN", 0x00006144),\
	PatchLipsSingleByteForFile2("EV01021.BIN", 0x00005f58),\
	PatchLipsSingleByteForFile2("EV01022.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV01023.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV01030.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV01050.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV01054.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV01055.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV02001.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV02002.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV02010.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV02021.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV02025.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV02050.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV02051.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV02052.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV02053.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV02054.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV03000.BIN", 0x000057f4),\
	PatchLipsSingleByteForFile2("EV03001.BIN", 0x00005cec),\
	PatchLipsSingleByteForFile2("EV03002.BIN", 0x00005b94),\
	PatchLipsSingleByteForFile2("EV03005.BIN", 0x00005b4c),\
	PatchLipsSingleByteForFile2("EV03010.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV03020.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV03021.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV03023.BIN", 0x00005b48),\
	PatchLipsSingleByteForFile2("EV03024.BIN", 0x00005b48),\
	PatchLipsSingleByteForFile2("EV03025.BIN", 0x00005b5c),\
	PatchLipsSingleByteForFile2("EV03050.BIN", 0x00005f60),\
	PatchLipsSingleByteForFile2("EV03051.BIN", 0x00005b84),\
	PatchLipsSingleByteForFile2("EV03052.BIN", 0x00005bf0),\
	PatchLipsSingleByteForFile2("EV03053.BIN", 0x00005bac),\
	PatchLipsSingleByteForFile2("EV04001.BIN", 0x000069d8),\
	PatchLipsSingleByteForFile2("EV04002.BIN", 0x00005b9c),\
	PatchLipsSingleByteForFile2("EV04003.BIN", 0x00005cd0),\
	PatchLipsSingleByteForFile2("EV04005.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV04010.BIN", 0x00005b94),\
	PatchLipsSingleByteForFile2("EV04020.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV04022.BIN", 0x00005b28),\
	PatchLipsSingleByteForFile2("EV04050.BIN", 0x00005bc8),\
	PatchLipsSingleByteForFile2("EV04053.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV04055.BIN", 0x00005b94),\
	PatchLipsSingleByteForFile2("EV05001.BIN", 0x00005b38),\
	PatchLipsSingleByteForFile2("EV05002.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05003.BIN", 0x00005cfc),\
	PatchLipsSingleByteForFile2("EV05004.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05005.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05007.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05010.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05011.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05018.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05019.BIN", 0x00005b48),\
	PatchLipsSingleByteForFile2("EV05020.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05021.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05022.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05023.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05025.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05026.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05027.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05051.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05052.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05053.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05054.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV05060.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV26001.BIN", 0x0000692c),\
	PatchLipsSingleByteForFile2("EV26002.BIN", 0x00005ec8),\
	PatchLipsSingleByteForFile2("EV26020.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV26021.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV27001.BIN", 0x00005b2c),\
	PatchLipsSingleByteForFile2("EV27002.BIN", 0x00005bd4),\
	PatchLipsSingleByteForFile2("EV27054.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV28001.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV28025.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV28050.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV28051.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV29001.BIN", 0x00005b44),\
	PatchLipsSingleByteForFile2("EV29002.BIN", 0x00005b44),\
	PatchLipsSingleByteForFile2("EV29021.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV29050.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV30003.BIN", 0x00006b68),\
	PatchLipsSingleByteForFile2("EV30020.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV30026.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV30027.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV31001.BIN", 0x00005dc8),\
	PatchLipsSingleByteForFile2("EV31002.BIN", 0x00005d20),\
	PatchLipsSingleByteForFile2("EV31023.BIN", 0x00005b48),\
	PatchLipsSingleByteForFile2("EV31024.BIN", 0x00005b48),\
	PatchLipsSingleByteForFile2("EV32002.BIN", 0x00005b54),\
	PatchLipsSingleByteForFile2("EV32020.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV32055.BIN", 0x00005b94),\
	PatchLipsSingleByteForFile2("EV33001.BIN", 0x00006074),\
	PatchLipsSingleByteForFile2("EV33002.BIN", 0x00005ddc),\
	PatchLipsSingleByteForFile2("EV33022.BIN", 0x00005b28),\
	PatchLipsSingleByteForFile2("EV33054.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV34001.BIN", 0x00005b44),\
	PatchLipsSingleByteForFile2("EV34002.BIN", 0x00005b5c),\
	PatchLipsSingleByteForFile2("EV34003.BIN", 0x00005b44),\
	PatchLipsSingleByteForFile2("EV34004.BIN", 0x00005b9c),\
	PatchLipsSingleByteForFile2("EV34005.BIN", 0x00005b9c),\
	PatchLipsSingleByteForFile2("EV34006.BIN", 0x00005bd4),\
	PatchLipsSingleByteForFile2("EV34007.BIN", 0x00005b44),\
	PatchLipsSingleByteForFile2("EV34008.BIN", 0x00005bd4),\
	PatchLipsSingleByteForFile2("EV34020.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV34030.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV34040.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV34041.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV34042.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV34043.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV34044.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("M00PRG.BIN",  0x0007961c),\
	PatchLipsSingleByteForFile2("M26PRG.BIN",  0x00078d2c),\
	PatchLipsSingleByteForFile2("TUTORI0.BIN", 0x00005c98)

//Single Byte Fast Forward
//060890e6 add r0, r0           //0x011c move.w @(r0, r1), r1
//060890e8 mov.w @(r0, r1), r1  //0x611c extu.b r1,r1
#define SingleByteFastForward(filename, address) \
	filename, address+0, 0x011c,\
	filename, address+2, 0x611c

#define SingleByteFastForwardInFiles() \
	SingleByteFastForward("M00PRG.BIN", 0x0002fbe0), \
	SingleByteFastForward("M00PRG.BIN", 0x0002fce6), \
	SingleByteFastForward("M00PRG.BIN", 0x0002fe1e), \
	SingleByteFastForward("M01PRG.BIN", 0x00028c80), \
	SingleByteFastForward("M01PRG.BIN", 0x00028d86), \
	SingleByteFastForward("M01PRG.BIN", 0x00028ebe), \
	SingleByteFastForward("M02PRG.BIN", 0x0002d420), \
	SingleByteFastForward("M02PRG.BIN", 0x0002d526), \
	SingleByteFastForward("M02PRG.BIN", 0x0002d65e), \
	SingleByteFastForward("M03PRG.BIN", 0x0002e404), \
	SingleByteFastForward("M03PRG.BIN", 0x0002e50a), \
	SingleByteFastForward("M03PRG.BIN", 0x0002e642), \
	SingleByteFastForward("M04PRG.BIN", 0x0002ebb4), \
	SingleByteFastForward("M04PRG.BIN", 0x0002ecba), \
	SingleByteFastForward("M04PRG.BIN", 0x0002edf2), \
	SingleByteFastForward("M05PRG.BIN", 0x0002d728), \
	SingleByteFastForward("M05PRG.BIN", 0x0002d82e), \
	SingleByteFastForward("M05PRG.BIN", 0x0002d966), \
	SingleByteFastForward("M26PRG.BIN", 0x0002f52c), \
	SingleByteFastForward("M26PRG.BIN", 0x0002f632), \
	SingleByteFastForward("M26PRG.BIN", 0x0002f76a), \
	SingleByteFastForward("M27PRG.BIN", 0x0002d2dc), \
	SingleByteFastForward("M27PRG.BIN", 0x0002d3e2), \
	SingleByteFastForward("M27PRG.BIN", 0x0002d51a), \
	SingleByteFastForward("M29PRG.BIN", 0x00027118), \
	SingleByteFastForward("M29PRG.BIN", 0x0002721e), \
	SingleByteFastForward("M29PRG.BIN", 0x00027356), \
	SingleByteFastForward("M31PRG.BIN", 0x0002d560), \
	SingleByteFastForward("M31PRG.BIN", 0x0002d666), \
	SingleByteFastForward("M31PRG.BIN", 0x0002d79e), \
	SingleByteFastForward("M33PRG.BIN", 0x0002e0d0), \
	SingleByteFastForward("M33PRG.BIN", 0x0002e1d6), \
	SingleByteFastForward("M33PRG.BIN", 0x0002e30e), \
	SingleByteFastForward("M34PRG.BIN", 0x0002d1ac), \
	SingleByteFastForward("M34PRG.BIN", 0x0002d2b2), \
	SingleByteFastForward("M34PRG.BIN", 0x0002d3ea), \
	SingleByteFastForward("SLGGOVER.BIN", 0x0001bb50),\
	SingleByteFastForward("SLGGOVER.BIN", 0x0001bc56),\
	SingleByteFastForward("SLGGOVER.BIN", 0x0001bd8e)

/*
	//Memory from battle 1 part 2 (When M26PRG.BIN is loaded)
	06088a4a extu.w r1, r2 //621C -> needs to be extu.b
	06088ae2 extu.w r6, r6 //666c -> needs to be extu.b
	fffe stored at 06088b9c
	ffff stored at 06088ba0
*/
#define PatchSingleByteHiCharacterHandling(filename, address) \
	filename, address + 0,   0x621c,\
	filename, address + 152, 0x666c,\
	filename, address + 339, 0x00,\
	filename, address + 343, 0x00  
	

#define PatchSingleByteHiCharacterHandlingInFiles() \
	PatchSingleByteHiCharacterHandling("M00PRG.BIN", 0x0002fcfe),\
	PatchSingleByteHiCharacterHandling("M01PRG.BIN", 0x00028d9e),\
	PatchSingleByteHiCharacterHandling("M02PRG.BIN", 0x0002d53e),\
	PatchSingleByteHiCharacterHandling("M03PRG.BIN", 0x0002e522),\
	PatchSingleByteHiCharacterHandling("M04PRG.BIN", 0x0002ecd2),\
	PatchSingleByteHiCharacterHandling("M05PRG.BIN", 0x0002d846),\
	PatchSingleByteHiCharacterHandling("M26PRG.BIN", 0x0002f64a),\
	PatchSingleByteHiCharacterHandling("M27PRG.BIN", 0x0002d3fa),\
	PatchSingleByteHiCharacterHandling("M29PRG.BIN", 0x00027236),\
	PatchSingleByteHiCharacterHandling("M31PRG.BIN", 0x0002d67e),\
	PatchSingleByteHiCharacterHandling("M33PRG.BIN", 0x0002e1ee),\
	PatchSingleByteHiCharacterHandling("M34PRG.BIN", 0x0002d2ca),\
	PatchSingleByteHiCharacterHandling("SLGGOVER.BIN", 0x0001bc6e)

/*
//PatchSingleByteZeroExtend
(battle1_b) 06088944 extu.w r1, r2 //should be extu.b r1, r2 621c
*/
#define PatchSingleByteZeroExtend(filename, address)\
	filename, address, 0x621c

#define PatchSingleByteZeroExtendInFiles() \
	PatchSingleByteZeroExtend("M00PRG.BIN",   0x0002fbf8),\
	PatchSingleByteZeroExtend("M01PRG.BIN",   0x00028c98),\
	PatchSingleByteZeroExtend("M02PRG.BIN",   0x0002d438),\
	PatchSingleByteZeroExtend("M03PRG.BIN",   0x0002e41c),\
	PatchSingleByteZeroExtend("M04PRG.BIN",   0x0002ebcc),\
	PatchSingleByteZeroExtend("M05PRG.BIN",   0x0002d740),\
	PatchSingleByteZeroExtend("M26PRG.BIN",   0x0002f544),\
	PatchSingleByteZeroExtend("M27PRG.BIN",   0x0002d2f4),\
	PatchSingleByteZeroExtend("M29PRG.BIN",   0x00027130),\
	PatchSingleByteZeroExtend("M31PRG.BIN",   0x0002d578),\
	PatchSingleByteZeroExtend("M33PRG.BIN",   0x0002e0e8),\
	PatchSingleByteZeroExtend("M34PRG.BIN",   0x0002d1c4),\
	PatchSingleByteZeroExtend("SLGGOVER.BIN", 0x0001bb68)

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
		//Change this to 27*4 * (48+2)(bytes) cause 48 is 8x12/2
		"M00PRG.BIN", 0x000300b0, (27*4*50),
		"M01PRG.BIN", 0x00029150, (27*4*50),
		"M02PRG.BIN", 0x0002d8f0, (27*4*50),
		"M03PRG.BIN", 0x0002e8d4, (27*4*50),
		"M04PRG.BIN", 0x0002f084, (27*4*50),
		"M05PRG.BIN", 0x0002dbf8, (27*4*50),
		"M26PRG.BIN", 0x0002f9fc, (27*4*50),
		"M27PRG.BIN", 0x0002d7ac, (27*4*50),
		"M29PRG.BIN", 0x000275e8, (27*4*50),
		"M31PRG.BIN", 0x0002da30, (27*4*50),
		"M33PRG.BIN", 0x0002e5a0, (27*4*50),
		"M34PRG.BIN", 0x0002d67c, (27*4*50),
		"SLGGOVER.BIN", 0x0001c020, (27*4*50),

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
		060d146c is calculation to center text.Set this to 4200 (shll r2)
		060d14ef is vertical offset.Change to 0x93
		060d1503 is X spacing.Set this to 0x08
		060d16bf is another horizontal offset.Set this to f8
		*/
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

		PatchCharacterDimensionsForFiles(),
		PatchCharacterDimensionsForLipsInFiles(),

		#if USE_SINGLE_BYTE_LOOKUPS
			/*
			(battle1_b) 06088944 extu.w r1, r2 //should be extu.b r1, r2 621c
			*/
			PatchSingleByteZeroExtendInFiles(),

			/*
			//fast forward
			060890e6 add r0, r0           //0009
			060890e8 mov.w @(r0, r1), r1  //011c
			*/
			SingleByteFastForwardInFiles(),

			/*
			Text read at 0608921e
			0608921e mov.w @(r0, r1), r1  //011c
			06089220 add r0, r0           //611c extu.b r1, r1
			*/
			"M00PRG.BIN", 0x0002fe1e, 0x011c,
			"M00PRG.BIN", 0x0002fe1e+2, 0x611c,
			"M01PRG.BIN", 0x00028ebe, 0x011c,
			"M01PRG.BIN", 0x00028ebe + 2, 0x611c,
			"M02PRG.BIN", 0x0002d65e, 0x011c,
			"M02PRG.BIN", 0x0002d65e + 2, 0x611c,
			"M03PRG.BIN", 0x0002e642, 0x011c,
			"M03PRG.BIN", 0x0002e642 + 2, 0x611c,
			"M04PRG.BIN", 0x0002edf2, 0x011c,
			"M04PRG.BIN", 0x0002edf2 + 2, 0x611c,
			"M05PRG.BIN", 0x0002d966, 0x011c,
			"M05PRG.BIN", 0x0002d966 + 2, 0x611c,
			"M26PRG.BIN", 0x0002f76a, 0x011c,
			"M26PRG.BIN", 0x0002f76a + 2, 0x611c,
			"M27PRG.BIN", 0x0002d51a, 0x011c,
			"M27PRG.BIN", 0x0002d51a + 2, 0x611c,
			"M29PRG.BIN", 0x00027356, 0x011c,
			"M29PRG.BIN", 0x00027356 + 2, 0x611c,
			"M31PRG.BIN", 0x0002d79e, 0x011c,
			"M31PRG.BIN", 0x0002d79e + 2, 0x611c,
			"M33PRG.BIN", 0x0002e30e, 0x011c,
			"M33PRG.BIN", 0x0002e30e + 2, 0x611c,
			"M34PRG.BIN", 0x0002d3ea, 0x011c,
			"M34PRG.BIN", 0x0002d3ea + 2, 0x611c,
			"SLGGOVER.BIN", 0x0001bd8e, 0x011c,
			"SLGGOVER.BIN", 0x0001bd8e + 2, 0x611c,
		
			PatchSingleByteHiCharacterHandlingInFiles(),
			LipsSingleBytePatching(),
			LipsSingleBytePatching2(),
		#endif
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

		const bool bIsWord = (patchingInfo[i].data.command.word & 0xff00) != 0 || patchingInfo[i].data.command.byte == 0x09;
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