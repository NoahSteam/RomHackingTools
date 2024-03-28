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
	filename, address+0,  0xe130, 0x4208,\
	filename, address+2,  0x212f, 0x4208,\
	filename, address+4,  0x021a, 0x322c,\
	filename, address+6,  0x0009, 0x4208,\
	filename, address+62, 0xe430, 0x4508,\
	filename, address+64, 0x245f, 0x4508,\
	filename, address+66, 0x051a, 0x355c,\
	filename, address+68, 0x6493, 0x4508,\
	filename, address+72, 0xe130, 0x4408,\
	filename, address+74, 0x214f, 0x4408,\
	filename, address+76, 0x041a, 0x344c,\
	filename, address+78, 0x0009, 0x4408,\
	filename, address+414,0x010c, 0x0210\

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
	filename, address,     0x7106, 0x7110,\
	filename, address+92,  0x010c, 0x0210,\
	filename, address+136, 0xe618, 0xe640,\
	filename, address+142, 0xe130, 0x4508,\
	filename, address+144, 0x215f, 0x4508,\
	filename, address+146, 0x051a, 0x355c,\
	filename, address+148, 0x0009, 0x4508,\
	filename, address+154, 0xe130, 0x4408,\
	filename, address+156, 0x214f, 0x4408,\
	filename, address+158, 0x041a, 0x344c,\
	filename, address+160, 0x0009, 0x4408

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
	filename, address,     0x009, 0x300c,\
	filename, address+2,   0x014c, 0x014d,\
	filename, address+58,  0x61b0, 0x61b1,\
	filename, address+76,  0x009, 0x300c,\
	filename, address+78,  0x04bc, 0x04bd,\
	filename, address+176, 0x009, 0x300c,\
	filename, address+178, 0x01bc, 0x01bd

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
	filename, address,     0x61b0, 0x61b1

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
	filename, address+0, 0x011c, 0x300c,\
	filename, address+2, 0x611c, 0x011d

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
	filename, address + 0,   0x621c, 0x621d,\
	filename, address + 152, 0x666c, 0x666d,\
	filename, address + 339, 0x00, 0xff,\
	filename, address + 343, 0x00, 0xff
	

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
	filename, address, 0x621c, 0x621d

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


/* Patch Single Byte Reads
0608921e add r0, r0           //611c extu.b r1, r1
06089220 mov.w @(r0, r1), r1  //011c
*/
#define PatchSingleByteReading(filename, address)\
	filename, address, 0x011c, 0x011d,\
	filename, address + 2, 0x611c, 0x611d

#define PatchSingleByteReadingInFiles()\
	PatchSingleByteReading("M00PRG.BIN",  0x0002fe1e),\
	PatchSingleByteReading("M01PRG.BIN",  0x00028ebe),\
	PatchSingleByteReading("M02PRG.BIN",  0x0002d65e),\
	PatchSingleByteReading("M03PRG.BIN",  0x0002e642),\
	PatchSingleByteReading("M04PRG.BIN",  0x0002edf2),\
	PatchSingleByteReading("M05PRG.BIN",  0x0002d966),\
	PatchSingleByteReading("M26PRG.BIN",  0x0002f76a),\
	PatchSingleByteReading("M27PRG.BIN",  0x0002d51a),\
	PatchSingleByteReading("M29PRG.BIN",  0x00027356),\
	PatchSingleByteReading("M31PRG.BIN",  0x0002d79e),\
	PatchSingleByteReading("M33PRG.BIN",  0x0002e30e),\
	PatchSingleByteReading("M34PRG.BIN",  0x0002d3ea),\
	PatchSingleByteReading("SLGGOVER.BIN", 0x0001bd8e)

//0x060893b0 multiplying by 16 (height spacing) We need to multiply by 12
/*
	mov 0x0c, r11
	mulu.w, r11, r10
	sts macl. r11
*/
#define PatchHeightSpacing(filename, address)\
	filename, address - 4, 0xeb0c, 0x6ba3,\
	filename, address - 2, 0x2abe, 0x4b08,\
	filename, address,     0x0b1a, 0x4b08

#define PatchHeightSpacingInFiles()\
		PatchHeightSpacing("M00PRG.BIN",  0x0002ffb0),\
		PatchHeightSpacing("M01PRG.BIN",  0x00029050),\
		PatchHeightSpacing("M02PRG.BIN",  0x0002d7f0),\
		PatchHeightSpacing("M03PRG.BIN",  0x0002e7d4),\
		PatchHeightSpacing("M04PRG.BIN",  0x0002ef84),\
		PatchHeightSpacing("M05PRG.BIN",  0x0002daf8),\
		PatchHeightSpacing("M26PRG.BIN",  0x0002f8fc),\
		PatchHeightSpacing("M27PRG.BIN",  0x0002d6ac),\
		PatchHeightSpacing("M29PRG.BIN",  0x000274e8),\
		PatchHeightSpacing("M31PRG.BIN",  0x0002d930),\
		PatchHeightSpacing("M33PRG.BIN",  0x0002e4a0),\
		PatchHeightSpacing("M34PRG.BIN",  0x0002d57c),\
		PatchHeightSpacing("SLGGOVER.BIN", 0x0001bf20)

//0x060893e0 will multiply by 16 (width spacing) (make this 4100 shll1)
#define PatchWidthSpacing(filename, address)\
		filename, address, 0x00, 0x4108

#define PatchWidthSpacingInFiles()\
		PatchWidthSpacing("M00PRG.BIN", 0x0002ffe0), \
		PatchWidthSpacing("M01PRG.BIN", 0x00029080), \
		PatchWidthSpacing("M02PRG.BIN", 0x0002d820), \
		PatchWidthSpacing("M03PRG.BIN", 0x0002e804), \
		PatchWidthSpacing("M04PRG.BIN", 0x0002efb4), \
		PatchWidthSpacing("M05PRG.BIN", 0x0002db28), \
		PatchWidthSpacing("M26PRG.BIN", 0x0002f92c), \
		PatchWidthSpacing("M27PRG.BIN", 0x0002d6dc), \
		PatchWidthSpacing("M29PRG.BIN", 0x00027518), \
		PatchWidthSpacing("M31PRG.BIN", 0x0002d960), \
		PatchWidthSpacing("M33PRG.BIN", 0x0002e4d0), \
		PatchWidthSpacing("M34PRG.BIN", 0x0002d5ac), \
		PatchWidthSpacing("SLGGOVER.BIN", 0x0001bf50)

//Scaled Lips Formatting (example in Battle 5_4 LIPS 060d17e4)
#define PatchScaledLipsFormatting(filename, address)\
	filename, address + 0,  0x7194, 0x7198, /*7198 to 7194*/\
	filename, address + 42, 0x1a00, 0x1a01, /*Normal sprite instead of scaled sprite*/\
	filename, address + 48, 0x010c, 0x0210, /*Font size*/\
/*	filename, address + 50, 0xffb8, 0xff68, /*Horizontal framing - not needed for single byte encoding*/\
	filename, address + 156, 0x7108, 0x7110 /*Horizontal spacing*/

