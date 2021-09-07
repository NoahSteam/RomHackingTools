
/* WARNING: Removing unreachable block (ram,0x0003c764) */
/* WARNING: Removing unreachable block (ram,0x0003c780) */
/* WARNING: Removing unreachable block (ram,0x0003c7b0) */
/* FUN_0003bd40 */

uint DisplayText(int param_1)

{
  byte bVar1;
  int iVar2;
  uint uVar3;
  uint uVar4;
  long lVar5;
  byte bVar6;
  char cVar7;
  undefined *puVar8;
  byte bVar9;
  uint uVar10;
  bool bVar11;
  
  uVar4 = 0;
  do {
    puVar8 = (undefined *)(uVar4 + 0x3ed444);
    iVar2 = uVar4 * 2;
    uVar4 = uVar4 + 1 & 0xff;
    *puVar8 = 0xff;
    *(undefined *)(iVar2 + 0x3ed44c) = 0xff;
  } while (uVar4 < 8);
  bVar11 = false;
  uVar10 = 0;
  cVar7 = '\0';
  uVar4 = 0;
  bVar6 = 0;
  do {
    if ((0x34 < uVar10) || (2 < bVar6)) {
      *(byte *)((uint)bRam003ed441 * 0x82c + 0x3ed47c) =
           *(byte *)((uint)bRam003ed441 * 0x82c + 0x3ed47c) | 4;
      uRam003ed442 = 0;
      iRam003ed474 = iRam003ed474 + uVar4;
      return uVar10;
    }
    uRam003e281e = 0;
    uRam003e281c = (ushort)bRam003ed45d;
    lVar5 = func_0x00383970();
    if (lVar5 == 1) {
      if (!bVar11) {
        bVar11 = true;
        uRam003ed428 = 0;
        *(undefined *)((uint)bRam003ed441 * 0x82c + 0x3ed47d) = 0;
        *(undefined4 *)((uint)bRam003ed441 * 0x82c + 0x3ed4a4) =
             *(undefined4 *)((uint)bRam003ed441 * 0x82c + 0x3ed4a0);
        uRam003ed478 = 0;
        *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) =
             *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed496);
        *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49c) =
             *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed498);
      }
      DisplayJapaneseText();//func_0x003bd2b0(); 
      cVar7 = cVar7 + '\x01';
      uVar4 = uVar4 + 1 & 0xff;
      uVar10 = uVar10 + 1 & 0xff;
LAB_0003c89c:
      uVar4 = uVar4 + 1;
    }
    else {
      bVar1 = *(byte *)(param_1 + uVar4);
      if (bVar1 == 0xd) {
        if (cVar7 != '\0') {
          cVar7 = '\0';
          bVar6 = bVar6 + 1;
          *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) =
               *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed496);
          *(short *)((uint)bRam003ed441 * 0x82c + 0x3ed49c) =
               *(short *)((uint)bRam003ed441 * 0x82c + 0x3ed498) + 0x10;
        }
        goto LAB_0003c89c;
      }
      if (bVar1 == 0x20) {
        cVar7 = cVar7 + '\x01';
        *(short *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) =
             *(short *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) + 8;
        goto LAB_0003c89c;
      }
      if (bVar1 < 0x10) {
        uVar3 = *(byte *)(param_1 + uVar4) - 1;
        if (uVar3 < 9) {
                    /* WARNING: Could not emulate address calculation at 0x0003bf60 */
                    /* WARNING: Treating indirect jump as call */
          uVar4 = (**(code **)(uVar3 * 4 + 0x3fd100))();
          return uVar4;
        }
        uVar4 = uVar4 + 1;
      }
      else 
      {
        if (bVar1 != 0x10) 
        {
          if (!bVar11) 
          {
            uRam003ed428 = 0;
            bVar11 = true;
            *(undefined *)((uint)bRam003ed441 * 0x82c + 0x3ed47d) = 0;
            *(undefined4 *)((uint)bRam003ed441 * 0x82c + 0x3ed4a4) = *(undefined4 *)((uint)bRam003ed441 * 0x82c + 0x3ed4a0);
            uRam003ed478 = 0;
            *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) = *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed496);
            *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49c) = *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed498);
          }
          DisplayEnglishText();//func_0x003bd478(); //Same as DisplayEnglishText2 line 541 Saturn version
          cVar7 = cVar7 + '\x01';
          uVar10 = uVar10 + 1 & 0xff;
          goto LAB_0003c89c;
        }
        uVar3 = uVar4 + 1 & 0xff;
        if (!bVar11) 
        {
          bVar11 = true;
          uRam003ed428 = 0;
          *(undefined *)((uint)bRam003ed441 * 0x82c + 0x3ed47d) = 0;
          *(undefined4 *)((uint)bRam003ed441 * 0x82c + 0x3ed4a4) = *(undefined4 *)((uint)bRam003ed441 * 0x82c + 0x3ed4a0);
          uRam003ed478 = 0;
          *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) = *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed496);
          *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49c) = *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed498);
        }
        if (*(byte *)(param_1 + uVar3) < 0x10) 
        {
                    /* WARNING: Could not emulate address calculation at 0x0003c4a4 */
                    /* WARNING: Treating indirect jump as call */
          uVar4 = (**(code **)((uint)*(byte *)(param_1 + uVar3) * 4 + 0x3fd130))();
          return uVar4;
        }
        bVar9 = 0;
        bVar1 = func_0x00188e50();
        uVar4 = uVar3 + 1;
        if (bVar1 != 0) 
        {
          do 
          {
            DisplayJapaneseText();//func_0x003bd2b0(); 
            bVar9 = bVar9 + 2;
            cVar7 = cVar7 + '\x01';
            uVar10 = uVar10 + 1 & 0xff;
          } while (bVar9 < bVar1);
          uVar4 = uVar3 + 1;
        }
      }
    }
    uVar4 = uVar4 & 0xff;
  } while( true );
}

