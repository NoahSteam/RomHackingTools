#pragma once

//0x06089264 checking for 0xe, change to 0x1c
#define PatchBattleCode1() \
	"M00PRG.BIN", 0x0002fc1e, 0x1c, 0x0e,\
	"M00PRG.BIN", 0x0002fd2e, 0x1c, 0x0e,\
	"M00PRG.BIN", 0x0002fe64, 0x1c, 0x0e,\
	"M01PRG.BIN", 0x00028cbe, 0x1c, 0x0e,\
	"M01PRG.BIN", 0x00028dce, 0x1c, 0x0e,\
	"M01PRG.BIN", 0x00028f04, 0x1c, 0x0e,\
	"M02PRG.BIN", 0x0002d45e, 0x1c, 0x0e,\
	"M02PRG.BIN", 0x0002d56e, 0x1c, 0x0e,\
	"M02PRG.BIN", 0x0002d6a4, 0x1c, 0x0e,\
	"M03PRG.BIN", 0x0002e442, 0x1c, 0x0e,\
	"M03PRG.BIN", 0x0002e552, 0x1c, 0x0e,\
	"M03PRG.BIN", 0x0002e688, 0x1c, 0x0e,\
	"M04PRG.BIN", 0x0002ebf2, 0x1c, 0x0e,\
	"M04PRG.BIN", 0x0002ed02, 0x1c, 0x0e,\
	"M04PRG.BIN", 0x0002ee38, 0x1c, 0x0e,\
	"M05PRG.BIN", 0x0002d766, 0x1c, 0x0e,\
	"M05PRG.BIN", 0x0002d876, 0x1c, 0x0e,\
	"M05PRG.BIN", 0x0002d9ac, 0x1c, 0x0e,\
	"M26PRG.BIN", 0x0002f56a, 0x1c, 0x0e,\
	"M26PRG.BIN", 0x0002f67a, 0x1c, 0x0e,\
	"M26PRG.BIN", 0x0002f7b0, 0x1c, 0x0e,\
	"M27PRG.BIN", 0x0002d31a, 0x1c, 0x0e,\
	"M27PRG.BIN", 0x0002d42a, 0x1c, 0x0e,\
	"M27PRG.BIN", 0x0002d560, 0x1c, 0x0e,\
	"M29PRG.BIN", 0x00027156, 0x1c, 0x0e,\
	"M29PRG.BIN", 0x00027266, 0x1c, 0x0e,\
	"M29PRG.BIN", 0x0002739c, 0x1c, 0x0e,\
	"M31PRG.BIN", 0x0002d59e, 0x1c, 0x0e,\
	"M31PRG.BIN", 0x0002d6ae, 0x1c, 0x0e,\
	"M31PRG.BIN", 0x0002d7e4, 0x1c, 0x0e,\
	"M33PRG.BIN", 0x0002e10e, 0x1c, 0x0e,\
	"M33PRG.BIN", 0x0002e21e, 0x1c, 0x0e,\
	"M33PRG.BIN", 0x0002e354, 0x1c, 0x0e,\
	"M34PRG.BIN", 0x0002d1ea, 0x1c, 0x0e,\
	"M34PRG.BIN", 0x0002d2fa, 0x1c, 0x0e,\
	"M34PRG.BIN", 0x0002d430, 0x1c, 0x0e,\
	"SLGGOVER.BIN", 0x0001bb8e, 0x1c, 0x0e,\
	"SLGGOVER.BIN", 0x0001bc9e, 0x1c, 0x0e,\
	"SLGGOVER.BIN", 0x0001bdd4, 0x1c, 0x0e,\
	"M07PRG.BIN", 0x0002e56a, 0x1c, 0x0e,\
	"M07PRG.BIN", 0x0002e67a, 0x1c, 0x0e,\
	"M07PRG.BIN", 0x0002e7b0, 0x1c, 0x0e,\
	"M08PRG.BIN", 0x00027dea, 0x1c, 0x0e,\
	"M08PRG.BIN", 0x00027efa, 0x1c, 0x0e,\
	"M08PRG.BIN", 0x00028030, 0x1c, 0x0e,\
	"M09PRG.BIN", 0x0002fbda, 0x1c, 0x0e,\
	"M09PRG.BIN", 0x0002fcea, 0x1c, 0x0e,\
	"M09PRG.BIN", 0x0002fe20, 0x1c, 0x0e,\
	"M10PRG.BIN", 0x00027b32, 0x1c, 0x0e,\
	"M10PRG.BIN", 0x00027c42, 0x1c, 0x0e,\
	"M10PRG.BIN", 0x00027d78, 0x1c, 0x0e,\
	"M11PRG.BIN", 0x00029542, 0x1c, 0x0e,\
	"M11PRG.BIN", 0x00029652, 0x1c, 0x0e,\
	"M11PRG.BIN", 0x00029788, 0x1c, 0x0e,\
	"M12PRG.BIN", 0x000272ee, 0x1c, 0x0e,\
	"M12PRG.BIN", 0x000273fe, 0x1c, 0x0e,\
	"M12PRG.BIN", 0x00027534, 0x1c, 0x0e,\
	"M13PRG.BIN", 0x000271d2, 0x1c, 0x0e,\
	"M13PRG.BIN", 0x000272e2, 0x1c, 0x0e,\
	"M13PRG.BIN", 0x00027418, 0x1c, 0x0e,\
	"M15PRG.BIN", 0x0002632a, 0x1c, 0x0e,\
	"M15PRG.BIN", 0x0002643a, 0x1c, 0x0e,\
	"M15PRG.BIN", 0x00026570, 0x1c, 0x0e,\
	"M16PRG.BIN", 0x00020e42, 0x1c, 0x0e,\
	"M16PRG.BIN", 0x00020f52, 0x1c, 0x0e,\
	"M16PRG.BIN", 0x00021088, 0x1c, 0x0e,\
	"M17PRG.BIN", 0x00028d7e, 0x1c, 0x0e,\
	"M17PRG.BIN", 0x00028e8e, 0x1c, 0x0e,\
	"M17PRG.BIN", 0x00028fc4, 0x1c, 0x0e,\
	"M18PRG.BIN", 0x000268ba, 0x1c, 0x0e,\
	"M18PRG.BIN", 0x000269ca, 0x1c, 0x0e,\
	"M18PRG.BIN", 0x00026b00, 0x1c, 0x0e,\
	"M19PRG.BIN", 0x0002079e, 0x1c, 0x0e,\
	"M19PRG.BIN", 0x000208ae, 0x1c, 0x0e,\
	"M19PRG.BIN", 0x000209e4, 0x1c, 0x0e,\
	"M20PRG.BIN", 0x00026572, 0x1c, 0x0e,\
	"M20PRG.BIN", 0x00026682, 0x1c, 0x0e,\
	"M20PRG.BIN", 0x000267b8, 0x1c, 0x0e,\
	"M21PRG.BIN", 0x0002e62e, 0x1c, 0x0e,\
	"M21PRG.BIN", 0x0002e73e, 0x1c, 0x0e,\
	"M21PRG.BIN", 0x0002e874, 0x1c, 0x0e,\
	"M22PRG.BIN", 0x0002fa06, 0x1c, 0x0e,\
	"M22PRG.BIN", 0x0002fb16, 0x1c, 0x0e,\
	"M22PRG.BIN", 0x0002fc4c, 0x1c, 0x0e,\
	"M23PRG.BIN", 0x0002de3e, 0x1c, 0x0e,\
	"M23PRG.BIN", 0x0002df4e, 0x1c, 0x0e,\
	"M23PRG.BIN", 0x0002e084, 0x1c, 0x0e,\
	"M24PRG.BIN", 0x0002e02a, 0x1c, 0x0e,\
	"M24PRG.BIN", 0x0002e13a, 0x1c, 0x0e,\
	"M24PRG.BIN", 0x0002e270, 0x1c, 0x0e,\
	"M25PRG.BIN", 0x0002df96, 0x1c, 0x0e,\
	"M25PRG.BIN", 0x0002e0a6, 0x1c, 0x0e,\
	"M25PRG.BIN", 0x0002e1dc, 0x1c, 0x0e,\
	"M36PRG.BIN", 0x0002ede2, 0x1c, 0x0e,\
	"M36PRG.BIN", 0x0002eef2, 0x1c, 0x0e,\
	"M36PRG.BIN", 0x0002f028, 0x1c, 0x0e,\
	"M37PRG.BIN", 0x00020ce6, 0x1c, 0x0e,\
	"M37PRG.BIN", 0x00020df6, 0x1c, 0x0e,\
	"M37PRG.BIN", 0x00020f2c, 0x1c, 0x0e,\
	"M45PRG.BIN", 0x0002080a, 0x1c, 0x0e,\
	"M45PRG.BIN", 0x0002091a, 0x1c, 0x0e,\
	"M45PRG.BIN", 0x00020a50, 0x1c, 0x0e,\
	"M46PRG.BIN", 0x000217ca, 0x1c, 0x0e,\
	"M46PRG.BIN", 0x000218da, 0x1c, 0x0e,\
	"M46PRG.BIN", 0x00021a10, 0x1c, 0x0e,\
	"M47PRG.BIN", 0x0002d63e, 0x1c, 0x0e,\
	"M47PRG.BIN", 0x0002d74e, 0x1c, 0x0e,\
	"M47PRG.BIN", 0x0002d884, 0x1c, 0x0e,\
	"M49PRG.BIN", 0x00020c5a, 0x1c, 0x0e,\
	"M49PRG.BIN", 0x00020d6a, 0x1c, 0x0e,\
	"M49PRG.BIN", 0x00020ea0, 0x1c, 0x0e,\
	"M50PRG.BIN", 0x00025592, 0x1c, 0x0e,\
	"M50PRG.BIN", 0x000256a2, 0x1c, 0x0e,\
	"M50PRG.BIN", 0x000257d8, 0x1c, 0x0e,\
	"M51PRG.BIN", 0x0002d8d6, 0x1c, 0x0e,\
	"M51PRG.BIN", 0x0002d9e6, 0x1c, 0x0e,\
	"M51PRG.BIN", 0x0002db1c, 0x1c, 0x0e,\
	"M52PRG.BIN", 0x000210ca, 0x1c, 0x0e,\
	"M52PRG.BIN", 0x000211da, 0x1c, 0x0e,\
	"M52PRG.BIN", 0x00021310, 0x1c, 0x0e,\
	"M53PRG.BIN", 0x0002e5c6, 0x1c, 0x0e,\
	"M53PRG.BIN", 0x0002e6d6, 0x1c, 0x0e,\
	"M53PRG.BIN", 0x0002e80c, 0x1c, 0x0e,\
	"M54PRG.BIN", 0x0002df46, 0x1c, 0x0e,\
	"M54PRG.BIN", 0x0002e056, 0x1c, 0x0e,\
	"M54PRG.BIN", 0x0002e18c, 0x1c, 0x0e,\
	"M91PRG.BIN", 0x0001d2f2, 0x1c, 0x0e,\
	"M91PRG.BIN", 0x0001d402, 0x1c, 0x0e,\
	"M91PRG.BIN", 0x0001d538, 0x1c, 0x0e,\
	"M92PRG.BIN", 0x00016836, 0x1c, 0x0e,\
	"M92PRG.BIN", 0x00016946, 0x1c, 0x0e,\
	"M92PRG.BIN", 0x00016a7c, 0x1c, 0x0e

//0x060893fc is characters per line (make this E11B)
#define PatchBattleCode2() \
	"M00PRG.BIN", 0x0002fffc, 0x1b, 0x0d,\
	"M01PRG.BIN", 0x0002909c, 0x1b, 0x0d,\
	"M02PRG.BIN", 0x0002d83c, 0x1b, 0x0d,\
	"M03PRG.BIN", 0x0002e820, 0x1b, 0x0d,\
	"M04PRG.BIN", 0x0002efd0, 0x1b, 0x0d,\
	"M05PRG.BIN", 0x0002db44, 0x1b, 0x0d,\
	"M26PRG.BIN", 0x0002f948, 0x1b, 0x0d,\
	"M27PRG.BIN", 0x0002d6f8, 0x1b, 0x0d,\
	"M29PRG.BIN", 0x00027534, 0x1b, 0x0d,\
	"M31PRG.BIN", 0x0002d97c, 0x1b, 0x0d,\
	"M33PRG.BIN", 0x0002e4ec, 0x1b, 0x0d,\
	"M34PRG.BIN", 0x0002d5c8, 0x1b, 0x0d,\
	"SLGGOVER.BIN", 0x0001bf6c, 0x1b, 0x0d,\
	"M07PRG.BIN", 0x0002e948, 0x1b, 0x0d,\
	"M08PRG.BIN", 0x000281c8, 0x1b, 0x0d,\
	"M09PRG.BIN", 0x0002ffb8, 0x1b, 0x0d,\
	"M10PRG.BIN", 0x00027f10, 0x1b, 0x0d,\
	"M11PRG.BIN", 0x00029920, 0x1b, 0x0d,\
	"M12PRG.BIN", 0x000276cc, 0x1b, 0x0d,\
	"M13PRG.BIN", 0x000275b0, 0x1b, 0x0d,\
	"M15PRG.BIN", 0x00026708, 0x1b, 0x0d,\
	"M16PRG.BIN", 0x00021220, 0x1b, 0x0d,\
	"M17PRG.BIN", 0x0002915c, 0x1b, 0x0d,\
	"M18PRG.BIN", 0x00026c98, 0x1b, 0x0d,\
	"M19PRG.BIN", 0x00020b7c, 0x1b, 0x0d,\
	"M20PRG.BIN", 0x00026950, 0x1b, 0x0d,\
	"M21PRG.BIN", 0x0002ea0c, 0x1b, 0x0d,\
	"M22PRG.BIN", 0x0002fde4, 0x1b, 0x0d,\
	"M23PRG.BIN", 0x0002e21c, 0x1b, 0x0d,\
	"M24PRG.BIN", 0x0002e408, 0x1b, 0x0d,\
	"M25PRG.BIN", 0x0002e374, 0x1b, 0x0d,\
	"M36PRG.BIN", 0x0002f1c0, 0x1b, 0x0d,\
	"M37PRG.BIN", 0x000210c4, 0x1b, 0x0d,\
	"M45PRG.BIN", 0x00020be8, 0x1b, 0x0d,\
	"M46PRG.BIN", 0x00021ba8, 0x1b, 0x0d,\
	"M47PRG.BIN", 0x0002da1c, 0x1b, 0x0d,\
	"M49PRG.BIN", 0x00021038, 0x1b, 0x0d,\
	"M50PRG.BIN", 0x00025970, 0x1b, 0x0d,\
	"M51PRG.BIN", 0x0002dcb4, 0x1b, 0x0d,\
	"M52PRG.BIN", 0x000214a8, 0x1b, 0x0d,\
	"M53PRG.BIN", 0x0002e9a4, 0x1b, 0x0d,\
	"M54PRG.BIN", 0x0002e324, 0x1b, 0x0d,\
	"M91PRG.BIN", 0x0001d6d0, 0x1b, 0x0d,\
	"M92PRG.BIN", 0x00016c14, 0x1b, 0x0d

//0x06089408 is num lines (make this e103)
#define PatchBattleCode3() \
	"M00PRG.BIN", 0x00030008, 0x03, 0x02,\
	"M01PRG.BIN", 0x000290a8, 0x03, 0x02,\
	"M02PRG.BIN", 0x0002d848, 0x03, 0x02,\
	"M03PRG.BIN", 0x0002e82c, 0x03, 0x02,\
	"M04PRG.BIN", 0x0002efdc, 0x03, 0x02,\
	"M05PRG.BIN", 0x0002db50, 0x03, 0x02,\
	"M26PRG.BIN", 0x0002f954, 0x03, 0x02,\
	"M27PRG.BIN", 0x0002d704, 0x03, 0x02,\
	"M29PRG.BIN", 0x00027540, 0x03, 0x02,\
	"M31PRG.BIN", 0x0002d988, 0x03, 0x02,\
	"M33PRG.BIN", 0x0002e4f8, 0x03, 0x02,\
	"M34PRG.BIN", 0x0002d5d4, 0x03, 0x02,\
	"SLGGOVER.BIN", 0x0001bf78, 0x03, 0x02,\
	"M07PRG.BIN", 0x0002e954, 0x03, 0x02,\
	"M08PRG.BIN", 0x000281d4, 0x03, 0x02,\
	"M09PRG.BIN", 0x0002ffc4, 0x03, 0x02,\
	"M10PRG.BIN", 0x00027f1c, 0x03, 0x02,\
	"M11PRG.BIN", 0x0002992c, 0x03, 0x02,\
	"M12PRG.BIN", 0x000276d8, 0x03, 0x02,\
	"M13PRG.BIN", 0x000275bc, 0x03, 0x02,\
	"M15PRG.BIN", 0x00026714, 0x03, 0x02,\
	"M16PRG.BIN", 0x0002122c, 0x03, 0x02,\
	"M17PRG.BIN", 0x00029168, 0x03, 0x02,\
	"M18PRG.BIN", 0x00026ca4, 0x03, 0x02,\
	"M19PRG.BIN", 0x00020b88, 0x03, 0x02,\
	"M20PRG.BIN", 0x0002695c, 0x03, 0x02,\
	"M21PRG.BIN", 0x0002ea18, 0x03, 0x02,\
	"M22PRG.BIN", 0x0002fdf0, 0x03, 0x02,\
	"M23PRG.BIN", 0x0002e228, 0x03, 0x02,\
	"M24PRG.BIN", 0x0002e414, 0x03, 0x02,\
	"M25PRG.BIN", 0x0002e380, 0x03, 0x02,\
	"M36PRG.BIN", 0x0002f1cc, 0x03, 0x02,\
	"M37PRG.BIN", 0x000210d0, 0x03, 0x02,\
	"M45PRG.BIN", 0x00020bf4, 0x03, 0x02,\
	"M46PRG.BIN", 0x00021bb4, 0x03, 0x02,\
	"M47PRG.BIN", 0x0002da28, 0x03, 0x02,\
	"M49PRG.BIN", 0x00021044, 0x03, 0x02,\
	"M50PRG.BIN", 0x0002597c, 0x03, 0x02,\
	"M51PRG.BIN", 0x0002dcc0, 0x03, 0x02,\
	"M52PRG.BIN", 0x000214b4, 0x03, 0x02,\
	"M53PRG.BIN", 0x0002e9b0, 0x03, 0x02,\
	"M54PRG.BIN", 0x0002e330, 0x03, 0x02,\
	"M91PRG.BIN", 0x0001d6dc, 0x03, 0x02,\
	"M92PRG.BIN", 0x00016c20, 0x03, 0x02

//0x06089450 multiplies by 0xe to get to next line write buffer, change to 0x1c
#define PatchBattleCode4()\
	"M00PRG.BIN", 0x00030050, 0x1c, 0x0e,\
	"M01PRG.BIN", 0x000290f0, 0x1c, 0x0e,\
	"M02PRG.BIN", 0x0002d890, 0x1c, 0x0e,\
	"M03PRG.BIN", 0x0002e874, 0x1c, 0x0e,\
	"M04PRG.BIN", 0x0002f024, 0x1c, 0x0e,\
	"M05PRG.BIN", 0x0002db98, 0x1c, 0x0e,\
	"M26PRG.BIN", 0x0002f99c, 0x1c, 0x0e,\
	"M27PRG.BIN", 0x0002d74c, 0x1c, 0x0e,\
	"M29PRG.BIN", 0x00027588, 0x1c, 0x0e,\
	"M31PRG.BIN", 0x0002d9d0, 0x1c, 0x0e,\
	"M33PRG.BIN", 0x0002e540, 0x1c, 0x0e,\
	"M34PRG.BIN", 0x0002d61c, 0x1c, 0x0e,\
	"SLGGOVER.BIN", 0x0001bfc0, 0x1c, 0x0e,\
	"M07PRG.BIN", 0x0002e99c, 0x1c, 0x0e,\
	"M08PRG.BIN", 0x0002821c, 0x1c, 0x0e,\
	"M09PRG.BIN", 0x0003000c, 0x1c, 0x0e,\
	"M10PRG.BIN", 0x00027f64, 0x1c, 0x0e,\
	"M11PRG.BIN", 0x00029974, 0x1c, 0x0e,\
	"M12PRG.BIN", 0x00027720, 0x1c, 0x0e,\
	"M13PRG.BIN", 0x00027604, 0x1c, 0x0e,\
	"M15PRG.BIN", 0x0002675c, 0x1c, 0x0e,\
	"M16PRG.BIN", 0x00021274, 0x1c, 0x0e,\
	"M17PRG.BIN", 0x000291b0, 0x1c, 0x0e,\
	"M18PRG.BIN", 0x00026cec, 0x1c, 0x0e,\
	"M19PRG.BIN", 0x00020bd0, 0x1c, 0x0e,\
	"M20PRG.BIN", 0x000269a4, 0x1c, 0x0e,\
	"M21PRG.BIN", 0x0002ea60, 0x1c, 0x0e,\
	"M22PRG.BIN", 0x0002fe38, 0x1c, 0x0e,\
	"M23PRG.BIN", 0x0002e270, 0x1c, 0x0e,\
	"M24PRG.BIN", 0x0002e45c, 0x1c, 0x0e,\
	"M25PRG.BIN", 0x0002e3c8, 0x1c, 0x0e,\
	"M36PRG.BIN", 0x0002f214, 0x1c, 0x0e,\
	"M37PRG.BIN", 0x00021118, 0x1c, 0x0e,\
	"M45PRG.BIN", 0x00020c3c, 0x1c, 0x0e,\
	"M46PRG.BIN", 0x00021bfc, 0x1c, 0x0e,\
	"M47PRG.BIN", 0x0002da70, 0x1c, 0x0e,\
	"M49PRG.BIN", 0x0002108c, 0x1c, 0x0e,\
	"M50PRG.BIN", 0x000259c4, 0x1c, 0x0e,\
	"M51PRG.BIN", 0x0002dd08, 0x1c, 0x0e,\
	"M52PRG.BIN", 0x000214fc, 0x1c, 0x0e,\
	"M53PRG.BIN", 0x0002e9f8, 0x1c, 0x0e,\
	"M54PRG.BIN", 0x0002e378, 0x1c, 0x0e,\
	"M91PRG.BIN", 0x0001d724, 0x1c, 0x0e,\
	"M92PRG.BIN", 0x00016c68, 0x1c, 0x0e

//060894b0 is 0xa7f.  This is the amount of bytes to clear in vdp1 when clearing the text box.
//Change this to 27*4 * (48+2)(bytes) cause 48 is 8x12/2.  Divide by two because the vdp1 buffer is cleared two bytes at a time.
#define PatchBattleCode5() \
	"M00PRG.BIN", 0x000300b0, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M01PRG.BIN", 0x00029150, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M02PRG.BIN", 0x0002d8f0, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M03PRG.BIN", 0x0002e8d4, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M04PRG.BIN", 0x0002f084, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M05PRG.BIN", 0x0002dbf8, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M26PRG.BIN", 0x0002f9fc, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M27PRG.BIN", 0x0002d7ac, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M29PRG.BIN", 0x000275e8, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M31PRG.BIN", 0x0002da30, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M33PRG.BIN", 0x0002e5a0, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M34PRG.BIN", 0x0002d67c, (27 * 4 * 50) >> 1, 0x0a7f,\
	"SLGGOVER.BIN", 0x0001c020, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M07PRG.BIN", 0x0002e9fc, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M08PRG.BIN", 0x0002827c, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M09PRG.BIN", 0x0003006c, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M10PRG.BIN", 0x00027fc4, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M11PRG.BIN", 0x000299d4, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M12PRG.BIN", 0x00027780, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M13PRG.BIN", 0x00027664, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M15PRG.BIN", 0x000267bc, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M16PRG.BIN", 0x000212d4, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M17PRG.BIN", 0x00029210, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M18PRG.BIN", 0x00026d4c, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M19PRG.BIN", 0x00020c30, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M20PRG.BIN", 0x00026a04, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M21PRG.BIN", 0x0002eac0, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M22PRG.BIN", 0x0002fe98, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M23PRG.BIN", 0x0002e2d0, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M24PRG.BIN", 0x0002e4bc, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M25PRG.BIN", 0x0002e428, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M36PRG.BIN", 0x0002f274, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M37PRG.BIN", 0x00021178, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M45PRG.BIN", 0x00020c9c, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M46PRG.BIN", 0x00021c5c, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M47PRG.BIN", 0x0002dad0, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M49PRG.BIN", 0x000210ec, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M50PRG.BIN", 0x00025a24, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M51PRG.BIN", 0x0002dd68, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M52PRG.BIN", 0x0002155c, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M53PRG.BIN", 0x0002ea58, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M54PRG.BIN", 0x0002e3d8, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M91PRG.BIN", 0x0001d784, (27 * 4 * 50) >> 1, 0x0a7f,\
	"M92PRG.BIN", 0x00016cc8, (27 * 4 * 50) >> 1, 0x0a7f

