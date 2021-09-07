
/* WARNING: Control flow encountered bad instruction data */

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
  *(short *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) = *(short *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) + 8;
  iVar1 = (uint)bRam003ed441 * 0x82c;
  uVar5 = *(ushort *)(iVar1 + 0x3ed49a);
  if ((long)((long)(int)((uint)*(ushort *)(iVar1 + 0x3ed496) + (uint)bRam003ed443 * 8) & 0xffffU) <=
      (long)*(short *)(iVar1 + 0x3ed49a))
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