void UndefinedFunction_0003cb74(undefined param_1,uint param_2)
{
    int iVar1;
    undefined2 uVar2;
    int *piVar3;
    short sVar4;
    ushort uVar5;
    int iVar6;
    
    iVar6 = (param_2 & 0xffff) * 0x20;
    *(short *)(iVar6 + (uint)bRam003ed441 * 0x82c + 0x3ed4b0) = (short)(*(uint *)((uint)bRam003ed441 * 0x82c + 0x3ed4a4) >> 3);
    *(undefined2 *)((uint)bRam003ed441 * 0x82c + iVar6 + 0x3ed4b2) = 0x110; //assigned to a1
    *(undefined2 *)(iVar6 + (uint)bRam003ed441 * 0x82c + 0x3ed4b4) = *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49a);
    *(undefined2 *)(iVar6 + (uint)bRam003ed441 * 0x82c + 0x3ed4b6) = *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49c);
    piVar3 = (int *)((uint)bRam003ed441 * 0x82c + 0x3ed4a4);
    *piVar3 = *piVar3 + 0x40;
    *(short *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) = *(short *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) + 8;
    iVar1 = (uint)bRam003ed441 * 0x82c;
    uVar5 = *(ushort *)(iVar1 + 0x3ed49a);
    if ((long)((long)(int)((uint)*(ushort *)(iVar1 + 0x3ed496) + (uint)bRam003ed443 * 8) & 0xffffU) <= (long)*(short *)(iVar1 + 0x3ed49a)) 
    {
      uVar5 = *(ushort *)(iVar1 + 0x3ed496);
    }
    *(ushort *)(iVar1 + 0x3ed49a) = uVar5;
    uVar2 = uRam003e281c;
    iVar1 = (uint)bRam003ed441 * 0x82c;
    sVar4 = *(short *)(iVar1 + 0x3ed49c) + 0x10;
    if (*(short *)(iVar1 + 0x3ed49a) != *(short *)(iVar1 + 0x3ed496)) 
    {
      sVar4 = *(short *)(iVar1 + 0x3ed49c);
    }
    *(short *)(iVar1 + 0x3ed49c) = sVar4;
    func_0x00129d88(*(undefined2 *)((uint)bRam003ed441 * 0x82c + iVar6 + 0x3ed4b0),uVar2,0,param_1);
                      /* WARNING: Bad instruction - Truncating control flow here */
    halt_baddata();
}

void UndefinedFunction_0003cb74(undefined param_1,uint param_2)
{
    int iVar1;
    undefined2 uVar2;
    int *piVar3;
    short sVar4;
    ushort uVar5;
    int iVar6;
    
    iVar6 = (param_2 & 0xffff) * 0x20;
    *(short *)(iVar6 + (uint)bRam003ed441 * 0x82c + 0x3ed4b0) = (short)(*(uint *)((uint)bRam003ed441 * 0x82c + 0x3ed4a4) >> 3);
    *(undefined2 *)((uint)bRam003ed441 * 0x82c + iVar6 + 0x3ed4b2) = 0x110;
    *(undefined2 *)(iVar6 + (uint)bRam003ed441 * 0x82c + 0x3ed4b4) = *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49a);
    *(undefined2 *)(iVar6 + (uint)bRam003ed441 * 0x82c + 0x3ed4b6) = *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49c);
    piVar3 = (int *)((uint)bRam003ed441 * 0x82c + 0x3ed4a4);
    *piVar3 = *piVar3 + 0x40;
    iVar1 = (uint)bRam003ed441 * 0x82c;
    ushort uVar_a = *(short *)(iVar1 + 0x3ed49a);
    //*(short *)(iVar1 + 0x3ed49a) = *(short *)(iVar1 + 0x3ed49a) + 8;
    ushort uVar_b = uVar_a + 8;
    uVar5 = uVar_a;
    if ((long)((long)(int)((uint)*(ushort *)(iVar1 + 0x3ed496) + (uint)bRam003ed443 * 8) & 0xffffU) <= uVar_a) 
    {
      uVar5 = *(ushort *)(iVar1 + 0x3ed496);
    }
    uVar_a = uVar5;//*(ushort *)(iVar1 + 0x3ed49a) = uVar5;
    uVar2 = uRam003e281c;
    iVar1 = (uint)bRam003ed441 * 0x82c;
    sVar4 = *(short *)(iVar1 + 0x3ed49c) + 0x10;
    if (*(short *)(iVar1 + 0x3ed49a) != *(short *)(iVar1 + 0x3ed496)) 
    {
      sVar4 = *(short *)(iVar1 + 0x3ed49c);
    }
    *(short *)(iVar1 + 0x3ed49c) = sVar4;
    func_0x00129d88(*(undefined2 *)((uint)bRam003ed441 * 0x82c + iVar6 + 0x3ed4b0),uVar2,0,param_1);
                      /* WARNING: Bad instruction - Truncating control flow here */
    halt_baddata();
}