//060892a2 change to 0x1a
#define PatchBattleCode6()\
	"M00PRG.BIN", 0x0002fc5c, 0x1b, 0x0d,\
	"M00PRG.BIN", 0x0002fd76, 0x1b, 0x0d,\
	"M00PRG.BIN", 0x0002fea2, 0x1b, 0x0d,\
	"M01PRG.BIN", 0x00028cfc, 0x1b, 0x0d,\
	"M01PRG.BIN", 0x00028e16, 0x1b, 0x0d,\
	"M01PRG.BIN", 0x00028f42, 0x1b, 0x0d,\
	"M02PRG.BIN", 0x0002d49c, 0x1b, 0x0d,\
	"M02PRG.BIN", 0x0002d5b6, 0x1b, 0x0d,\
	"M02PRG.BIN", 0x0002d6e2, 0x1b, 0x0d,\
	"M03PRG.BIN", 0x0002e480, 0x1b, 0x0d,\
	"M03PRG.BIN", 0x0002e59a, 0x1b, 0x0d,\
	"M03PRG.BIN", 0x0002e6c6, 0x1b, 0x0d,\
	"M04PRG.BIN", 0x0002ec30, 0x1b, 0x0d,\
	"M04PRG.BIN", 0x0002ed4a, 0x1b, 0x0d,\
	"M04PRG.BIN", 0x0002ee76, 0x1b, 0x0d,\
	"M05PRG.BIN", 0x0002d7a4, 0x1b, 0x0d,\
	"M05PRG.BIN", 0x0002d8be, 0x1b, 0x0d,\
	"M05PRG.BIN", 0x0002d9ea, 0x1b, 0x0d,\
	"M26PRG.BIN", 0x0002f5a8, 0x1b, 0x0d,\
	"M26PRG.BIN", 0x0002f6c2, 0x1b, 0x0d,\
	"M26PRG.BIN", 0x0002f7ee, 0x1b, 0x0d,\
	"M27PRG.BIN", 0x0002d358, 0x1b, 0x0d,\
	"M27PRG.BIN", 0x0002d472, 0x1b, 0x0d,\
	"M27PRG.BIN", 0x0002d59e, 0x1b, 0x0d,\
	"M29PRG.BIN", 0x00027194, 0x1b, 0x0d,\
	"M29PRG.BIN", 0x000272ae, 0x1b, 0x0d,\
	"M29PRG.BIN", 0x000273da, 0x1b, 0x0d,\
	"M31PRG.BIN", 0x0002d5dc, 0x1b, 0x0d,\
	"M31PRG.BIN", 0x0002d6f6, 0x1b, 0x0d,\
	"M31PRG.BIN", 0x0002d822, 0x1b, 0x0d,\
	"M33PRG.BIN", 0x0002e14c, 0x1b, 0x0d,\
	"M33PRG.BIN", 0x0002e266, 0x1b, 0x0d,\
	"M33PRG.BIN", 0x0002e392, 0x1b, 0x0d,\
	"M34PRG.BIN", 0x0002d228, 0x1b, 0x0d,\
	"M34PRG.BIN", 0x0002d342, 0x1b, 0x0d,\
	"M34PRG.BIN", 0x0002d46e, 0x1b, 0x0d,\
	"SLGGOVER.BIN", 0x0001bbcc, 0x1b, 0x0d,\
	"SLGGOVER.BIN", 0x0001bce6, 0x1b, 0x0d,\
	"SLGGOVER.BIN", 0x0001be12, 0x1b, 0x0d,\
	"M07PRG.BIN", 0x0002e5a8, 0x1b, 0x0d,\
	"M07PRG.BIN", 0x0002e6c2, 0x1b, 0x0d,\
	"M07PRG.BIN", 0x0002e7ee, 0x1b, 0x0d,\
	"M08PRG.BIN", 0x00027e28, 0x1b, 0x0d,\
	"M08PRG.BIN", 0x00027f42, 0x1b, 0x0d,\
	"M08PRG.BIN", 0x0002806e, 0x1b, 0x0d,\
	"M09PRG.BIN", 0x0002fc18, 0x1b, 0x0d,\
	"M09PRG.BIN", 0x0002fd32, 0x1b, 0x0d,\
	"M09PRG.BIN", 0x0002fe5e, 0x1b, 0x0d,\
	"M10PRG.BIN", 0x00027b70, 0x1b, 0x0d,\
	"M10PRG.BIN", 0x00027c8a, 0x1b, 0x0d,\
	"M10PRG.BIN", 0x00027db6, 0x1b, 0x0d,\
	"M11PRG.BIN", 0x00029580, 0x1b, 0x0d,\
	"M11PRG.BIN", 0x0002969a, 0x1b, 0x0d,\
	"M11PRG.BIN", 0x000297c6, 0x1b, 0x0d,\
	"M12PRG.BIN", 0x0002732c, 0x1b, 0x0d,\
	"M12PRG.BIN", 0x00027446, 0x1b, 0x0d,\
	"M12PRG.BIN", 0x00027572, 0x1b, 0x0d,\
	"M13PRG.BIN", 0x00027210, 0x1b, 0x0d,\
	"M13PRG.BIN", 0x0002732a, 0x1b, 0x0d,\
	"M13PRG.BIN", 0x00027456, 0x1b, 0x0d,\
	"M15PRG.BIN", 0x00026368, 0x1b, 0x0d,\
	"M15PRG.BIN", 0x00026482, 0x1b, 0x0d,\
	"M15PRG.BIN", 0x000265ae, 0x1b, 0x0d,\
	"M16PRG.BIN", 0x00020e80, 0x1b, 0x0d,\
	"M16PRG.BIN", 0x00020f9a, 0x1b, 0x0d,\
	"M16PRG.BIN", 0x000210c6, 0x1b, 0x0d,\
	"M17PRG.BIN", 0x00028dbc, 0x1b, 0x0d,\
	"M17PRG.BIN", 0x00028ed6, 0x1b, 0x0d,\
	"M17PRG.BIN", 0x00029002, 0x1b, 0x0d,\
	"M18PRG.BIN", 0x000268f8, 0x1b, 0x0d,\
	"M18PRG.BIN", 0x00026a12, 0x1b, 0x0d,\
	"M18PRG.BIN", 0x00026b3e, 0x1b, 0x0d,\
	"M19PRG.BIN", 0x000207dc, 0x1b, 0x0d,\
	"M19PRG.BIN", 0x000208f6, 0x1b, 0x0d,\
	"M19PRG.BIN", 0x00020a22, 0x1b, 0x0d,\
	"M20PRG.BIN", 0x000265b0, 0x1b, 0x0d,\
	"M20PRG.BIN", 0x000266ca, 0x1b, 0x0d,\
	"M20PRG.BIN", 0x000267f6, 0x1b, 0x0d,\
	"M21PRG.BIN", 0x0002e66c, 0x1b, 0x0d,\
	"M21PRG.BIN", 0x0002e786, 0x1b, 0x0d,\
	"M21PRG.BIN", 0x0002e8b2, 0x1b, 0x0d,\
	"M22PRG.BIN", 0x0002fa44, 0x1b, 0x0d,\
	"M22PRG.BIN", 0x0002fb5e, 0x1b, 0x0d,\
	"M22PRG.BIN", 0x0002fc8a, 0x1b, 0x0d,\
	"M23PRG.BIN", 0x0002de7c, 0x1b, 0x0d,\
	"M23PRG.BIN", 0x0002df96, 0x1b, 0x0d,\
	"M23PRG.BIN", 0x0002e0c2, 0x1b, 0x0d,\
	"M24PRG.BIN", 0x0002e068, 0x1b, 0x0d,\
	"M24PRG.BIN", 0x0002e182, 0x1b, 0x0d,\
	"M24PRG.BIN", 0x0002e2ae, 0x1b, 0x0d,\
	"M25PRG.BIN", 0x0002dfd4, 0x1b, 0x0d,\
	"M25PRG.BIN", 0x0002e0ee, 0x1b, 0x0d,\
	"M25PRG.BIN", 0x0002e21a, 0x1b, 0x0d,\
	"M36PRG.BIN", 0x0002ee20, 0x1b, 0x0d,\
	"M36PRG.BIN", 0x0002ef3a, 0x1b, 0x0d,\
	"M36PRG.BIN", 0x0002f066, 0x1b, 0x0d,\
	"M37PRG.BIN", 0x00020d24, 0x1b, 0x0d,\
	"M37PRG.BIN", 0x00020e3e, 0x1b, 0x0d,\
	"M37PRG.BIN", 0x00020f6a, 0x1b, 0x0d,\
	"M45PRG.BIN", 0x00020848, 0x1b, 0x0d,\
	"M45PRG.BIN", 0x00020962, 0x1b, 0x0d,\
	"M45PRG.BIN", 0x00020a8e, 0x1b, 0x0d,\
	"M46PRG.BIN", 0x00021808, 0x1b, 0x0d,\
	"M46PRG.BIN", 0x00021922, 0x1b, 0x0d,\
	"M46PRG.BIN", 0x00021a4e, 0x1b, 0x0d,\
	"M47PRG.BIN", 0x0002d67c, 0x1b, 0x0d,\
	"M47PRG.BIN", 0x0002d796, 0x1b, 0x0d,\
	"M47PRG.BIN", 0x0002d8c2, 0x1b, 0x0d,\
	"M49PRG.BIN", 0x00020c98, 0x1b, 0x0d,\
	"M49PRG.BIN", 0x00020db2, 0x1b, 0x0d,\
	"M49PRG.BIN", 0x00020ede, 0x1b, 0x0d,\
	"M50PRG.BIN", 0x000255d0, 0x1b, 0x0d,\
	"M50PRG.BIN", 0x000256ea, 0x1b, 0x0d,\
	"M50PRG.BIN", 0x00025816, 0x1b, 0x0d,\
	"M51PRG.BIN", 0x0002d914, 0x1b, 0x0d,\
	"M51PRG.BIN", 0x0002da2e, 0x1b, 0x0d,\
	"M51PRG.BIN", 0x0002db5a, 0x1b, 0x0d,\
	"M52PRG.BIN", 0x00021108, 0x1b, 0x0d,\
	"M52PRG.BIN", 0x00021222, 0x1b, 0x0d,\
	"M52PRG.BIN", 0x0002134e, 0x1b, 0x0d,\
	"M53PRG.BIN", 0x0002e604, 0x1b, 0x0d,\
	"M53PRG.BIN", 0x0002e71e, 0x1b, 0x0d,\
	"M53PRG.BIN", 0x0002e84a, 0x1b, 0x0d,\
	"M54PRG.BIN", 0x0002df84, 0x1b, 0x0d,\
	"M54PRG.BIN", 0x0002e09e, 0x1b, 0x0d,\
	"M54PRG.BIN", 0x0002e1ca, 0x1b, 0x0d,\
	"M91PRG.BIN", 0x0001d330, 0x1b, 0x0d,\
	"M91PRG.BIN", 0x0001d44a, 0x1b, 0x0d,\
	"M91PRG.BIN", 0x0001d576, 0x1b, 0x0d,\
	"M92PRG.BIN", 0x00016874, 0x1b, 0x0d,\
	"M92PRG.BIN", 0x0001698e, 0x1b, 0x0d,\
	"M92PRG.BIN", 0x00016aba, 0x1b, 0x0d

//0608903c and 06089282 change to 0x03 (number of lines)
#define PatchBattleCode7()\
	"M00PRG.BIN", 0x0002fc3c, 0x03, 0x02, \
	"M00PRG.BIN", 0x0002fe82, 0x03, 0x02, \
	"M01PRG.BIN", 0x00028cdc, 0x03, 0x02, \
	"M01PRG.BIN", 0x00028f22, 0x03, 0x02, \
	"M02PRG.BIN", 0x0002d47c, 0x03, 0x02, \
	"M02PRG.BIN", 0x0002d6c2, 0x03, 0x02, \
	"M03PRG.BIN", 0x0002e460, 0x03, 0x02, \
	"M03PRG.BIN", 0x0002e6a6, 0x03, 0x02, \
	"M04PRG.BIN", 0x0002ec10, 0x03, 0x02, \
	"M04PRG.BIN", 0x0002ee56, 0x03, 0x02, \
	"M05PRG.BIN", 0x0002d784, 0x03, 0x02, \
	"M05PRG.BIN", 0x0002d9ca, 0x03, 0x02, \
	"M26PRG.BIN", 0x0002f588, 0x03, 0x02, \
	"M26PRG.BIN", 0x0002f7ce, 0x03, 0x02, \
	"M27PRG.BIN", 0x0002d338, 0x03, 0x02, \
	"M27PRG.BIN", 0x0002d57e, 0x03, 0x02, \
	"M29PRG.BIN", 0x00027174, 0x03, 0x02, \
	"M29PRG.BIN", 0x000273ba, 0x03, 0x02, \
	"M31PRG.BIN", 0x0002d5bc, 0x03, 0x02, \
	"M31PRG.BIN", 0x0002d802, 0x03, 0x02, \
	"M33PRG.BIN", 0x0002e12c, 0x03, 0x02, \
	"M33PRG.BIN", 0x0002e372, 0x03, 0x02, \
	"M34PRG.BIN", 0x0002d208, 0x03, 0x02, \
	"M34PRG.BIN", 0x0002d44e, 0x03, 0x02, \
	"SLGGOVER.BIN", 0x0001bbac, 0x03, 0x02, \
	"SLGGOVER.BIN", 0x0001bdf2, 0x03, 0x02, \
	"M07PRG.BIN", 0x0002e588, 0x03, 0x02, \
	"M07PRG.BIN", 0x0002e7ce, 0x03, 0x02, \
	"M08PRG.BIN", 0x00027e08, 0x03, 0x02, \
	"M08PRG.BIN", 0x0002804e, 0x03, 0x02, \
	"M09PRG.BIN", 0x0002fbf8, 0x03, 0x02, \
	"M09PRG.BIN", 0x0002fe3e, 0x03, 0x02, \
	"M10PRG.BIN", 0x00027b50, 0x03, 0x02, \
	"M10PRG.BIN", 0x00027d96, 0x03, 0x02, \
	"M11PRG.BIN", 0x00029560, 0x03, 0x02, \
	"M11PRG.BIN", 0x000297a6, 0x03, 0x02, \
	"M12PRG.BIN", 0x0002730c, 0x03, 0x02, \
	"M12PRG.BIN", 0x00027552, 0x03, 0x02, \
	"M13PRG.BIN", 0x000271f0, 0x03, 0x02, \
	"M13PRG.BIN", 0x00027436, 0x03, 0x02, \
	"M15PRG.BIN", 0x00026348, 0x03, 0x02, \
	"M15PRG.BIN", 0x0002658e, 0x03, 0x02, \
	"M16PRG.BIN", 0x00020e60, 0x03, 0x02, \
	"M16PRG.BIN", 0x000210a6, 0x03, 0x02, \
	"M17PRG.BIN", 0x00028d9c, 0x03, 0x02, \
	"M17PRG.BIN", 0x00028fe2, 0x03, 0x02, \
	"M18PRG.BIN", 0x000268d8, 0x03, 0x02, \
	"M18PRG.BIN", 0x00026b1e, 0x03, 0x02, \
	"M19PRG.BIN", 0x000207bc, 0x03, 0x02, \
	"M19PRG.BIN", 0x00020a02, 0x03, 0x02, \
	"M20PRG.BIN", 0x00026590, 0x03, 0x02, \
	"M20PRG.BIN", 0x000267d6, 0x03, 0x02, \
	"M21PRG.BIN", 0x0002e64c, 0x03, 0x02, \
	"M21PRG.BIN", 0x0002e892, 0x03, 0x02, \
	"M22PRG.BIN", 0x0002fa24, 0x03, 0x02, \
	"M22PRG.BIN", 0x0002fc6a, 0x03, 0x02, \
	"M23PRG.BIN", 0x0002de5c, 0x03, 0x02, \
	"M23PRG.BIN", 0x0002e0a2, 0x03, 0x02, \
	"M24PRG.BIN", 0x0002e048, 0x03, 0x02, \
	"M24PRG.BIN", 0x0002e28e, 0x03, 0x02, \
	"M25PRG.BIN", 0x0002dfb4, 0x03, 0x02, \
	"M25PRG.BIN", 0x0002e1fa, 0x03, 0x02, \
	"M36PRG.BIN", 0x0002ee00, 0x03, 0x02, \
	"M36PRG.BIN", 0x0002f046, 0x03, 0x02, \
	"M37PRG.BIN", 0x00020d04, 0x03, 0x02, \
	"M37PRG.BIN", 0x00020f4a, 0x03, 0x02, \
	"M45PRG.BIN", 0x00020828, 0x03, 0x02, \
	"M45PRG.BIN", 0x00020a6e, 0x03, 0x02, \
	"M46PRG.BIN", 0x000217e8, 0x03, 0x02, \
	"M46PRG.BIN", 0x00021a2e, 0x03, 0x02, \
	"M47PRG.BIN", 0x0002d65c, 0x03, 0x02, \
	"M47PRG.BIN", 0x0002d8a2, 0x03, 0x02, \
	"M49PRG.BIN", 0x00020c78, 0x03, 0x02, \
	"M49PRG.BIN", 0x00020ebe, 0x03, 0x02, \
	"M50PRG.BIN", 0x000255b0, 0x03, 0x02, \
	"M50PRG.BIN", 0x000257f6, 0x03, 0x02, \
	"M51PRG.BIN", 0x0002d8f4, 0x03, 0x02, \
	"M51PRG.BIN", 0x0002db3a, 0x03, 0x02, \
	"M52PRG.BIN", 0x000210e8, 0x03, 0x02, \
	"M52PRG.BIN", 0x0002132e, 0x03, 0x02, \
	"M53PRG.BIN", 0x0002e5e4, 0x03, 0x02, \
	"M53PRG.BIN", 0x0002e82a, 0x03, 0x02, \
	"M54PRG.BIN", 0x0002df64, 0x03, 0x02, \
	"M54PRG.BIN", 0x0002e1aa, 0x03, 0x02, \
	"M91PRG.BIN", 0x0001d310, 0x03, 0x02, \
	"M91PRG.BIN", 0x0001d556, 0x03, 0x02, \
	"M92PRG.BIN", 0x00016854, 0x03, 0x02, \
	"M92PRG.BIN", 0x00016a9a, 0x03, 0x02

// 0608914c
#define PatchBattleCode8()\
	"M00PRG.BIN", 0x0002fd4c, 0x03, 0x02, \
	"M01PRG.BIN", 0x00028dec, 0x03, 0x02, \
	"M02PRG.BIN", 0x0002d58c, 0x03, 0x02, \
	"M03PRG.BIN", 0x0002e570, 0x03, 0x02, \
	"M04PRG.BIN", 0x0002ed20, 0x03, 0x02, \
	"M05PRG.BIN", 0x0002d894, 0x03, 0x02, \
	"M26PRG.BIN", 0x0002f698, 0x03, 0x02, \
	"M27PRG.BIN", 0x0002d448, 0x03, 0x02, \
	"M29PRG.BIN", 0x00027284, 0x03, 0x02, \
	"M31PRG.BIN", 0x0002d6cc, 0x03, 0x02, \
	"M33PRG.BIN", 0x0002e23c, 0x03, 0x02, \
	"M34PRG.BIN", 0x0002d318, 0x03, 0x02, \
	"SLGGOVER.BIN", 0x0001bcbc, 0x03, 0x02, \
	"M07PRG.BIN", 0x0002e698, 0x03, 0x02, \
	"M08PRG.BIN", 0x00027f18, 0x03, 0x02, \
	"M09PRG.BIN", 0x0002fd08, 0x03, 0x02, \
	"M10PRG.BIN", 0x00027c60, 0x03, 0x02, \
	"M11PRG.BIN", 0x00029670, 0x03, 0x02, \
	"M12PRG.BIN", 0x0002741c, 0x03, 0x02, \
	"M13PRG.BIN", 0x00027300, 0x03, 0x02, \
	"M15PRG.BIN", 0x00026458, 0x03, 0x02, \
	"M16PRG.BIN", 0x00020f70, 0x03, 0x02, \
	"M17PRG.BIN", 0x00028eac, 0x03, 0x02, \
	"M18PRG.BIN", 0x000269e8, 0x03, 0x02, \
	"M19PRG.BIN", 0x000208cc, 0x03, 0x02, \
	"M20PRG.BIN", 0x000266a0, 0x03, 0x02, \
	"M21PRG.BIN", 0x0002e75c, 0x03, 0x02, \
	"M22PRG.BIN", 0x0002fb34, 0x03, 0x02, \
	"M23PRG.BIN", 0x0002df6c, 0x03, 0x02, \
	"M24PRG.BIN", 0x0002e158, 0x03, 0x02, \
	"M25PRG.BIN", 0x0002e0c4, 0x03, 0x02, \
	"M36PRG.BIN", 0x0002ef10, 0x03, 0x02, \
	"M37PRG.BIN", 0x00020e14, 0x03, 0x02, \
	"M45PRG.BIN", 0x00020938, 0x03, 0x02, \
	"M46PRG.BIN", 0x000218f8, 0x03, 0x02, \
	"M47PRG.BIN", 0x0002d76c, 0x03, 0x02, \
	"M49PRG.BIN", 0x00020d88, 0x03, 0x02, \
	"M50PRG.BIN", 0x000256c0, 0x03, 0x02, \
	"M51PRG.BIN", 0x0002da04, 0x03, 0x02, \
	"M52PRG.BIN", 0x000211f8, 0x03, 0x02, \
	"M53PRG.BIN", 0x0002e6f4, 0x03, 0x02, \
	"M54PRG.BIN", 0x0002e074, 0x03, 0x02, \
	"M91PRG.BIN", 0x0001d420, 0x03, 0x02, \
	"M92PRG.BIN", 0x00016964, 0x03, 0x02

