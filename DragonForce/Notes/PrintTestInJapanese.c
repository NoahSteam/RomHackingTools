
/* WARNING: Control flow encountered bad instruction data */

void PrintTextInJapanese(undefined2 *param_1,uint param_2)

{
  int *piVar1;
  int iVar2;
  int iVar3;
  ushort uVar4;
  short sVar5;
  int iVar6;
  
  iVar6 = (param_2 & 0xffff) * 0x20;
  *(short *)(iVar6 + (uint)bRam003ed441 * 0x82c + 0x3ed4b0) =  (short)(*(uint *)((uint)bRam003ed441 * 0x82c + 0x3ed4a4) >> 3);
  *(undefined2 *)((uint)bRam003ed441 * 0x82c + iVar6 + 0x3ed4b2) = 0x210;
  *(undefined2 *)(iVar6 + (uint)bRam003ed441 * 0x82c + 0x3ed4b4) = *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49a);
  *(undefined2 *)(iVar6 + (uint)bRam003ed441 * 0x82c + 0x3ed4b6) = *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49c);
  piVar1 = (int *)((uint)bRam003ed441 * 0x82c + 0x3ed4a4);
  *piVar1 = *piVar1 + 0x80;
  iVar2 = func_0x00129d30();
  *(short *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) =  *(short *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) + (short)(iVar2 << 1);
  iVar2 = (uint)bRam003ed441 * 0x82c;
  uVar4 = *(ushort *)(iVar2 + 0x3ed49a);
  if ((long)((long)(int)((uint)*(ushort *)(iVar2 + 0x3ed496) + (uint)bRam003ed443 * 8) & 0xffffU) <=
      (long)*(short *)(iVar2 + 0x3ed49a)) 
  {
    uVar4 = *(ushort *)(iVar2 + 0x3ed496);
  }
  *(ushort *)(iVar2 + 0x3ed49a) = uVar4;
  iVar2 = (uint)bRam003ed441 * 0x82c;
  sVar5 = *(short *)(iVar2 + 0x3ed49c);
  if (*(short *)(iVar2 + 0x3ed49a) == *(short *)(iVar2 + 0x3ed496)) 
  {
    iVar3 = func_0x00129d30();
    sVar5 = sVar5 + (short)(iVar3 << 1);
  }
  *(short *)(iVar2 + 0x3ed49c) = sVar5;
  func_0x00129d88(*(undefined2 *)((uint)bRam003ed441 * 0x82c + iVar6 + 0x3ed4b0),uRam003e281c,0,
                  *param_1);
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}