#define PatchScaledLipsFormattingInFiles()\
	PatchScaledLipsFormatting("EV00001.BIN", 0x000076e0),\
	PatchScaledLipsFormatting("EV00002.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV00050.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV00051.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV00052.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV00054.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV00055.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV00060.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV01001.BIN", 0x000060f0),\
	PatchScaledLipsFormatting("EV01002.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV01020.BIN", 0x00006608),\
	PatchScaledLipsFormatting("EV01021.BIN", 0x0000641c),\
	PatchScaledLipsFormatting("EV01022.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV01023.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV01030.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV01050.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV01054.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV01055.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV02001.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV02002.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV02010.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV02021.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV02025.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV02050.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV02051.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV02052.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV02053.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV02054.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV03000.BIN", 0x00005cb8),\
	PatchScaledLipsFormatting("EV03001.BIN", 0x000061b0),\
	PatchScaledLipsFormatting("EV03002.BIN", 0x00006058),\
	PatchScaledLipsFormatting("EV03005.BIN", 0x00006010),\
	PatchScaledLipsFormatting("EV03010.BIN", 0x00005ff4),\
	PatchScaledLipsFormatting("EV03020.BIN", 0x00005ff4),\
	PatchScaledLipsFormatting("EV03021.BIN", 0x00005ff4),\
	PatchScaledLipsFormatting("EV03023.BIN", 0x0000600c),\
	PatchScaledLipsFormatting("EV03024.BIN", 0x0000600c),\
	PatchScaledLipsFormatting("EV03025.BIN", 0x00006020),\
	PatchScaledLipsFormatting("EV03050.BIN", 0x00006424),\
	PatchScaledLipsFormatting("EV03051.BIN", 0x00006048),\
	PatchScaledLipsFormatting("EV03052.BIN", 0x000060b4),\
	PatchScaledLipsFormatting("EV03053.BIN", 0x00006070),\
	PatchScaledLipsFormatting("EV04001.BIN", 0x00006e9c),\
	PatchScaledLipsFormatting("EV04002.BIN", 0x00006060),\
	PatchScaledLipsFormatting("EV04003.BIN", 0x00006194),\
	PatchScaledLipsFormatting("EV04005.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV04010.BIN", 0x00006058),\
	PatchScaledLipsFormatting("EV04020.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV04022.BIN", 0x00005fec),\
	PatchScaledLipsFormatting("EV04050.BIN", 0x0000608c),\
	PatchScaledLipsFormatting("EV04053.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV04055.BIN", 0x00006058),\
	PatchScaledLipsFormatting("EV05001.BIN", 0x00005ffc),\
	PatchScaledLipsFormatting("EV05002.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05003.BIN", 0x000061c0),\
	PatchScaledLipsFormatting("EV05004.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05005.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05007.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05010.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05011.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05018.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05019.BIN", 0x0000600c),\
	PatchScaledLipsFormatting("EV05020.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05021.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05022.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05023.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05025.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05026.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05027.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05051.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05052.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05053.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05054.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV05060.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV26001.BIN", 0x00006df0),\
	PatchScaledLipsFormatting("EV26002.BIN", 0x0000638c),\
	PatchScaledLipsFormatting("EV26020.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV26021.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV27001.BIN", 0x00005ff0),\
	PatchScaledLipsFormatting("EV27002.BIN", 0x00006098),\
	PatchScaledLipsFormatting("EV27054.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV28001.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV28025.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV28050.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV28051.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV29001.BIN", 0x00006008),\
	PatchScaledLipsFormatting("EV29002.BIN", 0x00006008),\
	PatchScaledLipsFormatting("EV29021.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV29050.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV30003.BIN", 0x0000702c),\
	PatchScaledLipsFormatting("EV30020.BIN", 0x00005ff4),\
	PatchScaledLipsFormatting("EV30026.BIN", 0x00005ff4),\
	PatchScaledLipsFormatting("EV30027.BIN", 0x00005ff4),\
	PatchScaledLipsFormatting("EV31001.BIN", 0x0000628c),\
	PatchScaledLipsFormatting("EV31002.BIN", 0x000061e4),\
	PatchScaledLipsFormatting("EV31023.BIN", 0x0000600c),\
	PatchScaledLipsFormatting("EV31024.BIN", 0x0000600c),\
	PatchScaledLipsFormatting("EV32002.BIN", 0x00006018),\
	PatchScaledLipsFormatting("EV32020.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV32055.BIN", 0x00006058),\
	PatchScaledLipsFormatting("EV33001.BIN", 0x00006538),\
	PatchScaledLipsFormatting("EV33002.BIN", 0x000062a0),\
	PatchScaledLipsFormatting("EV33022.BIN", 0x00005fec),\
	PatchScaledLipsFormatting("EV33054.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV34001.BIN", 0x00006008),\
	PatchScaledLipsFormatting("EV34002.BIN", 0x00006020),\
	PatchScaledLipsFormatting("EV34003.BIN", 0x00006008),\
	PatchScaledLipsFormatting("EV34004.BIN", 0x00006060),\
	PatchScaledLipsFormatting("EV34005.BIN", 0x00006060),\
	PatchScaledLipsFormatting("EV34006.BIN", 0x00006098),\
	PatchScaledLipsFormatting("EV34007.BIN", 0x00006008),\
	PatchScaledLipsFormatting("EV34008.BIN", 0x00006098),\
	PatchScaledLipsFormatting("EV34020.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV34030.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV34040.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV34041.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV34042.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV34043.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("EV34044.BIN", 0x00005fe4),\
	PatchScaledLipsFormatting("M00PRG.BIN",  0x00079ae0),\
	PatchScaledLipsFormatting("M26PRG.BIN",  0x000791f0),\
	PatchScaledLipsFormatting("TUTORI0.BIN", 0x0000615c)

//Add extra space for scaled LIPS characters
/*change 060d180a from 01d4 to 3cc to make more space for buffer*/
/*change 060d195a from 01d0 to 3c8 to make more space for buffer*/
/*change 060d195c from 01d2 to 3ca to make more space for buffer*/
/*change 060d1a98 from 01d0 to 3c8 to make more space for buffer*/
/*change 060d1a9a from 01d2 to 3ca to make more space for buffer* (code at 060d18fa)*/
#define PatchScaledLipsSpriteBuffer(filename, address, offset1, offset2)\
	filename, address + 0,   0x3cc, 0x1d4,\
	filename, address + 336 + offset1, 0x3c8, 0x1d0,\
	filename, address + 338 + offset1, 0x3ca, 0x1d2,\
	filename, address + 654 + offset2, 0x3c8, 0x1d0,\
	filename, address + 656 + offset2, 0x3ca, 0x1d2

#define PatchScaledLipsSpriteBufferInFiles()\
	PatchScaledLipsSpriteBuffer("EV00001.BIN", 0x00007706, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV00002.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV00050.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV00051.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV00052.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV00054.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV00055.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV00060.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV01001.BIN", 0x00006116, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV01002.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV01020.BIN", 0x0000662e, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV01021.BIN", 0x00006442, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV01022.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV01023.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV01030.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV01050.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV01054.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV01055.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV02001.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV02002.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV02010.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV02021.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV02025.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV02050.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV02051.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV02052.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV02053.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV02054.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV03000.BIN", 0x00005cde, -2, -4),\
	PatchScaledLipsSpriteBuffer("EV03001.BIN", 0x000061d6, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV03002.BIN", 0x0000607e, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV03005.BIN", 0x00006036, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV03010.BIN", 0x0000601a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV03020.BIN", 0x0000601a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV03021.BIN", 0x0000601a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV03023.BIN", 0x00006032, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV03024.BIN", 0x00006032, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV03025.BIN", 0x00006046, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV03050.BIN", 0x0000644a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV03051.BIN", 0x0000606e, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV03052.BIN", 0x000060da, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV03053.BIN", 0x00006096, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV04001.BIN", 0x00006ec2, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV04002.BIN", 0x00006086, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV04003.BIN", 0x000061ba, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV04005.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV04010.BIN", 0x0000607e, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV04020.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV04022.BIN", 0x00006012, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV04050.BIN", 0x000060b2, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV04053.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV04055.BIN", 0x0000607e, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05001.BIN", 0x00006022, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05002.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05003.BIN", 0x000061e6, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05004.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05005.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05007.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05010.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05011.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05018.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05019.BIN", 0x00006032, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05020.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05021.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05022.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05023.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05025.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05026.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05027.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05051.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05052.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05053.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05054.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV05060.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV26001.BIN", 0x00006e16, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV26002.BIN", 0x000063b2, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV26020.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV26021.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV27001.BIN", 0x00006016, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV27002.BIN", 0x000060be, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV27054.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV28001.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV28025.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV28050.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV28051.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV29001.BIN", 0x0000602e, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV29002.BIN", 0x0000602e, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV29021.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV29050.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV30003.BIN", 0x00007052, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV30020.BIN", 0x0000601a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV30026.BIN", 0x0000601a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV30027.BIN", 0x0000601a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV31001.BIN", 0x000062b2, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV31002.BIN", 0x0000620a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV31023.BIN", 0x00006032, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV31024.BIN", 0x00006032, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV32002.BIN", 0x0000603e, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV32020.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV32055.BIN", 0x0000607e, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV33001.BIN", 0x0000655e, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV33002.BIN", 0x000062c6, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV33022.BIN", 0x00006012, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV33054.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34001.BIN", 0x0000602e, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34002.BIN", 0x00006046, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34003.BIN", 0x0000602e, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34004.BIN", 0x00006086, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34005.BIN", 0x00006086, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34006.BIN", 0x000060be, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34007.BIN", 0x0000602e, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34008.BIN", 0x000060be, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34020.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34030.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34040.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34041.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34042.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34043.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV34044.BIN", 0x0000600a, 0, 0),\
	PatchScaledLipsSpriteBuffer("M00PRG.BIN",  0x00079b06, 0, 0),\
	PatchScaledLipsSpriteBuffer("M26PRG.BIN",  0x00079216, 0, 0),\
	PatchScaledLipsSpriteBuffer("TUTORI0.BIN", 0x00006182, 0, 0)

//Scaled Lips tile stride should be 48 bytes
/*
060d1290
mov 0x30, r2 //e230
muls.w r1, r2 //221f
sts macl, r1 //011a
nop          //0009
*/
#define PatchScaledLipsTileStride(filename, address)\
	filename, address + 0, 0xe230, 0x4108,\
	filename, address + 2, 0x221f, 0x4108,\
	filename, address + 4, 0x011a, 0x311c,\
	filename, address + 6, 0x0009, 0x4108

#define PatchScaledLipsTileStrideInFiles()\
	PatchScaledLipsTileStride("EV00001.BIN", 0x0000718c),\
	PatchScaledLipsTileStride("EV00002.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV00050.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV00051.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV00052.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV00054.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV00055.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV00060.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV01001.BIN", 0x00005b9c),\
	PatchScaledLipsTileStride("EV01002.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV01020.BIN", 0x000060b4),\
	PatchScaledLipsTileStride("EV01021.BIN", 0x00005ec8),\
	PatchScaledLipsTileStride("EV01022.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV01023.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV01030.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV01050.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV01054.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV01055.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV02001.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV02002.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV02010.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV02021.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV02025.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV02050.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV02051.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV02052.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV02053.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV02054.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV03000.BIN", 0x00005764),\
	PatchScaledLipsTileStride("EV03001.BIN", 0x00005c5c),\
	PatchScaledLipsTileStride("EV03002.BIN", 0x00005b04),\
	PatchScaledLipsTileStride("EV03005.BIN", 0x00005abc),\
	PatchScaledLipsTileStride("EV03010.BIN", 0x00005aa0),\
	PatchScaledLipsTileStride("EV03020.BIN", 0x00005aa0),\
	PatchScaledLipsTileStride("EV03021.BIN", 0x00005aa0),\
	PatchScaledLipsTileStride("EV03023.BIN", 0x00005ab8),\
	PatchScaledLipsTileStride("EV03024.BIN", 0x00005ab8),\
	PatchScaledLipsTileStride("EV03025.BIN", 0x00005acc),\
	PatchScaledLipsTileStride("EV03050.BIN", 0x00005ed0),\
	PatchScaledLipsTileStride("EV03051.BIN", 0x00005af4),\
	PatchScaledLipsTileStride("EV03052.BIN", 0x00005b60),\
	PatchScaledLipsTileStride("EV03053.BIN", 0x00005b1c),\
	PatchScaledLipsTileStride("EV04001.BIN", 0x00006948),\
	PatchScaledLipsTileStride("EV04002.BIN", 0x00005b0c),\
	PatchScaledLipsTileStride("EV04003.BIN", 0x00005c40),\
	PatchScaledLipsTileStride("EV04005.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV04010.BIN", 0x00005b04),\
	PatchScaledLipsTileStride("EV04020.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV04022.BIN", 0x00005a98),\
	PatchScaledLipsTileStride("EV04050.BIN", 0x00005b38),\
	PatchScaledLipsTileStride("EV04053.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV04055.BIN", 0x00005b04),\
	PatchScaledLipsTileStride("EV05001.BIN", 0x00005aa8),\
	PatchScaledLipsTileStride("EV05002.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05003.BIN", 0x00005c6c),\
	PatchScaledLipsTileStride("EV05004.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05005.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05007.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05010.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05011.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05018.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05019.BIN", 0x00005ab8),\
	PatchScaledLipsTileStride("EV05020.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05021.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05022.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05023.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05025.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05026.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05027.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05051.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05052.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05053.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05054.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV05060.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV26001.BIN", 0x0000689c),\
	PatchScaledLipsTileStride("EV26002.BIN", 0x00005e38),\
	PatchScaledLipsTileStride("EV26020.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV26021.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV27001.BIN", 0x00005a9c),\
	PatchScaledLipsTileStride("EV27002.BIN", 0x00005b44),\
	PatchScaledLipsTileStride("EV27054.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV28001.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV28025.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV28050.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV28051.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV29001.BIN", 0x00005ab4),\
	PatchScaledLipsTileStride("EV29002.BIN", 0x00005ab4),\
	PatchScaledLipsTileStride("EV29021.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV29050.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV30003.BIN", 0x00006ad8),\
	PatchScaledLipsTileStride("EV30020.BIN", 0x00005aa0),\
	PatchScaledLipsTileStride("EV30026.BIN", 0x00005aa0),\
	PatchScaledLipsTileStride("EV30027.BIN", 0x00005aa0),\
	PatchScaledLipsTileStride("EV31001.BIN", 0x00005d38),\
	PatchScaledLipsTileStride("EV31002.BIN", 0x00005c90),\
	PatchScaledLipsTileStride("EV31023.BIN", 0x00005ab8),\
	PatchScaledLipsTileStride("EV31024.BIN", 0x00005ab8),\
	PatchScaledLipsTileStride("EV32002.BIN", 0x00005ac4),\
	PatchScaledLipsTileStride("EV32020.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV32055.BIN", 0x00005b04),\
	PatchScaledLipsTileStride("EV33001.BIN", 0x00005fe4),\
	PatchScaledLipsTileStride("EV33002.BIN", 0x00005d4c),\
	PatchScaledLipsTileStride("EV33022.BIN", 0x00005a98),\
	PatchScaledLipsTileStride("EV33054.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV34001.BIN", 0x00005ab4),\
	PatchScaledLipsTileStride("EV34002.BIN", 0x00005acc),\
	PatchScaledLipsTileStride("EV34003.BIN", 0x00005ab4),\
	PatchScaledLipsTileStride("EV34004.BIN", 0x00005b0c),\
	PatchScaledLipsTileStride("EV34005.BIN", 0x00005b0c),\
	PatchScaledLipsTileStride("EV34006.BIN", 0x00005b44),\
	PatchScaledLipsTileStride("EV34007.BIN", 0x00005ab4),\
	PatchScaledLipsTileStride("EV34008.BIN", 0x00005b44),\
	PatchScaledLipsTileStride("EV34020.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV34030.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV34040.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV34041.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV34042.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV34043.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("EV34044.BIN", 0x00005a90),\
	PatchScaledLipsTileStride("M00PRG.BIN",  0x0007958c),\
	PatchScaledLipsTileStride("M26PRG.BIN",  0x00078c9c),\
	PatchScaledLipsTileStride("TUTORI0.BIN", 0x00005c08)

//Patch Scaled LIPS Single Byte Reading
/*
	060d127d 0x40

	060d12e8
	mov.b (r0, r4), r1
	extu.w r1, r1 //611d to 611c

	060d1300 is ffff // change to ff

	060d15f4
	add r0, r0
	mov.w @(r0, r4), r1
	060d15fa extu.w r1, r1 //611d to 611c

	060d1604 *
	mov.w @(r0, r5), r1
	extu.w r1, r2 //621d to 621c

	060d160c
	mov.w @(r0, r4), r1
	extu.w r1, r1 //611d to 611c

	060d161e
	add r0, r0
	mov.w @(r0, r4), r1
	extu.w r1, r1

	060d1632 is ffff // change to ff

	060d1760 *
	mov.w @r13, r1

	060d1778 add r0, r0 //300c to 0009

	060d17aa*
	

	060d1886
	add r0, r0
	mov.w @(r0, r13), r1
	extu.w r1, r1 //611d to 611c //060d188c

	060d1960 is ffff // change to ff
*/
#define PatchScaledLipsSingleByteReading(filename, address)\
	filename, address + 0,    0xE630, 0xE640,/*num bytes to copy from tile to vdp1, technically not necessary*/\
	filename, address + 108,  0x014c, 0x014d,/*mov.w (r0, r4), r1*/\
	filename, address + 110,  0x611c, 0x611d,/*extu.w r1, r1*/\
	filename, address + 134,  0xff,   0xffff,/*060d1300 is ffff*/\
	filename, address + 888,  0x0009, 0x300c,/*060d15f4 add r0, r0*/\
	filename, address + 890,  0x014c, 0x014d,/*mov.w @(r0, r4), r1*/\
	filename, address + 894,  0x611c, 0x611d,/*extu.w r1, r1*/\
	filename, address + 904,  0x015c, 0x015d,/*060d1604 mov.w @(r0, r5), r1*/\
	filename, address + 906,  0x621c, 0x621d,/*extu.w r1,r2*/\
	filename, address + 912,  0x014c, 0x014d,/*060d160c mov.w @(r0, r4), r1*/\
	filename, address + 914,  0x611c, 0x611d,/*extu.w r1, r1*/\
	filename, address + 928,  0x0009, 0x300c,/*060d161c add r1, r1*/\
	filename, address + 930,  0x014c, 0x014d,/*mov.w @(r0, r4), r1*/\
	filename, address + 932,  0x611c, 0x611d,/*extu.w r1, r1*/\
	filename, address + 950,  0xff, 0xffff,/*060d1632 is ffff*/\
	filename, address + 1252, 0x61d0, 0x61d1,/*060d1760 mov.w @r13, r1*/\
	filename, address + 1276, 0x009, 0x300c,/*060d1778 add r0, r0*/\
	filename, address + 1326, 0x04dc, 0x04dd,/*060d17aa mov.w @(r0, r13), r1*/\
	filename, address + 1546, 0x0009, 0x300c,/*060d1886 add r0, r0*/\
	filename, address + 1548, 0x01dc, 0x01dd,/*mov.w @(r0, r13), r1*/\
	filename, address + 1552, 0x611c, 0x611d,/*060d188c extu.w r1, r1*/\
	filename, address + 1766, 0xff, 0xffff/*060d1962 is ffff*/

#define PatchScaledLipsSingleByteReadingInFiles()\
	PatchScaledLipsSingleByteReading("EV00001.BIN", 0x00007178),\
	PatchScaledLipsSingleByteReading("EV00002.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV00050.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV00051.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV00052.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV00054.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV00055.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV00060.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV01001.BIN", 0x00005b88),\
	PatchScaledLipsSingleByteReading("EV01002.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV01020.BIN", 0x000060a0),\
	PatchScaledLipsSingleByteReading("EV01021.BIN", 0x00005eb4),\
	PatchScaledLipsSingleByteReading("EV01022.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV01023.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV01030.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV01050.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV01054.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV01055.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV02001.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV02002.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV02010.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV02021.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV02025.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV02050.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV02051.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV02052.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV02053.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV02054.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV03000.BIN", 0x00005750),\
	PatchScaledLipsSingleByteReading("EV03001.BIN", 0x00005c48),\
	PatchScaledLipsSingleByteReading("EV03002.BIN", 0x00005af0),\
	PatchScaledLipsSingleByteReading("EV03005.BIN", 0x00005aa8),\
	PatchScaledLipsSingleByteReading("EV03010.BIN", 0x00005a8c),\
	PatchScaledLipsSingleByteReading("EV03020.BIN", 0x00005a8c),\
	PatchScaledLipsSingleByteReading("EV03021.BIN", 0x00005a8c),\
	PatchScaledLipsSingleByteReading("EV03023.BIN", 0x00005aa4),\
	PatchScaledLipsSingleByteReading("EV03024.BIN", 0x00005aa4),\
	PatchScaledLipsSingleByteReading("EV03025.BIN", 0x00005ab8),\
	PatchScaledLipsSingleByteReading("EV03050.BIN", 0x00005ebc),\
	PatchScaledLipsSingleByteReading("EV03051.BIN", 0x00005ae0),\
	PatchScaledLipsSingleByteReading("EV03052.BIN", 0x00005b4c),\
	PatchScaledLipsSingleByteReading("EV03053.BIN", 0x00005b08),\
	PatchScaledLipsSingleByteReading("EV04001.BIN", 0x00006934),\
	PatchScaledLipsSingleByteReading("EV04002.BIN", 0x00005af8),\
	PatchScaledLipsSingleByteReading("EV04003.BIN", 0x00005c2c),\
	PatchScaledLipsSingleByteReading("EV04005.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV04010.BIN", 0x00005af0),\
	PatchScaledLipsSingleByteReading("EV04020.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV04022.BIN", 0x00005a84),\
	PatchScaledLipsSingleByteReading("EV04050.BIN", 0x00005b24),\
	PatchScaledLipsSingleByteReading("EV04053.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV04055.BIN", 0x00005af0),\
	PatchScaledLipsSingleByteReading("EV05001.BIN", 0x00005a94),\
	PatchScaledLipsSingleByteReading("EV05002.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05003.BIN", 0x00005c58),\
	PatchScaledLipsSingleByteReading("EV05004.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05005.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05007.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05010.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05011.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05018.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05019.BIN", 0x00005aa4),\
	PatchScaledLipsSingleByteReading("EV05020.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05021.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05022.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05023.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05025.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05026.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05027.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05051.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05052.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05053.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05054.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV05060.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV26001.BIN", 0x00006888),\
	PatchScaledLipsSingleByteReading("EV26002.BIN", 0x00005e24),\
	PatchScaledLipsSingleByteReading("EV26020.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV26021.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV27001.BIN", 0x00005a88),\
	PatchScaledLipsSingleByteReading("EV27002.BIN", 0x00005b30),\
	PatchScaledLipsSingleByteReading("EV27054.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV28001.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV28025.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV28050.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV28051.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV29001.BIN", 0x00005aa0),\
	PatchScaledLipsSingleByteReading("EV29002.BIN", 0x00005aa0),\
	PatchScaledLipsSingleByteReading("EV29021.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV29050.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV30003.BIN", 0x00006ac4),\
	PatchScaledLipsSingleByteReading("EV30020.BIN", 0x00005a8c),\
	PatchScaledLipsSingleByteReading("EV30026.BIN", 0x00005a8c),\
	PatchScaledLipsSingleByteReading("EV30027.BIN", 0x00005a8c),\
	PatchScaledLipsSingleByteReading("EV31001.BIN", 0x00005d24),\
	PatchScaledLipsSingleByteReading("EV31002.BIN", 0x00005c7c),\
	PatchScaledLipsSingleByteReading("EV31023.BIN", 0x00005aa4),\
	PatchScaledLipsSingleByteReading("EV31024.BIN", 0x00005aa4),\
	PatchScaledLipsSingleByteReading("EV32002.BIN", 0x00005ab0),\
	PatchScaledLipsSingleByteReading("EV32020.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV32055.BIN", 0x00005af0),\
	PatchScaledLipsSingleByteReading("EV33001.BIN", 0x00005fd0),\
	PatchScaledLipsSingleByteReading("EV33002.BIN", 0x00005d38),\
	PatchScaledLipsSingleByteReading("EV33022.BIN", 0x00005a84),\
	PatchScaledLipsSingleByteReading("EV33054.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV34001.BIN", 0x00005aa0),\
	PatchScaledLipsSingleByteReading("EV34002.BIN", 0x00005ab8),\
	PatchScaledLipsSingleByteReading("EV34003.BIN", 0x00005aa0),\
	PatchScaledLipsSingleByteReading("EV34004.BIN", 0x00005af8),\
	PatchScaledLipsSingleByteReading("EV34005.BIN", 0x00005af8),\
	PatchScaledLipsSingleByteReading("EV34006.BIN", 0x00005b30),\
	PatchScaledLipsSingleByteReading("EV34007.BIN", 0x00005aa0),\
	PatchScaledLipsSingleByteReading("EV34008.BIN", 0x00005b30),\
	PatchScaledLipsSingleByteReading("EV34020.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV34030.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV34040.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV34041.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV34042.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV34043.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("EV34044.BIN", 0x00005a7c),\
	PatchScaledLipsSingleByteReading("M00PRG.BIN", 0x00079578),\
	PatchScaledLipsSingleByteReading("M26PRG.BIN", 0x00078c88),\
	PatchScaledLipsSingleByteReading("TUTORI0.BIN", 0x00005bf4)

bool PatchBattleTextDrawingCode(const string& inOriginalDirectory, const string& inPatchedDirectory, const string& inDataDirectory)
{
	struct PatchingData
	{
		union Command
		{
			uint32 longWord;
			uint16 word;
			uint8 byte;
		};

		uint32 address;
		Command command;
		Command originalCommand;
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
		"M00PRG.BIN", 0x0002fc1e, 0x1b, 0x0e,
		"M00PRG.BIN", 0x0002fe64, 0x1b, 0x0e,
		"M01PRG.BIN", 0x00028cbe, 0x1b, 0x0e,
		"M01PRG.BIN", 0x00028f04, 0x1b, 0x0e,
		"M02PRG.BIN", 0x0002d45e, 0x1b, 0x0e,
		"M02PRG.BIN", 0x0002d6a4, 0x1b, 0x0e,
		"M03PRG.BIN", 0x0002e442, 0x1b, 0x0e,
		"M03PRG.BIN", 0x0002e688, 0x1b, 0x0e,
		"M04PRG.BIN", 0x0002ebf2, 0x1b, 0x0e,
		"M04PRG.BIN", 0x0002ee38, 0x1b, 0x0e,
		"M05PRG.BIN", 0x0002d766, 0x1b, 0x0e,
		"M05PRG.BIN", 0x0002d9ac, 0x1b, 0x0e,
		"M26PRG.BIN", 0x0002f56a, 0x1b, 0x0e,
		"M26PRG.BIN", 0x0002f7b0, 0x1b, 0x0e,
		"M27PRG.BIN", 0x0002d31a, 0x1b, 0x0e,
		"M27PRG.BIN", 0x0002d560, 0x1b, 0x0e,
		"M29PRG.BIN", 0x00027156, 0x1b, 0x0e,
		"M29PRG.BIN", 0x0002739c, 0x1b, 0x0e,
		"M31PRG.BIN", 0x0002d59e, 0x1b, 0x0e,
		"M31PRG.BIN", 0x0002d7e4, 0x1b, 0x0e,
		"M33PRG.BIN", 0x0002e10e, 0x1b, 0x0e,
		"M33PRG.BIN", 0x0002e354, 0x1b, 0x0e,
		"M34PRG.BIN", 0x0002d1ea, 0x1b, 0x0e,
		"M34PRG.BIN", 0x0002d430, 0x1b, 0x0e,
		"SLGGOVER.BIN", 0x0001bb8e, 0x1b, 0x0e,
		"SLGGOVER.BIN", 0x0001bdd4, 0x1b, 0x0e,

		//0x060893b0 multiplying by 16 (height spacing) We need to multiply by 12
		PatchHeightSpacingInFiles(),

		//0x060893e0 will multiply by 16 (width spacing) (make this 4100 shll1)
		PatchWidthSpacingInFiles(),

		//0x060893fc is characters per line (make this E11B)
		"M00PRG.BIN", 0x0002fffc, 0x1b, 0x0d,
		"M01PRG.BIN", 0x0002909c, 0x1b, 0x0d,
		"M02PRG.BIN", 0x0002d83c, 0x1b, 0x0d,
		"M03PRG.BIN", 0x0002e820, 0x1b, 0x0d,
		"M04PRG.BIN", 0x0002efd0, 0x1b, 0x0d,
		"M05PRG.BIN", 0x0002db44, 0x1b, 0x0d,
		"M26PRG.BIN", 0x0002f948, 0x1b, 0x0d,
		"M27PRG.BIN", 0x0002d6f8, 0x1b, 0x0d,
		"M29PRG.BIN", 0x00027534, 0x1b, 0x0d,
		"M31PRG.BIN", 0x0002d97c, 0x1b, 0x0d,
		"M33PRG.BIN", 0x0002e4ec, 0x1b, 0x0d,
		"M34PRG.BIN", 0x0002d5c8, 0x1b, 0x0d,
		"SLGGOVER.BIN", 0x0001bf6c, 0x1b, 0x0d,

		//0x06089408 is num lines (make this e103)
		"M00PRG.BIN", 0x00030008, 0x03, 0x02,
		"M01PRG.BIN", 0x000290a8, 0x03, 0x02,
		"M02PRG.BIN", 0x0002d848, 0x03, 0x02,
		"M03PRG.BIN", 0x0002e82c, 0x03, 0x02,
		"M04PRG.BIN", 0x0002efdc, 0x03, 0x02,
		"M05PRG.BIN", 0x0002db50, 0x03, 0x02,
		"M26PRG.BIN", 0x0002f954, 0x03, 0x02,
		"M27PRG.BIN", 0x0002d704, 0x03, 0x02,
		"M29PRG.BIN", 0x00027540, 0x03, 0x02,
		"M31PRG.BIN", 0x0002d988, 0x03, 0x02,
		"M33PRG.BIN", 0x0002e4f8, 0x03, 0x02,
		"M34PRG.BIN", 0x0002d5d4, 0x03, 0x02,
		"SLGGOVER.BIN", 0x0001bf78, 0x03, 0x02,

		//0x06089450 multiplies by 0xe to get to next line write buffer, change to 0x1c
		"M00PRG.BIN", 0x00030050, 0x1c, 0x0e,
		"M01PRG.BIN", 0x000290f0, 0x1c, 0x0e,
		"M02PRG.BIN", 0x0002d890, 0x1c, 0x0e,
		"M03PRG.BIN", 0x0002e874, 0x1c, 0x0e,
		"M04PRG.BIN", 0x0002f024, 0x1c, 0x0e,
		"M05PRG.BIN", 0x0002db98, 0x1c, 0x0e,
		"M26PRG.BIN", 0x0002f99c, 0x1c, 0x0e,
		"M27PRG.BIN", 0x0002d74c, 0x1c, 0x0e,
		"M29PRG.BIN", 0x00027588, 0x1c, 0x0e,
		"M31PRG.BIN", 0x0002d9d0, 0x1c, 0x0e,
		"M33PRG.BIN", 0x0002e540, 0x1c, 0x0e,
		"M34PRG.BIN", 0x0002d61c, 0x1c, 0x0e,
		"SLGGOVER.BIN", 0x0001bfc0, 0x1c, 0x0e,

		//060894b0 is 0xa7f.  This is the amount of bytes to clear in vdp1 when clearing the text box.
		//Change this to 27*4 * (48+2)(bytes) cause 48 is 8x12/2
		"M00PRG.BIN", 0x000300b0, (27*4*50), 0x0a7f,
		"M01PRG.BIN", 0x00029150, (27*4*50), 0x0a7f,
		"M02PRG.BIN", 0x0002d8f0, (27*4*50), 0x0a7f,
		"M03PRG.BIN", 0x0002e8d4, (27*4*50), 0x0a7f,
		"M04PRG.BIN", 0x0002f084, (27*4*50), 0x0a7f,
		"M05PRG.BIN", 0x0002dbf8, (27*4*50), 0x0a7f,
		"M26PRG.BIN", 0x0002f9fc, (27*4*50), 0x0a7f,
		"M27PRG.BIN", 0x0002d7ac, (27*4*50), 0x0a7f,
		"M29PRG.BIN", 0x000275e8, (27*4*50), 0x0a7f,
		"M31PRG.BIN", 0x0002da30, (27*4*50), 0x0a7f,
		"M33PRG.BIN", 0x0002e5a0, (27*4*50), 0x0a7f,
		"M34PRG.BIN", 0x0002d67c, (27*4*50), 0x0a7f,
		"SLGGOVER.BIN", 0x0001c020, (27*4*50),0x0a7f,

		//060892a2 change to 0x1a
		"M00PRG.BIN", 0x0002fc5c, 0x1a, 0x0d,
		"M00PRG.BIN", 0x0002fd76, 0x1a, 0x0d,
		"M00PRG.BIN", 0x0002fea2, 0x1a, 0x0d,
		"M01PRG.BIN", 0x00028cfc, 0x1a, 0x0d,
		"M01PRG.BIN", 0x00028e16, 0x1a, 0x0d,
		"M01PRG.BIN", 0x00028f42, 0x1a, 0x0d,
		"M02PRG.BIN", 0x0002d49c, 0x1a, 0x0d,
		"M02PRG.BIN", 0x0002d5b6, 0x1a, 0x0d,
		"M02PRG.BIN", 0x0002d6e2, 0x1a, 0x0d,
		"M03PRG.BIN", 0x0002e480, 0x1a, 0x0d,
		"M03PRG.BIN", 0x0002e59a, 0x1a, 0x0d,
		"M03PRG.BIN", 0x0002e6c6, 0x1a, 0x0d,
		"M04PRG.BIN", 0x0002ec30, 0x1a, 0x0d,
		"M04PRG.BIN", 0x0002ed4a, 0x1a, 0x0d,
		"M04PRG.BIN", 0x0002ee76, 0x1a, 0x0d,
		"M05PRG.BIN", 0x0002d7a4, 0x1a, 0x0d,
		"M05PRG.BIN", 0x0002d8be, 0x1a, 0x0d,
		"M05PRG.BIN", 0x0002d9ea, 0x1a, 0x0d,
		"M26PRG.BIN", 0x0002f5a8, 0x1a, 0x0d,
		"M26PRG.BIN", 0x0002f6c2, 0x1a, 0x0d,
		"M26PRG.BIN", 0x0002f7ee, 0x1a, 0x0d,
		"M27PRG.BIN", 0x0002d358, 0x1a, 0x0d,
		"M27PRG.BIN", 0x0002d472, 0x1a, 0x0d,
		"M27PRG.BIN", 0x0002d59e, 0x1a, 0x0d,
		"M29PRG.BIN", 0x00027194, 0x1a, 0x0d,
		"M29PRG.BIN", 0x000272ae, 0x1a, 0x0d,
		"M29PRG.BIN", 0x000273da, 0x1a, 0x0d,
		"M31PRG.BIN", 0x0002d5dc, 0x1a, 0x0d,
		"M31PRG.BIN", 0x0002d6f6, 0x1a, 0x0d,
		"M31PRG.BIN", 0x0002d822, 0x1a, 0x0d,
		"M33PRG.BIN", 0x0002e14c, 0x1a, 0x0d,
		"M33PRG.BIN", 0x0002e266, 0x1a, 0x0d,
		"M33PRG.BIN", 0x0002e392, 0x1a, 0x0d,
		"M34PRG.BIN", 0x0002d228, 0x1a, 0x0d,
		"M34PRG.BIN", 0x0002d342, 0x1a, 0x0d,
		"M34PRG.BIN", 0x0002d46e, 0x1a, 0x0d,
		"SLGGOVER.BIN", 0x0001bbcc, 0x1a, 0x0d,
		"SLGGOVER.BIN", 0x0001bce6, 0x1a, 0x0d,
		"SLGGOVER.BIN", 0x0001be12, 0x1a, 0x0d,

		//0602063e will write up to 0xf characters per line(mov 0x0f, r3)
		"0.SLG", 0x0001b63e, 0x1c, 0x0f,

		//0608905c within giant function
		"M00PRG.BIN", 0x0002fc5c, 0x1b, 0x0d,
		"M00PRG.BIN", 0x0002fd76, 0x1b, 0x0d,
		"M00PRG.BIN", 0x0002fea2, 0x1b, 0x0d,
		"M01PRG.BIN", 0x00028cfc, 0x1b, 0x0d,
		"M01PRG.BIN", 0x00028e16, 0x1b, 0x0d,
		"M01PRG.BIN", 0x00028f42, 0x1b, 0x0d,
		"M02PRG.BIN", 0x0002d49c, 0x1b, 0x0d,
		"M02PRG.BIN", 0x0002d5b6, 0x1b, 0x0d,
		"M02PRG.BIN", 0x0002d6e2, 0x1b, 0x0d,
		"M03PRG.BIN", 0x0002e480, 0x1b, 0x0d,
		"M03PRG.BIN", 0x0002e59a, 0x1b, 0x0d,
		"M03PRG.BIN", 0x0002e6c6, 0x1b, 0x0d,
		"M04PRG.BIN", 0x0002ec30, 0x1b, 0x0d,
		"M04PRG.BIN", 0x0002ed4a, 0x1b, 0x0d,
		"M04PRG.BIN", 0x0002ee76, 0x1b, 0x0d,
		"M05PRG.BIN", 0x0002d7a4, 0x1b, 0x0d,
		"M05PRG.BIN", 0x0002d8be, 0x1b, 0x0d,
		"M05PRG.BIN", 0x0002d9ea, 0x1b, 0x0d,
		"M26PRG.BIN", 0x0002f5a8, 0x1b, 0x0d,
		"M26PRG.BIN", 0x0002f6c2, 0x1b, 0x0d,
		"M26PRG.BIN", 0x0002f7ee, 0x1b, 0x0d,
		"M27PRG.BIN", 0x0002d358, 0x1b, 0x0d,
		"M27PRG.BIN", 0x0002d472, 0x1b, 0x0d,
		"M27PRG.BIN", 0x0002d59e, 0x1b, 0x0d,
		"M29PRG.BIN", 0x00027194, 0x1b, 0x0d,
		"M29PRG.BIN", 0x000272ae, 0x1b, 0x0d,
		"M29PRG.BIN", 0x000273da, 0x1b, 0x0d,
		"M31PRG.BIN", 0x0002d5dc, 0x1b, 0x0d,
		"M31PRG.BIN", 0x0002d6f6, 0x1b, 0x0d,
		"M31PRG.BIN", 0x0002d822, 0x1b, 0x0d,
		"M33PRG.BIN", 0x0002e14c, 0x1b, 0x0d,
		"M33PRG.BIN", 0x0002e266, 0x1b, 0x0d,
		"M33PRG.BIN", 0x0002e392, 0x1b, 0x0d,
		"M34PRG.BIN", 0x0002d228, 0x1b, 0x0d,
		"M34PRG.BIN", 0x0002d342, 0x1b, 0x0d,
		"M34PRG.BIN", 0x0002d46e, 0x1b, 0x0d,
		"SLGGOVER.BIN", 0x0001bbcc, 0x1b, 0x0d,
		"SLGGOVER.BIN", 0x0001bce6, 0x1b, 0x0d,
		"SLGGOVER.BIN", 0x0001be12, 0x1b, 0x0d,

		//0608912e More within giant function
		"M00PRG.BIN", 0x0002fc1e, 0x1c, 0x0e,
		"M00PRG.BIN", 0x0002fd2e, 0x1c, 0x0e,
		"M00PRG.BIN", 0x0002fe64, 0x1c, 0x0e,
		"M01PRG.BIN", 0x00028cbe, 0x1c, 0x0e,
		"M01PRG.BIN", 0x00028dce, 0x1c, 0x0e,
		"M01PRG.BIN", 0x00028f04, 0x1c, 0x0e,
		"M02PRG.BIN", 0x0002d45e, 0x1c, 0x0e,
		"M02PRG.BIN", 0x0002d56e, 0x1c, 0x0e,
		"M02PRG.BIN", 0x0002d6a4, 0x1c, 0x0e,
		"M03PRG.BIN", 0x0002e442, 0x1c, 0x0e,
		"M03PRG.BIN", 0x0002e552, 0x1c, 0x0e,
		"M03PRG.BIN", 0x0002e688, 0x1c, 0x0e,
		"M04PRG.BIN", 0x0002ebf2, 0x1c, 0x0e,
		"M04PRG.BIN", 0x0002ed02, 0x1c, 0x0e,
		"M04PRG.BIN", 0x0002ee38, 0x1c, 0x0e,
		"M05PRG.BIN", 0x0002d766, 0x1c, 0x0e,
		"M05PRG.BIN", 0x0002d876, 0x1c, 0x0e,
		"M05PRG.BIN", 0x0002d9ac, 0x1c, 0x0e,
		"M26PRG.BIN", 0x0002f56a, 0x1c, 0x0e,
		"M26PRG.BIN", 0x0002f67a, 0x1c, 0x0e,
		"M26PRG.BIN", 0x0002f7b0, 0x1c, 0x0e,
		"M27PRG.BIN", 0x0002d31a, 0x1c, 0x0e,
		"M27PRG.BIN", 0x0002d42a, 0x1c, 0x0e,
		"M27PRG.BIN", 0x0002d560, 0x1c, 0x0e,
		"M29PRG.BIN", 0x00027156, 0x1c, 0x0e,
		"M29PRG.BIN", 0x00027266, 0x1c, 0x0e,
		"M29PRG.BIN", 0x0002739c, 0x1c, 0x0e,
		"M31PRG.BIN", 0x0002d59e, 0x1c, 0x0e,
		"M31PRG.BIN", 0x0002d6ae, 0x1c, 0x0e,
		"M31PRG.BIN", 0x0002d7e4, 0x1c, 0x0e,
		"M33PRG.BIN", 0x0002e10e, 0x1c, 0x0e,
		"M33PRG.BIN", 0x0002e21e, 0x1c, 0x0e,
		"M33PRG.BIN", 0x0002e354, 0x1c, 0x0e,
		"M34PRG.BIN", 0x0002d1ea, 0x1c, 0x0e,
		"M34PRG.BIN", 0x0002d2fa, 0x1c, 0x0e,
		"M34PRG.BIN", 0x0002d430, 0x1c, 0x0e,
		"SLGGOVER.BIN", 0x0001bb8e, 0x1c, 0x0e,
		"SLGGOVER.BIN", 0x0001bc9e, 0x1c, 0x0e,
		"SLGGOVER.BIN", 0x0001bdd4, 0x1c, 0x0e,

		//0608903c and 06089282 change to 0x03 (number of lines)
		"M00PRG.BIN", 0x0002fc3c, 0x03, 0x02,
		"M00PRG.BIN", 0x0002fe82, 0x03, 0x02,
		"M01PRG.BIN", 0x00028cdc, 0x03, 0x02,
		"M01PRG.BIN", 0x00028f22, 0x03, 0x02,
		"M02PRG.BIN", 0x0002d47c, 0x03, 0x02,
		"M02PRG.BIN", 0x0002d6c2, 0x03, 0x02,
		"M03PRG.BIN", 0x0002e460, 0x03, 0x02,
		"M03PRG.BIN", 0x0002e6a6, 0x03, 0x02,
		"M04PRG.BIN", 0x0002ec10, 0x03, 0x02,
		"M04PRG.BIN", 0x0002ee56, 0x03, 0x02,
		"M05PRG.BIN", 0x0002d784, 0x03, 0x02,
		"M05PRG.BIN", 0x0002d9ca, 0x03, 0x02,
		"M26PRG.BIN", 0x0002f588, 0x03, 0x02,
		"M26PRG.BIN", 0x0002f7ce, 0x03, 0x02,
		"M27PRG.BIN", 0x0002d338, 0x03, 0x02,
		"M27PRG.BIN", 0x0002d57e, 0x03, 0x02,
		"M29PRG.BIN", 0x00027174, 0x03, 0x02,
		"M29PRG.BIN", 0x000273ba, 0x03, 0x02,
		"M31PRG.BIN", 0x0002d5bc, 0x03, 0x02,
		"M31PRG.BIN", 0x0002d802, 0x03, 0x02,
		"M33PRG.BIN", 0x0002e12c, 0x03, 0x02,
		"M33PRG.BIN", 0x0002e372, 0x03, 0x02,
		"M34PRG.BIN", 0x0002d208, 0x03, 0x02,
		"M34PRG.BIN", 0x0002d44e, 0x03, 0x02,
		"SLGGOVER.BIN", 0x0001bbac, 0x03, 0x02,
		"SLGGOVER.BIN", 0x0001bdf2, 0x03, 0x02,

		// 0608914c
		"M00PRG.BIN", 0x0002fd4c, 0x03, 0x02,
		"M01PRG.BIN", 0x00028dec, 0x03, 0x02,
		"M02PRG.BIN", 0x0002d58c, 0x03, 0x02,
		"M03PRG.BIN", 0x0002e570, 0x03, 0x02,
		"M04PRG.BIN", 0x0002ed20, 0x03, 0x02,
		"M05PRG.BIN", 0x0002d894, 0x03, 0x02,
		"M26PRG.BIN", 0x0002f698, 0x03, 0x02,
		"M27PRG.BIN", 0x0002d448, 0x03, 0x02,
		"M29PRG.BIN", 0x00027284, 0x03, 0x02,
		"M31PRG.BIN", 0x0002d6cc, 0x03, 0x02,
		"M33PRG.BIN", 0x0002e23c, 0x03, 0x02,
		"M34PRG.BIN", 0x0002d318, 0x03, 0x02,
		"SLGGOVER.BIN", 0x0001bcbc, 0x03, 0x02,
		
		/*Making space for a bigger text buffer
		06034760 (3 instances need to be changed to 6046000 in 0.slg) 
		- at 0600739c, 06007400, 060074e0[@0x0000239c, 0x2400, 0x24e0]
		0600735a need to change 0x3f to 6f*/
		"0.SLG", 0x0000239c, 0x06046000, 0x06034760,
		"0.SLG", 0x00002400, 0x06046000, 0x06034760,
		"0.SLG", 0x000024e0, 0x06046000, 0x06034760,
		"0.SLG", 0x0000235a, 0x6f, 0x3f,


		/*Lips spacing
		060d146c is calculation to center text.Set this to 4200 (shll r2)
		060d14ef is vertical offset.Change to 0x93
		060d1503 is X spacing.Set this to 0x08
		060d16bf is another horizontal offset.Set this to f8
		*/
		"EV00001.BIN", 0x000071f0, 0xffff, 0,
		"EV00002.BIN", 0x00005af4, 0xffff, 0,
		"EV00050.BIN", 0x00005af4, 0xffff, 0,
		"EV00051.BIN", 0x00005af4, 0xffff, 0,
		"EV00052.BIN", 0x00005af4, 0xffff, 0,
		"EV00054.BIN", 0x00005af4, 0xffff, 0,
		"EV00055.BIN", 0x00005af4, 0xffff, 0,
		"EV00060.BIN", 0x00005af4, 0xffff, 0,
		"EV01001.BIN", 0x00005c00, 0xffff, 0,
		"EV01002.BIN", 0x00005af4, 0xffff, 0,
		"EV01020.BIN", 0x00006118, 0xffff, 0,
		"EV01021.BIN", 0x00005f2c, 0xffff, 0,
		"EV01022.BIN", 0x00005af4, 0xffff, 0,
		"EV01023.BIN", 0x00005af4, 0xffff, 0,
		"EV01030.BIN", 0x00005af4, 0xffff, 0,
		"EV01050.BIN", 0x00005af4, 0xffff, 0,
		"EV01054.BIN", 0x00005af4, 0xffff, 0,
		"EV01055.BIN", 0x00005af4, 0xffff, 0,
		"EV02001.BIN", 0x00005af4, 0xffff, 0,
		"EV02002.BIN", 0x00005af4, 0xffff, 0,
		"EV02010.BIN", 0x00005af4, 0xffff, 0,
		"EV02021.BIN", 0x00005af4, 0xffff, 0,
		"EV02025.BIN", 0x00005af4, 0xffff, 0,
		"EV02050.BIN", 0x00005af4, 0xffff, 0,
		"EV02051.BIN", 0x00005af4, 0xffff, 0,
		"EV02052.BIN", 0x00005af4, 0xffff, 0,
		"EV02053.BIN", 0x00005af4, 0xffff, 0,
		"EV02054.BIN", 0x00005af4, 0xffff, 0,
		"EV03001.BIN", 0x00005cc0, 0xffff, 0,
		"EV03002.BIN", 0x00005b68, 0xffff, 0,
		"EV03005.BIN", 0x00005b20, 0xffff, 0,
		"EV03010.BIN", 0x00005b04, 0xffff, 0,
		"EV03020.BIN", 0x00005b04, 0xffff, 0,
		"EV03021.BIN", 0x00005b04, 0xffff, 0,
		"EV03023.BIN", 0x00005b1c, 0xffff, 0,
		"EV03024.BIN", 0x00005b1c, 0xffff, 0,
		"EV03025.BIN", 0x00005b30, 0xffff, 0,
		"EV03050.BIN", 0x00005f34, 0xffff, 0,
		"EV03051.BIN", 0x00005b58, 0xffff, 0,
		"EV03052.BIN", 0x00005bc4, 0xffff, 0,
		"EV03053.BIN", 0x00005b80, 0xffff, 0,
		"EV04001.BIN", 0x000069ac, 0xffff, 0,
		"EV04002.BIN", 0x00005b70, 0xffff, 0,
		"EV04003.BIN", 0x00005ca4, 0xffff, 0,
		"EV04005.BIN", 0x00005af4, 0xffff, 0,
		"EV04010.BIN", 0x00005b68, 0xffff, 0,
		"EV04020.BIN", 0x00005af4, 0xffff, 0,
		"EV04022.BIN", 0x00005afc, 0xffff, 0,
		"EV04050.BIN", 0x00005b9c, 0xffff, 0,
		"EV04053.BIN", 0x00005af4, 0xffff, 0,
		"EV04055.BIN", 0x00005b68, 0xffff, 0,
		"EV05001.BIN", 0x00005b0c, 0xffff, 0,
		"EV05002.BIN", 0x00005af4, 0xffff, 0,
		"EV05003.BIN", 0x00005cd0, 0xffff, 0,
		"EV05004.BIN", 0x00005af4, 0xffff, 0,
		"EV05005.BIN", 0x00005af4, 0xffff, 0,
		"EV05007.BIN", 0x00005af4, 0xffff, 0,
		"EV05010.BIN", 0x00005af4, 0xffff, 0,
		"EV05011.BIN", 0x00005af4, 0xffff, 0,
		"EV05018.BIN", 0x00005af4, 0xffff, 0,
		"EV05019.BIN", 0x00005b1c, 0xffff, 0,
		"EV05020.BIN", 0x00005af4, 0xffff, 0,
		"EV05021.BIN", 0x00005af4, 0xffff, 0,
		"EV05022.BIN", 0x00005af4, 0xffff, 0,
		"EV05023.BIN", 0x00005af4, 0xffff, 0,
		"EV05025.BIN", 0x00005af4, 0xffff, 0,
		"EV05026.BIN", 0x00005af4, 0xffff, 0,
		"EV05027.BIN", 0x00005af4, 0xffff, 0,
		"EV05051.BIN", 0x00005af4, 0xffff, 0,
		"EV05052.BIN", 0x00005af4, 0xffff, 0,
		"EV05053.BIN", 0x00005af4, 0xffff, 0,
		"EV05054.BIN", 0x00005af4, 0xffff, 0,
		"EV05060.BIN", 0x00005af4, 0xffff, 0,
		"EV26001.BIN", 0x00006900, 0xffff, 0,
		"EV26002.BIN", 0x00005e9c, 0xffff, 0,
		"EV26020.BIN", 0x00005af4, 0xffff, 0,
		"EV26021.BIN", 0x00005af4, 0xffff, 0,
		"EV27001.BIN", 0x00005b00, 0xffff, 0,
		"EV27002.BIN", 0x00005ba8, 0xffff, 0,
		"EV27054.BIN", 0x00005af4, 0xffff, 0,
		"EV28001.BIN", 0x00005af4, 0xffff, 0,
		"EV28025.BIN", 0x00005af4, 0xffff, 0,
		"EV28050.BIN", 0x00005af4, 0xffff, 0,
		"EV28051.BIN", 0x00005af4, 0xffff, 0,
		"EV29001.BIN", 0x00005b18, 0xffff, 0,
		"EV29002.BIN", 0x00005b18, 0xffff, 0,
		"EV29021.BIN", 0x00005af4, 0xffff, 0,
		"EV29050.BIN", 0x00005af4, 0xffff, 0,
		"EV30003.BIN", 0x00006b3c, 0xffff, 0,
		"EV30020.BIN", 0x00005b04, 0xffff, 0,
		"EV30026.BIN", 0x00005b04, 0xffff, 0,
		"EV30027.BIN", 0x00005b04, 0xffff, 0,
		"EV31001.BIN", 0x00005d9c, 0xffff, 0,
		"EV31002.BIN", 0x00005cf4, 0xffff, 0,
		"EV31023.BIN", 0x00005b1c, 0xffff, 0,
		"EV31024.BIN", 0x00005b1c, 0xffff, 0,
		"EV32002.BIN", 0x00005b28, 0xffff, 0,
		"EV32020.BIN", 0x00005af4, 0xffff, 0,
		"EV32055.BIN", 0x00005b68, 0xffff, 0,
		"EV33001.BIN", 0x00006048, 0xffff, 0,
		"EV33002.BIN", 0x00005db0, 0xffff, 0,
		"EV33022.BIN", 0x00005afc, 0xffff, 0,
		"EV33054.BIN", 0x00005af4, 0xffff, 0,
		"EV34001.BIN", 0x00005b18, 0xffff, 0,
		"EV34002.BIN", 0x00005b30, 0xffff, 0,
		"EV34003.BIN", 0x00005b18, 0xffff, 0,
		"EV34004.BIN", 0x00005b70, 0xffff, 0,
		"EV34005.BIN", 0x00005b70, 0xffff, 0,
		"EV34006.BIN", 0x00005ba8, 0xffff, 0,
		"EV34007.BIN", 0x00005b18, 0xffff, 0,
		"EV34008.BIN", 0x00005ba8, 0xffff, 0,
		"EV34020.BIN", 0x00005af4, 0xffff, 0,
		"EV34030.BIN", 0x00005af4, 0xffff, 0,
		"EV34040.BIN", 0x00005af4, 0xffff, 0,
		"EV34041.BIN", 0x00005af4, 0xffff, 0,
		"EV34042.BIN", 0x00005af4, 0xffff, 0,
		"EV34043.BIN", 0x00005af4, 0xffff, 0,
		"EV34044.BIN", 0x00005af4, 0xffff, 0,
		"M00PRG.BIN", 0x000795f0, 0xffff, 0,
		"M26PRG.BIN", 0x00078d00, 0xffff, 0,
		"TUTORI0.BIN", 0x00005c6c, 0xffff, 0,
		PatchCharacterDimensionsForFiles(),
		PatchCharacterDimensionsForLipsInFiles(),
		PatchScaledLipsFormattingInFiles(),
		PatchScaledLipsSpriteBufferInFiles(),
		PatchScaledLipsTileStrideInFiles(),

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
			0608921e add r0, r0           //611c extu.b r1, r1
			06089220 mov.w @(r0, r1), r1  //011c
			*/
		//	PatchSingleByteReadingInFiles(),
		
			PatchSingleByteHiCharacterHandlingInFiles(),
			LipsSingleBytePatching(),
			LipsSingleBytePatching2(),
			PatchScaledLipsSingleByteReadingInFiles()
		#endif
	};

	const int numEntries = sizeof(patchingInfo) / sizeof(patchingInfo[0]);
	for( int i = 0; i < numEntries; ++i )
	{		
		const string sakuraBinFilePath = inPatchedDirectory + Seperators + string("SAKURA2\\") + string(patchingInfo[i].pFileName);
		const string sakuraBinOriginalFilePath = inOriginalDirectory + Seperators + string("SAKURA2\\") + string(patchingInfo[i].pFileName);
		
		FileReadWriter sakuraBin;
		if (!sakuraBin.OpenFile(sakuraBinFilePath))
		{
			return false;
		}

		FileReadWriter sakuraBinOriginal;
		if (!sakuraBinOriginal.OpenFile(sakuraBinOriginalFilePath))
		{
			return false;
		}

		//Lips Patching
		if(patchingInfo[i].data.command.word == 0xffff)
		{
			sakuraBin.WriteData(patchingInfo[i].data.address, lipsPatchData.GetData(), lipsPatchData.GetDataSize(), false);
			continue;
		}

		const bool bIsLongWord = (patchingInfo[i].data.command.longWord) > 0xffff;
		if(bIsLongWord)
		{
			unsigned int originalValue = 0;
			sakuraBinOriginal.ReadData(patchingInfo[i].data.address, (char*)&originalValue, sizeof(originalValue), true);
			if(originalValue != patchingInfo[i].data.originalCommand.longWord)
			{
				printf("In %s at 0x%08x, expecting 0x%08x but found 0x%08x\n", patchingInfo[i].pFileName, patchingInfo[i].data.address, patchingInfo[i].data.originalCommand.longWord, originalValue);
			//	return false;
			}
		
			sakuraBin.WriteData(patchingInfo[i].data.address, (char*)&patchingInfo[i].data.command.longWord, 4, true);
			continue;
		}

		const bool bIsWord = (patchingInfo[i].data.command.word & 0xff00) != 0 || patchingInfo[i].data.command.byte == 0x09 || patchingInfo[i].data.originalCommand.word == 0xffff;
		if( bIsWord )
		{
			unsigned short originalValue = 0;
			sakuraBinOriginal.ReadData(patchingInfo[i].data.address, (char*)&originalValue, sizeof(originalValue), true);
			if (originalValue != patchingInfo[i].data.originalCommand.word)
			{
				printf("In %s at 0x%08x, expecting 0x%04x but found 0x%04x\n", patchingInfo[i].pFileName, patchingInfo[i].data.address, patchingInfo[i].data.originalCommand.word, originalValue);
		//		return false;
			}

			sakuraBin.WriteData(patchingInfo[i].data.address, (char*)&patchingInfo[i].data.command.word, 2, true);
		}
		else
		{
			unsigned char originalValue = 0;
			sakuraBinOriginal.ReadData(patchingInfo[i].data.address + 1, (char*)&originalValue, sizeof(originalValue), true);
			if (originalValue != patchingInfo[i].data.originalCommand.byte)
			{
				printf("In %s at 0x%08x, expecting 0x%01x but found 0x%01x\n", patchingInfo[i].pFileName, patchingInfo[i].data.address, patchingInfo[i].data.originalCommand.byte, originalValue);
			//	return false;
			}

			sakuraBin.WriteData(patchingInfo[i].data.address + 1, (char*)&patchingInfo[i].data.command.byte, 1, false);
		}
	}

	return true;
}