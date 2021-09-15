byte DisplayText(int param_1)

{
	undefined2 uVar1;
	bool bVar2;
	bool bVar3;
	short sVar4;
	undefined* puVar5;
	undefined* puVar6;
	short sVar7;
	short sVar8;
	short sVar9;
	char cVar10;
	byte bVar11;
	byte bVar12;
	undefined uVar14;
	undefined4 uVar13;
	uint uVar15;
	char* extraout_r3;
	char* extraout_r3_00;
	int iVar16;
	char* pcVar17;
	undefined* puVar18;
	byte* pbVar19;
	uint uVar20;
	bool bVar21;
	int iVar22;
	byte bVar23;
	char* pcStack100;
	char* pcStack96;
	byte bStack92;
	char cStack88;
	uint uStack84;
	char* pcStack76;
	byte bStack64;
	char cStack60;
	char cStack56;
	char cStack40;
	char cStack39;
	char cStack38;
	undefined uStack37;

	puVar5 = PTR_DAT_002b3dd8;
	bVar23 = 0;
	bVar2 = true;
	do
	{
		uVar14 = (undefined)DAT_002b3dd4;
		puVar5[bVar23 + 4] = uVar14;
		uVar15 = (uint)bVar23;
		bVar23 = bVar23 + 1;
		puVar5[uVar15 * 2 + 0xc] = uVar14;
		puVar6 = PTR_PTR_DAT_002b3ddc;
	} while (bVar23 < 8);
	iVar22 = (int)DAT_002b3dd6;
	bStack64 = 0;
	bVar23 = 0;
	bVar3 = false;
	cStack60 = '\0';
	cStack56 = '\0';
	cStack88 = '\0';
	uVar15 = 0;
	do
	{
		puVar18 = PTR_DAT_002b3de4;
		if (!bVar2)
		{
		LAB_002b45ce:
			*(uint*)(puVar5 + 0x34) = *(int*)(puVar5 + 0x34) + (uVar15 & 0xff);
			puVar5[2] = 0;
			return bVar23;
		}
		if ((0x34 < bVar23) || (2 < bStack64))
		{
			puVar5[(short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b3dd6) + 0x3c] =
				puVar5[(short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b3dd6) + 0x3c] | 4;
			goto LAB_002b45ce;
		}
		*(ushort*)PTR_DAT_002b3de0 = (short)(char)puVar5[0x1d] & 0xff;
		*(undefined2*)puVar18 = 0;
		sVar9 = (*(code*)PTR_FUN_002b3de8)((int)*(char*)(param_1 + (uVar15 & 0xff)));
		sVar8 = DAT_002b45f4;
		sVar7 = DAT_002b42fe;
		puVar18 = PTR_DAT_002b4014;
		sVar4 = DAT_002b3dd6;
		if (sVar9 == 1)
		{
			if (!bVar3)
			{
				puVar5[(short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b3dd6) + 0x3d] = 0;
				*(undefined4*)(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar4) + 0x3c + 0x28) =
					*(undefined4*)
					(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar4) + 0x3c + 0x24);
				*(undefined2*)(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar4) + 0x3c + 0x1e) =
					*(undefined2*)
					(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar4) + 0x3c + 0x1a);
				puVar18 = PTR_DAT_002b3dec;
				*(undefined2*)(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar4) + 0x3c + 0x20) =
					*(undefined2*)
					(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar4) + 0x3c + 0x1c);
				*(undefined4*)(puVar5 + 0x38) = 0;
				*(undefined4*)puVar18 = 0;
				bVar3 = true;
			}
			DisplayJapaneseText((uVar15 & 0xff) + param_1, bVar23);
			uVar15 = uVar15 + 1;
			bVar23 = bVar23 + 1;
		LAB_002b3e6e:
			cStack88 = cStack88 + '\x01';
		}
		else
		{
			bVar12 = *(byte*)(param_1 + (uVar15 & 0xff));
			if (bVar12 == 0xd)
			{
				if (cStack88 != '\0')
				{
					bStack64 = bStack64 + 1;
					*(undefined2*)
						(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b3f06) + 0x3c + 0x1e) =
						*(undefined2*)
						(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b3f06) + 0x3c + 0x1a);
					*(short*)(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b3f06) + 0x3c + 0x20
							  ) = *(short*)(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b3f06)
													  + 0x3c + 0x1c) + 0x10;
					cStack88 = '\0';
				}
				goto LAB_002b45c0;
			}
			if (bVar12 == 0x20)
			{
				*(short*)(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b3f06) + 0x3c + 0x1e)
					= *(short*)(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b3f06) + 0x3c +
								0x1e) + 8;
				goto LAB_002b3e6e;
			}
			if (bVar12 < 0x10)
			{
				pbVar19 = puVar5 + iVar22 + 0x3c;
				if ((bVar12 == 1) || (bVar12 == 2))
				{
					if (bVar23 == 0)
					{
						uVar1 = *(undefined2*)(puVar5 + 0x24);
						if (bVar12 == 1)
						{
							*pbVar19 = *pbVar19 | 0x10;
							if (*puVar18 == '\0')
							{
								FUN_002b325c((int)(char)*puVar5, 1, (int)DAT_002b400c, 0x10, 0xc, 0x10);
								if ((*PTR_DAT_002b4018 == '\0') && (*PTR_DAT_002b401c != '\0'))
								{
									puVar5[0x3c] = 0;
								}
							}
							else
							{
								FUN_002b325c((int)(char)*puVar5, 1, 0xc, 0x10, 0xc, 0x10);
							}
							puVar18 = PTR_DAT_002b4014;
							*(undefined2*)(puVar5 + 0x24) = uVar1;
							*puVar18 = *puVar18 + '\x01';
							puVar5[0x3c] = puVar5[0x3c] & (byte)DAT_002b400e;
							puVar5[iVar22 + 0x3c] = puVar5[iVar22 + 0x3c] | 2;
							puVar5[iVar22 + 0x3c] = puVar5[iVar22 + 0x3c] | 1;
							puVar5[1] = 1;
							puVar18 = PTR_DAT_002b4024;
							uVar13 = DAT_002b4028;
							if (((int)*(char*)(param_1 + (uVar15 + 1 & 0xff)) & 0xffU) !=
								(int)PTR_DAT_002b4010._0_2_)
							{
								pcVar17 = (char*)((uVar15 + 1 & 0xff) + param_1);
								*(ushort*)PTR_DAT_002b4020 = (short)*pcVar17 & 0xff;
								*puVar18 = *pcVar17;
								uVar13 = DAT_002b4028;
							}
						}
						else
						{
							puVar5[0x3c] = puVar5[0x3c] | 0x20;
							if (*PTR_DAT_002b4070 == '\0')
							{
								FUN_002b325c((int)(char)*puVar5, 0, (int)DAT_002b406e, (int)DAT_002b406c, 0x3c,
											 (int)DAT_002b406c);
								if ((*PTR_DAT_002b4074 == '\0') && (*PTR_DAT_002b4078 != '\0'))
								{
									puVar5[iVar22 + 0x3c] = 0;
								}
							}
							else
							{
								FUN_002b325c((int)(char)*puVar5, 0, 0x3c, (int)DAT_002b41be, 0x3c, (int)DAT_002b41be);
							}
							puVar18 = PTR_DAT_002b41c8;
							*(undefined2*)(puVar5 + 0x24) = uVar1;
							*puVar18 = *puVar18 + '\x01';
							puVar5[iVar22 + 0x3c] = puVar5[iVar22 + 0x3c] & 0xfd;
							puVar5[0x3c] = puVar5[0x3c] | 2;
							puVar5[0x3c] = puVar5[0x3c] | 1;
							puVar5[1] = 0;
							uVar13 = DAT_002b41d4;
							if (((int)*(char*)(param_1 + (uVar15 + 1 & 0xff)) & 0xffU) != (int)DAT_002b41c0)
							{
								pcVar17 = (char*)((uVar15 + 1 & 0xff) + param_1);
								*(ushort*)PTR_PTR_DAT_002b41cc = (short)*pcVar17 & 0xff;
								*PTR_DAT_002b41d0 = *pcVar17;
								uVar13 = DAT_002b41d4;
							}
						}
						sVar4 = DAT_002b41c2;
						uVar15 = uVar15 + 1;
						puVar5[(short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b41c2) + 0x3d] = 0;
						*(undefined2*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar4) + 0x3c + 0x1e) =
							*(undefined2*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar4) + 0x3c + 0x1a);
						puVar18 = PTR_DAT_002b41d8;
						*(undefined2*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar4) + 0x3c + 0x20) =
							*(undefined2*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar4) + 0x3c + 0x1c);
						cStack56 = '\0';
						cStack60 = '\0';
						*(undefined4*)(puVar5 + 0x38) = 0;
						*(undefined4*)puVar18 = 0;
						*(undefined4*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar4) + 0x3c + 0x28) =
							*(undefined4*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar4) + 0x3c + 0x24);
						if (((int)*(char*)(param_1 + (uVar15 & 0xff)) & 0xffU) != (int)DAT_002b41c0)
						{
							cVar10 = *(char*)(param_1 + (uVar15 & 0xff));
							uStack84 = (int)cVar10 & 0xff;
							if ((cVar10 == '\a') && (iVar16 = (*(code*)PTR_FUN_002b41dc)(7), iVar16 != 0))
							{
								uStack84 = SEXT24(DAT_002b41c4);
							}
							(*(code*)PTR_FUN_002b41e4)
								(uVar13, PTR_DAT_002b41e0 + uStack84 * 0x900, (int)DAT_002b41c6);
						}
						bVar3 = true;
					}
					else
					{
						uVar15 = uVar15 - 2;
						bVar2 = false;
						puVar5[(short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b41c2) + 0x3c] =
							puVar5[(short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b41c2) + 0x3c] | 4;
					}
				}
				else
				{
					if (bVar12 == 3)
					{
						*pbVar19 = *pbVar19 & 0xfd;
						(puVar5 + iVar22 + 0x3c)[4] = (puVar5 + iVar22 + 0x3c)[4] & 0xf1 | 4;
						*(undefined2*)(puVar5 + iVar22 + 0x3c + 0x16) =
							*(undefined2*)(puVar5 + iVar22 + 0x3c + 0xe);
						puVar18 = puVar5 + iVar22 + 0x3c;
					}
					else
					{
						if (bVar12 != 4)
						{
							if (bVar12 == 5)
							{
								bVar2 = false;
								puVar5[(short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b3f06) + 0x3c] =
									puVar5[(short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b3f06) + 0x3c] | 4;
								uVar15 = uVar15 + 1;
							}
							else
							{
								if (bVar12 == 6)
								{
									bVar2 = false;
									puVar5[(short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b3f06) + 0x3c] =
										puVar5[(short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b3f06) + 0x3c] &
										0xfb;
								}
								else
								{
									if (bVar12 == 7)
									{
										uVar15 = uVar15 + 1;
										iVar16 = ((int)cStack60 & 0xffU) * 2;
										puVar5[iVar16 + 0xc] = bVar23;
										puVar5[iVar16 + 0xd] = *(undefined*)(param_1 + (uVar15 & 0xff));
										cStack60 = cStack60 + '\x01';
									}
									else
									{
										if (bVar12 == 8)
										{
											puVar5[((int)cStack56 & 0xffU) + 4] = bVar23;
											cStack56 = cStack56 + '\x01';
										}
										else
										{
											if (bVar12 == 9)
											{
												uVar15 = uVar15 + 1;
												puVar5[0x1d] = *(undefined*)(param_1 + (uVar15 & 0xff));
											}
										}
									}
								}
							}
							goto LAB_002b45c0;
						}
						puVar5[0x3c] = puVar5[0x3c] & (byte)DAT_002b42fc;
						puVar5[0x40] = puVar5[0x40] & 0xf1 | 4;
						*(undefined2*)(puVar5 + 0x52) = *(undefined2*)(puVar5 + 0x4a);
						puVar18 = puVar5 + 0x3c;
					}
					uVar15 = uVar15 + 1;
					*(undefined2*)(puVar18 + 0x18) = *(undefined2*)(puVar18 + 0x10);
				}
			}
			else
			{
				if (bVar12 == 0x10)
				{
					uVar15 = uVar15 + 1;
					if (!bVar3)
					{
						puVar5[(short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b42fe) + 0x3d] = 0;
						*(undefined4*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar7) + 0x3c + 0x28) =
							*(undefined4*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar7) + 0x3c + 0x24);
						*(undefined2*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar7) + 0x3c + 0x1e) =
							*(undefined2*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar7) + 0x3c + 0x1a);
						*(undefined2*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar7) + 0x3c + 0x20) =
							*(undefined2*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar7) + 0x3c + 0x1c);
						*(undefined4*)(puVar5 + 0x38) = 0;
						*(undefined4*)PTR_DAT_002b4300 = 0;
						bVar3 = true;
					}
					bVar21 = false;
					cVar10 = *(char*)(param_1 + (uVar15 & 0xff));
					uVar20 = SEXT14((char)*PTR_DAT_002b4304);
					if (cVar10 == '\0')
					{
						uVar20 = uVar20 & 0xff;
					LAB_002b433e:
						pcStack76 = *(char**)(PTR_PTR_DAT_002b4308 + uVar20 * 4);
					}
					else
					{
						if (cVar10 == '\x01')
						{
						LAB_002b430c:
							if (*(char*)(param_1 + (uVar15 & 0xff)) != '\x01')
							{
								uVar20 = SEXT14((char)*PTR_DAT_002b4488);
							}
							pcStack76 = *(char**)(PTR_PTR_s_Highland_002b4490 +
												  ((int)*(short*)((uVar20 & 0xff) * 2 + DAT_002b448c) & 0xffffU)
												  * 4);
							goto Label_DisplayEnglish;
						}
						if (cVar10 == '\x02')
						{
							uVar20 = (int)*(short*)(DAT_002b448c + (uVar20 & 0xff) * 2) & 0xffff;
							goto LAB_002b433e;
						}
						if (cVar10 == '\x03')
						{
						LAB_002b4344:
							if (*(char*)(param_1 + (uVar15 & 0xff)) != '\x03')
							{
								uVar20 = SEXT14((char)*PTR_DAT_002b4488);
							}
							pcStack76 = *(char**)(PTR_PTR_DAT_002b4308 +
												  ((int)*(short*)(PTR_PTR_DAT_002b4498 +
													  ((int)*(short*)(PTR_DAT_002b4494 +
														  (uVar20 & 0xff) * 2) & 0xffU) *
													  2) & 0xffffU) * 4);
						}
						else
						{
							if (cVar10 == '\x04') goto LAB_002b430c;
							if (cVar10 == '\x05') goto LAB_002b4344;
							pcVar17 = PTR_DAT_002b449c;
							if (cVar10 == '\x06')
							{
							LAB_002b4412:
								pcStack76 = *(char**)(PTR_PTR_DAT_002b4308 + ((int)*pcVar17 & 0xffU) * 4);
							}
							else
							{
								if (cVar10 == '\a')
								{
									if ((*(byte*)(DAT_002b44a4 + ((int)(char)*PTR_DAT_002b44a0 & 0xffU) * 2) & 0x1f)
										< 0xb)
									{
										cVar10 = ' ';
										pcVar17 = &cStack40;
									}
									else
									{
										cVar10 = (*(code*)PTR_FUN_002b44a8)();
										cVar10 = cVar10 + '0';
										pcVar17 = extraout_r3;
									}
									*pcVar17 = cVar10;
									cStack39 = (*(code*)PTR_FUN_002b44ac)();
									cStack39 = cStack39 + '0';
									cStack38 = '\0';
								}
								else
								{
									if (cVar10 != '\b')
									{
										pcVar17 = PTR_DAT_002b44a0;
										if (cVar10 == '\t') goto LAB_002b4412;
										if (cVar10 == '\x0f')
										{
											pcStack76 = *(char**)(PTR_PTR_DAT_002b44b8 +
																  ((int)(char)*PTR_DAT_002b44b4 & 0xffU) * 4);
										}
										else
										{
											pcStack76 = *(char**)puVar6;
										}
										goto Label_DisplayEnglish;
									}
									pcVar17 = &cStack40;
									if ((byte)*PTR_DAT_002b44b0 < 0x65)
									{
										cVar10 = ' ';
									}
									else
									{
										cVar10 = (*(code*)PTR_FUN_002b44a8)();
										cVar10 = cVar10 + '0';
									}
									*pcVar17 = cVar10;
									bVar12 = (*(code*)PTR_FUN_002b44ac)();
									if (bVar12 < 0xb)
									{
										cVar10 = ' ';
										pcVar17 = &cStack39;
									}
									else
									{
										cVar10 = (*(code*)PTR_FUN_002b44a8)();
										cVar10 = cVar10 + '0';
										pcVar17 = extraout_r3_00;
									}
									*pcVar17 = cVar10;
									cStack38 = (*(code*)PTR_FUN_002b44ac)();
									cStack38 = cStack38 + '0';
									uStack37 = 0;
								}
								bVar21 = true;
							}
						}
					}
				Label_DisplayEnglish:
					if (bVar21)
					{
						bVar11 = (*(code*)PTR_FUN_002b44bc)(&cStack40);
						bVar12 = 0;
						pcStack96 = &cStack40;
						if (bVar11 != 0)
						{
							do
							{
								DisplayEnglishText2((int)*pcStack96, bVar23);
								cStack88 = cStack88 + '\x01';
								bVar12 = bVar12 + 1;
								pcStack96 = pcStack96 + 1;
								bVar23 = bVar23 + 1;
							} while (bVar12 < bVar11);
						}
					}
					else
					{
						bVar12 = (*(code*)PTR_FUN_002b45f8)(pcStack76);
						if ((int)DAT_002b45f2 < (int)((int)*pcStack76 & 0xffU))
						{
							bStack92 = 0;
							pcStack100 = pcStack76;
							if (bVar12 != 0)
							{
								do
								{
									DisplayJapaneseText(pcStack100, bVar23);
									cStack88 = cStack88 + '\x01';
									bStack92 = bStack92 + 2;
									pcStack100 = pcStack100 + 2;
									bVar23 = bVar23 + 1;
								} while (bStack92 < bVar12);
							}
						}
						else
						{
							bStack92 = 0;
							pcStack100 = pcStack76;
							if (bVar12 != 0)
							{
								do
								{
									DisplayEnglishText2((int)*pcStack100, bVar23);
									cStack88 = cStack88 + '\x01';
									bStack92 = bStack92 + 1;
									pcStack100 = pcStack100 + 1;
									bVar23 = bVar23 + 1;
								} while (bStack92 < bVar12);
							}
						}
					}
				}
				else
				{
					if (!bVar3)
					{
						puVar5[(short)(((short)(char)puVar5[1] & 0xffU) * DAT_002b45f4) + 0x3d] = 0;
						*(undefined4*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar8) + 0x3c + 0x28) =
							*(undefined4*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar8) + 0x3c + 0x24);
						*(undefined2*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar8) + 0x3c + 0x1e) =
							*(undefined2*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar8) + 0x3c + 0x1a);
						*(undefined2*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar8) + 0x3c + 0x20) =
							*(undefined2*)
							(puVar5 + (short)(((short)(char)puVar5[1] & 0xffU) * sVar8) + 0x3c + 0x1c);
						*(undefined4*)(puVar5 + 0x38) = 0;
						*(undefined4*)PTR_DAT_002b45fc = 0;
						bVar3 = true;
					}
					DisplayEnglishText2((int)*(char*)(param_1 + (uVar15 & 0xff)), bVar23);
					bVar23 = bVar23 + 1;
					cStack88 = cStack88 + '\x01';
				}
			}
		}
	LAB_002b45c0:
		uVar15 = uVar15 + 1;
	} while (true);
}