/*Lips spacing
	060d146c is calculation to center text.Set this to 4200 (shll r2)
	060d14ef is vertical offset.Change to 0x93
	060d1503 is X spacing.Set this to 0x08
	060d16bf is another horizontal offset.Set this to f8
*/
#define PatchLipsSpacing()\
	"EV00001.BIN", 0x000071f0, 0xffff, 0, \
	"EV00002.BIN", 0x00005af4, 0xffff, 0, \
	"EV00050.BIN", 0x00005af4, 0xffff, 0, \
	"EV00051.BIN", 0x00005af4, 0xffff, 0, \
	"EV00052.BIN", 0x00005af4, 0xffff, 0, \
	"EV00054.BIN", 0x00005af4, 0xffff, 0, \
	"EV00055.BIN", 0x00005af4, 0xffff, 0, \
	"EV00060.BIN", 0x00005af4, 0xffff, 0, \
	"EV01001.BIN", 0x00005c00, 0xffff, 0, \
	"EV01002.BIN", 0x00005af4, 0xffff, 0, \
	"EV01020.BIN", 0x00006118, 0xffff, 0, \
	"EV01021.BIN", 0x00005f2c, 0xffff, 0, \
	"EV01022.BIN", 0x00005af4, 0xffff, 0, \
	"EV01023.BIN", 0x00005af4, 0xffff, 0, \
	"EV01030.BIN", 0x00005af4, 0xffff, 0, \
	"EV01050.BIN", 0x00005af4, 0xffff, 0, \
	"EV01054.BIN", 0x00005af4, 0xffff, 0, \
	"EV01055.BIN", 0x00005af4, 0xffff, 0, \
	"EV02001.BIN", 0x00005af4, 0xffff, 0, \
	"EV02002.BIN", 0x00005af4, 0xffff, 0, \
	"EV02010.BIN", 0x00005af4, 0xffff, 0, \
	"EV02021.BIN", 0x00005af4, 0xffff, 0, \
	"EV02025.BIN", 0x00005af4, 0xffff, 0, \
	"EV02050.BIN", 0x00005af4, 0xffff, 0, \
	"EV02051.BIN", 0x00005af4, 0xffff, 0, \
	"EV02052.BIN", 0x00005af4, 0xffff, 0, \
	"EV02053.BIN", 0x00005af4, 0xffff, 0, \
	"EV02054.BIN", 0x00005af4, 0xffff, 0, \
	"EV03001.BIN", 0x00005cc0, 0xffff, 0, \
	"EV03002.BIN", 0x00005b68, 0xffff, 0, \
	"EV03005.BIN", 0x00005b20, 0xffff, 0, \
	"EV03010.BIN", 0x00005b04, 0xffff, 0, \
	"EV03020.BIN", 0x00005b04, 0xffff, 0, \
	"EV03021.BIN", 0x00005b04, 0xffff, 0, \
	"EV03023.BIN", 0x00005b1c, 0xffff, 0, \
	"EV03024.BIN", 0x00005b1c, 0xffff, 0, \
	"EV03025.BIN", 0x00005b30, 0xffff, 0, \
	"EV03050.BIN", 0x00005f34, 0xffff, 0, \
	"EV03051.BIN", 0x00005b58, 0xffff, 0, \
	"EV03052.BIN", 0x00005bc4, 0xffff, 0, \
	"EV03053.BIN", 0x00005b80, 0xffff, 0, \
	"EV04001.BIN", 0x000069ac, 0xffff, 0, \
	"EV04002.BIN", 0x00005b70, 0xffff, 0, \
	"EV04003.BIN", 0x00005ca4, 0xffff, 0, \
	"EV04005.BIN", 0x00005af4, 0xffff, 0, \
	"EV04010.BIN", 0x00005b68, 0xffff, 0, \
	"EV04020.BIN", 0x00005af4, 0xffff, 0, \
	"EV04022.BIN", 0x00005afc, 0xffff, 0, \
	"EV04050.BIN", 0x00005b9c, 0xffff, 0, \
	"EV04053.BIN", 0x00005af4, 0xffff, 0, \
	"EV04055.BIN", 0x00005b68, 0xffff, 0, \
	"EV05001.BIN", 0x00005b0c, 0xffff, 0, \
	"EV05002.BIN", 0x00005af4, 0xffff, 0, \
	"EV05003.BIN", 0x00005cd0, 0xffff, 0, \
	"EV05004.BIN", 0x00005af4, 0xffff, 0, \
	"EV05005.BIN", 0x00005af4, 0xffff, 0, \
	"EV05007.BIN", 0x00005af4, 0xffff, 0, \
	"EV05010.BIN", 0x00005af4, 0xffff, 0, \
	"EV05011.BIN", 0x00005af4, 0xffff, 0, \
	"EV05018.BIN", 0x00005af4, 0xffff, 0, \
	"EV05019.BIN", 0x00005b1c, 0xffff, 0, \
	"EV05020.BIN", 0x00005af4, 0xffff, 0, \
	"EV05021.BIN", 0x00005af4, 0xffff, 0, \
	"EV05022.BIN", 0x00005af4, 0xffff, 0, \
	"EV05023.BIN", 0x00005af4, 0xffff, 0, \
	"EV05025.BIN", 0x00005af4, 0xffff, 0, \
	"EV05026.BIN", 0x00005af4, 0xffff, 0, \
	"EV05027.BIN", 0x00005af4, 0xffff, 0, \
	"EV05051.BIN", 0x00005af4, 0xffff, 0, \
	"EV05052.BIN", 0x00005af4, 0xffff, 0, \
	"EV05053.BIN", 0x00005af4, 0xffff, 0, \
	"EV05054.BIN", 0x00005af4, 0xffff, 0, \
	"EV05060.BIN", 0x00005af4, 0xffff, 0, \
	"EV26001.BIN", 0x00006900, 0xffff, 0, \
	"EV26002.BIN", 0x00005e9c, 0xffff, 0, \
	"EV26020.BIN", 0x00005af4, 0xffff, 0, \
	"EV26021.BIN", 0x00005af4, 0xffff, 0, \
	"EV27001.BIN", 0x00005b00, 0xffff, 0, \
	"EV27002.BIN", 0x00005ba8, 0xffff, 0, \
	"EV27054.BIN", 0x00005af4, 0xffff, 0, \
	"EV28001.BIN", 0x00005af4, 0xffff, 0, \
	"EV28025.BIN", 0x00005af4, 0xffff, 0, \
	"EV28050.BIN", 0x00005af4, 0xffff, 0, \
	"EV28051.BIN", 0x00005af4, 0xffff, 0, \
	"EV29001.BIN", 0x00005b18, 0xffff, 0, \
	"EV29002.BIN", 0x00005b18, 0xffff, 0, \
	"EV29021.BIN", 0x00005af4, 0xffff, 0, \
	"EV29050.BIN", 0x00005af4, 0xffff, 0, \
	"EV30003.BIN", 0x00006b3c, 0xffff, 0, \
	"EV30020.BIN", 0x00005b04, 0xffff, 0, \
	"EV30026.BIN", 0x00005b04, 0xffff, 0, \
	"EV30027.BIN", 0x00005b04, 0xffff, 0, \
	"EV31001.BIN", 0x00005d9c, 0xffff, 0, \
	"EV31002.BIN", 0x00005cf4, 0xffff, 0, \
	"EV31023.BIN", 0x00005b1c, 0xffff, 0, \
	"EV31024.BIN", 0x00005b1c, 0xffff, 0, \
	"EV32002.BIN", 0x00005b28, 0xffff, 0, \
	"EV32020.BIN", 0x00005af4, 0xffff, 0, \
	"EV32055.BIN", 0x00005b68, 0xffff, 0, \
	"EV33001.BIN", 0x00006048, 0xffff, 0, \
	"EV33002.BIN", 0x00005db0, 0xffff, 0, \
	"EV33022.BIN", 0x00005afc, 0xffff, 0, \
	"EV33054.BIN", 0x00005af4, 0xffff, 0, \
	"EV34001.BIN", 0x00005b18, 0xffff, 0, \
	"EV34002.BIN", 0x00005b30, 0xffff, 0, \
	"EV34003.BIN", 0x00005b18, 0xffff, 0, \
	"EV34004.BIN", 0x00005b70, 0xffff, 0, \
	"EV34005.BIN", 0x00005b70, 0xffff, 0, \
	"EV34006.BIN", 0x00005ba8, 0xffff, 0, \
	"EV34007.BIN", 0x00005b18, 0xffff, 0, \
	"EV34008.BIN", 0x00005ba8, 0xffff, 0, \
	"EV34020.BIN", 0x00005af4, 0xffff, 0, \
	"EV34030.BIN", 0x00005af4, 0xffff, 0, \
	"EV34040.BIN", 0x00005af4, 0xffff, 0, \
	"EV34041.BIN", 0x00005af4, 0xffff, 0, \
	"EV34042.BIN", 0x00005af4, 0xffff, 0, \
	"EV34043.BIN", 0x00005af4, 0xffff, 0, \
	"EV34044.BIN", 0x00005af4, 0xffff, 0, \
	"M00PRG.BIN", 0x000795f0, 0xffff, 0, \
	"M26PRG.BIN", 0x00078d00, 0xffff, 0, \
	"TUTORI0.BIN", 0x00005c6c, 0xffff, 0, \
	"EV07000.BIN", 0x000057c8, 0xffff, 0, \
	"EV07001.BIN", 0x00005fbc, 0xffff, 0, \
	"EV07002.BIN", 0x00005b04, 0xffff, 0, \
	"EV07003.BIN", 0x00005b0c, 0xffff, 0, \
	"EV07005.BIN", 0x00005b30, 0xffff, 0, \
	"EV07006.BIN", 0x00005b40, 0xffff, 0, \
	"EV07007.BIN", 0x00005b30, 0xffff, 0, \
	"EV07008.BIN", 0x00005b40, 0xffff, 0, \
	"EV07009.BIN", 0x00005b18, 0xffff, 0, \
	"EV07020.BIN", 0x00005b04, 0xffff, 0, \
	"EV07021.BIN", 0x00005b04, 0xffff, 0, \
	"EV08000.BIN", 0x000057c8, 0xffff, 0, \
	"EV08001.BIN", 0x00006528, 0xffff, 0, \
	"EV08002.BIN", 0x000068c8, 0xffff, 0, \
	"EV08003.BIN", 0x00005b84, 0xffff, 0, \
	"EV08020.BIN", 0x00005b50, 0xffff, 0, \
	"EV08021.BIN", 0x00005b04, 0xffff, 0, \
	"EV08030.BIN", 0x00005b04, 0xffff, 0, \
	"EV08032.BIN", 0x00005cb4, 0xffff, 0, \
	"EV08033.BIN", 0x00005ca0, 0xffff, 0, \
	"EV08050.BIN", 0x00005b04, 0xffff, 0, \
	"EV08060.BIN", 0x00006ee0, 0xffff, 0, \
	"EV09001.BIN", 0x00006144, 0xffff, 0, \
	"EV09002.BIN", 0x00005b1c, 0xffff, 0, \
	"EV09005.BIN", 0x000061c4, 0xffff, 0, \
	"EV09006.BIN", 0x00005be0, 0xffff, 0, \
	"EV09007.BIN", 0x00005ca8, 0xffff, 0, \
	"EV09010.BIN", 0x00005b4c, 0xffff, 0, \
	"EV09011.BIN", 0x00006108, 0xffff, 0, \
	"EV09025.BIN", 0x00006124, 0xffff, 0, \
	"EV09026.BIN", 0x00006108, 0xffff, 0, \
	"EV09030.BIN", 0x00005b1c, 0xffff, 0, \
	"EV09031.BIN", 0x00005b1c, 0xffff, 0, \
	"EV09035.BIN", 0x00005b40, 0xffff, 0, \
	"EV09041.BIN", 0x00005b00, 0xffff, 0, \
	"EV09042.BIN", 0x00005b00, 0xffff, 0, \
	"EV09043.BIN", 0x00005b00, 0xffff, 0, \
	"EV09044.BIN", 0x00005b00, 0xffff, 0, \
	"EV09045.BIN", 0x00005b00, 0xffff, 0, \
	"EV09046.BIN", 0x00005b00, 0xffff, 0, \
	"EV09048.BIN", 0x00005b00, 0xffff, 0, \
	"EV10001.BIN", 0x00005c40, 0xffff, 0, \
	"EV10002.BIN", 0x00006038, 0xffff, 0, \
	"EV10020.BIN", 0x00005af4, 0xffff, 0, \
	"EV10025.BIN", 0x00005d48, 0xffff, 0, \
	"EV10026.BIN", 0x0000627c, 0xffff, 0, \
	"EV10030.BIN", 0x00005af4, 0xffff, 0, \
	"EV10035.BIN", 0x00005af4, 0xffff, 0, \
	"EV11001.BIN", 0x00005eb4, 0xffff, 0, \
	"EV11002.BIN", 0x000078a4, 0xffff, 0, \
	"EV11030.BIN", 0x00005af4, 0xffff, 0, \
	"EV11040.BIN", 0x00005c48, 0xffff, 0, \
	"EV12001.BIN", 0x00005d9c, 0xffff, 0, \
	"EV12002.BIN", 0x00005ba0, 0xffff, 0, \
	"EV12035.BIN", 0x00005bc0, 0xffff, 0, \
	"EV13001.BIN", 0x00005de8, 0xffff, 0, \
	"EV13002.BIN", 0x00005af4, 0xffff, 0, \
	"EV13035.BIN", 0x00005bc0, 0xffff, 0, \
	"EV15001.BIN", 0x00005c30, 0xffff, 0, \
	"EV15002.BIN", 0x0000642c, 0xffff, 0, \
	"EV16001.BIN", 0x00005af0, 0xffff, 0, \
	"EV16002.BIN", 0x00005af0, 0xffff, 0, \
	"EV16035.BIN", 0x00005b48, 0xffff, 0, \
	"EV16051.BIN", 0x0000609c, 0xffff, 0, \
	"EV16052.BIN", 0x00005af0, 0xffff, 0, \
	"EV16053.BIN", 0x00006078, 0xffff, 0, \
	"EV17001.BIN", 0x00005ca8, 0xffff, 0, \
	"EV17002.BIN", 0x00007a34, 0xffff, 0, \
	"EV17010.BIN", 0x000060cc, 0xffff, 0, \
	"EV17030.BIN", 0x00005af4, 0xffff, 0, \
	"EV17031.BIN", 0x00005af4, 0xffff, 0, \
	"EV17035.BIN", 0x00005b80, 0xffff, 0, \
	"EV18001.BIN", 0x00006b9c, 0xffff, 0, \
	"EV18002.BIN", 0x00006028, 0xffff, 0, \
	"EV18020.BIN", 0x00005b04, 0xffff, 0, \
	"EV18030.BIN", 0x00005bb4, 0xffff, 0, \
	"EV18040.BIN", 0x000062b4, 0xffff, 0, \
	"EV19001.BIN", 0x00005bc8, 0xffff, 0, \
	"EV19002.BIN", 0x00005af4, 0xffff, 0, \
	"EV19003.BIN", 0x00005bb0, 0xffff, 0, \
	"EV19020.BIN", 0x00005af4, 0xffff, 0, \
	"EV19021.BIN", 0x00005af4, 0xffff, 0, \
	"EV19022.BIN", 0x00005af4, 0xffff, 0, \
	"EV19023.BIN", 0x00005af4, 0xffff, 0, \
	"EV19041.BIN", 0x00005af4, 0xffff, 0, \
	"EV19042.BIN", 0x00005af4, 0xffff, 0, \
	"EV19043.BIN", 0x00005af4, 0xffff, 0, \
	"EV19044.BIN", 0x00005af4, 0xffff, 0, \
	"EV19045.BIN", 0x00005af4, 0xffff, 0, \
	"EV19046.BIN", 0x00005af4, 0xffff, 0, \
	"EV19047.BIN", 0x00005af4, 0xffff, 0, \
	"EV19048.BIN", 0x00005af4, 0xffff, 0, \
	"EV20001.BIN", 0x00005c60, 0xffff, 0, \
	"EV20002.BIN", 0x00005b04, 0xffff, 0, \
	"EV20003.BIN", 0x00005c20, 0xffff, 0, \
	"EV20020.BIN", 0x00005b24, 0xffff, 0, \
	"EV20021.BIN", 0x00005b04, 0xffff, 0, \
	"EV20022.BIN", 0x00005b04, 0xffff, 0, \
	"EV20023.BIN", 0x00005b04, 0xffff, 0, \
	"EV20024.BIN", 0x00005b04, 0xffff, 0, \
	"EV20041.BIN", 0x00005b04, 0xffff, 0, \
	"EV21001.BIN", 0x00005b50, 0xffff, 0, \
	"EV21002.BIN", 0x00005af4, 0xffff, 0, \
	"EV21010.BIN", 0x00005b70, 0xffff, 0, \
	"EV21020.BIN", 0x00005af4, 0xffff, 0, \
	"EV21031.BIN", 0x00005af4, 0xffff, 0, \
	"EV21032.BIN", 0x00005af4, 0xffff, 0, \
	"EV21033.BIN", 0x00005af4, 0xffff, 0, \
	"EV21034.BIN", 0x00005af4, 0xffff, 0, \
	"EV21035.BIN", 0x00005af4, 0xffff, 0, \
	"EV21036.BIN", 0x00005af4, 0xffff, 0, \
	"EV21037.BIN", 0x00005af4, 0xffff, 0, \
	"EV21038.BIN", 0x00005af4, 0xffff, 0, \
	"EV21050.BIN", 0x00005bb0, 0xffff, 0, \
	"EV21060.BIN", 0x00005b28, 0xffff, 0, \
	"EV21061.BIN", 0x00005b28, 0xffff, 0, \
	"EV21062.BIN", 0x00005b28, 0xffff, 0, \
	"EV22001.BIN", 0x00005af4, 0xffff, 0, \
	"EV22002.BIN", 0x000068b0, 0xffff, 0, \
	"EV22020.BIN", 0x00005af4, 0xffff, 0, \
	"EV22021.BIN", 0x00005af4, 0xffff, 0, \
	"EV22022.BIN", 0x000065e0, 0xffff, 0, \
	"EV22023.BIN", 0x00006670, 0xffff, 0, \
	"EV22024.BIN", 0x00006180, 0xffff, 0, \
	"EV22025.BIN", 0x0000616c, 0xffff, 0, \
	"EV22026.BIN", 0x00005f68, 0xffff, 0, \
	"EV22027.BIN", 0x00005b54, 0xffff, 0, \
	"EV22030.BIN", 0x00005b0c, 0xffff, 0, \
	"EV22031.BIN", 0x00005b0c, 0xffff, 0, \
	"EV22040.BIN", 0x00005af4, 0xffff, 0, \
	"EV23001.BIN", 0x00005b5c, 0xffff, 0, \
	"EV23002.BIN", 0x0000678c, 0xffff, 0, \
	"EV23003.BIN", 0x00005b88, 0xffff, 0, \
	"EV23010.BIN", 0x00005d08, 0xffff, 0, \
	"EV23020.BIN", 0x00005d08, 0xffff, 0, \
	"EV23021.BIN", 0x00005af4, 0xffff, 0, \
	"EV23022.BIN", 0x00005af4, 0xffff, 0, \
	"EV23023.BIN", 0x00005af4, 0xffff, 0, \
	"EV23041.BIN", 0x00005b94, 0xffff, 0, \
	"EV23042.BIN", 0x00005b94, 0xffff, 0, \
	"EV23043.BIN", 0x00005bb8, 0xffff, 0, \
	"EV23044.BIN", 0x00005b94, 0xffff, 0, \
	"EV23045.BIN", 0x00005b94, 0xffff, 0, \
	"EV23046.BIN", 0x00005bb8, 0xffff, 0, \
	"EV23047.BIN", 0x00005b94, 0xffff, 0, \
	"EV23061.BIN", 0x00005af4, 0xffff, 0, \
	"EV23063.BIN", 0x00005af4, 0xffff, 0, \
	"EV23064.BIN", 0x00005af4, 0xffff, 0, \
	"EV23065.BIN", 0x00005af4, 0xffff, 0, \
	"EV23067.BIN", 0x00005af4, 0xffff, 0, \
	"EV23068.BIN", 0x00005af4, 0xffff, 0, \
	"EV24001.BIN", 0x00005f54, 0xffff, 0, \
	"EV24002.BIN", 0x000062b0, 0xffff, 0, \
	"EV24021.BIN", 0x00005c64, 0xffff, 0, \
	"EV24040.BIN", 0x00005c98, 0xffff, 0, \
	"EV25001.BIN", 0x00005ce8, 0xffff, 0, \
	"EV25002.BIN", 0x00006710, 0xffff, 0, \
	"EV25003.BIN", 0x000065dc, 0xffff, 0, \
	"EV25004.BIN", 0x000060c0, 0xffff, 0, \
	"EV25005.BIN", 0x00006034, 0xffff, 0, \
	"EV25006.BIN", 0x00005d94, 0xffff, 0, \
	"EV25007.BIN", 0x00006050, 0xffff, 0, \
	"EV25008.BIN", 0x00006044, 0xffff, 0, \
	"EV25021.BIN", 0x00005b14, 0xffff, 0, \
	"EV25022.BIN", 0x00005b14, 0xffff, 0, \
	"EV25023.BIN", 0x00005b14, 0xffff, 0, \
	"EV25024.BIN", 0x00005b14, 0xffff, 0, \
	"EV25025.BIN", 0x00005b14, 0xffff, 0, \
	"EV25026.BIN", 0x00005b14, 0xffff, 0, \
	"EV25027.BIN", 0x00005b14, 0xffff, 0, \
	"EV25028.BIN", 0x00005b14, 0xffff, 0, \
	"EV25030.BIN", 0x00005c64, 0xffff, 0, \
	"EV25040.BIN", 0x00005c40, 0xffff, 0, \
	"EV25050.BIN", 0x00005da4, 0xffff, 0, \
	"EV35001.BIN", 0x00005fbc, 0xffff, 0, \
	"EV35003.BIN", 0x00005b0c, 0xffff, 0, \
	"EV35021.BIN", 0x00005b04, 0xffff, 0, \
	"EV36001.BIN", 0x00005ed0, 0xffff, 0, \
	"EV36002.BIN", 0x000067e8, 0xffff, 0, \
	"EV36020.BIN", 0x00005af0, 0xffff, 0, \
	"EV36026.BIN", 0x00005af0, 0xffff, 0, \
	"EV36028.BIN", 0x00005af0, 0xffff, 0, \
	"EV36030.BIN", 0x00006098, 0xffff, 0, \
	"EV37001.BIN", 0x000064c8, 0xffff, 0, \
	"EV37002.BIN", 0x00005b50, 0xffff, 0, \
	"EV45001.BIN", 0x00005c88, 0xffff, 0, \
	"EV45002.BIN", 0x00005d8c, 0xffff, 0, \
	"EV45003.BIN", 0x00005bb0, 0xffff, 0, \
	"EV45010.BIN", 0x00005b60, 0xffff, 0, \
	"EV45021.BIN", 0x00005af4, 0xffff, 0, \
	"EV45022.BIN", 0x00005af4, 0xffff, 0, \
	"EV45023.BIN", 0x00005af4, 0xffff, 0, \
	"EV45033.BIN", 0x00005af4, 0xffff, 0, \
	"EV45041.BIN", 0x00005af4, 0xffff, 0, \
	"EV45042.BIN", 0x00005af4, 0xffff, 0, \
	"EV45043.BIN", 0x00005af4, 0xffff, 0, \
	"EV45044.BIN", 0x00005af4, 0xffff, 0, \
	"EV45045.BIN", 0x00005af4, 0xffff, 0, \
	"EV45046.BIN", 0x00005af4, 0xffff, 0, \
	"EV45047.BIN", 0x00005af4, 0xffff, 0, \
	"EV45048.BIN", 0x00005af4, 0xffff, 0, \
	"EV45099.BIN", 0x00006250, 0xffff, 0, \
	"EV46001.BIN", 0x00005b04, 0xffff, 0, \
	"EV46002.BIN", 0x00005cf0, 0xffff, 0, \
	"EV46003.BIN", 0x00005c04, 0xffff, 0, \
	"EV46020.BIN", 0x00005c60, 0xffff, 0, \
	"EV46021.BIN", 0x00005b04, 0xffff, 0, \
	"EV46030.BIN", 0x00005b58, 0xffff, 0, \
	"EV47001.BIN", 0x00005e80, 0xffff, 0, \
	"EV47002.BIN", 0x00006254, 0xffff, 0, \
	"EV47020.BIN", 0x00005af4, 0xffff, 0, \
	"EV47021.BIN", 0x00005af4, 0xffff, 0, \
	"EV47031.BIN", 0x00005c40, 0xffff, 0, \
	"EV49001.BIN", 0x00005b04, 0xffff, 0, \
	"EV50002.BIN", 0x000065ac, 0xffff, 0, \
	"EV51002.BIN", 0x00005e14, 0xffff, 0, \
	"EV52002.BIN", 0x00005e14, 0xffff, 0, \
	"EV53002.BIN", 0x00005e14, 0xffff, 0, \
	"EV53010.BIN", 0x00005b70, 0xffff, 0, \
	"M18PRG.BIN", 0x00078f9c, 0xffff, 0, \
	"M22PRG.BIN", 0x00077ef4, 0xffff, 0

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
	PatchCharacterDimensionsForLips("TUTORI0.BIN", 0x00005b9c),\
	PatchCharacterDimensionsForLips("EV07000.BIN", 0x000056f8),\
	PatchCharacterDimensionsForLips("EV07001.BIN", 0x00005eec),\
	PatchCharacterDimensionsForLips("EV07002.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV07003.BIN", 0x00005a3c),\
	PatchCharacterDimensionsForLips("EV07005.BIN", 0x00005a60),\
	PatchCharacterDimensionsForLips("EV07006.BIN", 0x00005a70),\
	PatchCharacterDimensionsForLips("EV07007.BIN", 0x00005a60),\
	PatchCharacterDimensionsForLips("EV07008.BIN", 0x00005a70),\
	PatchCharacterDimensionsForLips("EV07009.BIN", 0x00005a48),\
	PatchCharacterDimensionsForLips("EV07020.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV07021.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV08000.BIN", 0x000056f8),\
	PatchCharacterDimensionsForLips("EV08001.BIN", 0x00006458),\
	PatchCharacterDimensionsForLips("EV08002.BIN", 0x000067f8),\
	PatchCharacterDimensionsForLips("EV08003.BIN", 0x00005ab4),\
	PatchCharacterDimensionsForLips("EV08020.BIN", 0x00005a80),\
	PatchCharacterDimensionsForLips("EV08021.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV08030.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV08032.BIN", 0x00005be4),\
	PatchCharacterDimensionsForLips("EV08033.BIN", 0x00005bd0),\
	PatchCharacterDimensionsForLips("EV08050.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV08060.BIN", 0x00006e10),\
	PatchCharacterDimensionsForLips("EV09001.BIN", 0x00006074),\
	PatchCharacterDimensionsForLips("EV09002.BIN", 0x00005a4c),\
	PatchCharacterDimensionsForLips("EV09005.BIN", 0x000060f4),\
	PatchCharacterDimensionsForLips("EV09006.BIN", 0x00005b10),\
	PatchCharacterDimensionsForLips("EV09007.BIN", 0x00005bd8),\
	PatchCharacterDimensionsForLips("EV09010.BIN", 0x00005a7c),\
	PatchCharacterDimensionsForLips("EV09011.BIN", 0x00006038),\
	PatchCharacterDimensionsForLips("EV09025.BIN", 0x00006054),\
	PatchCharacterDimensionsForLips("EV09026.BIN", 0x00006038),\
	PatchCharacterDimensionsForLips("EV09030.BIN", 0x00005a4c),\
	PatchCharacterDimensionsForLips("EV09031.BIN", 0x00005a4c),\
	PatchCharacterDimensionsForLips("EV09035.BIN", 0x00005a70),\
	PatchCharacterDimensionsForLips("EV09041.BIN", 0x00005a30),\
	PatchCharacterDimensionsForLips("EV09042.BIN", 0x00005a30),\
	PatchCharacterDimensionsForLips("EV09043.BIN", 0x00005a30),\
	PatchCharacterDimensionsForLips("EV09044.BIN", 0x00005a30),\
	PatchCharacterDimensionsForLips("EV09045.BIN", 0x00005a30),\
	PatchCharacterDimensionsForLips("EV09046.BIN", 0x00005a30),\
	PatchCharacterDimensionsForLips("EV09048.BIN", 0x00005a30),\
	PatchCharacterDimensionsForLips("EV10001.BIN", 0x00005b70),\
	PatchCharacterDimensionsForLips("EV10002.BIN", 0x00005f68),\
	PatchCharacterDimensionsForLips("EV10020.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV10025.BIN", 0x00005c78),\
	PatchCharacterDimensionsForLips("EV10026.BIN", 0x000061ac),\
	PatchCharacterDimensionsForLips("EV10030.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV10035.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV11001.BIN", 0x00005de4),\
	PatchCharacterDimensionsForLips("EV11002.BIN", 0x000077d4),\
	PatchCharacterDimensionsForLips("EV11030.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV11040.BIN", 0x00005b78),\
	PatchCharacterDimensionsForLips("EV12001.BIN", 0x00005ccc),\
	PatchCharacterDimensionsForLips("EV12002.BIN", 0x00005ad0),\
	PatchCharacterDimensionsForLips("EV12035.BIN", 0x00005af0),\
	PatchCharacterDimensionsForLips("EV13001.BIN", 0x00005d18),\
	PatchCharacterDimensionsForLips("EV13002.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV13035.BIN", 0x00005af0),\
	PatchCharacterDimensionsForLips("EV15001.BIN", 0x00005b60),\
	PatchCharacterDimensionsForLips("EV15002.BIN", 0x0000635c),\
	PatchCharacterDimensionsForLips("EV16001.BIN", 0x00005a20),\
	PatchCharacterDimensionsForLips("EV16002.BIN", 0x00005a20),\
	PatchCharacterDimensionsForLips("EV16035.BIN", 0x00005a78),\
	PatchCharacterDimensionsForLips("EV16051.BIN", 0x00005fcc),\
	PatchCharacterDimensionsForLips("EV16052.BIN", 0x00005a20),\
	PatchCharacterDimensionsForLips("EV16053.BIN", 0x00005fa8),\
	PatchCharacterDimensionsForLips("EV17001.BIN", 0x00005bd8),\
	PatchCharacterDimensionsForLips("EV17002.BIN", 0x00007964),\
	PatchCharacterDimensionsForLips("EV17010.BIN", 0x00005ffc),\
	PatchCharacterDimensionsForLips("EV17030.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV17031.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV17035.BIN", 0x00005ab0),\
	PatchCharacterDimensionsForLips("EV18001.BIN", 0x00006acc),\
	PatchCharacterDimensionsForLips("EV18002.BIN", 0x00005f58),\
	PatchCharacterDimensionsForLips("EV18020.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV18030.BIN", 0x00005ae4),\
	PatchCharacterDimensionsForLips("EV18040.BIN", 0x000061e4),\
	PatchCharacterDimensionsForLips("EV19001.BIN", 0x00005af8),\
	PatchCharacterDimensionsForLips("EV19002.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV19003.BIN", 0x00005ae0),\
	PatchCharacterDimensionsForLips("EV19020.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV19021.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV19022.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV19023.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV19041.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV19042.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV19043.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV19044.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV19045.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV19046.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV19047.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV19048.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV20001.BIN", 0x00005b90),\
	PatchCharacterDimensionsForLips("EV20002.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV20003.BIN", 0x00005b50),\
	PatchCharacterDimensionsForLips("EV20020.BIN", 0x00005a54),\
	PatchCharacterDimensionsForLips("EV20021.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV20022.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV20023.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV20024.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV20041.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV21001.BIN", 0x00005a80),\
	PatchCharacterDimensionsForLips("EV21002.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV21010.BIN", 0x00005aa0),\
	PatchCharacterDimensionsForLips("EV21020.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV21031.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV21032.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV21033.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV21034.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV21035.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV21036.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV21037.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV21038.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV21050.BIN", 0x00005ae0),\
	PatchCharacterDimensionsForLips("EV21060.BIN", 0x00005a58),\
	PatchCharacterDimensionsForLips("EV21061.BIN", 0x00005a58),\
	PatchCharacterDimensionsForLips("EV21062.BIN", 0x00005a58),\
	PatchCharacterDimensionsForLips("EV22001.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV22002.BIN", 0x000067e0),\
	PatchCharacterDimensionsForLips("EV22020.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV22021.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV22022.BIN", 0x00006510),\
	PatchCharacterDimensionsForLips("EV22023.BIN", 0x000065a0),\
	PatchCharacterDimensionsForLips("EV22024.BIN", 0x000060b0),\
	PatchCharacterDimensionsForLips("EV22025.BIN", 0x0000609c),\
	PatchCharacterDimensionsForLips("EV22026.BIN", 0x00005e98),\
	PatchCharacterDimensionsForLips("EV22027.BIN", 0x00005a84),\
	PatchCharacterDimensionsForLips("EV22030.BIN", 0x00005a3c),\
	PatchCharacterDimensionsForLips("EV22031.BIN", 0x00005a3c),\
	PatchCharacterDimensionsForLips("EV22040.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV23001.BIN", 0x00005a8c),\
	PatchCharacterDimensionsForLips("EV23002.BIN", 0x000066bc),\
	PatchCharacterDimensionsForLips("EV23003.BIN", 0x00005ab8),\
	PatchCharacterDimensionsForLips("EV23010.BIN", 0x00005c38),\
	PatchCharacterDimensionsForLips("EV23020.BIN", 0x00005c38),\
	PatchCharacterDimensionsForLips("EV23021.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV23022.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV23023.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV23041.BIN", 0x00005ac4),\
	PatchCharacterDimensionsForLips("EV23042.BIN", 0x00005ac4),\
	PatchCharacterDimensionsForLips("EV23043.BIN", 0x00005ae8),\
	PatchCharacterDimensionsForLips("EV23044.BIN", 0x00005ac4),\
	PatchCharacterDimensionsForLips("EV23045.BIN", 0x00005ac4),\
	PatchCharacterDimensionsForLips("EV23046.BIN", 0x00005ae8),\
	PatchCharacterDimensionsForLips("EV23047.BIN", 0x00005ac4),\
	PatchCharacterDimensionsForLips("EV23061.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV23063.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV23064.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV23065.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV23067.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV23068.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV24001.BIN", 0x00005e84),\
	PatchCharacterDimensionsForLips("EV24002.BIN", 0x000061e0),\
	PatchCharacterDimensionsForLips("EV24021.BIN", 0x00005b94),\
	PatchCharacterDimensionsForLips("EV24040.BIN", 0x00005bc8),\
	PatchCharacterDimensionsForLips("EV25001.BIN", 0x00005c18),\
	PatchCharacterDimensionsForLips("EV25002.BIN", 0x00006640),\
	PatchCharacterDimensionsForLips("EV25003.BIN", 0x0000650c),\
	PatchCharacterDimensionsForLips("EV25004.BIN", 0x00005ff0),\
	PatchCharacterDimensionsForLips("EV25005.BIN", 0x00005f64),\
	PatchCharacterDimensionsForLips("EV25006.BIN", 0x00005cc4),\
	PatchCharacterDimensionsForLips("EV25007.BIN", 0x00005f80),\
	PatchCharacterDimensionsForLips("EV25008.BIN", 0x00005f74),\
	PatchCharacterDimensionsForLips("EV25021.BIN", 0x00005a44),\
	PatchCharacterDimensionsForLips("EV25022.BIN", 0x00005a44),\
	PatchCharacterDimensionsForLips("EV25023.BIN", 0x00005a44),\
	PatchCharacterDimensionsForLips("EV25024.BIN", 0x00005a44),\
	PatchCharacterDimensionsForLips("EV25025.BIN", 0x00005a44),\
	PatchCharacterDimensionsForLips("EV25026.BIN", 0x00005a44),\
	PatchCharacterDimensionsForLips("EV25027.BIN", 0x00005a44),\
	PatchCharacterDimensionsForLips("EV25028.BIN", 0x00005a44),\
	PatchCharacterDimensionsForLips("EV25030.BIN", 0x00005b94),\
	PatchCharacterDimensionsForLips("EV25040.BIN", 0x00005b70),\
	PatchCharacterDimensionsForLips("EV25050.BIN", 0x00005cd4),\
	PatchCharacterDimensionsForLips("EV35001.BIN", 0x00005eec),\
	PatchCharacterDimensionsForLips("EV35003.BIN", 0x00005a3c),\
	PatchCharacterDimensionsForLips("EV35021.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV36001.BIN", 0x00005e00),\
	PatchCharacterDimensionsForLips("EV36002.BIN", 0x00006718),\
	PatchCharacterDimensionsForLips("EV36020.BIN", 0x00005a20),\
	PatchCharacterDimensionsForLips("EV36026.BIN", 0x00005a20),\
	PatchCharacterDimensionsForLips("EV36028.BIN", 0x00005a20),\
	PatchCharacterDimensionsForLips("EV36030.BIN", 0x00005fc8),\
	PatchCharacterDimensionsForLips("EV37001.BIN", 0x000063f8),\
	PatchCharacterDimensionsForLips("EV37002.BIN", 0x00005a80),\
	PatchCharacterDimensionsForLips("EV45001.BIN", 0x00005bb8),\
	PatchCharacterDimensionsForLips("EV45002.BIN", 0x00005cbc),\
	PatchCharacterDimensionsForLips("EV45003.BIN", 0x00005ae0),\
	PatchCharacterDimensionsForLips("EV45010.BIN", 0x00005a90),\
	PatchCharacterDimensionsForLips("EV45021.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV45022.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV45023.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV45033.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV45041.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV45042.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV45043.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV45044.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV45045.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV45046.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV45047.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV45048.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV45099.BIN", 0x00006180),\
	PatchCharacterDimensionsForLips("EV46001.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV46002.BIN", 0x00005c20),\
	PatchCharacterDimensionsForLips("EV46003.BIN", 0x00005b34),\
	PatchCharacterDimensionsForLips("EV46020.BIN", 0x00005b90),\
	PatchCharacterDimensionsForLips("EV46021.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV46030.BIN", 0x00005a88),\
	PatchCharacterDimensionsForLips("EV47001.BIN", 0x00005db0),\
	PatchCharacterDimensionsForLips("EV47002.BIN", 0x00006184),\
	PatchCharacterDimensionsForLips("EV47020.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV47021.BIN", 0x00005a24),\
	PatchCharacterDimensionsForLips("EV47031.BIN", 0x00005b70),\
	PatchCharacterDimensionsForLips("EV49001.BIN", 0x00005a34),\
	PatchCharacterDimensionsForLips("EV50002.BIN", 0x000064dc),\
	PatchCharacterDimensionsForLips("EV51002.BIN", 0x00005d44),\
	PatchCharacterDimensionsForLips("EV52002.BIN", 0x00005d44),\
	PatchCharacterDimensionsForLips("EV53002.BIN", 0x00005d44),\
	PatchCharacterDimensionsForLips("EV53010.BIN", 0x00005aa0),\
	PatchCharacterDimensionsForLips("M18PRG.BIN", 0x00078ecc),\
	PatchCharacterDimensionsForLips("M22PRG.BIN", 0x00077e24)

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
	PatchCharacterDimensions("SLGGOVER.BIN", 0x0001bf40),\
	PatchCharacterDimensions("M07PRG.BIN",   0x0002e91c),\
	PatchCharacterDimensions("M08PRG.BIN",   0x0002819c),\
	PatchCharacterDimensions("M09PRG.BIN",   0x0002ff8c),\
	PatchCharacterDimensions("M10PRG.BIN",   0x00027ee4),\
	PatchCharacterDimensions("M11PRG.BIN",   0x000298f4),\
	PatchCharacterDimensions("M12PRG.BIN",   0x000276a0),\
	PatchCharacterDimensions("M13PRG.BIN",   0x00027584),\
	PatchCharacterDimensions("M15PRG.BIN",   0x000266dc),\
	PatchCharacterDimensions("M16PRG.BIN",   0x000211f4),\
	PatchCharacterDimensions("M17PRG.BIN",   0x00029130),\
	PatchCharacterDimensions("M18PRG.BIN",   0x00026c6c),\
	PatchCharacterDimensions("M19PRG.BIN",   0x00020b50),\
	PatchCharacterDimensions("M20PRG.BIN",   0x00026924),\
	PatchCharacterDimensions("M21PRG.BIN",   0x0002e9e0),\
	PatchCharacterDimensions("M22PRG.BIN",   0x0002fdb8),\
	PatchCharacterDimensions("M23PRG.BIN",   0x0002e1f0),\
	PatchCharacterDimensions("M24PRG.BIN",   0x0002e3dc),\
	PatchCharacterDimensions("M25PRG.BIN",   0x0002e348),\
	PatchCharacterDimensions("M36PRG.BIN",   0x0002f194),\
	PatchCharacterDimensions("M37PRG.BIN",   0x00021098),\
	PatchCharacterDimensions("M45PRG.BIN",   0x00020bbc),\
	PatchCharacterDimensions("M46PRG.BIN",   0x00021b7c),\
	PatchCharacterDimensions("M47PRG.BIN",   0x0002d9f0),\
	PatchCharacterDimensions("M49PRG.BIN",   0x0002100c),\
	PatchCharacterDimensions("M50PRG.BIN",   0x00025944),\
	PatchCharacterDimensions("M51PRG.BIN",   0x0002dc88),\
	PatchCharacterDimensions("M52PRG.BIN",   0x0002147c),\
	PatchCharacterDimensions("M53PRG.BIN",   0x0002e978),\
	PatchCharacterDimensions("M54PRG.BIN",   0x0002e2f8),\
	PatchCharacterDimensions("M91PRG.BIN",   0x0001d6a4),\
	PatchCharacterDimensions("M92PRG.BIN",   0x00016be8)

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
	PatchLipsSingleByteForFile("TUTORI0.BIN", 0x00005c5e),\
	PatchLipsSingleByteForFile("EV07000.BIN", 0x000057ba),\
	PatchLipsSingleByteForFile("EV07001.BIN", 0x00005fae),\
	PatchLipsSingleByteForFile("EV07002.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV07003.BIN", 0x00005afe),\
	PatchLipsSingleByteForFile("EV07005.BIN", 0x00005b22),\
	PatchLipsSingleByteForFile("EV07006.BIN", 0x00005b32),\
	PatchLipsSingleByteForFile("EV07007.BIN", 0x00005b22),\
	PatchLipsSingleByteForFile("EV07008.BIN", 0x00005b32),\
	PatchLipsSingleByteForFile("EV07009.BIN", 0x00005b0a),\
	PatchLipsSingleByteForFile("EV07020.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV07021.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV08000.BIN", 0x000057ba),\
	PatchLipsSingleByteForFile("EV08001.BIN", 0x0000651a),\
	PatchLipsSingleByteForFile("EV08002.BIN", 0x000068ba),\
	PatchLipsSingleByteForFile("EV08003.BIN", 0x00005b76),\
	PatchLipsSingleByteForFile("EV08020.BIN", 0x00005b42),\
	PatchLipsSingleByteForFile("EV08021.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV08030.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV08032.BIN", 0x00005ca6),\
	PatchLipsSingleByteForFile("EV08033.BIN", 0x00005c92),\
	PatchLipsSingleByteForFile("EV08050.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV08060.BIN", 0x00006ed2),\
	PatchLipsSingleByteForFile("EV09001.BIN", 0x00006136),\
	PatchLipsSingleByteForFile("EV09002.BIN", 0x00005b0e),\
	PatchLipsSingleByteForFile("EV09005.BIN", 0x000061b6),\
	PatchLipsSingleByteForFile("EV09006.BIN", 0x00005bd2),\
	PatchLipsSingleByteForFile("EV09007.BIN", 0x00005c9a),\
	PatchLipsSingleByteForFile("EV09010.BIN", 0x00005b3e),\
	PatchLipsSingleByteForFile("EV09011.BIN", 0x000060fa),\
	PatchLipsSingleByteForFile("EV09025.BIN", 0x00006116),\
	PatchLipsSingleByteForFile("EV09026.BIN", 0x000060fa),\
	PatchLipsSingleByteForFile("EV09030.BIN", 0x00005b0e),\
	PatchLipsSingleByteForFile("EV09031.BIN", 0x00005b0e),\
	PatchLipsSingleByteForFile("EV09035.BIN", 0x00005b32),\
	PatchLipsSingleByteForFile("EV09041.BIN", 0x00005af2),\
	PatchLipsSingleByteForFile("EV09042.BIN", 0x00005af2),\
	PatchLipsSingleByteForFile("EV09043.BIN", 0x00005af2),\
	PatchLipsSingleByteForFile("EV09044.BIN", 0x00005af2),\
	PatchLipsSingleByteForFile("EV09045.BIN", 0x00005af2),\
	PatchLipsSingleByteForFile("EV09046.BIN", 0x00005af2),\
	PatchLipsSingleByteForFile("EV09048.BIN", 0x00005af2),\
	PatchLipsSingleByteForFile("EV10001.BIN", 0x00005c32),\
	PatchLipsSingleByteForFile("EV10002.BIN", 0x0000602a),\
	PatchLipsSingleByteForFile("EV10020.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV10025.BIN", 0x00005d3a),\
	PatchLipsSingleByteForFile("EV10026.BIN", 0x0000626e),\
	PatchLipsSingleByteForFile("EV10030.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV10035.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV11001.BIN", 0x00005ea6),\
	PatchLipsSingleByteForFile("EV11002.BIN", 0x00007896),\
	PatchLipsSingleByteForFile("EV11030.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV11040.BIN", 0x00005c3a),\
	PatchLipsSingleByteForFile("EV12001.BIN", 0x00005d8e),\
	PatchLipsSingleByteForFile("EV12002.BIN", 0x00005b92),\
	PatchLipsSingleByteForFile("EV12035.BIN", 0x00005bb2),\
	PatchLipsSingleByteForFile("EV13001.BIN", 0x00005dda),\
	PatchLipsSingleByteForFile("EV13002.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV13035.BIN", 0x00005bb2),\
	PatchLipsSingleByteForFile("EV15001.BIN", 0x00005c22),\
	PatchLipsSingleByteForFile("EV15002.BIN", 0x0000641e),\
	PatchLipsSingleByteForFile("EV16001.BIN", 0x00005ae2),\
	PatchLipsSingleByteForFile("EV16002.BIN", 0x00005ae2),\
	PatchLipsSingleByteForFile("EV16035.BIN", 0x00005b3a),\
	PatchLipsSingleByteForFile("EV16051.BIN", 0x0000608e),\
	PatchLipsSingleByteForFile("EV16052.BIN", 0x00005ae2),\
	PatchLipsSingleByteForFile("EV16053.BIN", 0x0000606a),\
	PatchLipsSingleByteForFile("EV17001.BIN", 0x00005c9a),\
	PatchLipsSingleByteForFile("EV17002.BIN", 0x00007a26),\
	PatchLipsSingleByteForFile("EV17010.BIN", 0x000060be),\
	PatchLipsSingleByteForFile("EV17030.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV17031.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV17035.BIN", 0x00005b72),\
	PatchLipsSingleByteForFile("EV18001.BIN", 0x00006b8e),\
	PatchLipsSingleByteForFile("EV18002.BIN", 0x0000601a),\
	PatchLipsSingleByteForFile("EV18020.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV18030.BIN", 0x00005ba6),\
	PatchLipsSingleByteForFile("EV18040.BIN", 0x000062a6),\
	PatchLipsSingleByteForFile("EV19001.BIN", 0x00005bba),\
	PatchLipsSingleByteForFile("EV19002.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV19003.BIN", 0x00005ba2),\
	PatchLipsSingleByteForFile("EV19020.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV19021.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV19022.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV19023.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV19041.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV19042.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV19043.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV19044.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV19045.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV19046.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV19047.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV19048.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV20001.BIN", 0x00005c52),\
	PatchLipsSingleByteForFile("EV20002.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV20003.BIN", 0x00005c12),\
	PatchLipsSingleByteForFile("EV20020.BIN", 0x00005b16),\
	PatchLipsSingleByteForFile("EV20021.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV20022.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV20023.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV20024.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV20041.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV21001.BIN", 0x00005b42),\
	PatchLipsSingleByteForFile("EV21002.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV21010.BIN", 0x00005b62),\
	PatchLipsSingleByteForFile("EV21020.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV21031.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV21032.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV21033.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV21034.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV21035.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV21036.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV21037.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV21038.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV21050.BIN", 0x00005ba2),\
	PatchLipsSingleByteForFile("EV21060.BIN", 0x00005b1a),\
	PatchLipsSingleByteForFile("EV21061.BIN", 0x00005b1a),\
	PatchLipsSingleByteForFile("EV21062.BIN", 0x00005b1a),\
	PatchLipsSingleByteForFile("EV22001.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV22002.BIN", 0x000068a2),\
	PatchLipsSingleByteForFile("EV22020.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV22021.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV22022.BIN", 0x000065d2),\
	PatchLipsSingleByteForFile("EV22023.BIN", 0x00006662),\
	PatchLipsSingleByteForFile("EV22024.BIN", 0x00006172),\
	PatchLipsSingleByteForFile("EV22025.BIN", 0x0000615e),\
	PatchLipsSingleByteForFile("EV22026.BIN", 0x00005f5a),\
	PatchLipsSingleByteForFile("EV22027.BIN", 0x00005b46),\
	PatchLipsSingleByteForFile("EV22030.BIN", 0x00005afe),\
	PatchLipsSingleByteForFile("EV22031.BIN", 0x00005afe),\
	PatchLipsSingleByteForFile("EV22040.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV23001.BIN", 0x00005b4e),\
	PatchLipsSingleByteForFile("EV23002.BIN", 0x0000677e),\
	PatchLipsSingleByteForFile("EV23003.BIN", 0x00005b7a),\
	PatchLipsSingleByteForFile("EV23010.BIN", 0x00005cfa),\
	PatchLipsSingleByteForFile("EV23020.BIN", 0x00005cfa),\
	PatchLipsSingleByteForFile("EV23021.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV23022.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV23023.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV23041.BIN", 0x00005b86),\
	PatchLipsSingleByteForFile("EV23042.BIN", 0x00005b86),\
	PatchLipsSingleByteForFile("EV23043.BIN", 0x00005baa),\
	PatchLipsSingleByteForFile("EV23044.BIN", 0x00005b86),\
	PatchLipsSingleByteForFile("EV23045.BIN", 0x00005b86),\
	PatchLipsSingleByteForFile("EV23046.BIN", 0x00005baa),\
	PatchLipsSingleByteForFile("EV23047.BIN", 0x00005b86),\
	PatchLipsSingleByteForFile("EV23061.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV23063.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV23064.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV23065.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV23067.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV23068.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV24001.BIN", 0x00005f46),\
	PatchLipsSingleByteForFile("EV24002.BIN", 0x000062a2),\
	PatchLipsSingleByteForFile("EV24021.BIN", 0x00005c56),\
	PatchLipsSingleByteForFile("EV24040.BIN", 0x00005c8a),\
	PatchLipsSingleByteForFile("EV25001.BIN", 0x00005cda),\
	PatchLipsSingleByteForFile("EV25002.BIN", 0x00006702),\
	PatchLipsSingleByteForFile("EV25003.BIN", 0x000065ce),\
	PatchLipsSingleByteForFile("EV25004.BIN", 0x000060b2),\
	PatchLipsSingleByteForFile("EV25005.BIN", 0x00006026),\
	PatchLipsSingleByteForFile("EV25006.BIN", 0x00005d86),\
	PatchLipsSingleByteForFile("EV25007.BIN", 0x00006042),\
	PatchLipsSingleByteForFile("EV25008.BIN", 0x00006036),\
	PatchLipsSingleByteForFile("EV25021.BIN", 0x00005b06),\
	PatchLipsSingleByteForFile("EV25022.BIN", 0x00005b06),\
	PatchLipsSingleByteForFile("EV25023.BIN", 0x00005b06),\
	PatchLipsSingleByteForFile("EV25024.BIN", 0x00005b06),\
	PatchLipsSingleByteForFile("EV25025.BIN", 0x00005b06),\
	PatchLipsSingleByteForFile("EV25026.BIN", 0x00005b06),\
	PatchLipsSingleByteForFile("EV25027.BIN", 0x00005b06),\
	PatchLipsSingleByteForFile("EV25028.BIN", 0x00005b06),\
	PatchLipsSingleByteForFile("EV25030.BIN", 0x00005c56),\
	PatchLipsSingleByteForFile("EV25040.BIN", 0x00005c32),\
	PatchLipsSingleByteForFile("EV25050.BIN", 0x00005d96),\
	PatchLipsSingleByteForFile("EV35001.BIN", 0x00005fae),\
	PatchLipsSingleByteForFile("EV35003.BIN", 0x00005afe),\
	PatchLipsSingleByteForFile("EV35021.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV36001.BIN", 0x00005ec2),\
	PatchLipsSingleByteForFile("EV36002.BIN", 0x000067da),\
	PatchLipsSingleByteForFile("EV36020.BIN", 0x00005ae2),\
	PatchLipsSingleByteForFile("EV36026.BIN", 0x00005ae2),\
	PatchLipsSingleByteForFile("EV36028.BIN", 0x00005ae2),\
	PatchLipsSingleByteForFile("EV36030.BIN", 0x0000608a),\
	PatchLipsSingleByteForFile("EV37001.BIN", 0x000064ba),\
	PatchLipsSingleByteForFile("EV37002.BIN", 0x00005b42),\
	PatchLipsSingleByteForFile("EV45001.BIN", 0x00005c7a),\
	PatchLipsSingleByteForFile("EV45002.BIN", 0x00005d7e),\
	PatchLipsSingleByteForFile("EV45003.BIN", 0x00005ba2),\
	PatchLipsSingleByteForFile("EV45010.BIN", 0x00005b52),\
	PatchLipsSingleByteForFile("EV45021.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV45022.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV45023.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV45033.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV45041.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV45042.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV45043.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV45044.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV45045.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV45046.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV45047.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV45048.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV45099.BIN", 0x00006242),\
	PatchLipsSingleByteForFile("EV46001.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV46002.BIN", 0x00005ce2),\
	PatchLipsSingleByteForFile("EV46003.BIN", 0x00005bf6),\
	PatchLipsSingleByteForFile("EV46020.BIN", 0x00005c52),\
	PatchLipsSingleByteForFile("EV46021.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV46030.BIN", 0x00005b4a),\
	PatchLipsSingleByteForFile("EV47001.BIN", 0x00005e72),\
	PatchLipsSingleByteForFile("EV47002.BIN", 0x00006246),\
	PatchLipsSingleByteForFile("EV47020.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV47021.BIN", 0x00005ae6),\
	PatchLipsSingleByteForFile("EV47031.BIN", 0x00005c32),\
	PatchLipsSingleByteForFile("EV49001.BIN", 0x00005af6),\
	PatchLipsSingleByteForFile("EV50002.BIN", 0x0000659e),\
	PatchLipsSingleByteForFile("EV51002.BIN", 0x00005e06),\
	PatchLipsSingleByteForFile("EV52002.BIN", 0x00005e06),\
	PatchLipsSingleByteForFile("EV53002.BIN", 0x00005e06),\
	PatchLipsSingleByteForFile("EV53010.BIN", 0x00005b62),\
	PatchLipsSingleByteForFile("M18PRG.BIN", 0x00078f8e),\
	PatchLipsSingleByteForFile("M22PRG.BIN", 0x00077ee6)
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
	PatchLipsSingleByteForFile2("TUTORI0.BIN", 0x00005c98),\
	PatchLipsSingleByteForFile2("EV07000.BIN", 0x000057f4),\
	PatchLipsSingleByteForFile2("EV07001.BIN", 0x00005fe8),\
	PatchLipsSingleByteForFile2("EV07002.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV07003.BIN", 0x00005b38),\
	PatchLipsSingleByteForFile2("EV07005.BIN", 0x00005b5c),\
	PatchLipsSingleByteForFile2("EV07006.BIN", 0x00005b6c),\
	PatchLipsSingleByteForFile2("EV07007.BIN", 0x00005b5c),\
	PatchLipsSingleByteForFile2("EV07008.BIN", 0x00005b6c),\
	PatchLipsSingleByteForFile2("EV07009.BIN", 0x00005b44),\
	PatchLipsSingleByteForFile2("EV07020.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV07021.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV08000.BIN", 0x000057f4),\
	PatchLipsSingleByteForFile2("EV08001.BIN", 0x00006554),\
	PatchLipsSingleByteForFile2("EV08002.BIN", 0x000068f4),\
	PatchLipsSingleByteForFile2("EV08003.BIN", 0x00005bb0),\
	PatchLipsSingleByteForFile2("EV08020.BIN", 0x00005b7c),\
	PatchLipsSingleByteForFile2("EV08021.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV08030.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV08032.BIN", 0x00005ce0),\
	PatchLipsSingleByteForFile2("EV08033.BIN", 0x00005ccc),\
	PatchLipsSingleByteForFile2("EV08050.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV08060.BIN", 0x00006f0c),\
	PatchLipsSingleByteForFile2("EV09001.BIN", 0x00006170),\
	PatchLipsSingleByteForFile2("EV09002.BIN", 0x00005b48),\
	PatchLipsSingleByteForFile2("EV09005.BIN", 0x000061f0),\
	PatchLipsSingleByteForFile2("EV09006.BIN", 0x00005c0c),\
	PatchLipsSingleByteForFile2("EV09007.BIN", 0x00005cd4),\
	PatchLipsSingleByteForFile2("EV09010.BIN", 0x00005b78),\
	PatchLipsSingleByteForFile2("EV09011.BIN", 0x00006134),\
	PatchLipsSingleByteForFile2("EV09025.BIN", 0x00006150),\
	PatchLipsSingleByteForFile2("EV09026.BIN", 0x00006134),\
	PatchLipsSingleByteForFile2("EV09030.BIN", 0x00005b48),\
	PatchLipsSingleByteForFile2("EV09031.BIN", 0x00005b48),\
	PatchLipsSingleByteForFile2("EV09035.BIN", 0x00005b6c),\
	PatchLipsSingleByteForFile2("EV09041.BIN", 0x00005b2c),\
	PatchLipsSingleByteForFile2("EV09042.BIN", 0x00005b2c),\
	PatchLipsSingleByteForFile2("EV09043.BIN", 0x00005b2c),\
	PatchLipsSingleByteForFile2("EV09044.BIN", 0x00005b2c),\
	PatchLipsSingleByteForFile2("EV09045.BIN", 0x00005b2c),\
	PatchLipsSingleByteForFile2("EV09046.BIN", 0x00005b2c),\
	PatchLipsSingleByteForFile2("EV09048.BIN", 0x00005b2c),\
	PatchLipsSingleByteForFile2("EV10001.BIN", 0x00005c6c),\
	PatchLipsSingleByteForFile2("EV10002.BIN", 0x00006064),\
	PatchLipsSingleByteForFile2("EV10020.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV10025.BIN", 0x00005d74),\
	PatchLipsSingleByteForFile2("EV10026.BIN", 0x000062a8),\
	PatchLipsSingleByteForFile2("EV10030.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV10035.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV11001.BIN", 0x00005ee0),\
	PatchLipsSingleByteForFile2("EV11002.BIN", 0x000078d0),\
	PatchLipsSingleByteForFile2("EV11030.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV11040.BIN", 0x00005c74),\
	PatchLipsSingleByteForFile2("EV12001.BIN", 0x00005dc8),\
	PatchLipsSingleByteForFile2("EV12002.BIN", 0x00005bcc),\
	PatchLipsSingleByteForFile2("EV12035.BIN", 0x00005bec),\
	PatchLipsSingleByteForFile2("EV13001.BIN", 0x00005e14),\
	PatchLipsSingleByteForFile2("EV13002.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV13035.BIN", 0x00005bec),\
	PatchLipsSingleByteForFile2("EV15001.BIN", 0x00005c5c),\
	PatchLipsSingleByteForFile2("EV15002.BIN", 0x00006458),\
	PatchLipsSingleByteForFile2("EV16001.BIN", 0x00005b1c),\
	PatchLipsSingleByteForFile2("EV16002.BIN", 0x00005b1c),\
	PatchLipsSingleByteForFile2("EV16035.BIN", 0x00005b74),\
	PatchLipsSingleByteForFile2("EV16051.BIN", 0x000060c8),\
	PatchLipsSingleByteForFile2("EV16052.BIN", 0x00005b1c),\
	PatchLipsSingleByteForFile2("EV16053.BIN", 0x000060a4),\
	PatchLipsSingleByteForFile2("EV17001.BIN", 0x00005cd4),\
	PatchLipsSingleByteForFile2("EV17002.BIN", 0x00007a60),\
	PatchLipsSingleByteForFile2("EV17010.BIN", 0x000060f8),\
	PatchLipsSingleByteForFile2("EV17030.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV17031.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV17035.BIN", 0x00005bac),\
	PatchLipsSingleByteForFile2("EV18001.BIN", 0x00006bc8),\
	PatchLipsSingleByteForFile2("EV18002.BIN", 0x00006054),\
	PatchLipsSingleByteForFile2("EV18020.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV18030.BIN", 0x00005be0),\
	PatchLipsSingleByteForFile2("EV18040.BIN", 0x000062e0),\
	PatchLipsSingleByteForFile2("EV19001.BIN", 0x00005bf4),\
	PatchLipsSingleByteForFile2("EV19002.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV19003.BIN", 0x00005bdc),\
	PatchLipsSingleByteForFile2("EV19020.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV19021.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV19022.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV19023.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV19041.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV19042.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV19043.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV19044.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV19045.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV19046.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV19047.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV19048.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV20001.BIN", 0x00005c8c),\
	PatchLipsSingleByteForFile2("EV20002.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV20003.BIN", 0x00005c4c),\
	PatchLipsSingleByteForFile2("EV20020.BIN", 0x00005b50),\
	PatchLipsSingleByteForFile2("EV20021.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV20022.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV20023.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV20024.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV20041.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV21001.BIN", 0x00005b7c),\
	PatchLipsSingleByteForFile2("EV21002.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV21010.BIN", 0x00005b9c),\
	PatchLipsSingleByteForFile2("EV21020.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV21031.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV21032.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV21033.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV21034.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV21035.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV21036.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV21037.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV21038.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV21050.BIN", 0x00005bdc),\
	PatchLipsSingleByteForFile2("EV21060.BIN", 0x00005b54),\
	PatchLipsSingleByteForFile2("EV21061.BIN", 0x00005b54),\
	PatchLipsSingleByteForFile2("EV21062.BIN", 0x00005b54),\
	PatchLipsSingleByteForFile2("EV22001.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV22002.BIN", 0x000068dc),\
	PatchLipsSingleByteForFile2("EV22020.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV22021.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV22022.BIN", 0x0000660c),\
	PatchLipsSingleByteForFile2("EV22023.BIN", 0x0000669c),\
	PatchLipsSingleByteForFile2("EV22024.BIN", 0x000061ac),\
	PatchLipsSingleByteForFile2("EV22025.BIN", 0x00006198),\
	PatchLipsSingleByteForFile2("EV22026.BIN", 0x00005f94),\
	PatchLipsSingleByteForFile2("EV22027.BIN", 0x00005b80),\
	PatchLipsSingleByteForFile2("EV22030.BIN", 0x00005b38),\
	PatchLipsSingleByteForFile2("EV22031.BIN", 0x00005b38),\
	PatchLipsSingleByteForFile2("EV22040.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV23001.BIN", 0x00005b88),\
	PatchLipsSingleByteForFile2("EV23002.BIN", 0x000067b8),\
	PatchLipsSingleByteForFile2("EV23003.BIN", 0x00005bb4),\
	PatchLipsSingleByteForFile2("EV23010.BIN", 0x00005d34),\
	PatchLipsSingleByteForFile2("EV23020.BIN", 0x00005d34),\
	PatchLipsSingleByteForFile2("EV23021.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV23022.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV23023.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV23041.BIN", 0x00005bc0),\
	PatchLipsSingleByteForFile2("EV23042.BIN", 0x00005bc0),\
	PatchLipsSingleByteForFile2("EV23043.BIN", 0x00005be4),\
	PatchLipsSingleByteForFile2("EV23044.BIN", 0x00005bc0),\
	PatchLipsSingleByteForFile2("EV23045.BIN", 0x00005bc0),\
	PatchLipsSingleByteForFile2("EV23046.BIN", 0x00005be4),\
	PatchLipsSingleByteForFile2("EV23047.BIN", 0x00005bc0),\
	PatchLipsSingleByteForFile2("EV23061.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV23063.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV23064.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV23065.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV23067.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV23068.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV24001.BIN", 0x00005f80),\
	PatchLipsSingleByteForFile2("EV24002.BIN", 0x000062dc),\
	PatchLipsSingleByteForFile2("EV24021.BIN", 0x00005c90),\
	PatchLipsSingleByteForFile2("EV24040.BIN", 0x00005cc4),\
	PatchLipsSingleByteForFile2("EV25001.BIN", 0x00005d14),\
	PatchLipsSingleByteForFile2("EV25002.BIN", 0x0000673c),\
	PatchLipsSingleByteForFile2("EV25003.BIN", 0x00006608),\
	PatchLipsSingleByteForFile2("EV25004.BIN", 0x000060ec),\
	PatchLipsSingleByteForFile2("EV25005.BIN", 0x00006060),\
	PatchLipsSingleByteForFile2("EV25006.BIN", 0x00005dc0),\
	PatchLipsSingleByteForFile2("EV25007.BIN", 0x0000607c),\
	PatchLipsSingleByteForFile2("EV25008.BIN", 0x00006070),\
	PatchLipsSingleByteForFile2("EV25021.BIN", 0x00005b40),\
	PatchLipsSingleByteForFile2("EV25022.BIN", 0x00005b40),\
	PatchLipsSingleByteForFile2("EV25023.BIN", 0x00005b40),\
	PatchLipsSingleByteForFile2("EV25024.BIN", 0x00005b40),\
	PatchLipsSingleByteForFile2("EV25025.BIN", 0x00005b40),\
	PatchLipsSingleByteForFile2("EV25026.BIN", 0x00005b40),\
	PatchLipsSingleByteForFile2("EV25027.BIN", 0x00005b40),\
	PatchLipsSingleByteForFile2("EV25028.BIN", 0x00005b40),\
	PatchLipsSingleByteForFile2("EV25030.BIN", 0x00005c90),\
	PatchLipsSingleByteForFile2("EV25040.BIN", 0x00005c6c),\
	PatchLipsSingleByteForFile2("EV25050.BIN", 0x00005dd0),\
	PatchLipsSingleByteForFile2("EV35001.BIN", 0x00005fe8),\
	PatchLipsSingleByteForFile2("EV35003.BIN", 0x00005b38),\
	PatchLipsSingleByteForFile2("EV35021.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV36001.BIN", 0x00005efc),\
	PatchLipsSingleByteForFile2("EV36002.BIN", 0x00006814),\
	PatchLipsSingleByteForFile2("EV36020.BIN", 0x00005b1c),\
	PatchLipsSingleByteForFile2("EV36026.BIN", 0x00005b1c),\
	PatchLipsSingleByteForFile2("EV36028.BIN", 0x00005b1c),\
	PatchLipsSingleByteForFile2("EV36030.BIN", 0x000060c4),\
	PatchLipsSingleByteForFile2("EV37001.BIN", 0x000064f4),\
	PatchLipsSingleByteForFile2("EV37002.BIN", 0x00005b7c),\
	PatchLipsSingleByteForFile2("EV45001.BIN", 0x00005cb4),\
	PatchLipsSingleByteForFile2("EV45002.BIN", 0x00005db8),\
	PatchLipsSingleByteForFile2("EV45003.BIN", 0x00005bdc),\
	PatchLipsSingleByteForFile2("EV45010.BIN", 0x00005b8c),\
	PatchLipsSingleByteForFile2("EV45021.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV45022.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV45023.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV45033.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV45041.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV45042.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV45043.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV45044.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV45045.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV45046.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV45047.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV45048.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV45099.BIN", 0x0000627c),\
	PatchLipsSingleByteForFile2("EV46001.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV46002.BIN", 0x00005d1c),\
	PatchLipsSingleByteForFile2("EV46003.BIN", 0x00005c30),\
	PatchLipsSingleByteForFile2("EV46020.BIN", 0x00005c8c),\
	PatchLipsSingleByteForFile2("EV46021.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV46030.BIN", 0x00005b84),\
	PatchLipsSingleByteForFile2("EV47001.BIN", 0x00005eac),\
	PatchLipsSingleByteForFile2("EV47002.BIN", 0x00006280),\
	PatchLipsSingleByteForFile2("EV47020.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV47021.BIN", 0x00005b20),\
	PatchLipsSingleByteForFile2("EV47031.BIN", 0x00005c6c),\
	PatchLipsSingleByteForFile2("EV49001.BIN", 0x00005b30),\
	PatchLipsSingleByteForFile2("EV50002.BIN", 0x000065d8),\
	PatchLipsSingleByteForFile2("EV51002.BIN", 0x00005e40),\
	PatchLipsSingleByteForFile2("EV52002.BIN", 0x00005e40),\
	PatchLipsSingleByteForFile2("EV53002.BIN", 0x00005e40),\
	PatchLipsSingleByteForFile2("EV53010.BIN", 0x00005b9c),\
	PatchLipsSingleByteForFile2("M18PRG.BIN", 0x00078fc8),\
	PatchLipsSingleByteForFile2("M22PRG.BIN", 0x00077f20)

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
	SingleByteFastForward("SLGGOVER.BIN", 0x0001bd8e),\
	SingleByteFastForward("M07PRG.BIN", 0x0002e52c),\
    SingleByteFastForward("M07PRG.BIN", 0x0002e632),\
    SingleByteFastForward("M07PRG.BIN", 0x0002e76a),\
    SingleByteFastForward("M08PRG.BIN", 0x00027dac),\
    SingleByteFastForward("M08PRG.BIN", 0x00027eb2),\
    SingleByteFastForward("M08PRG.BIN", 0x00027fea),\
    SingleByteFastForward("M09PRG.BIN", 0x0002fb9c),\
    SingleByteFastForward("M09PRG.BIN", 0x0002fca2),\
    SingleByteFastForward("M09PRG.BIN", 0x0002fdda),\
    SingleByteFastForward("M10PRG.BIN", 0x00027af4),\
    SingleByteFastForward("M10PRG.BIN", 0x00027bfa),\
    SingleByteFastForward("M10PRG.BIN", 0x00027d32),\
    SingleByteFastForward("M11PRG.BIN", 0x00029504),\
    SingleByteFastForward("M11PRG.BIN", 0x0002960a),\
    SingleByteFastForward("M11PRG.BIN", 0x00029742),\
    SingleByteFastForward("M12PRG.BIN", 0x000272b0),\
    SingleByteFastForward("M12PRG.BIN", 0x000273b6),\
    SingleByteFastForward("M12PRG.BIN", 0x000274ee),\
    SingleByteFastForward("M13PRG.BIN", 0x00027194),\
    SingleByteFastForward("M13PRG.BIN", 0x0002729a),\
    SingleByteFastForward("M13PRG.BIN", 0x000273d2),\
    SingleByteFastForward("M15PRG.BIN", 0x000262ec),\
    SingleByteFastForward("M15PRG.BIN", 0x000263f2),\
    SingleByteFastForward("M15PRG.BIN", 0x0002652a),\
    SingleByteFastForward("M16PRG.BIN", 0x00020e04),\
    SingleByteFastForward("M16PRG.BIN", 0x00020f0a),\
    SingleByteFastForward("M16PRG.BIN", 0x00021042),\
    SingleByteFastForward("M17PRG.BIN", 0x00028d40),\
    SingleByteFastForward("M17PRG.BIN", 0x00028e46),\
    SingleByteFastForward("M17PRG.BIN", 0x00028f7e),\
    SingleByteFastForward("M18PRG.BIN", 0x0002687c),\
    SingleByteFastForward("M18PRG.BIN", 0x00026982),\
    SingleByteFastForward("M18PRG.BIN", 0x00026aba),\
    SingleByteFastForward("M19PRG.BIN", 0x00020760),\
    SingleByteFastForward("M19PRG.BIN", 0x00020866),\
    SingleByteFastForward("M19PRG.BIN", 0x0002099e),\
    SingleByteFastForward("M20PRG.BIN", 0x00026534),\
    SingleByteFastForward("M20PRG.BIN", 0x0002663a),\
    SingleByteFastForward("M20PRG.BIN", 0x00026772),\
    SingleByteFastForward("M21PRG.BIN", 0x0002e5f0),\
    SingleByteFastForward("M21PRG.BIN", 0x0002e6f6),\
    SingleByteFastForward("M21PRG.BIN", 0x0002e82e),\
    SingleByteFastForward("M22PRG.BIN", 0x0002f9c8),\
    SingleByteFastForward("M22PRG.BIN", 0x0002face),\
    SingleByteFastForward("M22PRG.BIN", 0x0002fc06),\
    SingleByteFastForward("M23PRG.BIN", 0x0002de00),\
    SingleByteFastForward("M23PRG.BIN", 0x0002df06),\
    SingleByteFastForward("M23PRG.BIN", 0x0002e03e),\
    SingleByteFastForward("M24PRG.BIN", 0x0002dfec),\
    SingleByteFastForward("M24PRG.BIN", 0x0002e0f2),\
    SingleByteFastForward("M24PRG.BIN", 0x0002e22a),\
    SingleByteFastForward("M25PRG.BIN", 0x0002df58),\
    SingleByteFastForward("M25PRG.BIN", 0x0002e05e),\
    SingleByteFastForward("M25PRG.BIN", 0x0002e196),\
    SingleByteFastForward("M36PRG.BIN", 0x0002eda4),\
    SingleByteFastForward("M36PRG.BIN", 0x0002eeaa),\
    SingleByteFastForward("M36PRG.BIN", 0x0002efe2),\
    SingleByteFastForward("M37PRG.BIN", 0x00020ca8),\
    SingleByteFastForward("M37PRG.BIN", 0x00020dae),\
    SingleByteFastForward("M37PRG.BIN", 0x00020ee6),\
    SingleByteFastForward("M45PRG.BIN", 0x000207cc),\
    SingleByteFastForward("M45PRG.BIN", 0x000208d2),\
    SingleByteFastForward("M45PRG.BIN", 0x00020a0a),\
    SingleByteFastForward("M46PRG.BIN", 0x0002178c),\
    SingleByteFastForward("M46PRG.BIN", 0x00021892),\
    SingleByteFastForward("M46PRG.BIN", 0x000219ca),\
    SingleByteFastForward("M47PRG.BIN", 0x0002d600),\
    SingleByteFastForward("M47PRG.BIN", 0x0002d706),\
    SingleByteFastForward("M47PRG.BIN", 0x0002d83e),\
    SingleByteFastForward("M49PRG.BIN", 0x00020c1c),\
    SingleByteFastForward("M49PRG.BIN", 0x00020d22),\
    SingleByteFastForward("M49PRG.BIN", 0x00020e5a),\
    SingleByteFastForward("M50PRG.BIN", 0x00025554),\
    SingleByteFastForward("M50PRG.BIN", 0x0002565a),\
    SingleByteFastForward("M50PRG.BIN", 0x00025792),\
	SingleByteFastForward("M51PRG.BIN", 0x0002d898),\
	SingleByteFastForward("M51PRG.BIN", 0x0002d99e),\
	SingleByteFastForward("M51PRG.BIN", 0x0002dad6),\
    SingleByteFastForward("M52PRG.BIN", 0x0002108c),\
    SingleByteFastForward("M52PRG.BIN", 0x00021192),\
    SingleByteFastForward("M52PRG.BIN", 0x000212ca),\
    SingleByteFastForward("M53PRG.BIN", 0x0002e588),\
    SingleByteFastForward("M53PRG.BIN", 0x0002e68e),\
    SingleByteFastForward("M53PRG.BIN", 0x0002e7c6),\
    SingleByteFastForward("M54PRG.BIN", 0x0002df08),\
    SingleByteFastForward("M54PRG.BIN", 0x0002e00e),\
    SingleByteFastForward("M54PRG.BIN", 0x0002e146),\
    SingleByteFastForward("M91PRG.BIN", 0x0001d2b4),\
    SingleByteFastForward("M91PRG.BIN", 0x0001d3ba),\
    SingleByteFastForward("M91PRG.BIN", 0x0001d4f2),\
    SingleByteFastForward("M92PRG.BIN", 0x000167f8),\
    SingleByteFastForward("M92PRG.BIN", 0x000168fe),\
    SingleByteFastForward("M92PRG.BIN", 0x00016a36)

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
	PatchSingleByteHiCharacterHandling("SLGGOVER.BIN", 0x0001bc6e),\
	PatchSingleByteHiCharacterHandling("M07PRG.BIN", 0x0002e64a),\
    PatchSingleByteHiCharacterHandling("M08PRG.BIN", 0x00027eca),\
    PatchSingleByteHiCharacterHandling("M09PRG.BIN", 0x0002fcba),\
    PatchSingleByteHiCharacterHandling("M10PRG.BIN", 0x00027c12),\
    PatchSingleByteHiCharacterHandling("M11PRG.BIN", 0x00029622),\
    PatchSingleByteHiCharacterHandling("M12PRG.BIN", 0x000273ce),\
    PatchSingleByteHiCharacterHandling("M13PRG.BIN", 0x000272b2),\
    PatchSingleByteHiCharacterHandling("M15PRG.BIN", 0x0002640a),\
    PatchSingleByteHiCharacterHandling("M16PRG.BIN", 0x00020f22),\
    PatchSingleByteHiCharacterHandling("M17PRG.BIN", 0x00028e5e),\
    PatchSingleByteHiCharacterHandling("M18PRG.BIN", 0x0002699a),\
    PatchSingleByteHiCharacterHandling("M19PRG.BIN", 0x0002087e),\
    PatchSingleByteHiCharacterHandling("M20PRG.BIN", 0x00026652),\
    PatchSingleByteHiCharacterHandling("M21PRG.BIN", 0x0002e70e),\
    PatchSingleByteHiCharacterHandling("M22PRG.BIN", 0x0002fae6),\
    PatchSingleByteHiCharacterHandling("M23PRG.BIN", 0x0002df1e),\
    PatchSingleByteHiCharacterHandling("M24PRG.BIN", 0x0002e10a),\
    PatchSingleByteHiCharacterHandling("M25PRG.BIN", 0x0002e076),\
    PatchSingleByteHiCharacterHandling("M36PRG.BIN", 0x0002eec2),\
    PatchSingleByteHiCharacterHandling("M37PRG.BIN", 0x00020dc6),\
    PatchSingleByteHiCharacterHandling("M45PRG.BIN", 0x000208ea),\
    PatchSingleByteHiCharacterHandling("M46PRG.BIN", 0x000218aa),\
    PatchSingleByteHiCharacterHandling("M47PRG.BIN", 0x0002d71e),\
    PatchSingleByteHiCharacterHandling("M49PRG.BIN", 0x00020d3a),\
    PatchSingleByteHiCharacterHandling("M50PRG.BIN", 0x00025672),\
	PatchSingleByteHiCharacterHandling("M51PRG.BIN", 0x0002d9b6),\
    PatchSingleByteHiCharacterHandling("M52PRG.BIN", 0x000211aa),\
    PatchSingleByteHiCharacterHandling("M53PRG.BIN", 0x0002e6a6),\
    PatchSingleByteHiCharacterHandling("M54PRG.BIN", 0x0002e026),\
    PatchSingleByteHiCharacterHandling("M91PRG.BIN", 0x0001d3d2),\
    PatchSingleByteHiCharacterHandling("M92PRG.BIN", 0x00016916)

/*
	060d1324 extu.w r1, r1 //611d to 611c	
	060d133a extu.w r4, r4 //644d to 644c
	060d139c extu.w r1, r1
	060d13c8 0000ffff
*/
#define PatchSingleByteLipsHiCharacterHandling(filename, address) \
	filename, address + 0,   0x611c, 0x611d,\
	filename, address + 22,  0x644c, 0x644d,\
	filename, address + 120, 0x611c, 0x611d,\
	filename, address + 165, 0x00,   0xff

#define PatchSingleByteLipsHiCharacterHandlingInFiles() \
	PatchSingleByteLipsHiCharacterHandling("EV00001.BIN", 0x00007220),\
	PatchSingleByteLipsHiCharacterHandling("EV00002.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV00050.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV00051.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV00052.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV00054.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV00055.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV00060.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV01001.BIN", 0x00005c30),\
	PatchSingleByteLipsHiCharacterHandling("EV01002.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV01020.BIN", 0x00006148),\
	PatchSingleByteLipsHiCharacterHandling("EV01021.BIN", 0x00005f5c),\
	PatchSingleByteLipsHiCharacterHandling("EV01022.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV01023.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV01030.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV01050.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV01054.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV01055.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV02001.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV02002.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV02010.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV02021.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV02025.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV02050.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV02051.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV02052.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV02053.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV02054.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV03000.BIN", 0x000057f8),\
	PatchSingleByteLipsHiCharacterHandling("EV03001.BIN", 0x00005cf0),\
	PatchSingleByteLipsHiCharacterHandling("EV03002.BIN", 0x00005b98),\
	PatchSingleByteLipsHiCharacterHandling("EV03005.BIN", 0x00005b50),\
	PatchSingleByteLipsHiCharacterHandling("EV03010.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV03020.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV03021.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV03023.BIN", 0x00005b4c),\
	PatchSingleByteLipsHiCharacterHandling("EV03024.BIN", 0x00005b4c),\
	PatchSingleByteLipsHiCharacterHandling("EV03025.BIN", 0x00005b60),\
	PatchSingleByteLipsHiCharacterHandling("EV03050.BIN", 0x00005f64),\
	PatchSingleByteLipsHiCharacterHandling("EV03051.BIN", 0x00005b88),\
	PatchSingleByteLipsHiCharacterHandling("EV03052.BIN", 0x00005bf4),\
	PatchSingleByteLipsHiCharacterHandling("EV03053.BIN", 0x00005bb0),\
	PatchSingleByteLipsHiCharacterHandling("EV04001.BIN", 0x000069dc),\
	PatchSingleByteLipsHiCharacterHandling("EV04002.BIN", 0x00005ba0),\
	PatchSingleByteLipsHiCharacterHandling("EV04003.BIN", 0x00005cd4),\
	PatchSingleByteLipsHiCharacterHandling("EV04005.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV04010.BIN", 0x00005b98),\
	PatchSingleByteLipsHiCharacterHandling("EV04020.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV04022.BIN", 0x00005b2c),\
	PatchSingleByteLipsHiCharacterHandling("EV04050.BIN", 0x00005bcc),\
	PatchSingleByteLipsHiCharacterHandling("EV04053.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV04055.BIN", 0x00005b98),\
	PatchSingleByteLipsHiCharacterHandling("EV05001.BIN", 0x00005b3c),\
	PatchSingleByteLipsHiCharacterHandling("EV05002.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05003.BIN", 0x00005d00),\
	PatchSingleByteLipsHiCharacterHandling("EV05004.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05005.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05007.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05010.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05011.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05018.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05019.BIN", 0x00005b4c),\
	PatchSingleByteLipsHiCharacterHandling("EV05020.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05021.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05022.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05023.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05025.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05026.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05027.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05051.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05052.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05053.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05054.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV05060.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV26001.BIN", 0x00006930),\
	PatchSingleByteLipsHiCharacterHandling("EV26002.BIN", 0x00005ecc),\
	PatchSingleByteLipsHiCharacterHandling("EV26020.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV26021.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV27001.BIN", 0x00005b30),\
	PatchSingleByteLipsHiCharacterHandling("EV27002.BIN", 0x00005bd8),\
	PatchSingleByteLipsHiCharacterHandling("EV27054.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV28001.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV28025.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV28050.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV28051.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV29001.BIN", 0x00005b48),\
	PatchSingleByteLipsHiCharacterHandling("EV29002.BIN", 0x00005b48),\
	PatchSingleByteLipsHiCharacterHandling("EV29021.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV29050.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV30003.BIN", 0x00006b6c),\
	PatchSingleByteLipsHiCharacterHandling("EV30020.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV30026.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV30027.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV31001.BIN", 0x00005dcc),\
	PatchSingleByteLipsHiCharacterHandling("EV31002.BIN", 0x00005d24),\
	PatchSingleByteLipsHiCharacterHandling("EV31023.BIN", 0x00005b4c),\
	PatchSingleByteLipsHiCharacterHandling("EV31024.BIN", 0x00005b4c),\
	PatchSingleByteLipsHiCharacterHandling("EV32002.BIN", 0x00005b58),\
	PatchSingleByteLipsHiCharacterHandling("EV32020.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV32055.BIN", 0x00005b98),\
	PatchSingleByteLipsHiCharacterHandling("EV33001.BIN", 0x00006078),\
	PatchSingleByteLipsHiCharacterHandling("EV33002.BIN", 0x00005de0),\
	PatchSingleByteLipsHiCharacterHandling("EV33022.BIN", 0x00005b2c),\
	PatchSingleByteLipsHiCharacterHandling("EV33054.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV34001.BIN", 0x00005b48),\
	PatchSingleByteLipsHiCharacterHandling("EV34002.BIN", 0x00005b60),\
	PatchSingleByteLipsHiCharacterHandling("EV34003.BIN", 0x00005b48),\
	PatchSingleByteLipsHiCharacterHandling("EV34004.BIN", 0x00005ba0),\
	PatchSingleByteLipsHiCharacterHandling("EV34005.BIN", 0x00005ba0),\
	PatchSingleByteLipsHiCharacterHandling("EV34006.BIN", 0x00005bd8),\
	PatchSingleByteLipsHiCharacterHandling("EV34007.BIN", 0x00005b48),\
	PatchSingleByteLipsHiCharacterHandling("EV34008.BIN", 0x00005bd8),\
	PatchSingleByteLipsHiCharacterHandling("EV34020.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV34030.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV34040.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV34041.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV34042.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV34043.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV34044.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("M00PRG.BIN",  0x00079620),\
	PatchSingleByteLipsHiCharacterHandling("M26PRG.BIN",  0x00078d30),\
	PatchSingleByteLipsHiCharacterHandling("TUTORI0.BIN", 0x00005c9c),\
	PatchSingleByteLipsHiCharacterHandling("EV07000.BIN", 0x000057f8),\
	PatchSingleByteLipsHiCharacterHandling("EV07001.BIN", 0x00005fec),\
	PatchSingleByteLipsHiCharacterHandling("EV07002.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV07003.BIN", 0x00005b3c),\
	PatchSingleByteLipsHiCharacterHandling("EV07005.BIN", 0x00005b60),\
	PatchSingleByteLipsHiCharacterHandling("EV07006.BIN", 0x00005b70),\
	PatchSingleByteLipsHiCharacterHandling("EV07007.BIN", 0x00005b60),\
	PatchSingleByteLipsHiCharacterHandling("EV07008.BIN", 0x00005b70),\
	PatchSingleByteLipsHiCharacterHandling("EV07009.BIN", 0x00005b48),\
	PatchSingleByteLipsHiCharacterHandling("EV07020.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV07021.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV08000.BIN", 0x000057f8),\
	PatchSingleByteLipsHiCharacterHandling("EV08001.BIN", 0x00006558),\
	PatchSingleByteLipsHiCharacterHandling("EV08002.BIN", 0x000068f8),\
	PatchSingleByteLipsHiCharacterHandling("EV08003.BIN", 0x00005bb4),\
	PatchSingleByteLipsHiCharacterHandling("EV08020.BIN", 0x00005b80),\
	PatchSingleByteLipsHiCharacterHandling("EV08021.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV08030.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV08032.BIN", 0x00005ce4),\
	PatchSingleByteLipsHiCharacterHandling("EV08033.BIN", 0x00005cd0),\
	PatchSingleByteLipsHiCharacterHandling("EV08050.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV08060.BIN", 0x00006f10),\
	PatchSingleByteLipsHiCharacterHandling("EV09001.BIN", 0x00006174),\
	PatchSingleByteLipsHiCharacterHandling("EV09002.BIN", 0x00005b4c),\
	PatchSingleByteLipsHiCharacterHandling("EV09005.BIN", 0x000061f4),\
	PatchSingleByteLipsHiCharacterHandling("EV09006.BIN", 0x00005c10),\
	PatchSingleByteLipsHiCharacterHandling("EV09007.BIN", 0x00005cd8),\
	PatchSingleByteLipsHiCharacterHandling("EV09010.BIN", 0x00005b7c),\
	PatchSingleByteLipsHiCharacterHandling("EV09011.BIN", 0x00006138),\
	PatchSingleByteLipsHiCharacterHandling("EV09025.BIN", 0x00006154),\
	PatchSingleByteLipsHiCharacterHandling("EV09026.BIN", 0x00006138),\
	PatchSingleByteLipsHiCharacterHandling("EV09030.BIN", 0x00005b4c),\
	PatchSingleByteLipsHiCharacterHandling("EV09031.BIN", 0x00005b4c),\
	PatchSingleByteLipsHiCharacterHandling("EV09035.BIN", 0x00005b70),\
	PatchSingleByteLipsHiCharacterHandling("EV09041.BIN", 0x00005b30),\
	PatchSingleByteLipsHiCharacterHandling("EV09042.BIN", 0x00005b30),\
	PatchSingleByteLipsHiCharacterHandling("EV09043.BIN", 0x00005b30),\
	PatchSingleByteLipsHiCharacterHandling("EV09044.BIN", 0x00005b30),\
	PatchSingleByteLipsHiCharacterHandling("EV09045.BIN", 0x00005b30),\
	PatchSingleByteLipsHiCharacterHandling("EV09046.BIN", 0x00005b30),\
	PatchSingleByteLipsHiCharacterHandling("EV09048.BIN", 0x00005b30),\
	PatchSingleByteLipsHiCharacterHandling("EV10001.BIN", 0x00005c70),\
	PatchSingleByteLipsHiCharacterHandling("EV10002.BIN", 0x00006068),\
	PatchSingleByteLipsHiCharacterHandling("EV10020.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV10025.BIN", 0x00005d78),\
	PatchSingleByteLipsHiCharacterHandling("EV10026.BIN", 0x000062ac),\
	PatchSingleByteLipsHiCharacterHandling("EV10030.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV10035.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV11001.BIN", 0x00005ee4),\
	PatchSingleByteLipsHiCharacterHandling("EV11002.BIN", 0x000078d4),\
	PatchSingleByteLipsHiCharacterHandling("EV11030.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV11040.BIN", 0x00005c78),\
	PatchSingleByteLipsHiCharacterHandling("EV12001.BIN", 0x00005dcc),\
	PatchSingleByteLipsHiCharacterHandling("EV12002.BIN", 0x00005bd0),\
	PatchSingleByteLipsHiCharacterHandling("EV12035.BIN", 0x00005bf0),\
	PatchSingleByteLipsHiCharacterHandling("EV13001.BIN", 0x00005e18),\
	PatchSingleByteLipsHiCharacterHandling("EV13002.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV13035.BIN", 0x00005bf0),\
	PatchSingleByteLipsHiCharacterHandling("EV15001.BIN", 0x00005c60),\
	PatchSingleByteLipsHiCharacterHandling("EV15002.BIN", 0x0000645c),\
	PatchSingleByteLipsHiCharacterHandling("EV16001.BIN", 0x00005b20),\
	PatchSingleByteLipsHiCharacterHandling("EV16002.BIN", 0x00005b20),\
	PatchSingleByteLipsHiCharacterHandling("EV16035.BIN", 0x00005b78),\
	PatchSingleByteLipsHiCharacterHandling("EV16051.BIN", 0x000060cc),\
	PatchSingleByteLipsHiCharacterHandling("EV16052.BIN", 0x00005b20),\
	PatchSingleByteLipsHiCharacterHandling("EV16053.BIN", 0x000060a8),\
	PatchSingleByteLipsHiCharacterHandling("EV17001.BIN", 0x00005cd8),\
	PatchSingleByteLipsHiCharacterHandling("EV17002.BIN", 0x00007a64),\
	PatchSingleByteLipsHiCharacterHandling("EV17010.BIN", 0x000060fc),\
	PatchSingleByteLipsHiCharacterHandling("EV17030.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV17031.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV17035.BIN", 0x00005bb0),\
	PatchSingleByteLipsHiCharacterHandling("EV18001.BIN", 0x00006bcc),\
	PatchSingleByteLipsHiCharacterHandling("EV18002.BIN", 0x00006058),\
	PatchSingleByteLipsHiCharacterHandling("EV18020.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV18030.BIN", 0x00005be4),\
	PatchSingleByteLipsHiCharacterHandling("EV18040.BIN", 0x000062e4),\
	PatchSingleByteLipsHiCharacterHandling("EV19001.BIN", 0x00005bf8),\
	PatchSingleByteLipsHiCharacterHandling("EV19002.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV19003.BIN", 0x00005be0),\
	PatchSingleByteLipsHiCharacterHandling("EV19020.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV19021.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV19022.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV19023.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV19041.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV19042.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV19043.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV19044.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV19045.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV19046.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV19047.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV19048.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV20001.BIN", 0x00005c90),\
	PatchSingleByteLipsHiCharacterHandling("EV20002.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV20003.BIN", 0x00005c50),\
	PatchSingleByteLipsHiCharacterHandling("EV20020.BIN", 0x00005b54),\
	PatchSingleByteLipsHiCharacterHandling("EV20021.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV20022.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV20023.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV20024.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV20041.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV21001.BIN", 0x00005b80),\
	PatchSingleByteLipsHiCharacterHandling("EV21002.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV21010.BIN", 0x00005ba0),\
	PatchSingleByteLipsHiCharacterHandling("EV21020.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV21031.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV21032.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV21033.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV21034.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV21035.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV21036.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV21037.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV21038.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV21050.BIN", 0x00005be0),\
	PatchSingleByteLipsHiCharacterHandling("EV21060.BIN", 0x00005b58),\
	PatchSingleByteLipsHiCharacterHandling("EV21061.BIN", 0x00005b58),\
	PatchSingleByteLipsHiCharacterHandling("EV21062.BIN", 0x00005b58),\
	PatchSingleByteLipsHiCharacterHandling("EV22001.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV22002.BIN", 0x000068e0),\
	PatchSingleByteLipsHiCharacterHandling("EV22020.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV22021.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV22022.BIN", 0x00006610),\
	PatchSingleByteLipsHiCharacterHandling("EV22023.BIN", 0x000066a0),\
	PatchSingleByteLipsHiCharacterHandling("EV22024.BIN", 0x000061b0),\
	PatchSingleByteLipsHiCharacterHandling("EV22025.BIN", 0x0000619c),\
	PatchSingleByteLipsHiCharacterHandling("EV22026.BIN", 0x00005f98),\
	PatchSingleByteLipsHiCharacterHandling("EV22027.BIN", 0x00005b84),\
	PatchSingleByteLipsHiCharacterHandling("EV22030.BIN", 0x00005b3c),\
	PatchSingleByteLipsHiCharacterHandling("EV22031.BIN", 0x00005b3c),\
	PatchSingleByteLipsHiCharacterHandling("EV22040.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV23001.BIN", 0x00005b8c),\
	PatchSingleByteLipsHiCharacterHandling("EV23002.BIN", 0x000067bc),\
	PatchSingleByteLipsHiCharacterHandling("EV23003.BIN", 0x00005bb8),\
	PatchSingleByteLipsHiCharacterHandling("EV23010.BIN", 0x00005d38),\
	PatchSingleByteLipsHiCharacterHandling("EV23020.BIN", 0x00005d38),\
	PatchSingleByteLipsHiCharacterHandling("EV23021.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV23022.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV23023.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV23041.BIN", 0x00005bc4),\
	PatchSingleByteLipsHiCharacterHandling("EV23042.BIN", 0x00005bc4),\
	PatchSingleByteLipsHiCharacterHandling("EV23043.BIN", 0x00005be8),\
	PatchSingleByteLipsHiCharacterHandling("EV23044.BIN", 0x00005bc4),\
	PatchSingleByteLipsHiCharacterHandling("EV23045.BIN", 0x00005bc4),\
	PatchSingleByteLipsHiCharacterHandling("EV23046.BIN", 0x00005be8),\
	PatchSingleByteLipsHiCharacterHandling("EV23047.BIN", 0x00005bc4),\
	PatchSingleByteLipsHiCharacterHandling("EV23061.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV23063.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV23064.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV23065.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV23067.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV23068.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV24001.BIN", 0x00005f84),\
	PatchSingleByteLipsHiCharacterHandling("EV24002.BIN", 0x000062e0),\
	PatchSingleByteLipsHiCharacterHandling("EV24021.BIN", 0x00005c94),\
	PatchSingleByteLipsHiCharacterHandling("EV24040.BIN", 0x00005cc8),\
	PatchSingleByteLipsHiCharacterHandling("EV25001.BIN", 0x00005d18),\
	PatchSingleByteLipsHiCharacterHandling("EV25002.BIN", 0x00006740),\
	PatchSingleByteLipsHiCharacterHandling("EV25003.BIN", 0x0000660c),\
	PatchSingleByteLipsHiCharacterHandling("EV25004.BIN", 0x000060f0),\
	PatchSingleByteLipsHiCharacterHandling("EV25005.BIN", 0x00006064),\
	PatchSingleByteLipsHiCharacterHandling("EV25006.BIN", 0x00005dc4),\
	PatchSingleByteLipsHiCharacterHandling("EV25007.BIN", 0x00006080),\
	PatchSingleByteLipsHiCharacterHandling("EV25008.BIN", 0x00006074),\
	PatchSingleByteLipsHiCharacterHandling("EV25021.BIN", 0x00005b44),\
	PatchSingleByteLipsHiCharacterHandling("EV25022.BIN", 0x00005b44),\
	PatchSingleByteLipsHiCharacterHandling("EV25023.BIN", 0x00005b44),\
	PatchSingleByteLipsHiCharacterHandling("EV25024.BIN", 0x00005b44),\
	PatchSingleByteLipsHiCharacterHandling("EV25025.BIN", 0x00005b44),\
	PatchSingleByteLipsHiCharacterHandling("EV25026.BIN", 0x00005b44),\
	PatchSingleByteLipsHiCharacterHandling("EV25027.BIN", 0x00005b44),\
	PatchSingleByteLipsHiCharacterHandling("EV25028.BIN", 0x00005b44),\
	PatchSingleByteLipsHiCharacterHandling("EV25030.BIN", 0x00005c94),\
	PatchSingleByteLipsHiCharacterHandling("EV25040.BIN", 0x00005c70),\
	PatchSingleByteLipsHiCharacterHandling("EV25050.BIN", 0x00005dd4),\
	PatchSingleByteLipsHiCharacterHandling("EV35001.BIN", 0x00005fec),\
	PatchSingleByteLipsHiCharacterHandling("EV35003.BIN", 0x00005b3c),\
	PatchSingleByteLipsHiCharacterHandling("EV35021.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV36001.BIN", 0x00005f00),\
	PatchSingleByteLipsHiCharacterHandling("EV36002.BIN", 0x00006818),\
	PatchSingleByteLipsHiCharacterHandling("EV36020.BIN", 0x00005b20),\
	PatchSingleByteLipsHiCharacterHandling("EV36026.BIN", 0x00005b20),\
	PatchSingleByteLipsHiCharacterHandling("EV36028.BIN", 0x00005b20),\
	PatchSingleByteLipsHiCharacterHandling("EV36030.BIN", 0x000060c8),\
	PatchSingleByteLipsHiCharacterHandling("EV37001.BIN", 0x000064f8),\
	PatchSingleByteLipsHiCharacterHandling("EV37002.BIN", 0x00005b80),\
	PatchSingleByteLipsHiCharacterHandling("EV45001.BIN", 0x00005cb8),\
	PatchSingleByteLipsHiCharacterHandling("EV45002.BIN", 0x00005dbc),\
	PatchSingleByteLipsHiCharacterHandling("EV45003.BIN", 0x00005be0),\
	PatchSingleByteLipsHiCharacterHandling("EV45010.BIN", 0x00005b90),\
	PatchSingleByteLipsHiCharacterHandling("EV45021.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV45022.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV45023.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV45033.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV45041.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV45042.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV45043.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV45044.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV45045.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV45046.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV45047.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV45048.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV45099.BIN", 0x00006280),\
	PatchSingleByteLipsHiCharacterHandling("EV46001.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV46002.BIN", 0x00005d20),\
	PatchSingleByteLipsHiCharacterHandling("EV46003.BIN", 0x00005c34),\
	PatchSingleByteLipsHiCharacterHandling("EV46020.BIN", 0x00005c90),\
	PatchSingleByteLipsHiCharacterHandling("EV46021.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV46030.BIN", 0x00005b88),\
	PatchSingleByteLipsHiCharacterHandling("EV47001.BIN", 0x00005eb0),\
	PatchSingleByteLipsHiCharacterHandling("EV47002.BIN", 0x00006284),\
	PatchSingleByteLipsHiCharacterHandling("EV47020.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV47021.BIN", 0x00005b24),\
	PatchSingleByteLipsHiCharacterHandling("EV47031.BIN", 0x00005c70),\
	PatchSingleByteLipsHiCharacterHandling("EV49001.BIN", 0x00005b34),\
	PatchSingleByteLipsHiCharacterHandling("EV50002.BIN", 0x000065dc),\
	PatchSingleByteLipsHiCharacterHandling("EV51002.BIN", 0x00005e44),\
	PatchSingleByteLipsHiCharacterHandling("EV52002.BIN", 0x00005e44),\
	PatchSingleByteLipsHiCharacterHandling("EV53002.BIN", 0x00005e44),\
	PatchSingleByteLipsHiCharacterHandling("EV53010.BIN", 0x00005ba0),\
	PatchSingleByteLipsHiCharacterHandling("M18PRG.BIN", 0x00078fcc),\
	PatchSingleByteLipsHiCharacterHandling("M22PRG.BIN", 0x00077f24)
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
	PatchSingleByteZeroExtend("SLGGOVER.BIN", 0x0001bb68),\
	PatchSingleByteZeroExtend("M07PRG.BIN", 0x0002e544),\
    PatchSingleByteZeroExtend("M08PRG.BIN", 0x00027dc4),\
    PatchSingleByteZeroExtend("M09PRG.BIN", 0x0002fbb4),\
    PatchSingleByteZeroExtend("M10PRG.BIN", 0x00027b0c),\
    PatchSingleByteZeroExtend("M11PRG.BIN", 0x0002951c),\
    PatchSingleByteZeroExtend("M12PRG.BIN", 0x000272c8),\
    PatchSingleByteZeroExtend("M13PRG.BIN", 0x000271ac),\
    PatchSingleByteZeroExtend("M15PRG.BIN", 0x00026304),\
    PatchSingleByteZeroExtend("M16PRG.BIN", 0x00020e1c),\
    PatchSingleByteZeroExtend("M17PRG.BIN", 0x00028d58),\
    PatchSingleByteZeroExtend("M18PRG.BIN", 0x00026894),\
    PatchSingleByteZeroExtend("M19PRG.BIN", 0x00020778),\
    PatchSingleByteZeroExtend("M20PRG.BIN", 0x0002654c),\
    PatchSingleByteZeroExtend("M21PRG.BIN", 0x0002e608),\
    PatchSingleByteZeroExtend("M22PRG.BIN", 0x0002f9e0),\
    PatchSingleByteZeroExtend("M23PRG.BIN", 0x0002de18),\
    PatchSingleByteZeroExtend("M24PRG.BIN", 0x0002e004),\
    PatchSingleByteZeroExtend("M25PRG.BIN", 0x0002df70),\
    PatchSingleByteZeroExtend("M36PRG.BIN", 0x0002edbc),\
    PatchSingleByteZeroExtend("M37PRG.BIN", 0x00020cc0),\
    PatchSingleByteZeroExtend("M45PRG.BIN", 0x000207e4),\
    PatchSingleByteZeroExtend("M46PRG.BIN", 0x000217a4),\
    PatchSingleByteZeroExtend("M47PRG.BIN", 0x0002d618),\
    PatchSingleByteZeroExtend("M49PRG.BIN", 0x00020c34),\
    PatchSingleByteZeroExtend("M50PRG.BIN", 0x0002556c),\
	PatchSingleByteZeroExtend("M51PRG.BIN", 0x0002d8b0),\
    PatchSingleByteZeroExtend("M52PRG.BIN", 0x000210a4),\
    PatchSingleByteZeroExtend("M53PRG.BIN", 0x0002e5a0),\
    PatchSingleByteZeroExtend("M54PRG.BIN", 0x0002df20),\
    PatchSingleByteZeroExtend("M91PRG.BIN", 0x0001d2cc),\
    PatchSingleByteZeroExtend("M92PRG.BIN", 0x00016810)


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
		PatchHeightSpacing("SLGGOVER.BIN", 0x0001bf20),\
		PatchHeightSpacing("M07PRG.BIN", 0x0002e8fc),\
		PatchHeightSpacing("M08PRG.BIN", 0x0002817c),\
		PatchHeightSpacing("M09PRG.BIN", 0x0002ff6c),\
		PatchHeightSpacing("M10PRG.BIN", 0x00027ec4),\
		PatchHeightSpacing("M11PRG.BIN", 0x000298d4),\
		PatchHeightSpacing("M12PRG.BIN", 0x00027680),\
		PatchHeightSpacing("M13PRG.BIN", 0x00027564),\
		PatchHeightSpacing("M15PRG.BIN", 0x000266bc),\
		PatchHeightSpacing("M16PRG.BIN", 0x000211d4),\
		PatchHeightSpacing("M17PRG.BIN", 0x00029110),\
		PatchHeightSpacing("M18PRG.BIN", 0x00026c4c),\
		PatchHeightSpacing("M19PRG.BIN", 0x00020b30),\
		PatchHeightSpacing("M20PRG.BIN", 0x00026904),\
		PatchHeightSpacing("M21PRG.BIN", 0x0002e9c0),\
		PatchHeightSpacing("M22PRG.BIN", 0x0002fd98),\
		PatchHeightSpacing("M23PRG.BIN", 0x0002e1d0),\
		PatchHeightSpacing("M24PRG.BIN", 0x0002e3bc),\
		PatchHeightSpacing("M25PRG.BIN", 0x0002e328),\
		PatchHeightSpacing("M36PRG.BIN", 0x0002f174),\
		PatchHeightSpacing("M37PRG.BIN", 0x00021078),\
		PatchHeightSpacing("M45PRG.BIN", 0x00020b9c),\
		PatchHeightSpacing("M46PRG.BIN", 0x00021b5c),\
		PatchHeightSpacing("M47PRG.BIN", 0x0002d9d0),\
		PatchHeightSpacing("M49PRG.BIN", 0x00020fec),\
		PatchHeightSpacing("M50PRG.BIN", 0x00025924),\
		PatchHeightSpacing("M51PRG.BIN", 0x0002dc68),\
		PatchHeightSpacing("M52PRG.BIN", 0x0002145c),\
		PatchHeightSpacing("M53PRG.BIN", 0x0002e958),\
		PatchHeightSpacing("M54PRG.BIN", 0x0002e2d8),\
		PatchHeightSpacing("M91PRG.BIN", 0x0001d684),\
		PatchHeightSpacing("M92PRG.BIN", 0x00016bc8)

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
		PatchWidthSpacing("SLGGOVER.BIN", 0x0001bf50),\
		PatchWidthSpacing("M07PRG.BIN", 0x0002e92c),\
		PatchWidthSpacing("M08PRG.BIN", 0x000281ac),\
		PatchWidthSpacing("M09PRG.BIN", 0x0002ff9c),\
		PatchWidthSpacing("M10PRG.BIN", 0x00027ef4),\
		PatchWidthSpacing("M11PRG.BIN", 0x00029904),\
		PatchWidthSpacing("M12PRG.BIN", 0x000276b0),\
		PatchWidthSpacing("M13PRG.BIN", 0x00027594),\
		PatchWidthSpacing("M15PRG.BIN", 0x000266ec),\
		PatchWidthSpacing("M16PRG.BIN", 0x00021204),\
		PatchWidthSpacing("M17PRG.BIN", 0x00029140),\
		PatchWidthSpacing("M18PRG.BIN", 0x00026c7c),\
		PatchWidthSpacing("M19PRG.BIN", 0x00020b60),\
		PatchWidthSpacing("M20PRG.BIN", 0x00026934),\
		PatchWidthSpacing("M21PRG.BIN", 0x0002e9f0),\
		PatchWidthSpacing("M22PRG.BIN", 0x0002fdc8),\
		PatchWidthSpacing("M23PRG.BIN", 0x0002e200),\
		PatchWidthSpacing("M24PRG.BIN", 0x0002e3ec),\
		PatchWidthSpacing("M25PRG.BIN", 0x0002e358),\
		PatchWidthSpacing("M36PRG.BIN", 0x0002f1a4),\
		PatchWidthSpacing("M37PRG.BIN", 0x000210a8),\
		PatchWidthSpacing("M45PRG.BIN", 0x00020bcc),\
		PatchWidthSpacing("M46PRG.BIN", 0x00021b8c),\
		PatchWidthSpacing("M47PRG.BIN", 0x0002da00),\
		PatchWidthSpacing("M49PRG.BIN", 0x0002101c),\
		PatchWidthSpacing("M50PRG.BIN", 0x00025954),\
		PatchWidthSpacing("M51PRG.BIN", 0x0002dc98),\
		PatchWidthSpacing("M52PRG.BIN", 0x0002148c),\
		PatchWidthSpacing("M53PRG.BIN", 0x0002e988),\
		PatchWidthSpacing("M54PRG.BIN", 0x0002e308),\
		PatchWidthSpacing("M91PRG.BIN", 0x0001d6b4),\
		PatchWidthSpacing("M92PRG.BIN", 0x00016bf8)

#if USE_SINGLE_BYTE_LOOKUPS
const uint16 BattleTextHorizontalFraming = 0xff60;
#else
const uint16 BattleTextHorizontalFraming = 0xffb8;
#endif

//Scaled Lips Formatting (example in Battle 5_4 LIPS 060d17e4)
#define PatchScaledLipsFormatting(filename, address, offset4, zoomValueOffset)\
	filename, address + 0,  0x7199, 0x7198, /*7198 to 7199*/\
	filename, address + 42, 0x1a01, 0x1a01, /*Scaled sprite - had reverted to normal sprite but now keeping scaling*/\
	filename, address + 48, 0x010c, 0x0210, /*Font size*/\
	filename, address + 50, BattleTextHorizontalFraming, 0xff68, /*Horizontal framing*/\
	filename, address + 92, 0xe70b, 0x5296, /*Zoom Center Y, move 0xb into r7, add to to r2, move r7 into @r2, move 0x7 back into r7 (060d1840)*/\
	filename, address + 94, 0x7202, 0x321c,\
	filename, address + 96, 0x2271, 0x7222,\
	filename, address + 98, 0xe707, 0x2271,\
	filename, address + 156 + offset4, 0x7108, 0x7110, /*Horizontal spacing 060d1880*/\
	filename, address - 114, 0xe707, 0xe70f+zoomValueOffset, /*Zoom Center X (060d1772)*/\
	filename, address - 50, 0x644c, 0x644d /*extu.w r4, 41 to extu.b r4,r4 060d17b2*/


#define PatchScaledLipsFormattingInFiles()\
	PatchScaledLipsFormatting("EV00001.BIN", 0x000076e0, 0, 0),\
	PatchScaledLipsFormatting("EV00002.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV00050.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV00051.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV00052.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV00054.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV00055.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV00060.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV01001.BIN", 0x000060f0, 0, 0),\
	PatchScaledLipsFormatting("EV01002.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV01020.BIN", 0x00006608, 0, 0),\
	PatchScaledLipsFormatting("EV01021.BIN", 0x0000641c, 0, 0),\
	PatchScaledLipsFormatting("EV01022.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV01023.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV01030.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV01050.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV01054.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV01055.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV02001.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV02002.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV02010.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV02021.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV02025.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV02050.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV02051.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV02052.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV02053.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV02054.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV03000.BIN", 0x00005cb8, -2, 1),\
	PatchScaledLipsFormatting("EV03001.BIN", 0x000061b0, 0, 0),\
	PatchScaledLipsFormatting("EV03002.BIN", 0x00006058, 0, 0),\
	PatchScaledLipsFormatting("EV03005.BIN", 0x00006010, 0, 0),\
	PatchScaledLipsFormatting("EV03010.BIN", 0x00005ff4, 0, 0),\
	PatchScaledLipsFormatting("EV03020.BIN", 0x00005ff4, 0, 0),\
	PatchScaledLipsFormatting("EV03021.BIN", 0x00005ff4, 0, 0),\
	PatchScaledLipsFormatting("EV03023.BIN", 0x0000600c, 0, 0),\
	PatchScaledLipsFormatting("EV03024.BIN", 0x0000600c, 0, 0),\
	PatchScaledLipsFormatting("EV03025.BIN", 0x00006020, 0, 0),\
	PatchScaledLipsFormatting("EV03050.BIN", 0x00006424, 0, 0),\
	PatchScaledLipsFormatting("EV03051.BIN", 0x00006048, 0, 0),\
	PatchScaledLipsFormatting("EV03052.BIN", 0x000060b4, 0, 0),\
	PatchScaledLipsFormatting("EV03053.BIN", 0x00006070, 0, 0),\
	PatchScaledLipsFormatting("EV04001.BIN", 0x00006e9c, 0, 0),\
	PatchScaledLipsFormatting("EV04002.BIN", 0x00006060, 0, 0),\
	PatchScaledLipsFormatting("EV04003.BIN", 0x00006194, 0, 0),\
	PatchScaledLipsFormatting("EV04005.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV04010.BIN", 0x00006058, 0, 0),\
	PatchScaledLipsFormatting("EV04020.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV04022.BIN", 0x00005fec, 0, 0),\
	PatchScaledLipsFormatting("EV04050.BIN", 0x0000608c, 0, 0),\
	PatchScaledLipsFormatting("EV04053.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV04055.BIN", 0x00006058, 0, 0),\
	PatchScaledLipsFormatting("EV05001.BIN", 0x00005ffc, 0, 0),\
	PatchScaledLipsFormatting("EV05002.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05003.BIN", 0x000061c0, 0, 0),\
	PatchScaledLipsFormatting("EV05004.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05005.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05007.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05010.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05011.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05018.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05019.BIN", 0x0000600c, 0, 0),\
	PatchScaledLipsFormatting("EV05020.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05021.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05022.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05023.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05025.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05026.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05027.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05051.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05052.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05053.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05054.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV05060.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV26001.BIN", 0x00006df0, 0, 0),\
	PatchScaledLipsFormatting("EV26002.BIN", 0x0000638c, 0, 0),\
	PatchScaledLipsFormatting("EV26020.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV26021.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV27001.BIN", 0x00005ff0, 0, 0),\
	PatchScaledLipsFormatting("EV27002.BIN", 0x00006098, 0, 0),\
	PatchScaledLipsFormatting("EV27054.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV28001.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV28025.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV28050.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV28051.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV29001.BIN", 0x00006008, 0, 0),\
	PatchScaledLipsFormatting("EV29002.BIN", 0x00006008, 0, 0),\
	PatchScaledLipsFormatting("EV29021.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV29050.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV30003.BIN", 0x0000702c, 0, 0),\
	PatchScaledLipsFormatting("EV30020.BIN", 0x00005ff4, 0, 0),\
	PatchScaledLipsFormatting("EV30026.BIN", 0x00005ff4, 0, 0),\
	PatchScaledLipsFormatting("EV30027.BIN", 0x00005ff4, 0, 0),\
	PatchScaledLipsFormatting("EV31001.BIN", 0x0000628c, 0, 0),\
	PatchScaledLipsFormatting("EV31002.BIN", 0x000061e4, 0, 0),\
	PatchScaledLipsFormatting("EV31023.BIN", 0x0000600c, 0, 0),\
	PatchScaledLipsFormatting("EV31024.BIN", 0x0000600c, 0, 0),\
	PatchScaledLipsFormatting("EV32002.BIN", 0x00006018, 0, 0),\
	PatchScaledLipsFormatting("EV32020.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV32055.BIN", 0x00006058, 0, 0),\
	PatchScaledLipsFormatting("EV33001.BIN", 0x00006538, 0, 0),\
	PatchScaledLipsFormatting("EV33002.BIN", 0x000062a0, 0, 0),\
	PatchScaledLipsFormatting("EV33022.BIN", 0x00005fec, 0, 0),\
	PatchScaledLipsFormatting("EV33054.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV34001.BIN", 0x00006008, 0, 0),\
	PatchScaledLipsFormatting("EV34002.BIN", 0x00006020, 0, 0),\
	PatchScaledLipsFormatting("EV34003.BIN", 0x00006008, 0, 0),\
	PatchScaledLipsFormatting("EV34004.BIN", 0x00006060, 0, 0),\
	PatchScaledLipsFormatting("EV34005.BIN", 0x00006060, 0, 0),\
	PatchScaledLipsFormatting("EV34006.BIN", 0x00006098, 0, 0),\
	PatchScaledLipsFormatting("EV34007.BIN", 0x00006008, 0, 0),\
	PatchScaledLipsFormatting("EV34008.BIN", 0x00006098, 0, 0),\
	PatchScaledLipsFormatting("EV34020.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV34030.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV34040.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV34041.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV34042.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV34043.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("EV34044.BIN", 0x00005fe4, 0, 0),\
	PatchScaledLipsFormatting("M00PRG.BIN",  0x00079ae0, 0, 0),\
	PatchScaledLipsFormatting("M26PRG.BIN",  0x000791f0, 0, 0),\
	PatchScaledLipsFormatting("TUTORI0.BIN", 0x0000615c, 0, 0),\
	PatchScaledLipsFormatting("EV07000.BIN", 0x00005cb8, -2, 0),\
    PatchScaledLipsFormatting("EV07001.BIN", 0x000064ac, 0, 0),\
    PatchScaledLipsFormatting("EV07002.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV07003.BIN", 0x00005ffc, 0, 0),\
    PatchScaledLipsFormatting("EV07005.BIN", 0x00006020, 0, 0),\
    PatchScaledLipsFormatting("EV07006.BIN", 0x00006030, 0, 0),\
    PatchScaledLipsFormatting("EV07007.BIN", 0x00006020, 0, 0),\
    PatchScaledLipsFormatting("EV07008.BIN", 0x00006030, 0, 0),\
    PatchScaledLipsFormatting("EV07009.BIN", 0x00006008, 0, 0),\
    PatchScaledLipsFormatting("EV07020.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV07021.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV08000.BIN", 0x00005cb8, -2, 0),\
    PatchScaledLipsFormatting("EV08001.BIN", 0x00006a18, 0, 0),\
    PatchScaledLipsFormatting("EV08002.BIN", 0x00006db8, 0, 0),\
    PatchScaledLipsFormatting("EV08003.BIN", 0x00006074, 0, 0),\
    PatchScaledLipsFormatting("EV08020.BIN", 0x00006040, 0, 0),\
    PatchScaledLipsFormatting("EV08021.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV08030.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV08032.BIN", 0x000061a4, 0, 0),\
    PatchScaledLipsFormatting("EV08033.BIN", 0x00006190, 0, 0),\
    PatchScaledLipsFormatting("EV08050.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV08060.BIN", 0x000073d0, 0, 0),\
    PatchScaledLipsFormatting("EV09001.BIN", 0x00006634, 0, 0),\
    PatchScaledLipsFormatting("EV09002.BIN", 0x0000600c, 0, 0),\
    PatchScaledLipsFormatting("EV09005.BIN", 0x000066b4, 0, 0),\
    PatchScaledLipsFormatting("EV09006.BIN", 0x000060d0, 0, 0),\
    PatchScaledLipsFormatting("EV09007.BIN", 0x00006198, 0, 0),\
    PatchScaledLipsFormatting("EV09010.BIN", 0x0000603c, 0, 0),\
    PatchScaledLipsFormatting("EV09011.BIN", 0x000065f8, 0, 0),\
    PatchScaledLipsFormatting("EV09025.BIN", 0x00006614, 0, 0),\
    PatchScaledLipsFormatting("EV09026.BIN", 0x000065f8, 0, 0),\
    PatchScaledLipsFormatting("EV09030.BIN", 0x0000600c, 0, 0),\
    PatchScaledLipsFormatting("EV09031.BIN", 0x0000600c, 0, 0),\
    PatchScaledLipsFormatting("EV09035.BIN", 0x00006030, 0, 0),\
    PatchScaledLipsFormatting("EV09041.BIN", 0x00005ff0, 0, 0),\
    PatchScaledLipsFormatting("EV09042.BIN", 0x00005ff0, 0, 0),\
    PatchScaledLipsFormatting("EV09043.BIN", 0x00005ff0, 0, 0),\
    PatchScaledLipsFormatting("EV09044.BIN", 0x00005ff0, 0, 0),\
    PatchScaledLipsFormatting("EV09045.BIN", 0x00005ff0, 0, 0),\
    PatchScaledLipsFormatting("EV09046.BIN", 0x00005ff0, 0, 0),\
    PatchScaledLipsFormatting("EV09048.BIN", 0x00005ff0, 0, 0),\
    PatchScaledLipsFormatting("EV10001.BIN", 0x00006130, 0, 0),\
    PatchScaledLipsFormatting("EV10002.BIN", 0x00006528, 0, 0),\
    PatchScaledLipsFormatting("EV10020.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV10025.BIN", 0x00006238, 0, 0),\
    PatchScaledLipsFormatting("EV10026.BIN", 0x0000676c, 0, 0),\
    PatchScaledLipsFormatting("EV10030.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV10035.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV11001.BIN", 0x000063a4, 0, 0),\
    PatchScaledLipsFormatting("EV11002.BIN", 0x00007d94, 0, 0),\
    PatchScaledLipsFormatting("EV11030.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV11040.BIN", 0x00006138, 0, 0),\
    PatchScaledLipsFormatting("EV12001.BIN", 0x0000628c, 0, 0),\
    PatchScaledLipsFormatting("EV12002.BIN", 0x00006090, 0, 0),\
    PatchScaledLipsFormatting("EV12035.BIN", 0x000060b0, 0, 0),\
    PatchScaledLipsFormatting("EV13001.BIN", 0x000062d8, 0, 0),\
    PatchScaledLipsFormatting("EV13002.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV13035.BIN", 0x000060b0, 0, 0),\
    PatchScaledLipsFormatting("EV15001.BIN", 0x00006120, 0, 0),\
    PatchScaledLipsFormatting("EV15002.BIN", 0x0000691c, 0, 0),\
    PatchScaledLipsFormatting("EV16001.BIN", 0x00005fe0, 0, 0),\
    PatchScaledLipsFormatting("EV16002.BIN", 0x00005fe0, 0, 0),\
    PatchScaledLipsFormatting("EV16035.BIN", 0x00006038, 0, 0),\
    PatchScaledLipsFormatting("EV16051.BIN", 0x0000658c, 0, 0),\
    PatchScaledLipsFormatting("EV16052.BIN", 0x00005fe0, 0, 0),\
    PatchScaledLipsFormatting("EV16053.BIN", 0x00006568, 0, 0),\
    PatchScaledLipsFormatting("EV17001.BIN", 0x00006198, 0, 0),\
    PatchScaledLipsFormatting("EV17002.BIN", 0x00007f24, 0, 0),\
    PatchScaledLipsFormatting("EV17010.BIN", 0x000065bc, 0, 0),\
    PatchScaledLipsFormatting("EV17030.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV17031.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV17035.BIN", 0x00006070, 0, 0),\
    PatchScaledLipsFormatting("EV18001.BIN", 0x0000708c, 0, 0),\
    PatchScaledLipsFormatting("EV18002.BIN", 0x00006518, 0, 0),\
    PatchScaledLipsFormatting("EV18020.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV18030.BIN", 0x000060a4, 0, 0),\
    PatchScaledLipsFormatting("EV18040.BIN", 0x000067a4, 0, 0),\
    PatchScaledLipsFormatting("EV19001.BIN", 0x000060b8, 0, 0),\
    PatchScaledLipsFormatting("EV19002.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV19003.BIN", 0x000060a0, 0, 0),\
    PatchScaledLipsFormatting("EV19020.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV19021.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV19022.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV19023.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV19041.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV19042.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV19043.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV19044.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV19045.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV19046.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV19047.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV19048.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV20001.BIN", 0x00006150, 0, 0),\
    PatchScaledLipsFormatting("EV20002.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV20003.BIN", 0x00006110, 0, 0),\
    PatchScaledLipsFormatting("EV20020.BIN", 0x00006014, 0, 0),\
    PatchScaledLipsFormatting("EV20021.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV20022.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV20023.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV20024.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV20041.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV21001.BIN", 0x00006040, 0, 0),\
    PatchScaledLipsFormatting("EV21002.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV21010.BIN", 0x00006060, 0, 0),\
    PatchScaledLipsFormatting("EV21020.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV21031.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV21032.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV21033.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV21034.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV21035.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV21036.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV21037.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV21038.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV21050.BIN", 0x000060a0, 0, 0),\
    PatchScaledLipsFormatting("EV21060.BIN", 0x00006018, 0, 0),\
    PatchScaledLipsFormatting("EV21061.BIN", 0x00006018, 0, 0),\
    PatchScaledLipsFormatting("EV21062.BIN", 0x00006018, 0, 0),\
    PatchScaledLipsFormatting("EV22001.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV22002.BIN", 0x00006da0, 0, 0),\
    PatchScaledLipsFormatting("EV22020.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV22021.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV22022.BIN", 0x00006ad0, 0, 0),\
    PatchScaledLipsFormatting("EV22023.BIN", 0x00006b60, 0, 0),\
    PatchScaledLipsFormatting("EV22024.BIN", 0x00006670, 0, 0),\
    PatchScaledLipsFormatting("EV22025.BIN", 0x0000665c, 0, 0),\
    PatchScaledLipsFormatting("EV22026.BIN", 0x00006458, 0, 0),\
    PatchScaledLipsFormatting("EV22027.BIN", 0x00006044, 0, 0),\
    PatchScaledLipsFormatting("EV22030.BIN", 0x00005ffc, 0, 0),\
    PatchScaledLipsFormatting("EV22031.BIN", 0x00005ffc, 0, 0),\
    PatchScaledLipsFormatting("EV22040.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV23001.BIN", 0x0000604c, 0, 0),\
    PatchScaledLipsFormatting("EV23002.BIN", 0x00006c7c, 0, 0),\
    PatchScaledLipsFormatting("EV23003.BIN", 0x00006078, 0, 0),\
    PatchScaledLipsFormatting("EV23010.BIN", 0x000061f8, 0, 0),\
    PatchScaledLipsFormatting("EV23020.BIN", 0x000061f8, 0, 0),\
    PatchScaledLipsFormatting("EV23021.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV23022.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV23023.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV23041.BIN", 0x00006084, 0, 0),\
    PatchScaledLipsFormatting("EV23042.BIN", 0x00006084, 0, 0),\
    PatchScaledLipsFormatting("EV23043.BIN", 0x000060a8, 0, 0),\
    PatchScaledLipsFormatting("EV23044.BIN", 0x00006084, 0, 0),\
    PatchScaledLipsFormatting("EV23045.BIN", 0x00006084, 0, 0),\
    PatchScaledLipsFormatting("EV23046.BIN", 0x000060a8, 0, 0),\
    PatchScaledLipsFormatting("EV23047.BIN", 0x00006084, 0, 0),\
    PatchScaledLipsFormatting("EV23061.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV23063.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV23064.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV23065.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV23067.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV23068.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV24001.BIN", 0x00006444, 0, 0),\
    PatchScaledLipsFormatting("EV24002.BIN", 0x000067a0, 0, 0),\
    PatchScaledLipsFormatting("EV24021.BIN", 0x00006154, 0, 0),\
    PatchScaledLipsFormatting("EV24040.BIN", 0x00006188, 0, 0),\
    PatchScaledLipsFormatting("EV25001.BIN", 0x000061d8, 0, 0),\
    PatchScaledLipsFormatting("EV25002.BIN", 0x00006c00, 0, 0),\
    PatchScaledLipsFormatting("EV25003.BIN", 0x00006acc, 0, 0),\
    PatchScaledLipsFormatting("EV25004.BIN", 0x000065b0, 0, 0),\
    PatchScaledLipsFormatting("EV25005.BIN", 0x00006524, 0, 0),\
    PatchScaledLipsFormatting("EV25006.BIN", 0x00006284, 0, 0),\
    PatchScaledLipsFormatting("EV25007.BIN", 0x00006540, 0, 0),\
    PatchScaledLipsFormatting("EV25008.BIN", 0x00006534, 0, 0),\
    PatchScaledLipsFormatting("EV25021.BIN", 0x00006004, 0, 0),\
    PatchScaledLipsFormatting("EV25022.BIN", 0x00006004, 0, 0),\
    PatchScaledLipsFormatting("EV25023.BIN", 0x00006004, 0, 0),\
    PatchScaledLipsFormatting("EV25024.BIN", 0x00006004, 0, 0),\
    PatchScaledLipsFormatting("EV25025.BIN", 0x00006004, 0, 0),\
    PatchScaledLipsFormatting("EV25026.BIN", 0x00006004, 0, 0),\
    PatchScaledLipsFormatting("EV25027.BIN", 0x00006004, 0, 0),\
    PatchScaledLipsFormatting("EV25028.BIN", 0x00006004, 0, 0),\
    PatchScaledLipsFormatting("EV25030.BIN", 0x00006154, 0, 0),\
    PatchScaledLipsFormatting("EV25040.BIN", 0x00006130, 0, 0),\
    PatchScaledLipsFormatting("EV25050.BIN", 0x00006294, 0, 0),\
    PatchScaledLipsFormatting("EV35001.BIN", 0x000064ac, 0, 0),\
    PatchScaledLipsFormatting("EV35003.BIN", 0x00005ffc, 0, 0),\
    PatchScaledLipsFormatting("EV35021.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV36001.BIN", 0x000063c0, 0, 0),\
    PatchScaledLipsFormatting("EV36002.BIN", 0x00006cd8, 0, 0),\
    PatchScaledLipsFormatting("EV36020.BIN", 0x00005fe0, 0, 0),\
    PatchScaledLipsFormatting("EV36026.BIN", 0x00005fe0, 0, 0),\
    PatchScaledLipsFormatting("EV36028.BIN", 0x00005fe0, 0, 0),\
    PatchScaledLipsFormatting("EV36030.BIN", 0x00006588, 0, 0),\
    PatchScaledLipsFormatting("EV37001.BIN", 0x000069b8, 0, 0),\
    PatchScaledLipsFormatting("EV37002.BIN", 0x00006040, 0, 0),\
    PatchScaledLipsFormatting("EV45001.BIN", 0x00006178, 0, 0),\
    PatchScaledLipsFormatting("EV45002.BIN", 0x0000627c, 0, 0),\
    PatchScaledLipsFormatting("EV45003.BIN", 0x000060a0, 0, 0),\
    PatchScaledLipsFormatting("EV45010.BIN", 0x00006050, 0, 0),\
    PatchScaledLipsFormatting("EV45021.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV45022.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV45023.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV45033.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV45041.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV45042.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV45043.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV45044.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV45045.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV45046.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV45047.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV45048.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV45099.BIN", 0x00006740, 0, 0),\
    PatchScaledLipsFormatting("EV46001.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV46002.BIN", 0x000061e0, 0, 0),\
    PatchScaledLipsFormatting("EV46003.BIN", 0x000060f4, 0, 0),\
    PatchScaledLipsFormatting("EV46020.BIN", 0x00006150, 0, 0),\
    PatchScaledLipsFormatting("EV46021.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV46030.BIN", 0x00006048, 0, 0),\
    PatchScaledLipsFormatting("EV47001.BIN", 0x00006370, 0, 0),\
    PatchScaledLipsFormatting("EV47002.BIN", 0x00006744, 0, 0),\
    PatchScaledLipsFormatting("EV47020.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV47021.BIN", 0x00005fe4, 0, 0),\
    PatchScaledLipsFormatting("EV47031.BIN", 0x00006130, 0, 0),\
    PatchScaledLipsFormatting("EV49001.BIN", 0x00005ff4, 0, 0),\
    PatchScaledLipsFormatting("EV50002.BIN", 0x00006a9c, 0, 0),\
    PatchScaledLipsFormatting("EV51002.BIN", 0x00006304, 0, 0),\
    PatchScaledLipsFormatting("EV52002.BIN", 0x00006304, 0, 0),\
    PatchScaledLipsFormatting("EV53002.BIN", 0x00006304, 0, 0),\
    PatchScaledLipsFormatting("EV53010.BIN", 0x00006060, 0, 0),\
    PatchScaledLipsFormatting("M18PRG.BIN", 0x0007948c,	 0, 0),\
    PatchScaledLipsFormatting("M22PRG.BIN", 0x000783e4,	 0, 0)

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
	PatchScaledLipsSpriteBuffer("TUTORI0.BIN", 0x00006182, 0, 0),\
	PatchScaledLipsSpriteBuffer("EV07000.BIN", 0x00005cde, -2, -4),\
    PatchScaledLipsSpriteBuffer("EV07001.BIN", 0x000064d2, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV07002.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV07003.BIN", 0x00006022, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV07005.BIN", 0x00006046, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV07006.BIN", 0x00006056, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV07007.BIN", 0x00006046, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV07008.BIN", 0x00006056, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV07009.BIN", 0x0000602e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV07020.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV07021.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV08000.BIN", 0x00005cde, -2, -4),\
    PatchScaledLipsSpriteBuffer("EV08001.BIN", 0x00006a3e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV08002.BIN", 0x00006dde, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV08003.BIN", 0x0000609a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV08020.BIN", 0x00006066, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV08021.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV08030.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV08032.BIN", 0x000061ca, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV08033.BIN", 0x000061b6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV08050.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV08060.BIN", 0x000073f6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09001.BIN", 0x0000665a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09002.BIN", 0x00006032, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09005.BIN", 0x000066da, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09006.BIN", 0x000060f6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09007.BIN", 0x000061be, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09010.BIN", 0x00006062, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09011.BIN", 0x0000661e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09025.BIN", 0x0000663a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09026.BIN", 0x0000661e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09030.BIN", 0x00006032, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09031.BIN", 0x00006032, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09035.BIN", 0x00006056, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09041.BIN", 0x00006016, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09042.BIN", 0x00006016, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09043.BIN", 0x00006016, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09044.BIN", 0x00006016, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09045.BIN", 0x00006016, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09046.BIN", 0x00006016, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV09048.BIN", 0x00006016, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV10001.BIN", 0x00006156, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV10002.BIN", 0x0000654e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV10020.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV10025.BIN", 0x0000625e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV10026.BIN", 0x00006792, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV10030.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV10035.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV11001.BIN", 0x000063ca, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV11002.BIN", 0x00007dba, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV11030.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV11040.BIN", 0x0000615e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV12001.BIN", 0x000062b2, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV12002.BIN", 0x000060b6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV12035.BIN", 0x000060d6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV13001.BIN", 0x000062fe, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV13002.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV13035.BIN", 0x000060d6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV15001.BIN", 0x00006146, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV15002.BIN", 0x00006942, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV16001.BIN", 0x00006006, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV16002.BIN", 0x00006006, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV16035.BIN", 0x0000605e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV16051.BIN", 0x000065b2, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV16052.BIN", 0x00006006, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV16053.BIN", 0x0000658e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV17001.BIN", 0x000061be, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV17002.BIN", 0x00007f4a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV17010.BIN", 0x000065e2, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV17030.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV17031.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV17035.BIN", 0x00006096, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV18001.BIN", 0x000070b2, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV18002.BIN", 0x0000653e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV18020.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV18030.BIN", 0x000060ca, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV18040.BIN", 0x000067ca, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19001.BIN", 0x000060de, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19002.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19003.BIN", 0x000060c6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19020.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19021.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19022.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19023.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19041.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19042.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19043.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19044.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19045.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19046.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19047.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV19048.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV20001.BIN", 0x00006176, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV20002.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV20003.BIN", 0x00006136, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV20020.BIN", 0x0000603a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV20021.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV20022.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV20023.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV20024.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV20041.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21001.BIN", 0x00006066, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21002.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21010.BIN", 0x00006086, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21020.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21031.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21032.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21033.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21034.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21035.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21036.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21037.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21038.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21050.BIN", 0x000060c6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21060.BIN", 0x0000603e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21061.BIN", 0x0000603e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV21062.BIN", 0x0000603e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV22001.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV22002.BIN", 0x00006dc6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV22020.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV22021.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV22022.BIN", 0x00006af6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV22023.BIN", 0x00006b86, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV22024.BIN", 0x00006696, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV22025.BIN", 0x00006682, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV22026.BIN", 0x0000647e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV22027.BIN", 0x0000606a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV22030.BIN", 0x00006022, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV22031.BIN", 0x00006022, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV22040.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23001.BIN", 0x00006072, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23002.BIN", 0x00006ca2, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23003.BIN", 0x0000609e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23010.BIN", 0x0000621e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23020.BIN", 0x0000621e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23021.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23022.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23023.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23041.BIN", 0x000060aa, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23042.BIN", 0x000060aa, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23043.BIN", 0x000060ce, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23044.BIN", 0x000060aa, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23045.BIN", 0x000060aa, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23046.BIN", 0x000060ce, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23047.BIN", 0x000060aa, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23061.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23063.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23064.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23065.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23067.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV23068.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV24001.BIN", 0x0000646a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV24002.BIN", 0x000067c6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV24021.BIN", 0x0000617a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV24040.BIN", 0x000061ae, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25001.BIN", 0x000061fe, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25002.BIN", 0x00006c26, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25003.BIN", 0x00006af2, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25004.BIN", 0x000065d6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25005.BIN", 0x0000654a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25006.BIN", 0x000062aa, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25007.BIN", 0x00006566, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25008.BIN", 0x0000655a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25021.BIN", 0x0000602a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25022.BIN", 0x0000602a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25023.BIN", 0x0000602a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25024.BIN", 0x0000602a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25025.BIN", 0x0000602a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25026.BIN", 0x0000602a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25027.BIN", 0x0000602a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25028.BIN", 0x0000602a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25030.BIN", 0x0000617a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25040.BIN", 0x00006156, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV25050.BIN", 0x000062ba, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV35001.BIN", 0x000064d2, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV35003.BIN", 0x00006022, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV35021.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV36001.BIN", 0x000063e6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV36002.BIN", 0x00006cfe, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV36020.BIN", 0x00006006, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV36026.BIN", 0x00006006, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV36028.BIN", 0x00006006, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV36030.BIN", 0x000065ae, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV37001.BIN", 0x000069de, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV37002.BIN", 0x00006066, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45001.BIN", 0x0000619e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45002.BIN", 0x000062a2, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45003.BIN", 0x000060c6, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45010.BIN", 0x00006076, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45021.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45022.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45023.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45033.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45041.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45042.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45043.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45044.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45045.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45046.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45047.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45048.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV45099.BIN", 0x00006766, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV46001.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV46002.BIN", 0x00006206, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV46003.BIN", 0x0000611a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV46020.BIN", 0x00006176, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV46021.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV46030.BIN", 0x0000606e, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV47001.BIN", 0x00006396, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV47002.BIN", 0x0000676a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV47020.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV47021.BIN", 0x0000600a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV47031.BIN", 0x00006156, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV49001.BIN", 0x0000601a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV50002.BIN", 0x00006ac2, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV51002.BIN", 0x0000632a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV52002.BIN", 0x0000632a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV53002.BIN", 0x0000632a, 0, 0),\
    PatchScaledLipsSpriteBuffer("EV53010.BIN", 0x00006086, 0, 0),\
    PatchScaledLipsSpriteBuffer("M18PRG.BIN", 0x000794b2,  0, 0),\
    PatchScaledLipsSpriteBuffer("M22PRG.BIN", 0x0007840a,  0, 0)

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
	PatchScaledLipsTileStride("TUTORI0.BIN", 0x00005c08),\
	PatchScaledLipsTileStride("EV07000.BIN", 0x00005764),\
    PatchScaledLipsTileStride("EV07001.BIN", 0x00005f58),\
    PatchScaledLipsTileStride("EV07002.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV07003.BIN", 0x00005aa8),\
    PatchScaledLipsTileStride("EV07005.BIN", 0x00005acc),\
    PatchScaledLipsTileStride("EV07006.BIN", 0x00005adc),\
    PatchScaledLipsTileStride("EV07007.BIN", 0x00005acc),\
    PatchScaledLipsTileStride("EV07008.BIN", 0x00005adc),\
    PatchScaledLipsTileStride("EV07009.BIN", 0x00005ab4),\
    PatchScaledLipsTileStride("EV07020.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV07021.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV08000.BIN", 0x00005764),\
    PatchScaledLipsTileStride("EV08001.BIN", 0x000064c4),\
    PatchScaledLipsTileStride("EV08002.BIN", 0x00006864),\
    PatchScaledLipsTileStride("EV08003.BIN", 0x00005b20),\
    PatchScaledLipsTileStride("EV08020.BIN", 0x00005aec),\
    PatchScaledLipsTileStride("EV08021.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV08030.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV08032.BIN", 0x00005c50),\
    PatchScaledLipsTileStride("EV08033.BIN", 0x00005c3c),\
    PatchScaledLipsTileStride("EV08050.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV08060.BIN", 0x00006e7c),\
    PatchScaledLipsTileStride("EV09001.BIN", 0x000060e0),\
    PatchScaledLipsTileStride("EV09002.BIN", 0x00005ab8),\
    PatchScaledLipsTileStride("EV09005.BIN", 0x00006160),\
    PatchScaledLipsTileStride("EV09006.BIN", 0x00005b7c),\
    PatchScaledLipsTileStride("EV09007.BIN", 0x00005c44),\
    PatchScaledLipsTileStride("EV09010.BIN", 0x00005ae8),\
    PatchScaledLipsTileStride("EV09011.BIN", 0x000060a4),\
    PatchScaledLipsTileStride("EV09025.BIN", 0x000060c0),\
    PatchScaledLipsTileStride("EV09026.BIN", 0x000060a4),\
    PatchScaledLipsTileStride("EV09030.BIN", 0x00005ab8),\
    PatchScaledLipsTileStride("EV09031.BIN", 0x00005ab8),\
    PatchScaledLipsTileStride("EV09035.BIN", 0x00005adc),\
    PatchScaledLipsTileStride("EV09041.BIN", 0x00005a9c),\
    PatchScaledLipsTileStride("EV09042.BIN", 0x00005a9c),\
    PatchScaledLipsTileStride("EV09043.BIN", 0x00005a9c),\
    PatchScaledLipsTileStride("EV09044.BIN", 0x00005a9c),\
    PatchScaledLipsTileStride("EV09045.BIN", 0x00005a9c),\
    PatchScaledLipsTileStride("EV09046.BIN", 0x00005a9c),\
    PatchScaledLipsTileStride("EV09048.BIN", 0x00005a9c),\
    PatchScaledLipsTileStride("EV10001.BIN", 0x00005bdc),\
    PatchScaledLipsTileStride("EV10002.BIN", 0x00005fd4),\
    PatchScaledLipsTileStride("EV10020.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV10025.BIN", 0x00005ce4),\
    PatchScaledLipsTileStride("EV10026.BIN", 0x00006218),\
    PatchScaledLipsTileStride("EV10030.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV10035.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV11001.BIN", 0x00005e50),\
    PatchScaledLipsTileStride("EV11002.BIN", 0x00007840),\
    PatchScaledLipsTileStride("EV11030.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV11040.BIN", 0x00005be4),\
    PatchScaledLipsTileStride("EV12001.BIN", 0x00005d38),\
    PatchScaledLipsTileStride("EV12002.BIN", 0x00005b3c),\
    PatchScaledLipsTileStride("EV12035.BIN", 0x00005b5c),\
    PatchScaledLipsTileStride("EV13001.BIN", 0x00005d84),\
    PatchScaledLipsTileStride("EV13002.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV13035.BIN", 0x00005b5c),\
    PatchScaledLipsTileStride("EV15001.BIN", 0x00005bcc),\
    PatchScaledLipsTileStride("EV15002.BIN", 0x000063c8),\
    PatchScaledLipsTileStride("EV16001.BIN", 0x00005a8c),\
    PatchScaledLipsTileStride("EV16002.BIN", 0x00005a8c),\
    PatchScaledLipsTileStride("EV16035.BIN", 0x00005ae4),\
    PatchScaledLipsTileStride("EV16051.BIN", 0x00006038),\
    PatchScaledLipsTileStride("EV16052.BIN", 0x00005a8c),\
    PatchScaledLipsTileStride("EV16053.BIN", 0x00006014),\
    PatchScaledLipsTileStride("EV17001.BIN", 0x00005c44),\
    PatchScaledLipsTileStride("EV17002.BIN", 0x000079d0),\
    PatchScaledLipsTileStride("EV17010.BIN", 0x00006068),\
    PatchScaledLipsTileStride("EV17030.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV17031.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV17035.BIN", 0x00005b1c),\
    PatchScaledLipsTileStride("EV18001.BIN", 0x00006b38),\
    PatchScaledLipsTileStride("EV18002.BIN", 0x00005fc4),\
    PatchScaledLipsTileStride("EV18020.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV18030.BIN", 0x00005b50),\
    PatchScaledLipsTileStride("EV18040.BIN", 0x00006250),\
    PatchScaledLipsTileStride("EV19001.BIN", 0x00005b64),\
    PatchScaledLipsTileStride("EV19002.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV19003.BIN", 0x00005b4c),\
    PatchScaledLipsTileStride("EV19020.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV19021.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV19022.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV19023.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV19041.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV19042.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV19043.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV19044.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV19045.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV19046.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV19047.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV19048.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV20001.BIN", 0x00005bfc),\
    PatchScaledLipsTileStride("EV20002.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV20003.BIN", 0x00005bbc),\
    PatchScaledLipsTileStride("EV20020.BIN", 0x00005ac0),\
    PatchScaledLipsTileStride("EV20021.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV20022.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV20023.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV20024.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV20041.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV21001.BIN", 0x00005aec),\
    PatchScaledLipsTileStride("EV21002.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV21010.BIN", 0x00005b0c),\
    PatchScaledLipsTileStride("EV21020.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV21031.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV21032.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV21033.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV21034.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV21035.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV21036.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV21037.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV21038.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV21050.BIN", 0x00005b4c),\
    PatchScaledLipsTileStride("EV21060.BIN", 0x00005ac4),\
    PatchScaledLipsTileStride("EV21061.BIN", 0x00005ac4),\
    PatchScaledLipsTileStride("EV21062.BIN", 0x00005ac4),\
    PatchScaledLipsTileStride("EV22001.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV22002.BIN", 0x0000684c),\
    PatchScaledLipsTileStride("EV22020.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV22021.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV22022.BIN", 0x0000657c),\
    PatchScaledLipsTileStride("EV22023.BIN", 0x0000660c),\
    PatchScaledLipsTileStride("EV22024.BIN", 0x0000611c),\
    PatchScaledLipsTileStride("EV22025.BIN", 0x00006108),\
    PatchScaledLipsTileStride("EV22026.BIN", 0x00005f04),\
    PatchScaledLipsTileStride("EV22027.BIN", 0x00005af0),\
    PatchScaledLipsTileStride("EV22030.BIN", 0x00005aa8),\
    PatchScaledLipsTileStride("EV22031.BIN", 0x00005aa8),\
    PatchScaledLipsTileStride("EV22040.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV23001.BIN", 0x00005af8),\
    PatchScaledLipsTileStride("EV23002.BIN", 0x00006728),\
    PatchScaledLipsTileStride("EV23003.BIN", 0x00005b24),\
    PatchScaledLipsTileStride("EV23010.BIN", 0x00005ca4),\
    PatchScaledLipsTileStride("EV23020.BIN", 0x00005ca4),\
    PatchScaledLipsTileStride("EV23021.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV23022.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV23023.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV23041.BIN", 0x00005b30),\
    PatchScaledLipsTileStride("EV23042.BIN", 0x00005b30),\
    PatchScaledLipsTileStride("EV23043.BIN", 0x00005b54),\
    PatchScaledLipsTileStride("EV23044.BIN", 0x00005b30),\
    PatchScaledLipsTileStride("EV23045.BIN", 0x00005b30),\
    PatchScaledLipsTileStride("EV23046.BIN", 0x00005b54),\
    PatchScaledLipsTileStride("EV23047.BIN", 0x00005b30),\
    PatchScaledLipsTileStride("EV23061.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV23063.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV23064.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV23065.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV23067.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV23068.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV24001.BIN", 0x00005ef0),\
    PatchScaledLipsTileStride("EV24002.BIN", 0x0000624c),\
    PatchScaledLipsTileStride("EV24021.BIN", 0x00005c00),\
    PatchScaledLipsTileStride("EV24040.BIN", 0x00005c34),\
    PatchScaledLipsTileStride("EV25001.BIN", 0x00005c84),\
    PatchScaledLipsTileStride("EV25002.BIN", 0x000066ac),\
    PatchScaledLipsTileStride("EV25003.BIN", 0x00006578),\
    PatchScaledLipsTileStride("EV25004.BIN", 0x0000605c),\
    PatchScaledLipsTileStride("EV25005.BIN", 0x00005fd0),\
    PatchScaledLipsTileStride("EV25006.BIN", 0x00005d30),\
    PatchScaledLipsTileStride("EV25007.BIN", 0x00005fec),\
    PatchScaledLipsTileStride("EV25008.BIN", 0x00005fe0),\
    PatchScaledLipsTileStride("EV25021.BIN", 0x00005ab0),\
    PatchScaledLipsTileStride("EV25022.BIN", 0x00005ab0),\
    PatchScaledLipsTileStride("EV25023.BIN", 0x00005ab0),\
    PatchScaledLipsTileStride("EV25024.BIN", 0x00005ab0),\
    PatchScaledLipsTileStride("EV25025.BIN", 0x00005ab0),\
    PatchScaledLipsTileStride("EV25026.BIN", 0x00005ab0),\
    PatchScaledLipsTileStride("EV25027.BIN", 0x00005ab0),\
    PatchScaledLipsTileStride("EV25028.BIN", 0x00005ab0),\
    PatchScaledLipsTileStride("EV25030.BIN", 0x00005c00),\
    PatchScaledLipsTileStride("EV25040.BIN", 0x00005bdc),\
    PatchScaledLipsTileStride("EV25050.BIN", 0x00005d40),\
    PatchScaledLipsTileStride("EV35001.BIN", 0x00005f58),\
    PatchScaledLipsTileStride("EV35003.BIN", 0x00005aa8),\
    PatchScaledLipsTileStride("EV35021.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV36001.BIN", 0x00005e6c),\
    PatchScaledLipsTileStride("EV36002.BIN", 0x00006784),\
    PatchScaledLipsTileStride("EV36020.BIN", 0x00005a8c),\
    PatchScaledLipsTileStride("EV36026.BIN", 0x00005a8c),\
    PatchScaledLipsTileStride("EV36028.BIN", 0x00005a8c),\
    PatchScaledLipsTileStride("EV36030.BIN", 0x00006034),\
    PatchScaledLipsTileStride("EV37001.BIN", 0x00006464),\
    PatchScaledLipsTileStride("EV37002.BIN", 0x00005aec),\
    PatchScaledLipsTileStride("EV45001.BIN", 0x00005c24),\
    PatchScaledLipsTileStride("EV45002.BIN", 0x00005d28),\
    PatchScaledLipsTileStride("EV45003.BIN", 0x00005b4c),\
    PatchScaledLipsTileStride("EV45010.BIN", 0x00005afc),\
    PatchScaledLipsTileStride("EV45021.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV45022.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV45023.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV45033.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV45041.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV45042.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV45043.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV45044.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV45045.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV45046.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV45047.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV45048.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV45099.BIN", 0x000061ec),\
    PatchScaledLipsTileStride("EV46001.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV46002.BIN", 0x00005c8c),\
    PatchScaledLipsTileStride("EV46003.BIN", 0x00005ba0),\
    PatchScaledLipsTileStride("EV46020.BIN", 0x00005bfc),\
    PatchScaledLipsTileStride("EV46021.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV46030.BIN", 0x00005af4),\
    PatchScaledLipsTileStride("EV47001.BIN", 0x00005e1c),\
    PatchScaledLipsTileStride("EV47002.BIN", 0x000061f0),\
    PatchScaledLipsTileStride("EV47020.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV47021.BIN", 0x00005a90),\
    PatchScaledLipsTileStride("EV47031.BIN", 0x00005bdc),\
    PatchScaledLipsTileStride("EV49001.BIN", 0x00005aa0),\
    PatchScaledLipsTileStride("EV50002.BIN", 0x00006548),\
    PatchScaledLipsTileStride("EV51002.BIN", 0x00005db0),\
    PatchScaledLipsTileStride("EV52002.BIN", 0x00005db0),\
    PatchScaledLipsTileStride("EV53002.BIN", 0x00005db0),\
    PatchScaledLipsTileStride("EV53010.BIN", 0x00005b0c),\
    PatchScaledLipsTileStride("M18PRG.BIN", 0x00078f38),\
    PatchScaledLipsTileStride("M22PRG.BIN", 0x00077e90)

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
#define PatchScaledLipsSingleByteReading(filename, address, offset1, offset2)\
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
	filename, address + 1546 + offset1, 0x0009, 0x300c,/*060d1886 add r0, r0*/\
	filename, address + 1548 + offset1, 0x01dc, 0x01dd,/*mov.w @(r0, r13), r1*/\
	filename, address + 1552 + offset1, 0x611c, 0x611d,/*060d188c extu.w r1, r1*/\
	filename, address + 1766 + offset2, 0xff, 0xffff/*060d1962 is ffff*/

#define PatchScaledLipsSingleByteReadingInFiles()\
	PatchScaledLipsSingleByteReading("EV00001.BIN", 0x00007178, 0, 0),\
	PatchScaledLipsSingleByteReading("EV00002.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV00050.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV00051.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV00052.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV00054.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV00055.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV00060.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV01001.BIN", 0x00005b88, 0, 0),\
	PatchScaledLipsSingleByteReading("EV01002.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV01020.BIN", 0x000060a0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV01021.BIN", 0x00005eb4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV01022.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV01023.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV01030.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV01050.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV01054.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV01055.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV02001.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV02002.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV02010.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV02021.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV02025.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV02050.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV02051.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV02052.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV02053.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV02054.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV03000.BIN", 0x00005750, -2, -4),\
	PatchScaledLipsSingleByteReading("EV03001.BIN", 0x00005c48, 0, 0),\
	PatchScaledLipsSingleByteReading("EV03002.BIN", 0x00005af0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV03005.BIN", 0x00005aa8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV03010.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV03020.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV03021.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV03023.BIN", 0x00005aa4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV03024.BIN", 0x00005aa4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV03025.BIN", 0x00005ab8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV03050.BIN", 0x00005ebc, 0, 0),\
	PatchScaledLipsSingleByteReading("EV03051.BIN", 0x00005ae0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV03052.BIN", 0x00005b4c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV03053.BIN", 0x00005b08, 0, 0),\
	PatchScaledLipsSingleByteReading("EV04001.BIN", 0x00006934, 0, 0),\
	PatchScaledLipsSingleByteReading("EV04002.BIN", 0x00005af8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV04003.BIN", 0x00005c2c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV04005.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV04010.BIN", 0x00005af0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV04020.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV04022.BIN", 0x00005a84, 0, 0),\
	PatchScaledLipsSingleByteReading("EV04050.BIN", 0x00005b24, 0, 0),\
	PatchScaledLipsSingleByteReading("EV04053.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV04055.BIN", 0x00005af0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05001.BIN", 0x00005a94, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05002.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05003.BIN", 0x00005c58, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05004.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05005.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05007.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05010.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05011.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05018.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05019.BIN", 0x00005aa4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05020.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05021.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05022.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05023.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05025.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05026.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05027.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05051.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05052.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05053.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05054.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV05060.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV26001.BIN", 0x00006888, 0, 0),\
	PatchScaledLipsSingleByteReading("EV26002.BIN", 0x00005e24, 0, 0),\
	PatchScaledLipsSingleByteReading("EV26020.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV26021.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV27001.BIN", 0x00005a88, 0, 0),\
	PatchScaledLipsSingleByteReading("EV27002.BIN", 0x00005b30, 0, 0),\
	PatchScaledLipsSingleByteReading("EV27054.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV28001.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV28025.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV28050.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV28051.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV29001.BIN", 0x00005aa0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV29002.BIN", 0x00005aa0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV29021.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV29050.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV30003.BIN", 0x00006ac4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV30020.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV30026.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV30027.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV31001.BIN", 0x00005d24, 0, 0),\
	PatchScaledLipsSingleByteReading("EV31002.BIN", 0x00005c7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV31023.BIN", 0x00005aa4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV31024.BIN", 0x00005aa4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV32002.BIN", 0x00005ab0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV32020.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV32055.BIN", 0x00005af0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV33001.BIN", 0x00005fd0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV33002.BIN", 0x00005d38, 0, 0),\
	PatchScaledLipsSingleByteReading("EV33022.BIN", 0x00005a84, 0, 0),\
	PatchScaledLipsSingleByteReading("EV33054.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34001.BIN", 0x00005aa0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34002.BIN", 0x00005ab8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34003.BIN", 0x00005aa0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34004.BIN", 0x00005af8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34005.BIN", 0x00005af8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34006.BIN", 0x00005b30, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34007.BIN", 0x00005aa0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34008.BIN", 0x00005b30, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34020.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34030.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34040.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34041.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34042.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34043.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV34044.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("M00PRG.BIN", 0x00079578, 0, 0),\
	PatchScaledLipsSingleByteReading("M26PRG.BIN", 0x00078c88, 0, 0),\
	PatchScaledLipsSingleByteReading("TUTORI0.BIN", 0x00005bf4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV07000.BIN", 0x00005750, -2, -4),\
	PatchScaledLipsSingleByteReading("EV07001.BIN", 0x00005f44, 0, 0),\
	PatchScaledLipsSingleByteReading("EV07002.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV07003.BIN", 0x00005a94, 0, 0),\
	PatchScaledLipsSingleByteReading("EV07005.BIN", 0x00005ab8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV07006.BIN", 0x00005ac8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV07007.BIN", 0x00005ab8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV07008.BIN", 0x00005ac8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV07009.BIN", 0x00005aa0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV07020.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV07021.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV08000.BIN", 0x00005750, -2, -4),\
	PatchScaledLipsSingleByteReading("EV08001.BIN", 0x000064b0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV08002.BIN", 0x00006850, 0, 0),\
	PatchScaledLipsSingleByteReading("EV08003.BIN", 0x00005b0c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV08020.BIN", 0x00005ad8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV08021.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV08030.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV08032.BIN", 0x00005c3c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV08033.BIN", 0x00005c28, 0, 0),\
	PatchScaledLipsSingleByteReading("EV08050.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV08060.BIN", 0x00006e68, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09001.BIN", 0x000060cc, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09002.BIN", 0x00005aa4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09005.BIN", 0x0000614c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09006.BIN", 0x00005b68, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09007.BIN", 0x00005c30, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09010.BIN", 0x00005ad4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09011.BIN", 0x00006090, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09025.BIN", 0x000060ac, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09026.BIN", 0x00006090, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09030.BIN", 0x00005aa4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09031.BIN", 0x00005aa4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09035.BIN", 0x00005ac8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09041.BIN", 0x00005a88, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09042.BIN", 0x00005a88, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09043.BIN", 0x00005a88, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09044.BIN", 0x00005a88, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09045.BIN", 0x00005a88, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09046.BIN", 0x00005a88, 0, 0),\
	PatchScaledLipsSingleByteReading("EV09048.BIN", 0x00005a88, 0, 0),\
	PatchScaledLipsSingleByteReading("EV10001.BIN", 0x00005bc8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV10002.BIN", 0x00005fc0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV10020.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV10025.BIN", 0x00005cd0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV10026.BIN", 0x00006204, 0, 0),\
	PatchScaledLipsSingleByteReading("EV10030.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV10035.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV11001.BIN", 0x00005e3c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV11002.BIN", 0x0000782c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV11030.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV11040.BIN", 0x00005bd0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV12001.BIN", 0x00005d24, 0, 0),\
	PatchScaledLipsSingleByteReading("EV12002.BIN", 0x00005b28, 0, 0),\
	PatchScaledLipsSingleByteReading("EV12035.BIN", 0x00005b48, 0, 0),\
	PatchScaledLipsSingleByteReading("EV13001.BIN", 0x00005d70, 0, 0),\
	PatchScaledLipsSingleByteReading("EV13002.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV13035.BIN", 0x00005b48, 0, 0),\
	PatchScaledLipsSingleByteReading("EV15001.BIN", 0x00005bb8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV15002.BIN", 0x000063b4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV16001.BIN", 0x00005a78, 0, 0),\
	PatchScaledLipsSingleByteReading("EV16002.BIN", 0x00005a78, 0, 0),\
	PatchScaledLipsSingleByteReading("EV16035.BIN", 0x00005ad0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV16051.BIN", 0x00006024, 0, 0),\
	PatchScaledLipsSingleByteReading("EV16052.BIN", 0x00005a78, 0, 0),\
	PatchScaledLipsSingleByteReading("EV16053.BIN", 0x00006000, 0, 0),\
	PatchScaledLipsSingleByteReading("EV17001.BIN", 0x00005c30, 0, 0),\
	PatchScaledLipsSingleByteReading("EV17002.BIN", 0x000079bc, 0, 0),\
	PatchScaledLipsSingleByteReading("EV17010.BIN", 0x00006054, 0, 0),\
	PatchScaledLipsSingleByteReading("EV17030.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV17031.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV17035.BIN", 0x00005b08, 0, 0),\
	PatchScaledLipsSingleByteReading("EV18001.BIN", 0x00006b24, 0, 0),\
	PatchScaledLipsSingleByteReading("EV18002.BIN", 0x00005fb0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV18020.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV18030.BIN", 0x00005b3c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV18040.BIN", 0x0000623c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19001.BIN", 0x00005b50, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19002.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19003.BIN", 0x00005b38, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19020.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19021.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19022.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19023.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19041.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19042.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19043.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19044.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19045.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19046.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19047.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV19048.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV20001.BIN", 0x00005be8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV20002.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV20003.BIN", 0x00005ba8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV20020.BIN", 0x00005aac, 0, 0),\
	PatchScaledLipsSingleByteReading("EV20021.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV20022.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV20023.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV20024.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV20041.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21001.BIN", 0x00005ad8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21002.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21010.BIN", 0x00005af8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21020.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21031.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21032.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21033.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21034.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21035.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21036.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21037.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21038.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21050.BIN", 0x00005b38, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21060.BIN", 0x00005ab0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21061.BIN", 0x00005ab0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV21062.BIN", 0x00005ab0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV22001.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV22002.BIN", 0x00006838, 0, 0),\
	PatchScaledLipsSingleByteReading("EV22020.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV22021.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV22022.BIN", 0x00006568, 0, 0),\
	PatchScaledLipsSingleByteReading("EV22023.BIN", 0x000065f8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV22024.BIN", 0x00006108, 0, 0),\
	PatchScaledLipsSingleByteReading("EV22025.BIN", 0x000060f4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV22026.BIN", 0x00005ef0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV22027.BIN", 0x00005adc, 0, 0),\
	PatchScaledLipsSingleByteReading("EV22030.BIN", 0x00005a94, 0, 0),\
	PatchScaledLipsSingleByteReading("EV22031.BIN", 0x00005a94, 0, 0),\
	PatchScaledLipsSingleByteReading("EV22040.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23001.BIN", 0x00005ae4, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23002.BIN", 0x00006714, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23003.BIN", 0x00005b10, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23010.BIN", 0x00005c90, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23020.BIN", 0x00005c90, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23021.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23022.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23023.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23041.BIN", 0x00005b1c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23042.BIN", 0x00005b1c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23043.BIN", 0x00005b40, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23044.BIN", 0x00005b1c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23045.BIN", 0x00005b1c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23046.BIN", 0x00005b40, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23047.BIN", 0x00005b1c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23061.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23063.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23064.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23065.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23067.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV23068.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV24001.BIN", 0x00005edc, 0, 0),\
	PatchScaledLipsSingleByteReading("EV24002.BIN", 0x00006238, 0, 0),\
	PatchScaledLipsSingleByteReading("EV24021.BIN", 0x00005bec, 0, 0),\
	PatchScaledLipsSingleByteReading("EV24040.BIN", 0x00005c20, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25001.BIN", 0x00005c70, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25002.BIN", 0x00006698, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25003.BIN", 0x00006564, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25004.BIN", 0x00006048, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25005.BIN", 0x00005fbc, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25006.BIN", 0x00005d1c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25007.BIN", 0x00005fd8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25008.BIN", 0x00005fcc, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25021.BIN", 0x00005a9c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25022.BIN", 0x00005a9c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25023.BIN", 0x00005a9c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25024.BIN", 0x00005a9c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25025.BIN", 0x00005a9c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25026.BIN", 0x00005a9c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25027.BIN", 0x00005a9c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25028.BIN", 0x00005a9c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25030.BIN", 0x00005bec, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25040.BIN", 0x00005bc8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV25050.BIN", 0x00005d2c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV35001.BIN", 0x00005f44, 0, 0),\
	PatchScaledLipsSingleByteReading("EV35003.BIN", 0x00005a94, 0, 0),\
	PatchScaledLipsSingleByteReading("EV35021.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV36001.BIN", 0x00005e58, 0, 0),\
	PatchScaledLipsSingleByteReading("EV36002.BIN", 0x00006770, 0, 0),\
	PatchScaledLipsSingleByteReading("EV36020.BIN", 0x00005a78, 0, 0),\
	PatchScaledLipsSingleByteReading("EV36026.BIN", 0x00005a78, 0, 0),\
	PatchScaledLipsSingleByteReading("EV36028.BIN", 0x00005a78, 0, 0),\
	PatchScaledLipsSingleByteReading("EV36030.BIN", 0x00006020, 0, 0),\
	PatchScaledLipsSingleByteReading("EV37001.BIN", 0x00006450, 0, 0),\
	PatchScaledLipsSingleByteReading("EV37002.BIN", 0x00005ad8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45001.BIN", 0x00005c10, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45002.BIN", 0x00005d14, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45003.BIN", 0x00005b38, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45010.BIN", 0x00005ae8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45021.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45022.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45023.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45033.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45041.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45042.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45043.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45044.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45045.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45046.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45047.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45048.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV45099.BIN", 0x000061d8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV46001.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV46002.BIN", 0x00005c78, 0, 0),\
	PatchScaledLipsSingleByteReading("EV46003.BIN", 0x00005b8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV46020.BIN", 0x00005be8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV46021.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV46030.BIN", 0x00005ae0, 0, 0),\
	PatchScaledLipsSingleByteReading("EV47001.BIN", 0x00005e08, 0, 0),\
	PatchScaledLipsSingleByteReading("EV47002.BIN", 0x000061dc, 0, 0),\
	PatchScaledLipsSingleByteReading("EV47020.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV47021.BIN", 0x00005a7c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV47031.BIN", 0x00005bc8, 0, 0),\
	PatchScaledLipsSingleByteReading("EV49001.BIN", 0x00005a8c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV50002.BIN", 0x00006534, 0, 0),\
	PatchScaledLipsSingleByteReading("EV51002.BIN", 0x00005d9c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV52002.BIN", 0x00005d9c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV53002.BIN", 0x00005d9c, 0, 0),\
	PatchScaledLipsSingleByteReading("EV53010.BIN", 0x00005af8, 0, 0),\
	PatchScaledLipsSingleByteReading("M18PRG.BIN", 0x00078f24, 0, 0),\
	PatchScaledLipsSingleByteReading("M22PRG.BIN", 0x00077e7c, 0, 0)

/*  From TrekkiesUnite118
	0x060D1975 change from 0x0f to 0x0b - Changes dimensions from 0xf to 0xb
	0x060D1989 change from 0x0f to 0x0b
*/
#define PatchSpecialLipsVerticalSize(filename, address)\
	filename, address - 1,  0x0b, 0x0f,\
	filename, address + 0x13, 0x0b, 0x0f

#define PatchSpecialLipsVerticalSizeInFiles()\
	PatchSpecialLipsVerticalSize("EV00001.BIN", 0x00007871),\
	PatchSpecialLipsVerticalSize("EV00002.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV00050.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV00051.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV00052.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV00054.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV00055.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV00060.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV01001.BIN", 0x00006281),\
	PatchSpecialLipsVerticalSize("EV01002.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV01020.BIN", 0x00006799),\
	PatchSpecialLipsVerticalSize("EV01021.BIN", 0x000065ad),\
	PatchSpecialLipsVerticalSize("EV01022.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV01023.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV01030.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV01050.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV01054.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV01055.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV02001.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV02002.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV02010.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV02021.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV02025.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV02050.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV02051.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV02052.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV02053.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV02054.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV03001.BIN", 0x00006341),\
	PatchSpecialLipsVerticalSize("EV03002.BIN", 0x000061e9),\
	PatchSpecialLipsVerticalSize("EV03005.BIN", 0x000061a1),\
	PatchSpecialLipsVerticalSize("EV03010.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV03020.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV03021.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV03023.BIN", 0x0000619d),\
	PatchSpecialLipsVerticalSize("EV03024.BIN", 0x0000619d),\
	PatchSpecialLipsVerticalSize("EV03025.BIN", 0x000061b1),\
	PatchSpecialLipsVerticalSize("EV03050.BIN", 0x000065b5),\
	PatchSpecialLipsVerticalSize("EV03051.BIN", 0x000061d9),\
	PatchSpecialLipsVerticalSize("EV03052.BIN", 0x00006245),\
	PatchSpecialLipsVerticalSize("EV03053.BIN", 0x00006201),\
	PatchSpecialLipsVerticalSize("EV04001.BIN", 0x0000702d),\
	PatchSpecialLipsVerticalSize("EV04002.BIN", 0x000061f1),\
	PatchSpecialLipsVerticalSize("EV04003.BIN", 0x00006325),\
	PatchSpecialLipsVerticalSize("EV04005.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV04010.BIN", 0x000061e9),\
	PatchSpecialLipsVerticalSize("EV04020.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV04022.BIN", 0x0000617d),\
	PatchSpecialLipsVerticalSize("EV04050.BIN", 0x0000621d),\
	PatchSpecialLipsVerticalSize("EV04053.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV04055.BIN", 0x000061e9),\
	PatchSpecialLipsVerticalSize("EV05001.BIN", 0x0000618d),\
	PatchSpecialLipsVerticalSize("EV05002.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05003.BIN", 0x00006351),\
	PatchSpecialLipsVerticalSize("EV05004.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05005.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05007.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05010.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05011.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05018.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05019.BIN", 0x0000619d),\
	PatchSpecialLipsVerticalSize("EV05020.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05021.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05022.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05023.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05025.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05026.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05027.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05051.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05052.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05053.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05054.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV05060.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV26001.BIN", 0x00006f81),\
	PatchSpecialLipsVerticalSize("EV26002.BIN", 0x0000651d),\
	PatchSpecialLipsVerticalSize("EV26020.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV26021.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV27001.BIN", 0x00006181),\
	PatchSpecialLipsVerticalSize("EV27002.BIN", 0x00006229),\
	PatchSpecialLipsVerticalSize("EV27054.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV28001.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV28025.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV28050.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV28051.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV29001.BIN", 0x00006199),\
	PatchSpecialLipsVerticalSize("EV29002.BIN", 0x00006199),\
	PatchSpecialLipsVerticalSize("EV29021.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV29050.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV30003.BIN", 0x000071bd),\
	PatchSpecialLipsVerticalSize("EV30020.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV30026.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV30027.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV31001.BIN", 0x0000641d),\
	PatchSpecialLipsVerticalSize("EV31002.BIN", 0x00006375),\
	PatchSpecialLipsVerticalSize("EV31023.BIN", 0x0000619d),\
	PatchSpecialLipsVerticalSize("EV31024.BIN", 0x0000619d),\
	PatchSpecialLipsVerticalSize("EV32002.BIN", 0x000061a9),\
	PatchSpecialLipsVerticalSize("EV32020.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV32055.BIN", 0x000061e9),\
	PatchSpecialLipsVerticalSize("EV33001.BIN", 0x000066c9),\
	PatchSpecialLipsVerticalSize("EV33002.BIN", 0x00006431),\
	PatchSpecialLipsVerticalSize("EV33022.BIN", 0x0000617d),\
	PatchSpecialLipsVerticalSize("EV33054.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV34001.BIN", 0x00006199),\
	PatchSpecialLipsVerticalSize("EV34002.BIN", 0x000061b1),\
	PatchSpecialLipsVerticalSize("EV34003.BIN", 0x00006199),\
	PatchSpecialLipsVerticalSize("EV34004.BIN", 0x000061f1),\
	PatchSpecialLipsVerticalSize("EV34005.BIN", 0x000061f1),\
	PatchSpecialLipsVerticalSize("EV34006.BIN", 0x00006229),\
	PatchSpecialLipsVerticalSize("EV34007.BIN", 0x00006199),\
	PatchSpecialLipsVerticalSize("EV34008.BIN", 0x00006229),\
	PatchSpecialLipsVerticalSize("EV34020.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV34030.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV34040.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV34041.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV34042.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV34043.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV34044.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("M00PRG.BIN", 0x00079c71),\
	PatchSpecialLipsVerticalSize("M26PRG.BIN", 0x00079381),\
	PatchSpecialLipsVerticalSize("TUTORI0.BIN", 0x000062ed),\
	PatchSpecialLipsVerticalSize("EV07001.BIN", 0x0000663d),\
	PatchSpecialLipsVerticalSize("EV07002.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV07003.BIN", 0x0000618d),\
	PatchSpecialLipsVerticalSize("EV07005.BIN", 0x000061b1),\
	PatchSpecialLipsVerticalSize("EV07006.BIN", 0x000061c1),\
	PatchSpecialLipsVerticalSize("EV07007.BIN", 0x000061b1),\
	PatchSpecialLipsVerticalSize("EV07008.BIN", 0x000061c1),\
	PatchSpecialLipsVerticalSize("EV07009.BIN", 0x00006199),\
	PatchSpecialLipsVerticalSize("EV07020.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV07021.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV08001.BIN", 0x00006ba9),\
	PatchSpecialLipsVerticalSize("EV08002.BIN", 0x00006f49),\
	PatchSpecialLipsVerticalSize("EV08003.BIN", 0x00006205),\
	PatchSpecialLipsVerticalSize("EV08020.BIN", 0x000061d1),\
	PatchSpecialLipsVerticalSize("EV08021.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV08030.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV08032.BIN", 0x00006335),\
	PatchSpecialLipsVerticalSize("EV08033.BIN", 0x00006321),\
	PatchSpecialLipsVerticalSize("EV08050.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV08060.BIN", 0x00007561),\
	PatchSpecialLipsVerticalSize("EV09001.BIN", 0x000067c5),\
	PatchSpecialLipsVerticalSize("EV09002.BIN", 0x0000619d),\
	PatchSpecialLipsVerticalSize("EV09005.BIN", 0x00006845),\
	PatchSpecialLipsVerticalSize("EV09006.BIN", 0x00006261),\
	PatchSpecialLipsVerticalSize("EV09007.BIN", 0x00006329),\
	PatchSpecialLipsVerticalSize("EV09010.BIN", 0x000061cd),\
	PatchSpecialLipsVerticalSize("EV09011.BIN", 0x00006789),\
	PatchSpecialLipsVerticalSize("EV09025.BIN", 0x000067a5),\
	PatchSpecialLipsVerticalSize("EV09026.BIN", 0x00006789),\
	PatchSpecialLipsVerticalSize("EV09030.BIN", 0x0000619d),\
	PatchSpecialLipsVerticalSize("EV09031.BIN", 0x0000619d),\
	PatchSpecialLipsVerticalSize("EV09035.BIN", 0x000061c1),\
	PatchSpecialLipsVerticalSize("EV09041.BIN", 0x00006181),\
	PatchSpecialLipsVerticalSize("EV09042.BIN", 0x00006181),\
	PatchSpecialLipsVerticalSize("EV09043.BIN", 0x00006181),\
	PatchSpecialLipsVerticalSize("EV09044.BIN", 0x00006181),\
	PatchSpecialLipsVerticalSize("EV09045.BIN", 0x00006181),\
	PatchSpecialLipsVerticalSize("EV09046.BIN", 0x00006181),\
	PatchSpecialLipsVerticalSize("EV09048.BIN", 0x00006181),\
	PatchSpecialLipsVerticalSize("EV10001.BIN", 0x000062c1),\
	PatchSpecialLipsVerticalSize("EV10002.BIN", 0x000066b9),\
	PatchSpecialLipsVerticalSize("EV10020.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV10025.BIN", 0x000063c9),\
	PatchSpecialLipsVerticalSize("EV10026.BIN", 0x000068fd),\
	PatchSpecialLipsVerticalSize("EV10030.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV10035.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV11001.BIN", 0x00006535),\
	PatchSpecialLipsVerticalSize("EV11002.BIN", 0x00007f25),\
	PatchSpecialLipsVerticalSize("EV11030.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV11040.BIN", 0x000062c9),\
	PatchSpecialLipsVerticalSize("EV12001.BIN", 0x0000641d),\
	PatchSpecialLipsVerticalSize("EV12002.BIN", 0x00006221),\
	PatchSpecialLipsVerticalSize("EV12035.BIN", 0x00006241),\
	PatchSpecialLipsVerticalSize("EV13001.BIN", 0x00006469),\
	PatchSpecialLipsVerticalSize("EV13002.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV13035.BIN", 0x00006241),\
	PatchSpecialLipsVerticalSize("EV15001.BIN", 0x000062b1),\
	PatchSpecialLipsVerticalSize("EV15002.BIN", 0x00006aad),\
	PatchSpecialLipsVerticalSize("EV16001.BIN", 0x00006171),\
	PatchSpecialLipsVerticalSize("EV16002.BIN", 0x00006171),\
	PatchSpecialLipsVerticalSize("EV16035.BIN", 0x000061c9),\
	PatchSpecialLipsVerticalSize("EV16051.BIN", 0x0000671d),\
	PatchSpecialLipsVerticalSize("EV16052.BIN", 0x00006171),\
	PatchSpecialLipsVerticalSize("EV16053.BIN", 0x000066f9),\
	PatchSpecialLipsVerticalSize("EV17001.BIN", 0x00006329),\
	PatchSpecialLipsVerticalSize("EV17002.BIN", 0x000080b5),\
	PatchSpecialLipsVerticalSize("EV17010.BIN", 0x0000674d),\
	PatchSpecialLipsVerticalSize("EV17030.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV17031.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV17035.BIN", 0x00006201),\
	PatchSpecialLipsVerticalSize("EV18001.BIN", 0x0000721d),\
	PatchSpecialLipsVerticalSize("EV18002.BIN", 0x000066a9),\
	PatchSpecialLipsVerticalSize("EV18020.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV18030.BIN", 0x00006235),\
	PatchSpecialLipsVerticalSize("EV18040.BIN", 0x00006935),\
	PatchSpecialLipsVerticalSize("EV19001.BIN", 0x00006249),\
	PatchSpecialLipsVerticalSize("EV19002.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV19003.BIN", 0x00006231),\
	PatchSpecialLipsVerticalSize("EV19020.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV19021.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV19022.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV19023.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV19041.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV19042.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV19043.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV19044.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV19045.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV19046.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV19047.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV19048.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV20001.BIN", 0x000062e1),\
	PatchSpecialLipsVerticalSize("EV20002.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV20003.BIN", 0x000062a1),\
	PatchSpecialLipsVerticalSize("EV20020.BIN", 0x000061a5),\
	PatchSpecialLipsVerticalSize("EV20021.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV20022.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV20023.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV20024.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV20041.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV21001.BIN", 0x000061d1),\
	PatchSpecialLipsVerticalSize("EV21002.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV21010.BIN", 0x000061f1),\
	PatchSpecialLipsVerticalSize("EV21020.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV21031.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV21032.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV21033.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV21034.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV21035.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV21036.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV21037.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV21038.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV21050.BIN", 0x00006231),\
	PatchSpecialLipsVerticalSize("EV21060.BIN", 0x000061a9),\
	PatchSpecialLipsVerticalSize("EV21061.BIN", 0x000061a9),\
	PatchSpecialLipsVerticalSize("EV21062.BIN", 0x000061a9),\
	PatchSpecialLipsVerticalSize("EV22001.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV22002.BIN", 0x00006f31),\
	PatchSpecialLipsVerticalSize("EV22020.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV22021.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV22022.BIN", 0x00006c61),\
	PatchSpecialLipsVerticalSize("EV22023.BIN", 0x00006cf1),\
	PatchSpecialLipsVerticalSize("EV22024.BIN", 0x00006801),\
	PatchSpecialLipsVerticalSize("EV22025.BIN", 0x000067ed),\
	PatchSpecialLipsVerticalSize("EV22026.BIN", 0x000065e9),\
	PatchSpecialLipsVerticalSize("EV22027.BIN", 0x000061d5),\
	PatchSpecialLipsVerticalSize("EV22030.BIN", 0x0000618d),\
	PatchSpecialLipsVerticalSize("EV22031.BIN", 0x0000618d),\
	PatchSpecialLipsVerticalSize("EV22040.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV23001.BIN", 0x000061dd),\
	PatchSpecialLipsVerticalSize("EV23002.BIN", 0x00006e0d),\
	PatchSpecialLipsVerticalSize("EV23003.BIN", 0x00006209),\
	PatchSpecialLipsVerticalSize("EV23010.BIN", 0x00006389),\
	PatchSpecialLipsVerticalSize("EV23020.BIN", 0x00006389),\
	PatchSpecialLipsVerticalSize("EV23021.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV23022.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV23023.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV23041.BIN", 0x00006215),\
	PatchSpecialLipsVerticalSize("EV23042.BIN", 0x00006215),\
	PatchSpecialLipsVerticalSize("EV23043.BIN", 0x00006239),\
	PatchSpecialLipsVerticalSize("EV23044.BIN", 0x00006215),\
	PatchSpecialLipsVerticalSize("EV23045.BIN", 0x00006215),\
	PatchSpecialLipsVerticalSize("EV23046.BIN", 0x00006239),\
	PatchSpecialLipsVerticalSize("EV23047.BIN", 0x00006215),\
	PatchSpecialLipsVerticalSize("EV23061.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV23063.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV23064.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV23065.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV23067.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV23068.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV24001.BIN", 0x000065d5),\
	PatchSpecialLipsVerticalSize("EV24002.BIN", 0x00006931),\
	PatchSpecialLipsVerticalSize("EV24021.BIN", 0x000062e5),\
	PatchSpecialLipsVerticalSize("EV24040.BIN", 0x00006319),\
	PatchSpecialLipsVerticalSize("EV25001.BIN", 0x00006369),\
	PatchSpecialLipsVerticalSize("EV25002.BIN", 0x00006d91),\
	PatchSpecialLipsVerticalSize("EV25003.BIN", 0x00006c5d),\
	PatchSpecialLipsVerticalSize("EV25004.BIN", 0x00006741),\
	PatchSpecialLipsVerticalSize("EV25005.BIN", 0x000066b5),\
	PatchSpecialLipsVerticalSize("EV25006.BIN", 0x00006415),\
	PatchSpecialLipsVerticalSize("EV25007.BIN", 0x000066d1),\
	PatchSpecialLipsVerticalSize("EV25008.BIN", 0x000066c5),\
	PatchSpecialLipsVerticalSize("EV25021.BIN", 0x00006195),\
	PatchSpecialLipsVerticalSize("EV25022.BIN", 0x00006195),\
	PatchSpecialLipsVerticalSize("EV25023.BIN", 0x00006195),\
	PatchSpecialLipsVerticalSize("EV25024.BIN", 0x00006195),\
	PatchSpecialLipsVerticalSize("EV25025.BIN", 0x00006195),\
	PatchSpecialLipsVerticalSize("EV25026.BIN", 0x00006195),\
	PatchSpecialLipsVerticalSize("EV25027.BIN", 0x00006195),\
	PatchSpecialLipsVerticalSize("EV25028.BIN", 0x00006195),\
	PatchSpecialLipsVerticalSize("EV25030.BIN", 0x000062e5),\
	PatchSpecialLipsVerticalSize("EV25040.BIN", 0x000062c1),\
	PatchSpecialLipsVerticalSize("EV25050.BIN", 0x00006425),\
	PatchSpecialLipsVerticalSize("EV35001.BIN", 0x0000663d),\
	PatchSpecialLipsVerticalSize("EV35003.BIN", 0x0000618d),\
	PatchSpecialLipsVerticalSize("EV35021.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV36001.BIN", 0x00006551),\
	PatchSpecialLipsVerticalSize("EV36002.BIN", 0x00006e69),\
	PatchSpecialLipsVerticalSize("EV36020.BIN", 0x00006171),\
	PatchSpecialLipsVerticalSize("EV36026.BIN", 0x00006171),\
	PatchSpecialLipsVerticalSize("EV36028.BIN", 0x00006171),\
	PatchSpecialLipsVerticalSize("EV36030.BIN", 0x00006719),\
	PatchSpecialLipsVerticalSize("EV37001.BIN", 0x00006b49),\
	PatchSpecialLipsVerticalSize("EV37002.BIN", 0x000061d1),\
	PatchSpecialLipsVerticalSize("EV45001.BIN", 0x00006309),\
	PatchSpecialLipsVerticalSize("EV45002.BIN", 0x0000640d),\
	PatchSpecialLipsVerticalSize("EV45003.BIN", 0x00006231),\
	PatchSpecialLipsVerticalSize("EV45010.BIN", 0x000061e1),\
	PatchSpecialLipsVerticalSize("EV45021.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV45022.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV45023.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV45033.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV45041.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV45042.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV45043.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV45044.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV45045.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV45046.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV45047.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV45048.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV45099.BIN", 0x000068d1),\
	PatchSpecialLipsVerticalSize("EV46001.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV46002.BIN", 0x00006371),\
	PatchSpecialLipsVerticalSize("EV46003.BIN", 0x00006285),\
	PatchSpecialLipsVerticalSize("EV46020.BIN", 0x000062e1),\
	PatchSpecialLipsVerticalSize("EV46021.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV46030.BIN", 0x000061d9),\
	PatchSpecialLipsVerticalSize("EV47001.BIN", 0x00006501),\
	PatchSpecialLipsVerticalSize("EV47002.BIN", 0x000068d5),\
	PatchSpecialLipsVerticalSize("EV47020.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV47021.BIN", 0x00006175),\
	PatchSpecialLipsVerticalSize("EV47031.BIN", 0x000062c1),\
	PatchSpecialLipsVerticalSize("EV49001.BIN", 0x00006185),\
	PatchSpecialLipsVerticalSize("EV50002.BIN", 0x00006c2d),\
	PatchSpecialLipsVerticalSize("EV51002.BIN", 0x00006495),\
	PatchSpecialLipsVerticalSize("EV52002.BIN", 0x00006495),\
	PatchSpecialLipsVerticalSize("EV53002.BIN", 0x00006495),\
	PatchSpecialLipsVerticalSize("EV53010.BIN", 0x000061f1),\
	PatchSpecialLipsVerticalSize("M18PRG.BIN", 0x0007961d),\
	PatchSpecialLipsVerticalSize("M22PRG.BIN", 0x00078575)


bool PatchBattleTextDrawingCode(const string& inOriginalDirectory, const string& inPatchedDirectory, const string& inDataDirectory)
{
	printf("Patching Battle Status Screen (Slow, please wait)\n");

	const string lipPatchPath = inDataDirectory + Seperators + "BattleLipsPatching.bin";
	FileData lipsPatchData;
	if(!lipsPatchData.InitializeFileData(lipPatchPath))
	{
		return false;
	}

	PatchingEntry patchingInfo[] = 
	{
		//0x06089264 checking for 0xe, change to 0x1b
		PatchBattleCode1(),

		//0x060893b0 multiplying by 16 (height spacing) We need to multiply by 12
		PatchHeightSpacingInFiles(),

		//0x060893e0 will multiply by 16 (width spacing) (make this 4100 shll1)
		PatchWidthSpacingInFiles(),

		//0x060893fc is characters per line (make this E11B)
		PatchBattleCode2(),

		//0x06089408 is num lines (make this e103)
		PatchBattleCode3(),

		//0x06089450 multiplies by 0xe to get to next line write buffer, change to 0x1c
		PatchBattleCode4(),

		//060894b0 is 0xa7f.  This is the amount of bytes to clear in vdp1 when clearing the text box.
		//Change this to 27*4 * (48+2)(bytes) cause 48 is 8x12/2.  Divide by two because the vdp1 buffer is cleared two bytes at a time.
		PatchBattleCode5(),

		//060892a2 change to 0x1b
		PatchBattleCode6(),

		//0602063e will write up to 0xf characters per line(mov 0x0f, r3)
		"0.SLG", 0x0001b63e, 0x1c, 0x0f,

		//0608903c and 06089282 change to 0x03 (number of lines)
		PatchBattleCode7(),

		// 0608914c
		PatchBattleCode8(),

		/*Making space for a bigger text buffer
		06034760 (3 instances need to be changed to 6047000 in 0.slg) 
		- at 0600739c, 06007400, 060074e0[@0x0000239c, 0x2400, 0x24e0]
		0600735a need to change 0x3f to 6f*/
		"0.SLG", 0x0000239c, 0x06047000, 0x06034760,
		"0.SLG", 0x00002400, 0x06047000, 0x06034760,
		"0.SLG", 0x000024e0, 0x06047000, 0x06034760,
		"0.SLG", 0x0000235a, 0x6f, 0x3f,

		/*Lips spacing
		060d146c is calculation to center text.Set this to 4200 (shll r2)
		060d14ef is vertical offset.Change to 0x93
		060d1503 is X spacing.Set this to 0x08
		060d16bf is another horizontal offset.Set this to f8
		*/
		PatchLipsSpacing(),

	#if FIX_SLG_FONT_DRAWING_SIZE
		PatchCharacterDimensionsForFiles(),
		PatchCharacterDimensionsForLipsInFiles(),
		PatchScaledLipsFormattingInFiles(),
		PatchScaledLipsSpriteBufferInFiles(),
		PatchScaledLipsTileStrideInFiles(),
		PatchSpecialLipsVerticalSizeInFiles(),
	#endif

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
		PatchSingleByteLipsHiCharacterHandlingInFiles(),
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
		if (!sakuraBin.OpenFile(sakuraBinFilePath, false))
		{
			continue;
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