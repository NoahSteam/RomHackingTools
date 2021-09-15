typedef unsigned char   undefined;

typedef unsigned char    bool;
typedef unsigned char    byte;
typedef unsigned int    uint;
typedef unsigned long    ulong;
typedef unsigned char    undefined1;
typedef unsigned short    undefined2;
typedef unsigned int    undefined4;
typedef unsigned long    undefined8;
typedef unsigned short    ushort;



void FUN_000002e0(undefined4 param_1)

{
  undefined *puVar1;
  uint uVar2;
  ulong *puVar3;
  undefined auStack112 [8];
  undefined auStack104 [8];
  undefined auStack96 [8];
  undefined8 uStack88;
  
  cRam00400938 = '\x01';
  uRam003ecf54 = 0xfffffffc;
  uRam0040093c = 0;
  uRam00400940 = param_1;
  do {
    func_0x003839a8();
    func_0x00384000(0,0x13f,0xef);
    func_0x003840f0(0,0,0);
    func_0x00384078(0,0,0);
    func_0x00384168(0,0);
    puVar1 = auStack112 + 7;
    uVar2 = (uint)puVar1 & 7;
    *(ulong *)(puVar1 + -uVar2) =
         *(ulong *)(puVar1 + -uVar2) & -1 << (uVar2 + 1) * 8 | uRam003f0910 >> (7 - uVar2) * 8;
    auStack112 = uRam003f0910;
    puVar1 = auStack104 + 7;
    uVar2 = (uint)puVar1 & 7;
    *(ulong *)(puVar1 + -uVar2) =
         *(ulong *)(puVar1 + -uVar2) & -1 << (uVar2 + 1) * 8 | uRam003f0918 >> (7 - uVar2) * 8;
    auStack104 = uRam003f0918;
    puVar1 = auStack96 + 7;
    uVar2 = (uint)puVar1 & 7;
    *(ulong *)(puVar1 + -uVar2) =
         *(ulong *)(puVar1 + -uVar2) & -1 << (uVar2 + 1) * 8 | uRam003f0920 >> (7 - uVar2) * 8;
    auStack96 = uRam003f0920;
    uVar2 = (int)&uStack88 + 7U & 7;
    puVar3 = (ulong *)(((int)&uStack88 + 7U) - uVar2);
    *puVar3 = *puVar3 & -1 << (uVar2 + 1) * 8 | uRam003f0928 >> (7 - uVar2) * 8;
    uStack88 = uRam003f0928;
    func_0x00383a90(0);
    if (uRam0040093c == 1) {
      func_0x003812f8(0,8,0x10,0x3f09a8);
      return;
    }
    if (uRam0040093c < 2) {
      if (uRam0040093c == 0) {
        func_0x003812f8(0,8,0x10,0x3f09a8);
        return;
      }
    }
    else {
      if (uRam0040093c == 2) {
        func_0x003812f8(0,8,0xa8,0x3f09b0);
        return;
      }
    }
    if (uRam0040093c == 1) {
      func_0x00381a68();
    }
    else {
      if (uRam0040093c < 2) {
        if (uRam0040093c == 0) {
          func_0x003814d8();
        }
      }
      else {
        if (uRam0040093c == 2) {
          func_0x00382950();
        }
      }
    }
    if (((uRam002afcb0 & 0x800) != 0) && ((uRam003ecf60 & 0x800) == 0)) {
      cRam00400938 = '\0';
    }
    func_0x00383c00();
    func_0x001485c8();
  } while (cRam00400938 != '\0');
  return;
}



void FUN_000009f8(undefined param_1,short param_2,undefined2 param_3,undefined8 param_4,
                 undefined8 param_5,undefined8 param_6,undefined8 param_7,undefined8 param_8)

{
  uint uVar1;
  byte local_170 [256];
  undefined8 local_20;
  undefined8 local_18;
  undefined8 local_10;
  undefined8 local_8;
  
  uVar1 = 0;
  local_20 = param_5;
  local_18 = param_6;
  local_10 = param_7;
  local_8 = param_8;
  func_0x001384c0(local_170,param_4,&local_20);
  sRam003e1574 = param_2;
  uRam003e1576 = param_3;
  if ((local_170[0] != 0) && (uRam003e2854 < 0x200)) {
    while( true ) {
      sRam003e1570 = (short)((int)((uint)local_170[0] << 0x18) >> 0x16) + 0x1ddc;
      func_0x00383a90(param_1,0x3e1568);
      sRam003e1574 = sRam003e1574 + 7;
      uVar1 = uVar1 + 1 & 0xffff;
      if ((local_170[uVar1] == 0) || (0x1ff < uRam003e2854)) break;
      local_170[0] = local_170[uVar1];
    }
  }
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00000b08(void)

{
  undefined uVar1;
  undefined2 in_t0_lo;
  undefined *puVar2;
  uint uVar3;
  
  uVar3 = 0;
  func_0x00138448(0x400918,0x3f0b68,in_t0_lo);
  if (cRam00400918 != '\0') {
    puVar2 = (undefined *)0x400918;
    do {
      uVar1 = func_0x00189568(*puVar2);
      *puVar2 = uVar1;
      uVar3 = uVar3 + 1 & 0xff;
      puVar2 = (undefined *)(uVar3 + 0x400918);
    } while (*(char *)(uVar3 + 0x400918) != '\0');
  }
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00001168(void)

{
  bool bVar1;
  char *pcVar2;
  byte bVar3;
  ushort uVar4;
  int iVar5;
  uint uVar6;
  uint uVar7;
  
  if (iRam003ecf54 < 6) goto LAB_00001334;
  if ((uRam002afcb0 & 0x4000) == 0) {
    if ((uRam002afcb0 & 0x8000) == 0) {
      if ((uRam002afcb0 & 0x200) == 0) {
        if ((uRam002afcb0 & 8) != 0) {
          pcVar2 = (char *)0x400939;
          bVar1 = '\0' < (char)bRam00400939;
          bVar3 = bRam00400939;
          goto LAB_00001208;
        }
        if ((uRam002afcb0 & 0x80) != 0) {
          pcVar2 = (char *)0x400939;
          bVar1 = (char)bRam00400939 < '\x06';
          bVar3 = bRam00400939;
          goto LAB_00001238;
        }
      }
      else {
        iRam003ecf54 = 0;
        uRam0040093c = 0;
      }
    }
    else {
      pcVar2 = (char *)0x3e15d0;
      bVar1 = bRam003e15d0 < 7;
      bVar3 = bRam003e15d0;
LAB_00001238:
      iRam003ecf54 = 0;
      *pcVar2 = bVar3 + bVar1;
    }
  }
  else {
    pcVar2 = (char *)0x3e15d0;
    bVar1 = bRam003e15d0 != 0;
    bVar3 = bRam003e15d0;
LAB_00001208:
    iRam003ecf54 = 0;
    *pcVar2 = bVar3 - bVar1;
  }
  if ((uRam002afcb0 & 0x400) == 0) {
    if ((uRam002afcb0 & 0x100) != 0) {
      uVar6 = (7 - (uint)bRam003e15d0) * 4;
      uVar7 = uRam00400940 >> (uVar6 & 0x1f) & 0xf;
      iVar5 = 0xf;
      if (uVar7 != 0) {
        iVar5 = uVar7 - 1;
      }
      uRam00400940 = uRam00400940 & ~(uint)(0xf << (long)(int)uVar6) | iVar5 << (uVar6 & 0x1f);
      iRam003ecf54 = 0;
    }
  }
  else {
    uVar7 = (7 - (uint)bRam003e15d0) * 4;
    uVar6 = uRam00400940 >> (uVar7 & 0x1f) & 0xf;
    iVar5 = uVar6 + 1;
    if (0xe < uVar6) {
      iVar5 = 0;
    }
    uRam00400940 = uRam00400940 & ~(uint)(0xf << (long)(int)uVar7) | iVar5 << (uVar7 & 0x1f);
    iRam003ecf54 = 0;
  }
LAB_00001334:
  uVar4 = uRam003e15d2 + 1;
  if (0x1e < uRam003e15d2) {
    uVar4 = 0;
  }
  sRam003e15e8 = (ushort)bRam003e15d0 * 7 + 0x36;
  sRam003e15e4 = (ushort)bRam003e15d0 * 7 + 0x30;
  uRam003e15d2 = uVar4;
  uRam003e15de = uRam003e15de & 0xffe0 | uVar4;
  uRam003e15e6 = 0x10;
  uRam003e15ea = 0x10;
  sRam003e15ec = sRam003e15e8;
  uRam003e15ee = 0x16;
  sRam003e15f0 = sRam003e15e4;
  uRam003e15f2 = 0x16;
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_00002050(void)

{
  return;
}



void FUN_000021d8(undefined2 param_1,undefined2 param_2)

{
  long lVar1;
  
  lVar1 = func_0x003bae50();
  if (lVar1 == 0) {
    func_0x00129d68(0);
  }
  else {
    func_0x00129d68(1);
  }
  uRam003e284a = param_1;
  uRam003e284c = param_2;
  return;
}



void FUN_00002260(uint param_1,undefined param_2,undefined8 param_3,undefined8 param_4,
                 undefined8 param_5,undefined8 param_6,undefined8 param_7,undefined8 param_8)

{
  uint uVar1;
  undefined auStack368 [256];
  undefined8 local_28;
  undefined8 local_20;
  undefined8 local_18;
  undefined8 local_10;
  undefined8 local_8;
  
  uVar1 = param_1 & 0xff;
  if (((ushort)(sRam003e284c + 0xfU) < 0xff) &&
     ((uint)*(byte *)(uVar1 + 0x3e2840) <= *(uint *)(uVar1 * 4 + 0x3e2820))) {
    local_28 = param_4;
    local_20 = param_5;
    local_18 = param_6;
    local_10 = param_7;
    local_8 = param_8;
    func_0x001384c0(auStack368,param_3,&local_28);
    func_0x00382ce0(uVar1,param_2,auStack368);
  }
  return;
}



void FUN_00002320(uint param_1,undefined param_2,undefined8 param_3,undefined8 param_4,
                 undefined8 param_5,undefined8 param_6,undefined8 param_7,undefined8 param_8)

{
  uint uVar1;
  undefined auStack368 [256];
  undefined8 local_28;
  undefined8 local_20;
  undefined8 local_18;
  undefined8 local_10;
  undefined8 local_8;
  
  uVar1 = param_1 & 0xff;
  if (((ushort)(sRam003e284c + 0xfU) < 0xff) &&
     ((uint)*(byte *)(uVar1 + 0x3e2840) <= *(uint *)(uVar1 * 4 + 0x3e2820))) {
    local_28 = param_4;
    local_20 = param_5;
    local_18 = param_6;
    local_10 = param_7;
    local_8 = param_8;
    func_0x00138500(auStack368,param_3,&local_28);
    func_0x00382ce0(uVar1,param_2,auStack368);
  }
  return;
}



void FUN_000023e0(undefined param_1,char param_2,int param_3)

{
  char cVar1;
  undefined2 uVar2;
  short sVar3;
  short sVar4;
  uint uVar5;
  int iVar6;
  long lVar7;
  undefined8 uVar8;
  long lVar9;
  long lVar10;
  char *pcVar11;
  uint local_54;
  
  uRam003e281c = (ushort)bRam003e2848;
  uRam003e281e = (ushort)bRam003e2849;
  uVar5 = func_0x00188e50(param_3);
  local_54 = 0;
  if ((uVar5 & 0xffff) != 0) {
    do {
      if (0x140 < sRam003e284a) {
        return;
      }
      pcVar11 = (char *)(param_3 + local_54);
      lVar7 = func_0x00383970(*pcVar11);
      if (lVar7 == 0) {
        if (*pcVar11 != '\n') {
          if (sRam003e284a < -0xf) {
            cVar1 = *pcVar11;
          }
          else {
            if (param_2 == '\x01') {
              lVar7 = func_0x00383118(1,pcVar11);
              uVar8 = 1;
              if (lVar7 == 0xff) goto LAB_0000259c;
            }
            else {
              lVar7 = func_0x00383118(0,pcVar11);
              if (lVar7 == 0xff) {
                uVar8 = 0;
LAB_0000259c:
                lVar7 = func_0x003832c8(uVar8,pcVar11);
              }
            }
            sVar3 = sRam003e284c;
            sVar4 = sRam003e284a;
            if (param_2 == '\x01') {
              uVar2 = *(undefined2 *)((int)lVar7 * 6 + 0x3e1c08);
            }
            else {
              uVar2 = *(undefined2 *)((int)lVar7 * 6 + 0x3e2214);
            }
            uVar8 = 0x110;
            if (param_2 != '\x01') {
              uVar8 = 0x108;
            }
            lVar10 = (long)sRam003e284a;
            lVar9 = (long)sRam003e284c;
            if ((byte)(*pcVar11 + 0x22U) < 2) {
              iVar6 = func_0x00129d30();
              lVar9 = (long)((sVar3 + -2) * 0x10000 >> 0x10);
              lVar10 = (long)(((int)sVar4 - iVar6 / 2) * 0x10000 >> 0x10);
            }
            if (lVar7 != 0xff) {
              func_0x00383e18(param_1,0,0xc88,uRam003e284e,uVar2,uVar8,lVar10,lVar9);
            }
            cVar1 = *pcVar11;
          }
          if (1 < (byte)(cVar1 + 0x22U)) {
            sVar4 = func_0x00129d30();
            goto LAB_00002698;
          }
        }
      }
      else {
        if ((-0x10 < sRam003e284a) &&
           ((lVar7 = func_0x00383118(2,pcVar11), lVar7 != 0xff ||
            (lVar7 = func_0x003832c8(2,pcVar11), lVar7 != 0xff)))) {
          func_0x00383e18(param_1,0,0xc88,uRam003e284e,*(undefined2 *)((int)lVar7 * 6 + 0x3e15fc),
                          0x210,sRam003e284a,sRam003e284c);
        }
        local_54 = local_54 + 1 & 0xffff;
        iVar6 = func_0x00129d30();
        sVar4 = (short)(iVar6 << 1);
LAB_00002698:
        sRam003e284a = sRam003e284a + sVar4;
      }
      local_54 = local_54 + 1 & 0xffff;
    } while (local_54 < (uVar5 & 0xffff));
  }
  return;
}



void FUN_00002748(char param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4)

{
  uint uVar1;
  long lVar2;
  undefined *puVar3;
  uint uVar4;
  
  uVar4 = 0;
  uVar1 = func_0x00188e50(param_4);
  if ((uVar1 & 0xff) != 0) {
    puVar3 = (undefined *)param_4;
    do {
      lVar2 = func_0x00383970(*puVar3);
      if (lVar2 == 0) {
        if (param_1 == '\x01') {
          func_0x003832c8(1,puVar3);
        }
        else {
          func_0x003832c8(0,puVar3);
        }
      }
      else {
        func_0x003832c8(2,puVar3);
        uVar4 = uVar4 + 1 & 0xff;
      }
      uVar4 = uVar4 + 1 & 0xff;
      puVar3 = (undefined *)param_4 + uVar4;
    } while (uVar4 < (uVar1 & 0xff));
  }
  return;
}



uint FUN_00002818(byte param_1,char *param_2)

{
  int iVar1;
  uint uVar2;
  uint uVar3;
  
  uVar3 = (uint)bRam003e2849 | (bRam003e2848 & 0xf) << 4;
  if (param_1 == 1) {
    uVar2 = 0;
    if (bRam003e2204 != 0) {
      iVar1 = 0;
      while ((iVar1 = (iVar1 + uVar2) * 2, *(byte *)(iVar1 + 0x3e1c04) != uVar3 ||
             (*(char *)(iVar1 + 0x3e1c05) != *param_2))) {
        uVar2 = uVar2 + 1 & 0xff;
        if (bRam003e2204 <= uVar2) {
          return 0xff;
        }
        iVar1 = uVar2 << 1;
      }
      return uVar2;
    }
  }
  else {
    if (param_1 < 2) {
      if (param_1 == 0) {
        uVar2 = 0;
        if (bRam003e2810 != 0) {
          iVar1 = 0;
          while ((iVar1 = (iVar1 + uVar2) * 2, *(byte *)(iVar1 + 0x3e2210) != uVar3 ||
                 (*(char *)(iVar1 + 0x3e2211) != *param_2))) {
            uVar2 = uVar2 + 1 & 0xff;
            if (bRam003e2810 <= uVar2) {
              return 0xff;
            }
            iVar1 = uVar2 << 1;
          }
          return uVar2;
        }
      }
    }
    else {
      if (param_1 == 2) {
        uVar2 = 0;
        if (bRam003e1bf8 != 0) {
          iVar1 = 0;
          while( true ) {
            iVar1 = (iVar1 + uVar2) * 2;
            if (((*(byte *)(iVar1 + 0x3e15f8) == uVar3) && (*(char *)(iVar1 + 0x3e15f9) == *param_2)
                ) && (*(char *)(iVar1 + 0x3e15fa) == param_2[1])) {
              return uVar2;
            }
            uVar2 = uVar2 + 1 & 0xff;
            if (bRam003e1bf8 <= uVar2) break;
            iVar1 = uVar2 << 1;
          }
        }
      }
    }
  }
  return 0xff;
}



byte FUN_000029c8(byte param_1,undefined8 param_2)

{
  undefined2 uVar1;
  byte bVar2;
  byte bVar3;
  byte bVar4;
  byte bVar5;
  undefined2 *puVar6;
  
  func_0x0013d890();
  puVar6 = (undefined2 *)param_2;
  bVar2 = bRam003e1bf8;
  bVar3 = bRam003e2204;
  bVar4 = bRam003e2810;
  if (param_1 == 1) {
    bVar5 = 0xff;
    if (bRam003e2204 < bRam003e2205) {
      *(byte *)((uint)bRam003e2204 * 6 + 0x3e1c04) = bRam003e2849 | cRam003e2848 << 4;
      uVar1 = uRam003e281c;
      *(undefined *)((uint)bRam003e2204 * 6 + 0x3e1c05) = *(undefined *)puVar6;
      *(undefined *)((uint)bRam003e2204 * 6 + 0x3e1c06) = 0;
      *(short *)((uint)bRam003e2204 * 6 + 0x3e1c08) = (short)(uRam003e2208 >> 3);
      func_0x00129d88(*(undefined2 *)((uint)bRam003e2204 * 6 + 0x3e1c08),uVar1,0,
                      *(undefined *)puVar6);
      func_0x00383758(*(undefined *)puVar6,uRam003e2208);
      uRam003e2208 = uRam003e2208 + 0x40;
      bVar2 = bRam003e1bf8;
      bVar3 = bRam003e2204 + 1;
      bVar4 = bRam003e2810;
      bVar5 = bRam003e2204;
    }
  }
  else {
    if (param_1 < 2) {
      bVar5 = 0xff;
      if ((param_1 == 0) && (bVar5 = 0xff, bRam003e2810 < bRam003e2811)) {
        *(byte *)((uint)bRam003e2810 * 6 + 0x3e2210) = bRam003e2849 | cRam003e2848 << 4;
        uVar1 = uRam003e281c;
        *(undefined *)((uint)bRam003e2810 * 6 + 0x3e2211) = *(undefined *)puVar6;
        *(undefined *)((uint)bRam003e2810 * 6 + 0x3e2212) = 0;
        *(short *)((uint)bRam003e2810 * 6 + 0x3e2214) = (short)(uRam003e2814 >> 3);
        func_0x00129d88(*(undefined2 *)((uint)bRam003e2810 * 6 + 0x3e2214),uVar1,1,
                        *(undefined *)puVar6);
        func_0x003836c8(*(undefined *)puVar6,uRam003e2814);
        uRam003e2814 = uRam003e2814 + 0x20;
        bVar2 = bRam003e1bf8;
        bVar3 = bRam003e2204;
        bVar4 = bRam003e2810 + 1;
        bVar5 = bRam003e2810;
      }
    }
    else {
      bVar5 = 0xff;
      if ((param_1 == 2) && (bVar5 = 0xff, bRam003e1bf8 < bRam003e1bf9)) {
        *(byte *)((uint)bRam003e1bf8 * 6 + 0x3e15f8) = bRam003e2849 | cRam003e2848 << 4;
        uVar1 = uRam003e281c;
        *(undefined *)((uint)bRam003e1bf8 * 6 + 0x3e15f9) = *(undefined *)puVar6;
        *(undefined *)((uint)bRam003e1bf8 * 6 + 0x3e15fa) = *(undefined *)((int)puVar6 + 1);
        *(short *)((uint)bRam003e1bf8 * 6 + 0x3e15fc) = (short)(uRam003e1c00 >> 3);
        func_0x00129d88(*(undefined2 *)((uint)bRam003e1bf8 * 6 + 0x3e15fc),uVar1,0,*puVar6);
        func_0x003837e8(param_2,uRam003e1c00);
        uRam003e1c00 = uRam003e1c00 + 0x80;
        bVar2 = bRam003e1bf8 + 1;
        bVar3 = bRam003e2204;
        bVar4 = bRam003e2810;
        bVar5 = bRam003e1bf8;
      }
    }
  }
  bRam003e2810 = bVar4;
  bRam003e2204 = bVar3;
  bRam003e1bf8 = bVar2;
  return bVar5;
}



void FUN_00002dc8(uint param_1,int param_2)

{
  undefined uVar1;
  uint uVar2;
  undefined *puVar3;
  uint uVar4;
  
  puVar3 = (undefined *)((param_1 & 0xffff) * 8 + 0xca2460);
  uVar4 = 0;
  uVar2 = 0;
  do {
    do {
      uVar1 = *puVar3;
      puVar3 = puVar3 + 1;
      func_0x00383898(uVar1,(uVar4 + uVar2) * 4 + param_2);
      uVar2 = uVar2 + 1 & 0xffff;
    } while (uVar2 == 0);
    uVar4 = uVar4 + 1 & 0xffff;
    uVar2 = 0;
  } while (uVar4 < 8);
  return;
}



void FUN_00002e58(uint param_1,int param_2)

{
  undefined uVar1;
  uint uVar2;
  undefined *puVar3;
  uint uVar4;
  
  puVar3 = (undefined *)((param_1 & 0xffff) * 0x10 + 0xca1460);
  uVar4 = 0;
  uVar2 = 0;
  do {
    do {
      uVar1 = *puVar3;
      puVar3 = puVar3 + 1;
      func_0x00383898(uVar1,(uVar4 + uVar2) * 4 + param_2);
      uVar2 = uVar2 + 1 & 0xffff;
    } while (uVar2 == 0);
    uVar4 = uVar4 + 1 & 0xffff;
    uVar2 = 0;
  } while (uVar4 < 0x10);
  return;
}



void FUN_00002ee8(undefined *param_1,int param_2)

{
  undefined uVar1;
  int iVar2;
  uint uVar3;
  undefined *puVar4;
  uint uVar5;
  
  uVar5 = 0;
  iVar2 = func_0x00383638(*param_1,param_1[1]);
  puVar4 = (undefined *)(iVar2 * 0x20 + 0xc7f000);
  do {
    uVar3 = 0;
    iVar2 = uVar5 * 2;
    while( true ) {
      uVar1 = *puVar4;
      puVar4 = puVar4 + 1;
      func_0x00383898(uVar1,iVar2 * 4 + param_2);
      uVar3 = uVar3 + 1 & 0xffff;
      if (1 < uVar3) break;
      iVar2 = uVar5 * 2 + uVar3;
    }
    uVar5 = uVar5 + 1 & 0xffff;
  } while (uVar5 < 0x10);
  return;
}



void FUN_000030a8(void)

{
  int iVar1;
  int iVar2;
  uint uVar3;
  
  uRam00400948 = uRam003e2854;
  uRam0040094a = 1;
  func_0x00187580(iRam003e2850,0);
  iVar1 = iRam003e2850;
  uVar3 = 0;
  if (uRam003e2854 != 0) {
    do {
      iVar2 = uVar3 * 0x20;
      uVar3 = uVar3 + 1 & 0xffff;
      *(undefined2 *)(iVar2 + iVar1) = 0x8000;
    } while (uVar3 < uRam003e2854);
  }
  uVar3 = 0;
  do {
    iVar1 = uVar3 * 2;
    *(short *)(iVar1 + 0x400950) = (short)uVar3;
    uVar3 = uVar3 + 1 & 0xffff;
    *(undefined2 *)(iVar1 + 0x400960) = 9;
  } while (uVar3 < 8);
  uRam003e2854 = 8;
  return;
}



void FUN_00003300(void)

{
  int iVar1;
  long lVar2;
  int iVar3;
  ushort *puVar4;
  undefined2 *puVar5;
  ushort *puVar6;
  uint uVar7;
  
  uVar7 = 0;
  iVar3 = 0;
  do {
    iVar1 = iRam003e2850;
    puVar6 = (ushort *)(iVar3 + 0x400950);
    if (*(ushort *)(iVar3 + 0x400960) < 9) {
      puVar4 = (ushort *)((uint)*puVar6 * 0x20 + iRam003e2850);
      *puVar4 = *puVar4 | 0x1000;
      *(short *)((uint)*puVar6 * 0x20 + iVar1 + 2) = (short)(uVar7 << 2) + 4;
    }
    else {
      puVar4 = (ushort *)((uint)*puVar6 * 0x20 + iRam003e2850);
      if (puVar4[5] == 0) {
        *puVar4 = 0x4000;
      }
      else {
        *puVar4 = *puVar4 | 0x1000;
        *(short *)((uint)*puVar6 * 0x20 + iVar1 + 2) = (short)(uVar7 << 2) + 4;
      }
    }
    iVar1 = iRam003e2850;
    uVar7 = uVar7 + 1 & 0xffff;
    iVar3 = uVar7 << 1;
  } while (uVar7 < 7);
  if (*(ushort *)(uVar7 * 2 + 0x400960) < 9) {
    puVar4 = (ushort *)(uVar7 * 2 + 0x400950);
    puVar6 = (ushort *)((uint)*puVar4 * 0x20 + iRam003e2850);
    puVar5 = (undefined2 *)((uint)uRam003e2854 * 0x20 + iRam003e2850);
    *puVar6 = *puVar6 | 0x1000;
    *(ushort *)((uint)*puVar4 * 0x20 + iVar1 + 2) = uRam003e2854 << 2;
    *puVar5 = 0x8000;
    uRam003e2854 = uRam003e2854 + 1;
  }
  else {
    *(undefined2 *)(uVar7 * 0x20 + iRam003e2850) = 0x8000;
  }
  func_0x00141638(0x1000000,iRam003e2850,(uint)uRam003e2854 << 3);
  do {
    lVar2 = func_0x00141668();
  } while (lVar2 != 0);
  return;
}



void FUN_000034a0(undefined param_1,undefined2 param_2,undefined2 param_3,undefined2 param_4,
                 undefined2 param_5,undefined2 param_6,undefined2 param_7,undefined2 param_8)

{
  undefined2 local_30;
  undefined2 local_2e;
  undefined2 local_2c;
  undefined2 local_2a;
  undefined2 local_28;
  undefined2 local_26;
  undefined2 local_24;
  undefined2 local_22;
  undefined2 local_20;
  undefined2 local_1e;
  undefined2 local_1c;
  undefined2 local_1a;
  undefined2 local_18;
  undefined2 local_16;
  undefined2 local_14;
  undefined2 local_12;
  
  local_2e = 0;
  local_20 = 0;
  local_1e = 0;
  local_1c = 0;
  local_1a = 0;
  local_18 = 0;
  local_16 = 0;
  local_14 = 0;
  local_12 = 0;
  if (sRam0040094a == 1) {
    local_30 = param_2;
    local_2c = param_3;
    local_2a = param_4;
    local_28 = param_5;
    local_26 = param_6;
    local_24 = param_7;
    local_22 = param_8;
    func_0x00383a90(param_1,&local_30);
  }
  return;
}



void FUN_00003518(undefined param_1,ushort param_2,undefined2 param_3,undefined2 param_4,
                 undefined2 param_5,undefined2 param_6,undefined2 param_7,undefined2 param_8)

{
  ushort local_30;
  undefined2 local_2e;
  undefined2 local_2c;
  undefined2 local_2a;
  undefined2 local_28;
  undefined2 local_26;
  undefined2 local_24;
  undefined2 local_22;
  undefined2 local_20;
  undefined2 local_1e;
  undefined2 local_1c;
  undefined2 local_1a;
  undefined2 local_18;
  undefined2 local_16;
  undefined2 local_14;
  undefined2 local_12;
  
  local_30 = param_2 | 0x80;
  local_2e = 0;
  local_20 = 0;
  local_1e = 0;
  local_1c = 0;
  local_1a = 0;
  local_18 = 0;
  local_2c = param_3;
  local_2a = param_4;
  local_28 = param_5;
  local_26 = param_6;
  local_24 = param_7;
  local_22 = param_8;
  local_16 = func_0x00129d78();
  local_14 = 0;
  local_12 = 0;
  if (sRam0040094a == 1) {
    func_0x00383a90(param_1,&local_30);
  }
  return;
}



// WARNING: Restarted to delay deadcode elimination for space: stack

void FUN_00003658(undefined param_1,ushort param_2,undefined2 param_3,undefined2 param_4,
                 undefined2 param_5,undefined2 param_6,undefined2 param_7,undefined2 param_8,
                 undefined2 param_9,undefined2 param_10,undefined2 param_11,undefined2 param_12,
                 undefined2 param_13,undefined2 param_14)

{
  ushort local_40;
  undefined2 local_3e;
  undefined2 local_3c;
  undefined2 local_3a;
  undefined2 local_38;
  undefined2 local_36;
  undefined2 local_34;
  undefined2 local_32;
  undefined2 local_30;
  undefined2 local_2e;
  undefined2 local_2c;
  undefined2 local_2a;
  undefined2 local_28;
  undefined2 local_26;
  undefined2 local_24;
  undefined2 local_22;
  
  local_40 = param_2 | 2;
  local_30 = param_9;
  local_2e = param_10;
  local_2c = param_11;
  local_2a = param_12;
  local_28 = param_13;
  local_26 = param_14;
  local_3e = 0;
  local_24 = 0;
  local_22 = 0;
  if (sRam0040094a == 1) {
    local_3c = param_3;
    local_3a = param_4;
    local_38 = param_5;
    local_36 = param_6;
    local_34 = param_7;
    local_32 = param_8;
    func_0x00383a90(param_1,&local_40);
  }
  return;
}



void FUN_00003700(undefined param_1,undefined2 param_2,undefined2 param_3)

{
  undefined2 local_30;
  undefined2 local_2e;
  undefined2 local_2c;
  undefined2 local_2a;
  undefined2 local_28;
  undefined2 local_26;
  undefined2 local_24;
  undefined2 local_22;
  undefined2 local_20;
  undefined2 local_1e;
  undefined2 local_1c;
  undefined2 local_1a;
  undefined2 local_18;
  undefined2 local_16;
  undefined2 local_14;
  undefined2 local_12;
  
  local_30 = 9;
  local_2e = 0;
  local_2c = 0;
  local_2a = 0;
  local_28 = 0;
  local_26 = 0;
  local_24 = 0;
  local_22 = 0;
  local_20 = 0;
  local_1e = 0;
  local_18 = 0;
  local_16 = 0;
  local_14 = 0;
  local_12 = 0;
  if (sRam0040094a == 1) {
    local_1c = param_2;
    local_1a = param_3;
    func_0x00383a90(param_1,&local_30);
  }
  return;
}



void FUN_00003778(undefined param_1,undefined2 param_2,undefined2 param_3,undefined2 param_4,
                 undefined2 param_5)

{
  undefined2 local_30;
  undefined2 local_2e;
  undefined2 local_2c;
  undefined2 local_2a;
  undefined2 local_28;
  undefined2 local_26;
  undefined2 local_24;
  undefined2 local_22;
  undefined2 local_20;
  undefined2 local_1e;
  undefined2 local_1c;
  undefined2 local_1a;
  undefined2 local_18;
  undefined2 local_16;
  undefined2 local_14;
  undefined2 local_12;
  
  local_30 = 8;
  local_2e = 0;
  local_2c = 0;
  local_2a = 0;
  local_28 = 0;
  local_26 = 0;
  local_20 = 0;
  local_1e = 0;
  local_18 = 0;
  local_16 = 0;
  local_14 = 0;
  local_12 = 0;
  if (sRam0040094a == 1) {
    local_24 = param_2;
    local_22 = param_3;
    local_1c = param_4;
    local_1a = param_5;
    func_0x00383a90(param_1,&local_30);
  }
  return;
}



void FUN_000037f0(undefined param_1,undefined2 param_2,undefined2 param_3)

{
  undefined2 local_30;
  undefined2 local_2e;
  undefined2 local_2c;
  undefined2 local_2a;
  undefined2 local_28;
  undefined2 local_26;
  undefined2 local_24;
  undefined2 local_22;
  undefined2 local_20;
  undefined2 local_1e;
  undefined2 local_1c;
  undefined2 local_1a;
  undefined2 local_18;
  undefined2 local_16;
  undefined2 local_14;
  undefined2 local_12;
  
  local_30 = 10;
  local_2e = 0;
  local_2c = 0;
  local_2a = 0;
  local_28 = 0;
  local_26 = 0;
  local_20 = 0;
  local_1e = 0;
  local_1c = 0;
  local_1a = 0;
  local_18 = 0;
  local_16 = 0;
  local_14 = 0;
  local_12 = 0;
  if (sRam0040094a == 1) {
    local_24 = param_2;
    local_22 = param_3;
    func_0x00383a90(param_1,&local_30);
  }
  return;
}



void FUN_00003868(undefined param_1,undefined2 param_2,undefined2 param_3,undefined2 param_4,
                 undefined2 param_5,undefined2 param_6,short param_7)

{
  undefined2 local_30;
  undefined2 local_2e;
  undefined2 local_2c;
  undefined2 local_2a;
  undefined2 local_28;
  undefined2 local_26;
  undefined2 local_24;
  undefined2 local_22;
  undefined2 local_20;
  undefined2 local_1e;
  undefined2 local_1c;
  undefined2 local_1a;
  undefined2 local_18;
  undefined2 local_16;
  undefined2 local_14;
  undefined2 local_12;
  
  if (param_7 == 1) {
    local_30 = 4;
  }
  else {
    local_30 = 5;
  }
  local_2c = 0xcc0;
  local_2e = 0;
  local_28 = 0;
  local_26 = 0;
  local_14 = 0;
  local_12 = 0;
  if (sRam0040094a == 1) {
    local_2a = param_6;
    local_24 = param_2;
    local_22 = param_3;
    local_20 = param_4;
    local_1e = param_3;
    local_1c = param_4;
    local_1a = param_5;
    local_18 = param_2;
    local_16 = param_5;
    func_0x00383a90(param_1,&local_30);
  }
  return;
}



void FUN_000039c8(void)

{
  byte bVar1;
  byte bVar2;
  int iVar3;
  uint uVar4;
  char *pcVar5;
  int iVar6;
  uint uVar7;
  int iVar8;
  uint uVar9;
  
  if (((bRam003e2858 & 1) != 0) && (uVar9 = 0, bRam003e2859 != 0)) {
    iVar3 = 0;
    do {
      iVar3 = *(int *)(iVar3 + 0x3e285c);
      bVar1 = *(byte *)(iVar3 + 1);
      iVar8 = iVar3 + 0x30;
      if ((bVar1 & 2) == 0) {
        iVar8 = iVar3 + 0x10;
      }
      if ((bVar1 & 1) != 0) {
        bVar2 = *(byte *)(iVar3 + 0xc);
        uVar7 = (uint)bVar2;
        iVar6 = (uint)bVar2 * 2 + iVar8;
        if ((*(char *)(iVar6 + 1) != -1) &&
           ((ushort)*(byte *)(iVar6 + 1) < *(ushort *)(iVar3 + 0xe))) {
          *(undefined2 *)(iVar3 + 0xe) = 0;
          uVar4 = (uint)bVar2 + (uint)(uVar7 < 0x10);
          uVar7 = uVar4 & 0xff;
          *(char *)(iVar3 + 0xc) = (char)uVar4;
          pcVar5 = (char *)(uVar7 * 2 + iVar8);
          if (*pcVar5 == -1) {
            bVar2 = pcVar5[1];
            uVar7 = (uint)bVar2;
            *(byte *)(iVar3 + 0xc) = bVar2;
          }
        }
        if ((bVar1 & 4) == 0) {
          iVar8 = (uint)*(byte *)(uVar7 * 2 + iVar8) * (int)*(short *)(iVar3 + 10);
          iVar6 = iVar8 + 0x1060e20;
          if (iVar6 < 0) {
            iVar6 = iVar8 + 0x1060e27;
          }
          func_0x00383da0(0,0,0xc88,0xc1c0,iVar6 >> 3 & 0xffff,*(undefined2 *)(iVar3 + 8),
                          *(undefined2 *)(iVar3 + 4),*(undefined2 *)(iVar3 + 6));
        }
        else {
          iVar8 = (uint)*(byte *)(uVar7 * 2 + iVar8) * (int)*(short *)(iVar3 + 10);
          iVar6 = iVar8 + 0x106a220;
          if (iVar6 < 0) {
            iVar6 = iVar8 + 0x106a227;
          }
          func_0x00383da0(0,0x10,0x488,0xd440,iVar6 >> 3 & 0xffff,*(undefined2 *)(iVar3 + 8),
                          *(undefined2 *)(iVar3 + 2),*(undefined2 *)(iVar3 + 6));
        }
      }
      uVar9 = uVar9 + 1 & 0xffff;
      iVar3 = uVar9 << 2;
    } while (uVar9 < bRam003e2859);
  }
  return;
}



void FUN_00003b90(undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4,
                 undefined8 param_5,undefined8 param_6,undefined8 param_7,undefined8 param_8)

{
  undefined8 local_38;
  undefined8 local_30;
  undefined8 local_28;
  undefined8 local_20;
  undefined8 local_18;
  undefined8 local_10;
  undefined8 local_8;
  
  local_38 = param_2;
  local_30 = param_3;
  local_28 = param_4;
  local_20 = param_5;
  local_18 = param_6;
  local_10 = param_7;
  local_8 = param_8;
  func_0x00384978(0x16,0x56);
  func_0x003849b8(1,0x81,param_1,&local_38);
  uRam003e28ee = 0x300;
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00003e78(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



undefined8 FUN_00003e90(undefined8 param_1,int param_2)

{
  undefined uVar1;
  bool bVar2;
  undefined *puVar3;
  long lVar4;
  int iVar5;
  int iVar6;
  int iVar7;
  undefined uStack64;
  undefined local_3f;
  undefined local_3e;
  undefined local_3d;
  undefined local_3c;
  
  iVar7 = (int)param_1;
  puVar3 = (undefined *)(iVar7 + 7);
  iVar5 = 7;
  do {
    iVar5 = iVar5 + -1;
    *puVar3 = 0;
    puVar3 = puVar3 + -1;
  } while (-1 < iVar5);
  iVar5 = 0;
  func_0x00189140(param_1,param_2 + 0xc,6);
  bVar2 = true;
  do {
    iVar6 = iVar5 * 4;
    if (!bVar2) break;
    iVar5 = iVar5 + 1;
    lVar4 = func_0x00188bf8(param_1,*(undefined4 *)(iVar6 + 0x3e2908));
    bVar2 = iVar5 < 8;
  } while (lVar4 != 0);
  func_0x00138518(param_1,*(undefined4 *)(iVar6 + 0x3e7b78));
  *(undefined *)(iVar7 + 8) = 0x4c;
  *(undefined *)(iVar7 + 9) = 0x76;
  *(undefined *)(iVar7 + 10) = *(undefined *)(param_2 + 0x12);
  uVar1 = *(undefined *)(param_2 + 0x13);
  *(undefined *)(iVar7 + 0xc) = 0;
  *(undefined *)(iVar7 + 0xb) = uVar1;
  func_0x00138448(iVar7 + 0xd,0x3f18d0,*(undefined *)(param_2 + 0x14),*(undefined *)(param_2 + 0x15)
                 );
  func_0x00141498(*(undefined4 *)(param_2 + 0x18),&uStack64);
  func_0x00138448(iVar7 + 0x14,0x3f18e0,local_3f,local_3e);
  func_0x00138448(iVar7 + 0x1e,0x3f18f0,local_3d,local_3c);
  return param_1;
}



void FUN_00003fd8(undefined4 *param_1,int param_2)

{
  undefined4 uVar1;
  uint uVar2;
  uint uVar3;
  
  uVar2 = 0;
  do {
    uVar3 = uVar2 + 1;
    uVar1 = func_0x00141418(uVar2,0x46e0,param_2);
    *param_1 = uVar1;
    param_1 = param_1 + 1;
    param_2 = param_2 + 0x18;
    uVar2 = uVar3;
  } while (uVar3 < 2);
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00004050(undefined param_1)

{
  uRam00400cbc = param_1;
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00004078(undefined2 param_1,undefined2 param_2)

{
  uRam00400cb8 = param_1;
  uRam00400cba = param_2;
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_000041c0(undefined param_1,undefined param_2,undefined8 param_3,undefined8 param_4,
                 undefined8 param_5,undefined8 param_6,undefined8 param_7,undefined8 param_8)

{
  undefined8 local_28;
  undefined8 local_20;
  undefined8 local_18;
  undefined8 local_10;
  undefined8 local_8;
  
  local_28 = param_4;
  local_20 = param_5;
  local_18 = param_6;
  local_10 = param_7;
  local_8 = param_8;
  func_0x003849b8(param_1,param_2,param_3,&local_28);
  return;
}



bool FUN_00004260(undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4,
                 undefined8 param_5,undefined8 param_6,undefined8 param_7,undefined8 param_8)

{
  bool bVar1;
  undefined8 local_38;
  undefined8 local_30;
  undefined8 local_28;
  undefined8 local_20;
  undefined8 local_18;
  undefined8 local_10;
  undefined8 local_8;
  
  local_38 = param_2;
  local_30 = param_3;
  local_28 = param_4;
  local_20 = param_5;
  local_18 = param_6;
  local_10 = param_7;
  local_8 = param_8;
  func_0x00384950(7);
  bVar1 = (uRam004009b2 & 0x770) == 0;
  if (bVar1) {
    func_0x00384978(0x16,0x56);
    func_0x003849b8(1,1,param_1,&local_38);
    uRam003e28ee = 0x300;
    uRam00400cbd = 9;
    func_0x00384b18(0,8,0xffffffffffffffc0,0xffffffffffffffc0,0xffffffffffffffc0);
    func_0x00384b18(1,0x100,0,0,0);
    func_0x00384950(9);
  }
  else {
    (*pcRam00c0004c)(2,1);
    uRam00400cbd = 7;
    uRam003e28ee = 0x200;
    func_0x00384b18(0,8,0,0,0);
  }
  return !bVar1;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x000043a0: Changing call to branch
// WARNING: Removing unreachable block (ram,0x000043a8)

void FUN_00004398(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00004420(void)

{
  uRam003e28ee = 0x200;
  uRam00400cbd = 7;
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_00004460(undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4,
                 undefined8 param_5,undefined8 param_6,undefined8 param_7,undefined8 param_8)

{
  undefined8 local_38;
  undefined8 local_30;
  undefined8 local_28;
  undefined8 local_20;
  undefined8 local_18;
  undefined8 local_10;
  undefined8 local_8;
  
  local_38 = param_2;
  local_30 = param_3;
  local_28 = param_4;
  local_20 = param_5;
  local_18 = param_6;
  local_10 = param_7;
  local_8 = param_8;
  func_0x00384950(uRam00400cbd);
  func_0x00384978(0x54,0xb8);
  func_0x003849b8(0,1,param_1,&local_38);
  return;
}



void FUN_000044f8(void)

{
  undefined2 *puVar1;
  undefined2 uVar2;
  undefined2 *puVar3;
  int iVar4;
  long lVar5;
  
  puVar3 = (undefined2 *)0x3f17d8;
  lVar5 = 0;
  iVar4 = 0x3f17f0;
  do {
    if (lVar5 == cRam00400ca5) {
      func_0x00384950(uRam00400cbd);
      uVar2 = *puVar3;
    }
    else {
      func_0x00384950(9);
      uVar2 = *puVar3;
    }
    puVar1 = puVar3 + 1;
    puVar3 = puVar3 + 2;
    func_0x00384978(uVar2,*puVar1);
    func_0x00384ac0(0,1,iVar4);
    lVar5 = (long)(((int)lVar5 + 1) * 0x1000000 >> 0x18);
    iVar4 = iVar4 + 5;
  } while (lVar5 < 5);
  puVar3 = (undefined2 *)0x3f1840;
  iVar4 = 0;
  do {
    if (iVar4 == iRam00400ca8) {
      func_0x00384950(uRam00400cbd);
    }
    else {
      func_0x00384950(9);
    }
    if ((iVar4 == iRam00400ca8) || (iRam00400cb0 != 0)) {
      func_0x00384978(*puVar3,puVar3[1]);
      func_0x00384ac0(0,1,*(undefined4 *)(iVar4 * 4 + 0x3e2988));
    }
    iVar4 = (int)(char)((char)iVar4 + '\x01');
    puVar3 = puVar3 + 2;
  } while (iVar4 < 2);
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00004660(void)

{
  undefined8 uVar1;
  undefined8 uVar2;
  long lVar3;
  
  uVar1 = (*pcRam00c000c0)();
  func_0x00138548(0xb08000,uVar1);
  iRam003e28e0 = (*pcRam00c000d4)(0xb08010);
  iRam003e28e0 = iRam003e28e0 + 0x10;
  if (sRam00c0f100 != 0x11) {
    return;
  }
  uVar1 = func_0x0012c0a0();
  uVar2 = (*pcRam00c000c0)();
  lVar3 = func_0x00188bf8(uVar1,uVar2);
  if (lVar3 == 0) {
    func_0x0012c0a0();
  }
  else {
    uVar1 = func_0x0012c080();
    uVar2 = (*pcRam00c000c0)();
    lVar3 = func_0x00188bf8(uVar1,uVar2);
    if (lVar3 != 0) {
      return;
    }
    func_0x0012c080();
  }
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Removing unreachable block (ram,0x000047dc)

undefined8 FUN_00004798(undefined8 param_1)

{
  undefined uVar1;
  undefined uVar2;
  uint uVar3;
  uint uVar4;
  ulong *puVar5;
  uint uVar6;
  undefined8 uVar7;
  undefined8 uVar8;
  long lVar9;
  ulong uVar10;
  int iVar11;
  uint uVar12;
  
  func_0x00138518(param_1,*(undefined4 *)((uint)uRam00c00110 * 4 + 0x3e2908));
  iVar11 = 0x7f;
  uVar6 = *(ushort *)((uint)uRam00c00110 * 2 + 0xc00532) + 1 & 0xffff;
  uVar12 = (uint)param_1;
  *(char *)(uVar12 + 6) = (char)(uVar6 / 10) + '0';
  *(char *)(uVar12 + 7) = (char)(uVar6 % 10) + '0';
  do {
    iVar11 = iVar11 + -1;
  } while (-1 < iVar11);
  func_0x00138448(uVar12 + 8,0x3f1900);
  uVar10 = SEXT28(sRam00c0f100);
  if (uVar10 == 0x11) {
    uVar7 = func_0x0012c0a0();
    uVar8 = (*pcRam00c000c0)();
    lVar9 = func_0x00188bf8(uVar7,uVar8);
    if (lVar9 == 0) {
      uVar6 = func_0x0012c090();
    }
    else {
      uVar7 = func_0x0012c080();
      uVar8 = (*pcRam00c000c0)();
      lVar9 = func_0x00188bf8(uVar7,uVar8);
      if (lVar9 != 0) {
        return param_1;
      }
      uVar6 = func_0x0012c070();
    }
    uVar3 = uVar6 + 7 & 7;
    uVar4 = uVar6 & 7;
    uVar10 = (*(long *)((uVar6 + 7) - uVar3) << (7 - uVar3) * 8 |
             uVar10 & 0xffffffffffffffffU >> (uVar3 + 1) * 8) & -1 << (8 - uVar4) * 8 |
             *(ulong *)(uVar6 - uVar4) >> uVar4 * 8;
    uVar1 = *(undefined *)(uVar6 + 8);
    uVar2 = *(undefined *)(uVar6 + 9);
    uVar6 = uVar12 + 7 & 7;
    puVar5 = (ulong *)((uVar12 + 7) - uVar6);
    *puVar5 = *puVar5 & -1 << (uVar6 + 1) * 8 | uVar10 >> (7 - uVar6) * 8;
    uVar6 = uVar12 & 7;
    *(ulong *)(uVar12 - uVar6) =
         uVar10 << uVar6 * 8 | *(ulong *)(uVar12 - uVar6) & 0xffffffffffffffffU >> (8 - uVar6) * 8;
    *(undefined *)(uVar12 + 8) = uVar1;
    *(undefined *)(uVar12 + 9) = uVar2;
  }
  return param_1;
}



void FUN_00004938(undefined8 param_1,undefined8 param_2,ulong param_3,ulong param_4)

{
  uint uVar1;
  uint uVar2;
  ulong *puVar3;
  int iVar4;
  undefined8 *puVar5;
  undefined8 *puVar6;
  undefined8 *puVar7;
  undefined8 uVar8;
  undefined8 uVar9;
  ulong uVar10;
  ulong uVar11;
  long lVar12;
  undefined8 uVar13;
  
  uVar8 = func_0x0012c0a0();
  uVar9 = (*pcRam00c000c0)();
  func_0x00138548(uVar8,uVar9);
  iVar4 = func_0x0012c0a0();
  iRam003e28e0 = (*pcRam00c000d4)(iVar4 + 0x10);
  iRam003e28e0 = iRam003e28e0 + 0x10;
  uVar8 = func_0x0012c090();
  func_0x00385098(uVar8);
  uVar10 = func_0x0012c0a0();
  uVar11 = (*pcRam00c000c0)();
  lVar12 = func_0x00188bf8();
  if (lVar12 == 0) {
    puVar5 = (undefined8 *)func_0x0012c080();
    puVar6 = (undefined8 *)func_0x0012c0a0();
    puVar7 = puVar6 + 0xa00;
    if ((((uint)puVar6 | (uint)puVar5) & 7) == 0) {
      do {
        uVar8 = puVar6[1];
        uVar9 = puVar6[2];
        uVar13 = puVar6[3];
        *puVar5 = *puVar6;
        puVar5[1] = uVar8;
        puVar5[2] = uVar9;
        puVar5[3] = uVar13;
        puVar6 = puVar6 + 4;
        puVar5 = puVar5 + 4;
      } while (puVar6 != puVar7);
    }
    else {
      do {
        uVar1 = (int)puVar6 + 7U & 7;
        uVar2 = (uint)puVar6 & 7;
        uVar10 = (*(long *)(((int)puVar6 + 7U) - uVar1) << (7 - uVar1) * 8 |
                 uVar10 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((int)puVar6 - uVar2) >> uVar2 * 8;
        uVar1 = (int)puVar6 + 0xfU & 7;
        uVar2 = (uint)(puVar6 + 1) & 7;
        uVar11 = (*(long *)(((int)puVar6 + 0xfU) - uVar1) << (7 - uVar1) * 8 |
                 uVar11 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((int)(puVar6 + 1) - uVar2) >> uVar2 * 8;
        uVar1 = (int)puVar6 + 0x17U & 7;
        uVar2 = (uint)(puVar6 + 2) & 7;
        param_3 = (*(long *)(((int)puVar6 + 0x17U) - uVar1) << (7 - uVar1) * 8 |
                  param_3 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                  *(ulong *)((int)(puVar6 + 2) - uVar2) >> uVar2 * 8;
        uVar1 = (int)puVar6 + 0x1fU & 7;
        uVar2 = (uint)(puVar6 + 3) & 7;
        param_4 = (*(long *)(((int)puVar6 + 0x1fU) - uVar1) << (7 - uVar1) * 8 |
                  param_4 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                  *(ulong *)((int)(puVar6 + 3) - uVar2) >> uVar2 * 8;
        uVar1 = (int)puVar5 + 7U & 7;
        puVar3 = (ulong *)(((int)puVar5 + 7U) - uVar1);
        *puVar3 = *puVar3 & -1 << (uVar1 + 1) * 8 | uVar10 >> (7 - uVar1) * 8;
        uVar1 = (uint)puVar5 & 7;
        *(ulong *)((int)puVar5 - uVar1) =
             uVar10 << uVar1 * 8 |
             *(ulong *)((int)puVar5 - uVar1) & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = (int)puVar5 + 0xfU & 7;
        puVar3 = (ulong *)(((int)puVar5 + 0xfU) - uVar1);
        *puVar3 = *puVar3 & -1 << (uVar1 + 1) * 8 | uVar11 >> (7 - uVar1) * 8;
        uVar1 = (uint)(puVar5 + 1) & 7;
        puVar3 = (ulong *)((int)(puVar5 + 1) - uVar1);
        *puVar3 = uVar11 << uVar1 * 8 | *puVar3 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = (int)puVar5 + 0x17U & 7;
        puVar3 = (ulong *)(((int)puVar5 + 0x17U) - uVar1);
        *puVar3 = *puVar3 & -1 << (uVar1 + 1) * 8 | param_3 >> (7 - uVar1) * 8;
        uVar1 = (uint)(puVar5 + 2) & 7;
        puVar3 = (ulong *)((int)(puVar5 + 2) - uVar1);
        *puVar3 = param_3 << uVar1 * 8 | *puVar3 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = (int)puVar5 + 0x1fU & 7;
        puVar3 = (ulong *)(((int)puVar5 + 0x1fU) - uVar1);
        *puVar3 = *puVar3 & -1 << (uVar1 + 1) * 8 | param_4 >> (7 - uVar1) * 8;
        uVar1 = (uint)(puVar5 + 3) & 7;
        puVar3 = (ulong *)((int)(puVar5 + 3) - uVar1);
        *puVar3 = param_4 << uVar1 * 8 | *puVar3 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        puVar6 = puVar6 + 4;
        puVar5 = puVar5 + 4;
      } while (puVar6 != puVar7);
    }
  }
  return;
}



bool FUN_00004a98(undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined param_4)

{
  bool bVar1;
  int iVar2;
  long lVar3;
  undefined auStack112 [12];
  undefined auStack100 [11];
  undefined local_59;
  undefined4 local_58;
  undefined4 local_54;
  char local_40;
  byte local_3f;
  char local_3e;
  char local_3d;
  char local_3c;
  
  iVar2 = func_0x00142828();
  local_40 = (*(byte *)(iVar2 + 6) >> 4) * -0x18 + (*(byte *)(iVar2 + 5) & 0xf) +
             (*(byte *)(iVar2 + 5) >> 4) * '\n' + (*(byte *)(iVar2 + 6) & 0xf) * 'd' + 'D';
  local_3f = *(byte *)(iVar2 + 4) & 0xf;
  local_3e = (*(byte *)(iVar2 + 3) >> 4) * '\n' + (*(byte *)(iVar2 + 3) & 0xf);
  local_3d = (*(byte *)(iVar2 + 2) >> 4) * '\n' + (*(byte *)(iVar2 + 2) & 0xf);
  local_3c = (*(byte *)(iVar2 + 1) >> 4) * '\n' + (*(byte *)(iVar2 + 1) & 0xf);
  local_58 = func_0x001414e0(&local_40);
  local_54 = 0x46e0;
  func_0x00138448(auStack112,0x3f1908,0x3f1750,param_2);
  func_0x00138548(auStack100,param_3);
  local_59 = 0;
  lVar3 = func_0x00141430(param_1,auStack112,0xb08000,param_4);
  if (lVar3 == 0) {
    lVar3 = func_0x00141490(param_1,auStack112,0xb08000);
    bVar1 = lVar3 == 0;
  }
  else {
    bVar1 = false;
  }
  return bVar1;
}



int FUN_00004c40(undefined8 param_1,undefined8 param_2)

{
  int iVar1;
  int iVar2;
  
  iVar1 = func_0x00384778();
  iVar2 = func_0x00384778(param_2);
  return iVar1 - iVar2;
}



int FUN_00004c80(int param_1,int param_2)

{
  int iVar1;
  byte local_20;
  byte local_1f;
  byte local_1e;
  byte local_1d;
  byte local_1c;
  byte local_1a;
  byte local_19;
  byte local_18;
  byte local_17;
  byte local_16;
  
  func_0x00141498(*(undefined4 *)(param_1 + 0x18),&local_20);
  func_0x00141498(*(undefined4 *)(param_2 + 0x18),&local_1a);
  iVar1 = (uint)local_1a - (uint)local_20;
  if ((((local_1a == local_20) && (iVar1 = (uint)local_19 - (uint)local_1f, local_19 == local_1f))
      && (iVar1 = (uint)local_18 - (uint)local_1e, local_18 == local_1e)) &&
     (iVar1 = (uint)local_17 - (uint)local_1d, local_17 == local_1d)) {
    iVar1 = (uint)local_16 - (uint)local_1c;
  }
  return iVar1;
}



void FUN_00004d08(int param_1)

{
  undefined uVar1;
  undefined *puVar2;
  int iVar3;
  int iVar4;
  int *piVar5;
  int iVar6;
  int iVar7;
  int *piVar8;
  
  iVar4 = 0;
  iVar7 = param_1 * 4;
  piVar8 = (int *)(iVar7 + 0x3e2940);
  piVar5 = (int *)(iVar7 + 0x3e2948);
  func_0x001877e0(*piVar8,*piVar5,0x24,0x385580);
  if (0 < *piVar5) {
    iVar6 = 0;
    iVar3 = *piVar8;
    while( true ) {
      iVar3 = iVar3 + iVar6;
      iVar6 = iVar6 + 0x24;
      uVar1 = func_0x00384778(iVar3);
      puVar2 = (undefined *)(*(int *)(iVar7 + 0x3e2950) + iVar4);
      iVar4 = iVar4 + 1;
      *puVar2 = uVar1;
      if (*piVar5 <= iVar4) break;
      iVar3 = *piVar8;
    }
  }
  return;
}



void FUN_00004dd8(int param_1)

{
  int iVar1;
  int iVar2;
  int iVar3;
  int iVar4;
  int iVar5;
  
  iVar3 = param_1 * 4;
  iVar5 = 0;
  if (0 < *(int *)(iVar3 + 0x3e2948)) {
    iVar4 = 0;
    do {
      iVar1 = *(int *)(iVar3 + 0x3e2938) + iVar4;
      iVar5 = iVar5 + 1;
      iVar2 = *(int *)(iVar3 + 0x3e2940) + iVar4;
      iVar4 = iVar4 + 0x24;
      func_0x00384790(iVar1,iVar2);
    } while (iVar5 < *(int *)(iVar3 + 0x3e2948));
  }
  return;
}



undefined4 FUN_00004e98(undefined8 param_1)

{
  long lVar1;
  int iVar2;
  int *piVar3;
  undefined4 uVar4;
  char *pcVar5;
  undefined auStack64 [20];
  int local_2c;
  
  uVar4 = 2;
  lVar1 = func_0x00141418(param_1,0x46e0,auStack64);
  iVar2 = (int)param_1 * 4;
  piVar3 = (int *)(iVar2 + 0x3e2948);
  *piVar3 = 0;
  if ((lVar1 != 1) && (uVar4 = 3, lVar1 != 2)) {
    pcVar5 = (char *)((int)param_1 + 0x3f1760);
    lVar1 = func_0x00141478(param_1,0x3f1750,*pcVar5,*(undefined4 *)(iVar2 + 0x3e2940));
    *piVar3 = (int)lVar1;
    if (lVar1 < 0) {
      *piVar3 = (int)*pcVar5;
    }
    if (local_2c == 0) {
      if (*piVar3 == 0) {
        return 5;
      }
      uVar4 = 4;
    }
    else {
      if (*piVar3 == 0) {
        return 1;
      }
      uVar4 = 0;
    }
    func_0x00385608(param_1);
    func_0x003856d8(param_1);
  }
  return uVar4;
}



bool FUN_00004f98(void)

{
  long lVar1;
  
  lVar1 = func_0x00385798(1);
  return lVar1 != 2;
}



undefined4 FUN_00004fc0(void)

{
  long lVar1;
  undefined4 uVar2;
  
  lVar1 = 0xffff;
  if ((uRam004009b2 & 0x3000) != 0) {
    lVar1 = 2;
  }
  if (((uRam004009b2 & 0x1000) != 0) && (uRam00400ca8 != 0)) {
    uRam00400ca8 = 0;
  }
  if ((((uRam004009b2 & 0x2000) != 0) && (uRam00400ca8 != 1)) && (iRam00400cb0 != 0)) {
    uRam00400ca8 = 1;
  }
  uRam00400ca8 = uRam00400ca8 & 1;
  if ((uRam004009b2 & 0x100) == 0) {
    uVar2 = 2;
    if ((uRam004009b2 & 0x600) == 0) {
      uVar2 = 0;
    }
    else {
      lVar1 = 0;
    }
  }
  else {
    lVar1 = 1;
    uVar2 = 1;
  }
  if (lVar1 != 0xffff) {
    (*pcRam00c0004c)(2);
  }
  return uVar2;
}



void FUN_00005178(int param_1)

{
  undefined2 uVar1;
  int *piVar2;
  int iVar3;
  undefined2 *puVar4;
  int *piVar5;
  int iVar6;
  int iVar7;
  
  if (sRam003e28ee == 0x200) {
    iVar6 = 4;
    func_0x00384950(uRam00400cbd);
    puVar4 = (undefined2 *)0x3f1888;
    uVar1 = uRam003f1888;
    while( true ) {
      puVar4 = puVar4 + 1;
      iVar6 = iVar6 + -1;
      func_0x00384978(0x1c,uVar1);
      func_0x00384ac0(0,1,0x3f1910,0x3f1918);
      if (iVar6 < 0) break;
      uVar1 = *puVar4;
    }
    piVar2 = (int *)(param_1 * 4 + 0x3e2948);
    iVar6 = 0;
    if (0 < *piVar2) {
      piVar5 = (int *)(param_1 * 4 + 0x3e2938);
      puVar4 = (undefined2 *)0x3f1888;
      iVar7 = 0;
      uVar1 = uRam003f1888;
      while( true ) {
        iVar6 = iVar6 + 1;
        func_0x00384978(0x2c,uVar1);
        func_0x00384ac0(0,0x81,*piVar5 + iVar7);
        func_0x00384978(0x5c,*puVar4);
        func_0x00384ac0(0,0x81,*piVar5 + iVar7 + 8);
        func_0x00384978(0x82,*puVar4);
        func_0x00384ac0(0,0x81,*piVar5 + iVar7 + 0xd);
        func_0x00384978(0xb8,*puVar4);
        func_0x00384ac0(0,0x81,*piVar5 + iVar7 + 0x14);
        uVar1 = *puVar4;
        puVar4 = puVar4 + 1;
        func_0x00384978(0x106,uVar1);
        iVar3 = *piVar5 + iVar7;
        iVar7 = iVar7 + 0x24;
        func_0x00384ac0(0,0x81,iVar3 + 0x1e);
        if (*piVar2 <= iVar6) break;
        uVar1 = *puVar4;
      }
    }
  }
  return;
}



undefined8 FUN_00005320(void)

{
  int *piVar1;
  long lVar2;
  undefined8 unaff_s0;
  
  lVar2 = 0xffff;
  if (cRam003e29a4 == '\0') {
    cRam003e29a4 = '\x01';
    cRam00400cad = '\0';
  }
  else {
    if (cRam003e29a4 != '\x01') goto LAB_000054a8;
  }
  if ((uRam004009b2 & 0x3000) != 0) {
    lVar2 = 1;
  }
  if (((uRam004009b2 & 0x1000) == 0) || (cRam00400cad < '\x01')) {
    if (((uRam004009b2 & 0x2000) != 0) &&
       ((long)cRam00400cad < (long)(*(int *)(iRam00400ca8 * 4 + 0x3e2948) + -1))) {
      cRam00400cad = cRam00400cad + '\x01';
    }
  }
  else {
    cRam00400cad = cRam00400cad + -1;
  }
  piVar1 = (int *)(iRam00400ca8 * 4 + 0x3e2948);
  if ((long)*piVar1 <= (long)cRam00400cad) {
    cRam00400cad = *(char *)piVar1 + -1;
  }
  uRam00400cac = *(undefined *)(*(int *)(iRam00400ca8 * 4 + 0x3e2950) + (int)cRam00400cad);
  if ((uRam004009b2 & 0x100) == 0) {
    unaff_s0 = 2;
    if ((uRam004009b2 & 0x600) == 0) {
      unaff_s0 = 0;
    }
    else {
      cRam003e29a4 = '\0';
      lVar2 = 0;
    }
  }
  else {
    cRam003e29a4 = '\0';
    lVar2 = 1;
    unaff_s0 = 1;
  }
LAB_000054a8:
  if (lVar2 != 0xffff) {
    (*pcRam00c0004c)(2,lVar2);
  }
  return unaff_s0;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00005508(void)

{
  uRam00400cb0 = func_0x00385898();
  func_0x00385798(0);
  uRam00400cac = 0;
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_000055d0(void)

{
  func_0x00385e40();
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00005618(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



byte FUN_00005910(void)

{
  long lVar1;
  long lVar2;
  long lVar3;
  byte bVar4;
  
  func_0x00384d60(0x3f20c0);
  lVar3 = 0xffff;
  if ((uRam004009b2 & 0xf000) != 0) {
    lVar3 = 2;
  }
  if ((((uRam004009b2 & 0x1000) != 0) && ((long)(char)bRam003f1810 <= (long)(char)bRam00400ca5)) &&
     ((long)((int)(((uint)bRam00400ca5 - (uint)bRam003f1810) * 0x1000000) >> 0x18) <
      (long)(char)bRam003f1810)) {
    bRam00400ca5 = (byte)((uint)bRam00400ca5 - (uint)bRam003f1810);
  }
  if ((uRam004009b2 & 0x2000) == 0) {
LAB_000059e8:
    lVar2 = (long)(char)bRam003f1810;
    lVar1 = (long)(char)bRam00400ca5;
    if (lVar1 < lVar2) goto LAB_00005a14;
  }
  else {
    if ((char)bRam00400ca5 < (char)bRam003f1810) {
      if ((char)bRam00400ca5 < cRam003f1811) {
        bRam00400ca5 = bRam00400ca5 + bRam003f1810;
      }
      goto LAB_000059e8;
    }
  }
  lVar2 = (long)cRam003f1811;
  lVar1 = (long)((int)(((uint)bRam00400ca5 - (uint)bRam003f1810) * 0x1000000) >> 0x18);
LAB_00005a14:
  if (((uRam004009b2 & 0x8000) != 0) && (lVar1 < (int)lVar2 + -1)) {
    lVar1 = (long)(((int)lVar1 + 1) * 0x1000000 >> 0x18);
    bRam00400ca5 = bRam00400ca5 + 1;
  }
  if (((uRam004009b2 & 0x4000) != 0) && (0 < lVar1)) {
    bRam00400ca5 = bRam00400ca5 - 1;
  }
  if (4 < bRam00400ca5) {
    bRam00400ca5 = 0;
  }
  if ((uRam004009b2 & 0x600) == 0) {
    bVar4 = 5;
    if ((uRam004009b2 & 0x100) != 0) {
      lVar3 = 1;
      func_0x00385dd8();
      bVar4 = 4;
    }
  }
  else {
    lVar3 = 0;
    bVar4 = bRam00400ca5;
  }
  if (lVar3 != 0xffff) {
    (*pcRam00c0004c)(2,lVar3);
  }
  return bVar4;
}



undefined8 FUN_00005af0(void)

{
  undefined8 uVar1;
  
  if (bRam003e29ac < 9) {
                    // WARNING: Could not emulate address calculation at 0x00005b38
                    // WARNING: Treating indirect jump as call
    uVar1 = (**(code **)((char)bRam003e29ac * 4 + 0x3f1960))();
    return uVar1;
  }
  func_0x00384b60(0x3f1940);
  return 0;
}



undefined8 FUN_00006208(void)

{
  undefined8 uVar1;
  long lVar2;
  
  if (bRam003e29b8 < 0xe) {
                    // WARNING: Could not emulate address calculation at 0x00006250
                    // WARNING: Treating indirect jump as call
    uVar1 = (**(code **)((char)bRam003e29b8 * 4 + 0x3f19b0))();
    return uVar1;
  }
  lVar2 = func_0x00384b60(0x3f1988);
  if (lVar2 == 1) {
    bRam003e29b8 = 0xd;
  }
  return 0;
}



undefined8 FUN_00006b18(void)

{
  undefined8 uVar1;
  long lVar2;
  
  if (bRam003e29c4 < 0x67) {
                    // WARNING: Could not emulate address calculation at 0x00006b60
                    // WARNING: Treating indirect jump as call
    uVar1 = (**(code **)((char)bRam003e29c4 * 4 + 0x3f1a00))();
    return uVar1;
  }
  lVar2 = func_0x00384b60(0x3f19e8);
  if (lVar2 == 1) {
    bRam003e29c4 = 0xe;
  }
  return 0;
}



undefined8 FUN_000076d8(void)

{
  undefined8 uVar1;
  long lVar2;
  
  if (bRam003e29d8 < 0x6c) {
                    // WARNING: Could not emulate address calculation at 0x00007718
                    // WARNING: Treating indirect jump as call
    uVar1 = (**(code **)((char)bRam003e29d8 * 4 + 0x3f1bc0))();
    return uVar1;
  }
  lVar2 = func_0x00384b60(0x3f1ba0);
  if (lVar2 == 1) {
    bRam003e29d8 = 9;
  }
  return 0;
}



undefined8 FUN_00007de8(void)

{
  undefined8 uVar1;
  
  if (bRam003e29e4 < 7) {
                    // WARNING: Could not emulate address calculation at 0x00007e1c
                    // WARNING: Treating indirect jump as call
    uVar1 = (**(code **)((char)bRam003e29e4 * 4 + 0x3f1d80))();
    return uVar1;
  }
  return 0;
}



void FUN_00007ee8(void)

{
  long lVar1;
  
  uRam003ecf38 = 1;
  func_0x00384938();
  if (6 < bRam003e29e5) {
    if (sRam00c0f100 != 0x11) {
      func_0x00396780();
      func_0x001475c0(4,5);
      func_0x001475c0(8,4);
      uRam003ecf38 = 0;
    }
    uRam003eee52 = 0xff;
    uRam003eee4c = 0x3fff;
    bRam003e29e5 = 0;
    func_0x00384df8();
    func_0x00385a78(uRam00400ca8);
    func_0x00383a90(0,0x3e28e8);
    func_0x003b7c80(0,5,0xc,0xb0,0,0);
    func_0x00384950(uRam00400cbd);
    func_0x00384978(0x76,0x10);
    func_0x00384ac0(0,1,0x3f1dc8);
    lVar1 = (*pcRam00c000bc)();
    if (lVar1 == 1) {
      func_0x00384978(0x10,0x10);
      func_0x00384ac0(0,0,0x3f1dd8,uRam003e28e0);
      func_0x00384978(0xe0,0x10);
      func_0x00384ac0(0,0,0x3f1de8,0x46e0);
    }
    return;
  }
                    // WARNING: Could not emulate address calculation at 0x00007f38
                    // WARNING: Treating indirect jump as call
  (**(code **)((char)bRam003e29e5 * 4 + 0x3f1e00))();
  return;
}



// WARNING: Could not reconcile some variable overlaps

void FUN_00008368(void)

{
  undefined *puVar1;
  byte bVar2;
  undefined2 uVar3;
  ulong *puVar4;
  ushort uVar5;
  int iVar6;
  long lVar7;
  undefined2 *puVar8;
  ushort *puVar9;
  uint uVar10;
  undefined auStack80 [8];
  undefined auStack72 [8];
  undefined auStack64 [8];
  undefined auStack56 [8];
  undefined auStack48 [8];
  undefined8 uStack40;
  
  func_0x001470f8(1);
  func_0x00147460();
  func_0x001485c8();
  func_0x001472a0();
  func_0x001485c8();
  func_0x00147198();
  func_0x00147198();
  puVar1 = auStack80 + 7;
  uVar10 = (uint)puVar1 & 7;
  *(ulong *)(puVar1 + -uVar10) =
       *(ulong *)(puVar1 + -uVar10) & -1 << (uVar10 + 1) * 8 | uRam003f2480 >> (7 - uVar10) * 8;
  puVar1 = auStack72 + 3;
  uVar10 = (uint)puVar1 & 3;
  *(uint *)(puVar1 + -uVar10) =
       *(uint *)(puVar1 + -uVar10) & -1 << (uVar10 + 1) * 8 | uRam003f2488 >> (3 - uVar10) * 8;
  auStack72._0_4_ = uRam003f2488;
  uRam003ecf40 = 1;
  auStack80._0_7_ = CONCAT16(((byte)uRam00c00110 & 7) + 0x30,(int6)uRam003f2480);
  auStack80 = uRam003f2480 & 0xff00000000000000 | (ulong)auStack80._0_7_;
  (*pcRam00c0000c)(auStack80,0x100da00);
  func_0x001385d0(0);
  uRam003ecf40 = 0;
  func_0x00147198(0x100,0x480,0x10,0x100dbc0);
  func_0x00396918();
  func_0x001485c8();
  func_0x001478e0(6);
  func_0x00147928(0x100);
  lVar7 = func_0x003b5f10(0xe7);
  if (lVar7 != 0) {
    sRam00c0853c = 0xaa;
    uRam00c084fc = 0xaa;
  }
  if (sRam00c084ee == 0) {
    uRam00c0853e = 0xffff;
    uRam00c0857c = 0x8888;
    uRam00c084fe = 0xffff;
    if (uRam00c084f0 == 1) {
      func_0x00389340(0x427e);
      uVar10 = 0;
      iVar6 = func_0x003d0f90();
      if (uRam00c08500 != 0) {
        bVar2 = *(byte *)(iVar6 + 0x3e8908);
        puVar9 = (ushort *)0x3e2a60;
        do {
          *puVar9 = (ushort)bVar2;
          uVar10 = uVar10 + 1;
          puVar9 = puVar9 + 0x1d;
        } while (uVar10 < uRam00c08500);
      }
      func_0x00389340();
      uVar10 = 0;
      if (uRam00c08540 != 0) {
        puVar8 = (undefined2 *)0x3e2ca8;
        do {
          *puVar8 = 0x80;
          uVar10 = uVar10 + 1;
          puVar8 = puVar8 + 0x1d;
        } while (uVar10 < uRam00c08540);
      }
    }
    else {
      if (uRam00c084f0 < 2) {
        if (uRam00c084f0 == 0) {
          func_0x00389340();
          func_0x00389340();
        }
      }
      else {
        if (uRam00c084f0 == 2) {
          func_0x00389340(0x427e);
          uVar10 = 0;
          if (uRam00c08500 != 0) {
            puVar8 = (undefined2 *)0x3e2a60;
            do {
              *puVar8 = 0x80;
              uVar10 = uVar10 + 1;
              puVar8 = puVar8 + 0x1d;
            } while (uVar10 < uRam00c08500);
          }
          func_0x00389340(0x429e);
          uVar10 = 0;
          iVar6 = func_0x003d0f90();
          if (uRam00c08540 != 0) {
            bVar2 = *(byte *)(iVar6 + 0x3e8908);
            puVar9 = (ushort *)0x3e2ca8;
            do {
              *puVar9 = (ushort)bVar2;
              uVar10 = uVar10 + 1;
              puVar9 = puVar9 + 0x1d;
            } while (uVar10 < uRam00c08540);
          }
        }
      }
    }
    uRam00c0f200 = 0x123;
    uRam003e2a30 = 0xff;
    uRam00c0f202 = 0x4567;
    uRam00c0f208 = 1;
    uRam00c0f204 = 0;
    uRam00c0f206 = 0;
  }
  uVar10 = 0;
  if (uRam00c08500 != 0) {
    puVar8 = (undefined2 *)0xc08502;
    uVar3 = uRam00c08502;
    iVar6 = 0x1073600;
    while( true ) {
      puVar8 = puVar8 + 1;
      uVar10 = uVar10 + 1;
      func_0x00396a98(0x42,iVar6,uVar3);
      if (uRam00c08500 <= uVar10) break;
      uVar3 = *puVar8;
      iVar6 = iVar6 + 0x900;
    }
  }
  uVar10 = 0;
  if (uRam00c08540 != 0) {
    puVar8 = (undefined2 *)0xc08542;
    uVar3 = uRam00c08542;
    iVar6 = 0x1079000;
    while( true ) {
      puVar8 = puVar8 + 1;
      uVar10 = uVar10 + 1;
      func_0x00396a98(0x42,iVar6,uVar3);
      if (uRam00c08540 <= uVar10) break;
      uVar3 = *puVar8;
      iVar6 = iVar6 + 0x900;
    }
  }
  func_0x00396a98(0x53,0x107ea00,uRam00c00112);
  uVar5 = 0;
  do {
    func_0x00397998(uVar5,0xffffffffffffffff,0xffffffffffffffff,0,0);
    uVar5 = uVar5 + 1;
  } while (uVar5 < 0x20);
  func_0x00138590();
  func_0x003bb810();
  lVar7 = func_0x003b5f10();
  if (lVar7 != 0) goto LAB_00008970;
  if (uRam00c084f0 == 1) {
    if (sRam00c0853c != 0x3b) {
      (*pcRam00c00038)();
      goto LAB_00008970;
    }
LAB_00008940:
    (*pcRam00c00038)();
  }
  else {
    if (uRam00c084f0 < 2) {
      if (uRam00c084f0 == 0) {
        if (sRam00c0853c != 0x3b) {
          (*pcRam00c00038)();
          goto LAB_00008970;
        }
        goto LAB_00008940;
      }
    }
    else {
      if (uRam00c084f0 == 2) {
        if (sRam00c0853c != 0x3b) {
          (*pcRam00c00038)();
          goto LAB_00008970;
        }
        goto LAB_00008940;
      }
    }
    (*pcRam00c00038)();
  }
LAB_00008970:
  cRam003e2f5c = '\x01';
  func_0x003ddba0();
  if (cRam003e2f5c != '\0') goto LAB_000089b4;
  do {
    uRam00c0857c = 0x8888;
    uRam00c0853e = 0xffff;
    cRam003e2f5c = '\x01';
    sRam00c084ee = 0;
    uRam00c084fe = 0xffff;
LAB_000089b4:
    func_0x00389cf8();
  } while (cRam003e2f5c == '\0');
  func_0x003ddbd0();
  puVar1 = auStack64 + 7;
  uVar10 = (uint)puVar1 & 7;
  *(ulong *)(puVar1 + -uVar10) =
       *(ulong *)(puVar1 + -uVar10) & -1 << (uVar10 + 1) * 8 | uRam003f2490 >> (7 - uVar10) * 8;
  auStack64 = uRam003f2490;
  puVar1 = auStack56 + 7;
  uVar10 = (uint)puVar1 & 7;
  *(ulong *)(puVar1 + -uVar10) =
       *(ulong *)(puVar1 + -uVar10) & -1 << (uVar10 + 1) * 8 | uRam003f2498 >> (7 - uVar10) * 8;
  auStack56 = uRam003f2498;
  puVar1 = auStack48 + 7;
  uVar10 = (uint)puVar1 & 7;
  *(ulong *)(puVar1 + -uVar10) =
       *(ulong *)(puVar1 + -uVar10) & -1 << (uVar10 + 1) * 8 | uRam003f24a0 >> (7 - uVar10) * 8;
  auStack48 = uRam003f24a0;
  uVar10 = (int)&uStack40 + 7U & 7;
  puVar4 = (ulong *)(((int)&uStack40 + 7U) - uVar10);
  *puVar4 = *puVar4 & -1 << (uVar10 + 1) * 8 | uRam003f24a8 >> (7 - uVar10) * 8;
  uStack40 = uRam003f24a8;
  func_0x001485c8();
  func_0x001485c8();
  return;
}



void FUN_00008a40(uint param_1,ushort param_2)

{
  byte bVar1;
  int iVar2;
  int iVar3;
  uint uVar4;
  uint uVar5;
  int iVar6;
  uint uVar7;
  
  uVar7 = param_1 & 0xffff;
  iVar6 = uVar7 * 2;
  bVar1 = *(byte *)(undefined2 *)(iVar6 + 0xc00000);
  *(ushort *)(iVar6 + 0xc00004) = param_2 & 0xff;
  *(undefined2 *)(iVar6 + 0xc0001c) = 0xb;
  *(undefined2 *)(iVar6 + 0xc00006) = *(undefined2 *)(iVar6 + 0xc00000);
  if (uVar7 == 0x427e) {
    func_0x00389580();
  }
  else {
    func_0x00389580();
  }
  uVar4 = 1;
  uVar5 = 0;
  if (1 < *(ushort *)(iVar6 + 0xc00004)) {
    do {
      iVar3 = uVar5 * 2;
      if ((((uVar5 != bVar1) && (*(short *)(iVar3 + 0xc00130) == 3)) &&
          (iVar2 = (uint)bVar1 * 2, *(short *)(iVar3 + 0xc00286) == *(short *)(iVar2 + 0xc00286)))
         && (*(short *)(iVar2 + 0xc007de) == *(short *)(iVar3 + 0xc007de))) {
        iVar3 = (uVar7 + uVar4) * 2;
        *(undefined2 *)(iVar3 + 0xc0001c) = 0xb;
        *(short *)(iVar3 + 0xc00006) = (short)uVar5;
        if (uVar7 == 0x427e) {
          func_0x00389580();
        }
        else {
          func_0x00389580();
        }
        uVar4 = uVar4 + 1 & 0xff;
      }
      uVar5 = uVar5 + 1 & 0xff;
    } while ((uVar5 < 0xab) && (uVar4 < *(ushort *)(iVar6 + 0xc00004)));
  }
  *(short *)(iVar6 + 0xc00004) = (short)uVar4;
  iVar6 = (uVar7 + uVar4) * 2;
  *(undefined2 *)(iVar6 + 0xc0001c) = 3;
  *(undefined2 *)(iVar6 + 0xc00006) = 0xac;
  return;
}



// WARNING: Removing unreachable block (ram,0x00009330)
// WARNING: Removing unreachable block (ram,0x00009294)
// WARNING: Removing unreachable block (ram,0x000092c8)
// WARNING: Removing unreachable block (ram,0x0000937c)

void FUN_00008f78(uint param_1,uint param_2)

{
  ushort uVar1;
  long lVar2;
  short *psVar3;
  int iVar4;
  int iVar5;
  
  if (sRam00c0f200 == 0) {
    func_0x00187580(0xc0f260);
  }
  else {
    if (sRam00c0f200 == 1) {
      func_0x00187580(0xc0f240);
    }
    else {
      func_0x00187580(0xc0f240);
    }
  }
  uRam00c0f20c = (ushort)bRam003e2fd2;
  iVar5 = (param_1 & 0xff) * 0x3a;
  uRam00c0f240 = *(undefined2 *)(iVar5 + 0x3e2a38);
  uRam00c0f242 = *(undefined2 *)(iVar5 + 0x3e2a4a);
  sRam00c0f246 = *(short *)(iVar5 + 0x3e2a5e);
  uRam00c0f248 = *(undefined2 *)(iVar5 + 0x3e2a40);
  iVar4 = (param_2 & 0xff) * 0x3a;
  uRam00c0f24a = *(undefined2 *)(iVar5 + 0x3e2a40);
  uRam00c0f24c = *(undefined2 *)(iVar5 + 0x3e2a60);
  uRam00c0f24e = *(undefined2 *)(iVar5 + 0x3e2a66);
  uRam00c0f250 = *(undefined2 *)(iVar5 + 0x3e2a68);
  uRam00c0f252 = *(undefined2 *)(iVar5 + 0x3e2a6a);
  uRam00c0f254 = *(undefined2 *)(iVar5 + 0x3e2a6c);
  uRam00c0f256 = *(undefined2 *)(iVar5 + 0x3e2a6e);
  uRam00c0f258 = *(undefined2 *)(iVar5 + 0x3e2a70);
  uRam00c0f25a = *(undefined2 *)(iVar5 + 0x3e2a62);
  uRam00c0f260 = *(undefined2 *)(iVar4 + 0x3e2c80);
  uRam00c0f262 = *(undefined2 *)(iVar4 + 0x3e2c92);
  uRam00c0f266 = *(undefined2 *)(iVar4 + 0x3e2ca6);
  uRam00c0f268 = *(undefined2 *)(iVar4 + 0x3e2c88);
  uRam00c0f26a = *(undefined2 *)(iVar4 + 0x3e2c88);
  uRam00c0f26c = *(undefined2 *)(iVar4 + 0x3e2ca8);
  uRam00c0f26e = *(undefined2 *)(iVar4 + 0x3e2cae);
  uRam00c0f270 = *(undefined2 *)(iVar4 + 0x3e2cb0);
  uRam00c0f272 = *(undefined2 *)(iVar4 + 0x3e2cb2);
  uRam00c0f274 = *(undefined2 *)(iVar4 + 0x3e2cb4);
  uRam00c0f276 = *(undefined2 *)(iVar4 + 0x3e2cb6);
  uRam00c0f278 = *(undefined2 *)(iVar4 + 0x3e2cb8);
  uRam00c0f27a = *(undefined2 *)(iVar4 + 0x3e2caa);
  lVar2 = func_0x001251e8();
  if ((lVar2 != 0) || (lVar2 = func_0x00125258(), lVar2 != 0)) {
    uRam00c0f27a = 1;
  }
  if (sRam00c0857c != 0xff) {
    iVar4 = func_0x00397a48(0xc0857e,uRam00c08500,uRam00c084fe,0xff);
    if ((*(ushort *)(iVar4 * 2 + 0xc0857e) & 0x4000) == 0) {
      iVar4 = ((int)sRam00c0f246 + ((int)sRam00c0f246 / 10) * 2) * 0x10000 >> 0x10;
      if (0xff < iVar4) {
        iVar4 = 0xff;
      }
    }
    else {
      iVar4 = ((int)sRam00c0f246 - (int)sRam00c0f246 / 10) * 0x10000 >> 0x10;
      if (iVar4 < 0) {
        iVar4 = 0;
      }
    }
    sRam00c0f246 = (short)iVar4;
  }
  uVar1 = *(ushort *)((uint)uRam00c084fe * 2 + 0xc08518);
  if ((uVar1 & 1) == 0) {
    if ((uVar1 & 2) != 0) {
      iVar4 = ((int)sRam00c0f246 + ((int)sRam00c0f246 / 10) * 2) * 0x10000 >> 0x10;
      if (0xff < iVar4) {
        iVar4 = 0xff;
      }
      sRam00c0f246 = (short)iVar4;
      psVar3 = (short *)((uint)uRam00c084fe * 2 + 0xc0329e);
      *psVar3 = *psVar3 + 3;
    }
  }
  else {
    iVar4 = ((int)sRam00c0f246 - (int)sRam00c0f246 / 10) * 0x10000 >> 0x10;
    if (iVar4 < 0) {
      iVar4 = 0;
    }
    sRam00c0f246 = (short)iVar4;
  }
  return;
}



// WARNING: Removing unreachable block (ram,0x0000a878)
// WARNING: Removing unreachable block (ram,0x0000a948)

void FUN_000093f8(void)

{
  bool bVar1;
  uint uVar2;
  undefined4 uVar3;
  ushort uVar4;
  uint uVar5;
  short *psVar6;
  ushort *puVar7;
  long lVar8;
  undefined2 *puVar9;
  undefined *puVar10;
  uint uVar11;
  short sVar12;
  undefined4 *puVar13;
  char cVar14;
  int iVar15;
  undefined auStack96 [8];
  undefined4 local_58;
  
  puVar13 = (undefined4 *)0x3e2ef8;
  iVar15 = 0;
  uVar3 = uRam003e2ef8;
  while( true ) {
    puVar13 = puVar13 + 1;
    func_0x00396e38(uVar3,2,0,8,0x7fff,0);
    iVar15 = (iVar15 + 1) * 0x10000 >> 0x10;
    if (0xb < iVar15) break;
    uVar3 = *puVar13;
  }
  if (uRam00c08500 < 5) {
    iVar15 = (int)((uRam00c08500 + 0xffff) * 0x10000) >> 0x10;
  }
  else {
    iVar15 = 4;
  }
  func_0x00396e38(0x3e3008,0,0,iVar15,0x7fff,0);
  if (uRam00c08540 < 5) {
    iVar15 = (int)((uRam00c08540 + 0xffff) * 0x10000) >> 0x10;
  }
  else {
    iVar15 = 4;
  }
  func_0x00396e38(0x3e3018,0,0,iVar15,0x7fff,0);
  func_0x00396e38(0x3e3028,3,0,2,0x7fff);
  func_0x00187580(0x3ed3d0,1,0x20);
  func_0x00397db0(0x3e3088,1,0x18,300,0x18,0xb0,0x18,300);
  func_0x00397db0(0x3e30a0,1,0xffffffffffffff88,0x70,8,0x70,0xffffffffffffff88,0x70);
  func_0x00397db0(0x3e30b8,1,0xf8,0xffffffffffffff80,0xf8,0x10,0xf8,0xffffffffffffff80);
  func_0x00397db0(0x3e30d0,1,0x168,0x68,0xd8,0x68,0x168,0x68);
  uRam003e2fd2 = 0;
  uRam003e2f59 = 0;
  cRam003e2f5e = '\0';
  if ((sRam00c084ee == 0) || (cRam003e2f5c == '\0')) {
    iVar15 = 0;
    bRam003e3088 = bRam003e3088 & 0xfe;
    bRam003e30b8 = bRam003e30b8 & 0xfe;
    if (uRam00c08540 != 0) {
      puVar9 = (undefined2 *)0x3e2caa;
      do {
        *puVar9 = uRam00c0b9c0;
        iVar15 = (iVar15 + 1) * 0x10000 >> 0x10;
        puVar9 = puVar9 + 0x1d;
      } while (iVar15 < (int)(uint)uRam00c08540);
    }
    lVar8 = (*pcRam00c000bc)();
    if ((lVar8 != 0) && ((uRam002afcb0 & 0x10) != 0)) {
      func_0x0038d7e8();
    }
    lVar8 = func_0x001251e8();
    if ((lVar8 == 0) && (lVar8 = func_0x00125258(), lVar8 == 0)) {
      uRam003e2f5a = 1;
    }
    else {
      uRam003e2f5a = 0;
    }
    cRam003e31b0 = -1;
    iVar15 = 0;
    cRam003e2f5b = '\0';
    func_0x00147f88(0,0x104,0xffffffffffffff01,0xffffffffffffff01,0xffffffffffffff01);
    if (uRam00c08500 != 0) {
      puVar10 = (undefined *)0x3e2a40;
      do {
        *(undefined *)(iVar15 + 0x3e30e8) = *puVar10;
        iVar15 = (iVar15 + 1) * 0x10000 >> 0x10;
        puVar10 = puVar10 + 0x3a;
      } while (iVar15 < (int)(uint)uRam00c08500);
    }
    iVar15 = 0;
    if (uRam00c08540 != 0) {
      puVar10 = (undefined *)0x3e2c88;
      do {
        *(undefined *)(iVar15 + 0x3e30f8) = *puVar10;
        iVar15 = (iVar15 + 1) * 0x10000 >> 0x10;
        puVar10 = puVar10 + 0x3a;
      } while (iVar15 < (int)(uint)uRam00c08540);
    }
    bRam003ed3be = 0;
    if (cRam003e2f5b == '\x01') {
      uRam003ed3c8 = 2;
    }
    else {
      uRam003ed3c8 = 0;
    }
    bRam003ecf72 = 5;
    func_0x003b7a90();
    if (cRam003ecf4a == '\x01') {
      (*pcRam00c0002c)();
      return;
    }
    func_0x003979d8(1,0x104,0x10);
    if (cRam003e2a30 == '\0') {
      (*pcRam00c00038)(0x1e);
    }
    else {
      if (cRam003e2a30 == '\x01') {
        (*pcRam00c00038)(0x1f);
      }
    }
  }
  else {
    uRam003e3098 = uRam003e3094;
    uRam003ed3c8 = 9;
    bRam003e3088 = 0x2d;
    bRam003ecf72 = 5;
    bRam003ed3be = 0;
    func_0x003979d8(1,0x104,0x10);
  }
  if (cRam003e2f5e != '\x06') {
    (*pcRam00c00044)(0,6);
  }
  func_0x00382ab8(7,0);
  do {
    if (bRam003ecf72 == 0) {
      if (sRam00c0f100 == 0) {
        puVar7 = (ushort *)((uint)uRam00c084fe * 2 + 0xc0329e);
        uVar4 = *puVar7;
        *puVar7 = uVar4 + (uVar4 < 0xff);
      }
      else {
        if (cRam003e31b0 == '\0') {
          func_0x003915a0();
        }
      }
      return;
    }
    func_0x003839a8();
    func_0x00384000(0,0x13f,0xef);
    func_0x003840f0(0,0,0);
    func_0x00384078(0,0,0,0x13f,0xef);
    func_0x003978c0(0,0,0,0x13f,0xef,0xffffffffffffffff,0,0);
    if (*(char *)(bRam003ed3be + 0x3ed3c8) != '\x05') {
      func_0x0038bda8();
    }
    bRam003ed3bf = *(byte *)(bRam003ed3be + 0x3ed3c8);
    (**(code **)((uint)bRam003ed3bf * 4 + 0x3e2ec8))();
    if (bRam003ecf72 == 2) {
      if (sRam003ed41c < sRam003ed418) {
        bRam003ecf72 = (cRam003e2fd1 != '\x01') << 2;
        func_0x00147f88(0,0x104);
      }
      else {
LAB_00009b80:
        func_0x001481b0();
        sRam003ed41c = sRam003ed41c - sRam003ed418;
      }
    }
    else {
      if (bRam003ecf72 < 3) {
        if (bRam003ecf72 == 1) {
          if (*(char *)(*(int *)((uint)bRam003ed3bf * 4 + 0x3e2ef8) + 10) == -1) {
            bRam003ecf72 = (cRam003e2fd1 != '\x01') << 2;
          }
          else {
            if (sRam003ed418 <= sRam003ed41c) goto LAB_00009b80;
            func_0x00147f88(0,uRam003ed420);
          }
        }
      }
      else {
        if (bRam003ecf72 == 3) {
          if (*(char *)(*(int *)((uint)bRam003ed3bf * 4 + 0x3e2ef8) + 10) == -1) {
            bRam003ecf72 = 4;
          }
        }
        else {
          if (bRam003ecf72 == 5) {
            if (sRam003ed41c < sRam003ed418) {
              bRam003ecf72 = 4;
              func_0x00147f88(0,0x104);
            }
            else {
              func_0x001481b0();
              sRam003ed41c = sRam003ed41c - sRam003ed418;
            }
          }
        }
      }
    }
    iVar15 = *(int *)((uint)bRam003ed3bf * 4 + 0x3e2ef8);
    sVar12 = *(short *)(iVar15 + 4);
    if (sVar12 == 0x3fff) {
      if (*(char *)(iVar15 + 10) == '\0') {
        *(undefined *)(iVar15 + 10) = 0x80;
        bRam003ecf72 = 3;
      }
      if (bRam003ecf72 == 4) {
        func_0x00383000(7);
        *(undefined *)(*(int *)((uint)bRam003ed3bf * 4 + 0x3e2ef8) + 10) = 0;
        bVar1 = bRam003ed3be == 0;
        *(undefined2 *)(*(int *)((uint)bRam003ed3bf * 4 + 0x3e2ef8) + 4) = 0x7fff;
        bRam003ed3be = bRam003ed3be - 1;
        if (bVar1) {
          bRam003ed3be = 0;
        }
        bRam003ed3bf = *(byte *)(bRam003ed3be + 0x3ed3c8);
        *(undefined2 *)(*(int *)((uint)bRam003ed3bf * 4 + 0x3e2ef8) + 4) = 0x7fff;
      }
    }
    else {
      if (sVar12 != 0x7fff) {
        if (sVar12 < 4) {
          cVar14 = *(char *)((int)sVar12 + (uint)bRam003ed3bf * 4 + 0x3e2f28);
        }
        else {
          cVar14 = -1;
        }
        if (cVar14 < '\x01') {
          if (cVar14 == '\0') {
            iVar15 = *(int *)((uint)bRam003ed3bf * 4 + 0x3e2ef8);
            *(undefined2 *)(iVar15 + 4) = 0x7fff;
            *(undefined *)(iVar15 + 10) = 0;
          }
        }
        else {
          iVar15 = *(int *)((uint)bRam003ed3bf * 4 + 0x3e2ef8);
          if ((*(char *)(iVar15 + 10) == '\0') &&
             (*(undefined *)(iVar15 + 10) = 0x80, bRam003ecf72 == 4)) {
            bRam003ecf72 = 3;
          }
          if (bRam003ecf72 == 4) {
            func_0x00383000(7);
            bVar1 = 6 < bRam003ed3be;
            *(byte *)(bRam003ed3be + 0x3ed3c8) = bRam003ed3bf;
            bRam003ed3be = bRam003ed3be + 1;
            if (bVar1) {
              bRam003ed3be = 7;
            }
            iRam003ecf54 = 0;
            iVar15 = *(int *)((uint)bRam003ed3bf * 4 + 0x3e2ef8);
            *(char *)(bRam003ed3be + 0x3ed3c8) = cVar14;
            *(undefined *)(iVar15 + 10) = 0;
          }
        }
      }
    }
    lVar8 = (*pcRam00c000bc)();
    if (lVar8 != 0) {
      func_0x00382ad8(0xa0,8);
      func_0x00382b60(6);
      if (uRam002afcb0 == 0x888) {
        func_0x00380be0(0x200130);
        uRam002afcb2 = 0;
        uRam002afcb0 = 0;
        uVar4 = 0;
      }
      else {
        uVar4 = uRam002afcb0 & 8;
      }
      if (uVar4 != 0) {
        puVar10 = auStack96 + 7;
        uVar11 = (uint)puVar10 & 7;
        *(ulong *)(puVar10 + -uVar11) =
             *(ulong *)(puVar10 + -uVar11) & -1 << (uVar11 + 1) * 8 |
             uRam003f2850 >> (7 - uVar11) * 8;
        auStack96 = uRam003f2850;
        local_58 = uRam003f2858;
        func_0x003978c0(6,0x10,0x10,0x104,0x3c,0xffffffffffffffff,0,1);
        func_0x00382ab8(7,0);
        func_0x00382ad8(0x10,0x10);
        func_0x00382b60(6,0,0x3f2860,uRam003e3010,uRam003e3020);
        func_0x00382ad8(0x10,0x1a);
        func_0x00382b60(6,0,0x3f2878,uRam003e2f58,uRam003e300c,uRam003e301c);
        func_0x00382ad8(0x10,0x24);
        func_0x00382b60(6,0,0x3f2890,uRam003e2f5a,uRam003e2f59,cRam003e31b0);
        func_0x00382ad8(0x10,0x2e);
        func_0x00382b60(6,0,0x3f28b0,uRam003edca8,uRam003edcac);
        func_0x00382ad8(0x10,0x38);
        func_0x00382b60(6,0,0x3f28d0,uRam003ed47c,uRam003ed480);
        func_0x00382ad8(0x10,0x42);
        func_0x00382b60(6,0,0x3f28f0,cRam003e2f5c,cRam003e2f5b);
        func_0x003978c0(6,0x10,0xae,0xa0,0x30,0xffffffffffffffff,0,1);
        iVar15 = *(int *)((uint)bRam003ed3bf * 4 + 0x3e2ef8);
        func_0x003812f8(6,0x10,0xae,0x3f2900,iRam003ecf54,*(undefined2 *)(iVar15 + 6),
                        *(undefined2 *)(iVar15 + 4));
        return;
      }
      if ((uRam002afcb0 & 0x80) != 0) {
        func_0x003978c0(6,0x10,0x50,0x104,0x20,0xffffffffffffffff,0,1);
        func_0x00382ab8(7,0);
        func_0x00382ad8(0x10,0x50);
        func_0x00382b60(6,0,0x3f2960,
                        *(undefined2 *)((sRam003e3008 * 0x1c + (int)sRam003e3008) * 2 + 0x3e2a3a));
        func_0x00382ad8(0x10,0x60);
        func_0x00382b60(6);
      }
      if ((uRam002afcb4 & 0x800) != 0) {
        uVar11 = 0;
        if ((uRam00c08500 != 0) && (bRam003e2f5f != uRam003e2a38)) {
          uVar5 = 1;
          do {
            uVar11 = uVar5 & 0xffff;
            if (uRam00c08500 <= uVar11) break;
            uVar5 = uVar11 + 1;
          } while ((ushort)bRam003e2f5f != *(ushort *)(uVar11 * 0x3a + 0x3e2a38));
        }
        if (6 < iRam003ecf54) {
          if (((uRam002afcb4 & 0x40) != 0) &&
             (bVar1 = 3 < bRam003e2f60, bRam003e2f60 = bRam003e2f60 + 1, bVar1)) {
            bRam003e2f60 = 0;
          }
          if (((uRam002afcb4 & 0x20) != 0) &&
             (bVar1 = bRam003e2f5f == 0, bRam003e2f5f = bRam003e2f5f - 1, bVar1)) {
            bRam003e2f5f = 0xaa;
          }
          if (((uRam002afcb4 & 0x10) != 0) &&
             (bVar1 = 0xaa < bRam003e2f5f, bRam003e2f5f = bRam003e2f5f + 1, bVar1)) {
            bRam003e2f5f = 0;
          }
          if (bRam003e2f60 < 4) {
            sVar12 = 1;
            if ((uRam002afcb4 & 8) != 0) {
              sVar12 = 10;
            }
            if ((uRam002afcb4 & 0x80) != 0) {
              sVar12 = sVar12 * 1000;
            }
            if ((uRam002afcb4 & 0x400) != 0) {
              psVar6 = (short *)(((uint)*(ushort *)((uint)bRam003e2f60 * 2 + 0x3e2f68) +
                                 (uint)bRam003e2f5f) * 2 + 0xc00000);
              *psVar6 = sVar12 + *psVar6;
            }
            if ((uRam002afcb4 & 0x100) != 0) {
              psVar6 = (short *)(((uint)*(ushort *)((uint)bRam003e2f60 * 2 + 0x3e2f68) +
                                 (uint)bRam003e2f5f) * 2 + 0xc00000);
              *psVar6 = *psVar6 - sVar12;
            }
          }
          else {
            if ((uRam002afcb4 & 0x400) != 0) {
              if (uVar11 < uRam00c08500) {
                iVar15 = uVar11 * 0x3a;
                *(undefined2 *)(iVar15 + 0x3e2a4c) = 0xa0a;
                *(undefined2 *)(iVar15 + 0x3e2a4e) = 0xa0a;
                *(undefined2 *)(iVar15 + 0x3e2a50) = 0xa0a;
                *(undefined2 *)(iVar15 + 0x3e2a52) = 0xa0a;
                *(undefined2 *)(iVar15 + 0x3e2a54) = 0xa0a;
                *(undefined2 *)(iVar15 + 0x3e2a40) = 100;
              }
              else {
                uVar11 = 0;
                if (uRam00c08540 != 0) {
                  if (bRam003e2f5f != uRam003e2c80) {
                    uVar5 = 1;
                    do {
                      uVar11 = uVar5 & 0xffff;
                      if (uRam00c08540 <= uVar11) goto LAB_0000a63c;
                      uVar5 = uVar11 + 1;
                    } while ((ushort)bRam003e2f5f != *(ushort *)(uVar11 * 0x3a + 0x3e2c80));
                  }
                  if (uVar11 < uRam00c08540) {
                    iVar15 = uVar11 * 0x3a;
                    *(undefined2 *)(iVar15 + 0x3e2c94) = 0x101;
                    *(undefined2 *)(iVar15 + 0x3e2c96) = 0x101;
                    *(undefined2 *)(iVar15 + 0x3e2c98) = 0x101;
                    *(undefined2 *)(iVar15 + 0x3e2c9a) = 0x101;
                    *(undefined2 *)(iVar15 + 0x3e2c9c) = 0x101;
                    *(undefined2 *)(iVar15 + 0x3e2c88) = 0;
                  }
                }
              }
            }
          }
LAB_0000a63c:
          iRam003ecf54 = 0;
        }
        func_0x003978c0(6,0x10,0x50,200,0x50,0xffffffffffffffff,0,0);
        func_0x00384168(6,0x10,0x50,0xd8,0x77,0xffff,0);
        func_0x00382ad8(0x10,0x50);
        func_0x00382b60(6,0,0x3f2980,bRam003e2f5f);
        if (bRam003e2f60 == 0) {
          func_0x00382ab8(7,0);
        }
        else {
          func_0x00382ab8(9,0);
        }
        func_0x00382ad8(100,0x50);
        func_0x00382b60(6,0,0x3f2990,*(undefined2 *)((uint)bRam003e2f5f * 2 + 0xc0128e));
        if (bRam003e2f60 == 1) {
          func_0x00382ab8(7,0);
        }
        else {
          func_0x00382ab8(9,0);
        }
        func_0x00382ad8(100,0x5a);
        func_0x00382b60(6,0,0x3f29a0,*(undefined2 *)((uint)bRam003e2f5f * 2 + 0xc01a92));
        if (bRam003e2f60 == 2) {
          func_0x00382ab8(7,0);
        }
        else {
          func_0x00382ab8(9,0);
        }
        func_0x00382ad8(100,100);
        func_0x00382b60(6,0,0x3f29b0,*(undefined2 *)((uint)bRam003e2f5f * 2 + 0xc01be8));
        if (bRam003e2f60 == 3) {
          func_0x00382ab8(7,0);
        }
        else {
          func_0x00382ab8(9,0);
        }
        func_0x00382ad8(100,0x6e);
        uVar11 = 0;
        func_0x00382b60(6,0,0x3f29c0,*(undefined2 *)((uint)bRam003e2f5f * 2 + 0xc02bf0));
        func_0x00382ab8(7,0);
        func_0x00384168(6,0x10,0x78,0xd8,0x8b,0x893c,0);
        if (uRam00c08500 != 0) {
          uVar5 = 0;
          uVar2 = 0;
          while( true ) {
            func_0x00382ad8((int)(uVar2 * 0x1c0000 + 0x100000) >> 0x10,
                            (int)(uVar5 * 0xa0000 + 0x780000) >> 0x10);
            func_0x00382b60();
            uVar11 = uVar11 + 1 & 0xffff;
            if (uRam00c08500 <= uVar11) break;
            uVar5 = uVar11 / 5;
            uVar2 = uVar11 % 5;
          }
        }
        func_0x00382ab8(2,0);
        func_0x00384168(6,0x10,0x8c,0xd8,0x9f,0x801f,0);
        uVar11 = 0;
        if (uRam00c08540 != 0) {
          uVar5 = 0;
          uVar2 = 0;
          while( true ) {
            func_0x00382ad8((int)(uVar2 * 0x1c0000 + 0x100000) >> 0x10,
                            (int)(uVar5 * 0xa0000 + 0x8c0000) >> 0x10);
            func_0x00382b60();
            uVar11 = uVar11 + 1 & 0xffff;
            if (uRam00c08540 <= uVar11) break;
            uVar5 = uVar11 / 5;
            uVar2 = uVar11 % 5;
          }
        }
        func_0x00382ab8(7,0);
      }
    }
    func_0x00382a18(2);
    func_0x00383c00();
    uRam003ecf64 = 1;
    (*pcRam00c00024)();
    func_0x001485c8();
    if (*(char *)(bRam003ed3bf + 0x3ed3d0) != '\0') {
      uRam003ecf68 = 1;
    }
    lVar8 = (*pcRam00c0005c)(uRam002afcb0);
    if (lVar8 == 1) {
      cRam003ecf4a = '\x01';
      bRam003ecf72 = 0;
      (*pcRam00c0002c)();
    }
  } while( true );
}



void FUN_0000aaf8(void)

{
  undefined *puVar1;
  uint uVar2;
  long lVar3;
  undefined auStack96 [8];
  undefined4 local_58;
  undefined auStack80 [4];
  undefined2 local_4c;
  undefined auStack64 [4];
  undefined2 local_3c;
  
  puVar1 = auStack80 + 3;
  uVar2 = (uint)puVar1 & 3;
  *(uint *)(puVar1 + -uVar2) =
       *(uint *)(puVar1 + -uVar2) & -1 << (uVar2 + 1) * 8 | uRam003f2a00 >> (3 - uVar2) * 8;
  auStack80 = uRam003f2a00;
  local_4c = uRam003f2a04;
  puVar1 = auStack96 + 7;
  uVar2 = (uint)puVar1 & 7;
  *(ulong *)(puVar1 + -uVar2) =
       *(ulong *)(puVar1 + -uVar2) & -1 << (uVar2 + 1) * 8 | uRam003f29f0 >> (7 - uVar2) * 8;
  auStack96 = uRam003f29f0;
  local_58 = uRam003f29f8;
  puVar1 = auStack64 + 3;
  uVar2 = (uint)puVar1 & 3;
  *(uint *)(puVar1 + -uVar2) =
       *(uint *)(puVar1 + -uVar2) & -1 << (uVar2 + 1) * 8 | uRam003f2a08 >> (3 - uVar2) * 8;
  auStack64 = uRam003f2a08;
  local_3c = uRam003f2a0c;
  lVar3 = (*pcRam00c000bc)();
  if (lVar3 != 0) {
    func_0x00382ad8(0x10,0x20);
    func_0x00382b60(1,0,0x3f2a10,bRam003e2f70);
    func_0x00382ad8(0x10,0x30);
    func_0x00382b60(1,0,0x3f2a18,uRam003e2fe0,uRam003e2fde,uRam003e2fdc);
    func_0x00382ad8(0x10,0x40);
    func_0x00382b60(1,0,0x3f2a28,uRam003ed47c,uRam003ed480);
  }
  if (4 < bRam003e2f70) {
    if (bRam003e2f70 < 4) {
      func_0x003bbc08(uRam00400ec0);
    }
    return;
  }
                    // WARNING: Could not emulate address calculation at 0x0000ac40
                    // WARNING: Treating indirect jump as call
  (**(code **)((uint)bRam003e2f70 * 4 + 0x3f2a30))();
  return;
}



void FUN_0000b080(void)

{
  undefined *puVar1;
  uint uVar2;
  long lVar3;
  undefined auStack96 [16];
  undefined auStack80 [16];
  
  puVar1 = auStack96 + 3;
  uVar2 = (uint)puVar1 & 3;
  *(uint *)(puVar1 + -uVar2) =
       *(uint *)(puVar1 + -uVar2) & -1 << (uVar2 + 1) * 8 | uRam003f2a58 >> (3 - uVar2) * 8;
  auStack96._0_4_ = uRam003f2a58;
  puVar1 = auStack80 + 3;
  uVar2 = (uint)puVar1 & 3;
  *(uint *)(puVar1 + -uVar2) =
       *(uint *)(puVar1 + -uVar2) & -1 << (uVar2 + 1) * 8 | uRam003f2a60 >> (3 - uVar2) * 8;
  auStack80._0_4_ = uRam003f2a60;
  if (bRam003e2f72 != 1) {
    if (1 < bRam003e2f72) {
      if (bRam003e2f72 != 2) {
        return;
      }
      goto LAB_0000b270;
    }
    if (bRam003e2f72 != 0) {
      return;
    }
    func_0x00396e38(0x3e3028,3,0,2,0x7fff,0);
    func_0x00396a98(0x53,0x107ea00,uRam00c00112);
    func_0x003bba50(0,0,0xfffffffffffffed4,0xa0,400,0xa0);
    uRam00400ec2 = func_0x00396fb8(0x53,6,1,(undefined)uRam00c00112);
    uRam003e3096 = uRam003e308e;
    bRam003ed47c = bRam003ed47c & 0xfd;
    bRam003e3088 = bRam003e3088 & 0xf1 | 4;
    uRam003e3098 = uRam003e3090;
    bRam003e2f72 = 2;
    *(undefined *)(bRam003ed3bf + 0x3ed3d0) = 1;
  }
  if ((bRam003e2f72 == 1) && ((bRam003ed480 & 8) != 0)) {
    if ((bRam003e2f59 & 1) == 0) {
      bRam003e3088 = bRam003e3088 & 0xf1 | 2;
      uRam003e3096 = uRam003e308a;
      uRam003e3098 = uRam003e308c;
    }
    uRam003e3042 = 0xff;
    bRam003e2f72 = 0;
  }
LAB_0000b270:
  func_0x003bbc08(uRam00400ec2);
  func_0x00383da0(0,0,0x2c88,0x1b80,0x4fc8,0xe38,0x68,0x60);
  if (bRam003ed47d == uRam003ed47e) {
    func_0x00382ab8(8,0);
    func_0x00382ad8(0x9a,0x6d);
    func_0x00382b60(1,1,0x3f2a50);
    func_0x00382ad8(0x9a,0x7d);
    func_0x00382b60(1,1);
    func_0x00382ab8(7);
    if (bRam003e2f72 == 2) {
      bRam003e3030 = bRam003e3030 | 1;
    }
    else {
      bRam003e3030 = bRam003e3030 & 0xfe;
    }
    func_0x003b9a08(2);
    if (bRam003e2f72 == 2) {
      if (sRam003e302c == 0) {
        bRam003ed480 = bRam003ed480 & 0xf1 | 4;
        bRam003e2f59 = bRam003e2f59 | 1;
        bRam003e2f72 = 1;
        uRam003ed494 = uRam003ed48c;
        uRam003ed492 = uRam003ed48a;
        uRam003e303c = 0;
        lVar3 = func_0x001251e8();
        if (lVar3 == 0) {
          lVar3 = func_0x00125258();
          if (lVar3 != 0) {
            func_0x00125128();
            func_0x00124ff0();
          }
        }
        else {
          func_0x00125088();
          func_0x00124f58();
        }
        uRam00c0f102 = 1;
        uRam00c0f100 = 1;
      }
      else {
        if (sRam003e302c != 0x7fff) {
          bRam003ed480 = bRam003ed480 & 0xf1 | 4;
          uRam003e303c = 0x3fff;
          bRam003e2f72 = 1;
          uRam003ed494 = uRam003ed48c;
          uRam003ed492 = uRam003ed48a;
        }
      }
    }
  }
  return;
}



void FUN_0000b4a8(void)

{
  ushort uVar1;
  char cVar2;
  int iVar3;
  long lVar4;
  ushort *puVar5;
  undefined8 uVar6;
  byte bVar7;
  int iVar8;
  
  if (cRam003ed3bf != '\b') {
    func_0x00383da0(0,0,0x2c88,0x1b80,0x4f94,0xd08,0,0x60);
    func_0x00383da0(0,0,0x2c88,0x1b80,0x4fc8,0xe38,0x68,0x60);
    func_0x00383da0(0,0,0x2c88,0x1b80,0x5150,0xd08,0xd8,0x90);
  }
  func_0x00383da0(0,0,0x2c88,0x1b7c,0x4ee0,0x928,0x7c,0x68);
  func_0x0038caa8();
  if (((bRam003e3020 & 2) != 0) && (cRam003e2f58 != '\0')) {
    func_0x00383ea0();
  }
  func_0x00397e80();
  lVar4 = func_0x001251e8();
  if ((lVar4 == 0) && (lVar4 = func_0x00125258(), lVar4 == 0)) {
    uRam00400ec4 = *(ushort *)((uint)uRam00c0853c * 2 + 0xc00286);
    if (uRam00400ec4 < 8) {
      puVar5 = (ushort *)((uint)uRam00400ec4 * 4 + 0x3f2762);
      iVar3 = (*puVar5 & 0xf00) << 0x13;
      iVar8 = 0x28 - (iVar3 >> 0x19);
      func_0x00383da0(0,0,0xc88,0x1b84,*(ushort *)((uint)uRam00400ec4 * 4 + 0x3f2760) + 0x24f00 >> 3
                      ,*puVar5,(uint)uRam003e30de + iVar8 & 0xffff,uRam003e30e0);
    }
    else {
      uRam00400ec4 = uRam00c0853c;
      puVar5 = (ushort *)((uint)uRam00c0853c * 4 + 0x3f24b2);
      iVar3 = (*puVar5 & 0xf00) << 0x13;
      iVar8 = 0x28 - (iVar3 >> 0x19);
      func_0x00383da0(0,0,0xc88,0x1b84,*(ushort *)((uint)uRam00c0853c * 4 + 0x3f24b0) + 0x15080 >> 3
                      ,*puVar5,(uint)uRam003e30de + iVar8 & 0xffff,uRam003e30e0);
    }
    func_0x00383da0(0,0,0xc88,0x1b84,0x4e40,0x210,
                    (uint)uRam003e30de + iVar8 + (iVar3 >> 0x18) & 0xffff,uRam003e30e0);
  }
  uRam00400ec4 = uRam00c0853c;
  lVar4 = func_0x001251e8();
  if (lVar4 == 0) {
    lVar4 = func_0x00125258();
    if (lVar4 != 0) {
      uRam00400ec4 = 0x74;
      func_0x00125400();
    }
  }
  else {
    uRam00400ec4 = 0x74;
    func_0x00125348();
  }
  puVar5 = (ushort *)((uint)uRam00400ec4 * 4 + 0x3f24b2);
  func_0x00383da0(0,0,0xc88,0x1b84,*(ushort *)((uint)uRam00400ec4 * 4 + 0x3f24b0) + 0x15080 >> 3,
                  *puVar5,((uint)uRam003e30de +
                          ((int)((0x50 - ((*puVar5 & 0xf00) >> 5)) * 0x1000000) >> 0x18)) - 0x10 &
                          0xffff,uRam003e30e0 + 0x10);
  func_0x00383da0(0,0,0xc88,0x1b84,0x4e50,0x410,uRam003e30de + 0x40,uRam003e30e0 + 0x10);
  func_0x003978c0(1,(int)((uRam003e30de + 8) * 0x10000) >> 0x10,
                  (int)((uRam003e30e0 + 0x20) * 0x10000) >> 0x10,0x57,0xf,0xffffffffffffffff,1,0);
  sRam003ed2e4 = uRam003e30de + 8;
  sRam003ed2e6 = uRam003e30e0 + 0x20;
  func_0x00383a90(1,0x3ed2d8);
  func_0x00382ad8((int)((uRam003e30de + 0x3c) * 0x10000) >> 0x10,
                  (int)((uRam003e30e0 + 0x24) * 0x10000) >> 0x10);
  if (sRam00c084f0 == 0) {
    cVar2 = *(char *)((int)*(short *)((sRam003e3018 * 0x1c + (int)sRam003e3018) * 2 + 0x3e2c86) +
                      (uint)uRam00c084f2 * 0xc + (uint)uRam00c084f2 + 0x3e9c28);
    uVar6 = 0x2d;
    if (-1 < cVar2) {
      uVar6 = 0x2b;
    }
    if (cVar2 < '\0') {
      cVar2 = -cVar2;
    }
    func_0x00382b60(1,0,0x3f2a68,uVar6,cVar2);
  }
  else {
    uVar1 = uRam00c084fc;
    if (sRam00c084f0 == 1) {
      uVar1 = uRam00c0853c;
    }
    if (sRam00c084f0 == 1) {
      iVar3 = func_0x003d0f90(*(ushort *)((uint)uVar1 * 2 + 0xc007de) & 0x7fff);
      bVar7 = *(byte *)(iVar3 + 0x3e8940);
    }
    else {
      bVar7 = 0;
    }
    func_0x00382b60(1,0,0x3f2a68,0x2b,bVar7 & 0x7f);
  }
  func_0x00397e80(0x3e30d0,5);
  func_0x0038caa8(0,0x3e3008,uRam003e3096);
  if (((bRam003e3010 & 2) != 0) && (cRam003e2f58 != '\x01')) {
    func_0x00383ea0(0,0x500,0xc88,0x1b8c,0x1bc0,0x528,uRam003e3096,uRam003e3098);
  }
  func_0x00397e80();
  uRam00400ec4 = *(ushort *)((uint)uRam00c084fc * 2 + 0xc00286);
  if (uRam00400ec4 < 8) {
    puVar5 = (ushort *)((uint)uRam00400ec4 * 4 + 0x3f2762);
    func_0x00383da0(0,0,0xc88,0x1b84,*(ushort *)((uint)uRam00400ec4 * 4 + 0x3f2760) + 0x24f00 >> 3,
                    *puVar5,(uint)uRam003e30ae + (0x28 - ((*puVar5 & 0xf00) >> 6)) & 0xffff,
                    uRam003e30b0);
  }
  else {
    uRam00400ec4 = uRam00c084fc;
    puVar5 = (ushort *)((uint)uRam00c084fc * 4 + 0x3f24b2);
    func_0x00383da0(0,0,0xc88,0x1b84,*(ushort *)((uint)uRam00c084fc * 4 + 0x3f24b0) + 0x15080 >> 3,
                    *puVar5,(uint)uRam003e30ae + (0x28 - ((*puVar5 & 0xf00) >> 6)) & 0xffff,
                    uRam003e30b0);
  }
  func_0x00383da0(0,0,0xc88,0x1b84,0x4e40,0x210,uRam003e30ae + 0x50,uRam003e30b0);
  uRam00400ec4 = uRam00c084fc;
  puVar5 = (ushort *)((uint)uRam00c084fc * 4 + 0x3f24b2);
  func_0x00383da0(0,0,0xc88,0x1b84,*(ushort *)((uint)uRam00c084fc * 4 + 0x3f24b0) + 0x15080 >> 3,
                  *puVar5,(uint)uRam003e30ae +
                          ((int)((0x50 - ((*puVar5 & 0xf00) >> 5)) * 0x1000000) >> 0x18) & 0xffff,
                  uRam003e30b0 + 0x10);
  func_0x00383da0(0,0,0xc88,0x1b84,0x4e50,0x410,uRam003e30ae + 0x50,uRam003e30b0 + 0x10);
  func_0x003978c0(1,(int)((uRam003e30ae + 8) * 0x10000) >> 0x10,
                  (int)((uRam003e30b0 - 0x10) * 0x10000) >> 0x10,0x57,0xf,0xffffffffffffffff,1,0);
  sRam003ed2e4 = uRam003e30ae + 8;
  sRam003ed2e6 = uRam003e30b0 - 0x10;
  func_0x00383a90(1,0x3ed2d8);
  func_0x00382ad8((int)((uRam003e30ae + 0x3c) * 0x10000) >> 0x10,
                  (int)((uRam003e30b0 - 0xc) * 0x10000) >> 0x10);
  if (sRam00c084f0 == 0) {
    cVar2 = *(char *)((int)*(short *)((sRam003e3008 * 0x1c + (int)sRam003e3008) * 2 + 0x3e2a3e) +
                      (uint)uRam00c084f2 * 0xc + (uint)uRam00c084f2 + 0x3e9c28);
    uVar6 = 0x2d;
    if (-1 < cVar2) {
      uVar6 = 0x2b;
    }
    if (cVar2 < '\0') {
      cVar2 = -cVar2;
    }
    func_0x00382b60(1,0,0x3f2a68,uVar6,cVar2);
  }
  else {
    uVar1 = uRam00c084fc;
    if (sRam00c084f0 == 1) {
      uVar1 = uRam00c0853c;
    }
    if (sRam00c084f0 == 1) {
      bVar7 = 0;
    }
    else {
      iVar3 = func_0x003d0f90(*(ushort *)((uint)uVar1 * 2 + 0xc007de) & 0x7fff);
      bVar7 = *(byte *)(iVar3 + 0x3e8940);
    }
    func_0x00382b60(1,0,0x3f2a68,0x2b,bVar7 & 0x7f);
  }
  func_0x00397e80(0x3e30a0);
  if (cRam003e2f58 == -1) {
    if (sRam00c084fe != -1) {
      bRam003e3010 = bRam003e3010 | 2;
    }
    if (sRam00c0853e != -1) {
      bRam003e3020 = bRam003e3020 | 2;
    }
  }
  if (cRam003ed3bf == '\n') {
    uRam003e2f5d = 1;
  }
  else {
    if ((1 < iRam003ecf5c) && (uRam003e2f5d = 1, 3 < iRam003ecf5c)) {
      uRam003e2f5d = 0;
      iRam003ecf5c = 0;
    }
  }
  return;
}



void FUN_0000bf58(char param_1,ushort *param_2)

{
  byte bVar1;
  ushort uVar2;
  code *pcVar3;
  ushort uVar4;
  ushort uVar5;
  
  if ((*(byte *)(param_2 + 4) & 1) != 0) {
    if (0xc < iRam003ecf54) {
      uVar4 = 0x4000;
      uVar5 = 0x8000;
      if (param_1 != '\0') {
        uVar4 = 0x8000;
      }
      bVar1 = *(byte *)param_2;
      uVar2 = uRam00c08500;
      if (param_1 != '\0') {
        uVar5 = 0x4000;
        uVar2 = uRam00c08540;
      }
      if ((uRam002afcb0 & uVar5) == 0) {
        if ((uRam002afcb0 & uVar4) != 0) {
          iRam003ecf54 = 0;
          uVar5 = *param_2;
          if ((short)*param_2 < 1) {
            uVar5 = uVar2;
          }
          *param_2 = uVar5 - 1;
        }
        uVar5 = *param_2;
      }
      else {
        uVar5 = *param_2 + 1;
        if ((long)(int)(uVar2 - 1) <= (long)(short)*param_2) {
          uVar5 = 0;
        }
        *param_2 = uVar5;
        iRam003ecf54 = 0;
        uVar5 = *param_2;
      }
      if (bVar1 != uVar5) {
        (*pcRam00c0004c)();
      }
      if (cRam003e2f58 != -1) {
        if ((uRam002afcb0 & 0x1000) == 0) {
          if ((uRam002afcb0 & 0x2000) != 0) {
            cRam003e2f58 = '\0';
            iRam003ecf54 = 0;
          }
        }
        else {
          cRam003e2f58 = '\x01';
          iRam003ecf54 = 0;
        }
      }
    }
    pcVar3 = pcRam00c0004c;
    if ((uRam002afcb2 & uRam003ecf6c) == 0) {
      if ((uRam002afcb2 & uRam003ecf6e) != 0) {
        param_2[2] = 0x3fff;
        (*pcVar3)(2,1);
      }
    }
    else {
      if (cRam003e2f58 == -1) {
        if (param_1 == '\0') {
          uVar5 = *(ushort *)((short)*param_2 * 2 + 0xc08518);
          uVar4 = *param_2;
        }
        else {
          uVar4 = *param_2;
          uVar5 = *(ushort *)((short)*param_2 * 2 + 0xc08558);
        }
        if ((uVar5 & 2) == 0) {
          (*pcRam00c0004c)(2,3);
        }
        else {
          param_2[2] = uVar4;
          (*pcRam00c0004c)(2,0);
        }
      }
      else {
        param_2[2] = *param_2;
      }
    }
  }
  return;
}



void FUN_0000c1a8(uint param_1,short *param_2,undefined2 param_3,short param_4)

{
  undefined uVar1;
  int iVar3;
  ulong uVar4;
  ulong uVar5;
  short sVar6;
  ulong uVar7;
  uint uVar8;
  byte bVar2;
  
  uVar8 = param_1 & 0xff;
  bVar2 = bRam00c08540;
  if (uVar8 == 0) {
    bVar2 = bRam00c08500;
  }
  if ((ulong)bVar2 < 6) {
    uVar1 = *(undefined *)param_2;
  }
  else {
    if (uVar8 == 0) {
      uRam003e2f8c = 0x10;
      uRam003e2fac = 0x138;
    }
    else {
      uRam003e2f8c = 8;
      uRam003e2fac = 0x130;
    }
    sRam003e2f8e = param_4;
    sRam003e2fae = param_4;
    func_0x00383a90(0,0x3e2f80);
    func_0x00383a90(0,0x3e2fa0);
    uVar1 = *(undefined *)param_2;
  }
  func_0x0038cf58(uVar8,uVar1,param_3,param_4);
  uVar7 = 0;
  if ((long)*param_2 < (long)(int)(bVar2 - 1)) {
    uVar5 = (long)(int)(*(byte *)param_2 + 1) & 0xff;
  }
  else {
    uVar5 = 0;
  }
  if ((uVar5 != (long)(int)*param_2) && (0 < param_2[1])) {
    iVar3 = uVar8 * 4;
    do {
      sVar6 = param_4;
      if (uVar8 == 0) {
        sVar6 = param_4 + -0x10;
      }
      func_0x0038cc80(uVar8,uVar5,*(undefined2 *)(iVar3 * 2 + 0x3e2fc0),sVar6);
      uVar4 = SEXT48((int)uVar5 + 1);
      uVar5 = 0;
      if (uVar4 < bVar2) {
        uVar5 = uVar4 & 0xff;
      }
      uVar7 = (long)((int)uVar7 + 1) & 0xff;
    } while ((uVar5 != (long)*param_2) &&
            (iVar3 = uVar8 * 4 + (int)uVar7, (long)uVar7 < (long)param_2[1]));
  }
  return;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x0000c504: Changing call to branch
// WARNING: Possible PIC construction at 0x0000c550: Changing call to branch
// WARNING: Removing unreachable block (ram,0x0000c50c)
// WARNING: Removing unreachable block (ram,0x0000c558)
// WARNING: Removing unreachable block (ram,0x0000c588)
// WARNING: Removing unreachable block (ram,0x0000c5a0)
// WARNING: Removing unreachable block (ram,0x0000c5b8)
// WARNING: Removing unreachable block (ram,0x0000c5c4)
// WARNING: Removing unreachable block (ram,0x0000c5d4)

void FUN_0000c380(char param_1,uint param_2,short param_3,short param_4)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  undefined8 uVar4;
  int iVar5;
  uint uVar6;
  uint uVar7;
  
  uVar6 = SEXT24(param_4);
  uVar7 = SEXT24(param_3);
  uVar2 = param_2 & 0xff;
  if (param_1 == '\0') {
    iVar5 = uVar2 * 0x3a + 0x3e2a38;
  }
  else {
    iVar5 = uVar2 * 0x3a + 0x3e2c80;
  }
  iVar3 = 0x428c;
  if (param_1 == '\0') {
    uVar1 = uVar2 * 0x120 - 0x1940;
  }
  else {
    iVar3 = 0x42ac;
    uVar1 = uVar2 * 0x120 - 0xe00;
  }
  uVar4 = 0x200;
  if ((*(ushort *)((iVar3 + uVar2) * 2 + 0xc00000) & 2) == 0) {
    uVar4 = 0x300;
  }
  func_0x00383ea0(0,0x540,0xca0,uVar4,uVar1 & 0xffff,0x630,uVar7 & 0xffff,uVar6 & 0xffff);
  func_0x003b7c80(0,0xf,uVar7,uVar6,0,0);
  if (*(short *)(iVar5 + 0x12) < 0x1e) {
    func_0x00382ab8(2,0);
  }
  else {
    func_0x00382ab8(7,0);
  }
  func_0x00382ad8((int)((uVar7 - 4) * 0x10000) >> 0x10,(int)((uVar6 + 0x30) * 0x10000) >> 0x10);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Removing unreachable block (ram,0x0000cb04)
// WARNING: Removing unreachable block (ram,0x0000cb7c)

void FUN_0000c658(char param_1,uint param_2,short param_3,short param_4)

{
  uint uVar1;
  ushort uVar2;
  ushort uVar3;
  char *pcVar4;
  char cVar5;
  ushort uVar6;
  ushort uVar7;
  ushort uVar8;
  uint uVar9;
  long lVar10;
  undefined8 uVar11;
  char *pcVar12;
  int iVar13;
  int iVar14;
  int iVar15;
  uint uVar16;
  uint uVar17;
  uint uVar18;
  ushort *puVar19;
  
  uVar17 = SEXT24(param_4);
  uVar18 = SEXT24(param_3);
  uVar16 = param_2 & 0xff;
  if (param_1 == '\0') {
    puVar19 = (ushort *)(uVar16 * 0x3a + 0x3e2a38);
  }
  else {
    puVar19 = (ushort *)(uVar16 * 0x3a + 0x3e2c80);
  }
  iVar15 = 0x428c;
  if (param_1 == '\0') {
    uVar6 = 0xfff0;
    uVar7 = 0xfff0;
    uVar8 = 0x30;
    uVar9 = uVar16 * 0x120 - 0x1940;
  }
  else {
    iVar15 = 0x42ac;
    uVar6 = 0xffd0;
    uVar7 = 0x30;
    uVar8 = 0xffd0;
    uVar9 = uVar16 * 0x120 - 0xe00;
  }
  uVar11 = 0x200;
  if ((*(ushort *)((iVar15 + uVar16) * 2 + 0xc00000) & 2) == 0) {
    uVar11 = 0x300;
  }
  func_0x00383da0(0,0x40,0xca0,uVar11,uVar9 & 0xffff,0x630,uVar18 & 0xffff,uVar17 & 0xffff);
  func_0x003b7c80(0,5,uVar18,uVar17,0,0);
  if (cRam003e2f58 == -1) {
    if (param_1 == '\0') {
      if (sRam00c084fe != -1) {
        func_0x003978c0(0,(int)((uVar18 - 0x10) * 0x10000) >> 0x10,
                        (int)((uVar17 - 0x10) * 0x10000) >> 0x10,0x78,0xf,0xffffffffffffffff,5,1);
      }
      lVar10 = (*pcRam00c000bc)();
      if (lVar10 != 0) {
        func_0x003978c0(2,0x10,0xe0,0x18,8,0xffffffffffffffff,5,0);
        func_0x00382ad8(0x10,0xe0);
        func_0x00382b60(2,0,0x3f2a10,uVar16);
      }
    }
    else {
      if (sRam00c0853e != -1) {
        func_0x003978c0(0,(int)((uVar18 - 0x30) * 0x10000) >> 0x10,
                        (int)((uVar17 + 0x30) * 0x10000) >> 0x10,0x78,0xf,0xffffffffffffffff,5,1);
      }
      lVar10 = (*pcRam00c000bc)();
      if (lVar10 != 0) {
        iVar15 = (int)((uVar18 - 0x40) * 0x10000) >> 0x10;
        iVar13 = (int)((uVar17 + 0x50) * 0x10000) >> 0x10;
        func_0x003978c0(2,iVar15,iVar13,0x18,8,0xffffffffffffffff,5,0);
        func_0x00382ad8(iVar15,iVar13);
        func_0x00382b60(2,0,0x3f2a10,uVar16);
      }
    }
  }
  lVar10 = func_0x001251e8();
  if ((lVar10 == 0) || (param_1 == '\0')) {
    lVar10 = func_0x00125258();
    if (lVar10 == 0) {
      uVar2 = *puVar19;
    }
    else {
      if (param_1 != '\0') {
        uVar16 = uRam003f2680 + 0x1015080;
        func_0x00125400(uVar16);
        goto LAB_0000c9b4;
      }
      uVar2 = *puVar19;
    }
    uVar3 = *(ushort *)((uint)uVar2 * 4 + 0x3f24b2);
    uVar9 = (uVar3 & 0xf00) >> 5;
    func_0x00383da0(0,0,0xc88,0x1b84,*(ushort *)((uint)uVar2 * 4 + 0x3f24b0) + 0x15080 >> 3,uVar3,
                    uVar18 + uVar6 & 0xffff,uVar17 + uVar7 & 0xffff);
    func_0x00383da0(0,0,0xc88,0x1b84,0x4e70,0x310,uVar18 + uVar6 + uVar9 & 0xffff,
                    uVar17 + uVar7 & 0xffff);
    uVar2 = *puVar19;
  }
  else {
    uVar16 = uRam003f2680 + 0x1015080;
    func_0x00125348(uVar16);
LAB_0000c9b4:
    uVar9 = (uRam003f2682 & 0xf00) >> 5;
    func_0x00383da0(0,0,0xc88,0x1b84,uVar16 >> 3 & 0xffff,uRam003f2682,uVar18 + uVar6 & 0xffff,
                    uVar17 + uVar7 & 0xffff);
    func_0x00383da0(0,0,0xc88,0x1b84,0x4e70,0x310,uVar18 + uVar6 + uVar9 & 0xffff,
                    uVar17 + uVar7 & 0xffff);
    uVar2 = *puVar19;
  }
  uVar16 = *(byte *)((uint)uVar2 * 2 + 0xc00532) + 1 & 0xff;
  if (9 < uVar16) {
    uVar1 = uVar16 / 10;
    uVar16 = uVar16 % 10;
    iVar15 = uVar1 * 4;
    func_0x00383da0(0,0,0xc88,0x1b84,*(ushort *)(iVar15 + 0x3f27f0) + 0x26f80 >> 3,
                    *(undefined2 *)(iVar15 + 0x3f27f2),uVar18 + uVar6 + uVar9 + 0x18 & 0xffff,
                    uVar17 + uVar7 & 0xffff);
    uVar9 = (int)((uVar9 + 8) * 0x1000000) >> 0x18;
  }
  func_0x00383da0(0,0,0xc88,0x1b84,*(ushort *)(uVar16 * 4 + 0x3f27f0) + 0x26f80 >> 3,
                  *(undefined2 *)(uVar16 * 4 + 0x3f27f2),uVar18 + uVar6 + uVar9 + 0x18 & 0xffff,
                  uVar17 + uVar7 & 0xffff);
  if ((short)puVar19[9] < 0x1e) {
    func_0x00382ab8(2,0);
  }
  else {
    func_0x00382ab8(7,0);
  }
  func_0x00382ad8((int)((uVar18 + uVar8 + 4) * 0x10000) >> 0x10,uVar17);
  func_0x00382b60(0,0,0x3f2a70,puVar19[9]);
  func_0x00382ab8(7,0);
  func_0x00382ad8((int)((uVar18 + uVar8 + 4) * 0x10000) >> 0x10,
                  (int)((uVar17 + 9) * 0x10000) >> 0x10);
  func_0x00382b60(0,0,0x3f2a78,*(undefined2 *)((uint)*puVar19 * 2 + 0xc0128e));
  cVar5 = func_0x00188e50(*(undefined4 *)((short)puVar19[3] * 4 + 0x3e8270));
  iVar14 = (int)cVar5;
  iVar13 = 0;
  iVar15 = iVar14;
  if (iVar14 < 1) {
    uVar7 = puVar19[3];
  }
  else {
    uVar7 = puVar19[3];
    pcVar4 = *(char **)((short)puVar19[3] * 4 + 0x3e8270);
    pcVar12 = pcVar4;
    while( true ) {
      if ((byte)(*pcVar12 + 0x22U) < 2) {
        iVar15 = (iVar15 + -1) * 0x1000000 >> 0x18;
      }
      iVar13 = (int)(char)((char)iVar13 + '\x01');
      if (iVar14 <= iVar13) break;
      pcVar12 = pcVar4 + iVar13;
    }
  }
  iVar15 = (iVar15 * -4 + 0x18) * 0x1000000 >> 0x18;
  if (iVar15 < 0) {
    iVar15 = 0;
  }
  if ((short)uVar7 < 0xb) {
    func_0x00382ab8(7,0);
    func_0x00382ad8((int)((uVar18 + uVar8 + (int)(char)iVar15) * 0x10000) >> 0x10,
                    (int)((uVar17 + 0x18) * 0x10000) >> 0x10);
    uVar11 = func_0x001383d0(*(undefined4 *)((short)puVar19[3] * 4 + 0x3e8270));
    func_0x00382b60(0,0,0x3f2a80,uVar11);
  }
  func_0x00383da0(0,0,0xc88,0x1b88,0x4eb8,0x210,uVar18 + uVar8 & 0xffff,uVar17 + 0x20 & 0xffff);
  func_0x00382ad8((int)((uVar18 + uVar8 + 0x10) * 0x10000) >> 0x10,
                  (int)((uVar17 + 0x24) * 0x10000) >> 0x10);
  func_0x00382b60(0,0,0x3f2a88,puVar19[4]);
  if (cRam003e2fd0 == '\0') {
    if (bRam00400ec6 < 0x7c) {
      bRam00400ec6 = bRam00400ec6 + 1;
    }
    else {
      cRam003e2fd0 = '\x01';
    }
  }
  else {
    if (bRam00400ec6 < 0x11) {
      cRam003e2fd0 = '\0';
    }
    else {
      bRam00400ec6 = bRam00400ec6 - 1;
    }
  }
  sRam0118090a = (ushort)(bRam00400ec6 >> 2) * 0x400 + (ushort)(bRam00400ec6 >> 2) * 0x20 + -0x8000;
  return;
}



void FUN_0000cee8(void)

{
  undefined *puVar1;
  byte bVar2;
  undefined2 uVar3;
  ushort uVar4;
  bool bVar5;
  ushort uVar6;
  uint uVar7;
  uint uVar8;
  uint uVar9;
  undefined8 uVar10;
  byte bVar11;
  uint uVar12;
  byte *pbVar13;
  ushort *puVar14;
  int iVar15;
  uint uVar16;
  undefined4 uVar17;
  int iVar18;
  undefined2 *puVar19;
  uint unaff_s3_lo;
  byte local_90 [4];
  undefined local_8c;
  byte local_80 [4];
  undefined local_7c;
  undefined local_70 [8];
  undefined4 local_68;
  int local_60;
  
  uVar8 = 0;
  uVar9 = 0;
  puVar1 = local_70 + 7;
  uVar7 = (uint)puVar1 & 7;
  *(ulong *)(puVar1 + -uVar7) =
       *(ulong *)(puVar1 + -uVar7) & -1 << (uVar7 + 1) * 8 | uRam003f2ac0 >> (7 - uVar7) * 8;
  local_70 = uRam003f2ac0;
  local_68 = uRam003f2ac8;
  uVar7 = 0;
  func_0x00187580(local_90,0xac,5);
  func_0x00187580(local_80,0xac,5);
  local_60 = 1;
  do {
    func_0x003839a8();
    func_0x00384000(0,0x13f,0xef);
    func_0x003840f0(0,0,0);
    func_0x00384078(0,0,0,0x13f);
    func_0x003978c0(0,0,0,0x13f,0xef,0xffffffffffffffff,0,0);
    func_0x00382ab8(7);
    uVar16 = unaff_s3_lo >> 4;
    if (6 < iRam003ecf54) {
      iVar18 = 1;
      if ((uRam002afcb0 & 0x80) != 0) {
        iVar18 = 8;
      }
      if ((uRam002afcb0 & 0x1000) != 0) {
        if (uVar7 == 0) {
          uVar7 = 3;
        }
        else {
          uVar7 = uVar7 + 0xff & 0xff;
        }
      }
      if ((uRam002afcb0 & 0x2000) != 0) {
        if (uVar7 < 3) {
          uVar7 = uVar7 + 1 & 0xff;
        }
        else {
          uVar7 = 0;
        }
      }
      bVar5 = uVar7 < 2;
      if (((uRam002afcb0 & 0x4000) != 0) && (bVar5)) {
        if (uVar8 == 0) {
          uVar8 = 4;
        }
        else {
          uVar8 = uVar8 + 0xff & 0xff;
        }
      }
      if (((uRam002afcb0 & 0x8000) != 0) && (bVar5)) {
        if (uVar8 < 4) {
          uVar8 = uVar8 + 1 & 0xff;
        }
        else {
          uVar8 = 0;
        }
      }
      if (!bVar5) {
        uVar8 = 0;
      }
      if ((uRam002afcb0 & 0x400) != 0) {
        pbVar13 = local_80;
        if (uVar7 == 1) {
LAB_0000d110:
          bVar2 = pbVar13[uVar8];
          bVar11 = bVar2 + (char)iVar18;
          if (0xac < bVar2) {
            bVar11 = 0;
          }
          pbVar13[uVar8] = bVar11;
        }
        else {
          if (uVar7 < 2) {
            pbVar13 = local_90;
            if (uVar7 == 0) goto LAB_0000d110;
          }
          else {
            if (uVar7 == 2) {
              if (unaff_s3_lo < 0x400) {
                unaff_s3_lo = unaff_s3_lo + iVar18 & 0xffff;
              }
              else {
                unaff_s3_lo = 0;
              }
            }
            else {
              if (uVar7 == 3) {
                if (uVar9 < 2) {
                  uVar9 = uVar9 + 1 & 0xff;
                }
                else {
                  uVar9 = 0;
                }
              }
            }
          }
        }
      }
      if ((uRam002afcb0 & 0x100) != 0) {
        pbVar13 = local_80;
        if (uVar7 == 1) {
LAB_0000d1b4:
          bVar2 = pbVar13[uVar8];
          bVar11 = bVar2 - (char)iVar18;
          if (bVar2 == 0) {
            bVar11 = 0xad;
          }
          pbVar13[uVar8] = bVar11;
        }
        else {
          if (uVar7 < 2) {
            pbVar13 = local_90;
            if (uVar7 == 0) goto LAB_0000d1b4;
          }
          else {
            if (uVar7 == 2) {
              if (unaff_s3_lo == 0) {
                unaff_s3_lo = 0x3ff;
              }
              else {
                unaff_s3_lo = unaff_s3_lo - iVar18 & 0xffff;
              }
            }
            else {
              if (uVar7 == 3) {
                if (uVar9 == 0) {
                  uVar9 = 2;
                }
                else {
                  uVar9 = uVar9 + 0xff & 0xff;
                }
              }
            }
          }
        }
      }
      uVar16 = unaff_s3_lo >> 4;
      if ((uVar7 == 2) && ((uRam002afcb0 & 0x200) != 0)) {
        uVar12 = 0x8000 >> (unaff_s3_lo & 0xf);
        puVar14 = (ushort *)((uVar16 & 0xffff) * 2 + 0xc085b8);
        uVar4 = *puVar14;
        uVar6 = (ushort)uVar12;
        if ((uVar4 & uVar12) == 0) {
          *puVar14 = uVar4 | uVar6;
        }
        else {
          *puVar14 = uVar4 & ~uVar6;
        }
      }
      iRam003ecf54 = 0;
    }
    uVar10 = 0x4f;
    if (((uint)*(ushort *)((uVar16 & 0xffff) * 2 + 0xc085b8) & 0x8000 >> (unaff_s3_lo & 0xf)) == 0)
    {
      uVar10 = 0x58;
    }
    func_0x00382ad8(0x10,0x10);
    func_0x00382b60(1,0,0x3f2ad0,local_90[0],local_90[1],local_90[2],local_90[3],local_8c);
    func_0x00382ad8(0x10,0x20);
    func_0x00382b60(1,0,0x3f2af0,local_80[0],local_80[1],local_80[2],local_80[3],local_7c);
    func_0x00382ad8(0x10,0x30);
    func_0x00382b60(1,0,0x3f2b10,unaff_s3_lo,uVar10);
    func_0x00382ad8(0x10,0x40);
    uVar10 = func_0x001383d0(*(undefined4 *)(local_70 + uVar9 * 4));
    func_0x00382b60(1,0,0x3f2b20,uVar10);
    iVar18 = (int)(uVar7 * 0x100000 + 0x100000) >> 0x10;
    func_0x003978c0(0,0x10,iVar18,0x27,9,0xffffffffffffffff,2,0);
    if (uVar7 == 2) {
      iVar15 = 0x48;
      iVar18 = 0x30;
      uVar17 = 0x1f;
LAB_0000d414:
      func_0x003978c0(0,iVar15,iVar18,uVar17,9,0xffffffffffffffff,2,0);
    }
    else {
      if (uVar7 < 3) {
        uVar17 = 0x17;
        iVar15 = (int)(uVar8 * 0x280000 + 0x480000) >> 0x10;
        goto LAB_0000d414;
      }
      if (uVar7 == 3) {
        iVar18 = func_0x00188e50(*(undefined4 *)(local_70 + uVar9 * 4));
        func_0x003978c0(0,0x48,0x40,(iVar18 << 0x13) >> 0x10,9,0xffffffffffffffff,2,0);
      }
    }
    func_0x00382ad8(8,100);
    func_0x00382b60(1,0);
    func_0x00382ad8(8,0x6e);
    func_0x00382b60(1,1);
    func_0x00382ad8(8,0x7e);
    func_0x00382b60(1,1);
    func_0x00382ad8(8,0x8e);
    func_0x00382b60(1,1);
    func_0x00382ad8(8,0x9e);
    func_0x00382b60(1,1);
    func_0x00382ad8(8,0xae);
    func_0x00382b60(1,1);
    func_0x00382ad8(8,0xc2);
    func_0x00382b60(1,1);
    func_0x00382ab8();
    func_0x00382ad8();
    func_0x00382b60();
    if (uRam002afcb0 == 0x800) {
      uRam002afcb0 = 0;
      uRam002afcb2 = 0;
      local_60 = 0;
    }
    func_0x00382a18();
    func_0x00383c00();
    uRam003ecf64 = 1;
    func_0x001485c8();
  } while (local_60 != 0);
  uRam00c084f0 = (undefined2)uVar9;
  uRam00c084fc = (ushort)local_90[0];
  uRam00c0853c = (ushort)local_80[0];
  iVar18 = 0;
  if (local_90[0] != 0xac) {
    iVar15 = 0;
    while( true ) {
      *(undefined2 *)(iVar15 + 0xc08518) = 0xb;
      *(ushort *)(iVar15 + 0xc08502) = (ushort)local_90[0];
      iVar15 = iVar15 + 2;
      func_0x00389580();
      iVar18 = (iVar18 + 1) * 0x10000 >> 0x10;
      if ((4 < iVar18) || (local_90[iVar18] == 0xac)) break;
      local_90[0] = local_90[iVar18];
    }
  }
  *(undefined2 *)(iVar18 * 2 + 0xc08502) = 0xac;
  *(undefined2 *)(iVar18 * 2 + 0xc08518) = 0xb;
  uRam00c08500 = (ushort)iVar18;
  sRam003e300a = uRam00c08500 - 1;
  iVar18 = 0;
  if (local_80[0] != 0xac) {
    iVar15 = 0;
    pbVar13 = local_80;
    do {
      bVar2 = *pbVar13;
      *(undefined2 *)(iVar15 + 0xc08558) = 0xb;
      *(ushort *)(iVar15 + 0xc08542) = (ushort)bVar2;
      iVar15 = iVar15 + 2;
      func_0x00389580();
      iVar18 = (iVar18 + 1) * 0x10000 >> 0x10;
      pbVar13 = local_80 + iVar18;
      if (4 < iVar18) break;
    } while (*pbVar13 != 0xac);
  }
  *(undefined2 *)(iVar18 * 2 + 0xc08542) = 0xac;
  *(undefined2 *)(iVar18 * 2 + 0xc08558) = 0xb;
  uRam00c08540 = (ushort)iVar18;
  sRam003e301a = uRam00c08540 - 1;
  iVar18 = 0;
  if (uRam00c08500 != 0) {
    puVar19 = (undefined2 *)0xc08502;
    uVar3 = uRam00c08502;
    iVar15 = 0x1073600;
    while( true ) {
      puVar19 = puVar19 + 1;
      func_0x00396a98(0x42,iVar15,uVar3);
      iVar18 = (iVar18 + 1) * 0x10000 >> 0x10;
      if ((int)(uint)uRam00c08500 <= iVar18) break;
      uVar3 = *puVar19;
      iVar15 = iVar15 + 0x900;
    }
  }
  iVar18 = 0;
  if (uRam00c08540 != 0) {
    puVar19 = (undefined2 *)0xc08542;
    uVar3 = uRam00c08542;
    iVar15 = 0x1079000;
    while( true ) {
      puVar19 = puVar19 + 1;
      func_0x00396a98(0x42,iVar15,uVar3);
      iVar18 = (iVar18 + 1) * 0x10000 >> 0x10;
      if ((int)(uint)uRam00c08540 <= iVar18) break;
      uVar3 = *puVar19;
      iVar15 = iVar15 + 0x900;
    }
  }
  return;
}



void FUN_0000d828(void)

{
  undefined *puVar1;
  uint uVar2;
  long lVar3;
  undefined auStack144 [16];
  undefined auStack128 [48];
  
  if (((sRam00c084fe == -1) && (uRam00c0857c == 0xffff)) && (1 < uRam00c08500)) {
    puVar1 = auStack144 + 3;
    uVar2 = (uint)puVar1 & 3;
    *(uint *)(puVar1 + -uVar2) =
         *(uint *)(puVar1 + -uVar2) & -1 << (uVar2 + 1) * 8 | uRam003f2c30 >> (3 - uVar2) * 8;
    auStack144._0_4_ = uRam003f2c30;
    puVar1 = auStack128 + 3;
    uVar2 = (uint)puVar1 & 3;
    *(uint *)(puVar1 + -uVar2) =
         *(uint *)(puVar1 + -uVar2) & -1 << (uVar2 + 1) * 8 | uRam003f2c38 >> (3 - uVar2) * 8;
    auStack128._0_4_ = uRam003f2c38;
    if (bRam003e3108 < 5) {
                    // WARNING: Could not emulate address calculation at 0x0000d8d8
                    // WARNING: Treating indirect jump as call
      (**(code **)((uint)bRam003e3108 * 4 + 0x3f2c60))();
      return;
    }
  }
  else {
    if (uRam00c08500 == 1) {
      uRam00c0857c = 0xff;
    }
    if ((uRam00c0857c & 0x8000) != 0) {
      uRam00c0857c = 0xff;
    }
    uRam003e2ff2 = 0xff;
    uRam003e2fec = 1;
    lVar3 = func_0x003e0fc8();
    uRam00c0f208 = (ushort)(lVar3 == 1);
  }
  func_0x00383da0(0,0,0x2c88,0x1b80,0x4fc8,0xe38,0x68,0x60);
  return;
}



void FUN_0000e2e8(void)

{
  undefined *puVar1;
  uint uVar2;
  long lVar3;
  undefined auStack64 [4];
  undefined2 local_3c;
  undefined auStack48 [4];
  undefined2 local_2c;
  
  puVar1 = auStack64 + 3;
  uVar2 = (uint)puVar1 & 3;
  *(uint *)(puVar1 + -uVar2) =
       *(uint *)(puVar1 + -uVar2) & -1 << (uVar2 + 1) * 8 | uRam003f2cd8 >> (3 - uVar2) * 8;
  auStack64 = uRam003f2cd8;
  local_3c = uRam003f2cdc;
  puVar1 = auStack48 + 3;
  uVar2 = (uint)puVar1 & 3;
  *(uint *)(puVar1 + -uVar2) =
       *(uint *)(puVar1 + -uVar2) & -1 << (uVar2 + 1) * 8 | uRam003f2ce0 >> (3 - uVar2) * 8;
  auStack48 = uRam003f2ce0;
  local_2c = uRam003f2ce4;
  lVar3 = (*pcRam00c000bc)();
  if (lVar3 != 0) {
    func_0x003978c0(6,0x10,0x40,0x30,8,0xffffffffffffffff,0,1);
    func_0x00382ad8(0x10,0x40);
    func_0x00382b60(6,0,0x3f2ce8,bRam003e3120);
  }
  if (6 < bRam003e3120) {
    if (bRam003e3120 < 4) {
      func_0x003bbc08(uRam00400f00);
    }
    return;
  }
                    // WARNING: Could not emulate address calculation at 0x0000e3c8
                    // WARNING: Treating indirect jump as call
  (**(code **)((uint)bRam003e3120 * 4 + 0x3f2cf0))();
  return;
}



// WARNING: Removing unreachable block (ram,0x0000f01c)

void FUN_0000e9c8(void)

{
  undefined *puVar1;
  bool bVar2;
  short sVar3;
  byte bVar4;
  int iVar5;
  long lVar6;
  uint uVar7;
  short *psVar8;
  ushort *puVar9;
  uint uVar10;
  undefined auStack144 [16];
  undefined auStack128 [16];
  undefined auStack112 [8];
  undefined4 local_68;
  undefined auStack96 [8];
  undefined4 local_58;
  
  puVar1 = auStack144 + 3;
  uVar7 = (uint)puVar1 & 3;
  *(uint *)(puVar1 + -uVar7) =
       *(uint *)(puVar1 + -uVar7) & -1 << (uVar7 + 1) * 8 | uRam003f2d20 >> (3 - uVar7) * 8;
  auStack144._0_4_ = uRam003f2d20;
  puVar1 = auStack128 + 3;
  uVar7 = (uint)puVar1 & 3;
  *(uint *)(puVar1 + -uVar7) =
       *(uint *)(puVar1 + -uVar7) & -1 << (uVar7 + 1) * 8 | uRam003f2d28 >> (3 - uVar7) * 8;
  auStack128._0_4_ = uRam003f2d28;
  lVar6 = (*pcRam00c000bc)();
  if (lVar6 != 0) {
    puVar1 = auStack112 + 7;
    uVar7 = (uint)puVar1 & 7;
    *(ulong *)(puVar1 + -uVar7) =
         *(ulong *)(puVar1 + -uVar7) & -1 << (uVar7 + 1) * 8 | uRam003f2d48 >> (7 - uVar7) * 8;
    auStack112 = uRam003f2d48;
    local_68 = uRam003f2d50;
    puVar1 = auStack96 + 7;
    uVar7 = (uint)puVar1 & 7;
    *(ulong *)(puVar1 + -uVar7) =
         *(ulong *)(puVar1 + -uVar7) & -1 << (uVar7 + 1) * 8 | uRam003f2d70 >> (7 - uVar7) * 8;
    auStack96 = uRam003f2d70;
    local_58 = uRam003f2d78;
    func_0x00382ad8(0x10,0x20);
    func_0x001383d0(*(undefined4 *)(auStack112 + (uint)bRam003e3135 * 4));
    func_0x00382b60(6);
    if (bRam003e3135 == 0) {
      func_0x00382ad8(0x32,0x20);
      func_0x00382b60(6);
    }
    if (bRam003e3135 == 1) {
      func_0x00382ad8(0x70,0x20);
      func_0x001383d0(*(undefined4 *)(auStack96 + (uint)bRam003e3136 * 4));
      func_0x00382b60(6);
    }
  }
  bVar4 = bRam003e3135;
  uVar7 = (uint)bRam003ed3bf;
  bVar2 = bRam003e3135 == 1;
  *(undefined *)(uVar7 + 0x3ed3d0) = 1;
  if (bVar2) {
    func_0x0038c858(0);
    sVar3 = sRam003e300c;
    if (sRam003e300c == sRam003e3008) {
      bRam003e3010 = bRam003e3010 & 0xfe;
      bRam00400f1f = *(byte *)(sRam003e300c * 2 + 0xc08502);
      if (bRam003e3136 == 1) {
        iVar5 = ((sRam003e300c * 8 - (int)sRam003e300c) * 4 + (int)sRam003e300c) * 2;
        if ((*(short *)(iVar5 + 0x3e2a40) <
             *(short *)((sRam003e3018 * 0x1c + (int)sRam003e3018) * 2 + 0x3e2c88)) ||
           (*(short *)(iVar5 + 0x3e2a4a) < 0x1e)) {
          sRam00400f22 = func_0x0038fe78((undefined)sRam003e300c);
        }
        else {
          sRam00400f22 = 0;
        }
        if (sRam00400f22 == 1) {
          bRam003e3136 = 2;
        }
        else {
          bRam003e3136 = 0;
        }
      }
      else {
        if (bRam003e3136 < 2) {
          if (bRam003e3136 == 0) {
            if (cRam003e3137 == '\0') {
              func_0x00396e38(0x3e3028,3,0,2,0x7fff,0);
              cRam003e3137 = '\x01';
            }
            else {
              if (cRam003e3137 == '\x01') {
                func_0x00383da0(0,0,0x2c88,0x1b80,0x4fc8,0xe38,0x68,0x60);
                func_0x00382ab8(8,0);
                func_0x00382ad8(0x9a,0x6d);
                func_0x00382b60(2,1,0x3f2d18);
                func_0x00382ad8(0x9a,0x7d);
                func_0x00382b60(2,1,0x3f2d10);
                func_0x00382ab8(7,0);
                func_0x003b9a08(2,2,0x88,0x6c,0x3e3138,auStack144,auStack128,2);
                if (sRam003e302c == 0) {
                  bRam003e3135 = 0;
                  uRam00c084fe = (ushort)bRam00400f1f;
                }
                else {
                  if (sRam003e302c != 0x7fff) {
                    sRam003e300c = 0x7fff;
                    bRam003e3010 = bRam003e3010 | 1;
                    bRam003e3136 = 1;
                    cRam003e3137 = '\0';
                  }
                }
              }
            }
          }
        }
        else {
          if (bRam003e3136 == 2) {
            lVar6 = func_0x003900a8();
            if (lVar6 == 0) {
              psVar8 = (short *)((uint)bRam00400f1f * 2 + 0xc01be8);
              iVar5 = (int)*psVar8 - (int)*psVar8 / 10;
              if (iVar5 * 0x10000 >> 0x10 < 0) {
                *psVar8 = 0;
              }
              else {
                *psVar8 = (short)iVar5;
              }
              bRam003e3135 = 0;
              uRam00c084fe = (ushort)bRam00400f1f;
            }
            else {
              if (lVar6 == 1) {
                bRam003e3136 = 1;
                puVar9 = (ushort *)(sRam003e300c * 2 + 0xc08518);
                *puVar9 = *puVar9 & 0xfffd;
                sRam003e300c = 0x7fff;
                bRam003e3010 = bRam003e3010 | 1;
              }
            }
          }
        }
      }
    }
    else {
      if (sRam003e300c == 0x3fff) {
        bRam003e3010 = bRam003e3010 & 0xfc;
        uRam003e2ff2 = 0xff;
        sRam003e300c = 0x7fff;
        bRam003e3135 = 0;
        sRam003e2fec = sVar3;
      }
    }
  }
  else {
    if (bVar4 < 2) {
      if (bVar4 == 0) {
        *(undefined *)(uVar7 + 0x3ed3d0) = 1;
        if ((bRam003e3088 & 2) == 0) {
          bRam003e3088 = bRam003e3088 & 0xf1 | 2;
          uRam003e3096 = uRam003e308a;
          uRam003e3098 = uRam003e308c;
        }
        if (sRam00c0853e == -1) {
          bRam003e3020 = bRam003e3020 | 3;
          bRam003e3135 = 2;
          bRam003e3010 = bRam003e3010 & 0xfc;
        }
        else {
          if (uRam00c084fe == 0xffff) {
            bRam003e3010 = bRam003e3010 | 3;
            bRam003e3020 = bRam003e3020 & 0xfe | 2;
            bRam003e3136 = 1;
            sRam003e302c = 0x7fff;
            bRam003e3135 = 1;
            cRam003e3137 = '\0';
          }
          else {
            if (sRam00400f28 < 0x65) {
              sRam00400f28 = sRam00400f28 + 1;
            }
            else {
              uRam003e2ff2 = 0xff;
              sRam003e2fec = 0;
              if ((bRam003e3088 & 4) == 0) {
                bRam003e3088 = bRam003e3088 & 0xf1 | 4;
                uRam003e3098 = uRam003e3090;
                uRam003e3096 = uRam003e308e;
              }
              if ((bRam003e30b8 & 4) == 0) {
                bRam003e30b8 = bRam003e30b8 & 0xf1 | 4;
                uRam003e30c8 = uRam003e30c0;
                uRam003e30c6 = uRam003e30be;
              }
              bRam003e30a0 = bRam003e30a0 & 0xf1 | 4;
              bRam003e30d0 = bRam003e30d0 & 0xf1 | 4;
              uRam003e30b0 = uRam003e30a8;
              uRam003e30e0 = uRam003e30d8;
              uRam003e30ae = uRam003e30a6;
              uRam003e30de = uRam003e30d6;
              sRam00400f28 = 0;
            }
          }
        }
      }
    }
    else {
      if (bVar4 == 2) {
        if (sRam003e301c == sRam003e3018) {
          sRam00c0853e = *(short *)(sRam003e301c * 2 + 0xc08542);
          bRam003e3135 = 0;
          sRam003e301c = 0x7fff;
          if (uRam00c084fe == 0xffff) {
            uRam003e2ff2 = 0xff;
            sRam003e2fec = 1;
          }
        }
        else {
          uVar7 = 0;
          sRam00400f24 = 0;
          if (uRam00c08540 != 0) {
            do {
              iVar5 = (int)sRam00400f24;
              sRam00400f24 = sRam00400f24 + 1;
              if ((*(ushort *)(iVar5 * 2 + 0xc08558) & 1) != 0) {
                uVar7 = uVar7 + 1 & 0xff;
              }
            } while ((int)sRam00400f24 < (int)(uint)uRam00c08540);
          }
          if (((uRam00c08558 & 1) == 0) || (1 < uVar7)) {
            if (0xc < iRam003ecf54) {
              bVar2 = sRam003e301a <= sRam003e3018;
              sRam003e3018 = sRam003e3018 + 1;
              if (bVar2) {
                sRam003e3018 = 0;
              }
              iRam003ecf54 = 0;
              sRam00400f26 = sRam00400f26 + 1;
              (*pcRam00c0004c)(2);
            }
            if ((long)(int)(uVar7 << 2) < (long)sRam00400f26) {
              iVar5 = func_0x001881e8();
              if (sRam003e301a == 0) {
                trap(7);
              }
              uVar10 = iVar5 % (int)sRam003e301a + 1U & 0xff;
              if ((*(ushort *)(uVar10 * 2 + 0xc08558) & 1) == 0) {
                do {
                  if ((int)uVar10 < (int)(uRam00c08540 - 1)) {
                    uVar10 = uVar10 + 1 & 0xff;
                  }
                  else {
                    uVar10 = 1;
                  }
                } while ((*(ushort *)(uVar10 * 2 + 0xc08558) & 1) == 0);
              }
              sRam003e3018 = (short)uVar10;
              sRam00400f26 = 0;
              sRam003e301c = sRam003e3018;
              if ((2 < uVar7) || (cRam003ed3c8 != '\0')) {
                (*pcRam00c0004c)(2,0);
              }
            }
          }
          else {
            sRam003e3018 = 0;
            sRam003e301c = 0;
            if (sRam003e301a != 0) {
              (*pcRam00c0004c)(2,0);
            }
          }
        }
      }
    }
  }
  return;
}



void FUN_0000f390(void)

{
  byte bVar1;
  short sVar3;
  int iVar4;
  int iVar5;
  byte bVar2;
  
  if (cRam003e3140 == '\0') {
    cRam003e3140 = '\x01';
    cRam003e2f58 = '\0';
    *(undefined *)(bRam003ed3bf + 0x3ed3d0) = 1;
  }
  else {
    if (cRam003e3140 != '\x01') {
      return;
    }
  }
  if (cRam003e2f58 == '\0') {
    iVar5 = 0x3e3008;
    iVar4 = 0x3e3018;
    bVar2 = bRam003e3020;
  }
  else {
    iVar5 = 0x3e3018;
    iVar4 = 0x3e3008;
    bVar2 = bRam003e3010;
  }
  bVar1 = *(byte *)(iVar5 + 8);
  *(byte *)(iVar4 + 8) = bVar2 & 0xfc;
  *(byte *)(iVar5 + 8) = bVar1 | 3;
  func_0x0038c858();
  sVar3 = *(short *)(iVar5 + 4);
  if (sVar3 == 0x3fff) {
    bRam003e3020 = bRam003e3020 & 0xfe;
    bRam003e3010 = bRam003e3010 | 1;
    cRam003e2f58 = -1;
    uRam003eebd2 = 0xff;
    uRam003e301c = 0x7fff;
    uRam003e300c = 0x7fff;
    sRam003eebcc = sVar3;
    if (sRam00c0853e != -1) {
      uRam003e3018 = func_0x00397a48(0xc08542,uRam00c08540,sRam00c0853e,0xff);
    }
    cRam003e3140 = '\0';
  }
  else {
    if (sVar3 != 0x7fff) {
      uRam003eebf3 = cRam003e2f58 != '\0';
      if ((bool)uRam003eebf3) {
        uRam003e301c = 0x7fff;
      }
      else {
        uRam003e300c = 0x7fff;
      }
      uRam003eebd2 = 0xff;
      sRam003eebcc = 0;
      sRam003eebec = sVar3;
      (*pcRam00c0004c)(2,0);
    }
  }
  return;
}



// WARNING: Removing unreachable block (ram,0x0000f710)

undefined8 FUN_0000f578(uint param_1)

{
  byte bVar1;
  short sVar2;
  int iVar3;
  undefined8 uVar4;
  uint uVar5;
  int iVar6;
  int unaff_s2_lo;
  char cVar7;
  
  if (*(ushort *)(unaff_s2_lo * 2 + 0xc0329e) < 100) {
    iVar6 = (param_1 & 0xff) * 2;
    uVar5 = 0;
    if (uRam00c084fc != *(ushort *)(iVar6 + 0xc08502)) {
      bVar1 = *(byte *)(ushort *)(iVar6 + 0xc08502);
      cVar7 = '\0';
      if (uRam00c08500 != 0) {
        iVar3 = 0;
        do {
          if ((*(ushort *)(iVar3 + 0xc08518) & 1) != 0) {
            cVar7 = cVar7 + '\x01';
          }
          uVar5 = uVar5 + 1 & 0xff;
          iVar3 = uVar5 << 1;
        } while (uVar5 < uRam00c08500);
      }
      iVar3 = 100;
      if ((*(ushort *)(iVar6 + 0xc08518) & 0x10) == 0) {
        iVar3 = (int)((100 - (uint)*(ushort *)((uint)bVar1 * 2 + 0xc0329e)) * 0x10000) >> 0x10;
        if (iVar3 < 0) {
          iVar3 = 0;
        }
        sVar2 = (short)iVar3;
        if (100 < sVar2) {
          sVar2 = 100;
        }
        iVar3 = *(short *)((sRam003e3008 * 0x1c + (int)sRam003e3008) * 2 + 0x3e2a40) + 0x32;
        if (iVar3 == 0) {
          trap(7);
        }
        iVar3 = (int)sVar2 *
                ((*(short *)((sRam003e3018 * 0x1c + (int)sRam003e3018) * 2 + 0x3e2c88) + 0x32) /
                iVar3) * 0x10000 >> 0x10;
      }
      uVar4 = func_0x001881e8();
      if (iVar3 <= (int)uVar4 % 100) {
        return uVar4;
      }
      if ((cVar7 != '\x01') && (bVar1 != uRam00c084fc)) {
        *(ushort *)(iVar6 + 0xc08518) = *(ushort *)(iVar6 + 0xc08518) | 0x10;
        return 1;
      }
      *(ushort *)(iVar6 + 0xc08518) = *(ushort *)(iVar6 + 0xc08518) | 0x20;
      return 2;
    }
  }
  return 0;
}



undefined8 FUN_0000f7a8(void)

{
  undefined *puVar1;
  bool bVar2;
  uint uVar3;
  byte bVar4;
  long lVar5;
  undefined8 uVar6;
  undefined auStack112 [16];
  undefined auStack96 [16];
  
  puVar1 = auStack112 + 3;
  uVar3 = (uint)puVar1 & 3;
  *(uint *)(puVar1 + -uVar3) =
       *(uint *)(puVar1 + -uVar3) & -1 << (uVar3 + 1) * 8 | uRam003f2db0 >> (3 - uVar3) * 8;
  auStack112._0_4_ = uRam003f2db0;
  puVar1 = auStack96 + 3;
  uVar3 = (uint)puVar1 & 3;
  *(uint *)(puVar1 + -uVar3) =
       *(uint *)(puVar1 + -uVar3) & -1 << (uVar3 + 1) * 8 | uRam003f2db8 >> (3 - uVar3) * 8;
  auStack96._0_4_ = uRam003f2db8;
  lVar5 = (*pcRam00c000bc)();
  if (lVar5 != 0) {
    func_0x00382ad8(0x10,0x30);
    func_0x00382b60();
    bRam00400f2b = 0;
    if (bRam003e3198 != 0) {
      do {
        func_0x00382ad8((bRam00400f2b & 3) * 0x200000 + 0x100000 >> 0x10,
                        (uint)(bRam00400f2b >> 2) * 0x80000 + 0x100000 >> 0x10);
        func_0x00382b60(6);
        bRam00400f2b = bRam00400f2b + 1;
      } while (bRam00400f2b < bRam003e3198);
    }
  }
  uRam00400f2a = *(undefined *)(sRam003e300c * 2 + 0xc08502);
  if (bRam003e3148 == 1) {
LAB_0000f99c:
    bVar4 = bRam003e3149;
    bVar2 = bRam003e3149 < 5;
    *(undefined *)(bRam003ed3bf + 0x3ed3d0) = 1;
    if (bVar2) {
                    // WARNING: Could not emulate address calculation at 0x0000f9d4
                    // WARNING: Treating indirect jump as call
      uVar6 = (**(code **)((uint)bVar4 * 4 + 0x3f2df0))();
      return uVar6;
    }
    bRam003e3148 = 2;
  }
  else {
    if (bRam003e3148 < 2) {
      if (bRam003e3148 != 0) {
        return 2;
      }
      func_0x00396e38(0x3e3028,3,0,2,0x7fff,0);
      bRam003e3088 = bRam003e3088 & 0xf1 | 4;
      bRam003e3148 = 1;
      uRam003e3098 = uRam003e3090;
      uRam003e3096 = uRam003e308e;
      goto LAB_0000f99c;
    }
    if (bRam003e3148 != 2) {
      if (bRam003e3148 != 3) {
        return 2;
      }
      goto LAB_0000fdc4;
    }
  }
  if ((bRam003ed480 & 8) != 0) {
    *(undefined *)(bRam003ed3bf + 0x3ed3d0) = 0;
  }
  if (((bRam003ed47c & 2) == 0) && (bRam003e3149 < 5)) {
                    // WARNING: Could not emulate address calculation at 0x0000fbcc
                    // WARNING: Treating indirect jump as call
    uVar6 = (**(code **)((uint)bRam003e3149 * 4 + 0x3f2e10))();
    return uVar6;
  }
LAB_0000fdc4:
  if ((bRam003e3088 & 8) != 0) {
    func_0x003bbc08(uRam003e314c);
  }
  uVar6 = 2;
  if (bRam003e3148 == 3) {
    if ((bRam003ed480 & 8) == 0) {
      uVar6 = 2;
    }
    else {
      if (bRam003e3149 == 2) {
        uVar6 = 1;
      }
      else {
        if (bRam003e3149 != 4) {
          return 2;
        }
        uVar6 = 0;
      }
      bRam003e3088 = bRam003e3088 & 0xf1 | 2;
      bRam003e3148 = 0;
      uRam003e3098 = uRam003e308c;
      bRam003e3149 = 0;
      uRam003e3096 = uRam003e308a;
    }
  }
  return uVar6;
}



void FUN_0000fe90(void)

{
  undefined *puVar1;
  byte bVar2;
  uint uVar3;
  long lVar4;
  undefined auStack112 [16];
  undefined auStack96 [16];
  
  puVar1 = auStack112 + 3;
  uVar3 = (uint)puVar1 & 3;
  *(uint *)(puVar1 + -uVar3) =
       *(uint *)(puVar1 + -uVar3) & -1 << (uVar3 + 1) * 8 | uRam003f2e40 >> (3 - uVar3) * 8;
  auStack112._0_4_ = uRam003f2e40;
  puVar1 = auStack96 + 3;
  uVar3 = (uint)puVar1 & 3;
  *(uint *)(puVar1 + -uVar3) =
       *(uint *)(puVar1 + -uVar3) & -1 << (uVar3 + 1) * 8 | uRam003f2e48 >> (3 - uVar3) * 8;
  auStack96._0_4_ = uRam003f2e48;
  lVar4 = (*pcRam00c000bc)();
  if (lVar4 != 0) {
    func_0x00382ad8(0x10,0x40);
    func_0x00382b60(6,0,0x3f2e50,bRam003e31a0,bRam003e31a1,bRam003e3088,bRam003e30b8);
  }
  if (bRam003e31a0 == 1) {
LAB_00010048:
    bVar2 = bRam003e31a1;
    *(undefined *)(bRam003ed3bf + 0x3ed3d0) = 1;
    if (bVar2 < 6) {
                    // WARNING: Could not emulate address calculation at 0x00010078
                    // WARNING: Treating indirect jump as call
      (**(code **)((uint)bVar2 * 4 + 0x3f2e60))();
      return;
    }
    bRam003e31a0 = 2;
  }
  else {
    if (bRam003e31a0 < 2) {
      if (bRam003e31a0 != 0) {
        return;
      }
      if (bRam003e31a1 == 0) {
        func_0x00396e38(0x3e3028,3,0,2,0x7fff,0);
        if ((bRam003e3088 & 4) == 0) {
          bRam003e3088 = bRam003e3088 & 0xf1 | 4;
          uRam003e3098 = uRam003e3090;
          uRam003e3096 = uRam003e308e;
        }
      }
      else {
        if ((bRam003e31a1 < 6) && (1 < bRam003e31a1)) {
          bRam003e30b8 = bRam003e30b8 & 0xf1 | 4;
          uRam003e30c6 = uRam003e30be;
          uRam003e30c8 = uRam003e30c0;
        }
      }
      goto LAB_00010048;
    }
    if (bRam003e31a0 != 2) {
      if (bRam003e31a0 != 3) {
        return;
      }
      goto LAB_000108f4;
    }
  }
  uVar3 = (uint)bRam003ed441;
  if ((*(byte *)(uVar3 * 0x82c + 0x3ed480) & 8) != 0) {
    *(undefined *)(bRam003ed3bf + 0x3ed3d0) = 0;
  }
  if (((*(byte *)(uVar3 * 0x82c + 0x3ed47c) & 2) == 0) && (bRam003e31a1 < 6)) {
                    // WARNING: Could not emulate address calculation at 0x0001033c
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003e31a1 * 4 + 0x3f2e80))();
    return;
  }
LAB_000108f4:
  if (bRam003ed441 == 0) {
    if ((bRam003e3088 & 8) != 0) {
      bRam003ed47c = bRam003ed47c | 1;
    }
  }
  else {
    if ((bRam003ed441 == 1) && ((bRam003e30b8 & 8) != 0)) {
      bRam003edca8 = bRam003edca8 | 1;
    }
  }
  func_0x003bbc08(uRam003e31a4);
  if ((bRam003e31a0 == 3) && (bRam003e31a1 < 6)) {
                    // WARNING: Could not emulate address calculation at 0x00010980
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003e31a1 * 4 + 0x3f2ea0))();
    return;
  }
  return;
}



// WARNING: Removing unreachable block (ram,0x00010be0)
// WARNING: Removing unreachable block (ram,0x00010ad0)
// WARNING: Removing unreachable block (ram,0x00010a90)
// WARNING: Removing unreachable block (ram,0x00010bb8)
// WARNING: Removing unreachable block (ram,0x00010c2c)

undefined4 FUN_00010a50(uint param_1,uint param_2)

{
  ushort uVar1;
  int iVar2;
  int iVar3;
  undefined4 uVar4;
  uint uVar5;
  int iVar6;
  
  iVar6 = (param_2 & 0xff) * 2;
  iVar2 = func_0x001881e8();
  iVar3 = func_0x001881e8();
  iVar2 = (iVar2 % 10) * (iVar3 % 10);
  if ((int)((((uint)*(ushort *)(iVar6 + 0xc01be8) - iVar2) + -0x1c) * 0x10000) < 1) {
    iVar2 = 0;
    uVar5 = 0;
    do {
      iVar3 = uVar5 * 0x100;
      uVar5 = uVar5 + 1 & 0xffff;
      uVar1 = *(ushort *)((iVar3 + (param_2 & 0xff)) * 2 + 0xc02542);
      iVar2 = (int)((((int)((iVar2 + (uint)(uVar1 >> 8)) * 0x10000) >> 0x10) + (uVar1 & 0xff)) *
                   0x10000) >> 0x10;
    } while (uVar5 < 5);
    if (iVar2 < 0xd) {
      iVar2 = iVar2 * 0x50000 >> 0x10;
    }
    else {
      iVar2 = 0x3c;
    }
    uVar5 = (uint)uRam00c08540;
    iVar3 = func_0x001881e8();
    if ((int)((iVar2 + uVar5 * 10 + 0x1e) * 0x10000) >> 0x10 < iVar3 % 100) {
      return 0;
    }
    iVar2 = func_0x001881e8();
    if ((int)((((uint)*(ushort *)(iVar6 + 0xc03148) - iVar2 % 100) + -0x1c) * 0x10000) < 1) {
      return 3;
    }
    iVar2 = func_0x001881e8();
    uVar1 = *(ushort *)(iVar6 + 0xc02ff2);
  }
  else {
    iVar2 = func_0x001881e8(iVar2,10);
    if ((int)((((uint)*(ushort *)(iVar6 + 0xc03148) - iVar2 % 100) + -0x1c) * 0x10000) < 1) {
      return 3;
    }
    iVar2 = func_0x001881e8();
    uVar1 = *(ushort *)(iVar6 + 0xc02ff2);
  }
  uVar4 = 2;
  if ((0 < ((int)(uVar1 - 0x1c) / 2 - iVar2 % 100) * 0x10000) &&
     (uVar4 = 1, *(ushort *)(iVar6 + 0xc00a8a) <= *(ushort *)((param_1 & 0xff) * 2 + 0xc00a8a))) {
    uVar4 = 2;
  }
  return uVar4;
}



void FUN_00010d40(void)

{
  ushort uVar1;
  char cVar2;
  char cVar3;
  uint uVar4;
  long lVar5;
  int iVar6;
  ushort *puVar7;
  short sVar8;
  short *psVar9;
  uint uVar10;
  
  lVar5 = (*pcRam00c000bc)();
  if (lVar5 != 0) {
    iVar6 = 0;
    if (uRam00c08540 != 0) {
      do {
        iVar6 = (iVar6 + 1) * 0x10000 >> 0x10;
      } while (iVar6 < (int)(uint)uRam00c08540);
    }
    cVar2 = '\0';
    iVar6 = 0;
    if (uRam00c08500 != 0) {
      puVar7 = (ushort *)0xc08518;
      uVar1 = uRam00c08518;
      while( true ) {
        puVar7 = puVar7 + 1;
        if ((uVar1 & 1) != 0) {
          cVar2 = cVar2 + '\x01';
        }
        iVar6 = (iVar6 + 1) * 0x10000 >> 0x10;
        if ((int)(uint)uRam00c08500 <= iVar6) break;
        uVar1 = *puVar7;
      }
    }
    func_0x003978c0(6,0xf0,200,0x40,8,0xffffffffffffffff,0);
    func_0x00382ad8(0xf0,200);
    func_0x00382b60(6,0,0x3f2ec0,cVar2);
  }
  if (bRam003e31bc == 1) {
LAB_000111a0:
    lVar5 = func_0x00392228(uRam00400f2c,uRam00400f2d);
    if (lVar5 != 0) {
      bRam003e31bc = 2;
    }
    return;
  }
  if (bRam003e31bc < 2) {
    if (bRam003e31bc != 0) {
      return;
    }
    uRam003e31b8 = 0;
    uVar10 = 0;
    if (uRam00c08500 != 0) {
      uVar4 = 1;
      uVar1 = uRam00c08502;
      while ((ulong)uVar1 != (long)sRam00c0f240) {
        uVar10 = uVar4 & 0xff;
        if (uRam00c08500 <= uVar10) break;
        uVar4 = uVar10 + 1;
        uVar1 = *(ushort *)(uVar10 * 2 + 0xc08502);
      }
    }
    uRam003e3008 = (undefined2)uVar10;
    uVar4 = 0;
    if ((uRam00c08540 != 0) && ((ulong)uRam00c08542 != (long)sRam00c0f260)) {
      uVar4 = 1;
      while( true ) {
        uVar4 = uVar4 & 0xff;
        if ((uRam00c08540 <= uVar4) ||
           ((ulong)*(ushort *)(uVar4 * 2 + 0xc08542) == (long)sRam00c0f260)) break;
        uVar4 = uVar4 + 1;
      }
    }
    uRam003e3018 = (undefined2)uVar4;
    sRam00c0f242._0_1_ = '\0';
    if (-1 < sRam00c0f242) {
    }
    psVar9 = (short *)(uVar10 * 0x3a + 0x3e2a4a);
    cRam00400f5a = *(char *)psVar9 - (char)sRam00c0f242;
    sVar8 = sRam00c0f242;
    if (sRam00c0f242 < 0) {
      sVar8 = 0;
    }
    *psVar9 = sVar8;
    sRam00c0f262._0_1_ = '\0';
    if (-1 < sRam00c0f262) {
    }
    iVar6 = uVar4 * 0x3a;
    cRam00400f5b = *(char *)(short *)(iVar6 + 0x3e2c92) - (char)sRam00c0f262;
    sVar8 = sRam00c0f262;
    if (sRam00c0f262 < 0) {
      sVar8 = 0;
    }
    *(short *)(iVar6 + 0x3e2c92) = sVar8;
    if (sRam00c0f202 == 0) {
      uRam00c0f200 = 0;
    }
    if (uRam00c0f200 == 0) {
      puVar7 = (ushort *)(uVar4 * 2 + 0xc08558);
      *puVar7 = *puVar7 & 0xfffc;
      uRam00400f2c = *(undefined *)(uVar10 * 0x3a + 0x3e2a38);
      uRam00400f2d = *(undefined *)(iVar6 + 0x3e2c80);
    }
    else {
      puVar7 = (ushort *)(uVar10 * 2 + 0xc08518);
      *puVar7 = *puVar7 & 0xfffc;
      uRam00400f2c = *(undefined *)(iVar6 + 0x3e2c80);
      uRam00400f2d = *(undefined *)(uVar10 * 0x3a + 0x3e2a38);
    }
    if (sRam00c0f202 == 0) {
      puVar7 = (ushort *)(uVar10 * 2 + 0xc08518);
      uVar1 = *puVar7;
      if ((uVar1 & 1) == 0) {
        puVar7 = (ushort *)(uVar4 * 2 + 0xc08558);
        uVar1 = *puVar7;
      }
      *puVar7 = uVar1 & 0xfffc;
    }
    func_0x00391ec0(uRam00400f2c,uRam00400f2d);
    bRam003e31bc = 1;
    goto LAB_000111a0;
  }
  if (bRam003e31bc != 2) {
    if (bRam003e31bc != 3) {
      return;
    }
    if (sRam003ed418 <= sRam003ed41c) {
      return;
    }
    uRam003e305c = (ushort)bRam003e31bd;
    uRam003e3062 = 0xff;
    bRam003e31bc = 0;
    sRam00c084fe = 0xffff;
    sRam00c0853e = 0xffff;
    return;
  }
  if ((uRam00c08540 == 1) || (uRam00c08500 == 1)) {
    uRam003e31b0 = 0xff;
  }
  cVar2 = '\0';
  if (sRam00c084f0 == 0) {
    iVar6 = 0;
    if (uRam00c08540 != 0) {
      puVar7 = (ushort *)0xc08558;
      uVar1 = uRam00c08558;
      while( true ) {
        puVar7 = puVar7 + 1;
        if ((uVar1 & 1) != 0) {
          cVar2 = cVar2 + '\x01';
        }
        iVar6 = (iVar6 + 1) * 0x10000 >> 0x10;
        if ((int)(uint)uRam00c08540 <= iVar6) break;
        uVar1 = *puVar7;
      }
    }
    cVar3 = '\0';
    iVar6 = 0;
    if (uRam00c08500 != 0) {
      puVar7 = (ushort *)0xc08518;
      uVar1 = uRam00c08518;
      while( true ) {
        puVar7 = puVar7 + 1;
        if ((uVar1 & 1) != 0) {
          cVar3 = cVar3 + '\x01';
        }
        iVar6 = (iVar6 + 1) * 0x10000 >> 0x10;
        if ((int)(uint)uRam00c08500 <= iVar6) break;
        uVar1 = *puVar7;
      }
    }
    if (cVar2 != '\0') {
      if (cVar3 != '\0') {
        uRam003e31b0 = 0xff;
        bRam003e31bd = 1;
        goto LAB_000114f4;
      }
      goto LAB_000114d8;
    }
    if (cVar3 == '\0') {
      uRam00c0f100 = 1;
      bRam003e31bd = 0;
      uRam00c0f102 = 1;
      if (sRam00c0f202 == 0) {
        uRam00c0f200 = 1;
        uRam003e31b8 = 1;
        sRam00c0f202 = 1;
      }
      goto LAB_000114f4;
    }
  }
  else {
    iVar6 = 0;
    if (uRam00c08540 != 0) {
      puVar7 = (ushort *)0xc08558;
      uVar1 = uRam00c08558;
      while( true ) {
        puVar7 = puVar7 + 1;
        if ((uVar1 & 1) != 0) {
          cVar2 = cVar2 + '\x01';
        }
        iVar6 = (iVar6 + 1) * 0x10000 >> 0x10;
        if ((int)(uint)uRam00c08540 <= iVar6) break;
        uVar1 = *puVar7;
      }
    }
    cVar3 = '\0';
    iVar6 = 0;
    if (uRam00c08500 != 0) {
      puVar7 = (ushort *)0xc08518;
      uVar1 = uRam00c08518;
      while( true ) {
        puVar7 = puVar7 + 1;
        if ((uVar1 & 1) != 0) {
          cVar3 = cVar3 + '\x01';
        }
        iVar6 = (iVar6 + 1) * 0x10000 >> 0x10;
        if ((int)(uint)uRam00c08500 <= iVar6) break;
        uVar1 = *puVar7;
      }
    }
    if (cVar2 != '\0') {
      if (cVar3 != '\0') {
        uRam003e31b0 = 0xff;
        bRam003e31bd = 1;
        if (sRam00c084fe != -1) {
          bRam003e3010 = bRam003e3010 | 2;
        }
        if (sRam00c0853e != -1) {
          bRam003e3020 = bRam003e3020 | 2;
        }
        goto LAB_000114f4;
      }
LAB_000114d8:
      uRam00c0f102 = 1;
      bRam003e31bd = 0;
      uRam00c0f100 = 1;
      goto LAB_000114f4;
    }
    if (cVar3 == '\0') {
      uRam00c0f102 = (ushort)(sRam00c084f0 == 1);
      uRam00c0f100 = 1;
      bRam003e31bd = 0;
      if (sRam00c0f202 == 0) {
        uRam003e31b8 = 1;
        sRam00c0f202 = 1;
        uRam00c0f200 = uRam00c0f102;
      }
      goto LAB_000114f4;
    }
  }
  uRam00c0f100 = 1;
  bRam003e31bd = 0;
  uRam00c0f102 = 0;
LAB_000114f4:
  iVar6 = 0;
  if (uRam00c08500 != 0) {
    puVar7 = (ushort *)0xc08518;
    do {
      if ((*puVar7 & 1) != 0) {
        *puVar7 = *puVar7 | 2;
      }
      iVar6 = (iVar6 + 1) * 0x10000 >> 0x10;
      puVar7 = puVar7 + 1;
    } while (iVar6 < (int)(uint)uRam00c08500);
  }
  bRam003e31bc = 3;
  return;
}



undefined8 FUN_00011928(void)

{
  undefined8 uVar1;
  
  if (6 < bRam003e31be) {
    bRam003e31be = 0;
    bRam003ed47c = bRam003ed47c & 0xfe;
    func_0x003bbc08(uRam00400f2e);
    return 1;
  }
                    // WARNING: Could not emulate address calculation at 0x00011980
                    // WARNING: Treating indirect jump as call
  uVar1 = (**(code **)((char)bRam003e31be * 4 + 0x3f2ed0))();
  return uVar1;
}



undefined4 FUN_00011e68(void)

{
  undefined4 uVar1;
  
  if (cRam003e31bf == '\x01') {
    uVar1 = 0;
    if ((bRam003ed47c & 2) == 0) {
      bRam003ed480 = bRam003ed480 & 0xf1 | 4;
      cRam003e31bf = '\x02';
      uRam003ed494 = uRam003ed48c;
      uRam003ed492 = uRam003ed48a;
      uVar1 = 0;
    }
  }
  else {
    if (cRam003e31bf < '\x02') {
      uVar1 = 0;
      if (cRam003e31bf == '\0') {
        func_0x00396a98(0x53,0x107ea00,uRam00c00112);
        func_0x003bba50(0,0,0xfffffffffffffea0,0xa0,400,0xa0);
        cRam003e31bf = '\x01';
        bRam003ed47c = bRam003ed47c | 1;
        uVar1 = 0;
      }
    }
    else {
      uVar1 = 0;
      if ((cRam003e31bf == '\x02') && (uVar1 = 0, (bRam003ed480 & 8) != 0)) {
        cRam003e31bf = '\0';
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



undefined4 FUN_00011f90(void)

{
  undefined4 uVar1;
  
  if (cRam003e31c0 == '\0') {
    func_0x00396a98(0x53,0x107ea00,uRam00c00112);
    func_0x003bba50(0,0,0x3c,0xa0,0x3c,0xa0);
    cRam003e31c0 = '\x01';
    bRam003ed47c = bRam003ed47c | 1;
    uVar1 = 0;
  }
  else {
    uVar1 = 0;
    if ((cRam003e31c0 == '\x01') && (uVar1 = 0, (bRam003ed47c & 2) == 0)) {
      bRam003ed480 = bRam003ed480 & 0xf1 | 4;
      cRam003e31c0 = '\0';
      uRam003ed494 = uRam003ed48c;
      uRam003ed492 = uRam003ed48a;
      uVar1 = 1;
    }
  }
  return uVar1;
}



// WARNING: Removing unreachable block (ram,0x000128fc)

undefined8 FUN_00012078(void)

{
  undefined8 uVar1;
  
  do {
  } while (0xb < bRam003e31c1);
                    // WARNING: Could not emulate address calculation at 0x000120d0
                    // WARNING: Treating indirect jump as call
  uVar1 = (**(code **)((char)bRam003e31c1 * 4 + 0x3f2ef0))();
  return uVar1;
}



void FUN_00012930(void)

{
  int iVar1;
  long lVar2;
  uint uVar3;
  
  if (6 < bRam003e31c2) {
    lVar2 = (*pcRam00c000bc)();
    if (lVar2 != 0) {
      func_0x003978c0(3,0xa0,0x10,0x50,100,0xffffffffffffffff,8,1);
      uVar3 = 0;
      iVar1 = 0;
      do {
        iVar1 = (int)((iVar1 + uVar3) * 0x20000 + 0x100000) >> 0x10;
        func_0x00382ad8(0xa8,iVar1);
        func_0x00382b60(3,0,0x3f2f40,*(undefined2 *)(uVar3 * 0xc + 0x3e31c8),
                        *(undefined2 *)(uVar3 * 0xc + 0x3e31ca));
        if (bRam003e31c4 == uVar3) {
          func_0x00382ad8(0xa0,iVar1);
          func_0x00382b60(3,0,0x3f2f48);
        }
        uVar3 = uVar3 + 1 & 0xff;
        iVar1 = uVar3 << 2;
      } while (uVar3 < 10);
    }
    return;
  }
                    // WARNING: Could not emulate address calculation at 0x00012970
                    // WARNING: Treating indirect jump as call
  (**(code **)((uint)bRam003e31c2 * 4 + 0x3f2f50))();
  return;
}



void FUN_000135e0(void)

{
  undefined *puVar1;
  bool bVar2;
  ushort uVar3;
  undefined2 uVar4;
  byte bVar5;
  byte bVar6;
  int iVar7;
  long lVar8;
  undefined8 uVar9;
  ushort *puVar10;
  char cVar11;
  int iVar12;
  int iVar13;
  uint uVar14;
  undefined local_80 [8];
  undefined auStack120 [8];
  undefined auStack112 [32];
  
  lVar8 = (*pcRam00c000bc)();
  uVar3 = uRam002afcb4;
  if (lVar8 != 0) {
    puVar1 = local_80 + 7;
    uVar14 = (uint)puVar1 & 7;
    *(ulong *)(puVar1 + -uVar14) =
         *(ulong *)(puVar1 + -uVar14) & -1 << (uVar14 + 1) * 8 | uRam003f2fd0 >> (7 - uVar14) * 8;
    local_80 = uRam003f2fd0;
    puVar1 = auStack120 + 7;
    uVar14 = (uint)puVar1 & 7;
    *(ulong *)(puVar1 + -uVar14) =
         *(ulong *)(puVar1 + -uVar14) & -1 << (uVar14 + 1) * 8 | uRam003f2fd8 >> (7 - uVar14) * 8;
    auStack120 = uRam003f2fd8;
    puVar1 = auStack112 + 7;
    uVar14 = (uint)puVar1 & 7;
    *(ulong *)(puVar1 + -uVar14) =
         *(ulong *)(puVar1 + -uVar14) & -1 << (uVar14 + 1) * 8 | uRam003f2fe0 >> (7 - uVar14) * 8;
    auStack112._0_8_ = uRam003f2fe0;
    if (bRam00400fda < 6) {
                    // WARNING: Could not emulate address calculation at 0x00013678
                    // WARNING: Treating indirect jump as call
      (**(code **)((uint)bRam00400fda * 4 + 0x3f3070))();
      return;
    }
    if (5 < iRam003ecf54) {
      cVar11 = '\x01';
      if ((uRam002afcb4 & 0x80) != 0) {
        cVar11 = '\x04';
      }
      if ((uRam002afcb4 & 0x400) != 0) {
        bRam00400fdb = cVar11 + bRam00400fdb;
      }
      if ((uRam002afcb4 & 0x100) != 0) {
        bRam00400fdb = bRam00400fdb - cVar11;
      }
      if ((uRam002afcb4 & 0x40) != 0) {
        bRam00400fdc = cVar11 + bRam00400fdc;
      }
      if ((uRam002afcb4 & 0x20) != 0) {
        bRam00400fdc = bRam00400fdc - cVar11;
      }
      iRam003ecf54 = 0;
    }
    if ((uRam002afcb4 & 0x200) != 0) {
      *(byte *)((uint)uRam00c084fe * 0x50 + 0x3e34c9) =
           *(byte *)((uint)uRam00c084fe * 0x50 + 0x3e34c9) | 2;
      *(undefined *)((uint)uRam00c084fe * 0x50 + 0x3e34d4) = 0;
      *(undefined2 *)((uint)uRam00c084fe * 0x50 + 0x3e34d6) = 0;
      *(undefined *)((uint)uRam00c084fe * 0x50 + 0x3e34d4) = 0;
      *(undefined2 *)((uint)uRam00c084fe * 0x50 + 0x3e34d6) = 0;
      *(byte *)((uint)uRam00c0853e * 0x50 + 0x3e34c9) =
           *(byte *)((uint)uRam00c0853e * 0x50 + 0x3e34c9) | 2;
      *(undefined *)((uint)uRam00c0853e * 0x50 + 0x3e34d4) = 0;
      *(undefined2 *)((uint)uRam00c0853e * 0x50 + 0x3e34d6) = 0;
      *(undefined *)((uint)uRam00c0853e * 0x50 + 0x3e34d4) = 0;
      *(undefined2 *)((uint)uRam00c0853e * 0x50 + 0x3e34d6) = 0;
    }
    if ((uVar3 & 0x10) != 0) {
      *(byte *)((uint)uRam00c084fe * 0x50 + 0x3e34c9) =
           *(byte *)((uint)uRam00c084fe * 0x50 + 0x3e34c9) & 0xfd;
      *(undefined *)((uint)uRam00c084fe * 0x50 + 0x3e34d4) = 0;
      *(undefined2 *)((uint)uRam00c084fe * 0x50 + 0x3e34d6) = 0;
      *(byte *)((uint)uRam00c0853e * 0x50 + 0x3e34c9) =
           *(byte *)((uint)uRam00c0853e * 0x50 + 0x3e34c9) & 0xfd;
      *(undefined *)((uint)uRam00c0853e * 0x50 + 0x3e34d4) = 0;
      *(undefined2 *)((uint)uRam00c0853e * 0x50 + 0x3e34d6) = 0;
    }
    func_0x003978c0(6,0x60,0x30,0x50,8,0xffffffffffffffff,2,1);
    func_0x00382ad8(0x68,0x30);
    func_0x00382b60(6,0,0x3f2fe8,bRam00400fdb,bRam00400fdc);
    uVar3 = uRam002afcb4;
    if ((uRam002afcb4 & 0x800) != 0) {
      *(undefined2 *)((uint)uRam00c084fe * 0x50 + 0x3e34cc) = uRam00400fb6;
      bVar6 = bRam00400fdc;
      bVar5 = bRam00400fdb;
      bRam003e6d08 = 0;
      *(undefined2 *)((uint)uRam00c0853e * 0x50 + 0x3e34ca) = uRam00400fce;
      uRam00c084fe = (ushort)bVar5;
      uRam00c0853e = (ushort)bVar6;
    }
    if (4 < bRam00400fdd) {
      if ((uVar3 & 8) == 0) {
        bRam00400fdd = 0;
      }
      else {
        bVar2 = 4 < bRam00400fda;
        bRam00400fda = bRam00400fda + 1;
        if (bVar2) {
          bRam00400fda = 0;
        }
        bRam00400fdd = 0;
      }
    }
    bRam00400fdd = bRam00400fdd + 1;
    func_0x003978c0(6,100,0x10,0x60,8,0xffffffffffffffff,2,0);
    func_0x00382ad8(100,0x10);
    uVar9 = func_0x001383d0(*(undefined4 *)(local_80 + (uint)bRam00400fda * 4));
    func_0x00382b60(6,0,0x3f3018,uVar9);
    func_0x003978c0(6,0x50,0x38,0xa0,8,0xffffffffffffffff,2,0);
    func_0x00382ad8(0x50,0x38);
    func_0x00382b60(6,0,0x3f3020,uRam00400fbe,uRam00400fc0,uRam00400fd6,uRam00400fd8);
    uRam00400ff0 = *(undefined *)((uint)uRam00c084fe * 2 + 0xc04552);
    uRam00400ff1 = *(undefined *)((uint)uRam00c0853e * 2 + 0xc04552);
    func_0x003978c0(6,0x60,0x28,0x70,8,0xffffffffffffffff,2,1);
    func_0x00382ad8(0x60,0x28);
    func_0x00382b60(6,0,0x3f3038,uRam00400ff0,uRam00400ff1);
  }
  if (bRam003e6d08 < 8) {
                    // WARNING: Could not emulate address calculation at 0x000142b0
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003e6d08 * 4 + 0x3f3090))();
    return;
  }
  if (((bRam003e3088 & 8) != 0) && ((bRam003e30b8 & 8) != 0)) {
    func_0x003bbc08();
    uVar14 = (uint)(byte)uRam00c0853e;
    lVar8 = func_0x001251e8();
    if (lVar8 == 0) {
      lVar8 = func_0x00125258();
      if (lVar8 != 0) {
        uVar14 = 0x74;
        func_0x00125400();
      }
    }
    else {
      uVar14 = 0x74;
      func_0x00125348();
    }
    puVar10 = (ushort *)(uVar14 * 4 + 0x3f24b2);
    func_0x00383da0(1,0,0xc88,0x1b84,*(ushort *)(uVar14 * 4 + 0x3f24b0) + 0x15080 >> 3,*puVar10,
                    (0x28 - ((*puVar10 & 0xf00) >> 6)) + (uint)uRam00400fa6 & 0xffff,sRam00400fa8);
    lVar8 = func_0x001251e8();
    if (lVar8 == 0) {
      lVar8 = func_0x00125258();
      iVar7 = (uint)(byte)uRam00c0853e << 1;
      if (lVar8 == 0) {
        puVar10 = (ushort *)((uint)uRam00c0853c * 2 + 0xc00286);
        iVar7 = (uint)uRam00c0853c * 4;
        if (*puVar10 < 8) {
          iVar7 = (uint)*puVar10 * 4;
          puVar10 = (ushort *)(iVar7 + 0x3f2762);
          iVar12 = (*puVar10 & 0xf00) << 0xb;
          iVar13 = 0x28 - (iVar12 >> 0x11);
          func_0x00383da0(1,0,0xc88,0x1b84,*(ushort *)(iVar7 + 0x3f2760) + 0x24f00 >> 3,*puVar10,
                          iVar13 + (uint)uRam00400fa6 & 0xffff,sRam00400fa8 + 0x10);
        }
        else {
          puVar10 = (ushort *)(iVar7 + 0x3f24b2);
          iVar12 = (*puVar10 & 0xf00) << 0xb;
          iVar13 = 0x28 - (iVar12 >> 0x11);
          func_0x00383da0(1,0,0xc88,0x1b84,*(ushort *)(iVar7 + 0x3f24b0) + 0x15080 >> 3,*puVar10,
                          iVar13 + (uint)uRam00400fa6 & 0xffff,sRam00400fa8 + 0x10);
        }
        func_0x00383da0(1,0,0xc88,0x1b84,0x4e40,0x210,
                        (iVar12 >> 0x10) + iVar13 + (uint)uRam00400fa6 & 0xffff,sRam00400fa8 + 0x10)
        ;
        iVar7 = (uint)(byte)uRam00c0853e << 1;
      }
    }
    else {
      iVar7 = (uint)(byte)uRam00c0853e << 1;
    }
    if (*(ushort *)(iVar7 + 0xc00934) != 0) {
      func_0x00383da0(1,0,0xc88,0x1b84,0x4e88,0x610,uRam00400fa6,sRam00400fa8 + 0x58);
      iVar7 = (uint)*(ushort *)(iVar7 + 0xc00934) * 4;
      func_0x00383da0(1,0,0xc88,0x1b84,*(ushort *)(iVar7 + 0x3f27bc) + 0x25f40 >> 3,
                      *(undefined2 *)(iVar7 + 0x3f27be),uRam00400fa6 + 0x40,sRam00400fa8 + 0x58);
    }
    func_0x00397e80(0x400f98,4);
    iVar7 = (uint)(byte)uRam00c084fe * 4;
    puVar10 = (ushort *)(iVar7 + 0x3f24b2);
    func_0x00383da0(1,0,0xc88,0x1b84,*(ushort *)(iVar7 + 0x3f24b0) + 0x15080 >> 3,*puVar10,
                    (0x28 - ((*puVar10 & 0xf00) >> 6)) + (uint)uRam00400f8e & 0xffff,sRam00400f90);
    puVar10 = (ushort *)((uint)uRam00c084fc * 2 + 0xc00286);
    iVar7 = (uint)uRam00c084fc * 4;
    if (*puVar10 < 8) {
      iVar7 = (uint)*puVar10 * 4;
      puVar10 = (ushort *)(iVar7 + 0x3f2762);
      iVar12 = (*puVar10 & 0xf00) << 0xb;
      iVar13 = 0x28 - (iVar12 >> 0x11);
      func_0x00383da0(1,0,0xc88,0x1b84,*(ushort *)(iVar7 + 0x3f2760) + 0x24f00 >> 3,*puVar10,
                      iVar13 + (uint)uRam00400f8e & 0xffff,sRam00400f90 + 0x10);
    }
    else {
      puVar10 = (ushort *)(iVar7 + 0x3f24b2);
      iVar12 = (*puVar10 & 0xf00) << 0xb;
      iVar13 = 0x28 - (iVar12 >> 0x11);
      func_0x00383da0(1,0,0xc88,0x1b84,*(ushort *)(iVar7 + 0x3f24b0) + 0x15080 >> 3,*puVar10,
                      iVar13 + (uint)uRam00400f8e & 0xffff,sRam00400f90 + 0x10);
    }
    func_0x00383da0(1,0,0xc88,0x1b84,0x4e40,0x210,
                    (iVar12 >> 0x10) + iVar13 + (uint)uRam00400f8e & 0xffff,sRam00400f90 + 0x10);
    puVar10 = (ushort *)((uint)(byte)uRam00c084fe * 2 + 0xc00934);
    if (*puVar10 != 0) {
      func_0x00383da0(1,0,0xc88,0x1b84,0x4e88,0x610,uRam00400f8e,sRam00400f90 + 0x58);
      iVar7 = (uint)*puVar10 * 4;
      func_0x00383da0(1,0,0xc88,0x1b84,*(ushort *)(iVar7 + 0x3f27bc) + 0x25f40 >> 3,
                      *(undefined2 *)(iVar7 + 0x3f27be),uRam00400f8e + 0x40,sRam00400f90 + 0x58);
    }
    func_0x00397e80(0x400f80,4);
    uVar4 = uRam00400fd6;
    *(undefined2 *)((uint)uRam00c084fe * 0x50 + 0x3e34cc) = uRam00400fbe;
    *(undefined2 *)((uint)uRam00c0853e * 0x50 + 0x3e34ca) = uVar4;
    func_0x003842c8();
    func_0x00397e80(0x400fb0,4);
    func_0x00397e80(0x400fc8,4);
  }
  return;
}



void FUN_00015270(void)

{
  long lVar1;
  
  lVar1 = (*pcRam00c000bc)();
  if (lVar1 != 0) {
    func_0x003978c0(6,0x10,0x10,0x10,0x10,0xffffffffffffffff,1,1);
    func_0x00382ad8(0x10,0x10);
    func_0x00382b60(6,1,0x3f30b0,bRam003e6d0a);
  }
  if (5 < bRam003e6d0a) {
    return;
  }
                    // WARNING: Could not emulate address calculation at 0x00015310
                    // WARNING: Treating indirect jump as call
  (**(code **)((uint)bRam003e6d0a * 4 + 0x3f30c0))();
  return;
}



void FUN_00015748(short param_1)

{
  uint uVar1;
  uint uVar2;
  undefined2 uVar3;
  short sVar4;
  ulong *puVar5;
  int iVar6;
  undefined8 *puVar7;
  int iVar8;
  undefined8 *puVar9;
  undefined *puVar10;
  int iVar11;
  ulong in_t0;
  ulong uVar12;
  ulong in_t1;
  ulong uVar13;
  ulong in_t2;
  ulong uVar14;
  uint uVar15;
  int iVar16;
  ulong uVar17;
  
  if (param_1 == 0x427e) {
    puVar10 = (undefined *)((sRam003e3008 * 0x1c + (int)sRam003e3008) * 2 + 0x3e2a38);
    func_0x00389758(puVar10,*puVar10);
    uVar17 = (ulong)(byte)sRam003e3008;
    if ((long)uVar17 < (long)sRam003e300a) {
      uVar15 = (uint)(byte)sRam003e3008;
      do {
        iVar16 = (int)uVar17;
        iVar11 = iVar16 * 2;
        *(undefined2 *)(iVar11 + 0xc08502) = *(undefined2 *)(iVar11 + 0xc08504);
        iVar6 = (uVar15 * 0x20 + iVar16 * -4 + iVar16) * 2;
        *(ushort *)(iVar11 + 0xc08518) = *(ushort *)(iVar11 + 0xc0851a);
        uVar1 = iVar6 + 0x3e2a79U & 7;
        uVar2 = iVar6 + 0x3e2a72U & 7;
        uVar17 = (*(long *)((iVar6 + 0x3e2a79U) - uVar1) << (7 - uVar1) * 8 |
                 (ulong)*(ushort *)(iVar11 + 0xc0851a) & 0xffffffffffffffffU >> (uVar1 + 1) * 8) &
                 -1 << (8 - uVar2) * 8 | *(ulong *)((iVar6 + 0x3e2a72U) - uVar2) >> uVar2 * 8;
        uVar1 = iVar6 + 0x3e2a81U & 7;
        uVar2 = iVar6 + 0x3e2a7aU & 7;
        uVar12 = (*(long *)((iVar6 + 0x3e2a81U) - uVar1) << (7 - uVar1) * 8 |
                 in_t0 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((iVar6 + 0x3e2a7aU) - uVar2) >> uVar2 * 8;
        uVar1 = iVar6 + 0x3e2a89U & 7;
        uVar2 = iVar6 + 0x3e2a82U & 7;
        uVar13 = (*(long *)((iVar6 + 0x3e2a89U) - uVar1) << (7 - uVar1) * 8 |
                 in_t1 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((iVar6 + 0x3e2a82U) - uVar2) >> uVar2 * 8;
        uVar1 = iVar6 + 0x3e2a91U & 7;
        uVar2 = iVar6 + 0x3e2a8aU & 7;
        uVar14 = (*(long *)((iVar6 + 0x3e2a91U) - uVar1) << (7 - uVar1) * 8 |
                 in_t2 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((iVar6 + 0x3e2a8aU) - uVar2) >> uVar2 * 8;
        uVar1 = iVar6 + 0x3e2a3fU & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a3fU) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar17 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2a38U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a38U) - uVar1);
        *puVar5 = uVar17 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2a47U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a47U) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar12 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2a40U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a40U) - uVar1);
        *puVar5 = uVar12 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2a4fU & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a4fU) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar13 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2a48U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a48U) - uVar1);
        *puVar5 = uVar13 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2a57U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a57U) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar14 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2a50U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a50U) - uVar1);
        *puVar5 = uVar14 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2a99U & 7;
        uVar2 = iVar6 + 0x3e2a92U & 7;
        uVar17 = (*(long *)((iVar6 + 0x3e2a99U) - uVar1) << (7 - uVar1) * 8 |
                 uVar17 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((iVar6 + 0x3e2a92U) - uVar2) >> uVar2 * 8;
        uVar1 = iVar6 + 0x3e2aa1U & 7;
        uVar2 = iVar6 + 0x3e2a9aU & 7;
        uVar12 = (*(long *)((iVar6 + 0x3e2aa1U) - uVar1) << (7 - uVar1) * 8 |
                 uVar12 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((iVar6 + 0x3e2a9aU) - uVar2) >> uVar2 * 8;
        uVar1 = iVar6 + 0x3e2aa9U & 7;
        uVar2 = iVar6 + 0x3e2aa2U & 7;
        uVar13 = (*(long *)((iVar6 + 0x3e2aa9U) - uVar1) << (7 - uVar1) * 8 |
                 uVar13 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((iVar6 + 0x3e2aa2U) - uVar2) >> uVar2 * 8;
        uVar3 = *(undefined2 *)(iVar6 + 0x3e2aaa);
        uVar1 = iVar6 + 0x3e2a5fU & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a5fU) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar17 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2a58U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a58U) - uVar1);
        *puVar5 = uVar17 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2a67U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a67U) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar12 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2a60U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a60U) - uVar1);
        *puVar5 = uVar12 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2a6fU & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a6fU) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar13 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2a68U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2a68U) - uVar1);
        *puVar5 = uVar13 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        iVar8 = (uVar15 * 8 + iVar16) * 0x100;
        *(undefined2 *)(iVar6 + 0x3e2a70) = uVar3;
        puVar7 = (undefined8 *)(iVar8 + 0x1073600);
        puVar9 = (undefined8 *)(iVar8 + 0x1073f00);
        do {
          in_t0 = puVar9[1];
          in_t1 = puVar9[2];
          in_t2 = puVar9[3];
          *puVar7 = *puVar9;
          puVar7[1] = in_t0;
          puVar7[2] = in_t1;
          puVar7[3] = in_t2;
          puVar9 = puVar9 + 4;
          puVar7 = puVar7 + 4;
        } while (puVar9 != (undefined8 *)(iVar8 + 0x1074800));
        func_0x001247a0((uVar15 * 8 + iVar16) * 0x20 - 0x1940 & 0xffe0,
                        *(undefined2 *)(iVar11 + 0xc08504));
        uVar17 = (long)(iVar16 + 1) & 0xff;
        uVar15 = (uint)uVar17;
      } while ((long)uVar17 < (long)sRam003e300a);
    }
    iVar6 = 0x3e3008;
    sRam00c08500 = sRam003e300a;
    sVar4 = sRam003e300a;
  }
  else {
    puVar10 = (undefined *)((sRam003e3018 * 0x1c + (int)sRam003e3018) * 2 + 0x3e2c80);
    func_0x00389758(puVar10,*puVar10);
    uVar17 = (ulong)(byte)sRam003e3018;
    if ((long)uVar17 < (long)sRam003e301a) {
      uVar15 = (uint)(byte)sRam003e3018;
      do {
        iVar16 = (int)uVar17;
        iVar11 = iVar16 * 2;
        *(undefined2 *)(iVar11 + 0xc08542) = *(undefined2 *)(iVar11 + 0xc08544);
        iVar6 = (uVar15 * 0x20 + iVar16 * -4 + iVar16) * 2;
        *(ushort *)(iVar11 + 0xc08558) = *(ushort *)(iVar11 + 0xc0855a);
        uVar1 = iVar6 + 0x3e2cc1U & 7;
        uVar2 = iVar6 + 0x3e2cbaU & 7;
        uVar17 = (*(long *)((iVar6 + 0x3e2cc1U) - uVar1) << (7 - uVar1) * 8 |
                 (ulong)*(ushort *)(iVar11 + 0xc0855a) & 0xffffffffffffffffU >> (uVar1 + 1) * 8) &
                 -1 << (8 - uVar2) * 8 | *(ulong *)((iVar6 + 0x3e2cbaU) - uVar2) >> uVar2 * 8;
        uVar1 = iVar6 + 0x3e2cc9U & 7;
        uVar2 = iVar6 + 0x3e2cc2U & 7;
        uVar12 = (*(long *)((iVar6 + 0x3e2cc9U) - uVar1) << (7 - uVar1) * 8 |
                 in_t0 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((iVar6 + 0x3e2cc2U) - uVar2) >> uVar2 * 8;
        uVar1 = iVar6 + 0x3e2cd1U & 7;
        uVar2 = iVar6 + 0x3e2ccaU & 7;
        uVar13 = (*(long *)((iVar6 + 0x3e2cd1U) - uVar1) << (7 - uVar1) * 8 |
                 in_t1 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((iVar6 + 0x3e2ccaU) - uVar2) >> uVar2 * 8;
        uVar1 = iVar6 + 0x3e2cd9U & 7;
        uVar2 = iVar6 + 0x3e2cd2U & 7;
        uVar14 = (*(long *)((iVar6 + 0x3e2cd9U) - uVar1) << (7 - uVar1) * 8 |
                 in_t2 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((iVar6 + 0x3e2cd2U) - uVar2) >> uVar2 * 8;
        uVar1 = iVar6 + 0x3e2c87U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2c87U) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar17 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2c80U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2c80U) - uVar1);
        *puVar5 = uVar17 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2c8fU & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2c8fU) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar12 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2c88U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2c88U) - uVar1);
        *puVar5 = uVar12 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2c97U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2c97U) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar13 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2c90U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2c90U) - uVar1);
        *puVar5 = uVar13 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2c9fU & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2c9fU) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar14 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2c98U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2c98U) - uVar1);
        *puVar5 = uVar14 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2ce1U & 7;
        uVar2 = iVar6 + 0x3e2cdaU & 7;
        uVar17 = (*(long *)((iVar6 + 0x3e2ce1U) - uVar1) << (7 - uVar1) * 8 |
                 uVar17 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((iVar6 + 0x3e2cdaU) - uVar2) >> uVar2 * 8;
        uVar1 = iVar6 + 0x3e2ce9U & 7;
        uVar2 = iVar6 + 0x3e2ce2U & 7;
        uVar12 = (*(long *)((iVar6 + 0x3e2ce9U) - uVar1) << (7 - uVar1) * 8 |
                 uVar12 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((iVar6 + 0x3e2ce2U) - uVar2) >> uVar2 * 8;
        uVar1 = iVar6 + 0x3e2cf1U & 7;
        uVar2 = iVar6 + 0x3e2ceaU & 7;
        uVar13 = (*(long *)((iVar6 + 0x3e2cf1U) - uVar1) << (7 - uVar1) * 8 |
                 uVar13 & 0xffffffffffffffffU >> (uVar1 + 1) * 8) & -1 << (8 - uVar2) * 8 |
                 *(ulong *)((iVar6 + 0x3e2ceaU) - uVar2) >> uVar2 * 8;
        uVar3 = *(undefined2 *)(iVar6 + 0x3e2cf2);
        uVar1 = iVar6 + 0x3e2ca7U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2ca7U) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar17 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2ca0U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2ca0U) - uVar1);
        *puVar5 = uVar17 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2cafU & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2cafU) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar12 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2ca8U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2ca8U) - uVar1);
        *puVar5 = uVar12 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2cb7U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2cb7U) - uVar1);
        *puVar5 = *puVar5 & -1 << (uVar1 + 1) * 8 | uVar13 >> (7 - uVar1) * 8;
        uVar1 = iVar6 + 0x3e2cb0U & 7;
        puVar5 = (ulong *)((iVar6 + 0x3e2cb0U) - uVar1);
        *puVar5 = uVar13 << uVar1 * 8 | *puVar5 & 0xffffffffffffffffU >> (8 - uVar1) * 8;
        iVar8 = (uVar15 * 8 + iVar16) * 0x100;
        *(undefined2 *)(iVar6 + 0x3e2cb8) = uVar3;
        puVar7 = (undefined8 *)(iVar8 + 0x1079000);
        puVar9 = (undefined8 *)(iVar8 + 0x1079900);
        do {
          in_t0 = puVar9[1];
          in_t1 = puVar9[2];
          in_t2 = puVar9[3];
          *puVar7 = *puVar9;
          puVar7[1] = in_t0;
          puVar7[2] = in_t1;
          puVar7[3] = in_t2;
          puVar9 = puVar9 + 4;
          puVar7 = puVar7 + 4;
        } while (puVar9 != (undefined8 *)(iVar8 + 0x107a200));
        func_0x001247a0((uVar15 * 8 + iVar16) * 0x20 - 0xe00 & 0xffe0,
                        *(undefined2 *)(iVar11 + 0xc08544));
        uVar17 = (long)(iVar16 + 1) & 0xff;
        uVar15 = (uint)uVar17;
      } while ((long)uVar17 < (long)sRam003e301a);
    }
    iVar6 = 0x3e3018;
    sRam00c08540 = sRam003e301a;
    sVar4 = sRam003e301a;
  }
  *(ushort *)(iVar6 + 2) = sVar4 - (ushort)(0 < sVar4);
  return;
}



void FUN_00015b20(void)

{
  ushort uVar1;
  ushort uVar2;
  int iVar3;
  ushort *puVar4;
  uint uVar5;
  
  puVar4 = (ushort *)(sRam003e3008 * 2 + 0xc08518);
  *puVar4 = *puVar4 | 4;
  puVar4 = (ushort *)(sRam003e3018 * 2 + 0xc08558);
  *puVar4 = *puVar4 | 4;
  func_0x00389878((undefined)sRam003e3008,(undefined)sRam003e3018);
  if ((cRam003e31b0 == '\x03') && (sRam00c084fe == sRam00c084fc)) {
    uRam00c0f248 = 0;
    uRam00c0f24a = 0;
  }
  uVar5 = 0;
  uRam00c0f25c = 0;
  if (uRam00c08500 != 0) {
    iVar3 = 0;
    do {
      if ((*(ushort *)(iVar3 + 0xc08518) & 1) != 0) {
        uRam00c0f25c = uRam00c0f25c + 1 & 0xff;
      }
      uVar5 = uVar5 + 1 & 0xff;
      iVar3 = uVar5 << 1;
    } while (uVar5 < uRam00c08500);
  }
  if ((cRam003e2a30 == -1) ||
     (uRam00c0f20e = uRam00c0f25c, uVar1 = uRam00c0f25c, uVar2 = uRam00c0f25c, uRam00c0f25c != 1)) {
    uRam00c0f20e = 0;
    uVar1 = uRam00c0f25a;
    uVar2 = uRam00c0f27a;
  }
  uRam00c0f27a = uVar2;
  uRam00c0f25a = uVar1;
  uVar5 = 0;
  uRam00c0f27c = 0;
  if (uRam00c08540 != 0) {
    iVar3 = 0;
    uRam00c0f27c = 0;
    do {
      if ((*(ushort *)(iVar3 + 0xc08558) & 1) != 0) {
        uRam00c0f27c = uRam00c0f27c + 1 & 0xff;
      }
      uVar5 = uVar5 + 1 & 0xff;
      iVar3 = uVar5 << 1;
    } while (uVar5 < uRam00c08540);
  }
  uRam00c0f100 = 0;
  return;
}



void FUN_00015ce0(void)

{
  undefined *puVar1;
  ulong *puVar2;
  uint uVar3;
  uint uVar4;
  undefined local_d0 [4];
  undefined local_cc;
  undefined local_cb;
  undefined local_ca;
  undefined local_c9;
  undefined4 local_c4 [33];
  undefined auStack64 [9];
  undefined local_37;
  undefined auStack48 [8];
  undefined8 uStack40;
  
  puVar1 = auStack48 + 7;
  uVar3 = (uint)puVar1 & 7;
  *(ulong *)(puVar1 + -uVar3) =
       *(ulong *)(puVar1 + -uVar3) & -1 << (uVar3 + 1) * 8 | uRam003f30d8 >> (7 - uVar3) * 8;
  auStack48 = uRam003f30d8;
  uVar3 = (int)&uStack40 + 7U & 7;
  puVar2 = (ulong *)(((int)&uStack40 + 7U) - uVar3);
  *puVar2 = *puVar2 & -1 << (uVar3 + 1) * 8 | uRam003f30e0 >> (7 - uVar3) * 8;
  uStack40 = uRam003f30e0;
  func_0x00143ed0(auStack64);
  local_37 = 1;
  func_0x00143f50(auStack64);
  func_0x00143ef8(local_d0);
  local_cb = 1;
  local_d0[0] = 0;
  local_cc = 0;
  local_ca = 1;
  local_c9 = 0;
  uVar3 = 0;
  do {
    uVar4 = uVar3 + 1 & 0xffff;
    local_c4[uVar3] = 0x1140000;
    uVar3 = uVar4;
  } while (uVar4 < 0x10);
  func_0x001445e0(4,local_d0);
  local_cb = 1;
  uVar3 = 0;
  do {
    uVar4 = uVar3 + 1 & 0xffff;
    local_c4[uVar3] = 0x1160000;
    uVar3 = uVar4;
  } while (uVar4 < 0x10);
  func_0x001445e0(8,local_d0);
  func_0x00144e48(auStack48);
  func_0x001475c0(8,4);
  func_0x001475c0(4,5);
  (*pcRam00c00008)(0x3f30e8);
  (*pcRam00c0000c)(*(undefined4 *)((uRam00c00110 & 7) * 4 + 0x3ee4d8),0x1140000);
  func_0x00147198(4,0,0x100,0x100fee0);
  uRam0020d084 = 1;
  uRam0020d160 = uRam0020d160 | 0x101;
  return;
}



void FUN_00015e80(void)

{
  undefined *puVar1;
  ulong *puVar2;
  uint uVar3;
  uint uVar4;
  undefined local_d0 [4];
  undefined local_cc;
  undefined local_cb;
  undefined local_ca;
  undefined local_c9;
  undefined4 local_c4 [33];
  undefined auStack64 [9];
  undefined local_37;
  undefined auStack48 [8];
  undefined8 uStack40;
  
  puVar1 = auStack48 + 7;
  uVar3 = (uint)puVar1 & 7;
  *(ulong *)(puVar1 + -uVar3) =
       *(ulong *)(puVar1 + -uVar3) & -1 << (uVar3 + 1) * 8 | uRam003f30f0 >> (7 - uVar3) * 8;
  auStack48 = uRam003f30f0;
  uVar3 = (int)&uStack40 + 7U & 7;
  puVar2 = (ulong *)(((int)&uStack40 + 7U) - uVar3);
  *puVar2 = *puVar2 & -1 << (uVar3 + 1) * 8 | uRam003f30f8 >> (7 - uVar3) * 8;
  uStack40 = uRam003f30f8;
  func_0x00143ed0(auStack64);
  local_37 = 1;
  func_0x00143f50(auStack64);
  func_0x00143ef8(local_d0);
  local_cb = 1;
  local_d0[0] = 0;
  local_cc = 0;
  local_ca = 1;
  local_c9 = 0;
  uVar3 = 0;
  do {
    uVar4 = uVar3 + 1 & 0xffff;
    local_c4[uVar3] = 0x1140000;
    uVar3 = uVar4;
  } while (uVar4 < 0x10);
  func_0x001445e0(4,local_d0);
  local_cb = 1;
  uVar3 = 0;
  do {
    uVar4 = uVar3 + 1 & 0xffff;
    local_c4[uVar3] = 0x1160000;
    uVar3 = uVar4;
  } while (uVar4 < 0x10);
  func_0x001445e0(8,local_d0);
  func_0x00144e48(auStack48);
  func_0x001475c0(8,4);
  func_0x001475c0(4,5);
  (*pcRam00c00008)(0x3f30e8);
  (*pcRam00c0000c)();
  func_0x00147198(8,0x100,0x100,0x10100e0);
  uRam0020d084 = 1;
  uRam0020d160 = uRam0020d160 | 0x303;
  uRam0020d16c = 0x100;
  return;
}



// WARNING: Could not reconcile some variable overlaps

void FUN_00016018(void)

{
  undefined *puVar1;
  ulong *puVar2;
  uint uVar3;
  uint uVar4;
  undefined4 uVar5;
  undefined local_d0 [4];
  undefined local_cc;
  undefined local_cb;
  undefined local_ca;
  undefined local_c9;
  undefined4 local_c4 [33];
  undefined auStack64 [8];
  undefined local_38;
  undefined local_37;
  undefined auStack48 [8];
  undefined8 local_28;
  
  puVar1 = auStack48 + 7;
  uVar3 = (uint)puVar1 & 7;
  *(ulong *)(puVar1 + -uVar3) =
       *(ulong *)(puVar1 + -uVar3) & -1 << (uVar3 + 1) * 8 | uRam003f3108 >> (7 - uVar3) * 8;
  auStack48 = uRam003f3108;
  uVar3 = (int)&local_28 + 7U & 7;
  puVar2 = (ulong *)(((int)&local_28 + 7U) - uVar3);
  *puVar2 = *puVar2 & -1 << (uVar3 + 1) * 8 | uRam003f3110 >> (7 - uVar3) * 8;
  local_28 = uRam003f3110;
  if (sRam00c084ee == 0) {
    uVar5 = 0x1140000;
    local_28 = CONCAT62((int6)(uRam003f3110 >> 0x10),0x44ff);
  }
  else {
    uVar5 = 0x1120000;
    auStack48._0_6_ = CONCAT24(0x44ff,(int)uRam003f3108);
    auStack48 = uRam003f3108 & 0xffff000000000000 | (ulong)auStack48._0_6_;
  }
  func_0x00143ed0(auStack64);
  if (sRam00c084ee == 0) {
    local_37 = 1;
  }
  else {
    local_38 = 1;
  }
  func_0x00143f50(auStack64);
  func_0x00143ef8(local_d0);
  local_d0[0] = 0;
  func_0x001445e0(8);
  local_cb = 1;
  local_cc = 0;
  local_ca = 1;
  local_c9 = 0;
  uVar3 = 0;
  do {
    uVar4 = uVar3 + 1 & 0xffff;
    local_c4[uVar3] = uVar5;
    uVar3 = uVar4;
  } while (uVar4 < 0x10);
  func_0x001445e0(4,local_d0);
  func_0x00144e48(auStack48);
  func_0x001475c0(4,5);
  (*pcRam00c00008)(0x3f30e8);
  (*pcRam00c0000c)(0x3f3118,uVar5);
  func_0x00147198(4,0,0x100,0x100fee0);
  uRam0020d084 = 1;
  uRam0020d160 = 0x101;
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00016198(char param_1,ulong *param_2,short param_3)

{
  bool bVar1;
  ulong uVar2;
  ulong *puVar3;
  ulong *puVar4;
  long lVar5;
  ulong *puVar6;
  ulong uVar7;
  ulong uVar8;
  ulong uVar9;
  long lVar10;
  uint uVar11;
  
  lVar10 = (long)(int)param_3;
  if ((0xaa < lVar10) && (lVar10 != 0x7fff)) {
    lVar10 = 0x6e;
    sRam00c0853a = param_3;
  }
  if (param_1 == 'B') {
    bVar1 = puRam003e6d10 != param_2;
    puVar6 = param_2;
    if (!bVar1) {
      puVar6 = puRam003e6d10;
    }
    puRam003e6d10 = puVar6;
    uVar11 = (uint)bVar1;
    if (sRam003e6d14 == lVar10) goto LAB_0001623c;
  }
  uVar11 = 1;
  sRam003e6d14 = (short)lVar10;
LAB_0001623c:
  if (uVar11 != 1) {
    return;
  }
  if (lVar10 == 0x7fff) {
    func_0x00141c40(0);
    uRam00401008 = 0xf;
    uRam00401000 = 0;
    uRam00401004 = uVar11;
    func_0x00141670(0x401000);
    uRam00401010 = 0x3e6d18;
    uRam00401018 = 0x240;
    uRam00401024 = 0x800;
    uRam00401028 = 0x200;
    uRam00401034 = 0x2ff;
    uRam00401020 = 0;
    uRam0040102c = 0;
    puRam00401014 = param_2;
    uRam0040101c = uVar11;
    func_0x001416e0(0x401010,0);
    func_0x00141850(0);
    uRam002031fc = 0x900;
    puRam002031f8 = param_2;
  }
  else {
    uRam00400ff8 = (uint)lVar10;
    if ((param_1 == 'B') && (lVar10 < 0xab)) {
      uRam00400ff8 = (uint)*(byte *)(uRam00400ff8 * 2 + 0xc01e94);
    }
    uRam00400ff8 = uRam00400ff8 * 0x900;
    puVar6 = (ulong *)(uRam00400ff8 + 0xc30400);
    if (((uint)param_2 & 7) == 0) {
      puVar4 = (ulong *)(uRam00400ff8 + 0xc30d00);
      do {
        uVar7 = puVar6[1];
        uVar8 = puVar6[2];
        uVar9 = puVar6[3];
        *param_2 = *puVar6;
        param_2[1] = uVar7;
        param_2[2] = uVar8;
        param_2[3] = uVar9;
        puVar6 = puVar6 + 4;
        param_2 = param_2 + 4;
      } while (puVar6 != puVar4);
    }
    else {
      puVar4 = (ulong *)(uRam00400ff8 + 0xc30d00);
      do {
        uVar7 = *puVar6;
        uVar8 = puVar6[1];
        uVar9 = puVar6[2];
        uVar2 = puVar6[3];
        uVar11 = (int)param_2 + 7U & 7;
        puVar3 = (ulong *)(((int)param_2 + 7U) - uVar11);
        *puVar3 = *puVar3 & -1 << (uVar11 + 1) * 8 | uVar7 >> (7 - uVar11) * 8;
        uVar11 = (uint)param_2 & 7;
        *(ulong *)((int)param_2 - uVar11) =
             uVar7 << uVar11 * 8 |
             *(ulong *)((int)param_2 - uVar11) & 0xffffffffffffffffU >> (8 - uVar11) * 8;
        uVar11 = (int)param_2 + 0xfU & 7;
        puVar3 = (ulong *)(((int)param_2 + 0xfU) - uVar11);
        *puVar3 = *puVar3 & -1 << (uVar11 + 1) * 8 | uVar8 >> (7 - uVar11) * 8;
        uVar11 = (uint)(param_2 + 1) & 7;
        puVar3 = (ulong *)((int)(param_2 + 1) - uVar11);
        *puVar3 = uVar8 << uVar11 * 8 | *puVar3 & 0xffffffffffffffffU >> (8 - uVar11) * 8;
        uVar11 = (int)param_2 + 0x17U & 7;
        puVar3 = (ulong *)(((int)param_2 + 0x17U) - uVar11);
        *puVar3 = *puVar3 & -1 << (uVar11 + 1) * 8 | uVar9 >> (7 - uVar11) * 8;
        uVar11 = (uint)(param_2 + 2) & 7;
        puVar3 = (ulong *)((int)(param_2 + 2) - uVar11);
        *puVar3 = uVar9 << uVar11 * 8 | *puVar3 & 0xffffffffffffffffU >> (8 - uVar11) * 8;
        uVar11 = (int)param_2 + 0x1fU & 7;
        puVar3 = (ulong *)(((int)param_2 + 0x1fU) - uVar11);
        *puVar3 = *puVar3 & -1 << (uVar11 + 1) * 8 | uVar2 >> (7 - uVar11) * 8;
        uVar11 = (uint)(param_2 + 3) & 7;
        puVar3 = (ulong *)((int)(param_2 + 3) - uVar11);
        *puVar3 = uVar2 << uVar11 * 8 | *puVar3 & 0xffffffffffffffffU >> (8 - uVar11) * 8;
        puVar6 = puVar6 + 4;
        param_2 = param_2 + 4;
      } while (puVar6 != puVar4);
    }
  }
  if ((lVar10 != 0x7fff) && (param_1 == 'B')) {
    lVar5 = func_0x001252e8(lVar10);
    if ((lVar5 != 1) && ((lVar5 < 2 && (lVar5 == 0)))) {
      func_0x00124700(lVar10);
    }
  }
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_00016558(undefined param_1,undefined8 param_2,undefined8 param_3)

{
  uRam0040104c = param_1;
  func_0x00138548(0x401050);
  func_0x00138548(0x401060,param_3);
  uRam00401074 = 0x401060;
  uRam00401070 = 0x401050;
  func_0x00396e38(0x401040,2,0,2,0x7fff,0);
  bRam00401048 = bRam00401048 | 1;
  return;
}



// WARNING: Removing unreachable block (ram,0x000168a8)
// WARNING: Removing unreachable block (ram,0x000169a4)

uint FUN_000166b8(byte param_1,uint param_2,uint param_3,uint param_4)

{
  undefined uVar1;
  int iVar2;
  long lVar3;
  uint uVar4;
  byte bVar5;
  uint uVar6;
  uint uVar7;
  uint uVar8;
  uint uVar9;
  undefined uVar10;
  uint uVar11;
  ulong uVar12;
  ulong uVar13;
  
  uVar8 = param_3 & 0xff;
  uVar9 = param_2 & 0xff;
  uVar11 = param_4 & 0xff;
  func_0x00139358();
  uVar10 = (undefined)uVar11;
  uVar1 = uVar10;
  if (sRam00c084ec == 0) {
    if (param_1 != 0x42) {
      if (param_1 != 0x53) goto LAB_00016844;
      uRam003ecf4b = uRam00c00112;
      uVar1 = uRam003ecf4b;
    }
  }
  else {
    uVar13 = 0;
    if (param_1 == 0x42) {
      uVar13 = func_0x00397a48(0xc08502,bRam00c08500,uVar11,0xff);
    }
    bVar5 = param_1 | 0x80;
    if (uVar13 < bRam00c08500) {
      bVar5 = param_1;
    }
    uVar10 = uRam00c0853c;
    if (bVar5 == 0x53) {
LAB_00016834:
      uRam003ecf4b = uRam00c00100;
      uVar1 = uRam003ecf4b;
    }
    else {
      if (bVar5 < 0x54) {
        uVar10 = uRam00c0853c;
        if (bVar5 != 0x42) goto LAB_00016844;
      }
      else {
        uVar10 = uRam00c084fc;
        if (bVar5 != 0xc2) {
          uVar10 = uRam00c084fc;
          if (bVar5 != 0xd3) goto LAB_00016844;
          goto LAB_00016834;
        }
      }
    }
  }
  uRam003ecf4c = uVar10;
  uRam003ecf4b = uVar1;
LAB_00016844:
  if (param_1 == 0x45) {
    uRam0040103b = 0;
    uRam00401039 = (undefined)uVar9;
    uRam0040103a = (undefined)uVar8;
    bRam00401038 = param_1;
    func_0x00139398(uVar9,uVar8);
    func_0x001390a0(uRam00c00110 & 7);
  }
  else {
    if (param_1 < 0x46) {
      if (param_1 == 0x42) {
        if (uVar8 == 0) {
          trap(7);
        }
        uVar13 = SEXT48((int)(100 / uVar8));
        iVar2 = func_0x001881e8();
        iVar2 = (iVar2 / 0xb) % 0xff + 1;
        uVar12 = (ulong)((uint)(iVar2 * (iVar2 / 3)) % 100);
        if ((uVar8 != 1) && (uVar13 < uVar12)) {
          uVar6 = 1;
          while( true ) {
            uVar9 = uVar9 + 1 & 0xff;
            uVar13 = SEXT48((int)((int)uVar13 + 100 / uVar8));
            if ((uVar8 - 1 <= uVar6) || (uVar12 <= uVar13)) break;
            uVar6 = uVar6 + 1;
          }
        }
        if (sRam00c084ec == 0) {
          uRam0040103a = *(undefined *)(uVar9 * 0xab + uVar11 + 0x3eb328);
        }
        else {
          uRam0040103a = *(undefined *)(uVar9 * 0xab + uVar11 + 0x3e9dc8);
        }
        uRam00401039 = (undefined)uVar9;
        uRam0040103b = 0;
        bRam00401038 = param_1;
      }
    }
    else {
      if (param_1 == 0x53) {
        if (uVar8 == 0) {
          trap(7);
        }
        uVar11 = 100 / uVar8;
        iVar2 = func_0x001881e8();
        uVar6 = (iVar2 / 5) % 100;
        if ((uVar8 != 1) && (uVar11 < uVar6)) {
          uVar7 = 1;
          uVar4 = uVar11;
          while( true ) {
            uVar9 = uVar9 + 1 & 0xff;
            uVar4 = uVar4 + uVar11;
            if ((uVar8 - 1 <= uVar7) || (uVar6 <= uVar4)) break;
            uVar7 = uVar7 + 1;
          }
        }
        uRam0040103a = (undefined)uVar9;
        uRam0040103b = 0;
        uRam00401039 = 0;
        bRam00401038 = param_1;
      }
    }
  }
  uVar8 = 0;
  (*pcRam00c000bc)();
  if (uRam003ed466 != 0) {
    do {
      lVar3 = func_0x00187338(iRam003ed468 + uVar8 * 4,0x401038,3);
      if (lVar3 == 0) {
        return uVar8 & 0xffff;
      }
      uVar8 = uVar8 + 1;
    } while (uVar8 < uRam003ed466);
  }
  return 0;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00016ae0(undefined param_1,char param_2,short param_3,undefined2 param_4,uint param_5,
                 uint param_6)

{
  short sVar1;
  short sVar2;
  int iVar3;
  int iVar4;
  ushort *puVar5;
  
  iVar3 = (int)param_3;
  puVar5 = (ushort *)((param_6 & 0xff) * 2 + 0xc04552);
  iVar4 = (param_5 & 0xff) * 0x708;
  sVar1 = *(short *)((iVar4 + (uint)*puVar5) * 2 + 0xc08812);
  if (sVar1 < 0x50) {
    func_0x00382ad8(iVar3,param_4);
    if (param_2 == '\0') {
      func_0x00129d68(2);
      func_0x00382b60(param_1,1);
      sVar2 = param_3 + 0x30;
    }
    else {
      func_0x00382b60(param_1,1,*(undefined4 *)(sVar1 * 4 + 0x3e8360));
      func_0x00382ad8((iVar3 + 0x3c) * 0x10000 >> 0x10,param_4);
      func_0x00382b60(param_1,1,0x3f3128,*(undefined2 *)((iVar4 + (uint)*puVar5) * 2 + 0xc08c4a));
      func_0x00382ad8((iVar3 + 0x54) * 0x10000 >> 0x10,param_4);
      func_0x00382b60(param_1,1,0x3f3128,*(undefined2 *)((iVar4 + (uint)*puVar5) * 2 + 0xc094ba));
      func_0x00382ad8((iVar3 + 0x6c) * 0x10000 >> 0x10,param_4);
      func_0x00397630(param_5 & 0xff,param_6 & 0xff);
      func_0x00382b60(param_1,1);
      sVar2 = param_3 + 0x98;
    }
    iVar3 = (uint)*(byte *)(sVar1 + 0x3e84a0) * 0x20;
    *(short *)(iVar3 + 0x3ed324) = sVar2;
    *(undefined2 *)(iVar3 + 0x3ed326) = param_4;
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  return;
}



uint FUN_00017308(undefined param_1,byte *param_2,undefined param_3)

{
  byte bVar1;
  short sVar2;
  short sVar3;
  short sVar4;
  short unaff_s3_lo;
  short unaff_s4_lo;
  uint uVar5;
  byte unaff_s7_lo;
  byte unaff_s8_lo;
  
  bVar1 = *param_2;
  if ((bVar1 & 1) == 0) {
    return 0xff;
  }
  sVar2 = *(short *)(param_2 + 0x12);
  sVar4 = *(short *)(param_2 + 0x14);
  if ((bVar1 & 2) == 0) {
    if ((bVar1 & 4) != 0) {
      unaff_s3_lo = *(short *)(param_2 + 0xe);
      unaff_s7_lo = 0x40;
      unaff_s4_lo = *(short *)(param_2 + 0x10);
      unaff_s8_lo = 0x80;
      goto LAB_00017394;
    }
    bVar1 = param_2[1];
  }
  else {
    unaff_s7_lo = 0x10;
    unaff_s8_lo = 0x20;
    unaff_s3_lo = *(short *)(param_2 + 10);
    unaff_s4_lo = *(short *)(param_2 + 0xc);
LAB_00017394:
    bVar1 = param_2[1];
  }
  uVar5 = 0;
  func_0x003b7c80(param_1,bVar1,sVar2,sVar4,*(undefined2 *)(param_2 + 2),param_2[4]);
  if (sVar2 == unaff_s3_lo) {
    uVar5 = 0xf;
  }
  else {
    sVar2 = func_0x00397aa8(sVar2,unaff_s3_lo,param_3);
    if ((*param_2 & unaff_s7_lo) == 0) {
      sVar3 = sVar2;
      if (unaff_s3_lo <= sVar2) {
        sVar3 = unaff_s3_lo;
      }
    }
    else {
      sVar3 = unaff_s3_lo;
      if (unaff_s3_lo < sVar2) {
        *(short *)(param_2 + 0x12) = sVar2;
        goto LAB_00017404;
      }
    }
    *(short *)(param_2 + 0x12) = sVar3;
  }
LAB_00017404:
  if (sVar4 == unaff_s4_lo) {
    uVar5 = uVar5 | 0xf0;
  }
  else {
    sVar2 = func_0x00397aa8(sVar4,unaff_s4_lo,param_3);
    if ((*param_2 & unaff_s8_lo) == 0) {
      sVar4 = sVar2;
      if (unaff_s4_lo <= sVar2) {
        sVar4 = unaff_s4_lo;
      }
    }
    else {
      sVar4 = unaff_s4_lo;
      if (unaff_s4_lo < sVar2) {
        *(short *)(param_2 + 0x14) = sVar2;
        goto LAB_00017454;
      }
    }
    *(short *)(param_2 + 0x14) = sVar4;
  }
LAB_00017454:
  if (uVar5 == 0xff) {
    *param_2 = *param_2 | 8;
  }
  return uVar5;
}



uint FUN_00017580(byte *param_1,undefined param_2)

{
  byte bVar1;
  short sVar2;
  short sVar3;
  short sVar4;
  short unaff_s0_lo;
  short unaff_s3_lo;
  uint uVar5;
  byte unaff_s7_lo;
  byte unaff_s8_lo;
  
  bVar1 = *param_1;
  if ((bVar1 & 1) == 0) {
    return 0xff;
  }
  sVar3 = *(short *)(param_1 + 0x10);
  if ((bVar1 & 2) == 0) {
    if ((bVar1 & 4) != 0) {
      unaff_s0_lo = *(short *)(param_1 + 10);
      unaff_s7_lo = 0x40;
      unaff_s3_lo = *(short *)(param_1 + 0xc);
      unaff_s8_lo = 0x80;
    }
  }
  else {
    unaff_s7_lo = 0x10;
    unaff_s8_lo = 0x20;
    unaff_s0_lo = *(short *)(param_1 + 6);
    unaff_s3_lo = *(short *)(param_1 + 8);
  }
  uVar5 = 0;
  if (*(short *)(param_1 + 0xe) == unaff_s0_lo) {
    uVar5 = 0xf;
  }
  else {
    sVar2 = func_0x00397aa8(*(short *)(param_1 + 0xe),unaff_s0_lo,param_2);
    if ((unaff_s7_lo & bVar1) == 0) {
      sVar4 = sVar2;
      if (unaff_s0_lo <= sVar2) {
        sVar4 = unaff_s0_lo;
      }
    }
    else {
      sVar4 = unaff_s0_lo;
      if (unaff_s0_lo < sVar2) {
        *(short *)(param_1 + 0xe) = sVar2;
        goto LAB_00017654;
      }
    }
    *(short *)(param_1 + 0xe) = sVar4;
  }
LAB_00017654:
  if (sVar3 == unaff_s3_lo) {
    uVar5 = uVar5 | 0xf0;
  }
  else {
    sVar3 = func_0x00397aa8(sVar3,unaff_s3_lo,param_2);
    if ((unaff_s8_lo & bVar1) == 0) {
      sVar2 = sVar3;
      if (unaff_s3_lo <= sVar3) {
        sVar2 = unaff_s3_lo;
      }
    }
    else {
      sVar2 = unaff_s3_lo;
      if (unaff_s3_lo < sVar3) {
        *(short *)(param_1 + 0x10) = sVar3;
        goto LAB_000176a4;
      }
    }
    *(short *)(param_1 + 0x10) = sVar2;
  }
LAB_000176a4:
  if (uVar5 == 0xff) {
    *param_1 = bVar1 | 8;
  }
  return uVar5;
}



undefined4 FUN_000176f8(uint param_1)

{
  uint uVar1;
  int iVar2;
  uint uVar3;
  
  uVar1 = param_1 & 0xff;
  if (*(ushort *)(uVar1 * 2 + 0xc00286) == uVar1) {
    uVar3 = 0;
  }
  else {
    uVar3 = bRam00c00688 + 1 & 0xff;
  }
  iVar2 = func_0x00398078(uVar1);
  return *(undefined4 *)((uVar3 * 5 + iVar2) * 4 + 0x3e7fe8);
}



ulong FUN_00017820(short param_1,undefined8 param_2,uint param_3)

{
  ushort uVar1;
  int iVar2;
  ulong uVar3;
  long lVar4;
  uint uVar5;
  
  uVar5 = param_3 & 0xffff;
  if (param_1 == 0) {
    uVar1 = *(ushort *)(uVar5 * 2 + 0xc007de);
    uVar3 = 0;
    if (uVar5 == uRam00c0853c) {
      if (sRam00c0f102 == 0) {
        lVar4 = func_0x003b6cd0(0x429e,uVar5);
        if (lVar4 == 0) {
          uVar3 = 0;
        }
        else {
          iVar2 = func_0x003b7568(uVar5);
          uVar3 = (ulong)((int)(uint)(uVar1 >> 0xf) < iVar2);
        }
      }
      else {
        uVar3 = 0;
      }
    }
  }
  else {
    uVar3 = 1;
    if ((param_1 == 1) && (uVar3 = func_0x003b6290(param_2), uVar3 != 0)) {
      func_0x003e1430(0x429e,uVar5,1);
      func_0x003b6bd0(0x429e,uVar5,uVar5);
      uVar3 = 1;
    }
  }
  return uVar3;
}



bool FUN_00017900(short param_1,int param_2,short param_3,ulong param_4,undefined2 param_5)

{
  ulong uVar1;
  short sVar2;
  
  if (param_1 == 0) {
    uVar1 = param_4 & 0xffff ^ (ulong)uRam00c0853c;
  }
  else {
    if (param_1 != 1) {
      return true;
    }
    func_0x003b5eb0(0x3f3140);
    if (param_3 == sRam00c084fc) {
      sVar2 = *(short *)(param_2 + 0x1c);
    }
    else {
      sVar2 = *(short *)(param_2 + 0x1c) + 1;
    }
    uVar1 = func_0x003b67b8(param_5,sVar2,1);
  }
  return uVar1 == 0;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x00017a74: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00017a7c)
// WARNING: Removing unreachable block (ram,0x00017a90)
// WARNING: Removing unreachable block (ram,0x00017a84)
// WARNING: Removing unreachable block (ram,0x00017a94)

ulong FUN_000179a8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  uVar1 = 1;
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f3150);
    uVar1 = (ulong)(cRam003e6d34 < '\x02');
    if (cRam003e6d34 == '\x01') {
      return;
    }
    if (uVar1 == 0) {
      uVar1 = 2;
      if ((cRam003e6d34 == '\x02') && (uVar1 = func_0x003b6200(uRam003e6d32), uVar1 != 0)) {
        uRam003e6d32 = 0;
        uVar1 = 1;
        cRam003e6d34 = '\0';
      }
    }
    else {
      if (cRam003e6d34 == '\0') {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 0) {
          cRam003e6d34 = '\x01';
        }
        uVar1 = 0;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00017af0(short param_1)

{
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  if (param_1 != 1) {
    return;
  }
  func_0x003b5eb0(0x3f3160,0x3c);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



ulong FUN_00017b58(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x3c);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3170);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003dede0(0,0x429e);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}


/*
Unable to decompile 'FUN_00017bf0'
Cause: Exception while decompiling 00017bf0: Decompiler process died

*/


// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x00017d9c: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00017da4)
// WARNING: Removing unreachable block (ram,0x00017dac)

char FUN_00017d00(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  lVar1 = 1;
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f3190);
    if (cRam003e6d36 == '\0') {
      lVar1 = func_0x003b6290(param_2);
      if (lVar1 != 0) {
        return;
      }
    }
    else {
      if (cRam003e6d36 != '\x01') {
        return cRam003e6d36;
      }
      lVar1 = func_0x003b6200(0xa2);
      if (lVar1 != 0) {
        cRam003e6d36 = '\0';
        lVar1 = 1;
      }
    }
  }
  return (char)lVar1;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00017de8(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



ulong FUN_000180b0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0) {
      if (sRam00c0853c == 1) {
        uVar1 = (ulong)(sRam00c08540 == 1);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f31b0);
      uVar1 = SEXT18(cRam003e6d38);
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 0) {
          cRam003e6d38 = '\x01';
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0x35e), uVar1 != 0)) {
          func_0x003b6ea8(0x427e,0,uRam00c00a8a);
          func_0x003b6ea8(0x429e,1,uRam00c00a8c);
          func_0x003e1430(0x429e,1,1);
          func_0x003b6bd0(0x429e,1,1);
          func_0x003b5f40(0x50);
          func_0x003b5f40(0x57);
          cRam003e6d38 = '\0';
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



bool FUN_000181f8(short param_1)

{
  ushort uVar1;
  byte bVar2;
  int iVar3;
  ulong uVar4;
  long lVar5;
  
  uVar1 = uRam00c007e0;
  if (param_1 == 0) {
    lVar5 = func_0x003b69d0(0x429e,1);
    if ((lVar5 == 0) && (lVar5 = func_0x003b69d0(0x429e,0xe), lVar5 == 0)) {
      lVar5 = func_0x003b69d0(0x429e,0xc);
      uVar4 = 0;
      bVar2 = bRam003e6d39;
      if (lVar5 == 0) goto LAB_000183e8;
    }
    if (sRam00c0f102 == 0) {
      iVar3 = func_0x003b7568(1);
      uVar4 = (ulong)((int)(uint)(uVar1 >> 0xf) < iVar3);
      bVar2 = bRam003e6d39;
    }
    else {
      uVar4 = 0;
      bVar2 = bRam003e6d39;
    }
  }
  else {
    uVar4 = 1;
    bVar2 = bRam003e6d39;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f31c0);
      uVar4 = (ulong)(bRam003e6d39 < 2);
      if (bRam003e6d39 == 1) {
        uVar4 = func_0x003b6cd0(0x429e,0xe);
        if (uVar4 == 0) {
          bRam003e6d39 = 2;
          bVar2 = bRam003e6d39;
          goto LAB_000183e8;
        }
        lVar5 = func_0x003b6200(0x36f);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6d39;
          goto LAB_000183e8;
        }
        func_0x003b5f40(0x188);
        func_0x003e1430(0x429e,0xe,1);
        func_0x003b6bd0(0x429e,0xe,1);
        bRam003e6d39 = 2;
      }
      else {
        if (uVar4 == 0) {
          if (bRam003e6d39 != 2) {
            return true;
          }
          lVar5 = func_0x003b6cd0(0x429e,0xc);
          if (lVar5 != 0) {
            uVar4 = func_0x003b6200(0x36e);
            bVar2 = bRam003e6d39;
            if (uVar4 == 0) goto LAB_000183e8;
            func_0x003b5f40(0x187);
            func_0x003e1430(0x429e,0xc,1);
            func_0x003b6bd0(0x429e,0xc,1);
          }
          bRam003e6d39 = 0;
          uVar4 = 1;
          bVar2 = bRam003e6d39;
          goto LAB_000183e8;
        }
        bVar2 = bRam003e6d39;
        if (bRam003e6d39 != 0) goto LAB_000183e8;
        uVar4 = func_0x003b6cd0(0x429e,1);
        bVar2 = 1;
        if (uVar4 == 0) goto LAB_000183e8;
        lVar5 = func_0x003b6200(0x367);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6d39;
          goto LAB_000183e8;
        }
        func_0x003b5f40(0x57);
        func_0x003e1430(0x429e,1,1);
        func_0x003b6bd0(0x429e,1,1);
        bRam003e6d39 = 1;
      }
      uVar4 = 0;
      bVar2 = bRam003e6d39;
    }
  }
LAB_000183e8:
  bRam003e6d39 = bVar2;
  return (bool)(char)uVar4;
}



bool FUN_000183f8(short param_1)

{
  ushort uVar1;
  byte bVar2;
  int iVar3;
  ulong uVar4;
  long lVar5;
  
  uVar1 = uRam00c007e0;
  if (param_1 == 0) {
    lVar5 = func_0x003b69d0(0x429e,1);
    if ((lVar5 == 0) && (lVar5 = func_0x003b69d0(0x429e,0xe), lVar5 == 0)) {
      lVar5 = func_0x003b69d0(0x429e,0xc);
      uVar4 = 0;
      bVar2 = bRam003e6d3a;
      if (lVar5 == 0) goto LAB_00018610;
    }
    if (sRam00c0f102 == 0) {
      iVar3 = func_0x003b7568(1);
      uVar4 = (ulong)(iVar3 <= (int)(uint)(uVar1 >> 0xf));
      bVar2 = bRam003e6d3a;
    }
    else {
      uVar4 = 0;
      bVar2 = bRam003e6d3a;
    }
  }
  else {
    uVar4 = 1;
    bVar2 = bRam003e6d3a;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f31d0);
      uVar4 = (ulong)(bRam003e6d3a < 2);
      if (bRam003e6d3a == 1) {
        uVar4 = func_0x003b6cd0(0x429e,0xe);
        if (uVar4 == 0) {
          bRam003e6d3a = 2;
          bVar2 = bRam003e6d3a;
          goto LAB_00018610;
        }
        lVar5 = func_0x003b6200(899);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6d3a;
          goto LAB_00018610;
        }
        func_0x003b5f40(0x84);
        func_0x003e1430(0x429e,0xe,1);
        func_0x003b6bd0(0x429e,0xe,1);
        bRam003e6d3a = 2;
      }
      else {
        if (uVar4 == 0) {
          if (bRam003e6d3a != 2) {
            return true;
          }
          lVar5 = func_0x003b6cd0(0x429e,0xc);
          if (lVar5 != 0) {
            uVar4 = func_0x003b6200(0x382);
            bVar2 = bRam003e6d3a;
            if (uVar4 == 0) goto LAB_00018610;
            func_0x003b5f40(0x84);
            func_0x003e1430(0x429e,0xc,1);
            func_0x003b6bd0(0x429e,0xc,1);
          }
          bRam003e6d3a = 0;
          uVar4 = 1;
          bVar2 = bRam003e6d3a;
          goto LAB_00018610;
        }
        bVar2 = bRam003e6d3a;
        if (bRam003e6d3a != 0) goto LAB_00018610;
        uVar4 = func_0x003b6cd0(0x429e,1);
        bVar2 = 1;
        if (uVar4 == 0) goto LAB_00018610;
        lVar5 = func_0x003b6200(0x37b);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6d3a;
          goto LAB_00018610;
        }
        func_0x003b5f40(0x59);
        func_0x003e1430(0x429e,1,1);
        func_0x003b6bd0(0x429e,1,1);
        func_0x003b7620(0x429e,1,2,10);
        bRam003e6d3a = 1;
        uRam003e2c86 = 2;
      }
      uVar4 = 0;
      bVar2 = bRam003e6d3a;
    }
  }
LAB_00018610:
  bRam003e6d3a = bVar2;
  return (bool)(char)uVar4;
}



bool FUN_00018620(void)

{
  bool bVar1;
  long lVar2;
  
  lVar2 = func_0x001251e8();
  if (lVar2 == 0) {
    lVar2 = func_0x00125258();
    if (lVar2 == 0) {
      bVar1 = false;
      if ((sRam00c084fc == 0) && (bVar1 = false, sRam00c0853c == 0x2a)) {
        bVar1 = false;
        if (sRam00c08500 == 1) {
          bVar1 = sRam00c08540 == 1;
        }
      }
    }
    else {
      bVar1 = false;
    }
  }
  else {
    bVar1 = false;
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

long FUN_000186a8(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  lVar1 = 1;
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f31e0);
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 0) {
      func_0x003b7130(0,0x2a);
      func_0x003b7440(0x429e);
      lVar1 = 1;
    }
  }
  return lVar1;
}



ulong FUN_00018728(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x00398f20();
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f31f0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003de9c8(0,0x2a);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_000187b8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x00398f20();
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3200);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6ea8(0x429e,0x2a,uRam00c00ade);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00018850(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0853c == 0x2b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3210);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(0x2a,0x38);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000188e8(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 == 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3220);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00018978(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 != 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3230);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00018a30(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x2b && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3240);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00018ab8(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 != 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3250);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00018b48(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 == 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3260);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00018c00(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    if ((sRam00c084fc == 0) && (lVar2 = func_0x003b69d0(0x429e,0x3a), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x429e,0x39);
      bVar1 = lVar2 != 0;
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3270);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00018ca0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0) {
      uVar1 = (ulong)(sRam00c0853c == 1);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3280);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(0,1);
        func_0x003b71d8(0x427e,0);
        func_0x003b71d8(0x429e,1);
        func_0x003b74b0();
        func_0x003b7548();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00018d58(short param_1)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0) {
      if (sRam00c0853c == 1) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f32a8);
      uVar1 = (ulong)(bRam003e6d3b < 5);
      if (uVar1 != 0) {
                    // WARNING: Could not emulate address calculation at 0x00018dec
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((char)bRam003e6d3b * 4 + 0x3f32c0))();
        return uVar1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00018ec8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  long lVar3;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0) {
      uVar1 = (ulong)(sRam00c0853c == 0x55);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      uVar1 = func_0x003b5eb0(0x3f32d8);
      if (cRam003e6d3c == '\0') {
        lVar3 = func_0x003b6290(param_2);
        if (lVar3 == 0) {
          return 0;
        }
        cRam003e6d3c = '\x01';
      }
      else {
        if (cRam003e6d3c != '\x01') {
          return uVar1;
        }
        lVar2 = func_0x003b5f10(0x192);
        lVar3 = 0x607;
        if (lVar2 != 1) {
          lVar3 = 0x606;
        }
        lVar2 = func_0x003b67b8(0x5a,lVar3,1);
        if (lVar2 == 0) {
          if (lVar3 == 0x606) {
            func_0x003b5f40(0x191);
            func_0x003b5f40(0x192);
          }
          lVar3 = func_0x003e13d0(0x427e,2);
          if (lVar3 < 0) {
            uRam00c0354e = 1;
            uRam00c00134 = 6;
          }
          else {
            func_0x003b6c48(0x427e,2,1);
          }
          cRam003e6d3c = 0;
          return 1;
        }
      }
      uVar1 = 0;
    }
  }
  return uVar1;
}



ulong FUN_00019030(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x55) {
      if (sRam00c084fc == 0) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f32e8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x55,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000190d0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x55 && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f32f8);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00019158(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x55;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3308);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_000191d0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x55) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3318);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x55,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_00019268(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3328);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_00019358(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3338);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_00019458(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3348);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_00019548(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3358);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



ulong FUN_00019648(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    lVar1 = func_0x003b69d0(0x427e,0);
    uVar2 = 0;
    if (lVar1 != 0) {
      lVar1 = func_0x003b69d0(0x427e,1);
      uVar2 = 0;
      if (lVar1 != 0) {
        if (sRam00c08500 == 2) {
          lVar1 = func_0x003b69d0(0x429e,0x54);
          uVar2 = 0;
          if (lVar1 != 0) {
            lVar1 = func_0x003b69d0(0x429e,0x7f);
            uVar2 = (ulong)(lVar1 != 0);
          }
        }
        else {
          uVar2 = 0;
        }
      }
    }
  }
  else {
    uVar2 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3368);
      uVar2 = func_0x003b6290(param_2);
      if (uVar2 != 0) {
        func_0x003b7130(1,0x54);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



bool FUN_00019718(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,1);
      uVar1 = 0;
      if (lVar2 != 0) {
        if (sRam00c08500 == 2) {
          lVar2 = func_0x003b69d0(0x429e,0x54);
          if (lVar2 == 0) {
            uVar1 = 0;
          }
          else {
            lVar2 = func_0x003b69d0(0x429e,0x7f);
            if (lVar2 == 0) {
              uVar1 = 0;
            }
            else {
              uVar1 = (ulong)(sRam00c0f102 == 0);
            }
          }
        }
        else {
          uVar1 = 0;
        }
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3378);
      uVar1 = (ulong)(bRam003e6d3d < 2);
      if (bRam003e6d3d == 1) {
        lVar2 = func_0x003b67b8(0x34,0x796,1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_00019874;
        }
        bRam003e6d3d = 2;
      }
      else {
        if (uVar1 == 0) {
          if (bRam003e6d3d != 2) {
            return true;
          }
          uVar1 = func_0x003b6200(0x797);
          if (uVar1 != 0) {
            bRam003e6d3d = 0;
            uVar1 = 1;
          }
          goto LAB_00019874;
        }
        if (bRam003e6d3d != 0) goto LAB_00019874;
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_00019874;
        }
        bRam003e6d3d = 1;
      }
      uVar1 = 0;
    }
  }
LAB_00019874:
  return (bool)(char)uVar1;
}



ulong FUN_00019888(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      if (sRam00c08500 == 1) {
        uVar1 = (ulong)(sRam00c0853c == 0x3b);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3388);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b74b0();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00019938(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      uVar1 = (ulong)(sRam00c0853c == 0x3b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3398);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x427e,0x7c,1);
        uRam00c0f102 = 1;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_000199e0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c01d3e == 0x17);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f33a8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6aa0(0x429e,0x3b,8);
        func_0x003b6b20(0x429e,0x3b,0xa0);
        sRam00c01706 = sRam00c01706 + 8;
        sRam00c03dc4 = sRam00c03dc4 + 0xa0;
        sRam00c03f1a = sRam00c03f1a + 0xa0;
        sRam00c04070 = sRam00c04070 + 0xa0;
        sRam00c041c6 = sRam00c041c6 + 0xa0;
        sRam00c0431c = sRam00c0431c + 0xa0;
        func_0x003b7130(0,0x3b);
        func_0x003b71d8(0x427e,0);
        func_0x003b71d8(0x429e,0x3b);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00019b38(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f33b8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00019bd8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x3b);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f33c8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00019c78(short param_1)

{
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  if (param_1 != 1) {
    return;
  }
  func_0x003b5eb0(0x3f33d8,0x2c);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00019ce0(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



ulong FUN_00019cf8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x2c);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f33f0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003dede0(0,0x429e);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_00019d90(short param_1,int param_2)

{
  long lVar1;
  bool bVar2;
  
  if (param_1 == 0) {
    lVar1 = func_0x003b69d0(0x429e,0x34);
    bVar2 = lVar1 == 1 && sRam00c002ee == 0x34;
  }
  else {
    bVar2 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3400);
      lVar1 = func_0x003b67b8(0x6d,*(undefined2 *)(param_2 + 0x1c),1);
      bVar2 = lVar1 == 0;
    }
  }
  return bVar2;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00019e28(short param_1)

{
  long lVar1;
  bool bVar2;
  
  if (param_1 == 0) {
    lVar1 = func_0x003b69d0(0x429e,0x34);
    bVar2 = lVar1 == 1 && sRam00c002ee == 0x34;
  }
  else {
    bVar2 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3410);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar2;
}



ulong FUN_00019ec0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x34);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3420);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x34,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00019f60(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x45;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3430);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00019fd8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x45) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3440);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        uRam00c08638 = 0;
        func_0x003e1430(0x429e,0x45,1);
        func_0x003e1430(0x429e,0x46,1);
        func_0x003e1430(0x429e,0x47,1);
        func_0x003b5f78(0x141);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0001a0a0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0) {
      uVar1 = (ulong)(sRam00c0853c == 0x3e);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3450);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(0,0x3e);
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b71d8(0x427e,0);
        func_0x003b71d8(0x429e,0x3e);
        func_0x003b7440(0x429e);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001a168(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c084fc == 0) {
      if (sRam00c0853c == 0x3e) {
        bVar1 = cRam003e2f59 == '\0';
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3460);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001a200(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 0 && sRam00c0853c == 0x3f;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3470);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0001a288(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x3f) {
      if (sRam00c084fc == 0) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3480);
      uVar1 = (ulong)bRam003e6d3e;
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 1) {
          bRam003e6d3e = 1;
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0xd36), uVar1 != 0)) {
          lVar2 = func_0x003e14f0(0x429e,0x3f);
          if ((lVar2 == 4) || (lVar2 == 0)) {
            func_0x003e1430(0x429e,0x3f,3);
            func_0x003b6bd0(0x429e,0x3f,uRam00c00304);
          }
          bRam003e6d3e = 0;
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_0001a3b8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0) {
      if (sRam00c0853c == 0x3f) {
        uVar1 = (ulong)(sRam00c0f102 != 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3490);
      uVar1 = (ulong)bRam003e6d3f;
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 1) {
          bRam003e6d3f = 1;
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0xd36), uVar1 != 0)) {
          bRam003e6d3f = 0;
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x0001a504: Changing call to branch
// WARNING: Removing unreachable block (ram,0x0001a50c)
// WARNING: Removing unreachable block (ram,0x0001a514)
// WARNING: Removing unreachable block (ram,0x0001a53c)

void FUN_0001a4a0(short param_1)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f34a8);
    lVar1 = func_0x003b6f20(0x427e,0x3f34a0,2);
    if (lVar1 != 0) {
      return;
    }
  }
  return;
}



ulong FUN_0001a550(short param_1)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x3f);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f34b8);
      uVar1 = (ulong)(bRam003e6d40 < 5);
      if (uVar1 != 0) {
                    // WARNING: Could not emulate address calculation at 0x0001a5dc
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((char)bRam003e6d40 * 4 + 0x3f34d0))();
        return uVar1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x0001a78c: Changing call to branch
// WARNING: Removing unreachable block (ram,0x0001a794)
// WARNING: Removing unreachable block (ram,0x0001a79c)
// WARNING: Removing unreachable block (ram,0x0001a7c4)

void FUN_0001a728(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f34e8,0x3f);
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 0) {
      return;
    }
  }
  return;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001a7d8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x29 && sRam00c08540 == 1;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f34f8);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0001a868(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3508);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        lVar2 = func_0x003e14f0(0x429e,0x29);
        if (lVar2 == 4) {
          func_0x003e1430(0x429e,0x29,3);
          func_0x003b6bd0(0x429e,0x29,uRam00c002d8);
        }
        func_0x003b75b8(0x33);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001a948(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        bVar1 = sRam00c0f102 != 0;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3518);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0001a9e8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3528);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(0,0x28);
        func_0x003b6ea8(0x427e,0,0);
        func_0x003b6ea8(0x429e,0x28,0);
        func_0x003b74b0();
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b71d8(0x427e,0);
        func_0x003b71d8(0x429e,0x28);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0001aad0(short param_1)

{
  ulong uVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3538);
      uVar3 = 0xe07;
      if (sRam00c0f102 == 0) {
        uVar3 = 0xe08;
      }
      uVar1 = func_0x003b6200(uVar3);
      if (uVar1 != 0) {
        func_0x003b6ea8(0x427e,0,uRam00c00a8a);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_0001ab70(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc != 0) {
      uVar1 = (ulong)(sRam00c0853c == 0x68);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3548);
      if (cRam003e6d4c == '\x01') {
        lVar2 = func_0x003b67b8(0x5a,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_0001ac9c;
        }
      }
      else {
        if (cRam003e6d4c < '\x02') {
          if (cRam003e6d4c != '\0') {
            return cRam003e6d4c < '\x02';
          }
          lVar2 = func_0x003b5f10(0x192);
          uVar1 = 0;
          cRam003e6d4c = (lVar2 != 0) + '\x01';
          goto LAB_0001ac9c;
        }
        if (cRam003e6d4c != '\x02') {
          if (cRam003e6d4c != '\x03') {
            return true;
          }
          uVar1 = func_0x003b6200(0xfad);
          if (uVar1 != 0) {
            cRam003e6d4c = '\0';
            uVar1 = 1;
          }
          goto LAB_0001ac9c;
        }
        lVar2 = func_0x003b6200(0xfac);
        if (lVar2 == 0) {
          uVar1 = 0;
          goto LAB_0001ac9c;
        }
      }
      cRam003e6d4c = '\x03';
      uVar1 = 0;
    }
  }
LAB_0001ac9c:
  return (bool)(char)uVar1;
}



bool FUN_0001acb0(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0) {
      uVar1 = (ulong)(sRam00c0853c == 0x68);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3558);
      if (cRam003e6d4d == '\x01') {
        lVar2 = func_0x003b67b8(0x5a,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_0001addc;
        }
      }
      else {
        if (cRam003e6d4d < '\x02') {
          if (cRam003e6d4d != '\0') {
            return cRam003e6d4d < '\x02';
          }
          lVar2 = func_0x003b5f10(0x191);
          uVar1 = 0;
          cRam003e6d4d = (lVar2 != 0) + '\x01';
          goto LAB_0001addc;
        }
        if (cRam003e6d4d != '\x02') {
          if (cRam003e6d4d != '\x03') {
            return true;
          }
          uVar1 = func_0x003b6200(0xfb7);
          if (uVar1 != 0) {
            cRam003e6d4d = '\0';
            uVar1 = 1;
          }
          goto LAB_0001addc;
        }
        lVar2 = func_0x003b6200(0xfb6);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_0001addc;
        }
      }
      cRam003e6d4d = '\x03';
      uVar1 = 0;
    }
  }
LAB_0001addc:
  return (bool)(char)uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001adf0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc != 0 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3568);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001ae78(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 0 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3578);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001af00(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc != 0 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3588);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



bool FUN_0001af88(short param_1)

{
  bool bVar1;
  long lVar2;
  undefined4 uVar3;
  ushort *puVar4;
  int iVar5;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x68 && sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0();
      if (bRam003e6d4e == 1) {
        uVar3 = 0xffb;
      }
      else {
        if (bRam003e6d4e < 2) {
          if (bRam003e6d4e != 0) {
            return bRam003e6d4e < 2;
          }
          lVar2 = func_0x003b5f10();
          bRam003e6d4e = '\x02' - (lVar2 != 1);
          return false;
        }
        if (bRam003e6d4e != 2) {
          if (bRam003e6d4e != 3) {
            return true;
          }
          iVar5 = 0;
          puVar4 = (ushort *)0x3f3598;
          do {
            lVar2 = func_0x003e14f0(0x429e,*puVar4);
            if (lVar2 == 4) {
              func_0x003b6bd0(0x429e,*puVar4,*(undefined2 *)((uint)*puVar4 * 2 + 0xc00286));
              func_0x003e1430(0x429e,*puVar4,3);
            }
            iVar5 = (iVar5 + 1) * 0x1000000 >> 0x18;
            puVar4 = puVar4 + 1;
          } while (iVar5 < 4);
          uRam003e2c92 = uRam00c01208;
          bRam003e6d4e = 0;
          return true;
        }
        uVar3 = 0xffc;
      }
      lVar2 = func_0x003b6200(uVar3);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        bRam003e6d4e = 3;
        bVar1 = false;
      }
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001b130(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc != 0 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f35b0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



bool FUN_0001b1b8(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    uVar2 = 0;
    if (sRam00c084fc == 0) {
      uVar2 = (ulong)(sRam00c0853c == 0x68);
    }
    goto LAB_0001b2cc;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f35c0);
  if (cRam003e6d4f == '\x01') {
    lVar1 = func_0x003b6200(0x1024);
    if (lVar1 != 1) {
      uVar2 = 0;
      goto LAB_0001b2cc;
    }
    cRam003e6d4f = '\x02';
  }
  else {
    if (cRam003e6d4f >= '\x02') {
      if (cRam003e6d4f != '\x02') {
        return true;
      }
      uVar2 = func_0x003b6200(0x1025);
      if (uVar2 != 0) {
        func_0x003b6aa0(0x429e,0x68,3);
        cRam003e6d4f = '\0';
        uVar2 = 1;
      }
      goto LAB_0001b2cc;
    }
    if (cRam003e6d4f != '\0') {
      return cRam003e6d4f < '\x02';
    }
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 1) {
      uVar2 = 0;
      goto LAB_0001b2cc;
    }
    lVar1 = func_0x003b69d0(0x427e,0x6a);
    cRam003e6d4f = (lVar1 == 0) + '\x01';
  }
  uVar2 = 0;
LAB_0001b2cc:
  return (bool)(char)uVar2;
}



bool FUN_0001b2e8(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    uVar2 = 0;
    if (sRam00c084fc == 0) {
      if (sRam00c0853c == 0x68) {
        uVar2 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar2 = 0;
      }
    }
    goto LAB_0001b474;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f35d0);
  if (cRam003e6d50 == '\x01') {
    lVar1 = func_0x003b67b8(0x5c,0x102e,uRam0040107a);
    if (lVar1 != 0) {
      uVar2 = 0;
      goto LAB_0001b474;
    }
    cRam003e6d50 = (iRam0040107c == 0) + '\x02';
  }
  else {
    if (cRam003e6d50 < '\x02') {
      if (cRam003e6d50 != '\0') {
        return cRam003e6d50 < '\x02';
      }
      lVar1 = func_0x003b6290(param_2);
      if (lVar1 != 1) {
        uVar2 = 0;
        goto LAB_0001b474;
      }
      lVar1 = func_0x003b69d0(0x427e,0x6a);
      uRam0040107a = (ushort)(lVar1 == 0);
      cRam003e6d50 = '\x01';
      iRam0040107c = (int)lVar1;
    }
    else {
      if (cRam003e6d50 != '\x02') {
        if (cRam003e6d50 != '\x03') {
          return true;
        }
        uVar2 = func_0x003b6200(0x1030);
        if (uVar2 != 0) {
          func_0x003b6bd0(0x429e,0x68,0x68);
          func_0x003e1430(0x429e,0x68,1);
          cRam003e6d50 = '\0';
          uVar2 = 1;
        }
        goto LAB_0001b474;
      }
      lVar1 = func_0x003b67b8(0x5c,0x102f,1);
      if (lVar1 != 0) {
        uVar2 = 0;
        goto LAB_0001b474;
      }
      cRam003e6d50 = '\x03';
    }
  }
  uVar2 = 0;
LAB_0001b474:
  return (bool)(char)uVar2;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001b490(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c084fc == 0) {
      if (sRam00c0853c == 0x68) {
        bVar1 = sRam00c0f102 != 0;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f35e0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001b528(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 0 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f35f0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



bool FUN_0001b5b0(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
    return true;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f3600);
  if (cRam003e6d51 == '\x01') {
    lVar1 = func_0x003b6200(0x2334);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6d51 = 2;
  }
  else {
    if (cRam003e6d51 >= '\x02') {
      if (cRam003e6d51 != '\x02') {
        return false;
      }
      uRam00c0853c = 0x78;
      uRam00c08542 = 0x78;
      uRam00c00b7a = 100;
      uRam00c08540 = 1;
      uRam00c08558 = 3;
      uRam00c00a24 = 0;
      func_0x00389580(0x3e2c80,0x78);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 5;
      uRam00c08502 = 5;
      uRam00c08504 = 7;
      uRam00c0851a = 3;
      uRam00c08500 = 2;
      uRam00c08518 = 3;
      func_0x00389580(0x3e2a38,5);
      func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
      func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396a98(0x42,0x1073f00,uRam00c08504);
      uRam003e2a30 = 0;
      uRam003e2f5a = 0;
      cRam003e6d51 = 0;
      return true;
    }
    if (cRam003e6d51 != '\0') {
      return cRam003e6d51 < '\x02';
    }
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6d51 = 1;
  }
  return false;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001b7d0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3610);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_0001b848(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f3620);
    uVar1 = 0x3e0000;
    if (cRam003e6d52 == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6d52 = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6d52 == '\x01') {
        uRam00c0853c = 0x79;
        uRam00c08542 = 0x79;
        uRam00c00a26 = 9;
        uRam00c08540 = 1;
        uRam00c00b7c = 100;
        uRam00c08558 = 3;
        func_0x00389580(0x3e2c80,0x79);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c084fc = 3;
        uRam00c08502 = 3;
        uRam00c08504 = 4;
        uRam00c08500 = 2;
        uRam00c0851a = 3;
        uRam00c08518 = 3;
        func_0x00389580(0x3e2a38,3);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        cRam003e6d52 = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001ba30(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3630);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_0001baa8(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f3640);
    uVar1 = 0x3e0000;
    if (cRam003e6d53 == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 != 0) {
        cRam003e6d53 = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6d53 == '\x01') {
        uRam00c0853c = 0x8d;
        uRam00c08542 = 0x8d;
        uRam00c08558 = 3;
        uRam00c00a4e = 0;
        uRam00c00ba4 = 0;
        uRam00c047c2 = 0;
        uRam00c08540 = 1;
        func_0x00389580(0x3e2c80,0x8d);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c08502 = 1;
        uRam00c08504 = 2;
        uRam00c08506 = 6;
        uRam00c08500 = 3;
        uRam00c08518 = 3;
        uRam00c0851a = 3;
        uRam00c0851c = 3;
        uRam00c084fc = 1;
        func_0x00389580(0x3e2a38,1);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00389580(0x3e2aac,(undefined)uRam00c08506);
        func_0x00396e38(0x3e3008,0,0,2,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        func_0x00396a98(0x42,0x1074800,uRam00c08506);
        cRam003e6d53 = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001bcd8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3650);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0001bd50(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f3660);
    uVar1 = (ulong)(cRam003e6d54 < '\x02');
    if (cRam003e6d54 == '\x01') {
      uRam00c0853c = 0x7e;
      uRam00c08542 = 0x7e;
      uRam00c08558 = 3;
      uRam00c08540 = 1;
      func_0x00389580(0x3e2c80,0x7e);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 0;
      uRam00c08502 = 0;
      uRam00c08518 = 3;
      uRam00c08500 = 1;
      func_0x00389580(0x3e2a38,0);
      func_0x00396e38(0x3e3008,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      uRam003e2f5a = 0;
      func_0x00396420();
      uRam003e2a30 = 1;
      uVar1 = 0;
      cRam003e6d54 = '\x02';
    }
    else {
      if (uVar1 == 0) {
        uVar1 = 2;
        if ((cRam003e6d54 == '\x02') && (uVar1 = func_0x003b6428(), uVar1 != 0)) {
          cRam003e6d54 = '\0';
          uRam003ecf72 = 0;
          uVar1 = 1;
        }
      }
      else {
        if (cRam003e6d54 == '\0') {
          lVar2 = func_0x003b6290(param_2);
          if (lVar2 == 1) {
            cRam003e6d54 = '\x01';
          }
          uVar1 = 0;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_0001bf30(short param_1,undefined8 param_2,uint param_3)

{
  ushort uVar1;
  int iVar2;
  ulong uVar3;
  long lVar4;
  uint uVar5;
  
  uVar5 = param_3 & 0xffff;
  if (param_1 == 0) {
    uVar1 = *(ushort *)(uVar5 * 2 + 0xc007de);
    uVar3 = 0;
    if (uVar5 == uRam00c0853c) {
      if (sRam00c0f102 == 0) {
        lVar4 = func_0x003b6cd0(0x429e,uVar5);
        if (lVar4 == 0) {
          uVar3 = 0;
        }
        else {
          iVar2 = func_0x003b7568(uVar5);
          uVar3 = (ulong)((int)(uint)(uVar1 >> 0xf) < iVar2);
        }
      }
      else {
        uVar3 = 0;
      }
    }
  }
  else {
    uVar3 = 1;
    if ((param_1 == 1) && (uVar3 = func_0x003b6290(param_2), uVar3 != 0)) {
      func_0x003b6bd0(0x429e,uVar5,uVar5);
      func_0x003e1430(0x429e,uVar5,1);
      uVar3 = 1;
    }
  }
  return uVar3;
}



bool FUN_0001c010(short param_1,int param_2,short param_3,ulong param_4,undefined2 param_5)

{
  ulong uVar1;
  short sVar2;
  
  if (param_1 == 0) {
    uVar1 = param_4 & 0xffff ^ (ulong)uRam00c0853c;
  }
  else {
    if (param_1 != 1) {
      return true;
    }
    func_0x003b5eb0(0x3f3670);
    if (param_3 == sRam00c084fc) {
      sVar2 = *(short *)(param_2 + 0x1c);
    }
    else {
      sVar2 = *(short *)(param_2 + 0x1c) + 1;
    }
    uVar1 = func_0x003b67b8(param_5,sVar2,1);
  }
  return uVar1 == 0;
}



ulong FUN_0001c0b8(short param_1,int param_2)

{
  long lVar1;
  ulong uVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    if (sRam00c084fc != 1) {
      return 0;
    }
    return (ulong)(sRam00c0853c == 0x5c);
  }
  if (param_1 != 1) {
    return 1;
  }
  func_0x003b5eb0(0x3f3680);
  if (cRam003e6d5a == '\x01') {
    uVar3 = 0x85;
LAB_0001c1bc:
    uVar2 = func_0x003b6200(uVar3);
    if (uVar2 != 0) {
      cRam003e6d5a = '\0';
      uVar2 = 1;
    }
  }
  else {
    if ((ulong)(cRam003e6d5a < '\x02') == 0) {
      if (cRam003e6d5a != '\x02') {
        if (cRam003e6d5a != '\x03') {
          return 3;
        }
        uVar3 = 0x84;
        goto LAB_0001c1bc;
      }
      lVar1 = func_0x003b67b8(0x73,*(undefined2 *)(param_2 + 0x1c),1);
      if (lVar1 != 0) {
        return 0;
      }
      cRam003e6d5a = '\x03';
    }
    else {
      if (cRam003e6d5a != '\0') {
        return (ulong)(cRam003e6d5a < '\x02');
      }
      lVar1 = func_0x003b5f10(0xe);
      if (lVar1 == 0) {
        cRam003e6d5a = '\x02';
      }
      else {
        cRam003e6d5a = '\x01';
      }
    }
    uVar2 = 0;
  }
  return uVar2;
}



bool FUN_0001c1f0(short param_1,int param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = (ulong)uRam00c0853c ^ 0x5c;
  }
  else {
    if (param_1 != 1) {
      return true;
    }
    func_0x003b5eb0(0x3f3690);
    uVar1 = func_0x003b67b8(0x73,*(undefined2 *)(param_2 + 0x1c),1);
  }
  return uVar1 == 0;
}



ulong FUN_0001c260(short param_1,undefined8 param_2)

{
  ulong uVar1;
  int iVar2;
  uint uVar3;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x5c) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f36a0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        uVar3 = 0;
        if (uRam00c08540 != 0) {
          iVar2 = 0;
          do {
            func_0x003e1430(0x429e,*(undefined2 *)(iVar2 + 0xc08542),4);
            func_0x003b6bd0(0x429e,*(undefined2 *)(iVar2 + 0xc08542));
            uVar3 = uVar3 + 1 & 0xff;
            iVar2 = uVar3 << 1;
          } while (uVar3 < uRam00c08540);
        }
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0001c348(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0001c448(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0001c548(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0001c648(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0001c748(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0001c848(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0001c948(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



ulong FUN_0001cac8(short param_1,undefined8 param_2)

{
  char cVar1;
  ulong uVar2;
  long lVar3;
  
  if (param_1 == 0) {
    uVar2 = 0;
    if (sRam00c084fc == 1) {
      uVar2 = (ulong)(sRam00c0853c == 0);
    }
  }
  else {
    uVar2 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3720);
      cVar1 = cRam003e6d5b;
      uVar2 = 0x3e0000;
      if (cRam003e6d5b == '\0') {
        lVar3 = func_0x003b6290(param_2);
        if (lVar3 == 1) {
          cRam003e6d5b = '\x01';
        }
        uVar2 = 0;
      }
      else {
        if (cRam003e6d5b == '\x01') {
          func_0x003b7130(1,0);
          func_0x003b71d8(0x427e,1);
          func_0x003b71d8(0x429e,0);
          func_0x003b74b0();
          func_0x003b7548();
          cRam003e6d5b = '\0';
          cRam003e2fd2 = cVar1;
          uVar2 = 1;
        }
      }
    }
  }
  return uVar2;
}



char FUN_0001cbd0(short param_1)

{
  char cVar1;
  long lVar2;
  
  if (param_1 == 0) {
    cVar1 = '\x01';
    bRam003e6d5d = sRam00c0f102 != 0;
  }
  else {
    cVar1 = '\x01';
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3730);
      if (cRam003e6d5c == '\0') {
        lVar2 = func_0x003b6200(bRam003e6d5d + 0x42f);
        if (lVar2 != 0) {
          cRam003e6d5c = '\x01';
        }
        cVar1 = '\0';
      }
      else {
        cVar1 = cRam003e6d5c;
        if (cRam003e6d5c == '\x01') {
          func_0x003b5f40(bRam003e6d5d + 0x69);
          cRam003e6d5c = '\0';
          cVar1 = '\x01';
        }
      }
    }
  }
  return cVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001cca0(short param_1)

{
  bool bVar1;
  long lVar2;
  bool bVar3;
  
  if (param_1 == 0) {
    bVar3 = false;
    lVar2 = func_0x003b69d0(0x427e,2);
    bVar1 = bVar3;
    if ((lVar2 != 0) && (lVar2 = func_0x003b69d0(0x427e,4), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x427e,6);
      bVar1 = false;
      if (lVar2 != 0) {
        if (sRam00c08500 == 3) {
          lVar2 = func_0x003b69d0(0x429e,0x3a);
          bVar1 = bVar3;
          if (lVar2 != 0) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            bVar1 = lVar2 != 0;
          }
        }
        else {
          bVar1 = false;
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3740);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001cd78(short param_1)

{
  bool bVar1;
  long lVar2;
  bool bVar3;
  
  if (param_1 == 0) {
    bVar3 = false;
    lVar2 = func_0x003b69d0(0x427e,2);
    bVar1 = bVar3;
    if ((lVar2 != 0) && (lVar2 = func_0x003b69d0(0x427e,4), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x427e,6);
      bVar1 = false;
      if (lVar2 != 0) {
        if (sRam00c08500 == 3) {
          lVar2 = func_0x003b69d0(0x429e,0x3a);
          bVar1 = bVar3;
          if (lVar2 != 0) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              bVar1 = sRam00c0f102 == 0;
            }
          }
        }
        else {
          bVar1 = false;
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3750);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



bool FUN_0001ce60(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x4c);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x5b);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x3f);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3760);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001cf50(short param_1)

{
  bool bVar1;
  long lVar2;
  bool bVar3;
  
  if (param_1 == 0) {
    bVar3 = false;
    lVar2 = func_0x003b69d0(0x427e,3);
    bVar1 = bVar3;
    if ((lVar2 != 0) && (lVar2 = func_0x003b69d0(0x427e,5), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x427e,7);
      bVar1 = false;
      if (lVar2 != 0) {
        if (sRam00c08500 == 3) {
          lVar2 = func_0x003b69d0(0x429e,0x4c);
          bVar1 = bVar3;
          if ((lVar2 != 0) && (lVar2 = func_0x003b69d0(0x429e,0x5b), lVar2 != 0)) {
            lVar2 = func_0x003b69d0(0x429e,0x3f);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              bVar1 = sRam00c0f102 == 0;
            }
          }
        }
        else {
          bVar1 = false;
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3770);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0001d048(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    lVar1 = func_0x003b69d0(0x427e,1);
    uVar2 = 0;
    if (lVar1 != 0) {
      lVar1 = func_0x003b69d0(0x427e,0);
      uVar2 = 0;
      if (lVar1 != 0) {
        if (sRam00c08500 == 2) {
          lVar1 = func_0x003b69d0(0x429e,0x54);
          uVar2 = 0;
          if (lVar1 != 0) {
            lVar1 = func_0x003b69d0(0x429e,0x7f);
            uVar2 = (ulong)(lVar1 != 0);
          }
        }
        else {
          uVar2 = 0;
        }
      }
    }
  }
  else {
    uVar2 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3780);
      uVar2 = func_0x003b6290(param_2);
      if (uVar2 != 0) {
        func_0x003b7130(1,0x54);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



bool FUN_0001d118(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,1);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,0);
      uVar1 = 0;
      if (lVar2 != 0) {
        if (sRam00c08500 == 2) {
          lVar2 = func_0x003b69d0(0x429e,0x54);
          if (lVar2 == 0) {
            uVar1 = 0;
          }
          else {
            lVar2 = func_0x003b69d0(0x429e,0x7f);
            if (lVar2 == 0) {
              uVar1 = 0;
            }
            else {
              uVar1 = (ulong)(sRam00c0f102 == 0);
            }
          }
        }
        else {
          uVar1 = 0;
        }
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3790);
      uVar1 = (ulong)(bRam003e6d5e < 2);
      if (bRam003e6d5e == 1) {
        lVar2 = func_0x003b67b8(0x34,0x796,1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_0001d274;
        }
        bRam003e6d5e = 2;
      }
      else {
        if (uVar1 == 0) {
          if (bRam003e6d5e != 2) {
            return true;
          }
          uVar1 = func_0x003b6200(0x797);
          if (uVar1 != 0) {
            bRam003e6d5e = 0;
            uVar1 = 1;
          }
          goto LAB_0001d274;
        }
        if (bRam003e6d5e != 0) goto LAB_0001d274;
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_0001d274;
        }
        bRam003e6d5e = 1;
      }
      uVar1 = 0;
    }
  }
LAB_0001d274:
  return (bool)(char)uVar1;
}



ulong FUN_0001d288(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      if (sRam00c08500 == 1) {
        uVar1 = (ulong)(sRam00c0853c == 0x3b);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f37a0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b74b0();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0001d338(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      uVar1 = (ulong)(sRam00c0853c == 0x3b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f37b0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x427e,0x7c,1);
        uRam00c0f102 = 1;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0001d3e0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 1) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c01d40 == 0x17);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f37c0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6aa0(0x429e,0x3b,4);
        func_0x003b6b20(0x429e,0x3b,0x3c);
        sRam00c01706 = sRam00c01706 + 8;
        sRam00c03dc4 = sRam00c03dc4 + 0xa0;
        sRam00c03f1a = sRam00c03f1a + 0xa0;
        sRam00c04070 = sRam00c04070 + 0xa0;
        sRam00c041c6 = sRam00c041c6 + 0xa0;
        sRam00c0431c = sRam00c0431c + 0xa0;
        func_0x003b7130(1,0x3b);
        func_0x003b71d8(0x427e,1);
        func_0x003b71d8(0x429e,0x3b);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0001d538(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 1) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f37d0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_0001d5e0(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
    return true;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f37e0);
  if (cRam003e6d5f == '\x01') {
    lVar1 = func_0x003b6200(0x2334);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6d5f = 2;
  }
  else {
    if (cRam003e6d5f >= '\x02') {
      if (cRam003e6d5f != '\x02') {
        return false;
      }
      uRam00c0853c = 0x78;
      uRam00c08542 = 0x78;
      uRam00c00b7a = 100;
      uRam00c08540 = 1;
      uRam00c08558 = 3;
      uRam00c00a24 = 0;
      func_0x00389580(0x3e2c80,0x78);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 5;
      uRam00c08502 = 5;
      uRam00c08504 = 7;
      uRam00c0851a = 3;
      uRam00c08500 = 2;
      uRam00c08518 = 3;
      func_0x00389580(0x3e2a38,5);
      func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
      func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396a98(0x42,0x1073f00,uRam00c08504);
      uRam003e2a30 = 0;
      uRam003e2f5a = 0;
      cRam003e6d5f = 0;
      return true;
    }
    if (cRam003e6d5f != '\0') {
      return cRam003e6d5f < '\x02';
    }
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6d5f = 1;
  }
  return false;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001d800(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f37f0);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_0001d878(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f3800);
    uVar1 = 0x3e0000;
    if (cRam003e6d60 == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6d60 = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6d60 == '\x01') {
        uRam00c0853c = 0x79;
        uRam00c08542 = 0x79;
        uRam00c00a26 = 9;
        uRam00c08540 = 1;
        uRam00c00b7c = 100;
        uRam00c08558 = 3;
        func_0x00389580(0x3e2c80,0x79);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c084fc = 3;
        uRam00c08502 = 3;
        uRam00c08504 = 4;
        uRam00c08500 = 2;
        uRam00c0851a = 3;
        uRam00c08518 = 3;
        func_0x00389580(0x3e2a38,3);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        cRam003e6d60 = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001da60(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3810);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_0001dad8(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f3820);
    uVar1 = 0x3e0000;
    if (cRam003e6d61 == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6d61 = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6d61 == '\x01') {
        uRam00c0853c = 0x8d;
        uRam00c08542 = 0x8d;
        uRam00c08540 = 1;
        uRam00c08558 = 3;
        uRam00c00a4e = 0;
        uRam00c00ba4 = 0;
        uRam00c047c2 = 0;
        func_0x00389580(0x3e2c80,0x8d);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c084fc = 0;
        uRam00c08502 = 0;
        uRam00c08504 = 2;
        uRam00c08506 = 6;
        uRam00c08500 = 3;
        uRam00c08518 = 3;
        uRam00c0851a = 3;
        uRam00c0851c = 3;
        func_0x00389580(0x3e2a38,0);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00389580(0x3e2aac,(undefined)uRam00c08506);
        func_0x00396e38(0x3e3008,0,0,2,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        func_0x00396a98(0x42,0x1074800,uRam00c08506);
        cRam003e6d61 = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001dcf8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3830);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0001dd70(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f3840);
    uVar1 = (ulong)(cRam003e6d62 < '\x02');
    if (cRam003e6d62 == '\x01') {
      uRam00c0853c = 0x7e;
      uRam00c08542 = 0x7e;
      uRam00c08558 = 3;
      uRam00c08540 = 1;
      func_0x00389580(0x3e2c80,0x7e);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c08500 = 1;
      uRam00c08518 = 3;
      uRam00c084fc = 1;
      uRam00c08502 = uRam00c08500;
      func_0x00389580(0x3e2a38,1);
      func_0x00396e38(0x3e3008,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396420();
      uRam003e2a30 = 1;
      uVar1 = 0;
      cRam003e6d62 = '\x02';
    }
    else {
      if (uVar1 == 0) {
        uVar1 = 2;
        if ((cRam003e6d62 == '\x02') && (uVar1 = func_0x003b6428(), uVar1 != 0)) {
          cRam003e6d62 = '\0';
          uRam003ecf72 = 0;
          uVar1 = 1;
        }
      }
      else {
        if (cRam003e6d62 == '\0') {
          lVar2 = func_0x003b6290(param_2);
          if (lVar2 == 1) {
            cRam003e6d62 = '\x01';
          }
          uVar1 = 0;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_0001df50(short param_1,undefined8 param_2,uint param_3)

{
  ushort uVar1;
  int iVar2;
  ulong uVar3;
  long lVar4;
  uint uVar5;
  
  uVar5 = param_3 & 0xffff;
  if (param_1 == 0) {
    uVar1 = *(ushort *)(uVar5 * 2 + 0xc007de);
    uVar3 = 0;
    if (uVar5 == uRam00c0853c) {
      if (sRam00c0f102 == 0) {
        lVar4 = func_0x003b6cd0(0x429e,uVar5);
        if (lVar4 == 0) {
          uVar3 = 0;
        }
        else {
          iVar2 = func_0x003b7568(uVar5);
          uVar3 = (ulong)((int)(uint)(uVar1 >> 0xf) < iVar2);
        }
      }
      else {
        uVar3 = 0;
      }
    }
  }
  else {
    uVar3 = 1;
    if ((param_1 == 1) && (uVar3 = func_0x003b6290(param_2), uVar3 != 0)) {
      func_0x003e1430(0x429e,uVar5,1);
      func_0x003b6bd0(0x429e,uVar5,uVar5);
      uVar3 = 1;
    }
  }
  return uVar3;
}



bool FUN_0001e030(short param_1,int param_2,short param_3,ulong param_4,undefined2 param_5)

{
  ulong uVar1;
  short sVar2;
  
  if (param_1 == 0) {
    uVar1 = param_4 & 0xffff ^ (ulong)uRam00c0853c;
  }
  else {
    if (param_1 != 1) {
      return true;
    }
    func_0x003b5eb0(0x3f3850);
    if (param_3 == sRam00c084fc) {
      sVar2 = *(short *)(param_2 + 0x1c);
    }
    else {
      sVar2 = *(short *)(param_2 + 0x1c) + 1;
    }
    uVar1 = func_0x003b67b8(param_5,sVar2,1);
  }
  return uVar1 == 0;
}



ulong FUN_0001e0d8(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 100) {
      uVar1 = (ulong)(sRam00c084fc == 2);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3860);
      uVar1 = SEXT18(cRam003e6d6a);
      if (uVar1 == 0) {
        lVar2 = func_0x003b67b8(0x84,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 == 0) {
          cRam003e6d6a = '\x01';
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0x7a), uVar1 != 0)) {
          cRam003e6d6a = '\0';
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001e1c0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 100 && sRam00c084fc != 2;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3870);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



char FUN_0001e250(short param_1,undefined8 param_2)

{
  char cVar1;
  int iVar2;
  long lVar3;
  uint uVar4;
  
  if (param_1 == 0) {
    lVar3 = func_0x003b69d0(0x429e,100);
    if (lVar3 == 0) {
      cVar1 = '\0';
    }
    else {
      cVar1 = sRam00c0f102 == 0;
    }
  }
  else {
    cVar1 = '\x01';
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3880);
      if (cRam003e6d6b == '\0') {
        lVar3 = func_0x003b6290(param_2);
        if (lVar3 != 0) {
          cRam003e6d6b = '\x01';
        }
        cVar1 = '\0';
      }
      else {
        cVar1 = cRam003e6d6b;
        if (cRam003e6d6b == '\x01') {
          uVar4 = 0;
          if (uRam00c08540 != 0) {
            iVar2 = 0;
            do {
              func_0x003de9c8(2,*(undefined2 *)(iVar2 + 0xc08542));
              uVar4 = uVar4 + 1 & 0xff;
              iVar2 = uVar4 << 1;
            } while (uVar4 < uRam00c08540);
          }
          cRam003e6d6b = '\0';
          cVar1 = '\x01';
        }
      }
    }
  }
  return cVar1;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0001e370(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0001e4a8(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0001e618(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



bool FUN_0001e6d0(short param_1)

{
  ushort uVar1;
  byte bVar2;
  int iVar3;
  ulong uVar4;
  long lVar5;
  
  uVar1 = uRam00c007e0;
  if (param_1 == 0) {
    lVar5 = func_0x003b69d0(0x429e,1);
    if ((lVar5 == 0) && (lVar5 = func_0x003b69d0(0x429e,0xe), lVar5 == 0)) {
      lVar5 = func_0x003b69d0(0x429e,0xc);
      uVar4 = 0;
      bVar2 = bRam003e6d6d;
      if (lVar5 == 0) goto LAB_0001e8c0;
    }
    if (sRam00c0f102 == 0) {
      iVar3 = func_0x003b7568(1);
      uVar4 = (ulong)((int)(uint)(uVar1 >> 0xf) < iVar3);
      bVar2 = bRam003e6d6d;
    }
    else {
      uVar4 = 0;
      bVar2 = bRam003e6d6d;
    }
  }
  else {
    uVar4 = 1;
    bVar2 = bRam003e6d6d;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f38b0);
      uVar4 = (ulong)(bRam003e6d6d < 2);
      if (bRam003e6d6d == 1) {
        uVar4 = func_0x003b6cd0(0x429e,0xe);
        if (uVar4 == 0) {
          bRam003e6d6d = 2;
          bVar2 = bRam003e6d6d;
          goto LAB_0001e8c0;
        }
        lVar5 = func_0x003b6200(0x36f);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6d6d;
          goto LAB_0001e8c0;
        }
        func_0x003b5f40(0x188);
        func_0x003e1430(0x429e,0xe,1);
        func_0x003b6bd0(0x429e,0xe,1);
        bRam003e6d6d = 2;
      }
      else {
        if (uVar4 == 0) {
          if (bRam003e6d6d != 2) {
            return true;
          }
          lVar5 = func_0x003b6cd0(0x429e,0xc);
          if (lVar5 != 0) {
            uVar4 = func_0x003b6200(0x36e);
            bVar2 = bRam003e6d6d;
            if (uVar4 == 0) goto LAB_0001e8c0;
            func_0x003b5f40(0x187);
            func_0x003e1430(0x429e,0xc,1);
            func_0x003b6bd0(0x429e,0xc,1);
          }
          bRam003e6d6d = 0;
          uVar4 = 1;
          bVar2 = bRam003e6d6d;
          goto LAB_0001e8c0;
        }
        bVar2 = bRam003e6d6d;
        if (bRam003e6d6d != 0) goto LAB_0001e8c0;
        uVar4 = func_0x003b6cd0(0x429e,1);
        bVar2 = 1;
        if (uVar4 == 0) goto LAB_0001e8c0;
        lVar5 = func_0x003b6200(0x367);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6d6d;
          goto LAB_0001e8c0;
        }
        func_0x003b5f40(0x57);
        func_0x003e1430(0x429e,1,1);
        func_0x003b6bd0(0x429e,1,1);
        bRam003e6d6d = 1;
      }
      uVar4 = 0;
      bVar2 = bRam003e6d6d;
    }
  }
LAB_0001e8c0:
  bRam003e6d6d = bVar2;
  return (bool)(char)uVar4;
}



bool FUN_0001e8d0(short param_1)

{
  ushort uVar1;
  byte bVar2;
  int iVar3;
  ulong uVar4;
  long lVar5;
  
  uVar1 = uRam00c007e0;
  if (param_1 == 0) {
    lVar5 = func_0x003b69d0(0x429e,1);
    if ((lVar5 == 0) && (lVar5 = func_0x003b69d0(0x429e,0xe), lVar5 == 0)) {
      lVar5 = func_0x003b69d0(0x429e,0xc);
      uVar4 = 0;
      bVar2 = bRam003e6d6e;
      if (lVar5 == 0) goto LAB_0001eae8;
    }
    if (sRam00c0f102 == 0) {
      iVar3 = func_0x003b7568(1);
      uVar4 = (ulong)(iVar3 <= (int)(uint)(uVar1 >> 0xf));
      bVar2 = bRam003e6d6e;
    }
    else {
      uVar4 = 0;
      bVar2 = bRam003e6d6e;
    }
  }
  else {
    uVar4 = 1;
    bVar2 = bRam003e6d6e;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f38c0);
      uVar4 = (ulong)(bRam003e6d6e < 2);
      if (bRam003e6d6e == 1) {
        uVar4 = func_0x003b6cd0(0x429e,0xe);
        if (uVar4 == 0) {
          bRam003e6d6e = 2;
          bVar2 = bRam003e6d6e;
          goto LAB_0001eae8;
        }
        lVar5 = func_0x003b6200(899);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6d6e;
          goto LAB_0001eae8;
        }
        func_0x003b5f40(0x84);
        func_0x003e1430(0x429e,0xe,1);
        func_0x003b6bd0(0x429e,0xe,1);
        bRam003e6d6e = 2;
      }
      else {
        if (uVar4 == 0) {
          if (bRam003e6d6e != 2) {
            return true;
          }
          lVar5 = func_0x003b6cd0(0x429e,0xc);
          if (lVar5 != 0) {
            uVar4 = func_0x003b6200(0x382);
            bVar2 = bRam003e6d6e;
            if (uVar4 == 0) goto LAB_0001eae8;
            func_0x003b5f40(0x84);
            func_0x003e1430(0x429e,0xc,1);
            func_0x003b6bd0(0x429e,0xc,1);
          }
          bRam003e6d6e = 0;
          uVar4 = 1;
          bVar2 = bRam003e6d6e;
          goto LAB_0001eae8;
        }
        bVar2 = bRam003e6d6e;
        if (bRam003e6d6e != 0) goto LAB_0001eae8;
        uVar4 = func_0x003b6cd0(0x429e,1);
        bVar2 = 1;
        if (uVar4 == 0) goto LAB_0001eae8;
        lVar5 = func_0x003b6200(0x37b);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6d6e;
          goto LAB_0001eae8;
        }
        func_0x003b5f40(0x59);
        func_0x003e1430(0x429e,1,1);
        func_0x003b6bd0(0x429e,1,1);
        func_0x003b7620(0x429e,1,2,10);
        bRam003e6d6e = 1;
        uRam003e2c86 = 2;
      }
      uVar4 = 0;
      bVar2 = bRam003e6d6e;
    }
  }
LAB_0001eae8:
  bRam003e6d6e = bVar2;
  return (bool)(char)uVar4;
}



bool FUN_0001eaf8(void)

{
  bool bVar1;
  long lVar2;
  
  lVar2 = func_0x001251e8();
  if (lVar2 == 0) {
    lVar2 = func_0x00125258();
    if (lVar2 == 0) {
      bVar1 = false;
      if ((sRam00c084fc == 2) && (bVar1 = false, sRam00c0853c == 0x2a)) {
        bVar1 = false;
        if (sRam00c08500 == 1) {
          bVar1 = sRam00c08540 == 1;
        }
      }
    }
    else {
      bVar1 = false;
    }
  }
  else {
    bVar1 = false;
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

long FUN_0001eb88(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  lVar1 = 1;
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f38d0);
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 0) {
      func_0x003b7130(2,0x2a);
      func_0x003b7440(0x429e);
      func_0x003b6aa0(0x429e,0x2a,1);
      lVar1 = 1;
    }
  }
  return lVar1;
}



ulong FUN_0001ec18(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x0039f3f8();
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f38e0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003de9c8(2,0x2a);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0001eca8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x0039f3f8();
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f38f0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6ea8(0x429e,0x2a,uRam00c00ade);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0001ed40(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0853c == 0x2b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3900);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(0x2a,0x38);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001edd8(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 == 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3910);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0001ee68(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 != 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3920);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001ef20(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x2b && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3930);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001efa8(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 != 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3940);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0001f038(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 == 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3950);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001f0f0(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    if ((sRam00c084fc == 2) && (lVar2 = func_0x003b69d0(0x429e,0x3a), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x429e,0x39);
      bVar1 = lVar2 != 0;
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3960);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0001f190(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 6) {
      uVar1 = (ulong)(sRam00c0853c == 1);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3970);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(6,1);
        func_0x003b71d8(0x427e,6);
        func_0x003b71d8(0x429e,1);
        func_0x003b74b0();
        func_0x003b7548();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_0001f250(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 1) {
      if (sRam00c084fc == 6) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3980);
      uVar1 = (ulong)(cRam003e6d70 < '\x02');
      if (cRam003e6d70 == '\x01') {
        lVar2 = func_0x003b6200(0x5bc);
        if (lVar2 == 0) {
          uVar1 = 0;
          goto LAB_0001f38c;
        }
        cRam003e6d70 = '\x02';
      }
      else {
        if (uVar1 == 0) {
          if (cRam003e6d70 != '\x02') {
            if (cRam003e6d70 != '\x03') {
              return true;
            }
            uVar1 = func_0x003b6200(0x5b4);
            if (uVar1 != 0) {
              cRam003e6d70 = '\0';
              uVar1 = 1;
            }
            goto LAB_0001f38c;
          }
          lVar2 = func_0x003b67b8(0x1e,0x5bd,1);
          if (lVar2 != 0) {
            uVar1 = 0;
            goto LAB_0001f38c;
          }
          cRam003e6d70 = '\x03';
        }
        else {
          if (cRam003e6d70 != '\0') goto LAB_0001f38c;
          lVar2 = func_0x003b6290(param_2);
          if (lVar2 == 0) {
            uVar1 = 0;
            goto LAB_0001f38c;
          }
          cRam003e6d70 = '\x01';
        }
      }
      uVar1 = 0;
    }
  }
LAB_0001f38c:
  return (bool)(char)uVar1;
}



bool FUN_0001f3a0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 6) {
      if (sRam00c0853c == 1) {
        uVar1 = (ulong)(sRam00c0f102 != 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3990);
      uVar1 = (ulong)(cRam003e6d71 < '\x02');
      if (cRam003e6d71 == '\x01') {
        lVar2 = func_0x003b67b8(0x1e,0x5ca,1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_0001f4bc;
        }
        cRam003e6d71 = '\x02';
      }
      else {
        if (uVar1 == 0) {
          if (cRam003e6d71 != '\x02') {
            return true;
          }
          uVar1 = func_0x003b6200(0x5cb);
          if (uVar1 != 0) {
            cRam003e6d71 = '\0';
            uVar1 = 1;
          }
          goto LAB_0001f4bc;
        }
        if (cRam003e6d71 != '\0') goto LAB_0001f4bc;
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 0) {
          uVar1 = 0;
          goto LAB_0001f4bc;
        }
        cRam003e6d71 = '\x01';
      }
      uVar1 = 0;
    }
  }
LAB_0001f4bc:
  return (bool)(char)uVar1;
}



ulong FUN_0001f4d0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x65);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0853c == 0x55);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      uVar1 = func_0x003b5eb0(0x3f39a0);
      if (cRam003e6d72 == '\0') {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 1) {
          cRam003e6d72 = '\x01';
        }
        uVar1 = 0;
      }
      else {
        if (cRam003e6d72 == '\x01') {
          lVar2 = func_0x003e13d0(0x427e,100);
          if (lVar2 < 0) {
            uRam00c03612 = 1;
            uRam00c001f8 = 6;
          }
          else {
            func_0x003b6c48(0x427e,100,1);
          }
          func_0x003b7130(0x65,0x55);
          cRam003e6d72 = '\0';
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_0001f5f8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x55) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f39b0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x55,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0001f6a0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x55 && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f39c0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



bool FUN_0001f728(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x55;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f39d0);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



ulong FUN_0001f798(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x55) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f39e0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x55,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_0001f840(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f39f0);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_0001f930(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3a00);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_0001fa30(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3a10);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_0001fb20(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3a20);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



ulong FUN_0001fc20(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    lVar1 = func_0x003b69d0(0x427e,1);
    uVar2 = 0;
    if (lVar1 != 0) {
      lVar1 = func_0x003b69d0(0x427e,0);
      uVar2 = 0;
      if (lVar1 != 0) {
        if (sRam00c08500 == 2) {
          lVar1 = func_0x003b69d0(0x429e,0x54);
          uVar2 = 0;
          if (lVar1 != 0) {
            lVar1 = func_0x003b69d0(0x429e,0x7f);
            uVar2 = (ulong)(lVar1 != 0);
          }
        }
        else {
          uVar2 = 0;
        }
      }
    }
  }
  else {
    uVar2 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3a30);
      uVar2 = func_0x003b6290(param_2);
      if (uVar2 != 0) {
        func_0x003b7130(1,0x54);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



bool FUN_0001fcf0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,1);
      uVar1 = 0;
      if (lVar2 != 0) {
        if (sRam00c08500 == 2) {
          lVar2 = func_0x003b69d0(0x429e,0x54);
          if (lVar2 == 0) {
            uVar1 = 0;
          }
          else {
            lVar2 = func_0x003b69d0(0x429e,0x7f);
            if (lVar2 == 0) {
              uVar1 = 0;
            }
            else {
              uVar1 = (ulong)(sRam00c0f102 == 0);
            }
          }
        }
        else {
          uVar1 = 0;
        }
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3a40);
      uVar1 = (ulong)(bRam003e6d73 < 2);
      if (bRam003e6d73 == 1) {
        lVar2 = func_0x003b67b8(0x34,0x796,1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_0001fe4c;
        }
        bRam003e6d73 = 2;
      }
      else {
        if (uVar1 == 0) {
          if (bRam003e6d73 != 2) {
            return true;
          }
          uVar1 = func_0x003b6200(0x797);
          if (uVar1 != 0) {
            bRam003e6d73 = 0;
            uVar1 = 1;
          }
          goto LAB_0001fe4c;
        }
        if (bRam003e6d73 != 0) goto LAB_0001fe4c;
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 0) {
          uVar1 = 0;
          goto LAB_0001fe4c;
        }
        bRam003e6d73 = 1;
      }
      uVar1 = 0;
    }
  }
LAB_0001fe4c:
  return (bool)(char)uVar1;
}



ulong FUN_0001fe60(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      if (sRam00c08500 == 1) {
        uVar1 = (ulong)(sRam00c0853c == 0x3b);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3a50);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b74b0();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0001ff10(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      uVar1 = (ulong)(sRam00c0853c == 0x3b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3a60);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x427e,0x7c,1);
        uRam00c0f102 = 1;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0001ffb8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 2) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c01d42 == 0x19);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3a70);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6aa0(0x429e,0x3b,8);
        func_0x003b6b20(0x429e,0x3b,0xa0);
        sRam00c01706 = sRam00c01706 + 8;
        sRam00c03dc4 = sRam00c03dc4 + 0xa0;
        sRam00c03f1a = sRam00c03f1a + 0xa0;
        sRam00c04070 = sRam00c04070 + 0xa0;
        sRam00c041c6 = sRam00c041c6 + 0xa0;
        sRam00c0431c = sRam00c0431c + 0xa0;
        func_0x003b7130(2,0x3b);
        func_0x003b71d8(0x427e,2);
        func_0x003b71d8(0x429e,0x3b);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00020110(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 2) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3a80);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000201b8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x45;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3a90);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00020230(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x45) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3aa0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        uRam00c08648 = 0;
        func_0x003e1430(0x429e,0x45,1);
        func_0x003e1430(0x429e,0x46,1);
        func_0x003e1430(0x429e,0x47,1);
        func_0x003b5f78(0x141);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_000202f8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 2) {
      uVar1 = (ulong)(sRam00c0853c == 0x3e);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3ab0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(2,0x3e);
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b71d8(0x427e,2);
        func_0x003b71d8(0x429e,0x3e);
        func_0x003b7440(0x429e);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000203c8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c084fc == 2) {
      if (sRam00c0853c == 0x3e) {
        bVar1 = cRam003e2f59 == '\0';
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3ac0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00020468(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      lVar2 = func_0x003b69d0(0x427e,0x3e);
      if (lVar2 == 0) {
        lVar2 = func_0x003b69d0(0x429e,0x3f);
        bVar1 = lVar2 != 0;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3ad0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00020510(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      lVar2 = func_0x003b69d0(0x427e,0x3e);
      if (lVar2 == 0) {
        lVar2 = func_0x003b69d0(0x429e,0x3f);
        if (lVar2 == 0) {
          uVar1 = 0;
        }
        else {
          uVar1 = (ulong)(sRam00c0f102 == 0);
        }
      }
      else {
        uVar1 = 0;
      }
    }
    else {
      uVar1 = 0;
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3ae0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3f,3);
        func_0x003b6bd0(0x429e,0x3f,0x3f);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000205e0(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      lVar2 = func_0x003b69d0(0x427e,0x3e);
      if (lVar2 == 0) {
        lVar2 = func_0x003b69d0(0x429e,0x3f);
        if (lVar2 == 0) {
          bVar1 = false;
        }
        else {
          bVar1 = sRam00c0f102 != 0;
        }
      }
      else {
        bVar1 = false;
      }
    }
    else {
      bVar1 = false;
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3af0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00020698(short param_1)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = (ulong)(sRam00c0853c == 0x3f);
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3b00);
      uVar1 = (ulong)(bRam003e6d74 < 5);
      if (uVar1 != 0) {
                    // WARNING: Could not emulate address calculation at 0x00020704
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((uint)bRam003e6d74 * 4 + 0x3f3b10))();
        return uVar1;
      }
    }
  }
  return uVar1;
}



ulong FUN_000207e8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    lVar2 = func_0x003b69d0(0x429e,0x3f);
    if (lVar2 != 0) {
      lVar2 = func_0x003b69d0(0x427e,2);
      if ((lVar2 == 0) && (lVar2 = func_0x003b69d0(0x427e,0x3e), lVar2 == 0)) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3b38);
      uVar1 = SEXT18(cRam003e6d75);
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 0) {
          cRam003e6d75 = '\x01';
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6f20(0x427e,0x3f3b28,4), uVar1 != 0)) {
          func_0x003e1430(0x429e,0x3f,1);
          cRam003e6d75 = '\0';
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00020900(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x29 && sRam00c08540 == 1;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3b48);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00020990(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3b58);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        lVar2 = func_0x003e14f0(0x429e,0x29);
        if (lVar2 == 4) {
          func_0x003e1430(0x429e,0x29,3);
          func_0x003b6bd0(0x429e,0x29,uRam00c002d8);
        }
        func_0x003b75b8(0x33);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00020a70(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        bVar1 = sRam00c0f102 != 0;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3b68);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00020b10(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 2) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3b78);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(2,0x28);
        func_0x003b6ea8(0x427e,2,0);
        func_0x003b6ea8(0x429e,0x28,0);
        func_0x003b74b0();
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b71d8(0x427e,2);
        func_0x003b71d8(0x429e,0x28);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00020c00(short param_1)

{
  ulong uVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 2) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3b88);
      uVar3 = 0xe07;
      if (sRam00c0f102 == 0) {
        uVar3 = 0xe08;
      }
      uVar1 = func_0x003b6200(uVar3);
      if (uVar1 != 0) {
        func_0x003b6ea8(0x427e,2,uRam00c00a8e);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_00020ca8(short param_1)

{
  ulong uVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    uVar1 = (ulong)(sRam00c0853c == 0x68);
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3b98);
      if (bRam003e6d7c == 1) {
        uVar3 = 0xfab;
      }
      else {
        if (bRam003e6d7c < 2) {
          if (bRam003e6d7c != 0) {
            return bRam003e6d7c < 2;
          }
          lVar2 = func_0x003b69d0(0x427e,2);
          uVar1 = 0;
          bRam003e6d7c = (lVar2 == 0) + 1;
          goto LAB_00020db4;
        }
        if (bRam003e6d7c != 2) {
          if (bRam003e6d7c != 3) {
            return true;
          }
          uVar1 = func_0x003b6200(0xfad);
          if (uVar1 != 0) {
            bRam003e6d7c = 0;
            uVar1 = 1;
          }
          goto LAB_00020db4;
        }
        uVar3 = 0xfac;
      }
      lVar2 = func_0x003b67b8(0x5a,uVar3,1);
      if (lVar2 == 0) {
        bRam003e6d7c = 3;
        uVar1 = 0;
      }
      else {
        uVar1 = 0;
      }
    }
  }
LAB_00020db4:
  return (bool)(char)uVar1;
}



ulong FUN_00020dc8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  ushort *puVar3;
  int iVar4;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x68) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3bb0);
      uVar1 = func_0x003b6290(param_2);
      iVar4 = 0;
      if (uVar1 != 0) {
        puVar3 = (ushort *)0x3f3ba8;
        do {
          lVar2 = func_0x003e14f0(0x429e,*puVar3);
          if (lVar2 == 4) {
            func_0x003b6bd0(0x429e,*puVar3,*(undefined2 *)((uint)*puVar3 * 2 + 0xc00286));
            func_0x003e1430(0x429e,*puVar3,3);
          }
          iVar4 = (iVar4 + 1) * 0x1000000 >> 0x18;
          puVar3 = puVar3 + 1;
        } while (iVar4 < 4);
        uVar1 = 1;
        uRam003e2c92 = uRam00c01208;
      }
    }
  }
  return uVar1;
}



bool FUN_00020ed8(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
    return true;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f3bc0);
  if (cRam003e6d7d == '\x01') {
    lVar1 = func_0x003b6200(0x2334);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6d7d = 2;
  }
  else {
    if (cRam003e6d7d >= '\x02') {
      if (cRam003e6d7d != '\x02') {
        return false;
      }
      uRam00c0853c = 0x78;
      uRam00c08542 = 0x78;
      uRam00c00b7a = 100;
      uRam00c08540 = 1;
      uRam00c08558 = 3;
      uRam00c00a24 = 0;
      func_0x00389580(0x3e2c80,0x78);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 5;
      uRam00c08502 = 5;
      uRam00c08504 = 7;
      uRam00c0851a = 3;
      uRam00c08500 = 2;
      uRam00c08518 = 3;
      func_0x00389580(0x3e2a38,5);
      func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
      func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396a98(0x42,0x1073f00,uRam00c08504);
      uRam003e2a30 = 0;
      uRam003e2f5a = 0;
      cRam003e6d7d = 0;
      return true;
    }
    if (cRam003e6d7d != '\0') {
      return cRam003e6d7d < '\x02';
    }
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6d7d = 1;
  }
  return false;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000210f8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3bd0);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_00021170(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f3be0);
    uVar1 = 0x3e0000;
    if (cRam003e6d7e == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6d7e = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6d7e == '\x01') {
        uRam00c0853c = 0x79;
        uRam00c08542 = 0x79;
        uRam00c00a26 = 9;
        uRam00c08540 = 1;
        uRam00c00b7c = 100;
        uRam00c08558 = 3;
        func_0x00389580(0x3e2c80,0x79);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c084fc = 3;
        uRam00c08502 = 3;
        uRam00c08504 = 4;
        uRam00c08500 = 2;
        uRam00c0851a = 3;
        uRam00c08518 = 3;
        func_0x00389580(0x3e2a38,3);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        cRam003e6d7e = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00021358(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3bf0);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_000213d0(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f3c00);
    uVar1 = 0x3e0000;
    if (cRam003e6d7f == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6d7f = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6d7f == '\x01') {
        uRam00c0853c = 0x8d;
        uRam00c08542 = 0x8d;
        uRam00c08558 = 3;
        uRam00c00a4e = 0;
        uRam00c00ba4 = 0;
        uRam00c047c2 = 0;
        uRam00c08540 = 1;
        func_0x00389580(0x3e2c80,0x8d);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c084fc = 0;
        uRam00c08502 = 0;
        uRam00c08506 = 6;
        uRam00c08500 = 3;
        uRam00c08518 = 3;
        uRam00c0851a = 3;
        uRam00c0851c = 3;
        uRam00c08504 = 1;
        func_0x00389580(0x3e2a38,0);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00389580(0x3e2aac,(undefined)uRam00c08506);
        func_0x00396e38(0x3e3008,0,0,2,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        func_0x00396a98(0x42,0x1074800,uRam00c08506);
        cRam003e6d7f = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000215f8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3c10);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



bool FUN_00021670(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 == 0) || (param_1 != 1)) goto LAB_00021834;
  func_0x003b5eb0(0x3f3c20);
  uVar1 = (ulong)(cRam003e6d80 < '\x02');
  if (cRam003e6d80 == '\x01') {
    uRam00c0853c = 0x7e;
    uRam00c08542 = 0x7e;
    uRam00c08558 = 3;
    uRam00c08540 = 1;
    func_0x00389580(0x3e2c80,0x7e);
    func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
    func_0x00396a98(0x42,0x1079000,uRam00c08542);
    uRam00c084fc = 2;
    uRam00c08502 = 2;
    uRam00c08518 = 3;
    uRam00c08500 = 1;
    func_0x00389580(0x3e2a38,2);
    func_0x00396e38(0x3e3008,0,0,0,0x7fff,0);
    func_0x00396a98(0x42,0x1073600,uRam00c08502);
    func_0x00396420();
    cRam003e6d80 = '\x02';
    uRam003e2a30 = 1;
  }
  else {
    if (uVar1 == 0) {
      if (cRam003e6d80 != '\x02') {
        return true;
      }
      uVar1 = func_0x003b6428();
      if (uVar1 != 0) {
        cRam003e6d80 = '\0';
        uRam003ecf72 = 0;
        uVar1 = 1;
      }
      goto LAB_00021834;
    }
    if (cRam003e6d80 != '\0') goto LAB_00021834;
    lVar2 = func_0x003b6290(param_2);
    if (lVar2 != 1) {
      uVar1 = 0;
      goto LAB_00021834;
    }
    cRam003e6d80 = '\x01';
  }
  uVar1 = 0;
LAB_00021834:
  return (bool)(char)uVar1;
}



ulong FUN_00021858(short param_1,undefined8 param_2,uint param_3)

{
  ushort uVar1;
  int iVar2;
  ulong uVar3;
  long lVar4;
  uint uVar5;
  
  uVar5 = param_3 & 0xffff;
  if (param_1 == 0) {
    uVar1 = *(ushort *)(uVar5 * 2 + 0xc007de);
    uVar3 = 0;
    if (uVar5 == uRam00c0853c) {
      if (sRam00c0f102 == 0) {
        lVar4 = func_0x003b6cd0(0x429e,uVar5);
        if (lVar4 == 0) {
          uVar3 = 0;
        }
        else {
          iVar2 = func_0x003b7568(uVar5);
          uVar3 = (ulong)((int)(uint)(uVar1 >> 0xf) < iVar2);
        }
      }
      else {
        uVar3 = 0;
      }
    }
  }
  else {
    uVar3 = 1;
    if ((param_1 == 1) && (uVar3 = func_0x003b6290(param_2), uVar3 != 0)) {
      func_0x003e1430(0x429e,uVar5,1);
      func_0x003b6bd0(0x429e,uVar5,uVar5);
      uVar3 = 1;
    }
  }
  return uVar3;
}



bool FUN_00021938(short param_1,int param_2,short param_3,ulong param_4,undefined2 param_5)

{
  ulong uVar1;
  short sVar2;
  
  if (param_1 == 0) {
    uVar1 = param_4 & 0xffff ^ (ulong)uRam00c0853c;
  }
  else {
    if (param_1 != 1) {
      return true;
    }
    func_0x003b5eb0(0x3f3c30);
    if (param_3 == sRam00c084fc) {
      sVar2 = *(short *)(param_2 + 0x1c);
    }
    else {
      sVar2 = *(short *)(param_2 + 0x1c) + 1;
    }
    uVar1 = func_0x003b67b8(param_5,sVar2,1);
  }
  return uVar1 == 0;
}



// WARNING: Control flow encountered bad instruction data

void FUN_000219e0(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00021aa8(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00021bb8(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



ulong FUN_00021da8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 3) {
      if (sRam00c0853c == 1) {
        uVar1 = (ulong)(sRam00c08540 == 1);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3c70);
      uVar1 = SEXT18(cRam003e6d8b);
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 0) {
          cRam003e6d8b = '\x01';
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0x35e), uVar1 != 0)) {
          func_0x003b6ea8(0x427e,3,uRam00c00a90);
          func_0x003b6ea8(0x429e,1,uRam00c00a8c);
          func_0x003e1430(0x429e,1,1);
          func_0x003b6bd0(0x429e,1,1);
          func_0x003b5f40(0x50);
          func_0x003b5f40(0x57);
          cRam003e6d8b = '\0';
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



bool FUN_00021ef0(short param_1)

{
  ushort uVar1;
  byte bVar2;
  int iVar3;
  ulong uVar4;
  long lVar5;
  
  uVar1 = uRam00c007e0;
  if (param_1 == 0) {
    lVar5 = func_0x003b69d0(0x429e,1);
    if ((lVar5 == 0) && (lVar5 = func_0x003b69d0(0x429e,0xe), lVar5 == 0)) {
      lVar5 = func_0x003b69d0(0x429e,0xc);
      uVar4 = 0;
      bVar2 = bRam003e6d8c;
      if (lVar5 == 0) goto LAB_000220e0;
    }
    if (sRam00c0f102 == 0) {
      iVar3 = func_0x003b7568(1);
      uVar4 = (ulong)((int)(uint)(uVar1 >> 0xf) < iVar3);
      bVar2 = bRam003e6d8c;
    }
    else {
      uVar4 = 0;
      bVar2 = bRam003e6d8c;
    }
  }
  else {
    uVar4 = 1;
    bVar2 = bRam003e6d8c;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3c80);
      uVar4 = (ulong)(bRam003e6d8c < 2);
      if (bRam003e6d8c == 1) {
        uVar4 = func_0x003b6cd0(0x429e,0xe);
        if (uVar4 == 0) {
          bRam003e6d8c = 2;
          bVar2 = bRam003e6d8c;
          goto LAB_000220e0;
        }
        lVar5 = func_0x003b6200(0x36f);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6d8c;
          goto LAB_000220e0;
        }
        func_0x003b5f40(0x188);
        func_0x003e1430(0x429e,0xe,1);
        func_0x003b6bd0(0x429e,0xe,1);
        bRam003e6d8c = 2;
      }
      else {
        if (uVar4 == 0) {
          if (bRam003e6d8c != 2) {
            return true;
          }
          lVar5 = func_0x003b6cd0(0x429e,0xc);
          if (lVar5 != 0) {
            uVar4 = func_0x003b6200(0x36e);
            bVar2 = bRam003e6d8c;
            if (uVar4 == 0) goto LAB_000220e0;
            func_0x003b5f40(0x187);
            func_0x003e1430(0x429e,0xc,1);
            func_0x003b6bd0(0x429e,0xc,1);
          }
          bRam003e6d8c = 0;
          uVar4 = 1;
          bVar2 = bRam003e6d8c;
          goto LAB_000220e0;
        }
        bVar2 = bRam003e6d8c;
        if (bRam003e6d8c != 0) goto LAB_000220e0;
        uVar4 = func_0x003b6cd0(0x429e,1);
        bVar2 = 1;
        if (uVar4 == 0) goto LAB_000220e0;
        lVar5 = func_0x003b6200(0x367);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6d8c;
          goto LAB_000220e0;
        }
        func_0x003b5f40(0x57);
        func_0x003e1430(0x429e,1,1);
        func_0x003b6bd0(0x429e,1,1);
        bRam003e6d8c = 1;
      }
      uVar4 = 0;
      bVar2 = bRam003e6d8c;
    }
  }
LAB_000220e0:
  bRam003e6d8c = bVar2;
  return (bool)(char)uVar4;
}



bool FUN_000220f0(short param_1)

{
  ushort uVar1;
  byte bVar2;
  int iVar3;
  ulong uVar4;
  long lVar5;
  
  uVar1 = uRam00c007e0;
  if (param_1 == 0) {
    lVar5 = func_0x003b69d0(0x429e,1);
    if ((lVar5 == 0) && (lVar5 = func_0x003b69d0(0x429e,0xe), lVar5 == 0)) {
      lVar5 = func_0x003b69d0(0x429e,0xc);
      uVar4 = 0;
      bVar2 = bRam003e6d8d;
      if (lVar5 == 0) goto LAB_00022308;
    }
    if (sRam00c0f102 == 0) {
      iVar3 = func_0x003b7568(1);
      uVar4 = (ulong)(iVar3 <= (int)(uint)(uVar1 >> 0xf));
      bVar2 = bRam003e6d8d;
    }
    else {
      uVar4 = 0;
      bVar2 = bRam003e6d8d;
    }
  }
  else {
    uVar4 = 1;
    bVar2 = bRam003e6d8d;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3c90);
      uVar4 = (ulong)(bRam003e6d8d < 2);
      if (bRam003e6d8d == 1) {
        uVar4 = func_0x003b6cd0(0x429e,0xe);
        if (uVar4 == 0) {
          bRam003e6d8d = 2;
          bVar2 = bRam003e6d8d;
          goto LAB_00022308;
        }
        lVar5 = func_0x003b6200(899);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6d8d;
          goto LAB_00022308;
        }
        func_0x003b5f40(0x84);
        func_0x003e1430(0x429e,0xe,1);
        func_0x003b6bd0(0x429e,0xe,1);
        bRam003e6d8d = 2;
      }
      else {
        if (uVar4 == 0) {
          if (bRam003e6d8d != 2) {
            return true;
          }
          lVar5 = func_0x003b6cd0(0x429e,0xc);
          if (lVar5 != 0) {
            uVar4 = func_0x003b6200(0x382);
            bVar2 = bRam003e6d8d;
            if (uVar4 == 0) goto LAB_00022308;
            func_0x003b5f40(0x84);
            func_0x003e1430(0x429e,0xc,1);
            func_0x003b6bd0(0x429e,0xc,1);
          }
          bRam003e6d8d = 0;
          uVar4 = 1;
          bVar2 = bRam003e6d8d;
          goto LAB_00022308;
        }
        bVar2 = bRam003e6d8d;
        if (bRam003e6d8d != 0) goto LAB_00022308;
        uVar4 = func_0x003b6cd0(0x429e,1);
        bVar2 = 1;
        if (uVar4 == 0) goto LAB_00022308;
        lVar5 = func_0x003b6200(0x37b);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6d8d;
          goto LAB_00022308;
        }
        func_0x003b5f40(0x59);
        func_0x003e1430(0x429e,1,1);
        func_0x003b6bd0(0x429e,1,1);
        func_0x003b7620(0x429e,1,2,10);
        bRam003e6d8d = 1;
        uRam003e2c86 = 2;
      }
      uVar4 = 0;
      bVar2 = bRam003e6d8d;
    }
  }
LAB_00022308:
  bRam003e6d8d = bVar2;
  return (bool)(char)uVar4;
}



bool FUN_00022318(void)

{
  bool bVar1;
  long lVar2;
  
  lVar2 = func_0x001251e8();
  if (lVar2 == 0) {
    lVar2 = func_0x00125258();
    if (lVar2 == 0) {
      bVar1 = false;
      if ((sRam00c084fc == 3) && (bVar1 = false, sRam00c0853c == 0x2a)) {
        bVar1 = false;
        if (sRam00c08500 == 1) {
          bVar1 = sRam00c08540 == 1;
        }
      }
    }
    else {
      bVar1 = false;
    }
  }
  else {
    bVar1 = false;
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

long FUN_000223a8(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  lVar1 = 1;
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f3ca0);
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 0) {
      func_0x003b7130(3,0x2a);
      func_0x003b7440(0x429e);
      lVar1 = 1;
    }
  }
  return lVar1;
}



ulong FUN_00022428(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003a2c18();
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3cb0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003de9c8(3,0x2a);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_000224b8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003a2c18();
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3cc0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6ea8(0x429e,0x2a,uRam00c00ade);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00022550(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0853c == 0x2b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3cd0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(0x2a,0x38);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000225e8(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 == 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3ce0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00022678(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 != 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3cf0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00022730(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x2b && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3d00);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000227b8(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 != 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3d10);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00022848(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 == 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3d20);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00022900(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    if ((sRam00c084fc == 3) && (lVar2 = func_0x003b69d0(0x429e,0x3a), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x429e,0x39);
      bVar1 = lVar2 != 0;
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3d30);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_000229a0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 3) {
      uVar1 = (ulong)(sRam00c0853c == 1);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3d40);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(3,1);
        func_0x003b71d8(0x427e,3);
        func_0x003b71d8(0x429e,1);
        func_0x003b74b0();
        func_0x003b7548();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00022a60(short param_1)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 3) {
      if (sRam00c0853c == 1) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3d68);
      uVar1 = (ulong)(bRam003e6d8f < 6);
      if (uVar1 != 0) {
                    // WARNING: Could not emulate address calculation at 0x00022afc
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((char)bRam003e6d8f * 4 + 0x3f3d80))();
        return uVar1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00022bd8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  int iVar3;
  
  if (param_1 == 0) {
    if (sRam00c084fc != 3) {
      return 0;
    }
    return (ulong)(sRam00c0853c == 0x55);
  }
  if (param_1 != 1) {
    return 1;
  }
  func_0x003b5eb0(0x3f3d98);
  uVar1 = SEXT18(cRam003e6d90);
  if (uVar1 == 0) {
    lVar2 = func_0x003b6290(param_2);
    if (lVar2 != 0) {
      cRam003e6d90 = 1;
      return 0;
    }
    return 0;
  }
  if (uVar1 != 1) {
    return uVar1;
  }
  lVar2 = func_0x003b5f10(0x192);
  if (lVar2 == 0) {
    iVar3 = 0x606;
    lVar2 = func_0x003b5f10(0x193);
    if (lVar2 == 0) goto LAB_00022cb4;
  }
  iVar3 = 0x607;
LAB_00022cb4:
  lVar2 = func_0x003b67b8(0x5a,iVar3,1);
  uVar1 = 0;
  if (lVar2 == 0) {
    if (iVar3 == 0x606) {
      func_0x003b5f40(0x191);
      func_0x003b5f40(0x192);
    }
    lVar2 = func_0x003e13d0(0x427e,0x16);
    if (lVar2 < 0) {
      uRam00c0015c = 6;
      uRam00c03576 = 1;
    }
    else {
      func_0x003b6c48(0x427e,0x16,1);
    }
    cRam003e6d90 = '\0';
    uVar1 = 1;
  }
  return uVar1;
}



ulong FUN_00022d50(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x55) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3da8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x55,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00022de8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x55 && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3db8);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00022e70(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x55;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3dc8);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00022ee8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x55) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3dd8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x55,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_00022f80(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3de8);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_00023070(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3df8);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_00023170(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3e08);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_00023260(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3e18);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



ulong FUN_00023360(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    lVar1 = func_0x003b69d0(0x427e,1);
    uVar2 = 0;
    if (lVar1 != 0) {
      lVar1 = func_0x003b69d0(0x427e,0);
      uVar2 = 0;
      if (lVar1 != 0) {
        if (sRam00c08500 == 2) {
          lVar1 = func_0x003b69d0(0x429e,0x54);
          uVar2 = 0;
          if (lVar1 != 0) {
            lVar1 = func_0x003b69d0(0x429e,0x7f);
            uVar2 = (ulong)(lVar1 != 0);
          }
        }
        else {
          uVar2 = 0;
        }
      }
    }
  }
  else {
    uVar2 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3e28);
      uVar2 = func_0x003b6290(param_2);
      if (uVar2 != 0) {
        func_0x003b7130(1,0x54);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



bool FUN_00023430(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,1);
      uVar1 = 0;
      if (lVar2 != 0) {
        if (sRam00c08500 == 2) {
          lVar2 = func_0x003b69d0(0x429e,0x54);
          if (lVar2 == 0) {
            uVar1 = 0;
          }
          else {
            lVar2 = func_0x003b69d0(0x429e,0x7f);
            if (lVar2 == 0) {
              uVar1 = 0;
            }
            else {
              uVar1 = (ulong)(sRam00c0f102 == 0);
            }
          }
        }
        else {
          uVar1 = 0;
        }
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3e38);
      uVar1 = (ulong)(bRam003e6d91 < 2);
      if (bRam003e6d91 == 1) {
        lVar2 = func_0x003b67b8(0x34,0x796,1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_0002358c;
        }
        bRam003e6d91 = 2;
      }
      else {
        if (uVar1 == 0) {
          if (bRam003e6d91 != 2) {
            return true;
          }
          uVar1 = func_0x003b6200(0x797);
          if (uVar1 != 0) {
            bRam003e6d91 = 0;
            uVar1 = 1;
          }
          goto LAB_0002358c;
        }
        if (bRam003e6d91 != 0) goto LAB_0002358c;
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 0) {
          uVar1 = 0;
          goto LAB_0002358c;
        }
        bRam003e6d91 = 1;
      }
      uVar1 = 0;
    }
  }
LAB_0002358c:
  return (bool)(char)uVar1;
}



ulong FUN_000235a0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      if (sRam00c08500 == 1) {
        uVar1 = (ulong)(sRam00c0853c == 0x3b);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3e48);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b74b0();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00023650(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      uVar1 = (ulong)(sRam00c0853c == 0x3b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3e58);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x427e,0x7c,1);
        uRam00c0f102 = 1;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_000236f8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 3) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c01d44 == 0x17);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3e68);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6aa0(0x429e,0x3b,8);
        func_0x003b6b20(0x429e,0x3b,0xa0);
        sRam00c01706 = sRam00c01706 + 8;
        sRam00c03dc4 = sRam00c03dc4 + 0xa0;
        sRam00c03f1a = sRam00c03f1a + 0xa0;
        sRam00c04070 = sRam00c04070 + 0xa0;
        sRam00c041c6 = sRam00c041c6 + 0xa0;
        sRam00c0431c = sRam00c0431c + 0xa0;
        func_0x003b7130(3,0x3b);
        func_0x003b71d8(0x427e,3);
        func_0x003b71d8(0x429e,0x3b);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00023850(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 3) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3e78);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_000238f8(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x3b);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      bVar1 = sRam00c0f102 == 0;
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3e88);
      func_0x003e1430(0x429e,0x3b,1);
      bVar1 = true;
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00023980(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c084fc == 3) {
      lVar2 = func_0x003b69d0(0x429e,0x4c);
      bVar1 = lVar2 != 0;
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3e98);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00023a10(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x4c) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3ea8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x4c,1);
        func_0x003e1430(0x429e,0x4f,1);
        lVar2 = func_0x003b6cd0(0x429e,0x4d);
        if (lVar2 == 0) {
          func_0x003e1430(0x429e,0x4d,1);
        }
        lVar2 = func_0x003b6cd0(0x429e,0x4e);
        if (lVar2 == 0) {
          func_0x003e1430(0x429e,0x4e,1);
          uVar1 = 1;
        }
        else {
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_00023af8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x4c) {
      uVar1 = (ulong)(sRam00c0f102 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3eb8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x4c,1);
        func_0x003e1430(0x429e,0x4f,1);
        func_0x003e1430(0x429e,0x4d,1);
        func_0x003e1430(0x429e,0x4e,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00023bc0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x45;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3ec8);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00023c38(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x45) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3ed8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        uRam00c0863c = 2;
        func_0x003e1430(0x429e,0x45,1);
        func_0x003e1430(0x429e,0x46,1);
        func_0x003e1430(0x429e,0x47,1);
        func_0x003b5f78(0x141);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00023d00(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 3) {
      uVar1 = (ulong)(sRam00c0853c == 0x3e);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3ee8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(3,0x3e);
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b71d8(0x427e,3);
        func_0x003b71d8(0x429e,0x3e);
        func_0x003b7440(0x429e);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00023dd0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c084fc == 3) {
      if (sRam00c0853c == 0x3e) {
        bVar1 = cRam003e2f59 == '\0';
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3ef8);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00023e70(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 3 && sRam00c0853c == 0x3f;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3f08);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00023f00(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x3f) {
      if (sRam00c084fc == 3) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3f18);
      uVar1 = (ulong)bRam003e6d92;
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 0) {
          bRam003e6d92 = 1;
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0xd36), uVar1 != 0)) {
          lVar2 = func_0x003e14f0(0x429e,0x3f);
          if ((lVar2 == 4) || (lVar2 == 0)) {
            func_0x003b6bd0(0x429e,0x3f,uRam00c00304);
            func_0x003e1430(0x429e,0x3f,3);
          }
          bRam003e6d92 = 0;
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_00024038(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 3) {
      if (sRam00c0853c == 0x3f) {
        uVar1 = (ulong)(sRam00c0f102 != 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3f28);
      uVar1 = (ulong)bRam003e6d93;
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 0) {
          bRam003e6d93 = 1;
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0xd36), uVar1 != 0)) {
          bRam003e6d93 = 0;
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x0002418c: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00024194)
// WARNING: Removing unreachable block (ram,0x0002419c)
// WARNING: Removing unreachable block (ram,0x000241c4)

void FUN_00024128(short param_1)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f3f40);
    lVar1 = func_0x003b6f20(0x427e,0x3f3f38,2);
    if (lVar1 != 0) {
      return;
    }
  }
  return;
}



ulong FUN_000241d8(short param_1)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x3f);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3f50);
      uVar1 = (ulong)(bRam003e6d94 < 5);
      if (uVar1 != 0) {
                    // WARNING: Could not emulate address calculation at 0x00024264
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((char)bRam003e6d94 * 4 + 0x3f3f60))();
        return uVar1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x0002440c: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00024414)
// WARNING: Removing unreachable block (ram,0x0002441c)
// WARNING: Removing unreachable block (ram,0x00024444)

void FUN_000243a8(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f3fd8,0x3f);
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 0) {
      return;
    }
  }
  return;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00024458(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x29 && sRam00c08540 == 1;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3fe8);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_000244e8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f3ff8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        lVar2 = func_0x003e14f0(0x429e,0x29);
        if (lVar2 == 4) {
          func_0x003e1430(0x429e,0x29,3);
          func_0x003b6bd0(0x429e,0x29,uRam00c002d8);
        }
        func_0x003b75b8(0x33);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000245c8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        bVar1 = sRam00c0f102 != 0;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4008);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00024668(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 3) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4018);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(3,0x28);
        func_0x003b6ea8(0x427e,3,0);
        func_0x003b6ea8(0x429e,0x28,0);
        func_0x003b74b0();
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b71d8(0x427e,3);
        func_0x003b71d8(0x429e,0x28);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00024758(short param_1)

{
  ulong uVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 3) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4028);
      uVar1 = (ulong)bRam003e6d95;
      if (uVar1 == 0) {
        uVar3 = 0xe07;
        if (sRam00c0f102 != 0) {
          uVar3 = 0xe08;
        }
        lVar2 = func_0x003b6200(uVar3);
        if (lVar2 != 0) {
          bRam003e6d95 = 1;
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0xe09), uVar1 != 0)) {
          func_0x003b6ea8(0x427e,3,uRam00c00a90);
          bRam003e6d95 = 0;
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



bool FUN_00024850(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc != 3) {
      uVar1 = (ulong)(sRam00c0853c == 0x68);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4038);
      if (cRam003e6d9c == '\x01') {
        lVar2 = func_0x003b67b8(0x5a,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_00024984;
        }
      }
      else {
        if (cRam003e6d9c < '\x02') {
          if (cRam003e6d9c != '\0') {
            return cRam003e6d9c < '\x02';
          }
          lVar2 = func_0x003b5f10(0x192);
          uVar1 = 0;
          cRam003e6d9c = (lVar2 != 0) + '\x01';
          goto LAB_00024984;
        }
        if (cRam003e6d9c != '\x02') {
          if (cRam003e6d9c != '\x03') {
            return true;
          }
          uVar1 = func_0x003b6200(0xfad);
          if (uVar1 != 0) {
            cRam003e6d9c = '\0';
            uVar1 = 1;
          }
          goto LAB_00024984;
        }
        lVar2 = func_0x003b6200(0xfac);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_00024984;
        }
      }
      cRam003e6d9c = '\x03';
      uVar1 = 0;
    }
  }
LAB_00024984:
  return (bool)(char)uVar1;
}



bool FUN_00024998(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 3) {
      uVar1 = (ulong)(sRam00c0853c == 0x68);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4048);
      if (cRam003e6d9d == '\x01') {
        lVar2 = func_0x003b67b8(0x5a,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_00024acc;
        }
      }
      else {
        if (cRam003e6d9d < '\x02') {
          if (cRam003e6d9d != '\0') {
            return cRam003e6d9d < '\x02';
          }
          lVar2 = func_0x003b5f10(0x191);
          uVar1 = 0;
          cRam003e6d9d = (lVar2 != 0) + '\x01';
          goto LAB_00024acc;
        }
        if (cRam003e6d9d != '\x02') {
          if (cRam003e6d9d != '\x03') {
            return true;
          }
          uVar1 = func_0x003b6200(0xfb7);
          if (uVar1 != 0) {
            cRam003e6d9d = '\0';
            uVar1 = 1;
          }
          goto LAB_00024acc;
        }
        lVar2 = func_0x003b6200(0xfb6);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_00024acc;
        }
      }
      cRam003e6d9d = '\x03';
      uVar1 = 0;
    }
  }
LAB_00024acc:
  return (bool)(char)uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00024ae0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 3 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4058);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00024b70(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc != 3 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4068);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00024c00(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc != 3 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4078);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



bool FUN_00024c90(short param_1)

{
  bool bVar1;
  long lVar2;
  undefined4 uVar3;
  ushort *puVar4;
  int iVar5;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x68 && sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4090);
      if (bRam003e6d9e == 1) {
        uVar3 = 0xffb;
      }
      else {
        if (bRam003e6d9e < 2) {
          if (bRam003e6d9e != 0) {
            return bRam003e6d9e < 2;
          }
          lVar2 = func_0x003b5f10(0x198);
          bRam003e6d9e = '\x02' - (lVar2 == 0);
          return false;
        }
        if (bRam003e6d9e != 2) {
          if (bRam003e6d9e != 3) {
            return true;
          }
          iVar5 = 0;
          puVar4 = (ushort *)0x3f4088;
          do {
            lVar2 = func_0x003e14f0(0x429e,*puVar4);
            if (lVar2 == 4) {
              func_0x003b6bd0(0x429e,*puVar4,*(undefined2 *)((uint)*puVar4 * 2 + 0xc00286));
              func_0x003e1430(0x429e,*puVar4,3);
            }
            iVar5 = (iVar5 + 1) * 0x1000000 >> 0x18;
            puVar4 = puVar4 + 1;
          } while (iVar5 < 4);
          uRam003e2c92 = uRam00c01208;
          bRam003e6d9e = 0;
          return true;
        }
        uVar3 = 0xffc;
      }
      lVar2 = func_0x003b6200(uVar3);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        bRam003e6d9e = 3;
        bVar1 = false;
      }
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00024e30(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c084fc != 3) {
      if (sRam00c0853c == 0x68) {
        bVar1 = sRam00c0f102 == 0;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f40a0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



bool FUN_00024ed0(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    uVar2 = 0;
    if (sRam00c084fc == 3) {
      uVar2 = (ulong)(sRam00c0853c == 0x68);
    }
    goto LAB_00024fec;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f40b0);
  if (cRam003e6d9f == '\x01') {
    lVar1 = func_0x003b6200(0x1024);
    if (lVar1 != 1) {
      uVar2 = 0;
      goto LAB_00024fec;
    }
    cRam003e6d9f = '\x02';
  }
  else {
    if (cRam003e6d9f >= '\x02') {
      if (cRam003e6d9f != '\x02') {
        return true;
      }
      uVar2 = func_0x003b6200(0x1025);
      if (uVar2 != 0) {
        func_0x003b6aa0(0x429e,0x68,3);
        cRam003e6d9f = '\0';
        uVar2 = 1;
      }
      goto LAB_00024fec;
    }
    if (cRam003e6d9f != '\0') {
      return cRam003e6d9f < '\x02';
    }
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 1) {
      uVar2 = 0;
      goto LAB_00024fec;
    }
    lVar1 = func_0x003b69d0(0x427e,0x6a);
    cRam003e6d9f = (lVar1 == 0) + '\x01';
  }
  uVar2 = 0;
LAB_00024fec:
  return (bool)(char)uVar2;
}



bool FUN_00025008(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    uVar2 = 0;
    if (sRam00c084fc == 3) {
      if (sRam00c0853c == 0x68) {
        uVar2 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar2 = 0;
      }
    }
    goto LAB_0002519c;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f40c0);
  if (cRam003e6da0 == '\x01') {
    lVar1 = func_0x003b67b8(0x5c,0x102e,uRam00401082);
    if (lVar1 != 0) {
      uVar2 = 0;
      goto LAB_0002519c;
    }
    cRam003e6da0 = (iRam00401084 == 0) + '\x02';
  }
  else {
    if (cRam003e6da0 < '\x02') {
      if (cRam003e6da0 != '\0') {
        return cRam003e6da0 < '\x02';
      }
      lVar1 = func_0x003b6290(param_2);
      if (lVar1 != 1) {
        uVar2 = 0;
        goto LAB_0002519c;
      }
      lVar1 = func_0x003b69d0(0x427e,0x6a);
      uRam00401082 = (ushort)(lVar1 == 0);
      cRam003e6da0 = '\x01';
      iRam00401084 = (int)lVar1;
    }
    else {
      if (cRam003e6da0 != '\x02') {
        if (cRam003e6da0 != '\x03') {
          return true;
        }
        uVar2 = func_0x003b6200(0x1030);
        if (uVar2 != 0) {
          func_0x003b6bd0(0x429e,0x68,0x68);
          func_0x003e1430(0x429e,0x68,1);
          cRam003e6da0 = '\0';
          uVar2 = 1;
        }
        goto LAB_0002519c;
      }
      lVar1 = func_0x003b67b8(0x5c,0x102f,1);
      if (lVar1 != 0) {
        uVar2 = 0;
        goto LAB_0002519c;
      }
      cRam003e6da0 = '\x03';
    }
  }
  uVar2 = 0;
LAB_0002519c:
  return (bool)(char)uVar2;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000251b8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c084fc == 3) {
      if (sRam00c0853c == 0x68) {
        bVar1 = sRam00c0f102 != 0;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f40d0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00025258(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 3 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f40e0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



bool FUN_000252e8(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
    return true;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f40f0);
  if (cRam003e6da1 == '\x01') {
    lVar1 = func_0x003b6200(0x2334);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6da1 = 2;
  }
  else {
    if (cRam003e6da1 >= '\x02') {
      if (cRam003e6da1 != '\x02') {
        return false;
      }
      uRam00c0853c = 0x78;
      uRam00c08542 = 0x78;
      uRam00c00b7a = 100;
      uRam00c08540 = 1;
      uRam00c08558 = 3;
      uRam00c00a24 = 0;
      func_0x00389580(0x3e2c80,0x78);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 5;
      uRam00c08502 = 5;
      uRam00c08504 = 7;
      uRam00c0851a = 3;
      uRam00c08500 = 2;
      uRam00c08518 = 3;
      func_0x00389580(0x3e2a38,5);
      func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
      func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396a98(0x42,0x1073f00,uRam00c08504);
      uRam003e2a30 = 0;
      uRam003e2f5a = 0;
      cRam003e6da1 = 0;
      return true;
    }
    if (cRam003e6da1 != '\0') {
      return cRam003e6da1 < '\x02';
    }
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6da1 = 1;
  }
  return false;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00025508(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4100);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_00025580(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f4110);
    uVar1 = 0x3e0000;
    if (cRam003e6da2 == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6da2 = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6da2 == '\x01') {
        uRam00c0853c = 0x79;
        uRam00c08542 = 0x79;
        uRam00c00a26 = 9;
        uRam00c00b7c = 100;
        uRam00c08558 = 3;
        uRam00c08540 = 1;
        func_0x00389580(0x3e2c80,0x79);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c08502 = 1;
        uRam00c08504 = 4;
        uRam00c08500 = 2;
        uRam00c0851a = 3;
        uRam00c08518 = 3;
        uRam00c084fc = 1;
        func_0x00389580(0x3e2a38,1);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        cRam003e6da2 = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00025770(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4120);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_000257e8(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f4130);
    uVar1 = 0x3e0000;
    if (cRam003e6da3 == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6da3 = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6da3 == '\x01') {
        uRam00c0853c = 0x8d;
        uRam00c08542 = 0x8d;
        uRam00c08540 = 1;
        uRam00c08558 = 3;
        uRam00c00a4e = 0;
        uRam00c00ba4 = 0;
        uRam00c047c2 = 0;
        func_0x00389580(0x3e2c80,0x8d);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c084fc = 0;
        uRam00c08502 = 0;
        uRam00c08504 = 2;
        uRam00c08506 = 6;
        uRam00c08500 = 3;
        uRam00c08518 = 3;
        uRam00c0851a = 3;
        uRam00c0851c = 3;
        func_0x00389580(0x3e2a38,0);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00389580(0x3e2aac,(undefined)uRam00c08506);
        func_0x00396e38(0x3e3008,0,0,2,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        func_0x00396a98(0x42,0x1074800,uRam00c08506);
        cRam003e6da3 = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00025a08(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4140);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00025a80(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f4150);
    uVar1 = (ulong)(cRam003e6da4 < '\x02');
    if (cRam003e6da4 == '\x01') {
      uRam00c0853c = 0x7e;
      uRam00c08542 = 0x7e;
      uRam00c08558 = 3;
      uRam00c08540 = 1;
      func_0x00389580(0x3e2c80,0x7e);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 3;
      uRam00c08502 = 3;
      uRam00c08518 = 3;
      uRam00c08500 = 1;
      func_0x00389580(0x3e2a38,3);
      func_0x00396e38(0x3e3008,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396420();
      uRam003e2a30 = 1;
      uVar1 = 0;
      cRam003e6da4 = '\x02';
    }
    else {
      if (uVar1 == 0) {
        uVar1 = 2;
        if ((cRam003e6da4 == '\x02') && (uVar1 = func_0x003b6428(), uVar1 != 0)) {
          cRam003e6da4 = '\0';
          uRam003ecf72 = 0;
          uVar1 = 1;
        }
      }
      else {
        if (cRam003e6da4 == '\0') {
          lVar2 = func_0x003b6290(param_2);
          if (lVar2 == 1) {
            cRam003e6da4 = '\x01';
          }
          uVar1 = 0;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_00025c60(short param_1,undefined8 param_2,uint param_3)

{
  ushort uVar1;
  int iVar2;
  ulong uVar3;
  long lVar4;
  uint uVar5;
  
  uVar5 = param_3 & 0xffff;
  if (param_1 == 0) {
    uVar1 = *(ushort *)(uVar5 * 2 + 0xc007de);
    uVar3 = 0;
    if (uVar5 == uRam00c0853c) {
      if (sRam00c0f102 == 0) {
        lVar4 = func_0x003b6cd0(0x429e,uVar5);
        if (lVar4 == 0) {
          uVar3 = 0;
        }
        else {
          iVar2 = func_0x003b7568(uVar5);
          uVar3 = (ulong)((int)(uint)(uVar1 >> 0xf) < iVar2);
        }
      }
      else {
        uVar3 = 0;
      }
    }
  }
  else {
    uVar3 = 1;
    if ((param_1 == 1) && (uVar3 = func_0x003b6290(param_2), uVar3 != 0)) {
      func_0x003e1430(0x429e,uVar5,1);
      func_0x003b6bd0(0x429e,uVar5,uVar5);
      uVar3 = 1;
    }
  }
  return uVar3;
}



bool FUN_00025d40(short param_1,int param_2,short param_3,ulong param_4,undefined2 param_5,
                 undefined2 param_6)

{
  ulong uVar1;
  short sVar2;
  
  if (param_1 == 0) {
    uVar1 = param_4 & 0xffff ^ (ulong)uRam00c0853c;
  }
  else {
    if (param_1 != 1) {
      return true;
    }
    func_0x003b5eb0(0x3f4160);
    if (param_3 == sRam00c084fc) {
      sVar2 = *(short *)(param_2 + 0x1c);
    }
    else {
      sVar2 = *(short *)(param_2 + 0x1c) + 1;
      param_5 = param_6;
    }
    uVar1 = func_0x003b67b8(param_5,sVar2,1);
  }
  return uVar1 == 0;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00025df8(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00025e20(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00025e68(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



bool FUN_00025eb0(short param_1,undefined8 param_2)

{
  ushort uVar1;
  int iVar2;
  long lVar3;
  ulong uVar4;
  
  uVar1 = uRam00c007de;
  if (param_1 == 0) {
    uVar4 = 0;
    if (sRam00c0853c == 0) {
      if (sRam00c0f102 == 0) {
        lVar3 = func_0x003b6cd0(0x429e,0);
        if (lVar3 == 0) {
          uVar4 = 0;
        }
        else {
          iVar2 = func_0x003b7568(0);
          uVar4 = (ulong)((int)(uint)(uVar1 >> 0xf) < iVar2);
        }
      }
      else {
        uVar4 = 0;
      }
    }
    goto LAB_00025ffc;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f4170);
  if (cRam003e6da5 == '\0') {
    lVar3 = func_0x003b6290(param_2);
    if (lVar3 != 0) {
      func_0x003e1430(0x429e,0,1);
      func_0x003b6bd0(0x429e,0,0);
      lVar3 = func_0x003b5f10(0x17);
      if (lVar3 == 0) goto LAB_00025ff8;
      lVar3 = func_0x003b69d0(0x427e,2);
      if (lVar3 == 0) {
        uVar4 = 1;
        goto LAB_00025ffc;
      }
      cRam003e6da5 = '\x01';
    }
    uVar4 = 0;
  }
  else {
    if (cRam003e6da5 != '\x01') {
      return (bool)cRam003e6da5;
    }
    uVar4 = func_0x003b6200(0x1d8);
    if (uVar4 == 0) goto LAB_00025ffc;
    func_0x003b6bd0(0x429e,2,0);
    func_0x003e1430(0x429e,2,1);
    cRam003e6da5 = '\0';
LAB_00025ff8:
    uVar4 = 1;
  }
LAB_00025ffc:
  return (bool)(char)uVar4;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00026018(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00026040(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00026088(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_000260d0(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



bool FUN_00026118(short param_1)

{
  ushort uVar1;
  byte bVar2;
  int iVar3;
  ulong uVar4;
  long lVar5;
  
  uVar1 = uRam00c007e0;
  if (param_1 == 0) {
    lVar5 = func_0x003b69d0(0x429e,1);
    if ((lVar5 == 0) && (lVar5 = func_0x003b69d0(0x429e,0xe), lVar5 == 0)) {
      lVar5 = func_0x003b69d0(0x429e,0xc);
      uVar4 = 0;
      bVar2 = bRam003e6da6;
      if (lVar5 == 0) goto LAB_00026308;
    }
    if (sRam00c0f102 == 0) {
      iVar3 = func_0x003b7568(1);
      uVar4 = (ulong)((int)(uint)(uVar1 >> 0xf) < iVar3);
      bVar2 = bRam003e6da6;
    }
    else {
      uVar4 = 0;
      bVar2 = bRam003e6da6;
    }
  }
  else {
    uVar4 = 1;
    bVar2 = bRam003e6da6;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4170);
      uVar4 = (ulong)(bRam003e6da6 < 2);
      if (bRam003e6da6 == 1) {
        uVar4 = func_0x003b6cd0(0x429e,0xe);
        if (uVar4 == 0) {
          bRam003e6da6 = 2;
          bVar2 = bRam003e6da6;
          goto LAB_00026308;
        }
        lVar5 = func_0x003b6200(0x36f);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6da6;
          goto LAB_00026308;
        }
        func_0x003b5f40(0x188);
        func_0x003e1430(0x429e,0xe,1);
        func_0x003b6bd0(0x429e,0xe,1);
        bRam003e6da6 = 2;
      }
      else {
        if (uVar4 == 0) {
          if (bRam003e6da6 != 2) {
            return true;
          }
          lVar5 = func_0x003b6cd0(0x429e,0xc);
          if (lVar5 != 0) {
            uVar4 = func_0x003b6200(0x36e);
            bVar2 = bRam003e6da6;
            if (uVar4 == 0) goto LAB_00026308;
            func_0x003b5f40(0x187);
            func_0x003e1430(0x429e,0xc,1);
            func_0x003b6bd0(0x429e,0xc,1);
          }
          bRam003e6da6 = 0;
          uVar4 = 1;
          bVar2 = bRam003e6da6;
          goto LAB_00026308;
        }
        bVar2 = bRam003e6da6;
        if (bRam003e6da6 != 0) goto LAB_00026308;
        uVar4 = func_0x003b6cd0(0x429e,1);
        bVar2 = 1;
        if (uVar4 == 0) goto LAB_00026308;
        lVar5 = func_0x003b6200(0x367);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6da6;
          goto LAB_00026308;
        }
        func_0x003b5f40(0x57);
        func_0x003e1430(0x429e,1,1);
        func_0x003b6bd0(0x429e,1,1);
        bRam003e6da6 = 1;
      }
      uVar4 = 0;
      bVar2 = bRam003e6da6;
    }
  }
LAB_00026308:
  bRam003e6da6 = bVar2;
  return (bool)(char)uVar4;
}



bool FUN_00026318(short param_1)

{
  ushort uVar1;
  byte bVar2;
  int iVar3;
  ulong uVar4;
  long lVar5;
  
  uVar1 = uRam00c007e0;
  if (param_1 == 0) {
    lVar5 = func_0x003b69d0(0x429e,1);
    if ((lVar5 == 0) && (lVar5 = func_0x003b69d0(0x429e,0xe), lVar5 == 0)) {
      lVar5 = func_0x003b69d0(0x429e,0xc);
      uVar4 = 0;
      bVar2 = bRam003e6da7;
      if (lVar5 == 0) goto LAB_00026530;
    }
    if (sRam00c0f102 == 0) {
      iVar3 = func_0x003b7568(1);
      uVar4 = (ulong)(iVar3 <= (int)(uint)(uVar1 >> 0xf));
      bVar2 = bRam003e6da7;
    }
    else {
      uVar4 = 0;
      bVar2 = bRam003e6da7;
    }
  }
  else {
    uVar4 = 1;
    bVar2 = bRam003e6da7;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4180);
      uVar4 = (ulong)(bRam003e6da7 < 2);
      if (bRam003e6da7 == 1) {
        uVar4 = func_0x003b6cd0(0x429e,0xe);
        if (uVar4 == 0) {
          bRam003e6da7 = 2;
          bVar2 = bRam003e6da7;
          goto LAB_00026530;
        }
        lVar5 = func_0x003b6200(899);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6da7;
          goto LAB_00026530;
        }
        func_0x003b5f40(0x84);
        func_0x003e1430(0x429e,0xe,1);
        func_0x003b6bd0(0x429e,0xe,1);
        bRam003e6da7 = 2;
      }
      else {
        if (uVar4 == 0) {
          if (bRam003e6da7 != 2) {
            return true;
          }
          lVar5 = func_0x003b6cd0(0x429e,0xc);
          if (lVar5 != 0) {
            uVar4 = func_0x003b6200(0x382);
            bVar2 = bRam003e6da7;
            if (uVar4 == 0) goto LAB_00026530;
            func_0x003b5f40(0x84);
            func_0x003e1430(0x429e,0xc,1);
            func_0x003b6bd0(0x429e,0xc,1);
          }
          bRam003e6da7 = 0;
          uVar4 = 1;
          bVar2 = bRam003e6da7;
          goto LAB_00026530;
        }
        bVar2 = bRam003e6da7;
        if (bRam003e6da7 != 0) goto LAB_00026530;
        uVar4 = func_0x003b6cd0(0x429e,1);
        bVar2 = 1;
        if (uVar4 == 0) goto LAB_00026530;
        lVar5 = func_0x003b6200(0x37b);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6da7;
          goto LAB_00026530;
        }
        func_0x003b5f40(0x59);
        func_0x003e1430(0x429e,1,1);
        func_0x003b6bd0(0x429e,1,1);
        func_0x003b7620(0x429e,1,2,10);
        bRam003e6da7 = 1;
        uRam003e2c86 = 2;
      }
      uVar4 = 0;
      bVar2 = bRam003e6da7;
    }
  }
LAB_00026530:
  bRam003e6da7 = bVar2;
  return (bool)(char)uVar4;
}



bool FUN_00026540(void)

{
  bool bVar1;
  long lVar2;
  
  lVar2 = func_0x001251e8();
  if (lVar2 == 0) {
    lVar2 = func_0x00125258();
    if (lVar2 == 0) {
      bVar1 = false;
      if ((sRam00c084fc == 4) && (bVar1 = false, sRam00c0853c == 0x2a)) {
        bVar1 = false;
        if (sRam00c08500 == 1) {
          bVar1 = sRam00c08540 == 1;
        }
      }
    }
    else {
      bVar1 = false;
    }
  }
  else {
    bVar1 = false;
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

long FUN_000265d0(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  lVar1 = 1;
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f4190);
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 0) {
      func_0x003b7130(4,0x2a);
      func_0x003b7440(0x429e);
      func_0x003b6aa0(0x429e,0x2a,1);
      lVar1 = 1;
    }
  }
  return lVar1;
}



ulong FUN_00026660(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003a6e40();
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f41a0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003de9c8(4,0x2a);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000266f0(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003a6e40();
    bVar1 = lVar2 != 0 && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f41b0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00026778(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0853c == 0x2b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f41c0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(0x2a,0x38);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00026810(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 == 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f41d0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_000268a0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 != 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f41e0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00026958(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x2b && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f41f0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000269e0(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 != 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4200);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00026a70(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 == 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4210);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00026b28(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    if ((sRam00c084fc == 4) && (lVar2 = func_0x003b69d0(0x429e,0x3a), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x429e,0x39);
      bVar1 = lVar2 != 0;
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4220);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00026bc8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 4) {
      uVar1 = (ulong)(sRam00c0853c == 1);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4230);
      uVar1 = SEXT18(cRam003e6da8);
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 1) {
          cRam003e6da8 = '\x01';
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0x5ac), uVar1 != 0)) {
          func_0x003b7130(4,1);
          func_0x003b71d8(0x427e,4);
          func_0x003b71d8(0x429e,1);
          func_0x003b74b0();
          func_0x003b7548();
          cRam003e6da8 = '\0';
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_00026cd8(short param_1)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 4) {
      if (sRam00c0853c == 1) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4258);
      uVar1 = (ulong)(bRam003e6da9 < 6);
      if (uVar1 != 0) {
                    // WARNING: Could not emulate address calculation at 0x00026d74
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((char)bRam003e6da9 * 4 + 0x3f4270))();
        return uVar1;
      }
    }
  }
  return uVar1;
}



bool FUN_00026e50(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4288);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_00026f40(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4298);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_00027040(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f42a8);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_00027130(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f42b8);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



ulong FUN_00027230(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    lVar1 = func_0x003b69d0(0x427e,0);
    uVar2 = 0;
    if (lVar1 != 0) {
      lVar1 = func_0x003b69d0(0x427e,1);
      uVar2 = 0;
      if (lVar1 != 0) {
        if (sRam00c08500 == 2) {
          lVar1 = func_0x003b69d0(0x429e,0x54);
          uVar2 = 0;
          if (lVar1 != 0) {
            lVar1 = func_0x003b69d0(0x429e,0x7f);
            uVar2 = (ulong)(lVar1 != 0);
          }
        }
        else {
          uVar2 = 0;
        }
      }
    }
  }
  else {
    uVar2 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f42c8);
      uVar2 = func_0x003b6290(param_2);
      if (uVar2 != 0) {
        func_0x003b7130(1,0x54);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



bool FUN_00027300(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,1);
      uVar1 = 0;
      if (lVar2 != 0) {
        if (sRam00c08500 == 2) {
          lVar2 = func_0x003b69d0(0x429e,0x54);
          if (lVar2 == 0) {
            uVar1 = 0;
          }
          else {
            lVar2 = func_0x003b69d0(0x429e,0x7f);
            if (lVar2 == 0) {
              uVar1 = 0;
            }
            else {
              uVar1 = (ulong)(sRam00c0f102 == 0);
            }
          }
        }
        else {
          uVar1 = 0;
        }
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f42d8);
      uVar1 = (ulong)(bRam003e6daa < 2);
      if (bRam003e6daa == 1) {
        lVar2 = func_0x003b67b8(0x34,0x796,1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_0002745c;
        }
        bRam003e6daa = 2;
      }
      else {
        if (uVar1 == 0) {
          if (bRam003e6daa != 2) {
            return true;
          }
          uVar1 = func_0x003b6200(0x797);
          if (uVar1 != 0) {
            bRam003e6daa = 0;
            uVar1 = 1;
          }
          goto LAB_0002745c;
        }
        if (bRam003e6daa != 0) goto LAB_0002745c;
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_0002745c;
        }
        bRam003e6daa = 1;
      }
      uVar1 = 0;
    }
  }
LAB_0002745c:
  return (bool)(char)uVar1;
}



ulong FUN_00027470(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      if (sRam00c08500 == 1) {
        uVar1 = (ulong)(sRam00c0853c == 0x3b);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f42e8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b74b0();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00027520(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      uVar1 = (ulong)(sRam00c0853c == 0x3b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f42f8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x427e,0x7c,1);
        uRam00c0f102 = 1;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_000275c8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 4) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c01d46 == 0x19);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4308);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6aa0(0x429e,0x3b,8);
        func_0x003b6b20(0x429e,0x3b,0xa0);
        sRam00c01706 = sRam00c01706 + 8;
        sRam00c03dc4 = sRam00c03dc4 + 0xa0;
        sRam00c03f1a = sRam00c03f1a + 0xa0;
        sRam00c04070 = sRam00c04070 + 0xa0;
        sRam00c041c6 = sRam00c041c6 + 0xa0;
        sRam00c0431c = sRam00c0431c + 0xa0;
        func_0x003b7130(4,0x3b);
        func_0x003b71d8(0x427e,4);
        func_0x003b71d8(0x429e,0x3b);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00027720(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 4) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4318);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000277c8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x45;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4328);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00027840(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x45) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4338);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        uRam00c08638 = 0;
        func_0x003b5f78(0x141);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_000278d8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 4) {
      uVar1 = (ulong)(sRam00c0853c == 0x3e);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4348);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(4,0x3e);
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b71d8(0x427e,4);
        func_0x003b71d8(0x429e,0x3e);
        func_0x003b7440(0x429e);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000279a8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c084fc == 4) {
      if (sRam00c0853c == 0x3e) {
        bVar1 = cRam003e2f59 == '\0';
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4358);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00027a48(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 4 && sRam00c0853c == 0x3f;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4368);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00027ad8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x3f) {
      if (sRam00c084fc == 4) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4378);
      uVar1 = (ulong)bRam003e6dab;
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 1) {
          bRam003e6dab = 1;
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0xd36), uVar1 != 0)) {
          lVar2 = func_0x003e14f0(0x429e,0x3f);
          if ((lVar2 == 4) || (lVar2 == 0)) {
            func_0x003e1430(0x429e,0x3f,3);
            func_0x003b6bd0(0x429e,0x3f,uRam00c00304);
          }
          bRam003e6dab = 0;
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_00027c10(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 4) {
      if (sRam00c0853c == 0x3f) {
        uVar1 = (ulong)(sRam00c0f102 != 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4388);
      uVar1 = (ulong)bRam003e6dac;
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 1) {
          bRam003e6dac = 1;
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0xd36), uVar1 != 0)) {
          bRam003e6dac = 0;
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x00027d64: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00027d6c)
// WARNING: Removing unreachable block (ram,0x00027d74)
// WARNING: Removing unreachable block (ram,0x00027d9c)

void FUN_00027d00(short param_1)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f43a0);
    lVar1 = func_0x003b6f20(0x427e,0x3f4398,2);
    if (lVar1 != 0) {
      return;
    }
  }
  return;
}



bool FUN_00027db0(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
    lVar1 = func_0x003b69d0(0x429e,0x3f);
    if (lVar1 != 0) {
      return sRam00c0f102 == 0;
    }
    return false;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f43b0);
  if (cRam003e6dad == '\x01') {
    cRam003e6dad = 2;
    if (uRam00c084fc == 0x19) {
      uRam00401088 = 0xd73;
    }
    else {
      if (uRam00c084fc < 0x1a) {
        if (uRam00c084fc == 0x18) {
          cRam003e6dad = 2;
          uRam00401088 = 0xd72;
          return false;
        }
      }
      else {
        if (uRam00c084fc == 0x1a) {
          cRam003e6dad = 2;
          uRam00401088 = 0xd74;
          return false;
        }
        if (uRam00c084fc == 0x1b) {
          cRam003e6dad = 2;
          uRam00401088 = 0xd75;
          return false;
        }
      }
      cRam003e6dad = 3;
    }
  }
  else {
    if (cRam003e6dad < '\x02') {
      if (cRam003e6dad != '\0') {
        return cRam003e6dad < '\x02';
      }
      lVar1 = func_0x003b6290(param_2);
      if (lVar1 != 1) {
        return false;
      }
      cRam003e6dad = 1;
    }
    else {
      if (cRam003e6dad != '\x02') {
        if (cRam003e6dad == '\x03') {
          func_0x003e1430(0x429e,0x3f,1);
          cRam003e6dad = 0;
          return true;
        }
        return true;
      }
      lVar1 = func_0x003b6200(uRam00401088);
      if (lVar1 != 1) {
        return false;
      }
      cRam003e6dad = 3;
    }
  }
  return false;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x00027fd4: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00027fdc)
// WARNING: Removing unreachable block (ram,0x00027fe4)
// WARNING: Removing unreachable block (ram,0x0002800c)

void FUN_00027f70(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f43c0,0x3f);
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 0) {
      return;
    }
  }
  return;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00028020(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x29 && sRam00c08540 == 1;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f43d0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_000280b0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f43e0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        lVar2 = func_0x003e14f0(0x429e,0x29);
        if (lVar2 == 4) {
          func_0x003e1430(0x429e,0x29,3);
          func_0x003b6bd0(0x429e,0x29,uRam00c002d8);
        }
        func_0x003b75b8(0x33);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00028190(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        bVar1 = sRam00c0f102 != 0;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f43f0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00028230(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 4) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4400);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(4,0x28);
        func_0x003b6ea8(0x427e,4,0);
        func_0x003b6ea8(0x429e,0x28,0);
        func_0x003b74b0();
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b71d8(0x427e,4);
        func_0x003b71d8(0x429e,0x28);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00028320(short param_1)

{
  ulong uVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 4) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4410);
      uVar3 = 0xe07;
      if (sRam00c0f102 == 0) {
        uVar3 = 0xe08;
      }
      uVar1 = func_0x003b6200(uVar3);
      if (uVar1 != 0) {
        func_0x003b6ea8(0x427e,4,uRam00c00a92);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_000283c8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    lVar2 = func_0x003b69d0(0x427e,0x59);
    if (lVar2 != 0) {
      lVar2 = func_0x003b69d0(0x429e,0x5b);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      uVar1 = func_0x003b5eb0(0x3f4420);
      if (cRam003e6dae == '\0') {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 1) {
          cRam003e6dae = '\x01';
        }
        uVar1 = 0;
      }
      else {
        if (cRam003e6dae == '\x01') {
          lVar2 = func_0x003e13d0(0x427e,0x58);
          if (lVar2 < 0) {
            uRam00c001e0 = 6;
            uRam00c00336 = uRam00c00110;
            uRam00c035fa = 1;
          }
          else {
            func_0x003b6c48(0x427e,0x58,1);
          }
          func_0x003b7130(0x59,0x5b);
          cRam003e6dae = '\0';
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_00028500(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x5b);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4430);
      lVar2 = func_0x003b69d0(0x427e,0x59);
      if ((lVar2 == 0) || (uVar1 = func_0x003b6290(param_2), uVar1 != 0)) {
        func_0x003e1430(0x429e,0x5b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000285b0(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x5b);
    bVar1 = lVar2 != 0 && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4440);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



bool FUN_00028640(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc != 4) {
      uVar1 = (ulong)(sRam00c0853c == 0x68);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4450);
      if (cRam003e6daf == '\x01') {
        lVar2 = func_0x003b67b8(0x5a,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_00028774;
        }
      }
      else {
        if (cRam003e6daf < '\x02') {
          if (cRam003e6daf != '\0') {
            return cRam003e6daf < '\x02';
          }
          lVar2 = func_0x003b5f10(0x192);
          uVar1 = 0;
          cRam003e6daf = (lVar2 != 0) + '\x01';
          goto LAB_00028774;
        }
        if (cRam003e6daf != '\x02') {
          if (cRam003e6daf != '\x03') {
            return true;
          }
          uVar1 = func_0x003b6200(0xfad);
          if (uVar1 != 0) {
            cRam003e6daf = '\0';
            uVar1 = 1;
          }
          goto LAB_00028774;
        }
        lVar2 = func_0x003b6200(0xfac);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_00028774;
        }
      }
      cRam003e6daf = '\x03';
      uVar1 = 0;
    }
  }
LAB_00028774:
  return (bool)(char)uVar1;
}



bool FUN_00028788(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 4) {
      uVar1 = (ulong)(sRam00c0853c == 0x68);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4460);
      if (cRam003e6db0 == '\x01') {
        lVar2 = func_0x003b67b8(0x5a,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_000288bc;
        }
      }
      else {
        if (cRam003e6db0 < '\x02') {
          if (cRam003e6db0 != '\0') {
            return cRam003e6db0 < '\x02';
          }
          lVar2 = func_0x003b5f10(0x191);
          uVar1 = 0;
          cRam003e6db0 = (lVar2 != 0) + '\x01';
          goto LAB_000288bc;
        }
        if (cRam003e6db0 != '\x02') {
          if (cRam003e6db0 != '\x03') {
            return true;
          }
          uVar1 = func_0x003b6200(0xfb7);
          if (uVar1 != 0) {
            cRam003e6db0 = '\0';
            uVar1 = 1;
          }
          goto LAB_000288bc;
        }
        lVar2 = func_0x003b6200(0xfb6);
        if (lVar2 == 0) {
          uVar1 = 0;
          goto LAB_000288bc;
        }
      }
      cRam003e6db0 = '\x03';
      uVar1 = 0;
    }
  }
LAB_000288bc:
  return (bool)(char)uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000288d0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc != 4 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4470);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00028960(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 4 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4480);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_000289f0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  ushort *puVar3;
  int iVar4;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x68) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4498);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        puVar3 = (ushort *)0x3f4490;
        iVar4 = 0;
        do {
          lVar2 = func_0x003e14f0(0x429e,*puVar3);
          if (lVar2 == 4) {
            func_0x003b6bd0(0x429e,*puVar3,*(undefined2 *)((uint)*puVar3 * 2 + 0xc00286));
            func_0x003e1430(0x429e,*puVar3,3);
          }
          uRam003e2c92 = uRam00c01208;
          iVar4 = (iVar4 + 1) * 0x1000000 >> 0x18;
          puVar3 = puVar3 + 1;
        } while (iVar4 < 4);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_00028b08(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
    return true;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f44a8);
  if (cRam003e6db1 == '\x01') {
    lVar1 = func_0x003b6200(0x2334);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6db1 = 2;
  }
  else {
    if (cRam003e6db1 >= '\x02') {
      if (cRam003e6db1 != '\x02') {
        return false;
      }
      uRam00c0853c = 0x78;
      uRam00c08542 = 0x78;
      uRam00c00b7a = 100;
      uRam00c08540 = 1;
      uRam00c08558 = 3;
      uRam00c00a24 = 0;
      func_0x00389580(0x3e2c80,0x78);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 5;
      uRam00c08502 = 5;
      uRam00c08504 = 7;
      uRam00c0851a = 3;
      uRam00c08500 = 2;
      uRam00c08518 = 3;
      func_0x00389580(0x3e2a38,5);
      func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
      func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396a98(0x42,0x1073f00,uRam00c08504);
      uRam003e2a30 = 0;
      uRam003e2f5a = 0;
      cRam003e6db1 = 0;
      return true;
    }
    if (cRam003e6db1 != '\0') {
      return cRam003e6db1 < '\x02';
    }
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6db1 = 1;
  }
  return false;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00028d28(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f44b0);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_00028da0(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f44b8);
    uVar1 = 0x3e0000;
    if (cRam003e6db2 == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6db2 = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6db2 == '\x01') {
        uRam00c0853c = 0x79;
        uRam00c08542 = 0x79;
        uRam00c00a26 = 9;
        uRam00c00b7c = 100;
        uRam00c08558 = 3;
        uRam00c08540 = 1;
        func_0x00389580(0x3e2c80,0x79);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c08502 = 1;
        uRam00c08504 = 3;
        uRam00c08500 = 2;
        uRam00c0851a = 3;
        uRam00c08518 = 3;
        uRam00c084fc = 1;
        func_0x00389580(0x3e2a38,1);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        cRam003e6db2 = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00028f88(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f44c0);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_00029000(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f44c8);
    uVar1 = 0x3e0000;
    if (cRam003e6db3 == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6db3 = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6db3 == '\x01') {
        uRam00c0853c = 0x8d;
        uRam00c08542 = 0x8d;
        uRam00c08540 = 1;
        uRam00c08558 = 3;
        uRam00c00a4e = 0;
        uRam00c00ba4 = 0;
        uRam00c047c2 = 0;
        func_0x00389580(0x3e2c80,0x8d);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c084fc = 0;
        uRam00c08502 = 0;
        uRam00c08504 = 2;
        uRam00c08506 = 6;
        uRam00c08500 = 3;
        uRam00c08518 = 3;
        uRam00c0851a = 3;
        uRam00c0851c = 3;
        func_0x00389580(0x3e2a38,0);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00389580(0x3e2aac,(undefined)uRam00c08506);
        func_0x00396e38(0x3e3008,0,0,2,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        func_0x00396a98(0x42,0x1074800,uRam00c08506);
        cRam003e6db3 = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00029220(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f44d0);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00029298(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f44d8);
    uVar1 = (ulong)(cRam003e6db4 < '\x02');
    if (cRam003e6db4 == '\x01') {
      uRam00c0853c = 0x7e;
      uRam00c08542 = 0x7e;
      uRam00c08558 = 3;
      uRam00c08540 = 1;
      func_0x00389580(0x3e2c80,0x7e);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 4;
      uRam00c08502 = 4;
      uRam00c08518 = 3;
      uRam00c08500 = 1;
      func_0x00389580(0x3e2a38,4);
      func_0x00396e38(0x3e3008,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396420();
      uRam003e2a30 = 1;
      uVar1 = 0;
      cRam003e6db4 = '\x02';
    }
    else {
      if (uVar1 == 0) {
        uVar1 = 2;
        if ((cRam003e6db4 == '\x02') && (uVar1 = func_0x003b6428(), uVar1 != 0)) {
          cRam003e6db4 = '\0';
          uRam003ecf72 = 0;
          uVar1 = 1;
        }
      }
      else {
        if (cRam003e6db4 == '\0') {
          lVar2 = func_0x003b6290(param_2);
          if (lVar2 == 1) {
            cRam003e6db4 = '\x01';
          }
          uVar1 = 0;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_00029480(short param_1,undefined8 param_2,uint param_3)

{
  ushort uVar1;
  int iVar2;
  ulong uVar3;
  long lVar4;
  uint uVar5;
  
  uVar5 = param_3 & 0xffff;
  if (param_1 == 0) {
    uVar1 = *(ushort *)(uVar5 * 2 + 0xc007de);
    uVar3 = 0;
    if (uVar5 == uRam00c0853c) {
      if (sRam00c0f102 == 0) {
        lVar4 = func_0x003b6cd0(0x429e,uVar5);
        if (lVar4 == 0) {
          uVar3 = 0;
        }
        else {
          iVar2 = func_0x003b7568(uVar5);
          uVar3 = (ulong)((int)(uint)(uVar1 >> 0xf) < iVar2);
        }
      }
      else {
        uVar3 = 0;
      }
    }
  }
  else {
    uVar3 = 1;
    if ((param_1 == 1) && (uVar3 = func_0x003b6290(param_2), uVar3 != 0)) {
      func_0x003b6bd0(0x429e,uVar5,uVar5);
      func_0x003e1430(0x429e,uVar5,1);
      uVar3 = 1;
    }
  }
  return uVar3;
}



bool FUN_00029560(short param_1,int param_2,short param_3,ulong param_4,undefined2 param_5)

{
  ulong uVar1;
  short sVar2;
  
  if (param_1 == 0) {
    uVar1 = param_4 & 0xffff ^ (ulong)uRam00c0853c;
  }
  else {
    if (param_1 != 1) {
      return true;
    }
    func_0x003b5eb0(0x3f44e0);
    if (param_3 == sRam00c084fc) {
      sVar2 = *(short *)(param_2 + 0x1c);
    }
    else {
      sVar2 = *(short *)(param_2 + 0x1c) + 1;
    }
    uVar1 = func_0x003b67b8(param_5,sVar2,1);
  }
  return uVar1 == 0;
}



ulong FUN_00029608(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x50) {
      uVar1 = (ulong)(sRam00c084fc == 5);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f44f0);
      uVar1 = SEXT18(cRam003e6dba);
      if (uVar1 == 0) {
        lVar2 = func_0x003b67b8(0x6f,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 == 0) {
          cRam003e6dba = '\x01';
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0x84), uVar1 != 0)) {
          cRam003e6dba = '\0';
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000296f0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x50;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4500);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00029768(short param_1)

{
  ulong uVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x50) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4510);
      if (bRam003e6dbb == 1) {
        uVar3 = 0x97;
      }
      else {
        if ((ulong)(bRam003e6dbb < 2) != 0) {
          if (bRam003e6dbb != 0) {
            return (ulong)(bRam003e6dbb < 2);
          }
          lVar2 = func_0x003b69d0(0x427e,5);
          bRam003e6dbb = '\x02' - (lVar2 == 0);
          return 0;
        }
        if (bRam003e6dbb != 2) {
          return 2;
        }
        uVar3 = 0x98;
      }
      uVar1 = func_0x003b6200(uVar3);
      if (uVar1 != 0) {
        bRam003e6dbb = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_00029868(short param_1)

{
  long lVar1;
  bool bVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    bVar2 = sRam00c0853c == 0x50 && sRam00c0f102 != 0;
  }
  else {
    bVar2 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4510);
      lVar1 = func_0x003b69d0(0x427e,5);
      uVar3 = 0xa1;
      if (lVar1 == 0) {
        uVar3 = 0xa2;
      }
      lVar1 = func_0x003b6200(uVar3);
      bVar2 = true;
      if (lVar1 == 0) {
        bVar2 = false;
      }
    }
  }
  return bVar2;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00029900(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00029bc8(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00029d50(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



bool FUN_00029f78(short param_1)

{
  ushort uVar1;
  byte bVar2;
  int iVar3;
  ulong uVar4;
  long lVar5;
  
  uVar1 = uRam00c007e0;
  if (param_1 == 0) {
    lVar5 = func_0x003b69d0(0x429e,1);
    if ((lVar5 == 0) && (lVar5 = func_0x003b69d0(0x429e,0xe), lVar5 == 0)) {
      lVar5 = func_0x003b69d0(0x429e,0xc);
      uVar4 = 0;
      bVar2 = bRam003e6dbf;
      if (lVar5 == 0) goto LAB_0002a190;
    }
    if (sRam00c0f102 == 0) {
      iVar3 = func_0x003b7568(1);
      uVar4 = (ulong)(iVar3 <= (int)(uint)(uVar1 >> 0xf));
      bVar2 = bRam003e6dbf;
    }
    else {
      uVar4 = 0;
      bVar2 = bRam003e6dbf;
    }
  }
  else {
    uVar4 = 1;
    bVar2 = bRam003e6dbf;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4540);
      uVar4 = (ulong)(bRam003e6dbf < 2);
      if (bRam003e6dbf == 1) {
        uVar4 = func_0x003b6cd0(0x429e,0xe);
        if (uVar4 == 0) {
          bRam003e6dbf = 2;
          bVar2 = bRam003e6dbf;
          goto LAB_0002a190;
        }
        lVar5 = func_0x003b6200(899);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6dbf;
          goto LAB_0002a190;
        }
        func_0x003b5f40(0x84);
        func_0x003e1430(0x429e,0xe,1);
        func_0x003b6bd0(0x429e,0xe,1);
        bRam003e6dbf = 2;
      }
      else {
        if (uVar4 == 0) {
          if (bRam003e6dbf != 2) {
            return true;
          }
          lVar5 = func_0x003b6cd0(0x429e,0xc);
          if (lVar5 != 0) {
            uVar4 = func_0x003b6200(0x382);
            bVar2 = bRam003e6dbf;
            if (uVar4 == 0) goto LAB_0002a190;
            func_0x003b5f40(0x84);
            func_0x003e1430(0x429e,0xc,1);
            func_0x003b6bd0(0x429e,0xc,1);
          }
          bRam003e6dbf = 0;
          uVar4 = 1;
          bVar2 = bRam003e6dbf;
          goto LAB_0002a190;
        }
        bVar2 = bRam003e6dbf;
        if (bRam003e6dbf != 0) goto LAB_0002a190;
        uVar4 = func_0x003b6cd0(0x429e,1);
        bVar2 = 1;
        if (uVar4 == 0) goto LAB_0002a190;
        lVar5 = func_0x003b6200(0x37b);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6dbf;
          goto LAB_0002a190;
        }
        func_0x003b5f40(0x59);
        func_0x003e1430(0x429e,1,1);
        func_0x003b6bd0(0x429e,1,1);
        func_0x003b7620(0x429e,1,2,10);
        bRam003e6dbf = 1;
        uRam003e2c86 = 2;
      }
      uVar4 = 0;
      bVar2 = bRam003e6dbf;
    }
  }
LAB_0002a190:
  bRam003e6dbf = bVar2;
  return (bool)(char)uVar4;
}



bool FUN_0002a1a0(void)

{
  bool bVar1;
  long lVar2;
  
  lVar2 = func_0x001251e8();
  if (lVar2 == 0) {
    lVar2 = func_0x00125258();
    if (lVar2 == 0) {
      bVar1 = false;
      if ((sRam00c084fc == 5) && (bVar1 = false, sRam00c0853c == 0x2a)) {
        bVar1 = false;
        if (sRam00c08500 == 1) {
          bVar1 = sRam00c08540 == 1;
        }
      }
    }
    else {
      bVar1 = false;
    }
  }
  else {
    bVar1 = false;
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

long FUN_0002a230(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  lVar1 = 1;
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f4550);
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 0) {
      func_0x003b7130(5,0x2a);
      func_0x003b7440(0x429e);
      lVar1 = 1;
    }
  }
  return lVar1;
}



ulong FUN_0002a2b0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003aaaa0();
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4560);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003de9c8(5,0x2a);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0002a340(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003aaaa0();
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4570);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6ea8(0x429e,0x2a,uRam00c00ade);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0002a3d8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0853c == 0x2b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4580);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(0x2a,0x38);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002a470(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 == 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4590);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002a500(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 != 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f45a0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002a5b8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x2b && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f45b0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002a640(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 != 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f45c0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002a6d0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 == 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f45d0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_0002a788(short param_1,undefined8 param_2)

{
  long lVar1;
  bool bVar2;
  
  if (param_1 == 0) {
    bVar2 = sRam00c084fc == 5 && sRam00c0853c == 0x8a;
  }
  else {
    bVar2 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f45e0);
      lVar1 = func_0x003b6290(param_2);
      if (lVar1 == 1) {
        func_0x003b7130(5,0x8a);
        func_0x003b74b0();
        func_0x003b7548();
        bVar2 = true;
      }
      else {
        bVar2 = false;
      }
    }
  }
  return bVar2;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002a838(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 5 && sRam00c0853c == 0x8a;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f45f0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002a8c8(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    if ((sRam00c084fc == 5) && (lVar2 = func_0x003b69d0(0x429e,0x3a), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x429e,0x39);
      bVar1 = lVar2 != 0;
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4600);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



bool FUN_0002a968(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 5 && sRam00c0853c == 1;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4610);
      if (cRam003e6dc0 == '\x01') {
        lVar2 = func_0x003b67b8(0x1d,0x5ac,1);
        if (lVar2 != 0) {
          return false;
        }
        cRam003e6dc0 = '\x02';
      }
      else {
        if (cRam003e6dc0 >= '\x02') {
          if (cRam003e6dc0 != '\x02') {
            return false;
          }
          lVar2 = func_0x003b67b8(0x1e,0x5ad,1);
          if (lVar2 != 0) {
            return false;
          }
          func_0x003b7130(5,1);
          func_0x003b71d8(0x427e,5);
          func_0x003b71d8(0x429e,1);
          func_0x003b74b0();
          func_0x003b7548();
          cRam003e6dc0 = 0;
          return true;
        }
        if (cRam003e6dc0 != '\0') {
          return cRam003e6dc0 < '\x02';
        }
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 1) {
          return false;
        }
        cRam003e6dc0 = '\x01';
      }
      bVar1 = false;
    }
  }
  return bVar1;
}



bool FUN_0002aad0(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    uVar2 = 0;
    if (sRam00c084fc == 5) {
      if (sRam00c0853c == 1) {
        uVar2 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar2 = 0;
      }
    }
    goto LAB_0002abec;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f4630);
  if (cRam003e6dc1 == '\x01') {
    lVar1 = func_0x003b6f20(0x427e,0x3f4620,4);
    uVar2 = 0;
    if (lVar1 != 1) goto LAB_0002abec;
    cRam003e6dc1 = '\x02';
  }
  else {
    if (cRam003e6dc1 >= '\x02') {
      if (cRam003e6dc1 != '\x02') {
        return true;
      }
      uVar2 = func_0x003b6200(0x5b6);
      if (uVar2 == 1) {
        cRam003e6dc1 = '\0';
      }
      else {
        uVar2 = 0;
      }
      goto LAB_0002abec;
    }
    if (cRam003e6dc1 != '\0') {
      return cRam003e6dc1 < '\x02';
    }
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 1) {
      uVar2 = 0;
      goto LAB_0002abec;
    }
    cRam003e6dc1 = '\x01';
  }
  uVar2 = 0;
LAB_0002abec:
  return (bool)(char)uVar2;
}



bool FUN_0002ac08(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 5 && sRam00c0853c == 0x55;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4640);
      if (cRam003e6dc2 == '\x01') {
        lVar2 = func_0x003b6200(0x607);
        if (lVar2 == 0) {
          return false;
        }
        cRam003e6dc2 = '\x03';
      }
      else {
        if (cRam003e6dc2 < '\x02') {
          if (cRam003e6dc2 != '\0') {
            return cRam003e6dc2 < '\x02';
          }
          lVar2 = func_0x003b6290(param_2);
          if (lVar2 == 0) {
            return false;
          }
          lVar2 = func_0x003b5f10(0x196);
          cRam003e6dc2 = (lVar2 == 0) + '\x01';
        }
        else {
          if (cRam003e6dc2 != '\x02') {
            if (cRam003e6dc2 != '\x03') {
              return true;
            }
            lVar2 = func_0x003e13d0(0x427e,0x1d);
            if (lVar2 < 0) {
              uRam00c03584 = 1;
              uRam00c0016a = 6;
            }
            else {
              func_0x003b6c48(0x427e,0x1d,1);
            }
            cRam003e6dc2 = 0;
            return true;
          }
          lVar2 = func_0x003b67b8(0x5a,0x606,1);
          if (lVar2 != 0) {
            return false;
          }
          func_0x003b5f40(0x191);
          func_0x003b5f40(0x192);
          cRam003e6dc2 = '\x03';
        }
      }
      bVar1 = false;
    }
  }
  return bVar1;
}



bool FUN_0002ad98(short param_1)

{
  bool bVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x55 && sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4650);
      if (cRam003e6dc6 == '\x01') {
        lVar2 = func_0x003b6200(0x611);
        if (lVar2 == 0) {
          return false;
        }
        cRam003e6dc6 = '\x02';
      }
      else {
        if (cRam003e6dc6 >= '\x02') {
          if (cRam003e6dc6 != '\x02') {
            return false;
          }
          func_0x003e1430(0x429e,0x55,1);
          cRam003e6dc6 = 0;
          return true;
        }
        if (cRam003e6dc6 != '\0') {
          return cRam003e6dc6 < '\x02';
        }
        lVar2 = func_0x003b5f10(0x196);
        uVar3 = 0x60f;
        if (lVar2 == 0) {
          uVar3 = 0x610;
        }
        lVar2 = func_0x003b6200(uVar3);
        if (lVar2 == 0) {
          return false;
        }
        lVar2 = func_0x003b69d0(0x427e,0x6a);
        cRam003e6dc6 = (lVar2 == 0) + '\x01';
      }
      bVar1 = false;
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002aec8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x55 && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4660);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002af50(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x55;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4670);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



bool FUN_0002afc8(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x55 && sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4680);
      if (cRam003e6dc7 == '\x01') {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 0) {
          return false;
        }
        cRam003e6dc7 = '\x02';
      }
      else {
        if (cRam003e6dc7 >= '\x02') {
          if (cRam003e6dc7 != '\x02') {
            return false;
          }
          func_0x003e1430(0x429e,0x55,1);
          cRam003e6dc7 = 0;
          return true;
        }
        if (cRam003e6dc7 != '\0') {
          return cRam003e6dc7 < '\x02';
        }
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 0) {
          return false;
        }
        lVar2 = func_0x003b5f10(0x196);
        cRam003e6dc7 = (lVar2 == 0) + '\x01';
      }
      bVar1 = false;
    }
  }
  return bVar1;
}



bool FUN_0002b0e8(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4690);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_0002b1d8(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f46a0);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_0002b2d8(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f46b0);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_0002b3c8(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f46c0);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



ulong FUN_0002b4c8(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    lVar1 = func_0x003b69d0(0x427e,1);
    uVar2 = 0;
    if (lVar1 != 0) {
      lVar1 = func_0x003b69d0(0x427e,0);
      uVar2 = 0;
      if (lVar1 != 0) {
        if (sRam00c08500 == 2) {
          lVar1 = func_0x003b69d0(0x429e,0x54);
          uVar2 = 0;
          if (lVar1 != 0) {
            lVar1 = func_0x003b69d0(0x429e,0x7f);
            uVar2 = (ulong)(lVar1 != 0);
          }
        }
        else {
          uVar2 = 0;
        }
      }
    }
  }
  else {
    uVar2 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f46d0);
      uVar2 = func_0x003b6290(param_2);
      if (uVar2 != 0) {
        func_0x003b7130(1,0x54);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



bool FUN_0002b598(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,1);
      uVar1 = 0;
      if (lVar2 != 0) {
        if (sRam00c08500 == 2) {
          lVar2 = func_0x003b69d0(0x429e,0x54);
          if (lVar2 == 0) {
            uVar1 = 0;
          }
          else {
            lVar2 = func_0x003b69d0(0x429e,0x7f);
            if (lVar2 == 0) {
              uVar1 = 0;
            }
            else {
              uVar1 = (ulong)(sRam00c0f102 == 0);
            }
          }
        }
        else {
          uVar1 = 0;
        }
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f46e0);
      uVar1 = (ulong)(bRam003e6dc8 < 2);
      if (bRam003e6dc8 == 1) {
        lVar2 = func_0x003b67b8(0x34,0x796,1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_0002b6f4;
        }
        bRam003e6dc8 = 2;
      }
      else {
        if (uVar1 == 0) {
          if (bRam003e6dc8 != 2) {
            return true;
          }
          uVar1 = func_0x003b6200(0x797);
          if (uVar1 != 0) {
            bRam003e6dc8 = 0;
            uVar1 = 1;
          }
          goto LAB_0002b6f4;
        }
        if (bRam003e6dc8 != 0) goto LAB_0002b6f4;
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_0002b6f4;
        }
        bRam003e6dc8 = 1;
      }
      uVar1 = 0;
    }
  }
LAB_0002b6f4:
  return (bool)(char)uVar1;
}



ulong FUN_0002b708(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      if (sRam00c08500 == 1) {
        uVar1 = (ulong)(sRam00c0853c == 0x3b);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f46f0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        uRam003e2fd2 = 0x11;
        func_0x003b74b0();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0002b7b0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      uVar1 = (ulong)(sRam00c0853c == 0x3b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4700);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x427e,0x7c,1);
        uRam00c0f102 = 1;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0002b858(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 5) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c01d48 == 0x18);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4710);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6aa0(0x429e,0x3b,8);
        func_0x003b6b20(0x429e,0x3b,0xa0);
        sRam00c01706 = sRam00c01706 + 8;
        sRam00c03dc4 = sRam00c03dc4 + 0xa0;
        sRam00c03f1a = sRam00c03f1a + 0xa0;
        sRam00c04070 = sRam00c04070 + 0xa0;
        sRam00c041c6 = sRam00c041c6 + 0xa0;
        sRam00c0431c = sRam00c0431c + 0xa0;
        func_0x003b7130(5,0x3b);
        func_0x003b71d8(0x427e,5);
        func_0x003b71d8(0x429e,0x3b);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0002b9b0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 5) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4720);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0002ba58(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x3b);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4730);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002baf8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x45;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4740);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002bb70(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x45) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4750);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        uRam00c08648 = 0;
        func_0x003e1430(0x429e,0x45,1);
        func_0x003e1430(0x429e,0x46,1);
        func_0x003e1430(0x429e,0x47,1);
        func_0x003b5f78(0x141);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0002bc38(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 5) {
      uVar1 = (ulong)(sRam00c0853c == 0x3e);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4760);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(5,0x3e);
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b71d8(0x427e,5);
        func_0x003b71d8(0x429e,0x3e);
        func_0x003b7440(0x429e);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002bd08(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c084fc == 5) {
      if (sRam00c0853c == 0x3e) {
        bVar1 = cRam003e2f59 == '\0';
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4770);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002bda8(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    lVar2 = func_0x003b69d0(0x427e,5);
    if (lVar2 == 0) {
      lVar2 = func_0x003b69d0(0x427e,0x3e);
      if (lVar2 == 0) {
        lVar2 = func_0x003b69d0(0x429e,0x3f);
        bVar1 = lVar2 != 0;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4780);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002be50(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,5);
    if (lVar2 == 0) {
      lVar2 = func_0x003b69d0(0x427e,0x3e);
      if (lVar2 == 0) {
        lVar2 = func_0x003b69d0(0x429e,0x3f);
        if (lVar2 == 0) {
          uVar1 = 0;
        }
        else {
          uVar1 = (ulong)(sRam00c0f102 == 0);
        }
      }
      else {
        uVar1 = 0;
      }
    }
    else {
      uVar1 = 0;
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4790);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3f,3);
        func_0x003b6bd0(0x429e,0x3f,0x3f);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002bf20(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      lVar2 = func_0x003b69d0(0x427e,0x3e);
      if (lVar2 == 0) {
        lVar2 = func_0x003b69d0(0x429e,0x3f);
        if (lVar2 == 0) {
          bVar1 = false;
        }
        else {
          bVar1 = sRam00c0f102 != 0;
        }
      }
      else {
        bVar1 = false;
      }
    }
    else {
      bVar1 = false;
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f47a0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002bfd8(short param_1)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = (ulong)(sRam00c0853c == 0x3f);
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f47b0);
      uVar1 = (ulong)(bRam003e6dc9 < 5);
      if (uVar1 != 0) {
                    // WARNING: Could not emulate address calculation at 0x0002c04c
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((uint)bRam003e6dc9 * 4 + 0x3f47c0))();
        return uVar1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0002c128(short param_1)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    lVar2 = func_0x003b69d0(0x429e,0x3f);
    if (lVar2 != 0) {
      lVar2 = func_0x003b69d0(0x427e,5);
      if ((lVar2 == 0) && (lVar2 = func_0x003b69d0(0x427e,0x3e), lVar2 == 0)) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f47f0);
      uVar1 = func_0x003b6f20(0x427e,0x3f47d8,6);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3f,1);
        uRam003e6dca = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002c1f0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x29 && sRam00c08540 == 1;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4800);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002c280(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4810);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        lVar2 = func_0x003e14f0(0x429e,0x29);
        if (lVar2 == 4) {
          func_0x003e1430(0x429e,0x29,3);
          func_0x003b6bd0(0x429e,0x29,uRam00c002d8);
        }
        func_0x003b75b8(0x33);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002c360(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        bVar1 = sRam00c0f102 != 0;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4820);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002c400(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 5) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4830);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(5,0x28);
        uRam003e2fd2 = 0x11;
        func_0x003b6ea8(0x427e,5,0);
        func_0x003b6ea8(0x429e,0x28,0);
        func_0x003b71d8(0x427e,5);
        func_0x003b71d8(0x429e,0x28);
        func_0x003b74b0();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



char FUN_0002c4e8(short param_1)

{
  char cVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    cVar1 = '\0';
    if (sRam00c084fc == 5) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      cVar1 = lVar2 != 0;
    }
  }
  else {
    cVar1 = '\x01';
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4840);
      if (cRam003e6dcb == '\0') {
        uVar3 = 0xe07;
        if (sRam00c0f102 == 0) {
          uVar3 = 0xe08;
        }
        lVar2 = func_0x003b6200(uVar3);
        if (lVar2 != 0) {
          cRam003e6dcb = '\x01';
        }
        cVar1 = '\0';
      }
      else {
        cVar1 = cRam003e6dcb;
        if (cRam003e6dcb == '\x01') {
          func_0x003b6ea8(0x427e,5,uRam00c00a94);
          cRam003e6dcb = '\0';
          cVar1 = '\x01';
        }
      }
    }
  }
  return cVar1;
}



bool FUN_0002c5d0(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc != 5) {
      uVar1 = (ulong)(sRam00c0853c == 0x68);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4850);
      if (cRam003e6dd4 == '\x01') {
        lVar2 = func_0x003b67b8(0x5a,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_0002c704;
        }
      }
      else {
        if (cRam003e6dd4 < '\x02') {
          if (cRam003e6dd4 != '\0') {
            return cRam003e6dd4 < '\x02';
          }
          lVar2 = func_0x003b5f10(0x192);
          uVar1 = 0;
          cRam003e6dd4 = (lVar2 != 0) + '\x01';
          goto LAB_0002c704;
        }
        if (cRam003e6dd4 != '\x02') {
          if (cRam003e6dd4 != '\x03') {
            return true;
          }
          uVar1 = func_0x003b6200(0xfad);
          if (uVar1 != 0) {
            cRam003e6dd4 = '\0';
            uVar1 = 1;
          }
          goto LAB_0002c704;
        }
        lVar2 = func_0x003b6200(0xfac);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_0002c704;
        }
      }
      cRam003e6dd4 = '\x03';
      uVar1 = 0;
    }
  }
LAB_0002c704:
  return (bool)(char)uVar1;
}



bool FUN_0002c718(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 5) {
      uVar1 = (ulong)(sRam00c0853c == 0x68);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4860);
      if (cRam003e6dd5 == '\x01') {
        lVar2 = func_0x003b67b8(0x5a,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_0002c84c;
        }
      }
      else {
        if (cRam003e6dd5 < '\x02') {
          if (cRam003e6dd5 != '\0') {
            return cRam003e6dd5 < '\x02';
          }
          lVar2 = func_0x003b5f10(0x191);
          uVar1 = 0;
          cRam003e6dd5 = (lVar2 != 0) + '\x01';
          goto LAB_0002c84c;
        }
        if (cRam003e6dd5 != '\x02') {
          if (cRam003e6dd5 != '\x03') {
            return true;
          }
          uVar1 = func_0x003b6200(0xfb7);
          if (uVar1 != 0) {
            cRam003e6dd5 = '\0';
            uVar1 = 1;
          }
          goto LAB_0002c84c;
        }
        lVar2 = func_0x003b6200(0xfb6);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_0002c84c;
        }
      }
      cRam003e6dd5 = '\x03';
      uVar1 = 0;
    }
  }
LAB_0002c84c:
  return (bool)(char)uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002c860(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc != 5 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4870);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002c8f0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 5 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4880);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002c980(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc != 5 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4890);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002ca10(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  ushort *puVar3;
  int iVar4;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x68) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f48a8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        puVar3 = (ushort *)0x3f48a0;
        iVar4 = 0;
        do {
          lVar2 = func_0x003e14f0(0x429e,*puVar3);
          if (lVar2 == 4) {
            func_0x003b6bd0(0x429e,*puVar3,*(undefined2 *)((uint)*puVar3 * 2 + 0xc00286));
            func_0x003e1430(0x429e,*puVar3,3);
          }
          uRam003e2c92 = uRam00c01208;
          iVar4 = (iVar4 + 1) * 0x1000000 >> 0x18;
          puVar3 = puVar3 + 1;
        } while (iVar4 < 4);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_0002cb28(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
    return true;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f48b8);
  if (cRam003e6dd6 == '\x01') {
    lVar1 = func_0x003b6200(0x2334);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6dd6 = 2;
  }
  else {
    if (cRam003e6dd6 >= '\x02') {
      if (cRam003e6dd6 != '\x02') {
        return false;
      }
      uRam00c0853c = 0x78;
      uRam00c08542 = 0x78;
      uRam00c00b7a = 100;
      uRam00c08558 = 3;
      uRam00c08540 = 1;
      uRam00c00a24 = 0;
      func_0x00389580(0x3e2c80,0x78);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 1;
      uRam00c08502 = 1;
      uRam00c08504 = 7;
      uRam00c0851a = 3;
      uRam00c08500 = 2;
      uRam00c08518 = 3;
      func_0x00389580(0x3e2a38,1);
      func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
      func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396a98(0x42,0x1073f00,uRam00c08504);
      uRam003e2a30 = 0;
      uRam003e2f5a = 0;
      cRam003e6dd6 = 0;
      return true;
    }
    if (cRam003e6dd6 != '\0') {
      return cRam003e6dd6 < '\x02';
    }
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6dd6 = 1;
  }
  return false;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002cd50(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f48c8);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_0002cdc8(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f48d8);
    uVar1 = 0x3e0000;
    if (cRam003e6dd7 == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6dd7 = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6dd7 == '\x01') {
        uRam00c0853c = 0x79;
        uRam00c08542 = 0x79;
        uRam00c00a26 = 9;
        uRam00c08540 = 1;
        uRam00c00b7c = 100;
        uRam00c08558 = 3;
        func_0x00389580(0x3e2c80,0x79);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c084fc = 3;
        uRam00c08502 = 3;
        uRam00c08504 = 4;
        uRam00c08500 = 2;
        uRam00c0851a = 3;
        uRam00c08518 = 3;
        func_0x00389580(0x3e2a38,3);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        cRam003e6dd7 = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002cfb0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f48e8);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_0002d028(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f48f8);
    uVar1 = 0x3e0000;
    if (cRam003e6dd8 == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6dd8 = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6dd8 == '\x01') {
        uRam00c0853c = 0x8d;
        uRam00c08542 = 0x8d;
        uRam00c08540 = 1;
        uRam00c08558 = 3;
        uRam00c00a4e = 0;
        uRam00c00ba4 = 0;
        uRam00c047c2 = 0;
        func_0x00389580(0x3e2c80,0x8d);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c084fc = 0;
        uRam00c08502 = 0;
        uRam00c08504 = 2;
        uRam00c08506 = 6;
        uRam00c08500 = 3;
        uRam00c08518 = 3;
        uRam00c0851a = 3;
        uRam00c0851c = 3;
        func_0x00389580(0x3e2a38,0);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00389580(0x3e2aac,(undefined)uRam00c08506);
        func_0x00396e38(0x3e3008,0,0,2,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        func_0x00396a98(0x42,0x1074800,uRam00c08506);
        cRam003e6dd8 = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002d248(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4908);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002d2c0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f4918);
    uVar1 = (ulong)(cRam003e6dd9 < '\x02');
    if (cRam003e6dd9 == '\x01') {
      uRam00c0853c = 0x7e;
      uRam00c08542 = 0x7e;
      uRam00c08558 = 3;
      uRam00c08540 = 1;
      func_0x00389580(0x3e2c80,0x7e);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 5;
      uRam00c08502 = 5;
      uRam00c08518 = 3;
      uRam00c08500 = 1;
      func_0x00389580(0x3e2a38,5);
      func_0x00396e38(0x3e3008,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396420();
      uRam003e2a30 = 1;
      uVar1 = 0;
      cRam003e6dd9 = '\x02';
    }
    else {
      if (uVar1 == 0) {
        uVar1 = 2;
        if ((cRam003e6dd9 == '\x02') && (uVar1 = func_0x003b6428(), uVar1 != 0)) {
          cRam003e6dd9 = '\0';
          uRam003ecf72 = 0;
          uVar1 = 1;
        }
      }
      else {
        if (cRam003e6dd9 == '\0') {
          lVar2 = func_0x003b6290(param_2);
          if (lVar2 == 1) {
            cRam003e6dd9 = '\x01';
          }
          uVar1 = 0;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_0002d4a0(short param_1,undefined8 param_2,uint param_3)

{
  ushort uVar1;
  int iVar2;
  ulong uVar3;
  long lVar4;
  uint uVar5;
  
  uVar5 = param_3 & 0xffff;
  if (param_1 == 0) {
    uVar1 = *(ushort *)(uVar5 * 2 + 0xc007de);
    uVar3 = 0;
    if (uVar5 == uRam00c0853c) {
      if (sRam00c0f102 == 0) {
        lVar4 = func_0x003b6cd0(0x429e,uVar5);
        if (lVar4 == 0) {
          uVar3 = 0;
        }
        else {
          iVar2 = func_0x003b7568(uVar5);
          uVar3 = (ulong)((int)(uint)(uVar1 >> 0xf) < iVar2);
        }
      }
      else {
        uVar3 = 0;
      }
    }
  }
  else {
    uVar3 = 1;
    if ((param_1 == 1) && (uVar3 = func_0x003b6290(param_2), uVar3 != 0)) {
      func_0x003e1430(0x429e,uVar5,1);
      func_0x003b6bd0(0x429e,uVar5,uVar5);
      uVar3 = 1;
    }
  }
  return uVar3;
}



bool FUN_0002d580(short param_1,int param_2,short param_3,ulong param_4,undefined2 param_5)

{
  ulong uVar1;
  short sVar2;
  
  if (param_1 == 0) {
    uVar1 = param_4 & 0xffff ^ (ulong)uRam00c0853c;
  }
  else {
    if (param_1 != 1) {
      return true;
    }
    func_0x003b5eb0(0x3f4930);
    if (param_3 == sRam00c084fc) {
      sVar2 = *(short *)(param_2 + 0x1c);
    }
    else {
      sVar2 = *(short *)(param_2 + 0x1c) + 1;
    }
    uVar1 = func_0x003b67b8(param_5,sVar2,1);
  }
  return uVar1 == 0;
}



ulong FUN_0002d628(short param_1)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 6) {
      lVar2 = func_0x003b69d0(0x429e,0x43);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4940);
      uVar1 = (ulong)bRam003e6de2;
      if (uVar1 == 0) {
        lVar2 = func_0x003b67b8(0x6c,0x83,1);
        if (lVar2 == 0) {
          bRam003e6de2 = 1;
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0x84), uVar1 != 0)) {
          bRam003e6de2 = 0;
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0002d708(short param_1)

{
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  if (param_1 != 1) {
    return;
  }
  func_0x003b5eb0(0x3f4950,0x43);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



bool FUN_0002d770(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x43);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      bVar1 = sRam00c0f102 == 0;
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4960);
      if (bRam003e6de3 == 1) {
        lVar2 = func_0x003b6200(0x98);
        if (lVar2 == 0) {
          return false;
        }
        bRam003e6de3 = 2;
      }
      else {
        if (bRam003e6de3 >= 2) {
          if (bRam003e6de3 != 2) {
            return false;
          }
          func_0x003de9c8(6,0x43);
          bRam003e6de3 = 0;
          return true;
        }
        if (bRam003e6de3 != 0) {
          return bRam003e6de3 < 2;
        }
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 0) {
          return false;
        }
        lVar2 = func_0x003b69d0(0x427e,6);
        bRam003e6de3 = (lVar2 == 0) + 1;
      }
      bVar1 = false;
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002d898(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x43);
    bVar1 = lVar2 != 0 && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4970);
      lVar2 = func_0x003b69d0(0x427e,6);
      if (lVar2 != 0) {
                    // WARNING: Bad instruction - Truncating control flow here
        halt_baddata();
      }
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0002d950(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0002db58(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0002dcc8(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



bool FUN_0002def0(short param_1)

{
  ushort uVar1;
  byte bVar2;
  int iVar3;
  ulong uVar4;
  long lVar5;
  
  uVar1 = uRam00c007e0;
  if (param_1 == 0) {
    lVar5 = func_0x003b69d0(0x429e,1);
    if ((lVar5 == 0) && (lVar5 = func_0x003b69d0(0x429e,0xe), lVar5 == 0)) {
      lVar5 = func_0x003b69d0(0x429e,0xc);
      uVar4 = 0;
      bVar2 = bRam003e6de6;
      if (lVar5 == 0) goto LAB_0002e108;
    }
    if (sRam00c0f102 == 0) {
      iVar3 = func_0x003b7568(1);
      uVar4 = (ulong)(iVar3 <= (int)(uint)(uVar1 >> 0xf));
      bVar2 = bRam003e6de6;
    }
    else {
      uVar4 = 0;
      bVar2 = bRam003e6de6;
    }
  }
  else {
    uVar4 = 1;
    bVar2 = bRam003e6de6;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f49b0);
      uVar4 = (ulong)(bRam003e6de6 < 2);
      if (bRam003e6de6 == 1) {
        uVar4 = func_0x003b6cd0(0x429e,0xe);
        if (uVar4 == 0) {
          bRam003e6de6 = 2;
          bVar2 = bRam003e6de6;
          goto LAB_0002e108;
        }
        lVar5 = func_0x003b6200(899);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6de6;
          goto LAB_0002e108;
        }
        func_0x003b5f40(0x84);
        func_0x003e1430(0x429e,0xe,1);
        func_0x003b6bd0(0x429e,0xe,1);
        bRam003e6de6 = 2;
      }
      else {
        if (uVar4 == 0) {
          if (bRam003e6de6 != 2) {
            return true;
          }
          lVar5 = func_0x003b6cd0(0x429e,0xc);
          if (lVar5 != 0) {
            uVar4 = func_0x003b6200(0x382);
            bVar2 = bRam003e6de6;
            if (uVar4 == 0) goto LAB_0002e108;
            func_0x003b5f40(0x84);
            func_0x003e1430(0x429e,0xc,1);
            func_0x003b6bd0(0x429e,0xc,1);
          }
          bRam003e6de6 = 0;
          uVar4 = 1;
          bVar2 = bRam003e6de6;
          goto LAB_0002e108;
        }
        bVar2 = bRam003e6de6;
        if (bRam003e6de6 != 0) goto LAB_0002e108;
        uVar4 = func_0x003b6cd0(0x429e,1);
        bVar2 = 1;
        if (uVar4 == 0) goto LAB_0002e108;
        lVar5 = func_0x003b6200(0x37b);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6de6;
          goto LAB_0002e108;
        }
        func_0x003b5f40(0x59);
        func_0x003e1430(0x429e,1,1);
        func_0x003b6bd0(0x429e,1,1);
        func_0x003b7620(0x429e,1,2,10);
        bRam003e6de6 = 1;
        uRam003e2c86 = 2;
      }
      uVar4 = 0;
      bVar2 = bRam003e6de6;
    }
  }
LAB_0002e108:
  bRam003e6de6 = bVar2;
  return (bool)(char)uVar4;
}



bool FUN_0002e118(void)

{
  bool bVar1;
  long lVar2;
  
  lVar2 = func_0x001251e8();
  if (lVar2 == 0) {
    lVar2 = func_0x00125258();
    if (lVar2 == 0) {
      bVar1 = false;
      if ((sRam00c084fc == 6) && (bVar1 = false, sRam00c0853c == 0x2a)) {
        bVar1 = false;
        if (sRam00c08500 == 1) {
          bVar1 = sRam00c08540 == 1;
        }
      }
    }
    else {
      bVar1 = false;
    }
  }
  else {
    bVar1 = false;
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

long FUN_0002e1a8(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  lVar1 = 1;
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f49c0);
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 0) {
      func_0x003b7130(6,0x2a);
      lVar1 = 1;
    }
  }
  return lVar1;
}



ulong FUN_0002e220(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003aea18();
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f49d0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003de9c8(6,0x2a);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002e2b0(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003aea18();
    bVar1 = lVar2 != 0 && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f49e0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002e338(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0853c == 0x2b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f49f0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(0x2a,0x38);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002e3d0(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 == 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4a00);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002e460(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 != 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4a10);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002e518(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x2b && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4a20);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002e5a0(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 != 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4a30);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002e630(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 == 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4a40);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_0002e6e8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 6 && sRam00c0853c == 0x8b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4a50);
      func_0x003b7130(6,0x8b);
      bRam003e2fd2 = bRam003e2fd2 | 0x11;
      func_0x003b74b0();
      bVar1 = true;
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

undefined4 FUN_0002e780(short param_1)

{
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f4a60);
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  return 1;
}



ulong FUN_0002e7e8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if ((sRam00c084fc == 6) && (lVar2 = func_0x003b69d0(0x429e,0x3a), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x429e,0x39);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4a70);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6aa0(0x429e,0x39,0xfffffffffffffffd);
        func_0x003b6aa0(0x429e,0x3a,0xfffffffffffffffd);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002e8a0(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    if ((sRam00c084fc == 6) && (lVar2 = func_0x003b69d0(0x429e,0x3a), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x429e,0x39);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        bVar1 = sRam00c0f102 == 0;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4a80);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

undefined4 FUN_0002e950(short param_1)

{
  undefined4 uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if ((sRam00c084fc == 6) && (lVar2 = func_0x003b69d0(0x429e,0x3a), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x429e,0x39);
      uVar1 = 0;
      if (lVar2 != 0) {
        if (sRam00c0f102 == 0) {
          if ((bRam003e2f59 & 1) == 0) {
            uVar1 = 0;
          }
          else {
            uVar1 = 1;
          }
        }
        else {
          uVar1 = 1;
        }
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4a90);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return uVar1;
}



ulong FUN_0002ea20(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  int iVar3;
  
  if (param_1 == 0) {
    if (sRam00c084fc == 6) {
      return (ulong)(sRam00c0853c == 0x55);
    }
    return 0;
  }
  if (param_1 != 1) {
    return 1;
  }
  func_0x003b5eb0(0x3f4aa0);
  uVar1 = SEXT18(cRam003e6de7);
  if (uVar1 == 0) {
    lVar2 = func_0x003b6290(param_2);
    if (lVar2 != 0) {
      cRam003e6de7 = 1;
      return 0;
    }
    return 0;
  }
  if (uVar1 != 1) {
    return uVar1;
  }
  lVar2 = func_0x003b5f10(0x191);
  if (lVar2 == 0) {
    iVar3 = 0x606;
    lVar2 = func_0x003b5f10(0x192);
    if (lVar2 == 0) goto LAB_0002eafc;
  }
  iVar3 = 0x607;
LAB_0002eafc:
  lVar2 = func_0x003b67b8(0x5a,iVar3,1);
  if (lVar2 != 0) {
    return 0;
  }
  if (iVar3 == 0x606) {
    func_0x003b5f40(0x191);
    func_0x003b5f40(0x192);
  }
  lVar2 = func_0x003e13d0(0x427e,0x22);
  if (lVar2 < 0) {
    uRam00c00174 = 6;
    uRam00c0358e = 1;
  }
  else {
    func_0x003b6c48(0x427e,0x22,1);
  }
  cRam003e6de7 = 0;
  return 1;
}



ulong FUN_0002eb98(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x55) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4ab0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x55,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002ec30(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x55 && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4ac0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002ecb8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 6 && sRam00c0853c == 0x55;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4ad0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002ed48(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 6) {
      if (sRam00c0853c == 0x55) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4ae0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x55,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_0002edf0(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4af0);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_0002eee0(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4b00);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



ulong FUN_0002efe0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 6) {
      uVar1 = (ulong)(sRam00c0853c == 0x43);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4b10);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(6,0x43);
        func_0x003b74b0();
        func_0x003b7548();
        func_0x003b71d8(0x427e,6);
        func_0x003b71d8(0x429e,0x43);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_0002f0a0(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    uVar2 = 0;
    if (sRam00c084fc == 6) {
      if (sRam00c0853c == 0x43) {
        uVar2 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar2 = 0;
      }
    }
    goto LAB_0002f1dc;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f4b20);
  if (cRam003e6de8 == '\x01') {
    lVar1 = func_0x003b67b8(0x93,0x700,1);
    if (lVar1 != 0) {
      uVar2 = 0;
      goto LAB_0002f1dc;
    }
    cRam003e6de8 = '\x02';
  }
  else {
    if (cRam003e6de8 >= '\x02') {
      if (cRam003e6de8 != '\x02') {
        return true;
      }
      uVar2 = func_0x003b6200(0x701);
      if (uVar2 != 0) {
        uRam00c0455e = 0xb1;
        uRam00c046b4 = 3;
        func_0x003e1430(0x429e,0x43,1);
        cRam003e6de8 = '\0';
        uVar2 = 1;
      }
      goto LAB_0002f1dc;
    }
    if (cRam003e6de8 != '\0') {
      return cRam003e6de8 < '\x02';
    }
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 1) {
      uVar2 = 0;
      goto LAB_0002f1dc;
    }
    cRam003e6de8 = '\x01';
  }
  uVar2 = 0;
LAB_0002f1dc:
  return (bool)(char)uVar2;
}



bool FUN_0002f1f8(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4b30);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_0002f2e8(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4b40);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



ulong FUN_0002f3e8(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    lVar1 = func_0x003b69d0(0x427e,0);
    uVar2 = 0;
    if (lVar1 != 0) {
      lVar1 = func_0x003b69d0(0x427e,1);
      uVar2 = 0;
      if (lVar1 != 0) {
        if (sRam00c08500 == 2) {
          lVar1 = func_0x003b69d0(0x429e,0x54);
          uVar2 = 0;
          if (lVar1 != 0) {
            lVar1 = func_0x003b69d0(0x429e,0x7f);
            uVar2 = (ulong)(lVar1 != 0);
          }
        }
        else {
          uVar2 = 0;
        }
      }
    }
  }
  else {
    uVar2 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4b50);
      uVar2 = func_0x003b6290(param_2);
      if (uVar2 != 0) {
        func_0x003b7130(1,0x54);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



bool FUN_0002f4b8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,1);
      uVar1 = 0;
      if (lVar2 != 0) {
        if (sRam00c08500 == 2) {
          lVar2 = func_0x003b69d0(0x429e,0x54);
          if (lVar2 == 0) {
            uVar1 = 0;
          }
          else {
            lVar2 = func_0x003b69d0(0x429e,0x7f);
            if (lVar2 == 0) {
              uVar1 = 0;
            }
            else {
              uVar1 = (ulong)(sRam00c0f102 == 0);
            }
          }
        }
        else {
          uVar1 = 0;
        }
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4b60);
      uVar1 = (ulong)(bRam003e6de9 < 2);
      if (bRam003e6de9 == 1) {
        lVar2 = func_0x003b67b8(0x34,0x796,1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_0002f614;
        }
        bRam003e6de9 = 2;
      }
      else {
        if (uVar1 == 0) {
          if (bRam003e6de9 != 2) {
            return true;
          }
          uVar1 = func_0x003b6200(0x797);
          if (uVar1 != 0) {
            bRam003e6de9 = 0;
            uVar1 = 1;
          }
          goto LAB_0002f614;
        }
        if (bRam003e6de9 != 0) goto LAB_0002f614;
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_0002f614;
        }
        bRam003e6de9 = 1;
      }
      uVar1 = 0;
    }
  }
LAB_0002f614:
  return (bool)(char)uVar1;
}



ulong FUN_0002f628(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      if (sRam00c08500 == 1) {
        uVar1 = (ulong)(sRam00c0853c == 0x3b);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4b70);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b74b0();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0002f6d8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 0x7c) {
      uVar1 = (ulong)(sRam00c0853c == 0x3b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4b80);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x427e,0x7c,1);
        uRam00c0f102 = 1;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0002f780(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 6) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c01d4a == 0x18);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4b90);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6aa0(0x429e,0x3b,8);
        func_0x003b6b20(0x429e,0x3b,0xa0);
        sRam00c01706 = sRam00c01706 + 8;
        sRam00c03dc4 = sRam00c03dc4 + 0xa0;
        sRam00c03f1a = sRam00c03f1a + 0xa0;
        sRam00c04070 = sRam00c04070 + 0xa0;
        sRam00c041c6 = sRam00c041c6 + 0xa0;
        sRam00c0431c = sRam00c0431c + 0xa0;
        func_0x003b7130(6,0x3b);
        func_0x003b71d8(0x427e,6);
        func_0x003b71d8(0x429e,0x3b);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0002f8d8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 6) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4ba0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0002f980(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x3b);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4bb0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002fa20(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x45;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4bc0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002fa98(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x45) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4bd0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        uRam00c0864a = 0;
        func_0x003e1430(0x429e,0x45,1);
        func_0x003e1430(0x429e,0x46,1);
        func_0x003e1430(0x429e,0x47,1);
        func_0x003b5f78(0x141);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_0002fb60(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 6) {
      uVar1 = (ulong)(sRam00c0853c == 0x3e);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4be0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(6,0x3e);
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b71d8(0x427e,6);
        func_0x003b71d8(0x429e,0x3e);
        func_0x003b7440(0x429e);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002fc30(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c084fc == 6) {
      if (sRam00c0853c == 0x3e) {
        bVar1 = cRam003e2f59 == '\0';
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4bf0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002fcd0(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    lVar2 = func_0x003b69d0(0x427e,6);
    if (lVar2 == 0) {
      lVar2 = func_0x003b69d0(0x427e,0x3e);
      if (lVar2 == 0) {
        bVar1 = sRam00c0853c == 0x3f;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4c00);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002fd78(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,6);
    if (lVar2 == 0) {
      lVar2 = func_0x003b69d0(0x427e,0x3e);
      uVar1 = 0;
      if (lVar2 == 0) {
        if (sRam00c0853c == 0x3f) {
          uVar1 = (ulong)(sRam00c0f102 == 0);
        }
        else {
          uVar1 = 0;
        }
      }
    }
    else {
      uVar1 = 0;
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4c10);
      uVar1 = func_0x003b6290(param_2);
      if ((uVar1 != 0) &&
         ((lVar2 = func_0x003e14f0(0x429e,0x3f), lVar2 == 4 || (uVar1 = 1, lVar2 == 0)))) {
        func_0x003e1430(0x429e,0x3f,3);
        func_0x003b6bd0(0x429e,0x3f,uRam00c00304);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_0002fe70(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,6);
    if (lVar2 == 0) {
      lVar2 = func_0x003b69d0(0x427e,0x3e);
      bVar1 = false;
      if (lVar2 == 0) {
        if (sRam00c0853c == 0x3f) {
          bVar1 = sRam00c0f102 != 0;
        }
        else {
          bVar1 = false;
        }
      }
    }
    else {
      bVar1 = false;
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4c20);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_0002ff28(short param_1)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = (ulong)(sRam00c0853c == 0x3f);
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4c30);
      uVar1 = (ulong)(bRam003e6dea < 5);
      if (uVar1 != 0) {
                    // WARNING: Could not emulate address calculation at 0x0002ff9c
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((uint)bRam003e6dea * 4 + 0x3f4c40))();
        return uVar1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00030078(short param_1)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    lVar2 = func_0x003b69d0(0x427e,6);
    if ((lVar2 != 0) || (lVar2 = func_0x003b69d0(0x427e,0x3e), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x429e,0x3f);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4c70);
      uVar1 = func_0x003b6f20(0x427e,0x3f4c58,6);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3f,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00030138(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x29 && sRam00c08540 == 1;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4c80);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_000301c8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4c90);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        lVar2 = func_0x003e14f0(0x429e,0x29);
        if (lVar2 == 4) {
          func_0x003e1430(0x429e,0x29,3);
          func_0x003b6bd0(0x429e,0x29,uRam00c002d8);
        }
        func_0x003b75b8(0x33);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000302a8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        bVar1 = sRam00c0f102 != 0;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4ca0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00030348(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 6) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4cb0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(6,0x28);
        func_0x003b6ea8(0x427e,6,0);
        func_0x003b6ea8(0x429e,0x28,0);
        func_0x003b74b0();
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b71d8(0x427e,6);
        func_0x003b71d8(0x429e,0x28);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00030438(short param_1)

{
  ulong uVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 6) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4cc0);
      uVar3 = 0xe07;
      if (sRam00c0f102 == 0) {
        uVar3 = 0xe08;
      }
      uVar1 = func_0x003b6200(uVar3);
      if (uVar1 != 0) {
        func_0x003b6ea8(0x427e,6,uRam00c00a96);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_000304e0(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc != 6) {
      uVar1 = (ulong)(sRam00c0853c == 0x68);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4cd0);
      if (cRam003e6deb == '\x01') {
        lVar2 = func_0x003b67b8(0x5a,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_00030614;
        }
      }
      else {
        if (cRam003e6deb < '\x02') {
          if (cRam003e6deb != '\0') {
            return cRam003e6deb < '\x02';
          }
          lVar2 = func_0x003b5f10(0x192);
          uVar1 = 0;
          cRam003e6deb = (lVar2 != 0) + '\x01';
          goto LAB_00030614;
        }
        if (cRam003e6deb != '\x02') {
          if (cRam003e6deb != '\x03') {
            return true;
          }
          uVar1 = func_0x003b6200(0xfad);
          if (uVar1 != 0) {
            cRam003e6deb = '\0';
            uVar1 = 1;
          }
          goto LAB_00030614;
        }
        lVar2 = func_0x003b6200(0xfac);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_00030614;
        }
      }
      cRam003e6deb = '\x03';
      uVar1 = 0;
    }
  }
LAB_00030614:
  return (bool)(char)uVar1;
}



bool FUN_00030628(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 6) {
      uVar1 = (ulong)(sRam00c0853c == 0x68);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4ce0);
      if (cRam003e6dec == '\x01') {
        lVar2 = func_0x003b67b8(0x5a,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_0003075c;
        }
      }
      else {
        if (cRam003e6dec < '\x02') {
          if (cRam003e6dec != '\0') {
            return cRam003e6dec < '\x02';
          }
          lVar2 = func_0x003b5f10(0x191);
          uVar1 = 0;
          cRam003e6dec = (lVar2 != 0) + '\x01';
          goto LAB_0003075c;
        }
        if (cRam003e6dec != '\x02') {
          if (cRam003e6dec != '\x03') {
            return true;
          }
          uVar1 = func_0x003b6200(0xfb7);
          if (uVar1 != 0) {
            cRam003e6dec = '\0';
            uVar1 = 1;
          }
          goto LAB_0003075c;
        }
        lVar2 = func_0x003b6200(0xfb6);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_0003075c;
        }
      }
      cRam003e6dec = '\x03';
      uVar1 = 0;
    }
  }
LAB_0003075c:
  return (bool)(char)uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00030770(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc != 6 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4cf0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00030800(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc != 6 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4d00);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00030890(short param_1,undefined8 param_2)

{
  int iVar1;
  ulong uVar2;
  long lVar3;
  
  if (param_1 == 0) {
    uVar2 = 0;
    lVar3 = func_0x003b69d0(0x427e,0x20);
    if (((lVar3 != 0) && (lVar3 = func_0x003b69d0(0x427e,0x21), lVar3 != 0)) &&
       (lVar3 = func_0x003b69d0(0x427e,0x22), lVar3 != 0)) {
      lVar3 = func_0x003b69d0(0x427e,0x23);
      if (lVar3 == 0) {
        uVar2 = 0;
      }
      else {
        uVar2 = (ulong)(sRam00c0853c == 0x68);
      }
    }
  }
  else {
    uVar2 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4d10);
      uVar2 = SEXT18(cRam003e6ded);
      if (uVar2 == 0) {
        lVar3 = func_0x003b6290(param_2);
        if (lVar3 == 1) {
          cRam003e6ded = '\x01';
          uVar2 = 0;
        }
        else {
          uVar2 = 0;
        }
      }
      else {
        if (uVar2 == 1) {
          lVar3 = func_0x003b67b8(0x9a,0xfe8,1);
          if (lVar3 == 0) {
            func_0x003b6aa0(0x429e,0x68,3);
            iVar1 = (int)((uRam00c0160a + 0xffe2) * 0x10000) >> 0x10;
            if (iVar1 < 0) {
              iVar1 = 0;
            }
            uRam00c0160a = (ushort)iVar1;
            sRam00c01760 = sRam00c01760 + 3;
            uRam00c01e0e = 0;
            func_0x003b74b0();
            cRam003e6ded = '\0';
            uVar2 = 1;
          }
          else {
            uVar2 = 0;
          }
        }
      }
    }
  }
  return uVar2;
}



ulong FUN_00030a18(short param_1)

{
  undefined2 uVar1;
  ulong uVar2;
  undefined2 *puVar3;
  int iVar4;
  
  if (param_1 == 0) {
    uVar2 = 0;
    if (sRam00c0853c == 0x68) {
      uVar2 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar2 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4d28);
      uVar2 = func_0x003b6200(0x100f);
      iVar4 = 0;
      if (uVar2 != 0) {
        puVar3 = (undefined2 *)0x3f4d20;
        uVar1 = uRam003f4d20;
        while( true ) {
          func_0x003b6bd0(0x429e,uVar1,6);
          uVar1 = *puVar3;
          puVar3 = puVar3 + 1;
          func_0x003e1430(0x429e,uVar1);
          iVar4 = (iVar4 + 1) * 0x1000000 >> 0x18;
          if (3 < iVar4) break;
          uVar1 = *puVar3;
        }
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



ulong FUN_00030ae8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  ushort *puVar3;
  int iVar4;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x68) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4d40);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        puVar3 = (ushort *)0x3f4d38;
        iVar4 = 0;
        do {
          lVar2 = func_0x003e14f0(0x429e,*puVar3);
          if (lVar2 == 4) {
            func_0x003b6bd0(0x429e,*puVar3,*(undefined2 *)((uint)*puVar3 * 2 + 0xc00286));
            func_0x003e1430(0x429e,*puVar3,3);
          }
          uRam003e2c92 = uRam00c01208;
          iVar4 = (iVar4 + 1) * 0x1000000 >> 0x18;
          puVar3 = puVar3 + 1;
        } while (iVar4 < 4);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_00030c00(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
    return true;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f4d50);
  if (cRam003e6dee == '\x01') {
    lVar1 = func_0x003b6200(0x2334);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6dee = 2;
  }
  else {
    if (cRam003e6dee >= '\x02') {
      if (cRam003e6dee != '\x02') {
        return false;
      }
      uRam00c0853c = 0x78;
      uRam00c08542 = 0x78;
      uRam00c00b7a = 100;
      uRam00c08540 = 1;
      uRam00c08558 = 3;
      uRam00c00a24 = 0;
      func_0x00389580(0x3e2c80,0x78);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 5;
      uRam00c08502 = 5;
      uRam00c08504 = 7;
      uRam00c0851a = 3;
      uRam00c08500 = 2;
      uRam00c08518 = 3;
      func_0x00389580(0x3e2a38,5);
      func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
      func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396a98(0x42,0x1073f00,uRam00c08504);
      uRam003e2a30 = 0;
      uRam003e2f5a = 0;
      cRam003e6dee = 0;
      return true;
    }
    if (cRam003e6dee != '\0') {
      return cRam003e6dee < '\x02';
    }
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6dee = 1;
  }
  return false;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00030e20(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4d58);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_00030e98(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f4d60);
    uVar1 = 0x3e0000;
    if (cRam003e6def == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6def = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6def == '\x01') {
        uRam00c0853c = 0x79;
        uRam00c08542 = 0x79;
        uRam00c00a26 = 9;
        uRam00c08540 = 1;
        uRam00c00b7c = 100;
        uRam00c08558 = 3;
        func_0x00389580(0x3e2c80,0x79);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c084fc = 3;
        uRam00c08502 = 3;
        uRam00c08504 = 4;
        uRam00c08500 = 2;
        uRam00c0851a = 3;
        uRam00c08518 = 3;
        func_0x00389580(0x3e2a38,3);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        cRam003e6def = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00031080(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4d68);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_000310f8(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f4d70);
    uVar1 = 0x3e0000;
    if (cRam003e6df0 == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6df0 = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6df0 == '\x01') {
        uRam00c0853c = 0x8d;
        uRam00c08542 = 0x8d;
        uRam00c08558 = 3;
        uRam00c00a4e = 0;
        uRam00c00ba4 = 0;
        uRam00c047c2 = 0;
        uRam00c08540 = 1;
        func_0x00389580(0x3e2c80,0x8d);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c084fc = 0;
        uRam00c08502 = 0;
        uRam00c08506 = 2;
        uRam00c08500 = 3;
        uRam00c08518 = 3;
        uRam00c0851a = 3;
        uRam00c0851c = 3;
        uRam00c08504 = 1;
        func_0x00389580(0x3e2a38,0);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00389580(0x3e2aac,(undefined)uRam00c08506);
        func_0x00396e38(0x3e3008,0,0,2,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        func_0x00396a98(0x42,0x1074800,uRam00c08506);
        cRam003e6df0 = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00031320(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4d78);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00031398(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f4d80);
    uVar1 = (ulong)(cRam003e6df1 < '\x02');
    if (cRam003e6df1 == '\x01') {
      uRam00c0853c = 0x7e;
      uRam00c08542 = 0x7e;
      uRam00c08558 = 3;
      uRam00c08540 = 1;
      func_0x00389580(0x3e2c80,0x7e);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 6;
      uRam00c08502 = 6;
      uRam00c08518 = 3;
      uRam00c08500 = 1;
      func_0x00389580(0x3e2a38,6);
      func_0x00396e38(0x3e3008,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396420();
      uRam003e2a30 = 1;
      uVar1 = 0;
      cRam003e6df1 = '\x02';
    }
    else {
      if (uVar1 == 0) {
        uVar1 = 2;
        if ((cRam003e6df1 == '\x02') && (uVar1 = func_0x003b6428(), uVar1 != 0)) {
          cRam003e6df1 = '\0';
          uRam003ecf72 = 0;
          uVar1 = 1;
        }
      }
      else {
        if (cRam003e6df1 == '\0') {
          lVar2 = func_0x003b6290(param_2);
          if (lVar2 == 1) {
            cRam003e6df1 = '\x01';
          }
          uVar1 = 0;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_00031580(short param_1,undefined8 param_2,uint param_3)

{
  ushort uVar1;
  int iVar2;
  ulong uVar3;
  long lVar4;
  uint uVar5;
  
  uVar5 = param_3 & 0xffff;
  if (param_1 == 0) {
    uVar1 = *(ushort *)(uVar5 * 2 + 0xc007de);
    uVar3 = 0;
    if (uVar5 == uRam00c0853c) {
      if (sRam00c0f102 == 0) {
        lVar4 = func_0x003b6cd0(0x429e,uVar5);
        if (lVar4 == 0) {
          uVar3 = 0;
        }
        else {
          iVar2 = func_0x003b7568(uVar5);
          uVar3 = (ulong)((int)(uint)(uVar1 >> 0xf) < iVar2);
        }
      }
      else {
        uVar3 = 0;
      }
    }
  }
  else {
    uVar3 = 1;
    if ((param_1 == 1) && (uVar3 = func_0x003b6290(param_2), uVar3 != 0)) {
      func_0x003e1430(0x429e,uVar5,1);
      func_0x003b6bd0(0x429e,uVar5,uVar5);
      uVar3 = 1;
    }
  }
  return uVar3;
}



bool FUN_00031660(short param_1,int param_2,short param_3,ulong param_4,undefined2 param_5)

{
  ulong uVar1;
  short sVar2;
  
  if (param_1 == 0) {
    uVar1 = param_4 & 0xffff ^ (ulong)uRam00c0853c;
  }
  else {
    if (param_1 != 1) {
      return true;
    }
    func_0x003b5eb0(0x3f4d88);
    if (param_3 == sRam00c084fc) {
      sVar2 = *(short *)(param_2 + 0x1c);
    }
    else {
      sVar2 = *(short *)(param_2 + 0x1c) + 1;
    }
    uVar1 = func_0x003b67b8(param_5,sVar2,1);
  }
  return uVar1 == 0;
}



bool FUN_00031708(short param_1,int param_2)

{
  long lVar1;
  bool bVar2;
  
  if (param_1 == 0) {
    bVar2 = sRam00c084fc != 7 && sRam00c0853c == 2;
  }
  else {
    bVar2 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4d98);
      lVar1 = func_0x003b67b8(0x77,*(undefined2 *)(param_2 + 0x1c),1);
      bVar2 = lVar1 == 0;
    }
  }
  return bVar2;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00031798(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



bool FUN_000319e8(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c084fc == 7) {
      if (sRam00c0853c == 0) {
        bVar1 = sRam00c0f102 == 0;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4db8);
      if (cRam003e6dfb == '\x01') {
        lVar2 = func_0x003b67b8(4,0x2f0,1);
        if (lVar2 != 0) {
          return false;
        }
        cRam003e6dfb = '\x02';
      }
      else {
        if (cRam003e6dfb < '\x02') {
          if (cRam003e6dfb != '\0') {
            return cRam003e6dfb < '\x02';
          }
          lVar2 = func_0x003b6290(param_2);
          if (lVar2 == 0) {
            return false;
          }
          cRam003e6dfb = '\x01';
        }
        else {
          if (cRam003e6dfb != '\x02') {
            if (cRam003e6dfb != '\x03') {
              return true;
            }
            lVar2 = func_0x003e14f0(0x429e,0);
            if (lVar2 == 4) {
              func_0x003e1430(0x429e,0,3);
              func_0x003b6bd0(0x429e,0,0);
            }
            cRam003e6dfb = 0;
            return true;
          }
          lVar2 = func_0x003b6200(0x2f1);
          if (lVar2 == 0) {
            return false;
          }
          cRam003e6dfb = '\x03';
        }
      }
      bVar1 = false;
    }
  }
  return bVar1;
}



bool FUN_00031b58(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 7) {
      if (sRam00c0853c == 0) {
        uVar1 = (ulong)(sRam00c0f102 != 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4dc8);
      uVar1 = (ulong)(cRam003e6dfc < '\x02');
      if (cRam003e6dfc == '\x01') {
        lVar2 = func_0x003b67b8(4,0x2f0,1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_00031c6c;
        }
        cRam003e6dfc = '\x02';
      }
      else {
        if (uVar1 == 0) {
          if (cRam003e6dfc != '\x02') {
            return true;
          }
          uVar1 = func_0x003b6200(0x2f1);
          if (uVar1 != 0) {
            cRam003e6dfc = '\0';
            uVar1 = 1;
          }
          goto LAB_00031c6c;
        }
        if (cRam003e6dfc != '\0') goto LAB_00031c6c;
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 0) {
          uVar1 = 0;
          goto LAB_00031c6c;
        }
        cRam003e6dfc = '\x01';
      }
      uVar1 = 0;
    }
  }
LAB_00031c6c:
  return (bool)(char)uVar1;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00031c80(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



bool FUN_00031ea8(short param_1)

{
  ushort uVar1;
  byte bVar2;
  int iVar3;
  ulong uVar4;
  long lVar5;
  
  uVar1 = uRam00c007e0;
  if (param_1 == 0) {
    lVar5 = func_0x003b69d0(0x429e,1);
    if ((lVar5 == 0) && (lVar5 = func_0x003b69d0(0x429e,0xe), lVar5 == 0)) {
      lVar5 = func_0x003b69d0(0x429e,0xc);
      uVar4 = 0;
      bVar2 = bRam003e6dfe;
      if (lVar5 == 0) goto LAB_000320c0;
    }
    if (sRam00c0f102 == 0) {
      iVar3 = func_0x003b7568(1);
      uVar4 = (ulong)(iVar3 <= (int)(uint)(uVar1 >> 0xf));
      bVar2 = bRam003e6dfe;
    }
    else {
      uVar4 = 0;
      bVar2 = bRam003e6dfe;
    }
  }
  else {
    uVar4 = 1;
    bVar2 = bRam003e6dfe;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4de8);
      uVar4 = (ulong)(bRam003e6dfe < 2);
      if (bRam003e6dfe == 1) {
        uVar4 = func_0x003b6cd0(0x429e,0xe);
        if (uVar4 == 0) {
          bRam003e6dfe = 2;
          bVar2 = bRam003e6dfe;
          goto LAB_000320c0;
        }
        lVar5 = func_0x003b6200(899);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6dfe;
          goto LAB_000320c0;
        }
        func_0x003b5f40(0x84);
        func_0x003e1430(0x429e,0xe,1);
        func_0x003b6bd0(0x429e,0xe,1);
        bRam003e6dfe = 2;
      }
      else {
        if (uVar4 == 0) {
          if (bRam003e6dfe != 2) {
            return true;
          }
          lVar5 = func_0x003b6cd0(0x429e,0xc);
          if (lVar5 != 0) {
            uVar4 = func_0x003b6200(0x382);
            bVar2 = bRam003e6dfe;
            if (uVar4 == 0) goto LAB_000320c0;
            func_0x003b5f40(0x84);
            func_0x003e1430(0x429e,0xc,1);
            func_0x003b6bd0(0x429e,0xc,1);
          }
          bRam003e6dfe = 0;
          uVar4 = 1;
          bVar2 = bRam003e6dfe;
          goto LAB_000320c0;
        }
        bVar2 = bRam003e6dfe;
        if (bRam003e6dfe != 0) goto LAB_000320c0;
        uVar4 = func_0x003b6cd0(0x429e,1);
        bVar2 = 1;
        if (uVar4 == 0) goto LAB_000320c0;
        lVar5 = func_0x003b6200(0x37b);
        if (lVar5 == 0) {
          uVar4 = 0;
          bVar2 = bRam003e6dfe;
          goto LAB_000320c0;
        }
        func_0x003b5f40(0x59);
        func_0x003e1430(0x429e,1,1);
        func_0x003b6bd0(0x429e,1,1);
        func_0x003b7620(0x429e,1,2,10);
        bRam003e6dfe = 1;
        uRam003e2c86 = 2;
      }
      uVar4 = 0;
      bVar2 = bRam003e6dfe;
    }
  }
LAB_000320c0:
  bRam003e6dfe = bVar2;
  return (bool)(char)uVar4;
}



bool FUN_000320d0(void)

{
  bool bVar1;
  long lVar2;
  
  lVar2 = func_0x001251e8();
  if (lVar2 == 0) {
    lVar2 = func_0x00125258();
    if (lVar2 == 0) {
      bVar1 = false;
      if ((sRam00c084fc == 7) && (bVar1 = false, sRam00c0853c == 0x2a)) {
        bVar1 = false;
        if (sRam00c08500 == 1) {
          bVar1 = sRam00c08540 == 1;
        }
      }
    }
    else {
      bVar1 = false;
    }
  }
  else {
    bVar1 = false;
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

long FUN_00032160(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  lVar1 = 1;
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f4df8);
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 0) {
      func_0x003b7130(7,0x2a);
      lVar1 = 1;
    }
  }
  return lVar1;
}



ulong FUN_000321d8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b29d0();
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4e08);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003de9c8(7,0x2a);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00032268(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b29d0();
    bVar1 = lVar2 != 0 && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4e18);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_000322f0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0853c == 0x2b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4e28);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(0x2a,0x38);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00032388(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 == 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4e38);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00032418(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 != 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4e48);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000324d0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x2b && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4e58);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00032558(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    bVar1 = lVar2 != 0 && sRam00c0853c == 0x2b;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4e68);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_000325e8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0x2a);
    uVar1 = 0;
    if (lVar2 == 0) {
      if (sRam00c0853c == 0x2b) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4e78);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x2b,1);
        func_0x003e1430(0x429e,0x38,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_000326a0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 7) {
      uVar1 = (ulong)(sRam00c0853c == 0x8b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4e88);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(7,0x8b);
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b74b0();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



char FUN_00032750(short param_1,undefined8 param_2)

{
  char cVar1;
  long lVar2;
  
  cVar1 = '\x01';
  if ((param_1 != 0) && (cVar1 = '\x01', param_1 == 1)) {
    func_0x003b5eb0(0x3f4e98);
    if (cRam003e6dff == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 0) {
        cVar1 = '\0';
      }
      else {
        cRam003e6dff = '\x01';
        cVar1 = '\0';
      }
    }
    else {
      cVar1 = cRam003e6dff;
      if (cRam003e6dff == '\x01') {
        lVar2 = func_0x003b67b8(100,0x4e4,1);
        if (lVar2 == 0) {
          cRam003e6dff = '\0';
          cVar1 = '\x01';
        }
        else {
          cVar1 = '\0';
        }
      }
    }
  }
  return cVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00032810(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    if ((sRam00c084fc == 7) && (lVar2 = func_0x003b69d0(0x429e,0x3a), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x429e,0x39);
      bVar1 = lVar2 != 0;
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4ea8);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000328b0(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    if ((sRam00c084fc == 7) && (lVar2 = func_0x003b69d0(0x429e,0x3a), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x429e,0x39);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        bVar1 = sRam00c0f102 == 0;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4eb8);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00032960(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    bVar1 = false;
    if ((sRam00c084fc == 7) && (lVar2 = func_0x003b69d0(0x429e,0x3a), lVar2 != 0)) {
      lVar2 = func_0x003b69d0(0x429e,0x39);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        bVar1 = sRam00c0f102 != 0;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4ec8);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00032a10(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 7) {
      uVar1 = (ulong)(sRam00c0853c == 1);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4ed8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(7,1);
        func_0x003b71d8(0x427e,7);
        func_0x003b71d8(0x429e,1);
        func_0x003b74b0();
        func_0x003b7548();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_00032ad0(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    uVar2 = 0;
    if (sRam00c084fc == 7) {
      if (sRam00c0853c == 1) {
        uVar2 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar2 = 0;
      }
    }
    goto LAB_00032bf4;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f4f10);
  if (cRam003e6e00 == '\x01') {
    lVar1 = func_0x003b6f20(0x427e,0x3f4ee8,10);
    if (lVar1 != 1) {
      uVar2 = 0;
      goto LAB_00032bf4;
    }
    cRam003e6e00 = '\x02';
  }
  else {
    if (cRam003e6e00 >= '\x02') {
      if (cRam003e6e00 != '\x02') {
        return true;
      }
      uVar2 = func_0x003b6200(0x5bc);
      if (uVar2 != 0) {
        cRam003e6e00 = '\0';
        uVar2 = 1;
      }
      goto LAB_00032bf4;
    }
    if (cRam003e6e00 != '\0') {
      return cRam003e6e00 < '\x02';
    }
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 == 0) {
      uVar2 = 0;
      goto LAB_00032bf4;
    }
    cRam003e6e00 = '\x01';
  }
  uVar2 = 0;
LAB_00032bf4:
  return (bool)(char)uVar2;
}



ulong FUN_00032c10(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 7) {
      uVar1 = (ulong)(sRam00c0853c == 0x55);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4f20);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(7,0x55);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00032ca8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x55) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4f30);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x55,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00032d40(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x55 && sRam00c0f102 != 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4f40);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00032dc8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 7 && sRam00c0853c == 0x55;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4f50);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00032e58(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 1) {
      if (sRam00c08500 == 1) {
        uVar1 = (ulong)(sRam00c0853c == 0x3b);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4f60);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6aa0(0x427e,1,0xfffffffffffffffd);
        func_0x003b6aa0(0x429e,0x3b,5);
        func_0x003b74b0();
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00032f18(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 1) {
      uVar1 = (ulong)(sRam00c0853c == 0x3b);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4f70);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x427e,1,1);
        func_0x003e1430(0x429e,0x3b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_00032fc8(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4f80);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_000330b8(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,2);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,4);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,6);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x39);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4f90);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_000331b8(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                bVar1 = lVar2 != 0;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4fa0);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



bool FUN_000332a8(short param_1,undefined8 param_2)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,3);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,5);
      if (lVar2 == 0) {
        bVar1 = false;
      }
      else {
        lVar2 = func_0x003b69d0(0x427e,7);
        bVar1 = false;
        if (lVar2 != 0) {
          if (sRam00c08500 == 3) {
            lVar2 = func_0x003b69d0(0x429e,0x3a);
            if (lVar2 == 0) {
              bVar1 = false;
            }
            else {
              lVar2 = func_0x003b69d0(0x429e,0x7f);
              if (lVar2 == 0) {
                bVar1 = false;
              }
              else {
                lVar2 = func_0x003b69d0(0x429e,0x80);
                if (lVar2 == 0) {
                  bVar1 = false;
                }
                else {
                  bVar1 = sRam00c0f102 == 0;
                }
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4fb0);
      lVar2 = func_0x003b6290(param_2);
      bVar1 = lVar2 != 0;
    }
  }
  return bVar1;
}



ulong FUN_000333a8(short param_1,undefined8 param_2)

{
  long lVar1;
  ulong uVar2;
  
  if (param_1 == 0) {
    lVar1 = func_0x003b69d0(0x427e,0);
    uVar2 = 0;
    if (lVar1 != 0) {
      lVar1 = func_0x003b69d0(0x427e,1);
      uVar2 = 0;
      if (lVar1 != 0) {
        if (sRam00c08500 == 2) {
          lVar1 = func_0x003b69d0(0x429e,0x54);
          uVar2 = 0;
          if (lVar1 != 0) {
            lVar1 = func_0x003b69d0(0x429e,0x7f);
            uVar2 = (ulong)(lVar1 != 0);
          }
        }
        else {
          uVar2 = 0;
        }
      }
    }
  }
  else {
    uVar2 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4fc0);
      uVar2 = func_0x003b6290(param_2);
      if (uVar2 != 0) {
        func_0x003b7130(1,0x54);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



bool FUN_00033478(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,0);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      lVar2 = func_0x003b69d0(0x427e,1);
      uVar1 = 0;
      if (lVar2 != 0) {
        if (sRam00c08500 == 2) {
          lVar2 = func_0x003b69d0(0x429e,0x54);
          if (lVar2 == 0) {
            uVar1 = 0;
          }
          else {
            lVar2 = func_0x003b69d0(0x429e,0x7f);
            if (lVar2 == 0) {
              uVar1 = 0;
            }
            else {
              uVar1 = (ulong)(sRam00c0f102 == 0);
            }
          }
        }
        else {
          uVar1 = 0;
        }
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4fd0);
      uVar1 = (ulong)(bRam003e6e01 < 2);
      if (bRam003e6e01 == 1) {
        lVar2 = func_0x003b67b8(0x34,0x796,1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_000335d4;
        }
        bRam003e6e01 = 2;
      }
      else {
        if (uVar1 == 0) {
          if (bRam003e6e01 != 2) {
            return true;
          }
          uVar1 = func_0x003b6200(0x797);
          if (uVar1 != 0) {
            bRam003e6e01 = 0;
            uVar1 = 1;
          }
          goto LAB_000335d4;
        }
        if (bRam003e6e01 != 0) goto LAB_000335d4;
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_000335d4;
        }
        bRam003e6e01 = 1;
      }
      uVar1 = 0;
    }
  }
LAB_000335d4:
  return (bool)(char)uVar1;
}



ulong FUN_000335e8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 7) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c01d4c == 0x17);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4fe0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b6aa0(0x429e,0x3b,8);
        func_0x003b6b20(0x429e,0x3b,0xa0);
        sRam00c01706 = sRam00c01706 + 8;
        sRam00c03dc4 = sRam00c03dc4 + 0xa0;
        sRam00c03f1a = sRam00c03f1a + 0xa0;
        sRam00c04070 = sRam00c04070 + 0xa0;
        sRam00c041c6 = sRam00c041c6 + 0xa0;
        sRam00c0431c = sRam00c0431c + 0xa0;
        func_0x003b7130(7,0x3b);
        func_0x003b71d8(0x427e,7);
        func_0x003b71d8(0x429e,0x3b);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00033740(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 7) {
      lVar2 = func_0x003b69d0(0x429e,0x3b);
      if (lVar2 == 0) {
        uVar1 = 0;
      }
      else {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f4ff0);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_000337e8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x3b);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5000);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x3b,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00033888(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,7);
    bVar1 = lVar2 == 0 && sRam00c0853c == 0x41;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5010);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00033918(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,7);
    bVar1 = lVar2 != 0 && sRam00c0853c == 0x41;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5020);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_000339a8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x41) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5030);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003de9c8(7,0x41);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00033a38(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,7);
    bVar1 = lVar2 == 0 && sRam00c0853c == 0x40;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5040);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



bool FUN_00033ac8(short param_1)

{
  bool bVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x427e,7);
    if (lVar2 == 0) {
      bVar1 = false;
    }
    else {
      bVar1 = sRam00c0853c == 0x40;
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5050);
      lVar2 = func_0x003b67b8(0x70,0xc63,1);
      bVar1 = lVar2 == 0;
    }
  }
  return bVar1;
}



ulong FUN_00033b58(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x40) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5060);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003e1430(0x429e,0x40,1);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00033bf0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0853c == 0x45;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5070);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00033c68(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x45) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5080);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        uRam00c0864c = 0;
        func_0x003e1430(0x429e,0x45,1);
        func_0x003e1430(0x429e,0x46,1);
        func_0x003e1430(0x429e,0x47,1);
        func_0x003b5f78(0x141);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00033d30(short param_1,undefined8 param_2)

{
  ulong uVar1;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 7) {
      uVar1 = (ulong)(sRam00c0853c == 0x3e);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5090);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(7,0x3e);
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b71d8(0x427e,7);
        func_0x003b71d8(0x429e,0x3e);
        func_0x003b7440(0x429e);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00033e00(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c084fc == 7) {
      if (sRam00c0853c == 0x3e) {
        bVar1 = cRam003e2f59 == '\0';
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f50a0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00033ea0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc == 7 && sRam00c0853c == 0x3f;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f50b0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00033f30(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x3f) {
      if (sRam00c084fc == 7) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f50c0);
      uVar1 = (ulong)bRam003e6e02;
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 0) {
          bRam003e6e02 = 1;
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0xd36), uVar1 != 0)) {
          lVar2 = func_0x003e14f0(0x429e,0x3f);
          if ((lVar2 == 4) || (lVar2 == 0)) {
            func_0x003e1430(0x429e,0x3f,3);
            func_0x003b6bd0(0x429e,0x3f,uRam00c00304);
          }
          bRam003e6e02 = 0;
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_00034068(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 7) {
      if (sRam00c0853c == 0x3f) {
        uVar1 = (ulong)(sRam00c0f102 != 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f50d0);
      uVar1 = (ulong)bRam003e6e03;
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 != 0) {
          bRam003e6e03 = 1;
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0xd36), uVar1 != 0)) {
          bRam003e6e03 = 0;
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x000341bc: Changing call to branch
// WARNING: Removing unreachable block (ram,0x000341c4)
// WARNING: Removing unreachable block (ram,0x000341cc)
// WARNING: Removing unreachable block (ram,0x000341f4)

void FUN_00034158(short param_1)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f50e8);
    lVar1 = func_0x003b6f20(0x427e,0x3f50e0,2);
    if (lVar1 != 0) {
      return;
    }
  }
  return;
}



ulong FUN_00034208(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    lVar2 = func_0x003b69d0(0x429e,0x3f);
    if (lVar2 == 0) {
      uVar1 = 0;
    }
    else {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5110);
      uVar1 = SEXT18(cRam003e6e04);
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        if (lVar2 == 1) {
          cRam003e6e04 = '\x01';
        }
        uVar1 = 0;
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6f20(0x427e,0x3f50f8,5), uVar1 != 0)) {
          func_0x003e1430(0x429e,0x3f,1);
          cRam003e6e04 = '\0';
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x00034364: Changing call to branch
// WARNING: Removing unreachable block (ram,0x0003436c)
// WARNING: Removing unreachable block (ram,0x00034374)
// WARNING: Removing unreachable block (ram,0x0003439c)

void FUN_00034300(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  if (param_1 == 1) {
    func_0x003b5eb0(0x3f5120,0x3f);
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 0) {
      return;
    }
  }
  return;
}



ulong FUN_000343b0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x29) {
      uVar1 = (ulong)(sRam00c08540 == 1);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5130);
      uVar1 = SEXT18(cRam003e6e05);
      if (uVar1 == 0) {
        lVar2 = func_0x003b6290(param_2);
        uVar1 = 0;
        if (lVar2 == 1) {
          if (sRam00c084fc == 7) {
            lVar2 = func_0x003b5f10(0xa8);
            if (lVar2 == 0) {
              cRam003e6e05 = '\x01';
              uVar1 = 0;
            }
            else {
              uVar1 = 1;
            }
          }
          else {
            uVar1 = 1;
          }
        }
      }
      else {
        if ((uVar1 == 1) && (uVar1 = func_0x003b6200(0xdc2), uVar1 != 0)) {
          func_0x003b6aa0(0x429e,0x29,3);
          func_0x003b6b20(0x429e,0x29,0x3c);
          cRam003e6e05 = '\0';
          uVar1 = 1;
        }
      }
    }
  }
  return uVar1;
}



ulong FUN_000344e0(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        uVar1 = (ulong)(sRam00c0f102 == 0);
      }
      else {
        uVar1 = 0;
      }
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5140);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        lVar2 = func_0x003e14f0(0x429e,0x29);
        if (lVar2 == 4) {
          func_0x003e1430(0x429e,0x29,3);
          func_0x003b6bd0(0x429e,0x29,uRam00c002d8);
        }
        func_0x003b75b8(0x33);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000345c0(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = false;
    if (sRam00c0853c == 0x29) {
      if (sRam00c08540 == 1) {
        bVar1 = sRam00c0f102 != 0;
      }
      else {
        bVar1 = false;
      }
    }
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5150);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00034660(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 7) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5160);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        func_0x003b7130(7,0x28);
        func_0x003b6ea8(0x427e,7,0);
        func_0x003b6ea8(0x429e,0x28,0);
        func_0x003b74b0();
        bRam003e2fd2 = bRam003e2fd2 | 0x11;
        func_0x003b71d8(0x427e,7);
        func_0x003b71d8(0x429e,0x28);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



ulong FUN_00034750(short param_1)

{
  ulong uVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 7) {
      lVar2 = func_0x003b69d0(0x429e,0x28);
      uVar1 = (ulong)(lVar2 != 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5170);
      uVar3 = 0xe07;
      if (sRam00c0f102 == 0) {
        uVar3 = 0xe08;
      }
      uVar1 = func_0x003b6200(uVar3);
      if (uVar1 != 0) {
        func_0x003b6ea8(0x427e,7,uRam00c00a98);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_000347f8(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc != 7) {
      uVar1 = (ulong)(sRam00c0853c == 0x68);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5180);
      if (cRam003e6e06 == '\x01') {
        lVar2 = func_0x003b67b8(0x5a,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_0003492c;
        }
      }
      else {
        if (cRam003e6e06 < '\x02') {
          if (cRam003e6e06 != '\0') {
            return cRam003e6e06 < '\x02';
          }
          lVar2 = func_0x003b5f10(0x192);
          uVar1 = 0;
          cRam003e6e06 = (lVar2 != 0) + '\x01';
          goto LAB_0003492c;
        }
        if (cRam003e6e06 != '\x02') {
          if (cRam003e6e06 != '\x03') {
            return true;
          }
          uVar1 = func_0x003b6200(0xfad);
          if (uVar1 != 0) {
            cRam003e6e06 = '\0';
            uVar1 = 1;
          }
          goto LAB_0003492c;
        }
        lVar2 = func_0x003b6200(0xfac);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_0003492c;
        }
      }
      cRam003e6e06 = '\x03';
      uVar1 = 0;
    }
  }
LAB_0003492c:
  return (bool)(char)uVar1;
}



bool FUN_00034940(short param_1,int param_2)

{
  ulong uVar1;
  long lVar2;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c084fc == 7) {
      uVar1 = (ulong)(sRam00c0853c == 0x68);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f5190);
      if (cRam003e6e07 == '\x01') {
        lVar2 = func_0x003b67b8(0x5a,*(undefined2 *)(param_2 + 0x1c),1);
        if (lVar2 != 0) {
          uVar1 = 0;
          goto LAB_00034a74;
        }
      }
      else {
        if (cRam003e6e07 < '\x02') {
          if (cRam003e6e07 != '\0') {
            return cRam003e6e07 < '\x02';
          }
          lVar2 = func_0x003b5f10(0x191);
          uVar1 = 0;
          cRam003e6e07 = (lVar2 != 0) + '\x01';
          goto LAB_00034a74;
        }
        if (cRam003e6e07 != '\x02') {
          if (cRam003e6e07 != '\x03') {
            return true;
          }
          uVar1 = func_0x003b6200(0xfb7);
          if (uVar1 != 0) {
            cRam003e6e07 = '\0';
            uVar1 = 1;
          }
          goto LAB_00034a74;
        }
        lVar2 = func_0x003b6200(0xfb6);
        if (lVar2 != 1) {
          uVar1 = 0;
          goto LAB_00034a74;
        }
      }
      cRam003e6e07 = '\x03';
      uVar1 = 0;
    }
  }
LAB_00034a74:
  return (bool)(char)uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00034a88(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c084fc != 7 && sRam00c0853c == 0x68;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f51a0);
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_00034b18(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  ushort *puVar3;
  int iVar4;
  
  if (param_1 == 0) {
    uVar1 = 0;
    if (sRam00c0853c == 0x68) {
      uVar1 = (ulong)(sRam00c0f102 == 0);
    }
  }
  else {
    uVar1 = 1;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f51b8);
      uVar1 = func_0x003b6290(param_2);
      if (uVar1 != 0) {
        puVar3 = (ushort *)0x3f51b0;
        iVar4 = 0;
        do {
          lVar2 = func_0x003e14f0(0x429e,*puVar3);
          if (lVar2 == 4) {
            func_0x003b6bd0(0x429e,*puVar3,*(undefined2 *)((uint)*puVar3 * 2 + 0xc00286));
            func_0x003e1430(0x429e,*puVar3,3);
          }
          uRam003e2c92 = uRam00c01208;
          iVar4 = (iVar4 + 1) * 0x1000000 >> 0x18;
          puVar3 = puVar3 + 1;
        } while (iVar4 < 4);
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



bool FUN_00034c30(short param_1,undefined8 param_2)

{
  long lVar1;
  
  if (param_1 == 0) {
    return true;
  }
  if (param_1 != 1) {
    return true;
  }
  func_0x003b5eb0(0x3f51c8);
  if (cRam003e6e08 == '\x01') {
    lVar1 = func_0x003b6200(0x2334);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6e08 = 2;
  }
  else {
    if (cRam003e6e08 >= '\x02') {
      if (cRam003e6e08 != '\x02') {
        return false;
      }
      uRam00c0853c = 0x78;
      uRam00c08542 = 0x78;
      uRam00c00b7a = 100;
      uRam00c08558 = 3;
      uRam00c08540 = 1;
      uRam00c00a24 = 0;
      func_0x00389580(0x3e2c80,0x78);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 1;
      uRam00c08502 = 1;
      uRam00c08504 = 5;
      uRam00c0851a = 3;
      uRam00c08500 = 2;
      uRam00c08518 = 3;
      func_0x00389580(0x3e2a38,1);
      func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
      func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396a98(0x42,0x1073f00,uRam00c08504);
      uRam003e2a30 = 0;
      uRam003e2f5a = 0;
      cRam003e6e08 = 0;
      return true;
    }
    if (cRam003e6e08 != '\0') {
      return cRam003e6e08 < '\x02';
    }
    lVar1 = func_0x003b6290(param_2);
    if (lVar1 != 1) {
      return false;
    }
    cRam003e6e08 = 1;
  }
  return false;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00034e58(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f51d0);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_00034ed0(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f51d8);
    uVar1 = 0x3e0000;
    if (cRam003e6e09 == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6e09 = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6e09 == '\x01') {
        uRam00c0853c = 0x79;
        uRam00c08542 = 0x79;
        uRam00c00a26 = 9;
        uRam00c08540 = 1;
        uRam00c00b7c = 100;
        uRam00c08558 = 3;
        func_0x00389580(0x3e2c80,0x79);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c084fc = 3;
        uRam00c08502 = 3;
        uRam00c08504 = 4;
        uRam00c08500 = 2;
        uRam00c0851a = 3;
        uRam00c08518 = 3;
        func_0x00389580(0x3e2a38,3);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00396e38(0x3e3008,0,0,1,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        cRam003e6e09 = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_000350b8(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f51e0);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



undefined8 FUN_00035130(short param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f51e8);
    uVar1 = 0x3e0000;
    if (cRam003e6e0a == '\0') {
      lVar2 = func_0x003b6290(param_2);
      if (lVar2 == 1) {
        cRam003e6e0a = '\x01';
      }
      uVar1 = 0;
    }
    else {
      if (cRam003e6e0a == '\x01') {
        uRam00c0853c = 0x8d;
        uRam00c08542 = 0x8d;
        uRam00c08540 = 1;
        uRam00c08558 = 3;
        uRam00c00a4e = 0;
        uRam00c00ba4 = 0;
        uRam00c047c2 = 0;
        func_0x00389580(0x3e2c80,0x8d);
        func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
        func_0x00396a98(0x42,0x1079000,uRam00c08542);
        uRam00c084fc = 0;
        uRam00c08502 = 0;
        uRam00c08504 = 2;
        uRam00c08506 = 6;
        uRam00c08500 = 3;
        uRam00c08518 = 3;
        uRam00c0851a = 3;
        uRam00c0851c = 3;
        func_0x00389580(0x3e2a38,0);
        func_0x00389580(0x3e2a72,(undefined)uRam00c08504);
        func_0x00389580(0x3e2aac,(undefined)uRam00c08506);
        func_0x00396e38(0x3e3008,0,0,2,0x7fff,0);
        func_0x00396a98(0x42,0x1073600,uRam00c08502);
        func_0x00396a98(0x42,0x1073f00,uRam00c08504);
        func_0x00396a98(0x42,0x1074800,uRam00c08506);
        cRam003e6e0a = '\0';
        uRam003e2a30 = 0;
        uRam003e2f5a = 0;
        uVar1 = 1;
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

bool FUN_00035350(short param_1)

{
  bool bVar1;
  
  if (param_1 == 0) {
    bVar1 = sRam00c0f102 == 0;
  }
  else {
    bVar1 = true;
    if (param_1 == 1) {
      func_0x003b5eb0(0x3f51f0);
      uRam003e2f5c = 0;
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
  }
  return bVar1;
}



ulong FUN_000353c8(short param_1,undefined8 param_2)

{
  ulong uVar1;
  long lVar2;
  
  uVar1 = 1;
  if ((param_1 != 0) && (param_1 == 1)) {
    func_0x003b5eb0(0x3f51f8);
    uVar1 = (ulong)(cRam003e6e0b < '\x02');
    if (cRam003e6e0b == '\x01') {
      uRam00c0853c = 0x7e;
      uRam00c08542 = 0x7e;
      uRam00c08558 = 3;
      uRam00c08540 = 1;
      func_0x00389580(0x3e2c80,0x7e);
      func_0x00396e38(0x3e3018,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1079000,uRam00c08542);
      uRam00c084fc = 7;
      uRam00c08502 = 7;
      uRam00c08518 = 3;
      uRam00c08500 = 1;
      func_0x00389580(0x3e2a38,7);
      func_0x00396e38(0x3e3008,0,0,0,0x7fff,0);
      func_0x00396a98(0x42,0x1073600,uRam00c08502);
      func_0x00396420();
      uRam003e2a30 = 1;
      uVar1 = 0;
      cRam003e6e0b = '\x02';
    }
    else {
      if (uVar1 == 0) {
        uVar1 = 2;
        if ((cRam003e6e0b == '\x02') && (uVar1 = func_0x003b6428(), uVar1 != 0)) {
          cRam003e6e0b = '\0';
          uRam003ecf72 = 0;
          uVar1 = 1;
        }
      }
      else {
        if (cRam003e6e0b == '\0') {
          lVar2 = func_0x003b6290(param_2);
          if (lVar2 == 1) {
            cRam003e6e0b = '\x01';
          }
          uVar1 = 0;
        }
      }
    }
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

void FUN_000355b0(void)

{
  long lVar1;
  
  lVar1 = (*pcRam00c000bc)();
  if (lVar1 == 1) {
    func_0x00382ad8(0x10,0x40);
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  return;
}



// WARNING: Removing unreachable block (ram,0x000357a4)

undefined4 FUN_000356b0(uint param_1,long param_2)

{
  long lVar1;
  byte *pbVar2;
  
  lVar1 = (*pcRam00c000bc)();
  if (lVar1 == 1) {
    func_0x00382ad8(0x10);
    func_0x00382b60();
  }
  if (sRam003e6e50 == 1) {
    if ((*(byte *)((uint)bRam003ed441 * 0x82c + 0x3ed47c) & 2) == 0) {
      sRam003e6e50 = 2;
      if (param_2 == 0) {
        sRam003e6e50 = 0;
        return 1;
      }
      pbVar2 = (byte *)((uint)bRam003ed441 * 0x82c + 0x3ed480);
      *pbVar2 = *pbVar2 & 0xf1 | 4;
      *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed492) =
           *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed48a);
      *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed494) =
           *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed48c);
    }
  }
  else {
    if (sRam003e6e50 < 2) {
      if (sRam003e6e50 == 0) {
        if (param_2 == 0) {
          func_0x003bba50(1,0,0x3c,0xa0,0x3c,0xa0);
        }
        else {
          func_0x003bba50(1,0,0xfffffffffffffed4,0xa0,400,0xa0);
        }
        uRam003e6e52 = func_0x00396fb8(0x45,(param_1 & 0xffff) / 100 & 0xff,(param_1 & 0xffff) % 100
                                       ,0);
        sRam003e6e50 = sRam003e6e50 + 1;
      }
    }
    else {
      if ((sRam003e6e50 == 2) && ((*(byte *)((uint)bRam003ed441 * 0x82c + 0x3ed480) & 8) != 0)) {
        sRam003e6e50 = 0;
        return 1;
      }
    }
  }
  lVar1 = func_0x00124e80();
  if (lVar1 == 0) {
    func_0x003bbc08(uRam003e6e52);
  }
  return 0;
}



// WARNING: Control flow encountered bad instruction data

void FUN_000358c0(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00035990(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



undefined8 FUN_00035a08(void)

{
  if (sRam003e6e54 == 0) {
    if ((~(uRam0020d12c & 0x1ff) + 1 & 0xff) == 0) {
      func_0x003b62a8();
      return 0;
    }
    func_0x003979d8(1,0x10c,8);
    func_0x003b62a8();
    sRam003e6e54 = sRam003e6e54 + 1;
  }
  else {
    if (sRam003e6e54 != 1) {
      return 1;
    }
    if (sRam003ed41c < sRam003ed418) {
      func_0x00147f88(0,uRam003ed420,0,0,0);
      sRam003e6e54 = 0;
      return 0;
    }
    func_0x001481b0(0,sRam003ed418,sRam003ed418,sRam003ed418);
    sRam003ed41c = sRam003ed41c - sRam003ed418;
  }
  return 1;
}



undefined8 FUN_00035b28(void)

{
  int iVar1;
  
  if (sRam003e6e54 == 0) {
    if ((~(uRam0020d12c & 0x1ff) + 1 & 0xff) == 0xff) {
      func_0x003b62d8();
      return 0;
    }
    func_0x003979d8(0xffffffffffffffff,0x10c,8);
    sRam003e6e54 = sRam003e6e54 + 1;
  }
  else {
    if (sRam003e6e54 != 1) {
      return 1;
    }
    if (sRam003ed41c < (short)uRam003ed418) {
      func_0x00147f88(0,uRam003ed420,0xffffffffffffff01,0xffffffffffffff01,0xffffffffffffff01);
      func_0x003b62d8();
      sRam003e6e54 = 0;
      return 0;
    }
    iVar1 = (int)((uint)uRam003ed418 * -0x10000) >> 0x10;
    func_0x001481b0(0,iVar1,iVar1,iVar1);
    sRam003ed41c = sRam003ed41c - uRam003ed418;
  }
  return 1;
}



void FUN_00035c60(void)

{
  undefined4 uVar1;
  uint uVar2;
  uint uVar3;
  undefined auStack336 [8];
  undefined local_148;
  undefined local_147;
  undefined local_146;
  undefined local_145;
  undefined local_144;
  undefined local_143;
  undefined local_142;
  undefined local_b0 [4];
  undefined local_ac;
  undefined local_ab;
  undefined local_aa;
  undefined local_a9;
  undefined4 local_a4 [33];
  
  func_0x00143ed0(auStack336);
  local_148 = 0;
  local_147 = 0;
  local_146 = 0;
  local_145 = 0;
  local_144 = 0;
  local_143 = 0;
  local_142 = 0;
  func_0x00143f50(auStack336);
  func_0x00143ef8();
  local_aa = 1;
  local_b0[0] = 1;
  local_ab = 3;
  local_ac = 0;
  uVar1 = *(undefined4 *)(cRam003e6e58 * 4 + 0x3e6e60);
  local_a9 = 2;
  uVar2 = 0;
  do {
    uVar3 = uVar2 + 1 & 0xff;
    local_a4[uVar2] = uVar1;
    uVar2 = uVar3;
  } while (uVar3 < 0x10);
  func_0x001445e0(8,local_b0);
  func_0x00144e48(cRam003e6e58 * 0x10 + 0x3e6e70);
  func_0x001475c0(4,0);
  func_0x001475c0(8,4);
  func_0x003b62d8();
  func_0x001485c8();
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00035d68(void)

{
  func_0x00396918();
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



undefined8 FUN_00035eb8(undefined2 param_1,ulong param_2,int param_3)

{
  undefined8 uVar1;
  
  if (uRam003e6e56 < 7) {
    uRam0040108c = param_1;
                    // WARNING: Could not emulate address calculation at 0x00035f00
                    // WARNING: Treating indirect jump as call
    uVar1 = (**(code **)((short)uRam003e6e56 * 4 + 0x3f5220))
                      (param_2 & 0xffff,param_2,param_3 << 0x10);
    return uVar1;
  }
  uRam0040108c = param_1;
  return 1;
}



void FUN_000361a0(long param_1,undefined8 param_2,short param_3)

{
  int iVar1;
  long lVar2;
  
  lVar2 = func_0x003e13d0();
  if (param_1 == 0x427e) {
    iVar1 = 0x3e2a38;
  }
  else {
    iVar1 = 0x3e2c80;
  }
  if (-1 < lVar2) {
    iVar1 = (int)lVar2 * 0x3a + iVar1;
    *(short *)(iVar1 + 0x2e) = param_3 + *(short *)(iVar1 + 0x2e);
  }
  return;
}



void FUN_00036220(long param_1,undefined8 param_2,short param_3)

{
  long lVar1;
  int iVar2;
  
  lVar1 = func_0x003e13d0();
  if (param_1 == 0x427e) {
    iVar2 = 0x3e2a38;
  }
  else {
    iVar2 = 0x3e2c80;
  }
  if (-1 < lVar1) {
    iVar2 = (int)lVar1 * 0x3a + iVar2;
    *(short *)(iVar2 + 0x30) = param_3 + *(short *)(iVar2 + 0x30);
    *(short *)(iVar2 + 0x32) = param_3 + *(short *)(iVar2 + 0x32);
    *(short *)(iVar2 + 0x38) = param_3 + *(short *)(iVar2 + 0x38);
    *(short *)(iVar2 + 0x34) = param_3 + *(short *)(iVar2 + 0x34);
    *(short *)(iVar2 + 0x36) = param_3 + *(short *)(iVar2 + 0x36);
  }
  return;
}



void FUN_000362d0(long param_1,undefined8 param_2,undefined2 param_3)

{
  long lVar1;
  int iVar2;
  
  lVar1 = func_0x003e13d0();
  if (param_1 == 0x427e) {
    iVar2 = 0x3e2a38;
  }
  else {
    iVar2 = 0x3e2c80;
  }
  if (-1 < lVar1) {
    *(undefined2 *)((int)lVar1 * 0x3a + iVar2 + 4) = param_3;
  }
  return;
}



void FUN_00036348(long param_1,undefined8 param_2,undefined2 param_3)

{
  int iVar1;
  long lVar2;
  
  lVar2 = func_0x003e13d0();
  if (param_1 == 0x427e) {
    iVar1 = 0x3e2a38;
  }
  else {
    iVar1 = 0x3e2c80;
  }
  iVar1 = (int)lVar2 * 0x3a + iVar1;
  if ((-1 < lVar2) && (*(short *)(iVar1 + 2) != 1)) {
    *(undefined2 *)(iVar1 + 0x10) = param_3;
    *(undefined2 *)(iVar1 + 2) = 6;
  }
  return;
}



undefined4 FUN_000363d0(long param_1,undefined2 param_2)

{
  undefined4 uVar1;
  long lVar2;
  int iVar3;
  int iVar4;
  
  lVar2 = func_0x003e13d0(param_1,param_2);
  uVar1 = 0;
  iVar3 = 0x429e;
  if (param_1 != 0x427e) {
    iVar3 = 0x427e;
  }
  if (param_1 == 0x427e) {
    iVar4 = 0x3e2a38;
  }
  else {
    iVar4 = 0x3e2c80;
  }
  iVar4 = (int)lVar2 * 0x3a + iVar4;
  if (((-1 < lVar2) && (uVar1 = 0, *(short *)(iVar4 + 2) == 4)) &&
     (uVar1 = 1,
     (ulong)*(ushort *)((uint)*(ushort *)(iVar3 * 2 + 0xc00000) * 2 + 0xc00286) !=
     (long)*(short *)(iVar4 + 4))) {
    uVar1 = 0;
  }
  return uVar1;
}



ushort FUN_00036488(undefined param_1)

{
  short sVar1;
  short sVar2;
  int iVar3;
  ushort uVar4;
  short *psVar5;
  
  iVar3 = 1;
  uVar4 = 0;
  psVar5 = (short *)0x3e825a;
  do {
    sVar2 = func_0x003977c0(iVar3 - 1U & 0xff,param_1);
    sVar1 = *psVar5;
    psVar5 = psVar5 + 1;
    iVar3 = (iVar3 + 1) * 0x1000000 >> 0x18;
    if (uVar4 < (ushort)(sVar1 * sVar2)) {
      uVar4 = sVar1 * sVar2;
    }
  } while (iVar3 < 0xb);
  return uVar4;
}



void FUN_00036518(long param_1,undefined2 param_2)

{
  undefined2 uVar1;
  long lVar2;
  int iVar3;
  
  lVar2 = func_0x003e13d0(param_1,param_2);
  if (param_1 == 0x427e) {
    iVar3 = 0x3e2a38;
  }
  else {
    iVar3 = 0x3e2c80;
  }
  if (-1 < lVar2) {
    uVar1 = func_0x003b6d88(param_2);
    *(undefined2 *)((int)lVar2 * 0x3a + iVar3 + 8) = uVar1;
  }
  return;
}



void FUN_000365a8(long param_1,undefined8 param_2,undefined2 param_3)

{
  long lVar1;
  int iVar2;
  
  lVar1 = func_0x003e13d0();
  if (param_1 == 0x427e) {
    iVar2 = 0x3e2a38;
  }
  else {
    iVar2 = 0x3e2c80;
  }
  if (-1 < lVar1) {
    *(undefined2 *)((int)lVar1 * 0x3a + iVar2 + 8) = param_3;
  }
  return;
}



bool FUN_00036620(undefined8 param_1,int param_2,char param_3)

{
  long lVar1;
  
  if (cRam003e6e9b != '\x01') {
    if (cRam003e6e9b >= '\x02') {
      if (cRam003e6e9b != '\x02') {
        if (cRam003e6e9b == '\x03') {
          cRam003e6e9a = 0;
          cRam003e6e9b = 0;
          return true;
        }
        return true;
      }
      lVar1 = func_0x003b6200(*(undefined2 *)(cRam003e6e9a * 4 + param_2 + 2));
      if (lVar1 == 1) {
        cRam003e6e9a = cRam003e6e9a + '\x01';
        cRam003e6e9b = 1;
        return false;
      }
      return false;
    }
    if (cRam003e6e9b != '\0') {
      return cRam003e6e9b < '\x02';
    }
    cRam003e6e9b = '\x01';
    cRam003e6e9a = '\0';
  }
  do {
    if ((long)(int)param_3 <= (long)cRam003e6e9a) {
LAB_000366f4:
      if ((long)(int)param_3 == (long)(int)cRam003e6e9a) {
        cRam003e6e9b = 3;
        return false;
      }
      return false;
    }
    lVar1 = func_0x003b69d0(param_1,*(undefined2 *)(cRam003e6e9a * 4 + param_2));
    if (lVar1 != 0) {
      cRam003e6e9b = '\x02';
      goto LAB_000366f4;
    }
    cRam003e6e9a = cRam003e6e9a + '\x01';
  } while( true );
}



void FUN_00036830(undefined2 param_1,undefined2 param_2)

{
  func_0x003b7088(param_1,0x427e,0x3e3008);
  func_0x003b7088(param_2,0x429e,0x3e3018);
  uRam003e2f5b = 1;
  return;
}



void FUN_00036888(undefined8 param_1,undefined2 param_2)

{
  long lVar1;
  ushort *puVar2;
  
  lVar1 = func_0x003e13d0(param_1,param_2);
  if (-1 < lVar1) {
    puVar2 = (ushort *)(((int)param_1 + (int)lVar1 + 0xe) * 2 + 0xc00000);
    *puVar2 = *puVar2 & 0xfffc;
  }
  return;
}



void FUN_000368d8(undefined8 param_1,short param_2)

{
  short *psVar1;
  int iVar2;
  uint uVar3;
  
  iVar2 = 0;
  uVar3 = (uint)*(ushort *)((int)param_1 * 2 + 0xc00004);
  if (uVar3 != 0) {
    psVar1 = (short *)(((int)param_1 + 3) * 2 + 0xc00000);
    do {
      if (param_2 != *psVar1) {
        func_0x003b7188(param_1,*psVar1);
      }
      iVar2 = (iVar2 + 1) * 0x1000000 >> 0x18;
      psVar1 = psVar1 + 1;
    } while (iVar2 < (int)uVar3);
  }
  return;
}



uint FUN_00036ab8(int param_1,uint param_2,uint param_3)

{
  uint uVar1;
  uint uVar2;
  
  uVar1 = param_3 & 0xffff;
  uVar2 = param_2 & 0xffff;
  if ((uVar2 != 0) && (uVar1 + 1 <= uVar2)) {
    uVar2 = uVar2 + 0xffff & 0xffff;
    if (uVar1 < uVar2) {
      func_0x001873cc(param_1 + uVar1 * 0x900,param_1 + uVar1 * 0x900 + 0x900,
                      (uVar2 - uVar1) * 0x900);
    }
  }
  return uVar2;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x00036bc0: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00036bc8)

void FUN_00036bb0(void)

{
  uRam003e2f5a = 0;
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x00036c50: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00036c58)

void FUN_00036c48(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_00036d20(long param_1,undefined2 param_2,uint param_3,uint param_4)

{
  int iVar1;
  long lVar2;
  ushort *puVar3;
  uint uVar4;
  
  lVar2 = func_0x003e13d0(param_1,param_2);
  uVar4 = (param_3 & 0xff) + 0xff;
  if (param_1 == 0x427e) {
    iVar1 = 0x3e2a38;
  }
  else {
    iVar1 = 0x3e2c80;
  }
  iVar1 = iVar1 + (int)lVar2 * 0x3a;
  if (-1 < lVar2) {
    if ((uVar4 & 1) == 0) {
      puVar3 = (ushort *)((uVar4 & 0xfe) + iVar1 + 0x14);
      *puVar3 = (ushort)*(byte *)puVar3 | (ushort)((param_4 & 0xff) << 8);
    }
    else {
      puVar3 = (ushort *)((uVar4 & 0xfe) + iVar1 + 0x14);
      *puVar3 = *puVar3 & 0xff00 | (ushort)(param_4 & 0xff);
    }
  }
  return;
}



byte * FUN_00036df8(byte *param_1)

{
  int *piVar1;
  short sVar2;
  long lVar3;
  short *psVar4;
  byte *pbVar5;
  int iVar6;
  int iVar7;
  int iVar8;
  
  iVar8 = 0;
  if (*(int *)(param_1 + 0x20) != 0) {
    iVar7 = 0;
    pbVar5 = param_1;
    do {
      iVar6 = 0;
      sVar2 = 0;
      psVar4 = (short *)(param_1 + (iVar7 + iVar8) * 8 + 0x12);
      do {
        if ((*psVar4 != 0) && (lVar3 = func_0x003b5f10(*psVar4), lVar3 == 1)) {
          iVar6 = -1;
          break;
        }
        sVar2 = sVar2 + 1;
        psVar4 = psVar4 + 1;
      } while (sVar2 < 4);
      if (iVar6 == 0) {
        sVar2 = 0;
        psVar4 = (short *)(param_1 + (iVar7 + iVar8) * 8);
        do {
          psVar4 = psVar4 + 1;
          if (*psVar4 != 0) {
            lVar3 = func_0x003b5f10(*psVar4);
            if (lVar3 == 1) {
              iVar6 = (iVar6 + 1) * 0x10000 >> 0x10;
            }
          }
          sVar2 = sVar2 + 1;
        } while (sVar2 < 8);
        if (((int)(uint)*pbVar5 <= iVar6) &&
           (lVar3 = (**(code **)(pbVar5 + 0x20))(0,pbVar5), lVar3 == 1)) {
          return pbVar5;
        }
      }
      piVar1 = (int *)(pbVar5 + 0x48);
      iVar8 = (iVar8 + 1) * 0x10000 >> 0x10;
      iVar7 = iVar7 + 4;
      pbVar5 = pbVar5 + 0x28;
    } while (*piVar1 != 0);
  }
  return (byte *)0x0;
}



void FUN_00036f80(undefined8 param_1)

{
  long lVar1;
  int iVar2;
  
  uRam003e6e50 = 0;
  uRam003e6e52 = 0;
  uRam003e6e54 = 0;
  uRam003e6e56 = 0;
  func_0x00383000(7);
  lVar1 = func_0x003b76f8(param_1);
  iVar2 = (int)lVar1;
  if (lVar1 == 0) {
    cRam003e6e68 = '\0';
  }
  else {
    cRam003e6e68 = '\x04';
    lVar1 = func_0x003b5f10(0xe7);
    if ((lVar1 != 0) && (lVar1 = func_0x003b5f10(0x385), lVar1 == 0)) {
      (*pcRam00c00038)(0x1e);
    }
    (*pcRam00c00044)(0,*(undefined *)(iVar2 + 0x24));
    uRam003e2f5e = *(undefined *)(iVar2 + 0x24);
  }
  while (cRam003e6e68 != '\0') {
    func_0x003839a8();
    func_0x00384000(0,0x13f,0xef);
    func_0x003840f0(0,0,0);
    func_0x00384078(0,0,0,0x13f,0xef);
    func_0x003978c0(0,0);
    func_0x00382ab8(7,0);
    lVar1 = (**(code **)(iVar2 + 0x20))(1);
    if (lVar1 == 1) {
      cRam003e6e68 = '\0';
      func_0x003b5f40(*(undefined2 *)(iVar2 + 0x1a));
    }
    func_0x00382a18(2);
    func_0x00383c00();
    uRam003ecf64 = 1;
    func_0x001485c8();
    uRam003ecf68 = 1;
    lVar1 = (*pcRam00c0005c)(uRam002afcb0);
    if (lVar1 == 1) {
      uRam003ecf4a = 1;
      cRam003e6e68 = '\0';
    }
  }
  bRam003ed47c = bRam003ed47c & 0xfe;
  bRam003edca8 = bRam003edca8 & 0xfe;
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00037190(void)

{
  long lVar1;
  
  lVar1 = (*pcRam00c000cc)();
  if (lVar1 == 1) {
    uRam003e6e58 = 0;
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_000371e8(void)

{
  long lVar1;
  
  lVar1 = (*pcRam00c000cc)();
  if (lVar1 == 1) {
    uRam003e6e58 = 0;
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00037240(undefined2 *param_1,ushort param_2,ushort param_3,undefined param_4,short param_5,
                 short param_6,undefined2 param_7,undefined param_8)

{
  int iVar1;
  int iVar2;
  int iVar3;
  int iVar4;
  
  iVar1 = (param_5 + 2) * 0x10000 >> 0x10;
  iVar2 = (param_6 + 2) * 0x10000 >> 0x10;
  iVar3 = ((((int)((uint)param_2 << 0x10) >> 0x10) - ((int)((uint)param_2 << 0x10) >> 0x1f) >> 1) +
          4) * 0x10000 >> 0x10;
  iVar4 = ((((int)((uint)param_3 << 0x10) >> 0x10) - ((int)((uint)param_3 << 0x10) >> 0x1f) >> 1) +
          4) * 0x10000 >> 0x10;
  if (iRam003ecf38 == 0) {
    func_0x003978c0(param_4,iVar1,iVar2,iVar3,iVar4,0xffffffffffffffff,param_7,param_8);
  }
  else {
    func_0x003978c0(param_4,iVar1,iVar2,iVar3,iVar4,0xffffffffffffffff,param_7,0);
  }
  param_1[6] = param_5;
  param_1[7] = param_6;
  param_1[8] = param_2;
  param_1[9] = param_3;
  *param_1 = 0xc1;
  param_1[4] = 0x2a10;
  param_1[5] = 0xc08;
  param_1[0xd] = 1;
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



undefined8 FUN_00038bb8(undefined2 param_1,undefined2 param_2)

{
  long lVar1;
  undefined8 uVar2;
  
  lVar1 = func_0x003deba0(param_1,param_2);
  uVar2 = 3;
  if (lVar1 == 1) {
    uVar2 = 1;
  }
  return uVar2;
}



undefined8
FUN_00038be8(undefined2 param_1,uint param_2,undefined2 param_3,long param_4,short param_5,
            ushort param_6)

{
  ushort uVar1;
  undefined8 uVar2;
  
  uVar1 = *(ushort *)((param_2 & 0xffff) * 2 + 0xc007de);
  if (((uVar1 & 0x8000) == 0) && (*(short *)((uint)uVar1 * 2 + 0xc04c00) == 0x13))
  goto LAB_00038ca0;
  if (((param_4 == 0) && (param_5 < 0x3c)) && ((param_5 < 0x28 || (0x3b < param_6)))) {
    if (param_5 < 0x14) {
      if (param_6 < 10) goto LAB_00038c90;
    }
    else {
      if (param_6 < 0x1e) goto LAB_00038c90;
    }
LAB_00038ca0:
    uVar2 = 0;
  }
  else {
LAB_00038c90:
    uVar2 = func_0x003b94b8(param_1,param_3);
  }
  return uVar2;
}



undefined8
FUN_00038cb0(undefined2 param_1,undefined2 param_2,undefined2 param_3,long param_4,short param_5,
            ushort param_6)

{
  undefined8 uVar1;
  
  if (sRam00c0f200 == 0) {
    uVar1 = func_0x003b94e8(param_1,param_2);
  }
  else {
    if (param_4 == 0) {
      if ((param_5 < 0x3c) && ((param_5 < 0x28 || (0x3b < param_6)))) {
        if (param_5 < 0x14) {
          if (9 < param_6) {
            return 0;
          }
        }
        else {
          if (0x1d < param_6) {
            return 0;
          }
        }
      }
      uVar1 = func_0x003b93d0(param_3,2);
    }
    else {
      uVar1 = func_0x003b93d0(param_3,3);
    }
  }
  return uVar1;
}



undefined8
FUN_00038d78(undefined2 param_1,uint param_2,undefined2 param_3,long param_4,short param_5,
            ushort param_6)

{
  ushort uVar1;
  undefined8 uVar2;
  
  if (sRam00c0f200 == 0) {
    uVar2 = func_0x003b94e8(param_1);
  }
  else {
    uVar1 = *(ushort *)((param_2 & 0xffff) * 2 + 0xc007de);
    if (((uVar1 & 0x8000) != 0) || (uVar2 = 0, *(short *)((uint)uVar1 * 2 + 0xc04c00) != 0x13)) {
      if (((param_4 == 0) && (param_5 < 0x3c)) && ((param_5 < 0x28 || (0x3b < param_6)))) {
        if (param_5 < 0x14) {
          if (9 < param_6) {
            return 0;
          }
        }
        else {
          if (0x1d < param_6) {
            return 0;
          }
        }
      }
      uVar2 = func_0x003b94b8(param_1,param_3);
    }
  }
  return uVar2;
}



long FUN_00038e60(void)

{
  ushort uVar1;
  undefined2 uVar2;
  undefined2 uVar3;
  bool bVar4;
  short sVar5;
  int iVar6;
  long lVar7;
  ushort uVar8;
  int iVar9;
  int iVar10;
  int iVar11;
  int iVar12;
  
  if (sRam00c0f200 == 0) {
    iVar10 = 0x120;
    iVar11 = (sRam003e3008 * 0x1c + (int)sRam003e3008) * 2 + 0x3e2a38;
    iVar12 = (sRam003e3018 * 0x1c + (int)sRam003e3018) * 2 + 0x3e2c80;
    iVar6 = 0x130;
  }
  else {
    iVar10 = 0x130;
    iVar6 = 0x120;
    iVar11 = (sRam003e3018 * 0x1c + (int)sRam003e3018) * 2 + 0x3e2c80;
    iVar12 = (sRam003e3008 * 0x1c + (int)sRam003e3008) * 2 + 0x3e2a38;
  }
  iVar6 = iVar6 * 2;
  iVar10 = iVar10 * 2;
  iVar9 = (uint)*(ushort *)(iVar6 + 0xc0f00a) << 0x10;
  uVar1 = *(ushort *)(iVar6 + 0xc0f000);
  uVar2 = *(undefined2 *)(iVar6 + 0xc0f002);
  uVar3 = *(undefined2 *)(iVar10 + 0xc0f000);
  if ((long)*(short *)(iVar6 + 0xc0f008) < (long)(iVar9 >> 0x10)) {
    iVar9 = (uint)*(ushort *)(iVar6 + 0xc0f008) << 0x10;
    *(ushort *)(iVar12 + 8) = *(ushort *)(iVar6 + 0xc0f008);
  }
  else {
    *(ushort *)(iVar12 + 8) = *(ushort *)(iVar6 + 0xc0f00a);
  }
  iVar6 = (uint)*(ushort *)(iVar10 + 0xc0f00a) << 0x10;
  uVar8 = *(ushort *)(iVar10 + 0xc0f00a);
  if ((long)*(short *)(iVar10 + 0xc0f008) < (long)(iVar6 >> 0x10)) {
    iVar6 = (uint)*(ushort *)(iVar10 + 0xc0f008) << 0x10;
    uVar8 = *(ushort *)(iVar10 + 0xc0f008);
  }
  *(ushort *)(iVar11 + 8) = uVar8;
  lVar7 = func_0x001251e8();
  if (lVar7 == 0) {
    lVar7 = func_0x00125258();
    if (lVar7 == 0) {
      if (((uVar1 != uRam00c00110) &&
          (((uVar8 = *(ushort *)((uint)uVar1 * 2 + 0xc007de), (uVar8 & 0x8000) != 0 ||
            (*(short *)((uint)uVar8 * 2 + 0xc04c00) != 0x13)) &&
           (lVar7 = func_0x003de970(uVar1), lVar7 != 0)))) && (lVar7 = 0, sRam00c0f202 != 0)) {
        bVar4 = sRam00c0f202 == 2;
        if (bVar4) {
          sVar5 = *(short *)(iVar12 + 0xc);
        }
        else {
          if (iVar9 >> 0x10 != 0) {
            return 0;
          }
          sVar5 = *(short *)(iVar12 + 0xc);
        }
        if (sVar5 < 0xff) {
          lVar7 = func_0x003b9678(*(undefined2 *)(iVar11 + 4),uVar3,uVar1,bVar4,iVar6 >> 0x10,uVar2)
          ;
        }
        else {
          lVar7 = func_0x003b95b0(*(undefined2 *)(iVar11 + 4),uVar3,uVar1,bVar4,iVar6 >> 0x10,uVar2)
          ;
        }
      }
    }
    else {
      lVar7 = 0;
    }
  }
  else {
    lVar7 = 0;
  }
  return lVar7;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x00039338: Changing call to branch
// WARNING: Possible PIC construction at 0x00039cf0: Changing call to branch
// WARNING: Possible PIC construction at 0x0003a32c: Changing call to branch
// WARNING: Possible PIC construction at 0x0003a3e8: Changing call to branch
// WARNING: Possible PIC construction at 0x0003942c: Changing call to branch
// WARNING: Removing unreachable block (ram,0x0003a334)
// WARNING: Removing unreachable block (ram,0x00039cf8)
// WARNING: Removing unreachable block (ram,0x00039434)
// WARNING: Restarted to delay deadcode elimination for space: stack

void FUN_00039108(uint param_1,ushort param_2,short param_3,short param_4,int param_5,int param_6,
                 int param_7,short param_8,ushort *param_9,char param_10)

{
  ushort uVar1;
  short sVar2;
  short sVar3;
  short sVar4;
  short sVar5;
  ushort uVar6;
  int iVar7;
  undefined4 *puVar8;
  code *pcVar9;
  undefined8 uVar10;
  undefined8 uVar11;
  long lVar12;
  short sVar13;
  undefined2 uVar14;
  int iVar15;
  uint uVar16;
  uint uVar17;
  uint uVar18;
  int iVar19;
  uint uVar20;
  ushort uVar21;
  undefined8 uVar22;
  byte bVar23;
  short sVar24;
  int iVar25;
  short *psVar26;
  int iVar27;
  int iVar28;
  short sVar29;
  undefined4 uVar30;
  short sVar31;
  short *psVar32;
  int iVar33;
  uint uVar34;
  int local_6c;
  int local_58;
  int local_54;
  
  uVar17 = param_1 & 0xff;
  iVar25 = (int)param_3;
  iVar27 = (int)param_4;
  iVar28 = (int)param_8;
  if ((*(byte *)(param_9 + 4) & 2) == 0) {
    uVar21 = *param_9;
    uVar6 = param_9[1];
  }
  else {
    sVar2 = 0x72;
    sVar3 = -0xc;
    sVar4 = 0x72;
    sVar5 = 0x30;
    if (param_2 == 0x20) {
LAB_0003928c:
      sVar29 = sVar5;
      sVar31 = sVar4;
      sVar24 = sVar3;
      sVar13 = sVar2;
      uVar21 = *param_9;
    }
    else {
      if (0x20 < param_2) {
        if (param_2 == 0x80) {
LAB_00039250:
          sVar2 = 0x72;
          sVar3 = -0xc;
          sVar4 = 0x72;
          sVar5 = 0x74;
        }
        else {
          if (param_2 < 0x81) {
            sVar2 = 0x72;
            sVar3 = -10;
            sVar4 = 0x72;
            sVar5 = 0x32;
            if (param_2 == 0x40) goto LAB_0003928c;
          }
          else {
            if (param_2 == 0x100) goto LAB_00039250;
            if (param_2 == 0x200) goto LAB_0003926c;
          }
LAB_00039288:
          sVar2 = 0x140;
          sVar3 = 0xf0;
          sVar4 = 0x140;
          sVar5 = 0xf0;
        }
        goto LAB_0003928c;
      }
      if (param_2 == 4) {
LAB_0003926c:
        sVar2 = 0x30;
        sVar3 = -0xc;
        sVar4 = 0x30;
        sVar5 = 0x76;
        goto LAB_0003928c;
      }
      if (4 < param_2) {
        sVar5 = 0x2e;
        if (param_2 != 8) {
          sVar2 = 0x140;
          sVar3 = -8;
          sVar4 = 0x140;
          sVar5 = 0xb0;
          if (param_2 != 0x10) goto LAB_00039288;
        }
        goto LAB_0003928c;
      }
      sVar13 = 0x140;
      sVar31 = 0x140;
      sVar24 = 0xf0;
      sVar29 = 0xf0;
      sVar2 = 0x140;
      sVar3 = 0xf0;
      sVar4 = 0x140;
      sVar5 = 0xf0;
      if (param_2 == 1) goto LAB_0003928c;
      uVar21 = *param_9;
    }
    if ((uVar21 == 0) && (param_10 != '\x01')) {
      uVar1 = param_9[1];
    }
    else {
      sRam003ecfc4 = param_3 + sVar13;
      sRam003ecfc6 = param_4 + sVar24;
      func_0x00383a90(uVar17 + 1 & 0xff,0x3ecfb8);
      uVar21 = *param_9;
      uVar1 = param_9[1];
    }
    uVar6 = param_9[1];
    if (((long)(short)uVar1 < (long)iVar28) && ((int)(short)uVar21 < iVar28 - (short)uVar1)) {
      sRam003ecfe4 = param_3 + sVar31;
      sRam003ecfe6 = param_4 + sVar29;
      return;
    }
  }
  iVar7 = (int)(short)uVar21;
  local_6c = 0;
  if ((0 < (int)((uint)uVar6 << 0x10)) && (0 < iVar28)) {
    psVar26 = (short *)(iVar28 * 2 + param_6);
    uVar34 = uVar17 + 1;
    uVar18 = uVar34 & 0xff;
    iVar15 = iVar28 + -1;
    local_58 = iVar27 + -6;
    local_54 = iVar27 + -2;
    do {
      if (iVar28 <= iVar7) {
        iVar7 = 0;
      }
      iVar33 = iVar7 * 2;
      if ((*(ushort *)(iVar33 + param_7) & 2) == 0) {
        return;
      }
      if ((*(ushort *)(iVar33 + param_7) & 1) == 0) {
        return;
      }
      func_0x00382ab8();
      iVar19 = (iVar25 + 8) * 0x10000 >> 0x10;
      uVar14 = (undefined2)local_58;
      if (param_2 == 0x10) {
        func_0x00382ad8(iVar19,uVar14);
        uVar20 = uVar34 & 0xff;
        uVar16 = (int)*(short *)(iVar33 + param_6) << 2;
LAB_00039938:
        func_0x00382b60(uVar20,1,*(undefined4 *)(uVar16 + param_5));
      }
      else {
        if (param_2 < 0x11) {
          if (param_2 == 2) {
            func_0x00382ad8((iVar25 + 0x10) * 0x10000 >> 0x10,
                            (iVar27 + local_6c * 0x10) * 0x10000 >> 0x10);
            uVar20 = uVar34 & 0xff;
            uVar16 = (int)*(short *)(iVar33 + param_6) << 2;
            goto LAB_00039938;
          }
          if (param_2 < 3) {
            if (param_2 != 1) {
              return;
            }
            lVar12 = func_0x003bae50(7);
            if (lVar12 == 0) {
              func_0x00382ad8(iVar19,(iVar27 + local_6c * 0x10 + -6) * 0x10000 >> 0x10);
            }
            else {
              func_0x00382ad8(iVar19,uVar14);
            }
            puVar8 = (undefined4 *)(*(short *)(iVar33 + param_6) * 4 + param_5);
LAB_00039798:
            func_0x00382b60(uVar34 & 0xff,1,*puVar8);
          }
          else {
            if (param_2 == 4) {
LAB_00039ba0:
              if (iVar7 == iVar15) {
                func_0x00382ad8(iVar19,uVar14);
                sVar2 = *psVar26;
                if (sVar2 == 1) {
                  uVar22 = 0x3fb120;
LAB_00039c5c:
                  func_0x00382b60(uVar34 & 0xff,1,uVar22);
                }
                else {
                  if (sVar2 < 2) {
                    if (sVar2 == 0) {
                      uVar22 = 0x3fb110;
                      goto LAB_00039c5c;
                    }
                  }
                  else {
                    if (sVar2 == 2) {
                      uVar22 = 0x3fb130;
                      goto LAB_00039c5c;
                    }
                  }
                }
              }
              else {
                if (iVar7 == iVar28 + -2) {
                  func_0x00382ad8(iVar19,uVar14);
                  uVar22 = 0x3fb140;
                  goto LAB_00039c5c;
                }
                func_0x00382ad8(iVar19,uVar14);
                func_0x00382b60(uVar18,1,*(undefined4 *)
                                          (*(short *)(iVar33 + param_6) * 4 + 0x3e7ed0));
                func_0x00382ad8((iVar25 + 0x38) * 0x10000 >> 0x10,uVar14);
                uVar22 = func_0x003d0f90(*(short *)(iVar33 + param_6));
                func_0x00382b60(uVar18,1,0x3fb040,uVar22);
              }
            }
            else {
              if (param_2 != 8) {
                return;
              }
              if (iVar7 == iVar15) {
                func_0x00382ad8(iVar19,uVar14);
                uVar22 = 0x3fb028;
                goto LAB_00039c5c;
              }
              psVar32 = (short *)(iVar33 + param_6);
              func_0x00382ad8(iVar19,uVar14);
              func_0x00382b60(uVar18,1,*(undefined4 *)(*psVar32 * 4 + param_5));
              func_0x00382ad8((iVar25 + 100) * 0x10000 >> 0x10,uVar14);
              uVar22 = func_0x003d0e88(0x299,*(undefined2 *)(*psVar32 * 2 + 0xc00532));
              func_0x00382b60(uVar18,1,0x3fb040,uVar22);
              func_0x00382ad8((iVar25 + 0x78) * 0x10000 >> 0x10,uVar14);
              uVar22 = func_0x003d12d8(*psVar32);
              func_0x00382b60(uVar18,1,uVar22);
              func_0x00382ad8((iVar25 + 0xa8) * 0x10000 >> 0x10,uVar14);
              func_0x00382b60(uVar18,1,0x3fb048,*(undefined2 *)(*psVar32 * 2 + 0xc01fea),
                              *(undefined2 *)(*psVar32 * 2 + 0xc02140));
            }
          }
        }
        else {
          if (param_2 == 0x80) {
            func_0x00382ad8(iVar19,uVar14);
            if (iVar7 == iVar15) {
              if (*psVar26 == 0) {
                uVar22 = 0x3fb088;
              }
              else {
                uVar22 = 0x3fb098;
              }
              goto LAB_00039c5c;
            }
            psVar32 = (short *)(iVar33 + param_6);
            lVar12 = func_0x003d0598(*psVar32,uRam00c00110);
            uVar30 = uRam003e6f88;
            if (((lVar12 != 1) &&
                (lVar12 = func_0x003d0558(*psVar32,uRam00c00110), uVar30 = uRam003e6f8c, lVar12 != 1
                )) && (uVar30 = uRam003e6f90, (long)*psVar32 != (ulong)uRam00c00110)) {
              uVar30 = *(undefined4 *)((uint)*(ushort *)(*psVar32 * 2 + 0xc003dc) * 4 + 0x3e8248);
            }
            uVar22 = func_0x001383d0(*(undefined4 *)(*(short *)(iVar33 + param_6) * 4 + param_5));
            uVar10 = func_0x001383d0(uVar30);
            uVar11 = func_0x003d0be8(*(short *)(iVar33 + param_6));
            func_0x00382c20(uVar17 + 1,1,0x3fb0c8,uVar22,uVar10,uVar11);
          }
          else {
            if (param_2 < 0x81) {
              if (param_2 == 0x20) {
                if (iVar7 != iVar15) {
                  func_0x00382ad8(iVar19,uVar14);
                  func_0x00382b60(uVar18,1,*(undefined4 *)
                                            (*(short *)(iVar33 + param_6) * 4 + param_5));
                  func_0x00382ad8((iVar25 + 0x84) * 0x10000 >> 0x10,uVar14);
                  puVar8 = (undefined4 *)
                           ((uint)*(ushort *)(*(short *)(iVar33 + param_6) * 2 + 0xc01d3e) * 4 +
                           0x3e7798);
                  goto LAB_00039798;
                }
              }
              else {
                if (param_2 != 0x40) {
                  return;
                }
                uVar14 = (undefined2)local_54;
                if (iVar7 == iVar28 + -2) {
                  func_0x00382ad8(iVar19,uVar14);
                  uVar22 = 0x3fb068;
                  goto LAB_00039c5c;
                }
                if (iVar7 != iVar15) {
                  func_0x00382ad8(iVar19,uVar14);
                  uVar16 = uVar17 + 1 & 0xff;
                  func_0x00382b60(uVar16,1,*(undefined4 *)
                                            (*(short *)(iVar33 + param_6) * 4 + 0x3e7798));
                  func_0x00382ad8((iVar25 + 0x62) * 0x10000 >> 0x10,uVar14);
                  if ((*(ushort *)(*(short *)(iVar33 + param_6) * 0x1c + 0x3e6fac) & 0x4000) == 0) {
                    func_0x00382b60(uVar16,1,0x3fb080);
                  }
                  else {
                    func_0x00382b60(uVar16,1,0x3fb078);
                  }
                  func_0x00382ad8((iVar25 + 0x74) * 0x10000 >> 0x10,uVar14);
                  uVar20 = uVar17 + 1 & 0xff;
                  uVar16 = *(ushort *)(*(short *)(iVar33 + param_6) * 0x1c + 0x3e6fac) >> 6 & 0xfc;
                  goto LAB_00039938;
                }
              }
              func_0x00382ad8(iVar19,uVar14);
              func_0x00382b60(uVar34 & 0xff,1,0x3fb050);
            }
            else {
              if (param_2 != 0x100) {
                if (param_2 != 0x200) {
                  return;
                }
                goto LAB_00039ba0;
              }
              func_0x00382ad8(iVar19,uVar14);
              if (iVar7 == iVar15) {
                uVar22 = 0x3fb0e0;
                goto LAB_00039c5c;
              }
              psVar32 = (short *)(iVar33 + param_6);
              uVar22 = func_0x001383d0(*(undefined4 *)(*psVar32 * 4 + param_5));
              uVar10 = func_0x001383d0(*(undefined4 *)
                                        ((uint)*(ushort *)(*psVar32 * 2 + 0xc003dc) * 4 + 0x3e8248))
              ;
              uVar11 = func_0x003d0e88(0x299,*(undefined2 *)(*psVar32 * 2 + 0xc00532));
              func_0x00382b60(uVar18,1,0x3fb0f0,uVar22,uVar10,uVar11);
              uVar22 = func_0x003c72d8(0xd49,*psVar32);
              uVar22 = func_0x003d0e88(0xd49,uVar22);
              func_0x00382b60(uVar18,1,0x3fb108,uVar22);
            }
          }
        }
      }
      local_6c = (local_6c + 1) * 0x10000 >> 0x10;
      iVar7 = (iVar7 + 1) * 0x10000 >> 0x10;
      local_58 = local_58 + 0xc;
      local_54 = local_54 + 0xc;
    } while (((long)local_6c < (long)(short)param_9[1]) && ((long)local_6c < (long)iVar28));
  }
  lVar12 = (*pcRam00c000bc)();
  if (lVar12 != 0) {
    if (((uRam002afcb0 & 0x20) != 0) && ((uRam002afcb2 & 0x20) != 0)) {
      bRam003e6f82 = ~bRam003e6f82;
      uRam003e6f84 = param_2;
    }
    if (bRam003e6f82 != 0) {
      uVar22 = 0xa0;
      if ((uRam003e6f84 ^ param_2) != 0) {
        uVar22 = 8;
      }
      func_0x003978c0(6,uVar22,8,0xa0,0x30,0xffffffffffffffff,(uRam003e6f84 ^ param_2) != 0,1);
      func_0x003812f8(6,uVar22,8,0x3fb170,param_9[3],param_9[2],*param_9);
      return;
    }
  }
  bVar23 = *(byte *)(param_9 + 4);
  if ((bVar23 & 1) == 0) {
    if ((bVar23 & 2) == 0) {
      return;
    }
    if (param_2 == 2) {
      sRam003ecfa4 = param_3;
      sRam003ecfa6 = param_4 + (short)((int)(short)param_9[3] << 4);
      return;
    }
    if (param_2 == 0x40) {
      sRam003ecfa4 = param_3 + -4;
      sRam003ecfa6 = param_4 + param_9[3] * 0xc + -2;
      return;
    }
    sRam003ecfa4 = param_3 + -4;
    sRam003ecfa6 = param_4 + param_9[3] * 0xc + -6;
    return;
  }
  if ((uRam002afcb2 & uRam003ecf6c) == 0) {
    if ((uRam002afcb2 & uRam003ecf6e) == 0) {
      if ((uRam002afcb2 & 8) == 0) {
        if ((uRam002afcb2 & 0x80) != 0) {
          if (((long)(short)*param_9 < (long)(iVar28 - (short)param_9[1])) ||
             ((long)(short)param_9[3] < (long)((short)param_9[1] + -1))) {
            (*pcRam00c0004c)(2,2);
            bVar23 = *(byte *)(param_9 + 4);
            *param_9 = param_8 - param_9[1];
            param_9[3] = param_9[1] - 1;
          }
          goto LAB_0003a2f8;
        }
        lVar12 = (long)(short)*param_9;
        uVar21 = param_9[3];
        iVar25 = (int)(short)*param_9;
        if ((uRam002afcba & 0x1000) == 0) {
          iVar27 = (short)uVar21 + 1;
          if ((uRam002afcba & 0x2000) == 0) goto LAB_0003a2bc;
          uVar6 = param_9[3] + 1;
          if ((long)iVar27 != (long)(short)param_9[1]) goto LAB_0003a2b8;
          if ((long)iVar27 < (long)iVar28) {
            if (param_10 == '\x01') {
              param_9[3] = 0;
              iVar25 = 0;
              *param_9 = 0;
              goto LAB_0003a2bc;
            }
            iVar7 = iVar25 << 0x10;
            if (lVar12 < iVar28 - iVar27) {
              iVar25 = iVar25 + 1;
              goto LAB_0003a294;
            }
          }
          else {
            if (param_10 == '\x01') {
              param_9[3] = 0;
              goto LAB_0003a2bc;
            }
            iVar7 = iVar25 << 0x10;
          }
        }
        else {
          uVar6 = param_9[3] - 1;
          if (uVar21 == 0) {
            uVar6 = param_9[1];
            if ((long)(short)param_9[1] < (long)iVar28) {
              if (param_10 == '\x01') {
                param_9[3] = uVar6 + 1;
                iVar25 = iVar28 - (uint)uVar6;
                *param_9 = (ushort)iVar25;
              }
              else {
                if (lVar12 < 1) {
                  iVar7 = iVar25 << 0x10;
                  goto LAB_0003a2c0;
                }
                iVar25 = iVar25 + -1;
LAB_0003a294:
                *param_9 = (ushort)iVar25;
              }
              goto LAB_0003a2bc;
            }
            if (param_10 != '\x01') {
              iVar7 = iVar25 << 0x10;
              goto LAB_0003a2c0;
            }
            uVar6 = uVar6 - 1;
          }
LAB_0003a2b8:
          param_9[3] = uVar6;
LAB_0003a2bc:
          iVar7 = iVar25 << 0x10;
        }
LAB_0003a2c0:
        if ((lVar12 == iVar7 >> 0x10) && (uVar21 == param_9[3])) goto LAB_0003a2f8;
        uVar22 = 2;
        pcVar9 = pcRam00c0004c;
        goto LAB_0003a2e8;
      }
      if (((short)*param_9 < 1) && ((short)param_9[3] < 1)) goto LAB_0003a2f8;
      (*pcRam00c0004c)(2,2);
      param_9[3] = 0;
      *param_9 = 0;
    }
    else {
      (*pcRam00c00054)(2,1);
      param_9[2] = 0x3fff;
    }
  }
  else {
    iVar25 = (int)(((uint)*param_9 + (uint)param_9[3]) * 0x10000) >> 0x10;
    if (iVar28 <= iVar25) {
      iVar25 = iVar25 - iVar28;
    }
    param_9[2] = (ushort)iVar25;
    if ((*(ushort *)(((iVar25 << 0x10) >> 0xf) + param_7) & 3) != 3) {
      (*pcRam00c00054)(2,3);
      param_9[2] = 0x7fff;
      goto LAB_0003a2f0;
    }
    uVar22 = 0;
    pcVar9 = pcRam00c00054;
LAB_0003a2e8:
    (*pcVar9)(2,uVar22);
  }
LAB_0003a2f0:
  bVar23 = *(byte *)(param_9 + 4);
LAB_0003a2f8:
  if (param_2 == 2) {
    if ((bVar23 & 2) == 0) {
                    // WARNING: Bad instruction - Truncating control flow here
      halt_baddata();
    }
    sRam003ecfa6 = param_4 + (short)((int)(short)param_9[3] << 4);
    sRam003ecfa4 = param_3;
  }
  else {
    if ((bVar23 & 2) == 0) {
      return;
    }
    lVar12 = func_0x003bae50();
    if (lVar12 == 0) {
      sRam003ecfa6 = param_4 + (short)((int)(short)param_9[3] << 4) + -6;
    }
    else {
      sRam003ecfa6 = param_4 + param_9[3] * 0xc + -6;
      if (param_2 == 0x40) {
        sRam003ecfa6 = param_4 + param_9[3] * 0xc + -2;
      }
    }
    sRam003ecfa4 = param_3 + -4;
  }
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_0003a458(void)

{
  uRam003ecf3c = 0x3bad58;
  func_0x003bae78();
  func_0x003839a8();
  func_0x00384000(0,0x13f,0xef);
  func_0x003840f0(0,0,0);
  func_0x00384078(0,0,0,0x13f,0xef);
  func_0x003978c0(0,0,0,0x13f,0xef,0xffffffffffffffff,0x8000,0);
  func_0x00383c00();
  func_0x001485c8();
  uRam003ed3c8 = 0;
  if (sRam00c084ec == 0) {
    func_0x003bd5f8();
    func_0x00385238();
  }
  else {
    func_0x00388c68();
  }
  uRam00c00102 = func_0x001881e8();
  func_0x003bb290();
  uRam003ecf3c = 0;
  return;
}



void FUN_0003a578(void)

{
  undefined4 *puVar1;
  int iVar2;
  
  func_0x003bb1d8();
  func_0x003baf18();
  func_0x00382960(0x100da00,0x1004000,0x8e,0x97,0x9a);
  func_0x001881d0(uRam00c00102);
  uRam004010e2 = 0;
  puVar1 = (undefined4 *)0x2afcfc;
  uRam003ed414 = 7;
  iVar2 = 0xf;
  do {
    iVar2 = iVar2 + -1;
    *puVar1 = 0xffffffff;
    puVar1 = puVar1 + -1;
  } while (-1 < iVar2);
  uRam003ed410 = 0;
  return;
}



// WARNING: Could not reconcile some variable overlaps

void FUN_0003a618(void)

{
  undefined *puVar1;
  ulong *puVar2;
  uint uVar3;
  uint uVar4;
  undefined4 uVar5;
  undefined local_d0 [4];
  undefined local_cc;
  undefined local_cb;
  undefined local_ca;
  undefined local_c9;
  undefined4 local_c4 [33];
  undefined auStack64 [8];
  undefined local_38;
  undefined local_37;
  undefined auStack48 [8];
  undefined8 local_28;
  
  func_0x00143b68();
  func_0x00148f88(0x3ed424);
  func_0x00143c08(0,1,0);
  func_0x001490a0(0,1,0);
  func_0x001491c8(0,0,0,0x13f,0xef);
  func_0x001470a0(0x117fffe,1);
  func_0x00187580(0x1180000,0);
  func_0x00143ed0(auStack64);
  func_0x00143ef8(local_d0);
  local_d0[0] = 0;
  local_cc = 0;
  local_ca = 1;
  local_c9 = 0;
  if (sRam00c084ec == 0) {
    local_cb = 1;
    uVar3 = 0;
    do {
      uVar4 = uVar3 + 1 & 0xffff;
      local_c4[uVar3] = 0x1140000;
      uVar3 = uVar4;
    } while (uVar4 < 0x10);
    func_0x001445e0(4,local_d0);
    local_cb = 1;
    uVar3 = 0;
    do {
      uVar4 = uVar3 + 1 & 0xffff;
      local_c4[uVar3] = 0x1160000;
      uVar3 = uVar4;
    } while (uVar4 < 0x10);
    func_0x001445e0(8);
    local_37 = 1;
  }
  else {
    if (sRam00c084ee == 0) {
      local_37 = 1;
      uVar5 = 0x1140000;
    }
    else {
      local_38 = 1;
      uVar5 = 0x1120000;
    }
    local_cb = 1;
    uVar3 = 0;
    do {
      uVar4 = uVar3 + 1 & 0xffff;
      local_c4[uVar3] = uVar5;
      uVar3 = uVar4;
    } while (uVar4 < 0x10);
    func_0x001445e0(4);
  }
  func_0x00143f50();
  puVar1 = auStack48 + 7;
  uVar3 = (uint)puVar1 & 7;
  *(ulong *)(puVar1 + -uVar3) =
       *(ulong *)(puVar1 + -uVar3) & -1 << (uVar3 + 1) * 8 | uRam003fd0f0 >> (7 - uVar3) * 8;
  auStack48 = uRam003fd0f0;
  uVar3 = (int)&local_28 + 7U & 7;
  puVar2 = (ulong *)(((int)&local_28 + 7U) - uVar3);
  *puVar2 = *puVar2 & -1 << (uVar3 + 1) * 8 | uRam003fd0f8 >> (7 - uVar3) * 8;
  local_28 = uRam003fd0f8;
  local_28._2_6_ = (undefined6)(uRam003fd0f8 >> 0x10);
  if (sRam00c084ec == 0) {
    local_28 = CONCAT62(local_28._2_6_,0x44ff);
    local_28._0_6_ = CONCAT24(0x55ff,(undefined4)local_28);
    local_28 = uRam003fd0f8 & 0xffff000000000000 | (ulong)(uint6)local_28;
  }
  else {
    if (sRam00c084ee == 0) {
      local_28 = CONCAT62(local_28._2_6_,0x44ff);
    }
    else {
      auStack48._0_6_ = CONCAT24(0x44ff,(int)uRam003fd0f0);
      auStack48 = uRam003fd0f0 & 0xffff000000000000 | (ulong)auStack48._0_6_;
    }
  }
  func_0x00144e48();
  func_0x001470a8(1,1,1);
  func_0x001475c0(0x33,0);
  func_0x001475c0(8,4);
  func_0x001475c0(4,5);
  func_0x001475c0(0x200,6);
  func_0x001475c0(0xfd00,6);
  func_0x001484a0();
  func_0x003ddbd0();
  uRam0020d084 = 1;
  uRam01000000 = 0x8000;
  func_0x001485c8();
  return;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x0003a8f0: Changing call to branch
// WARNING: Removing unreachable block (ram,0x0003a8f8)

void FUN_0003a8d8(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x0003a998: Changing call to branch
// WARNING: Removing unreachable block (ram,0x0003a9a0)

void FUN_0003a990(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_0003a9f8(void)

{
  func_0x00148640();
  (*pcRam00c00020)();
  return;
}



void FUN_0003aa28(void)

{
  int iVar1;
  ushort uVar2;
  uint uVar3;
  int *piVar4;
  int iVar5;
  ushort uVar6;
  uint uVar7;
  
  func_0x001486a8();
  uVar2 = uRam002afcb4;
  uRam003ecf62 = uRam002afcb2;
  uRam004010e2 = uRam002afcb0;
  uRam003ecf60 = uRam002afcb0;
  uVar6 = ~uRam002afcb0;
  func_0x003bb678(0x2afcb0);
  uVar7 = (uint)uRam002afcb2;
  uRam004010b2 = uRam004010b2 | uRam002afcb0 & uVar6;
  uVar3 = 1;
  piVar4 = (int *)0x2afcc0;
  iVar5 = 0;
  do {
    uVar6 = (ushort)uVar3;
    if ((uVar7 & uVar3) == 0) {
      if ((uRam002afcb0 & uVar3) == 0) {
        if (cRam003ecf64 == '\x01') {
          *piVar4 = -1;
LAB_0003ab6c:
          uRam004010b4 = uRam004010b4 & ~uVar6;
        }
      }
      else {
        iVar1 = *piVar4;
        *piVar4 = iVar1 + -1;
        if (iVar1 + -1 != 0) goto LAB_0003ab6c;
        if (cRam003ecf64 == '\x01') {
          *piVar4 = 7;
          uRam004010b4 = uVar6 | uRam004010b4;
        }
        else {
          *piVar4 = 1;
        }
      }
    }
    else {
      *piVar4 = 0x11;
      uRam004010b4 = uVar6 | uRam004010b4;
    }
    iVar5 = iVar5 + 1;
    uVar3 = (uVar3 & 0x7fff) << 1;
    piVar4 = piVar4 + 1;
  } while (iVar5 < 0x10);
  if (cRam003ecf64 == '\x01') {
    uRam002afcba = uRam004010b4;
    uRam002afcb2 = uRam004010b2;
    uRam004010b2 = 0;
  }
  uVar2 = uRam003ed3bc | uRam002afcb4 & ~uVar2;
  uRam003ed3bc = uVar2;
  if (cRam003ecf64 == '\x01') {
    uRam003ed3bc = 0;
    uRam002afcb6 = uVar2;
  }
  iRam003ecf54 = iRam003ecf54 + 1;
  cRam003ecf64 = 0;
  iRam003ed410 = iRam003ed410 + 1;
  iRam003ecf58 = iRam003ecf58 + 1;
  func_0x00382a18(1);
  func_0x00384270();
  if (iRam003ecf40 == 0) {
    if (cRam003e2f58 == '\0') {
      if (cRam003e2f5d == '\0') {
        uRam0100dc7e = 0x839c;
      }
      else {
        uRam0100dc7e = 0x801f;
      }
    }
    else {
      if (cRam003e2f58 == '\x01') {
        uRam0100dc9e = 0x801f;
        if (cRam003e2f5d != '\0') goto LAB_0003ad34;
      }
      else {
        if (cRam003e2f5d != '\0') {
          uRam0100dc9e = 0x801f;
          uRam0100dc7e = 0x801f;
          goto LAB_0003ad34;
        }
        if (sRam00c084fe == -1) {
          uRam0100dc7e = 0x839c;
        }
        if (sRam00c0853e != -1) goto LAB_0003ad34;
      }
      uRam0100dc9e = 0x839c;
    }
  }
LAB_0003ad34:
  iRam003ecf5c = iRam003ecf5c + 1;
  sRam003ed3b8 = sRam003ed3b8 + 1;
  return;
}



void FUN_0003ad78(ushort *param_1,ushort *param_2)

{
  byte bVar1;
  byte *pbVar2;
  ushort uVar3;
  int iVar4;
  uint uVar5;
  uint uVar6;
  
  func_0x001427e8(0x4010b8,0x4010bc);
  pbVar2 = pbRam004010bc;
  *param_1 = 0xffff;
  bVar1 = pbVar2[1];
  if (((*pbVar2 | bVar1) != 0xf0) && (iVar4 = 0, bVar1 != 0)) {
    if (*pcRam004010b8 == '\0') {
      uVar3 = *(ushort *)(pcRam004010b8 + 2);
    }
    else {
      do {
        iVar4 = (iVar4 + 1) * 0x10000 >> 0x10;
        if ((int)(uint)pbRam004010bc[1] <= iVar4) goto LAB_0003ae38;
      } while (pcRam004010b8[iVar4 * 4] != '\0');
      uVar3 = *(ushort *)(pcRam004010b8 + iVar4 * 4 + 2);
    }
    *param_1 = uVar3;
  }
LAB_0003ae38:
  pbVar2 = pbRam004010bc;
  *param_2 = 0xffff;
  uVar6 = (uint)pbVar2[1];
  if ((pbVar2[2] | pbVar2[3]) != 0xf0) {
    if (pbVar2[3] + uVar6 <= uVar6) {
      uVar3 = *param_1;
      goto LAB_0003aed8;
    }
    uVar5 = uVar6;
    if (pcRam004010b8[uVar6 * 4] == '\0') {
      uVar3 = *(ushort *)(pcRam004010b8 + uVar6 * 4 + 2);
    }
    else {
      do {
        uVar5 = (int)((uVar5 + 1) * 0x10000) >> 0x10;
        if ((int)(pbRam004010bc[3] + uVar6) <= (int)uVar5) goto LAB_0003aed4;
      } while (pcRam004010b8[uVar5 * 4] != '\0');
      uVar3 = *(ushort *)(pcRam004010b8 + uVar5 * 4 + 2);
    }
    *param_2 = uVar3;
  }
LAB_0003aed4:
  uVar3 = *param_1;
LAB_0003aed8:
  *param_1 = ~uVar3;
  *param_2 = ~*param_2;
  return;
}



void FUN_0003b150(byte param_1,uint param_2)

{
  int iVar1;
  short sVar2;
  short sVar3;
  uint uVar4;
  
  uVar4 = param_2 & 0xff;
  sVar2 = 0xa0;
  if (uVar4 != 0) {
    sVar2 = 0x10;
  }
  sVar3 = 0x3c;
  if (uVar4 != 0) {
    sVar3 = 0xc;
  }
  func_0x00397b30(uVar4 * 0x82c + 0x3ed480,1,*(undefined *)(uVar4 * 0x82c + 0x3ed481));
  iVar1 = uVar4 * 0x82c;
  bRam003ed440 = param_1;
  if (8 < param_1) {
    bRam003ed440 = 7;
  }
  uRam003ed442 = 1;
  uRam003ed464 = 0xffff;
  uRam003ed441 = (undefined)uVar4;
  uRam003ed478 = uRam003ecf58;
  *(short *)(iVar1 + 0x3ed496) = sVar3 + 0x28;
  if (uVar4 == 0) {
    *(short *)(iVar1 + 0x3ed498) = sVar2 + 0x16;
  }
  else {
    *(short *)(iVar1 + 0x3ed498) = sVar2 + 6;
  }
  *(undefined4 *)(iVar1 + 0x3ed4a4) = *(undefined4 *)(iVar1 + 0x3ed4a0);
  *(undefined2 *)(iVar1 + 0x3ed49a) = *(undefined2 *)(iVar1 + 0x3ed496);
  *(undefined2 *)(iVar1 + 0x3ed49c) = *(undefined2 *)(iVar1 + 0x3ed498);
  uRam003ed428 = uRam003ed478;
  *(undefined *)(iVar1 + 0x3ed47c) = 3;
  *(undefined *)(iVar1 + 0x3ed47d) = 0;
  *(undefined2 *)(iVar1 + 0x3ed47e) = 0;
  uRam003ed45d = 7;
  uRam003ed44d = 0;
  uRam003ed44c = 0;
  *(byte *)(iVar1 + 0x3ed47c) = *(byte *)(iVar1 + 0x3ed47c) | 3;
  return;
}



void FUN_0003b308(uint param_1)

{
  byte bVar1;
  bool bVar2;
  short sVar3;
  short sVar4;
  undefined2 uVar5;
  ushort uVar6;
  int iVar7;
  undefined8 *puVar8;
  long lVar9;
  ushort *puVar10;
  short *psVar11;
  undefined8 *puVar12;
  uint uVar13;
  int iVar14;
  short *psVar15;
  undefined8 uVar16;
  undefined8 uVar17;
  undefined8 uVar18;
  uint uVar19;
  int local_60;
  int local_5c;
  int local_58;
  
  uVar19 = param_1 & 0xffff;
  func_0x00129d68();
  if (uRam003ed464 != uVar19) {
    uVar13 = 0;
    if (uVar19 <= uRam003ed466) {
      uVar13 = uVar19;
    }
    uVar19 = *(uint *)(uVar13 * 4 + iRam003ed46c);
    uRam003ed438 = uRam003ed462;
    uRam003ed474 = uVar19 << 0x18 | uVar19 >> 0x18 | uVar19 >> 8 & 0xff00 | (uVar19 & 0xff00) << 8;
    bRam003ed42d = 0;
    bRam003ed42c = 0;
    uRam004012b6 = 0;
    uRam004012b7 = 0;
    cRam004012ae = '\0';
    uRam003ed464 = (ushort)uVar13;
  }
  if (cRam003ed442 == '\x01') {
    uVar5 = func_0x003bc640(iRam003ed470 + uRam003ed474);
    iRam003ecf58 = 0;
    *(undefined *)((uint)bRam003ed441 * 0x82c + 0x3ed47d) = 0;
    iRam003ed428 = 0;
    iRam003ed478 = 0;
    *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed47e) = uVar5;
    uRam003ed438 = uRam003ed462;
  }
  local_60 = 0x30;
  local_58 = 0;
  local_5c = 0x3ed470;
  uVar19 = 0;
  do {
    if ((*(byte *)(local_5c + 0xc) & 1) != 0) {
      if ((uVar19 == bRam003ed441) &&
         ((ushort)*(byte *)(local_5c + 0xd) < *(ushort *)(local_5c + 0xe))) {
        uVar13 = (uint)bRam003ed42d;
        uVar6 = (ushort)*(byte *)(uVar13 * 2 + 0x3ed44c);
        if ((uVar6 != 0xff) && (uVar6 <= *(byte *)(local_5c + 0xd))) {
          bVar2 = 2 < bRam003ed42d;
          bRam003ed42d = bRam003ed42d + 1;
          uRam003ed438 = (ushort)*(byte *)(uVar13 * 2 + 0x3ed44d);
          if (bVar2) {
            bRam003ed42d = 3;
          }
        }
        if (iRam003ed478 + (short)uRam003ed438 < iRam003ecf58) {
          if ((*(byte *)((uint)bRam003ed441 * 0x82c + 0x3ed480) & 8) == 0) {
            cRam004012ae = '\0';
          }
          else {
            if (iRam003ecf58 - iRam003ed478 < 2) {
              if (cRam004012ae == '\x01') {
                cRam004012ae = '\0';
              }
              else {
                cRam004012ae = '\x01';
              }
            }
            else {
              cRam004012ae = '\x01';
            }
          }
          if (cRam004012ae == '\x01') {
            (*pcRam00c0004c)(2);
          }
          *(char *)((uint)bRam003ed441 * 0x82c + 0x3ed47d) =
               *(char *)((uint)bRam003ed441 * 0x82c + 0x3ed47d) + '\x01';
          iVar7 = (uint)bRam003ed441 * 0x82c;
          if (*(ushort *)(iVar7 + 0x3ed47e) < (ushort)*(byte *)(iVar7 + 0x3ed47d)) {
            *(undefined *)(iVar7 + 0x3ed47d) = *(undefined *)(iVar7 + 0x3ed47e);
          }
          iRam003ed478 = iRam003ecf58;
        }
        bVar1 = *(byte *)(bRam003ed42c + 0x3ed444);
        if (bVar1 != 0xff) {
          if (cRam003ed42e == '\0') {
            if (iRam003ed434 == 0) {
              iRam003ed434 = iRam003ecf58;
            }
            if (bVar1 <= *(byte *)((uint)bRam003ed441 * 0x82c + 0x3ed47d)) {
              *(byte *)((uint)bRam003ed441 * 0x82c + 0x3ed47d) = bVar1;
            }
          }
          else {
            bRam003ed42c = bRam003ed42c + 1;
            iRam003ecf58 = iRam003ed434;
            cRam003ed42e = '\0';
            iRam003ed434 = 0;
          }
        }
        lVar9 = func_0x00121a68();
        iVar7 = iRam003ecf58;
        if (lVar9 != 0) {
          uRam002afcb2 = 0;
          *(undefined *)((uint)bRam003ed441 * 0x82c + 0x3ed47d) =
               *(undefined *)((uint)bRam003ed441 * 0x82c + 0x3ed47e);
          iRam003ed478 = iVar7;
          uRam003ed438 = uRam003ed462;
          uRam002afcb0 = uRam003ed45e;
        }
        iVar7 = iRam003ecf58;
        if ((uRam002afcb2 & uRam003ed45e) != 0) {
          uRam002afcb2 = 0;
          *(undefined *)((uint)bRam003ed441 * 0x82c + 0x3ed47d) =
               *(undefined *)((uint)bRam003ed441 * 0x82c + 0x3ed47e);
          iRam003ed478 = iVar7;
          uRam002afcb0 = uRam003ed45e;
          uRam003ed438 = uRam003ed462;
        }
      }
      bVar1 = *(byte *)((uint)bRam003ed441 * 0x82c + 0x3ed480);
      if ((((bVar1 & 1) != 0) && ((bVar1 & 8) == 0)) && ((bVar1 & 2) != 0)) {
        iRam003ed478 = 0;
        *(undefined *)((uint)bRam003ed441 * 0x82c + 0x3ed47d) = 0;
        iRam003ecf58 = 0;
      }
      bVar1 = *(byte *)(local_58 + 0x3ed480);
      uVar13 = 0;
      if ((bVar1 & 1) != 0) {
        sRam004012b2 = *(short *)(local_58 + 0x3ed494);
        sRam004012b0 = *(short *)(local_58 + 0x3ed492);
        func_0x00397c08(uRam003ed440,local_58 + 0x3ed480,4);
        bVar1 = *(byte *)(local_58 + 0x3ed480);
      }
      if ((bVar1 & 0xc) == 0) {
LAB_0003b8c4:
        bVar1 = *(byte *)(local_5c + 0x10);
      }
      else {
        if (*(char *)(local_58 + 0x3ed47d) != '\0') {
          iVar7 = 0;
          do {
            iVar7 = iVar7 + uVar19 * 0x82c;
            puVar10 = (ushort *)(iVar7 + 0x3ed4a8);
            *puVar10 = *puVar10 | 0x80;
            uVar5 = func_0x00129d78();
            *(undefined2 *)(iVar7 + 0x3ed4c2) = uVar5;
            func_0x00383a90();
            uVar13 = uVar13 + 1 & 0xff;
            iVar7 = uVar13 << 5;
          } while (uVar13 < *(byte *)(uVar19 * 0x82c + 0x3ed47d));
          goto LAB_0003b8c4;
        }
        bVar1 = *(byte *)(local_5c + 0x10);
      }
      sVar4 = sRam004012b2;
      sVar3 = sRam004012b0;
      if (((bVar1 & 4) != 0) && (uVar13 = 0, *(short *)(local_5c + 0xe) != 0)) {
        iVar7 = uVar19 * 0x82c;
        iVar14 = 0;
        do {
          psVar11 = (short *)(iVar14 + iVar7 + 0x3ed4b4);
          psVar15 = (short *)(iVar14 + iVar7 + 0x3ed4b6);
          uVar13 = uVar13 + 1 & 0xff;
          *psVar11 = *psVar11 + (*(short *)(iVar7 + 0x3ed492) - sVar3);
          *psVar15 = *psVar15 + (*(short *)(iVar7 + 0x3ed494) - sVar4);
          iVar14 = uVar13 << 5;
        } while (uVar13 < *(ushort *)(iVar7 + 0x3ed47e));
      }
      if (bRam003ed441 == uVar19) {
        if ((ushort)*(byte *)(local_60 + 0x3ed44d) < *(ushort *)(local_60 + 0x3ed44e)) {
          if (((uRam002afcb2 & uRam003ed45e) != 0) && (*(char *)(bRam003ed42c + 0x3ed444) != -1)) {
            cRam003ed42e = '\x01';
          }
        }
        else {
          if (((*(byte *)(local_60 + 0x3ed450) & 8) != 0) &&
             ((*(byte *)(local_60 + 0x3ed44c) & 2) != 0)) {
            bVar2 = iRam003ed428 + 0xc < iRam003ecf58;
            *(byte *)(local_60 + 0x3ed44c) = *(byte *)(local_60 + 0x3ed44c) | 8;
            if (bVar2) {
              if (uVar19 == 0) {
                sRam003ecf84 = sRam004012b0 + 0xd0;
                sRam003ecf86 = sRam004012b2 + 0x2c;
              }
              else {
                sRam003ecf84 = sRam004012b0 + 0xcc;
                sRam003ecf86 = sRam004012b2 + 0x1c;
              }
              func_0x00383a90(uRam003ed440);
              iRam003ed428 = iRam003ecf58;
            }
            if ((uRam002afcb2 & uRam003ed45e) != 0) {
              bVar1 = *(byte *)((uint)bRam003ed441 * 0x82c + 0x3ed47c);
              if ((bVar1 & 4) == 0) {
                *(byte *)((uint)bRam003ed441 * 0x82c + 0x3ed47c) = bVar1 & 0xfd;
              }
              else {
                cRam003ed442 = '\x01';
                func_0x001390a0(uRam00c00110 & 7);
              }
              if (*(char *)(bRam003ed42c + 0x3ed444) != -1) {
                cRam003ed42e = '\x01';
              }
            }
            lVar9 = func_0x00121a68();
            if (((lVar9 != 0) && ((*(byte *)((uint)bRam003ed441 * 0x82c + 0x3ed47c) & 4) != 0)) &&
               (cRam003ed442 != '\x01')) {
              cRam003ed442 = '\x01';
              func_0x001390a0();
            }
            lVar9 = func_0x00121a40();
            if (lVar9 == 1) {
              iRam003ed43c = iRam003ed43c + 1;
              if (0x8b < iRam003ed43c) {
                iRam003ed43c = 0;
              }
              puVar12 = (undefined8 *)0x107ea00;
              iVar7 = iRam003ed43c * 0x900;
              puVar8 = (undefined8 *)(iVar7 + 0xc30400);
              do {
                uVar16 = puVar8[1];
                uVar17 = puVar8[2];
                uVar18 = puVar8[3];
                *puVar12 = *puVar8;
                puVar12[1] = uVar16;
                puVar12[2] = uVar17;
                puVar12[3] = uVar18;
                puVar8 = puVar8 + 4;
                puVar12 = puVar12 + 4;
              } while (puVar8 != (undefined8 *)(iVar7 + 0xc30d00));
              func_0x0013d890();
            }
            else {
              if (lVar9 == 2) {
                iRam003ed43c = iRam003ed43c + -1;
                if (iRam003ed43c < 0) {
                  iRam003ed43c = 0x8b;
                }
                puVar12 = (undefined8 *)0x107ea00;
                iVar7 = iRam003ed43c * 0x900;
                puVar8 = (undefined8 *)(iVar7 + 0xc30400);
                do {
                  uVar16 = puVar8[1];
                  uVar17 = puVar8[2];
                  uVar18 = puVar8[3];
                  *puVar12 = *puVar8;
                  puVar12[1] = uVar16;
                  puVar12[2] = uVar17;
                  puVar12[3] = uVar18;
                  puVar8 = puVar8 + 4;
                  puVar12 = puVar12 + 4;
                } while (puVar8 != (undefined8 *)(iVar7 + 0xc30d00));
                func_0x0013d890();
              }
            }
          }
        }
      }
    }
    uVar19 = SEXT24((short)((short)uVar19 + 1));
    local_60 = local_60 + 0x82c;
    local_5c = local_5c + 0x82c;
    local_58 = local_58 + 0x82c;
    if (1 < (int)uVar19) {
      return;
    }
  } while( true );
}



// WARNING: Removing unreachable block (ram,0x0003c764)
// WARNING: Removing unreachable block (ram,0x0003c780)
// WARNING: Removing unreachable block (ram,0x0003c7b0)
// FUN_0003bd40

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
      func_0x003bd2b0();
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
                    // WARNING: Could not emulate address calculation at 0x0003bf60
                    // WARNING: Treating indirect jump as call
          uVar4 = (**(code **)(uVar3 * 4 + 0x3fd100))();
          return uVar4;
        }
        uVar4 = uVar4 + 1;
      }
      else {
        if (bVar1 != 0x10) {
          if (!bVar11) {
            uRam003ed428 = 0;
            bVar11 = true;
            *(undefined *)((uint)bRam003ed441 * 0x82c + 0x3ed47d) = 0;
            *(undefined4 *)((uint)bRam003ed441 * 0x82c + 0x3ed4a4) =
                 *(undefined4 *)((uint)bRam003ed441 * 0x82c + 0x3ed4a0);
            uRam003ed478 = 0;
            *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) =
                 *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed496);
            *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49c) =
                 *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed498);
          }
          func_0x003bd478();
          cVar7 = cVar7 + '\x01';
          uVar10 = uVar10 + 1 & 0xff;
          goto LAB_0003c89c;
        }
        uVar3 = uVar4 + 1 & 0xff;
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
        if (*(byte *)(param_1 + uVar3) < 0x10) {
                    // WARNING: Could not emulate address calculation at 0x0003c4a4
                    // WARNING: Treating indirect jump as call
          uVar4 = (**(code **)((uint)*(byte *)(param_1 + uVar3) * 4 + 0x3fd130))();
          return uVar4;
        }
        bVar9 = 0;
        bVar1 = func_0x00188e50();
        uVar4 = uVar3 + 1;
        if (bVar1 != 0) {
          do {
            func_0x003bd2b0();
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



uint FUN_0003c938(char *param_1)

{
  int iVar1;
  char *pcVar2;
  char cVar3;
  uint uVar4;
  char local_20 [16];
  
  uVar4 = 0;
  if (*param_1 != ';') {
    cVar3 = *param_1;
    while( true ) {
      pcVar2 = local_20 + uVar4;
      uVar4 = uVar4 + 1;
      *pcVar2 = cVar3;
      if (param_1[uVar4] == ';') break;
      cVar3 = param_1[uVar4];
    }
  }
  local_20[uVar4] = '\0';
  iVar1 = func_0x00186160(local_20);
  return iVar1 << 0x10 | uVar4;
}



// WARNING: Control flow encountered bad instruction data

void PrintTextInJapanese(undefined2 *param_1,uint param_2)

{
  int *piVar1;
  int iVar2;
  int iVar3;
  ushort uVar4;
  short sVar5;
  int iVar6;
  
  iVar6 = (param_2 & 0xffff) * 0x20;
  *(short *)(iVar6 + (uint)bRam003ed441 * 0x82c + 0x3ed4b0) =
       (short)(*(uint *)((uint)bRam003ed441 * 0x82c + 0x3ed4a4) >> 3);
  *(undefined2 *)((uint)bRam003ed441 * 0x82c + iVar6 + 0x3ed4b2) = 0x210;
  *(undefined2 *)(iVar6 + (uint)bRam003ed441 * 0x82c + 0x3ed4b4) =
       *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49a);
  *(undefined2 *)(iVar6 + (uint)bRam003ed441 * 0x82c + 0x3ed4b6) =
       *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed49c);
  piVar1 = (int *)((uint)bRam003ed441 * 0x82c + 0x3ed4a4);
  *piVar1 = *piVar1 + 0x80;
  iVar2 = func_0x00129d30();
  *(short *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) =
       *(short *)((uint)bRam003ed441 * 0x82c + 0x3ed49a) + (short)(iVar2 << 1);
  iVar2 = (uint)bRam003ed441 * 0x82c;
  uVar4 = *(ushort *)(iVar2 + 0x3ed49a);
  if ((long)((long)(int)((uint)*(ushort *)(iVar2 + 0x3ed496) + (uint)bRam003ed443 * 8) & 0xffffU) <=
      (long)*(short *)(iVar2 + 0x3ed49a)) {
    uVar4 = *(ushort *)(iVar2 + 0x3ed496);
  }
  *(ushort *)(iVar2 + 0x3ed49a) = uVar4;
  iVar2 = (uint)bRam003ed441 * 0x82c;
  sVar5 = *(short *)(iVar2 + 0x3ed49c);
  if (*(short *)(iVar2 + 0x3ed49a) == *(short *)(iVar2 + 0x3ed496)) {
    iVar3 = func_0x00129d30();
    sVar5 = sVar5 + (short)(iVar3 << 1);
  }
  *(short *)(iVar2 + 0x3ed49c) = sVar5;
  func_0x00129d88(*(undefined2 *)((uint)bRam003ed441 * 0x82c + iVar6 + 0x3ed4b0),uRam003e281c,0,
                  *param_1);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0003cfb8(void)

{
  ushort uVar1;
  short sVar2;
  long lVar3;
  ushort *puVar4;
  int iVar5;
  
  func_0x001881d0(uRam00c00102);
  sVar2 = 0;
  if (sRam00c00002 != 1) {
    puVar4 = (ushort *)0xc02d46;
    do {
      uVar1 = puVar4[0xab];
      if (0 < (int)((uint)*puVar4 - (uint)uVar1)) {
        if (puVar4[-0x160b] == 6) {
          *puVar4 = uVar1;
        }
        else {
          *puVar4 = uVar1 + 1;
        }
      }
      lVar3 = func_0x003d04f0(sVar2,uRam00c00110);
      if (lVar3 == 1) {
        if (0 < (int)((uint)*puVar4 - (uint)puVar4[0xab])) {
          sRam00c084e8 = sRam00c084e8 + 1;
        }
      }
      else {
        lVar3 = func_0x003d0558(sVar2,uRam00c00110);
        if (lVar3 == 1) {
          func_0x003d0a90(sVar2);
        }
      }
      sVar2 = sVar2 + 1;
      puVar4 = puVar4 + 1;
    } while (sVar2 < 0xab);
  }
  for (iVar5 = 0xa9; -1 < iVar5; iVar5 = iVar5 + -1) {
  }
  func_0x00396a98(0x42,0x107f300,uRam00c00110);
  func_0x00396a98(0x53,0x107ea00,uRam00c00112);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



undefined4 FUN_0003d9b0(undefined param_1)

{
  undefined4 uVar1;
  
  uVar1 = 0;
  if (bRam003ee550 == 1) {
    if ((bRam003ed480 & 8) != 0) {
      bRam003ee550 = 0;
      uVar1 = 1;
    }
  }
  else {
    if (bRam003ee550 < 2) {
      if (bRam003ee550 == 0) {
        func_0x00383000(7);
        func_0x00396a98(0x53,0x107ea00,uRam00c00112);
        func_0x003bba50(3,0,0xfffffffffffffed4,0xa0,400,0xa0);
        uRam004012b8 = func_0x00396fb8(0x53,param_1,1,(undefined)uRam00c00112);
        bRam003ee550 = 2;
      }
    }
    else {
      if (bRam003ee550 == 2) {
        if (((bRam003ed480 & 8) != 0) && ((bRam003ed47c & 8) != 0)) {
          bRam003ee550 = 3;
          uRam003ed410 = 0;
        }
      }
      else {
        if ((bRam003ee550 == 3) && (((bRam003ed47c & 2) == 0 || (300 < uRam003ed410)))) {
          bRam003ee550 = 1;
          uRam003ed492 = uRam003ed48a;
          bRam003ed480 = bRam003ed480 & 0xf1 | 4;
          uRam003ed494 = uRam003ed48c;
        }
      }
    }
  }
  func_0x00382ab8(7,0);
  func_0x003bbc08(uRam004012b8);
  return uVar1;
}



undefined4 FUN_0003db78(short param_1)

{
  undefined8 uVar1;
  undefined4 uVar2;
  int iVar3;
  int iVar4;
  uint uVar5;
  undefined4 uVar6;
  
  uVar5 = SEXT24(param_1);
  uVar6 = 0;
  if (bRam003ee551 == 1) {
    if (((bRam004012c0 & 8) != 0) && ((bRam003ed480 & 8) != 0)) {
      bRam003ee551 = 0;
      uVar6 = 1;
    }
  }
  else {
    if (bRam003ee551 < 2) {
      if (bRam003ee551 == 0) {
        func_0x00383000();
        func_0x00397b30(0x4012c0,1,0x13,1,0x140,0x10,0xc,0x10);
        func_0x00396a98(0x42,0x107f300,uVar5);
        func_0x00396a98(0x53,0x107ea00,uRam00c00112);
        uRam003ecf4f = (undefined)param_1;
        func_0x003bba50(3,0,0xfffffffffffffed4,0xa0,400,0xa0);
        uRam004012d6 = func_0x00396fb8(0x53,0x11,1,(undefined)uRam00c00112);
        sRam004012da = 0;
        uRam004012d8 = 0;
        do {
          uRam004012dc = func_0x003977c0((undefined)uRam004012d8,uVar5 & 0xff);
          if (uRam004012dc < 0xb) {
            sRam004012da = sRam004012da + uRam004012dc;
          }
          uRam004012d8 = uRam004012d8 + 1;
        } while (uRam004012d8 < 10);
        bRam003ee551 = 2;
      }
    }
    else {
      if (((bRam003ee551 == 2) && ((bRam003ed480 & 8) != 0)) && ((bRam003ed47c & 2) == 0)) {
        bRam004012c0 = bRam004012c0 & 0xf1 | 4;
        bRam003ed480 = bRam003ed480 & 0xf1 | 4;
        uRam004012d4 = uRam004012cc;
        bRam003ee551 = 1;
        uRam003ed494 = uRam003ed48c;
        uRam004012d2 = uRam004012ca;
        uRam003ed492 = uRam003ed48a;
      }
    }
  }
  iVar4 = uVar5 * 2;
  func_0x00382ab8(7,0);
  func_0x00382ad8((int)((uRam004012d2 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam004012d4 + 2) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd2e8);
  func_0x00382ad8((int)((uRam004012d2 + 0x6a) * 0x10000) >> 0x10,
                  (int)((uRam004012d4 + 2) * 0x10000) >> 0x10);
  uVar1 = func_0x003d0ae8(uVar5);
  func_0x00382c20(1,1,uVar1);
  func_0x00382ad8((int)((uRam004012d2 + 10) * 0x10000) >> 0x10,
                  (int)((uRam004012d4 + 0x14) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,*(undefined4 *)(uVar5 * 4 + 0x3e78c0));
  func_0x00382ad8((int)((uRam004012d2 + 0x5e) * 0x10000) >> 0x10,
                  (int)((uRam004012d4 + 0x26) * 0x10000) >> 0x10);
  iVar3 = (uint)*(ushort *)(iVar4 + 0xc01fea) - (uint)*(ushort *)(iVar4 + 0xc02296);
  func_0x00382b60(1,1,0x3fd2f0,
                  (iVar3 + (uint)*(ushort *)(iVar4 + 0xc02140)) -
                  (uint)*(ushort *)(iVar4 + 0xc023ec),iVar3,
                  (uint)*(ushort *)(iVar4 + 0xc02140) - (uint)*(ushort *)(iVar4 + 0xc023ec));
  func_0x00382ad8((int)((uRam004012d2 + 100) * 0x10000) >> 0x10,
                  (int)((uRam004012d4 + 0x38) * 0x10000) >> 0x10);
  if ((int)((uint)*(ushort *)(iVar4 + 0xc02d46) - (uint)*(ushort *)(iVar4 + 0xc02e9c)) < 1) {
    uVar2 = 0x3fd308;
  }
  else {
    uVar2 = 0x3fd300;
  }
  func_0x00382b60(1,1,uVar2);
  func_0x00382ad8((int)((uRam004012d2 + 0xc4) * 0x10000) >> 0x10,
                  (int)((uRam004012d4 + 0x38) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd310,*(undefined2 *)(iVar4 + 0xc02d46));
  func_0x00382ad8((int)((uRam004012d2 + 0xc4) * 0x10000) >> 0x10,
                  (int)((uRam004012d4 + 0x4a) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd310,sRam004012da);
  func_0x00382ad8((int)((uRam004012d2 + 0x5e) * 0x10000) >> 0x10,
                  (int)((uRam004012d4 + 0x14) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd318,uRam00c0011c,uRam00c0011e,uRam00c00120);
  func_0x00382ad8((int)((uRam004012d2 + 0x3a) * 0x10000) >> 0x10,
                  (int)((uRam004012d4 + 0x26) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd330);
  func_0x00382ad8((int)((uRam004012d2 + 0x3a) * 0x10000) >> 0x10,
                  (int)((uRam004012d4 + 0x38) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd348);
  func_0x00382ad8((int)((uRam004012d2 + 0x8e) * 0x10000) >> 0x10,
                  (int)((uRam004012d4 + 0x4a) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd360);
  func_0x00397c08(0,0x4012c0,uRam003ecf48);
  func_0x003bbc08(uRam004012d6);
  return uVar6;
}



undefined4 FUN_0003e148(short param_1)

{
  undefined8 uVar1;
  int iVar2;
  undefined4 uVar3;
  
  uVar3 = 0;
  iVar2 = (int)param_1;
  if (bRam003ee552 == 1) {
    if (((bRam004012e0 & 8) != 0) && ((bRam003ed480 & 8) != 0)) {
      bRam003ee552 = 0;
      uVar3 = 1;
    }
  }
  else {
    if (bRam003ee552 < 2) {
      if (bRam003ee552 == 0) {
        func_0x00383000(7);
        func_0x00397b30(0x4012e0,1,0x14,1,0x140);
        func_0x00396a98(0x42,0x107f300,iVar2);
        func_0x00396a98(0x53,0x107ea00,uRam00c00112);
        uRam003ecf4f = (undefined)param_1;
        func_0x003bba50(3,0,0xfffffffffffffed4,0xa0,400);
        uRam004012f6 = func_0x00396fb8(0x53,0x14,1,(undefined)uRam00c00112);
        bRam003ee552 = 2;
      }
    }
    else {
      if (((bRam003ee552 == 2) && ((bRam003ed480 & 8) != 0)) && ((bRam003ed47c & 2) == 0)) {
        bRam004012e0 = bRam004012e0 & 0xf1 | 4;
        bRam003ed480 = bRam003ed480 & 0xf1 | 4;
        uRam004012f4 = uRam004012ec;
        bRam003ee552 = 1;
        uRam003ed494 = uRam003ed48c;
        uRam004012f2 = uRam004012ea;
        uRam003ed492 = uRam003ed48a;
      }
    }
  }
  func_0x00382ab8(7,0);
  func_0x00382ad8((int)((uRam004012f2 + 0x30) * 0x10000) >> 0x10,
                  (int)((uRam004012f4 + 0xc) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd370);
  func_0x00382ad8((int)((uRam004012f2 + 0x40) * 0x10000) >> 0x10,
                  (int)((uRam004012f4 + 8) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,*(undefined4 *)(iVar2 * 4 + 0x3e78c0));
  func_0x00382ad8((int)((uRam004012f2 + 0x60) * 0x10000) >> 0x10,
                  (int)((uRam004012f4 + 0x2a) * 0x10000) >> 0x10);
  uVar1 = func_0x003d12d8(iVar2);
  uVar1 = func_0x001383d0(uVar1);
  func_0x00382b60(1,0,0x3fd378,uVar1);
  func_0x00382ad8((int)((uRam004012f2 + 0x60) * 0x10000) >> 0x10,
                  (int)((uRam004012f4 + 0x34) * 0x10000) >> 0x10);
  uVar1 = func_0x003d0e88(0x299,*(undefined2 *)(iVar2 * 2 + 0xc00532));
  func_0x00382b60(1,0,0x3fd380,uVar1);
  func_0x003d1510((int)((uRam004012f2 + 0x60) * 0x10000) >> 0x10,
                  (int)((uRam004012f4 + 0x3e) * 0x10000) >> 0x10,1,0);
  func_0x00382ad8((int)((uRam004012f2 + 0x48) * 0x10000) >> 0x10,
                  (int)((uRam004012f4 + 0x48) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,*(undefined4 *)((uint)*(ushort *)(iVar2 * 2 + 0xc00934) * 4 + 0x3ee524));
  func_0x00382ad8((int)((uRam004012f2 + 0x80) * 0x10000) >> 0x10,
                  (int)((uRam004012f4 + 0x48) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd388);
  sRam003ed0c6 = uRam004012f4 + 0x3e;
  sRam003ed066 = uRam004012f4 + 0xc;
  sRam003ed124 = uRam004012f2 + 0x46;
  sRam003ed0c4 = uRam004012f2 + 0x48;
  sRam003ed126 = uRam004012f4 + 0x2a;
  sRam003ed146 = uRam004012f4 + 0x34;
  sRam003ed064 = uRam004012f2 + 0x14;
  sRam003ed144 = sRam003ed124;
  func_0x00383a90(1,0x3ed058);
  func_0x00383a90(1,0x3ed118);
  func_0x00383a90(1,0x3ed138);
  func_0x00383a90(1,0x3ed0b8);
  func_0x00397c08(0,0x4012e0,uRam003ecf48);
  func_0x003bbc08(uRam004012f6);
  return uVar3;
}



void FUN_0003e5f0(void)

{
  int iVar1;
  
  iVar1 = 0;
  if (sRam00c00002 != 1) {
    do {
      func_0x003c3f58();
      func_0x003c5da0();
      func_0x003c8fc8();
      func_0x003cb6e0();
      func_0x003cb990();
      iVar1 = (iVar1 + 1) * 0x10000 >> 0x10;
    } while (iVar1 < 0xab);
  }
  return;
}



void FUN_0003e660(void)

{
  int iVar1;
  
  iVar1 = 0;
  do {
    func_0x003d14b8(iVar1,0xff00);
    iVar1 = (iVar1 + 1) * 0x10000 >> 0x10;
  } while (iVar1 < 0xab);
  return;
}



void FUN_0003e6a8(void)

{
  short sVar1;
  bool bVar2;
  uint uVar3;
  long lVar4;
  int iVar5;
  char cVar6;
  int iVar7;
  
  iVar7 = 0;
  func_0x003c1a20();
  func_0x003beef0();
  func_0x003d1630();
  do {
    iVar5 = iVar7 * 4;
    iVar7 = iVar7 + 1;
    func_0x00396e38(*(undefined4 *)(iVar5 + 0x3ee740),2,0,8,0x7fff,0);
    *(undefined4 *)(iVar5 + 0x3eebf8) = 1;
  } while (iVar7 < 0x23);
  func_0x00187580(0x3ed3d0,1,0x40);
  uRam003ed3ba = 0;
  uRam003ed3b8 = 0;
  uRam00401459 = 0;
  uRam00401458 = 0;
  uRam00401457 = 0;
  uRam00401456 = 0;
  uRam00401511 = 0;
  uRam00401510 = 0;
  bRam003ed3c0 = bRam003ed3c8;
  uRam002afcb2 = 0;
  uRam004012f9 = 0;
  bRam003ecf72 = 4;
  bRam003ed3be = 0;
  uRam002afcb0 = 0;
  do {
    func_0x003839a8();
    func_0x00384000(0,0x13f,0xef);
    func_0x003840f0(0,0,0);
    func_0x00384078(0,0,0,0x13f,0xef);
    func_0x003978c0(0,0);
    bRam003ed3bf = *(byte *)(bRam003ed3be + 0x3ed3c8);
    (**(code **)((uint)bRam003ed3bf * 4 + 0x3ee6b0))();
    if (bRam003ecf72 == 2) {
      if (sRam003ed41c < sRam003ed418) {
        func_0x00147f88(0,uRam003ed420);
LAB_0003e97c:
        sRam003ed41c = 0;
        bRam003ecf72 = 4;
      }
      else {
LAB_0003ea10:
        func_0x001481b0(0);
        sRam003ed41c = sRam003ed41c - sRam003ed418;
      }
    }
    else {
      if (bRam003ecf72 < 3) {
        if (bRam003ecf72 == 1) {
          iVar7 = *(int *)((uint)bRam003ed3bf * 4 + 0x3ee740);
          if (*(char *)(iVar7 + 10) == -1) {
            bRam003ecf72 = 4;
            if ((bRam003ed3bf == 3) && (bRam003ecf72 = 0, *(short *)(iVar7 + 4) != 0)) {
              bRam003ecf72 = 4;
            }
          }
          else {
            if (sRam003ed418 <= sRam003ed41c) goto LAB_0003ea10;
            func_0x00147f88(0,uRam003ed420);
            sRam003ed41c = 0;
          }
        }
      }
      else {
        if (bRam003ecf72 == 3) {
          if (*(char *)(*(int *)((uint)bRam003ed3bf * 4 + 0x3ee740) + 10) == -1) {
            bRam003ecf72 = 4;
          }
        }
        else {
          if (bRam003ecf72 == 5) {
            if (sRam003ed41c < sRam003ed418) {
              func_0x00147f88(0,uRam003ed420);
              goto LAB_0003e97c;
            }
            func_0x001481b0(0);
            sRam003ed41c = sRam003ed41c - sRam003ed418;
          }
        }
      }
    }
    if (bRam003ecf72 == 4) {
      iVar7 = *(int *)((uint)bRam003ed3bf * 4 + 0x3ee740);
      sVar1 = *(short *)(iVar7 + 4);
      if (sVar1 == 0x3fff) {
        if (*(char *)(iVar7 + 10) == '\0') {
          (*pcRam00c0004c)();
          *(undefined *)(*(int *)((uint)bRam003ed3bf * 4 + 0x3ee740) + 10) = 0x80;
          bRam003ecf72 = 3;
        }
        else {
          (*pcRam00c0004c)();
          *(undefined *)(*(int *)((uint)bRam003ed3bf * 4 + 0x3ee740) + 10) = 0;
          bVar2 = bRam003ed3be == 0;
          *(undefined2 *)(*(int *)((uint)bRam003ed3bf * 4 + 0x3ee740) + 4) = 0xff;
          bRam003ed3be = bRam003ed3be - 1;
          if (bVar2) {
            bRam003ed3be = 0;
          }
          bRam003ed3c0 = bRam003ed3bf;
          bRam003ed3bf = *(byte *)(bRam003ed3be + 0x3ed3c8);
          *(undefined2 *)(*(int *)((uint)bRam003ed3bf * 4 + 0x3ee740) + 4) = 0xff;
        }
      }
      else {
        if (sVar1 != 0x7fff) {
          if (sVar1 < 8) {
            cVar6 = *(char *)((int)sVar1 + (uint)bRam003ed3bf * 8 + 0x3ee7d0);
          }
          else {
            cVar6 = -1;
          }
          if (cVar6 < '\x01') {
            if (cVar6 == '\0') {
              iVar7 = *(int *)((uint)bRam003ed3bf * 4 + 0x3ee740);
              *(undefined2 *)(iVar7 + 4) = 0x7fff;
              *(undefined *)(iVar7 + 10) = 0;
            }
          }
          else {
            if (*(char *)(*(int *)((uint)bRam003ed3bf * 4 + 0x3ee740) + 10) == '\0') {
              (*pcRam00c0004c)(2,5);
              *(undefined *)(*(int *)((uint)bRam003ed3bf * 4 + 0x3ee740) + 10) = 0x80;
              if (bRam003ecf72 == 4) {
                bRam003ecf72 = 3;
              }
            }
            else {
              (*pcRam00c0004c)(2,4);
              bRam003ed3c0 = bRam003ed3bf;
              uVar3 = (uint)bRam003ed3be;
              bVar2 = 6 < bRam003ed3be;
              bRam003ed3be = bRam003ed3be + 1;
              *(byte *)(uVar3 + 0x3ed3c8) = bRam003ed3bf;
              if (bVar2) {
                bRam003ed3be = 7;
              }
              uRam003ecf54 = 0;
              iVar7 = *(int *)((uint)bRam003ed3bf * 4 + 0x3ee740);
              *(char *)(bRam003ed3be + 0x3ed3c8) = cVar6;
              *(undefined *)(iVar7 + 10) = 0;
            }
          }
        }
      }
    }
    func_0x00382a18(2);
    func_0x00383c00();
    uRam003ecf64 = 1;
    func_0x001485c8();
    if (*(char *)(bRam003ed3bf + 0x3ed3d0) == '\x01') {
      uRam003ecf68 = 1;
    }
    lVar4 = (*pcRam00c0005c)(uRam002afcb0);
    if (lVar4 == 1) {
      bRam003ecf72 = 0;
      uRam003ecf4a = 1;
    }
    if (bRam003ecf72 == 0) {
      func_0x003bef60();
      uRam00c00002 = 0;
      return;
    }
  } while( true );
}



void FUN_0003ed80(void)

{
  int iVar1;
  int iVar2;
  
  iVar2 = 0;
  func_0x003c1a20();
  func_0x003beef0();
  func_0x003d1630();
  do {
    iVar1 = iVar2 * 4;
    iVar2 = iVar2 + 1;
    func_0x00396e38(*(undefined4 *)(iVar1 + 0x3ee740),2,0,8,0x7fff,0);
    *(undefined4 *)(iVar1 + 0x3eebf8) = 1;
  } while (iVar2 < 0x23);
  func_0x00187580(0x3ed3d0,1,0x40);
  uRam003ed3ba = 0;
  uRam003ed3b8 = 0;
  uRam00401456 = 0;
  uRam003ed3c0 = uRam003ed3c8;
  uRam00401459 = 0;
  uRam00401458 = 0;
  uRam00401457 = 0;
  uRam00401511 = 0;
  uRam00401510 = 0;
  uRam003ed3be = 0;
  uRam004012fb = 0;
  uRam002afcb0 = 0;
  uRam002afcb2 = 0;
  cRam003ecf72 = '\x04';
  do {
    func_0x003839a8();
    func_0x00384000(0,0x13f,0xef);
    func_0x003840f0(0,0,0);
    func_0x00384078(0,0,0,0x13f,0xef);
    func_0x003978c0(0);
    bRam003ed3bf = 4;
    (*pcRam003ee6c0)();
    if (cRam003ecf72 == '\x02') {
      if (sRam003ed41c < sRam003ed418) {
        func_0x00147f88(0);
LAB_0003f034:
        sRam003ed41c = 0;
        cRam003ecf72 = '\x04';
      }
      else {
        func_0x001481b0(0);
        sRam003ed41c = sRam003ed41c - sRam003ed418;
      }
    }
    else {
      if (cRam003ecf72 == '\x05') {
        if (sRam003ed41c < sRam003ed418) {
          func_0x00147f88(0);
          goto LAB_0003f034;
        }
        func_0x001481b0(0);
        sRam003ed41c = sRam003ed41c - sRam003ed418;
      }
    }
    if ((cRam003ecf72 == '\x04') &&
       (*(short *)(*(int *)((uint)bRam003ed3bf * 4 + 0x3ee740) + 4) == 0x3fff)) {
      cRam003ecf72 = '\0';
    }
    func_0x00382a18(2);
    func_0x00383c00();
    uRam003ecf64 = 1;
    func_0x001485c8();
    if (*(char *)(bRam003ed3bf + 0x3ed3d0) == '\x01') {
      uRam003ecf68 = 1;
    }
    if (cRam003ecf72 == '\0') {
      func_0x003bef60();
      uRam00c00002 = 0;
      return;
    }
  } while( true );
}



// WARNING: Control flow encountered bad instruction data

void FUN_0003f100(void)

{
  short sVar1;
  long lVar2;
  undefined8 uVar3;
  int iVar4;
  int iVar5;
  
  if (bRam003eeacc == 1) {
    if ((((sRam003ed41c == 0) && ((bRam00401300 & 8) != 0)) && ((bRam00401318 & 8) != 0)) &&
       ((bRam00401330 & 8) != 0)) {
      bRam003eeacc = 0;
      uRam003eee62 = 0xff;
    }
  }
  else {
    if (bRam003eeacc < 2) {
      if (bRam003eeacc == 0) {
        func_0x00383000();
        if ((iRam003ecf44 == 1) || (lVar2 = (*pcRam00c000bc)(), lVar2 == 1)) {
          iRam00401350 = 1;
        }
        else {
          iRam00401350 = 0;
        }
        bRam00401354 = 6;
        if (iRam00401350 != 1) {
          bRam00401354 = 4;
        }
        func_0x00396e38(0x3eee58,2,bRam00401457,bRam00401354,0x7fff,
                        (uint)bRam00401456 - (uint)bRam00401457);
        bRam003eee60 = bRam003eee60 & 0xfe;
        uRam003eee5a = (ushort)bRam00401354;
        iVar4 = 0;
        func_0x00397b30(0x401300,1,0,1,0xf0,0xffffffffffffffc0,0xf0,0x10);
        func_0x00397b30(0x401318,1,1,1,0xc,0x110,0xc,0x10);
        uVar3 = 0x11;
        if (iRam00401350 != 1) {
          uVar3 = 2;
        }
        iVar5 = 0;
        func_0x00397b30(0x401330,1,uVar3,1,0x160,0x20,0xf0,0x20);
        sRam0040134c = 0;
        sRam0040134e = 0;
        sRam00401346 = 0;
        do {
          lVar2 = func_0x003d04f0(iVar4,uRam00c00110);
          if (lVar2 == 1) {
            sRam0040134c = sRam0040134c + *(short *)(iVar5 + 0xc01fea);
            sRam00401346 = sRam00401346 + 1;
            sRam0040134e = sRam0040134e + *(short *)(iVar5 + 0xc02140);
          }
          iVar4 = (iVar4 + 1) * 0x10000 >> 0x10;
          iVar5 = iVar5 + 2;
        } while (iVar4 < 0xab);
        sRam00401348 = 0;
        sRam0040134a = sRam0040134c + sRam0040134e;
        sVar1 = 0;
        iVar4 = 0;
        do {
          if ((0x17 < *(ushort *)(iVar4 + 0xc066b8)) &&
             (*(ushort *)(iVar4 + 0xc068b8) == uRam00c00110)) {
            sRam00401348 = sRam00401348 + 1;
          }
          sVar1 = sVar1 + 1;
          iVar4 = iVar4 + 2;
        } while (sVar1 < 0x80);
        func_0x00396a98(0x42,0x107f300,uRam00c00110);
        if ((byte)(cRam003ed3c0 - 4U) < 2) {
          func_0x003979d8(1,0x104,8);
          func_0x001475c0(8,4);
          func_0x001475c0(4,5);
          uRam003ecf72 = 5;
        }
        else {
          sRam003ed41c = 0;
        }
        bRam003eeacc = 2;
      }
    }
    else {
      if ((((bRam003eeacc == 2) && (sRam003ed41c == 0)) && ((bRam00401300 & 8) != 0)) &&
         (((bRam00401318 & 8) != 0 && ((bRam00401330 & 8) != 0)))) {
        bRam003eeacc = 3;
        bRam003eee60 = bRam003eee60 | 1;
      }
    }
  }
  func_0x00382ab8(7,0);
  func_0x003b9a08(1,1,(int)((uRam00401342 + 8) * 0x10000) >> 0x10,
                  (int)((uRam00401344 + 8) * 0x10000) >> 0x10,0x3eea98,0x3eeab0,0x3eeac0,
                  bRam00401354);
  if ((bRam003eeacc == 3) && ((short)uRam003eee5c < (short)(ushort)bRam00401354)) {
    if (uRam003eee5c - 2 < 2) {
      func_0x003979d8(0xffffffffffffffff);
      uRam003ecf72 = 1;
    }
    else {
      sRam003ed41c = 0;
    }
    bRam00401300 = bRam00401300 & 0xf1 | 4;
    bRam00401318 = bRam00401318 & 0xf1 | 4;
    bRam00401330 = bRam00401330 & 0xf1 | 4;
    bRam003eee60 = bRam003eee60 & 0xfe;
    uRam00401314 = uRam0040130c;
    bRam00401456 = (byte)uRam003eee5c;
    uRam0040132c = uRam00401324;
    bRam00401457 = bRam003eee58;
    uRam00401344 = uRam0040133c;
    uRam00401312 = uRam0040130a;
    bRam003eeacc = 1;
    uRam0040132a = uRam00401322;
    uRam00401342 = uRam0040133a;
  }
  func_0x00382ad8((int)((uRam00401312 + 4) * 0x10000) >> 0x10,uRam00401314);
  func_0x00129d68(2);
  func_0x00382b60(1,1,0x3fd3f0,uRam00c0011c,uRam00c0011e,uRam00c00120);
  func_0x00397c08(0,0x401300,uRam003ecf48);
  func_0x00382ad8((int)((uRam0040132a + 4) * 0x10000) >> 0x10,
                  (int)((uRam0040132c + 2) * 0x10000) >> 0x10);
  if ((uRam00c00110 == 1) || (uRam00c00110 == 7)) {
    uVar3 = func_0x001383d0(*(undefined4 *)((uint)uRam00c00110 * 4 + 0x3e82e0));
    func_0x00382b60(1,1,0x3fd400,uVar3);
  }
  else {
    uVar3 = func_0x001383d0(*(undefined4 *)((uint)uRam00c00110 * 4 + 0x3e82e0));
    func_0x00382b60(1,1,0x3fd408,uVar3);
  }
  func_0x00382ad8((int)((uRam0040132a + 4) * 0x10000) >> 0x10,
                  (int)((uRam0040132c + 0x16) * 0x10000) >> 0x10);
  uVar3 = func_0x001383d0(*(undefined4 *)((uint)uRam00c00110 * 4 + 0x3e78c0));
  func_0x00382b60(1,1,0x3fd410,uVar3);
  func_0x00382ad8((int)((uRam0040132a + 0x48) * 0x10000) >> 0x10,
                  (int)((uRam0040132c + 0x36) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd420);
  func_0x00382ad8((int)((uRam0040132a + 0x5a) * 0x10000) >> 0x10,
                  (int)((uRam0040132c + 0x42) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd430,sRam00401346);
  func_0x00382ad8((int)((uRam0040132a + 0x48) * 0x10000) >> 0x10,
                  (int)((uRam0040132c + 0x4e) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd438);
  func_0x00382ad8((int)((uRam0040132a + 0x60) * 0x10000) >> 0x10,
                  (int)((uRam0040132c + 0x5a) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd440,sRam00401348);
  func_0x00382ad8((int)((uRam0040132a + 4) * 0x10000) >> 0x10,
                  (int)((uRam0040132c + 0x6e) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd448,sRam0040134a);
  func_0x00382ad8((int)((uRam0040132a + 4) * 0x10000) >> 0x10,
                  (int)((uRam0040132c + 0x7a) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd460,sRam0040134c);
  func_0x00382ad8((int)((uRam0040132a + 4) * 0x10000) >> 0x10,
                  (int)((uRam0040132c + 0x86) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd478,sRam0040134e);
  func_0x00397c08(0,0x401318,uRam003ecf48);
  func_0x00397c08(0,0x401330,uRam003ecf48);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0003ff10(void)

{
  long lVar1;
  
  if (bRam003eeb0f == 1) {
    if ((((bRam00401388 & 8) != 0) && ((bRam004013a0 & 8) != 0)) && ((bRam003ed480 & 8) != 0)) {
      uRam003eec92 = 0xff;
      bRam003eeb0f = 0;
    }
  }
  else {
    if (bRam003eeb0f < 2) {
      if (bRam003eeb0f == 0) {
        func_0x00383000(7);
        func_0x00396e38(0x3eec88,2,0,2,0x7fff);
        func_0x00396e58(1,0x3fd490,0x3fd500);
        bRam003eec90 = bRam003eec90 & 0xfe;
        func_0x00397b30(0x401388,1,4,1,0x160,0x20,0xf0,0x20);
        func_0x00397b30(0x4013a0,1,0,1,0xf0,0xffffffffffffffc0,0xf0,0x10);
        func_0x00396a98(0x53,0x107ea00);
        func_0x003bba50(0,0,0xfffffffffffffed4,0xa0,400);
        uRam004013b6 = func_0x00396fb8(0x53,0x17,1,(undefined)uRam00c00112);
        bRam003eeb0f = 2;
      }
    }
    else {
      if (((bRam003eeb0f == 2) && ((bRam00401388 & 8) != 0)) &&
         (((bRam004013a0 & 8) != 0 && ((bRam003ed480 & 8) != 0)))) {
        bRam003ed47c = bRam003ed47c & 0xfd;
        bRam003eec90 = bRam003eec90 | 1;
        bRam003eeb0f = 3;
      }
    }
  }
  lVar1 = func_0x00396ef8(uRam0040139a,uRam0040139c);
  if (bRam003eeb0f == 3) {
    if (lVar1 == 1) {
      bRam003eec90 = bRam003eec90 & 0xfe;
      bRam00401388 = bRam00401388 & 0xf1 | 4;
      bRam004013a0 = bRam004013a0 & 0xf1 | 4;
      bRam003ed480 = bRam003ed480 & 0xf1 | 4;
      uRam0040139c = uRam00401394;
      bRam003eeb0f = 1;
      uRam004013b4 = uRam004013ac;
      uRam003ed494 = uRam003ed48c;
      uRam003eec8c = 0x3fff;
      uRam0040139a = uRam00401392;
      uRam004013b2 = uRam004013aa;
      uRam003ed492 = uRam003ed48a;
    }
    else {
      if (lVar1 == 2) {
        (*pcRam00c00068)();
        bRam003eec90 = bRam003eec90 & 0xfe;
        bRam00401388 = bRam00401388 & 0xf1 | 4;
        bRam004013a0 = bRam004013a0 & 0xf1 | 4;
        bRam003ed480 = bRam003ed480 & 0xf1 | 4;
        uRam0040139c = uRam00401394;
        bRam003eeb0f = 1;
        uRam004013b4 = uRam004013ac;
        uRam003ed494 = uRam003ed48c;
        uRam0040139a = uRam00401392;
        uRam004013b2 = uRam004013aa;
        uRam003ed492 = uRam003ed48a;
        uRam003eec8c = 0;
      }
    }
  }
  func_0x00382ab8(7,0);
  func_0x00382ad8((int)((uRam004013b2 + 4) * 0x10000) >> 0x10,uRam004013b4);
  func_0x00129d68(2);
  func_0x00382b60(1,1,0x3fd3f0,uRam00c0011c,uRam00c0011e,uRam00c00120);
  func_0x00397c08(0,0x4013a0,uRam003ecf48);
  func_0x00397c08(0,0x401388,uRam003ecf48);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00040368(void)

{
  byte bVar1;
  short sVar2;
  int iVar3;
  int iVar4;
  
  bVar1 = bRam003eeb10;
  if (bRam003eeb10 == 1) {
    bRam003eeb10 = bVar1;
    if (sRam003ed41c == 0) {
      uRam003eeca2 = 0xff;
      bRam003eeb10 = 0;
    }
  }
  else {
    if (bRam003eeb10 < 2) {
      bRam003eeb10 = bVar1;
      if (bRam003eeb10 == 0) {
        func_0x00383000(7);
        func_0x00396e38(0x3eec98,2,0,2,0x7fff,0);
        bRam003eeca0 = bRam003eeca0 & 0xfe;
        func_0x00396a98(0x53,0x107ea00,uRam00c00112);
        sRam004013ba = 0;
        bRam003eeb10 = 2;
      }
    }
    else {
      bRam003eeb10 = bVar1;
      if (((bRam003eeb10 == 2) && ((bRam003ed480 & 8) != 0)) && ((bRam003ed47c & 2) == 0)) {
        sRam004013ba = func_0x003c0ee8(sRam004013ba);
        if (sRam004013ba < 0xab) {
          (*pcRam00c00054)(0,10);
          uRam003ecf4f = (undefined)sRam004013ba;
          func_0x003bba50(0,0,0xfffffffffffffed4,0xa0,400,0xa0);
          uRam004013b8 = func_0x00396fb8(0x53,0x16,1,(undefined)uRam00c00112);
          bRam003eeb10 = bVar1;
        }
        else {
          func_0x003c11d8();
          iVar4 = 0;
          sVar2 = 0;
          iVar3 = 0;
          do {
            if ((0x17 < *(ushort *)(iVar3 + 0xc066b8)) &&
               (*(short *)(iVar3 + 0xc068b8) == sRam00c00110)) {
              iVar4 = (iVar4 + 1) * 0x10000 >> 0x10;
            }
            sVar2 = sVar2 + 1;
            iVar3 = iVar3 + 2;
          } while (sVar2 < 0x80);
          if ((int)(uint)uRam00c082de < iVar4) {
            uRam00c082de = (ushort)iVar4;
          }
          func_0x003979d8(0xffffffffffffffff,0x104,8);
          bRam003eeb10 = 1;
          uRam003ecf72 = 1;
          uRam003eec9c = 0;
        }
      }
    }
  }
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Removing unreachable block (ram,0x00040b24)
// WARNING: Removing unreachable block (ram,0x00041020)

void FUN_000408d8(void)

{
  short sVar1;
  short sVar2;
  short sVar3;
  short *psVar4;
  int iVar5;
  int iVar6;
  long lVar7;
  short *psVar8;
  undefined2 *puVar9;
  ushort *puVar10;
  int iVar11;
  uint uVar12;
  int iVar13;
  int *piVar14;
  short *psVar15;
  short local_1e0 [7];
  short local_1d2 [177];
  short local_70 [8];
  short local_60 [8];
  
  puVar9 = (undefined2 *)0xc00130;
  iVar11 = 0;
  iVar13 = 0;
  do {
    lVar7 = func_0x003d05d8(iVar11,uRam00c00110);
    if (lVar7 != 1) {
      lVar7 = func_0x003d0598(iVar11,puVar9[0xab]);
      if ((lVar7 == 1) &&
         (sVar1 = puVar9[0x1a0d], puVar9[0x1a0d] = sVar1 + -1, (short)(sVar1 + -1) == 0)) {
        if ((ushort)puVar9[0xab] < 8) {
          if (puVar9[0xab] == *(ushort *)((uint)(ushort)puVar9[0xab] * 2 + 0xc00286)) {
            *(undefined2 *)(iVar13 + 0xc00130) = 8;
            *(undefined2 *)(iVar13 + 0xc007de) =
                 *(undefined2 *)((uint)*(ushort *)(iVar13 + 0xc00286) * 2 + 0xc007de);
          }
          else {
            func_0x003d0848(iVar11);
            func_0x003d0a90();
            *puVar9 = 2;
          }
        }
        else {
          func_0x003d0848(iVar11);
          func_0x003d0a90();
          *(undefined2 *)(iVar13 + 0xc00130) = 2;
        }
      }
    }
    puVar9 = puVar9 + 1;
    iVar11 = (iVar11 + 1) * 0x10000 >> 0x10;
    iVar13 = iVar13 + 2;
  } while (iVar11 < 0xab);
  iVar11 = 7;
  psVar4 = local_1d2;
  do {
    iVar11 = iVar11 + -1;
    *psVar4 = 0;
    psVar4 = psVar4 + -1;
  } while (-1 < iVar11);
  sVar1 = 0;
  puVar10 = (ushort *)0xc00286;
  do {
    if (*puVar10 < 8) {
      local_1e0[*puVar10] = local_1e0[*puVar10] + 1;
    }
    sVar1 = sVar1 + 1;
    puVar10 = puVar10 + 1;
  } while (sVar1 < 0xab);
  uVar12 = 0;
  sVar1 = local_1e0[uRam00c00110];
  func_0x003cca28();
  if (0 < sRam003f02c0) {
    piVar14 = (int *)0x3eeb18;
    iVar11 = 0;
    do {
      if (((uVar12 != uRam00c00110) && (*(ushort *)(iVar11 + 0xc00286) == uVar12)) &&
         (1 < (int)sVar1 - (int)*(short *)((int)local_1e0 + iVar11))) {
        iVar13 = func_0x001881e8();
        iVar5 = func_0x001881e8();
        if ((iVar13 % 10) * 10 + iVar5 % 10 <= *piVar14) {
          iVar13 = func_0x001881e8();
          if (sRam003f02c0 == 0) {
            trap(7);
          }
          sVar2 = *(short *)((iVar13 % (int)sRam003f02c0) * 2 + 0x3f0168);
          if (0x27 < sVar2) {
            func_0x003d0848();
            func_0x003d0650(sVar2,uVar12);
            func_0x003cca28();
          }
        }
      }
      piVar14 = piVar14 + 1;
      iVar11 = iVar11 + 2;
      uVar12 = SEXT24((short)((short)uVar12 + 1));
    } while ((0 < sRam003f02c0) && ((int)uVar12 < 8));
  }
  psVar4 = local_60;
  iVar11 = 0;
  do {
    iVar13 = (iVar11 + 1) * 0x10000 >> 0x10;
    local_70[iVar11] = 0;
    psVar4[iVar11] = 0;
    iVar11 = iVar13;
  } while (iVar13 < 8);
  iVar11 = 0;
  psVar15 = local_1d2 + 1;
  iVar13 = 0;
  puVar10 = (ushort *)0xc00286;
  do {
    lVar7 = func_0x003977c0(*(char *)(puVar10 + 0x357) + -1);
    if (10 < lVar7) {
      lVar7 = 0;
    }
    iVar11 = (iVar11 + 1) * 0x10000 >> 0x10;
    *(short *)((int)psVar15 + iVar13) = (short)lVar7;
    iVar13 = iVar13 + 2;
    if (*puVar10 < 8) {
      psVar8 = local_70 + *puVar10;
      *psVar8 = (short)lVar7 + *psVar8;
    }
    puVar10 = puVar10 + 1;
  } while (iVar11 < 0xab);
  sVar1 = 0;
  puVar10 = (ushort *)0xc00286;
  do {
    lVar7 = func_0x003d05d8(sVar1);
    if (((lVar7 != 1) && (*puVar10 < 8)) &&
       (0 < (int)((uint)puVar10[0x1560] - (uint)puVar10[0x160b]))) {
      sVar2 = func_0x003c1030(sVar1);
      psVar4[*puVar10] = psVar4[*puVar10] + sVar2;
    }
    sVar1 = sVar1 + 1;
    puVar10 = puVar10 + 1;
  } while (sVar1 < 0xab);
  uVar12 = 0;
  iVar11 = 0;
  sVar1 = local_1e0[uRam00c00110];
  sVar2 = local_70[uRam00c00110];
  if (sVar1 == 0) {
    trap(7);
  }
  do {
    if ((uVar12 != uRam00c00110) && (*(ushort *)(iVar11 + 0xc00286) == uVar12)) {
      if (*(short *)((int)local_1e0 + iVar11) == 0) {
        trap(7);
      }
      if (*(short *)((int)local_70 + iVar11) / *(short *)((int)local_1e0 + iVar11) < sVar2 / sVar1)
      {
        iVar13 = 0;
        do {
          lVar7 = func_0x003d05d8(iVar13);
          if (lVar7 == 1) {
            sVar3 = func_0x003c1030(iVar13);
            *(short *)((int)psVar4 + iVar11) = *(short *)((int)psVar4 + iVar11) + sVar3;
          }
          iVar13 = (iVar13 + 1) * 0x10000 >> 0x10;
        } while (iVar13 < 0xab);
      }
    }
    uVar12 = SEXT24((short)((short)uVar12 + 1));
    iVar11 = iVar11 + 2;
  } while ((int)uVar12 < 8);
  uVar12 = 0;
  iVar11 = 0;
  do {
    if (((uVar12 != uRam00c00110) && (*(ushort *)(iVar11 + 0xc00286) == uVar12)) &&
       (*(short *)((int)psVar15 + iVar11) < psVar15[uRam00c00110])) {
      sVar1 = func_0x003c1030();
      *(short *)((int)psVar4 + iVar11) = *(short *)((int)psVar4 + iVar11) + sVar1;
    }
    uVar12 = SEXT24((short)((short)uVar12 + 1));
    iVar11 = iVar11 + 2;
  } while ((int)uVar12 < 8);
  iVar11 = 0;
  uVar12 = 0;
  do {
    if (((uVar12 != uRam00c00110) && (*(ushort *)(iVar11 + 0xc00286) == uVar12)) &&
       (iVar13 = 0, 0 < *psVar4)) {
      do {
        lVar7 = func_0x003d05d8(iVar13,uVar12);
        if (lVar7 == 1) {
          func_0x003c1030(iVar13);
          *psVar4 = *psVar4 + -1;
        }
        iVar13 = (iVar13 + 1) * 0x10000 >> 0x10;
      } while ((0 < *psVar4) && (iVar13 < 0xab));
    }
    psVar4 = psVar4 + 1;
    uVar12 = SEXT24((short)((short)uVar12 + 1));
    iVar11 = iVar11 + 2;
  } while ((int)uVar12 < 8);
  iVar13 = 0;
  iVar11 = 0;
  puVar10 = (ushort *)0xc00532;
  do {
    lVar7 = func_0x003d05d8(iVar11,uRam00c00110);
    if ((lVar7 == 1) && (iVar13 < (int)(uint)*puVar10)) {
      iVar13 = (int)(short)*puVar10;
    }
    iVar11 = (iVar11 + 1) * 0x10000 >> 0x10;
    puVar10 = puVar10 + 1;
  } while (iVar11 < 0xab);
  sVar1 = 0;
  psVar4 = (short *)0xc02bf0;
  do {
    lVar7 = func_0x003d05d8(sVar1);
    if (((lVar7 != 1) && ((ushort)psVar4[-0x135f] < 0x32)) &&
       ((int)(uint)(ushort)psVar4[-0x135f] < iVar13 + -3)) {
      iVar11 = (int)psVar4[-0x14b5];
      if (7 < (ushort)psVar4[-0x14b5]) {
        iVar11 = 8;
      }
      iVar5 = func_0x001881e8();
      iVar6 = func_0x001881e8();
      if ((iVar5 % 10) * 10 + iVar6 % 10 < *(int *)(iVar11 * 4 + 0x3eeb38)) {
        *psVar4 = *psVar4 + (*(short *)((uint)(ushort)psVar4[-0x135f] * 2 + 0x3e7f82) -
                            *(short *)((uint)(ushort)psVar4[-0x135f] * 2 + 0x3e7f80));
      }
    }
    sVar1 = sVar1 + 1;
    psVar4 = psVar4 + 1;
  } while (sVar1 < 0xab);
  iVar11 = 0;
  puVar9 = (undefined2 *)0xc02e9c;
  do {
    lVar7 = func_0x003d05d8(iVar11);
    if (lVar7 != 1) {
      puVar9[-0x603] = puVar9[-0x759];
      puVar9[-0x558] = puVar9[-0x6ae];
      *puVar9 = puVar9[-0xab];
    }
    iVar11 = (iVar11 + 1) * 0x10000 >> 0x10;
    puVar9 = puVar9 + 1;
  } while (iVar11 < 0xab);
  return;
}



// WARNING: Removing unreachable block (ram,0x00041ac4)
// WARNING: Removing unreachable block (ram,0x00041b6c)

void FUN_00041210(void)

{
  short sVar1;
  short sVar2;
  undefined8 uVar3;
  short sVar4;
  uint uVar5;
  long lVar6;
  long lVar7;
  int iVar8;
  int iVar9;
  int iVar10;
  short *psVar11;
  int iVar12;
  
  if (9 < bRam003eeb5c) {
    func_0x00382ab8(7,0);
    func_0x003b9a08(4,4,(int)((uRam004013d2 + 8) * 0x10000) >> 0x10,
                    (int)((uRam004013d4 + 0x26) * 0x10000) >> 0x10,0x3e7ed0);
    if (bRam003eeb5c == 3) {
      lVar6 = (long)sRam003eee7c;
      lVar7 = (long)(sRam0040150e + -2);
      if (lVar6 < lVar7) {
        bRam00401510 = (byte)sRam003eee7c;
        sRam0040150c = *(short *)((uint)(byte)sRam003eee7c * 2 + 0x401460);
        bRam003eee80 = bRam003eee80 & 0xfe;
        *(undefined2 *)((short)uRam0040141c * 2 + 0x401410) =
             *(undefined2 *)(((int)(short)uRam0040141c + 1U & 1) * 2 + 0x401410);
        cRam00401511 = (char)sRam003eee78;
        sRam003eee7c = 0;
        bRam003eeb5c = 7;
      }
      else {
        if (lVar6 == lVar7) {
          *(undefined2 *)((short)uRam0040141c * 2 + 0x401410) =
               *(undefined2 *)(((int)(short)uRam0040141c + 1U & 1) * 2 + 0x401410);
          bRam003eee80 = bRam003eee80 & 0xfe;
          bRam004013c0 = bRam004013c0 & 0xf1 | 4;
          uRam004013d4 = uRam004013cc;
          bRam003eeb5c = 4;
          uRam004013d2 = uRam004013ca;
          sRam00401418 = sRam00401410 / 0xf + 1;
        }
        else {
          if ((lVar6 == sRam0040150e + -1) || (lVar6 == 0x3fff)) {
            *(undefined2 *)((short)uRam0040141c * 2 + 0x401410) =
                 *(undefined2 *)(((int)(short)uRam0040141c + 1U & 1) * 2 + 0x401410);
            sRam00401418 = sRam00401410 / 0xf + 1;
            func_0x003979d8(0xffffffffffffffff,0x108,8);
            bRam004013c0 = bRam004013c0 & 0xf1 | 4;
            bRam003eee80 = bRam003eee80 & 0xfe;
            bRam00401510 = (char)sRam003eee7e + (char)sRam003eee78;
            cRam00401511 = (char)sRam003eee78;
            bRam003eeb5c = 1;
            uRam004013d4 = uRam004013cc;
            uRam003ecf72 = 1;
            sRam003eee7c = 0x3fff;
            uRam004013d2 = uRam004013ca;
          }
        }
      }
    }
    func_0x00382ad8((int)((uRam004013d2 + 0x10) * 0x10000) >> 0x10,
                    (int)((uRam004013d4 + 2) * 0x10000) >> 0x10);
    func_0x00382b60(4,1,0x3fd3c0);
    sRam003ed144 = uRam004013d2 + 0x40;
    sRam003ed146 = uRam004013d4 + 0x1a;
    func_0x00383a90(4,0x3ed138);
    func_0x00397c08(3,0x4013c0);
    if (iRam00401420 == 1) {
      sVar1 = *(short *)(sRam0040150c * 2 + 0xc068b8);
      if (sVar1 < 0xab) {
        func_0x00382ad8((int)((uRam004013ea + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam004013ec + 6) * 0x10000) >> 0x10);
        func_0x00382b60(4,1,*(undefined4 *)(sRam0040150c * 4 + 0x3e7e30));
        func_0x00382ad8((int)((uRam004013ea + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam004013ec + 0x1e) * 0x10000) >> 0x10);
        if (sVar1 < 8) {
          uVar3 = func_0x001383d0(*(undefined4 *)(sVar1 * 4 + 0x3e82e0));
        }
        else {
          uVar3 = func_0x001383d0(0x3fd520);
        }
        func_0x00382b60(4,1,0x3fd510,uVar3);
        func_0x00382ad8((int)((uRam00401402 + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam00401404 + 6) * 0x10000) >> 0x10);
        uVar3 = func_0x001383d0(*(undefined4 *)(sVar1 * 4 + 0x3e78c0));
        func_0x00382b60(4,1,0x3fd528,uVar3);
        func_0x00382ad8((int)((uRam00401402 + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam00401404 + 0x16) * 0x10000) >> 0x10);
        uVar3 = func_0x001383d0(*(undefined4 *)(sRam00401406 * 4 + 0x3e78c0));
        func_0x00382b60(4,1,0x3fd538,uVar3);
        func_0x00382ad8((int)((uRam00401402 + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam00401404 + 0x2e) * 0x10000) >> 0x10);
        func_0x00382b60(4,1,0x3fd548,sRam0040140c);
        func_0x00382ad8((int)((uRam00401402 + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam00401404 + 0x3e) * 0x10000) >> 0x10);
        func_0x00382b60(4,1,0x3fd558,uRam00401408,10);
        func_0x00382ad8((int)((uRam00401402 + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam00401404 + 0x4e) * 0x10000) >> 0x10);
        func_0x00382b60(4,1,0x3fd570,*(undefined2 *)(sRam0040150c * 2 + 0xc08208),
                        *(undefined2 *)(sRam0040140c * 2 + 0x3e8510));
        func_0x00382ad8((int)((uRam00401402 + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam00401404 + 0x5e) * 0x10000) >> 0x10);
        func_0x00382b60(4,1,0x3fd588);
      }
      else {
        func_0x00382ad8((int)((uRam004013ea + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam004013ec + 6) * 0x10000) >> 0x10);
        func_0x00382b60(4,1,*(undefined4 *)(sRam0040150c * 4 + 0x3e7e30));
        func_0x00382ad8((int)((uRam004013ea + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam004013ec + 0x1e) * 0x10000) >> 0x10);
        func_0x00382b60(4,1,0x3fd5a0);
        func_0x00382ad8((int)((uRam00401402 + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam00401404 + 6) * 0x10000) >> 0x10);
        func_0x00382b60(4,1,0x3fd5b8);
        func_0x00382ad8((int)((uRam00401402 + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam00401404 + 0x16) * 0x10000) >> 0x10);
        func_0x00382b60(4,1,0x3fd5d0);
        func_0x00382ad8((int)((uRam00401402 + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam00401404 + 0x2e) * 0x10000) >> 0x10);
        func_0x00382b60(4,1,0x3fd548,sRam0040140c);
        func_0x00382ad8((int)((uRam00401402 + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam00401404 + 0x3e) * 0x10000) >> 0x10);
        func_0x00382b60(4,1,0x3fd5e8,10);
        func_0x00382ad8((int)((uRam00401402 + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam00401404 + 0x4e) * 0x10000) >> 0x10);
        func_0x00382b60(4,1,0x3fd600);
        func_0x00382ad8((int)((uRam00401402 + 0x10) * 0x10000) >> 0x10,
                        (int)((uRam00401404 + 0x5e) * 0x10000) >> 0x10);
        func_0x00382b60(4,1,0x3fd618);
      }
      func_0x00397c08(3,0x4013d8,uRam003ecf48);
      func_0x00397c08(3,0x4013f0);
    }
    psVar11 = (short *)0x3ef528;
    lVar6 = 0;
    iVar10 = 0;
    iVar12 = 0;
    do {
      if (0x17 < *(ushort *)(iVar12 + 0xc066b8)) {
        if (*(ushort *)(iVar12 + 0xc068b8) < 8) {
          iVar9 = (int)(short)*(ushort *)(iVar12 + 0xc068b8);
        }
        else {
          iVar9 = 8;
        }
        if (((int)sRam003eee7e + (int)sRam003eee78 < sRam0040150e + -2) &&
           (lVar6 == *(short *)(((int)sRam003eee7e + (int)sRam003eee78) * 2 + 0x401460))) {
          iVar8 = iVar9 * 0x20;
          sVar1 = psVar11[1];
          sVar4 = *psVar11 - *(short *)((short)uRam0040141c * 2 + 0x401410);
          *(undefined2 *)(iVar8 + 0x3ef408) = 1;
          *(short *)(iVar8 + 0x3ef414) = sVar4 + -8;
          *(short *)(iVar8 + 0x3ef416) = sVar1 + -10;
          *(short *)(iVar8 + 0x3ef41c) = sVar4 + 0x17;
          *(short *)(iVar8 + 0x3ef41e) = sVar1 + 6;
        }
        else {
          iVar8 = iVar9 * 0x20;
          sVar1 = *psVar11;
          sVar4 = psVar11[1];
          sVar2 = *(short *)((short)uRam0040141c * 2 + 0x401410);
          *(undefined2 *)(iVar8 + 0x3ef408) = 0;
          *(short *)(iVar8 + 0x3ef414) = (sVar1 - sVar2) + -4;
          *(short *)(iVar8 + 0x3ef416) = sVar4 + -4;
        }
        func_0x00383a90(2);
        if (iVar9 < 8) {
          uRam003ee8f2 = *(undefined2 *)(iVar10 + 0x3ee90c);
          uRam003ee8ee = (short)iVar9 * 0x10 + 0x400U | 0x2000;
          uRam003ee8f0 = *(undefined2 *)(iVar10 + 0x3ee90a);
          uRam003ee8f6 = *(undefined2 *)(iVar10 + 0x3ee910);
          sRam003ee8f4 = *(short *)(iVar10 + 0x3ee90e) -
                         *(short *)((short)uRam0040141c * 2 + 0x401410);
          func_0x00383a90(1,0x3ee8e8);
        }
      }
      iVar10 = iVar10 + 10;
      psVar11 = psVar11 + 2;
      lVar6 = (long)(int)(short)((short)lVar6 + 1);
      iVar12 = iVar12 + 2;
    } while (lVar6 < 0x80);
    uVar5 = uRam0040141c + 1 & 1;
    uRam0020d084 = 1;
    iRam0020d1c0 = (int)((float)(int)*(short *)((uVar5 ^ 1) * 2 + 0x401410) * 65536.0);
    uRam0040141c = (short)uVar5;
    return;
  }
                    // WARNING: Could not emulate address calculation at 0x00041258
                    // WARNING: Treating indirect jump as call
  (**(code **)((uint)bRam003eeb5c * 4 + 0x3fd630))();
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00042418(void)

{
  int iVar1;
  int iVar2;
  long lVar3;
  
  iVar1 = *(int *)((uint)uRam00c00100 * 4 + 0x3eeb60);
  if (bRam003eeb80 == 1) {
    if ((sRam003ed41c == 0) && ((bRam00401428 & 8) != 0)) {
      uRam003eee92 = 0xff;
      bRam003eeb80 = 0;
      func_0x001392a0();
    }
  }
  else {
    if (bRam003eeb80 < 2) {
      if (bRam003eeb80 == 0) {
        func_0x00383000(7);
        func_0x00396e38(0x3eee88,2,0,2,0x7fff,0);
        func_0x00397b30(0x401428,1,0x26,1,0x70,0x100,0x70,0x68);
        bRam003eeb80 = 2;
        uRam003eeb88 = *(ushort *)(iRam003eeb84 * 0x24 + iVar1 + 0x1c) & 0xfff;
      }
    }
    else {
      if (bRam003eeb80 == 2) {
        if ((uRam002afcba & 0x8000) == 0) {
          if ((uRam002afcba & 0x4000) == 0) {
            if ((uRam002afcb2 & 0x100) == 0) {
              if ((uRam002afcb2 & 0x600) != 0) {
                lVar3 = func_0x003de7c8();
                if (lVar3 == 1) {
                  func_0x003979d8(1,0x104,8);
                  uRam003ecf72 = 5;
                }
                else {
                  sRam003ed41c = 0;
                }
                bRam00401428 = bRam00401428 & 0xf1 | 4;
                bRam003eeb80 = 1;
                uRam0040143c = uRam00401434;
                uRam003eee8c = 0;
                uRam0040143a = uRam00401432;
              }
            }
            else {
              bRam00401428 = bRam00401428 & 0xf1 | 4;
              uRam003eee8c = 0x3fff;
              bRam003eeb80 = 1;
              uRam0040143c = uRam00401434;
              sRam003ed41c = 0;
              uRam0040143a = uRam00401432;
            }
            goto LAB_00042710;
          }
          if (iRam003eeb84 < 1) goto LAB_00042710;
          iVar2 = iRam003eeb84 + -1;
        }
        else {
          iVar2 = iRam003eeb84 + 1;
          if (*(short *)(iRam003eeb84 * 0x24 + iVar1 + 0x40) == -1) goto LAB_00042710;
        }
        uRam003eeb88 = *(ushort *)(iVar2 * 0x24 + iVar1 + 0x1c) & 0xfff;
        iRam003eeb84 = iVar2;
      }
    }
  }
LAB_00042710:
  func_0x00382ab8(7,0);
  func_0x00382ad8((int)((uRam0040143a + 0x20) * 0x10000) >> 0x10,
                  (int)((uRam0040143c + 8) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd658,uRam003eeb88);
  if (0 < iRam003eeb84) {
    sRam003ed004 = uRam0040143a + 0xc;
    sRam003ed006 = uRam0040143c + 0xc;
    func_0x00383a90(1,0x3ecff8);
  }
  if (*(short *)(iRam003eeb84 * 0x24 + iVar1 + 0x40) != -1) {
    sRam003ed024 = uRam0040143a + 0x4c;
    sRam003ed026 = uRam0040143c + 0xc;
    func_0x00383a90(1,0x3ed018);
  }
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_00042818(void)

{
  long lVar1;
  
  lVar1 = func_0x003de838();
  if (lVar1 != 0) {
    uRam003ecf72 = 4;
    uRam003eee9c = 0x3fff;
    uRam003eeea2 = 0xff;
  }
  return;
}



void FUN_00042860(void)

{
  bool bVar1;
  ushort uVar2;
  undefined4 uVar3;
  int iVar4;
  undefined8 uVar5;
  long lVar6;
  ushort *puVar7;
  uint uVar8;
  undefined4 *puVar9;
  int iVar10;
  undefined auStack144 [64];
  
  if (bRam003eebc4 == 1) {
    if ((sRam003ed41c == 0) && ((bRam00401440 & 8) != 0)) {
      uRam003eeeb2 = 0xff;
      bRam003eebc4 = 0;
    }
  }
  else {
    if (bRam003eebc4 < 2) {
      if (bRam003eebc4 == 0) {
        func_0x00383000(7);
        func_0x00396e38(0x3eeea8,2,0,2,0x7fff,0);
        func_0x00397b30(0x401440,1,0x27,1,8,0x100,8,8);
        bRam003eebc4 = 2;
      }
    }
    else {
      if (bRam003eebc4 == 2) {
        if ((uRam002afcb2 & 0x700) == 0) {
          if ((uRam002afcba & 0x4000) == 0) {
            if ((uRam002afcba & 0x8000) == 0) {
              if ((uRam002afcb2 & 8) == 0) {
                if ((uRam002afcb2 & 0x80) != 0) {
                  sRam003eebc6 = 0xaa;
                }
              }
              else {
                sRam003eebc6 = 0;
              }
            }
            else {
              bVar1 = 0xa9 < sRam003eebc6;
              sRam003eebc6 = sRam003eebc6 + 1;
              if (bVar1) {
                sRam003eebc6 = 0xaa;
              }
            }
          }
          else {
            bVar1 = sRam003eebc6 < 1;
            sRam003eebc6 = sRam003eebc6 + -1;
            if (bVar1) {
              sRam003eebc6 = 0;
            }
          }
        }
        else {
          bRam00401440 = bRam00401440 & 0xf1 | 4;
          uRam003eeeac = 0x3fff;
          bRam003eebc4 = 1;
          uRam00401454 = uRam0040144c;
          sRam003ed41c = 0;
          uRam00401452 = uRam0040144a;
        }
      }
    }
  }
  func_0x00382ab8(7,0);
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x10) * 0x10000) >> 0x10);
  uVar5 = func_0x001383d0(*(undefined4 *)(sRam003eebc6 * 4 + 0x3e7b78));
  func_0x00382b60(1,0,0x3fd6d0,sRam003eebc6,uVar5);
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x18) * 0x10000) >> 0x10);
  if (*(short *)(sRam003eebc6 * 2 + 0xc043fc) == 1) {
    uVar5 = func_0x001383d0(0x3fd6f0);
  }
  else {
    uVar5 = func_0x001383d0(0x3fd6f8);
  }
  func_0x00382b60(1,0,0x3fd6e0,uVar5);
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x20) * 0x10000) >> 0x10);
  uVar5 = func_0x001383d0(*(undefined4 *)
                           ((uint)*(ushort *)(sRam003eebc6 * 2 + 0xc00130) * 4 + 0x3eeb90));
  func_0x00382b60(1,0,0x3fd700,uVar5);
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x28) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd710,*(undefined2 *)(sRam003eebc6 * 2 + 0xc0354a));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x30) * 0x10000) >> 0x10);
  uVar5 = func_0x001383d0(*(undefined4 *)
                           ((uint)*(ushort *)(sRam003eebc6 * 2 + 0xc003dc) * 4 + 0x3eebb8));
  func_0x00382b60(1,0,0x3fd720,uVar5);
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x38) * 0x10000) >> 0x10);
  uVar5 = func_0x003d12d8(sRam003eebc6);
  uVar5 = func_0x001383d0(uVar5);
  func_0x00382b60(1,0,0x3fd730,uVar5);
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x40) * 0x10000) >> 0x10);
  uVar5 = func_0x001383d0(*(undefined4 *)
                           ((uint)*(ushort *)(sRam003eebc6 * 2 + 0xc00286) * 4 + 0x3e7b78));
  func_0x00382b60(1,0,0x3fd740,*(undefined2 *)(sRam003eebc6 * 2 + 0xc00286),uVar5);
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x48) * 0x10000) >> 0x10);
  uVar5 = func_0x003d0d08(sRam003eebc6);
  func_0x00382c20(1,0,0x3fd750,uVar5);
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x50) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd760,*(undefined *)(sRam003eebc6 * 2 + 0xc01e94));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x58) * 0x10000) >> 0x10);
  iVar4 = sRam003eebc6 * 2;
  func_0x00382b60(1,0,0x3fd770,*(undefined2 *)(iVar4 + 0xc00e8c),*(undefined2 *)(iVar4 + 0xc00fe2),
                  *(undefined2 *)(iVar4 + 0xc01138));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x60) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd780,*(undefined2 *)(sRam003eebc6 * 2 + 0xc0128e),
                  *(undefined2 *)(sRam003eebc6 * 2 + 0xc013e4));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x68) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd790,*(undefined2 *)(sRam003eebc6 * 2 + 0xc0153a));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x70) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd7a0,*(undefined2 *)(sRam003eebc6 * 2 + 0xc01690));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x78) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd7b0,*(undefined2 *)(sRam003eebc6 * 2 + 0xc017e6));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x80) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd7c0,*(undefined2 *)(sRam003eebc6 * 2 + 0xc0193c));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x88) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd7d0,*(undefined2 *)(sRam003eebc6 * 2 + 0xc01a92));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x90) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd7e0,*(undefined2 *)(sRam003eebc6 * 2 + 0xc01be8));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0x98) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd7f0,
                  (uint)*(ushort *)(sRam003eebc6 * 2 + 0xc01fea) -
                  (uint)*(ushort *)(sRam003eebc6 * 2 + 0xc02296));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0xa0) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd800,
                  (uint)*(ushort *)(sRam003eebc6 * 2 + 0xc02140) -
                  (uint)*(ushort *)(sRam003eebc6 * 2 + 0xc023ec));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0xa8) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd800,
                  (uint)*(ushort *)(sRam003eebc6 * 2 + 0xc02d46) -
                  (uint)*(ushort *)(sRam003eebc6 * 2 + 0xc02e9c));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0xb0) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd810,*(undefined2 *)(sRam003eebc6 * 2 + 0xc00532));
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0xb8) * 0x10000) >> 0x10);
  puVar7 = (ushort *)(sRam003eebc6 * 2 + 0xc00532);
  if (*puVar7 < 0x32) {
    iVar4 = (uint)*puVar7 * 2 + 2;
  }
  else {
    iVar4 = 100;
  }
  func_0x00382b60(1,0,0x3fd820,*(undefined2 *)(sRam003eebc6 * 2 + 0xc02bf0),
                  *(undefined2 *)(iVar4 + 0x3e7f80));
  iVar4 = 1;
  uVar8 = 0;
  func_0x00382ad8((int)((uRam00401452 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam00401454 + 0xc0) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd830,*(ushort *)(sRam003eebc6 * 2 + 0xc01e94) >> 8);
  puVar9 = (undefined4 *)0x3e8274;
  puVar7 = (ushort *)0x3e825a;
  do {
    lVar6 = func_0x003977c0(uVar8 & 0xff,(undefined)sRam003eebc6);
    if (10 < lVar6) {
      lVar6 = 0;
    }
    func_0x00382ad8((int)((uRam00401452 + 0xa8) * 0x10000) >> 0x10,
                    (int)(((uint)uRam00401454 + iVar4 * 8 + 8) * 0x10000) >> 0x10);
    uVar3 = *puVar9;
    puVar9 = puVar9 + 1;
    func_0x00382b60(1,0,uVar3);
    func_0x00382ad8((int)((uRam00401452 + 0xe8) * 0x10000) >> 0x10,
                    (int)(((uint)uRam00401454 + iVar4 * 8 + 8) * 0x10000) >> 0x10);
    uVar2 = *puVar7;
    puVar7 = puVar7 + 1;
    func_0x00382b60(1,0,0x3fd840,lVar6,(int)lVar6 * (uint)uVar2);
    iVar4 = (iVar4 + 1) * 0x10000 >> 0x10;
    uVar8 = (int)((uVar8 + 1) * 0x10000) >> 0x10;
  } while ((int)uVar8 < 10);
  iVar10 = (iVar4 + 1) * 0x10000 >> 0x10;
  func_0x00382ad8((int)((uRam00401452 + 0xa8) * 0x10000) >> 0x10,
                  (int)(((uint)uRam00401454 + iVar4 * 8 + 8) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd850);
  iVar4 = iVar10 * 8;
  func_0x00382ad8((int)((uRam00401452 + 0xa8) * 0x10000) >> 0x10,
                  (int)(((uint)uRam00401454 + iVar4 + 8) * 0x10000) >> 0x10);
  uVar5 = func_0x001383d0(*(undefined4 *)
                           ((uint)*(ushort *)(sRam003eebc6 * 2 + 0xc00934) * 4 + 0x3e8270));
  func_0x00382b60(1,0,0x3fd860,uVar5);
  func_0x00382ad8((int)((uRam00401452 + 0xe8) * 0x10000) >> 0x10,
                  (int)(((uint)uRam00401454 + iVar4 + 8) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fd868,*(undefined2 *)(sRam003eebc6 * 2 + 0xc00a8a));
  func_0x00397c08(0,0x401440,uRam003ecf48);
  iVar4 = sRam003eebc6 * 2;
  func_0x001383e8(auStack144,0x3fd870,*(undefined2 *)(iVar4 + 0xc08db2),
                  *(undefined2 *)(iVar4 + 0xc09bc2),*(undefined2 *)(iVar4 + 0xc0a9d2));
  func_0x00382ad8((int)((uRam00401452 + 0xa8) * 0x10000) >> 0x10,
                  (int)(((uint)uRam00401454 + ((iVar10 + 1) * 0x10000 >> 0x10) * 8 + 0x18) * 0x10000
                       ) >> 0x10);
  func_0x00382b60(1,0,0x3fd860,auStack144);
  lVar6 = func_0x00121a78();
  if (lVar6 != 0) {
    if ((uRam002afcb2 & 0x400) != 0) {
      iVar4 = 1;
      do {
        if (iVar4 == 0x30) {
          iVar4 = 0x33;
        }
        func_0x003b75b8(iVar4);
        iVar4 = (iVar4 + 1) * 0x10000 >> 0x10;
      } while ((iVar4 < 0x49) && (iVar4 < 0x46));
    }
    if ((uRam002afcb2 & 0x100) != 0) {
      func_0x003b75b8(0x41);
      func_0x003b75b8(0x42);
      func_0x003b75b8(0x43);
      func_0x003b75b8(0x44);
      func_0x003b75b8(0x45);
      func_0x003b75b8(0x33);
      func_0x003b75b8(0x34);
      func_0x003b75b8(0x35);
    }
  }
  return;
}



void FUN_00043658(void)

{
  int iVar1;
  
  iVar1 = 0;
  do {
    func_0x003d1490(iVar1,0x8000);
    iVar1 = (iVar1 + 1) * 0x10000 >> 0x10;
  } while (iVar1 < 0xab);
  uRam00401834 = 0;
  uRam00401835 = 0;
  uRam0040186a = 0;
  uRam0040186b = 0;
  return;
}



void FUN_000436c0(void)

{
  byte bVar1;
  byte bVar2;
  short sVar3;
  int iVar4;
  long lVar5;
  undefined2 uVar6;
  
  sVar3 = 0;
  uVar6 = 3;
  sRam00401832 = 0;
  if (sRam00c084e8 == 0) {
    uVar6 = 0;
  }
  do {
    lVar5 = func_0x003d04f0(sVar3,uRam00c00110);
    if ((lVar5 == 1) && (lVar5 = func_0x003d0598(sVar3,uRam00c00110), lVar5 == 0)) {
      iVar4 = (int)sRam00401832;
      sRam00401832 = sRam00401832 + 1;
      *(undefined2 *)(iVar4 * 2 + 0x4016d8) = uVar6;
      *(short *)(iVar4 * 2 + 0x401580) = sVar3;
    }
    sVar3 = sVar3 + 1;
  } while (sVar3 < 0xab);
  func_0x003d16d8(0x401580,0x4016d8,sRam00401832);
  bVar2 = bRam0040186a;
  bVar1 = bRam00401834;
  *(undefined2 *)(sRam00401832 * 2 + 0x401580) = 0x7fff;
  uRam00401830 = *(undefined2 *)((uint)bVar1 * 2 + 0x401580);
  uRam00401868 = *(undefined2 *)((uint)bVar2 * 2 + 0x401838);
  *(undefined2 *)(sRam00401832 * 2 + 0x4016d8) = 3;
  sRam00401832 = sRam00401832 + 1;
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00043848(undefined2 param_1)

{
  long lVar1;
  
  lVar1 = func_0x003d04f0(param_1,uRam00c00110);
  if (lVar1 == 1) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  return;
}



undefined4 FUN_000438b0(short param_1)

{
  long lVar1;
  long lVar2;
  undefined4 uVar3;
  int iVar4;
  uint uVar5;
  
  uVar5 = SEXT24(param_1);
  lVar1 = func_0x003d04f0(uVar5,uRam00c00110);
  uVar3 = 0;
  if (lVar1 == 1) {
    iVar4 = uVar5 * 2;
    uVar3 = 0;
    if (*(ushort *)(iVar4 + 0xc01be8) < 0xff) {
      if ((ushort)(*(short *)(iVar4 + 0xc00934) - 1U) < 10) {
        lVar1 = func_0x003977c0(*(char *)(short *)(iVar4 + 0xc00934) + -1,uVar5 & 0xff);
        if (10 < lVar1) {
          lVar1 = 0;
        }
      }
      else {
        lVar1 = 10;
      }
      if ((int)((uint)*(ushort *)(iVar4 + 0xc02d46) - (uint)*(ushort *)(iVar4 + 0xc02e9c)) < 1) {
        lVar1 = func_0x003d14e8(uVar5,0x8000);
        uVar3 = 0;
        if (lVar1 == 0) {
          uVar3 = 3;
        }
      }
      else {
        lVar2 = func_0x003d14e8(uVar5,0x8000);
        if (lVar2 == 1) {
          uVar3 = 0xffffffe2;
          if (9 < lVar1) {
            uVar3 = 9;
          }
        }
        else {
          uVar3 = 9;
        }
      }
    }
  }
  return uVar3;
}



// WARNING: Control flow encountered bad instruction data

void FUN_000439d0(void)

{
  short *psVar1;
  int iVar2;
  long lVar3;
  undefined4 uVar4;
  int iVar5;
  uint uVar6;
  
  func_0x00383000(7);
  if (bRam003eef70 < 7) {
                    // WARNING: Could not emulate address calculation at 0x00043a24
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003eef70 * 4 + 0x3fd9d0))();
    return;
  }
  func_0x00382ab8(7);
  func_0x003b9a08(1);
  if (bRam003eef70 == 6) {
    lVar3 = (long)sRam003eecac;
    if (lVar3 < sRam00401832 + -1) {
      bRam00401834 = (byte)sRam003eecac;
      uRam00401830 = *(undefined2 *)((uint)(byte)sRam003eecac * 2 + 0x401580);
      sRam003eecac = 0;
    }
    else {
      if ((lVar3 != sRam00401832 + -1) && (lVar3 != 0x3fff)) goto LAB_00043e8c;
      bRam00401834 = (char)sRam003eecae + (char)sRam003eeca8;
      sRam003eecac = 0x3fff;
    }
    bRam00401518 = bRam00401518 & 0xf1 | 4;
    bRam003eecb0 = bRam003eecb0 & 0xfe;
    cRam00401835 = (char)sRam003eeca8;
    uRam0040152c = uRam00401524;
    uRam0040152a = uRam00401522;
    bRam003eef70 = 1;
  }
LAB_00043e8c:
  func_0x00382ab8(7,0);
  psVar1 = (short *)(((int)sRam003eecae + (int)sRam003eeca8) * 2 + 0x401580);
  if (sRam00401530 != *psVar1) {
    sRam00401530 = *psVar1;
    func_0x00396a98(0x42,0x107f300);
  }
  func_0x00382ad8((int)((uRam0040152a + 4) * 0x10000) >> 0x10,
                  (int)((uRam0040152c + 2) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd900);
  func_0x00382ad8((int)((uRam0040152a + 0x94) * 0x10000) >> 0x10,
                  (int)((uRam0040152c + 2) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd910,uRam00c084e8);
  if (sRam00401530 == 0x7fff) {
    func_0x00382ad8((int)((uRam0040152a + 100) * 0x10000) >> 0x10,
                    (int)((uRam0040152c + 0x26) * 0x10000) >> 0x10);
    func_0x00382b60(1,1,0x3fd948);
    func_0x00382ad8((int)((uRam0040152a + 0x68) * 0x10000) >> 0x10,
                    (int)((uRam0040152c + 0x3a) * 0x10000) >> 0x10);
    func_0x00382b60(1,1,0x3fd958);
    func_0x00382ad8((int)((uRam0040152a + 200) * 0x10000) >> 0x10,
                    (int)((uRam0040152c + 0x4a) * 0x10000) >> 0x10);
    func_0x00382b60(1,1,0x3fd970);
  }
  else {
    func_0x00382ad8((int)((uRam0040152a + 4) * 0x10000) >> 0x10,
                    (int)((uRam0040152c + 0x16) * 0x10000) >> 0x10);
    func_0x00382b60(1,1,*(undefined4 *)(sRam00401530 * 4 + 0x3e78c0));
    func_0x00382ad8((int)((uRam0040152a + 100) * 0x10000) >> 0x10,
                    (int)((uRam0040152c + 0x26) * 0x10000) >> 0x10);
    iVar2 = sRam00401530 * 2;
    iVar5 = (uint)*(ushort *)(iVar2 + 0xc01fea) - (uint)*(ushort *)(iVar2 + 0xc02296);
    func_0x00382b60(1,1,0x3fd920,
                    (iVar5 + (uint)*(ushort *)(iVar2 + 0xc02140)) -
                    (uint)*(ushort *)(iVar2 + 0xc023ec),iVar5,
                    (uint)*(ushort *)(iVar2 + 0xc02140) - (uint)*(ushort *)(iVar2 + 0xc023ec));
    func_0x00382ad8((int)((uRam0040152a + 0x68) * 0x10000) >> 0x10,
                    (int)((uRam0040152c + 0x3a) * 0x10000) >> 0x10);
    if ((int)((uint)*(ushort *)(sRam00401530 * 2 + 0xc02d46) -
             (uint)*(ushort *)(sRam00401530 * 2 + 0xc02e9c)) < 1) {
      uVar4 = 0x3fd938;
    }
    else {
      uVar4 = 0x3fd930;
    }
    func_0x00382b60(1,1,uVar4);
    iVar2 = 0;
    uVar6 = 0;
    func_0x00382ad8((int)((uRam0040152a + 200) * 0x10000) >> 0x10,
                    (int)((uRam0040152c + 0x3a) * 0x10000) >> 0x10);
    func_0x00382b60(1,1,0x3fd940,*(undefined2 *)(sRam00401530 * 2 + 0xc02d46));
    do {
      lVar3 = func_0x003977c0(uVar6 & 0xff,(undefined)sRam00401530);
      if (lVar3 < 0xb) {
        iVar2 = (iVar2 + (int)lVar3) * 0x10000;
      }
      else {
        iVar2 = iVar2 << 0x10;
      }
      iVar2 = iVar2 >> 0x10;
      uVar6 = (int)((uVar6 + 1) * 0x10000) >> 0x10;
    } while ((int)uVar6 < 10);
    func_0x00382ad8((int)((uRam0040152a + 200) * 0x10000) >> 0x10,
                    (int)((uRam0040152c + 0x4a) * 0x10000) >> 0x10);
    func_0x00382b60(1,1,0x3fd940,iVar2);
  }
  func_0x00382ad8((int)((uRam0040152a + 0x60) * 0x10000) >> 0x10,
                  (int)((uRam0040152c + 0x16) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd978);
  func_0x00382ad8((int)((uRam0040152a + 0x40) * 0x10000) >> 0x10,
                  (int)((uRam0040152c + 0x26) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd990);
  func_0x00382ad8((int)((uRam0040152a + 0x40) * 0x10000) >> 0x10,
                  (int)((uRam0040152c + 0x3a) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd9a8);
  func_0x00382ad8((int)((uRam0040152a + 0x94) * 0x10000) >> 0x10,
                  (int)((uRam0040152c + 0x4a) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fd9c0);
  sRam003ed204 = uRam0040152a + 200;
  sRam003ed064 = uRam0040152a + 0x10;
  sRam003ed066 = uRam0040152c + 0x5e;
  sRam003ed144 = uRam0040152a + 0x6c;
  sRam003ed124 = uRam0040152a + 0x80;
  sRam003ed1e4 = uRam0040152a + 0xb0;
  sRam003ed126 = sRam003ed066;
  sRam003ed146 = sRam003ed066;
  sRam003ed1e6 = sRam003ed066;
  sRam003ed206 = sRam003ed066;
  func_0x00383a90(1,0x3ed058);
  func_0x00383a90(1,0x3ed138);
  func_0x00383a90(1,0x3ed118);
  func_0x00383a90(1,0x3ed1d8);
  func_0x00383a90(1,0x3ed1f8);
  if ((sRam00401530 < 0xab) && (lVar3 = func_0x003d14e8(sRam00401530,0x8000), lVar3 == 0)) {
    sRam003eef5e = uRam0040152c + 0x4a;
    sRam003eef5c = uRam0040152a + 0x40;
    func_0x00383a90(1,0x3eef50);
  }
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_00044450(void)

{
  long lVar1;
  int iVar2;
  undefined2 *puVar3;
  int iVar4;
  
  iVar4 = 0;
  do {
    lVar1 = func_0x003977c0();
    iVar2 = iVar4 * 2;
    if (10 < lVar1) {
      lVar1 = 0;
    }
    *(short *)(iVar2 + 0x401838) = (short)iVar4;
    iVar4 = (iVar4 + 1) * 0x10000 >> 0x10;
    puVar3 = (undefined2 *)(iVar2 + 0x401850);
    if (((int)lVar1 - 1U & 0xffff) < 9) {
      *puVar3 = 3;
    }
    else {
      *puVar3 = 0;
    }
  } while (iVar4 < 10);
  uRam0040184c = 10;
  uRam00401864 = 3;
  return;
}



void FUN_00044530(void)

{
  short sVar1;
  ushort uVar2;
  undefined8 uVar3;
  short sVar4;
  ushort *puVar5;
  long lVar6;
  undefined4 uVar7;
  int iVar8;
  short *psVar9;
  ushort local_5e;
  
  func_0x00383000();
  if (bRam003eef71 < 0xc) {
                    // WARNING: Could not emulate address calculation at 0x00044584
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003eef71 * 4 + 0x3fda60))();
    return;
  }
  func_0x00382ab8(7,0);
  func_0x003b9a08(1,0x10);
  if (bRam003eef71 == 6) {
    lVar6 = (long)sRam003eecbc;
    if (lVar6 < sRam003eef48 + -1) {
      bRam0040186a = (byte)sRam003eecbc;
      sRam003eecbc = sRam003eef48 + 1;
      uRam00401868 = *(undefined2 *)((uint)(byte)sRam003eecbc * 2 + 0x401838);
      bRam003eef71 = 7;
    }
    else {
      if (((lVar6 != sRam003eef48 + -1) && (lVar6 != 0x3fff)) && (sRam00c084e8 != 0))
      goto LAB_00044e58;
      bRam00401538 = bRam00401538 & 0xf1 | 4;
      bRam0040186a = cRam003eecbe + cRam003eecb8;
      uRam0040154c = uRam00401544;
      bRam003eef71 = 1;
      sRam003eecbc = 0x3fff;
      uRam0040154a = uRam00401542;
    }
    bRam003eecc0 = bRam003eecc0 & 0xfe;
    cRam0040186b = cRam003eecb8;
  }
LAB_00044e58:
  func_0x00382ab8(7,0);
  func_0x00382ad8((int)((uRam0040154a + 0x1c) * 0x10000) >> 0x10,
                  (int)((uRam0040154c + 0x3e) * 0x10000) >> 0x10);
  func_0x00129d68(2);
  uVar3 = func_0x001383d0(*(undefined4 *)(sRam00401830 * 4 + 0x3e78c0));
  func_0x00382b60(1,1,0x3fda10,uVar3);
  func_0x00382ad8((int)((uRam0040154a + 0x1c) * 0x10000) >> 0x10,
                  (int)((uRam0040154c + 0x46) * 0x10000) >> 0x10);
  func_0x00129d68(2);
  puVar5 = (ushort *)(sRam00401830 * 2 + 0xc00286);
  if (*puVar5 < 8) {
    uVar3 = func_0x001383d0(*(undefined4 *)((uint)*puVar5 * 4 + 0x3e82e0));
  }
  else {
    uVar3 = func_0x001383d0(0x3fda18);
  }
  func_0x00382b60(1,1,0x3fda10,uVar3);
  func_0x00382ad8((int)((uRam0040154a + 0x1c) * 0x10000) >> 0x10,
                  (int)((uRam0040154c + 0x4e) * 0x10000) >> 0x10);
  func_0x00129d68(2);
  uVar3 = func_0x003d1358(sRam00401830);
  uVar3 = func_0x001383d0(uVar3);
  func_0x00382b60(1,1,0x3fda10,uVar3);
  func_0x00382ad8((int)((uRam0040154a + 0x1c) * 0x10000) >> 0x10,
                  (int)((uRam0040154c + 0x56) * 0x10000) >> 0x10);
  func_0x00129d68(2);
  uVar3 = func_0x003d0e88(0x299,*(undefined2 *)(sRam00401830 * 2 + 0xc00532));
  func_0x00382b60(1,1,0x3fda20,uVar3);
  func_0x00382ad8((int)((uRam0040154a + 10) * 0x10000) >> 0x10,
                  (int)((uRam0040154c + 0x68) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fda28);
  func_0x00382ad8((int)((uRam0040154a + 0x3a) * 0x10000) >> 0x10,
                  (int)((uRam0040154c + 0x68) * 0x10000) >> 0x10);
  if ((int)((uint)*(ushort *)(sRam00401830 * 2 + 0xc02d46) -
           (uint)*(ushort *)(sRam00401830 * 2 + 0xc02e9c)) < 1) {
    uVar7 = 0x3fd938;
  }
  else {
    uVar7 = 0x3fd930;
  }
  func_0x00382b60(1,1,uVar7);
  iVar8 = 0;
  func_0x00382ad8((int)((uRam0040154a + 10) * 0x10000) >> 0x10,
                  (int)((uRam0040154c + 0x74) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fda30,*(undefined2 *)(sRam00401830 * 2 + 0xc02d46));
  func_0x00382ad8((int)((uRam0040154a + 10) * 0x10000) >> 0x10,
                  (int)((uRam0040154c + 0x86) * 0x10000) >> 0x10);
  func_0x00382b60(1,1);
  psVar9 = (short *)0x401568;
  local_5e = uRam0040154c;
  do {
    uVar2 = uRam0040154a;
    local_5e = local_5e + 0xc;
    sVar1 = *psVar9;
    lVar6 = (long)sVar1;
    if ((int)sVar1 - 1U < 10) {
      sVar4 = uRam0040154a + 0x94;
      if (lVar6 == 10) {
        uRam003eef06 = local_5e;
        sRam003eef04 = sVar4;
        func_0x00383a90(1,0x3eeef8);
      }
      else {
        if (4 < lVar6) {
          sRam003eeee4 = sVar4;
          uRam003eeee6 = local_5e;
          func_0x00383a90(1,0x3eeed8);
          lVar6 = (long)((sVar1 + -5) * 0x10000 >> 0x10);
          sVar4 = uVar2 + 0xac;
        }
        for (; lVar6 != 0; lVar6 = (long)(((int)lVar6 + -1) * 0x10000 >> 0x10)) {
          uRam003eeec6 = local_5e;
          sRam003eeec4 = sVar4;
          func_0x00383a90(1,0x3eeeb8);
          sVar4 = sVar4 + 8;
        }
      }
      func_0x00382ab8(7,0);
      func_0x00382ad8((int)((uRam0040154a + 0xc4) * 0x10000) >> 0x10,
                      (int)((local_5e + 6) * 0x10000) >> 0x10);
      func_0x00382b60(1,0);
      func_0x00382ad8((int)((uRam0040154a + 0xca) * 0x10000) >> 0x10,local_5e);
      func_0x00382b60(1,1);
    }
    else {
      func_0x00382ab8(9,0);
      func_0x00382ad8((int)((uRam0040154a + 0xc4) * 0x10000) >> 0x10,local_5e);
      func_0x00382b60(1,1);
    }
    psVar9 = psVar9 + 1;
    iVar8 = (iVar8 + 1) * 0x10000 >> 0x10;
  } while (iVar8 < 10);
  sRam003ed226 = uRam0040154c + 2;
  sRam003ed064 = uRam0040154a + 8;
  sRam003ed124 = uRam0040154a + 7;
  sRam003ed224 = uRam0040154a + 0x94;
  sRam003ed304 = uRam0040154a + 100;
  sRam003ed066 = uRam0040154c + 0x42;
  sRam003ed106 = uRam0040154c + 0x4a;
  sRam003ed126 = uRam0040154c + 0x52;
  sRam003ed146 = uRam0040154c + 0x5a;
  sRam003ed104 = sRam003ed064;
  sRam003ed144 = sRam003ed124;
  sRam003ed306 = sRam003ed226;
  func_0x00383a90(1,0x3ed058);
  func_0x00383a90(1,0x3ed0f8);
  func_0x00383a90(1,0x3ed118);
  func_0x00383a90(1,0x3ed138);
  func_0x00383a90(1,0x3ed2f8);
  func_0x00383a90(1,0x3ed218);
  func_0x00397c08(0,0x401538,uRam003ecf48);
  return;
}



bool FUN_00045440(void)

{
  uRam00401830 = uRam00c00110;
  *(undefined4 *)((uint)bRam003ed3bf * 4 + 0x3eebf8) = 1;
  uRam0040184c = 0xb;
  func_0x003c4e30();
  return cRam003eecc2 == -1;
}



void FUN_000454a0(void)

{
  int iVar1;
  
  iVar1 = 0;
  do {
    func_0x003d1490(iVar1,0x4000);
    iVar1 = (iVar1 + 1) * 0x10000 >> 0x10;
  } while (iVar1 < 0xab);
  uRam00401bec = 0;
  uRam00401bed = 0;
  uRam00401ff4 = 0;
  uRam00401ff5 = 0;
  return;
}



void FUN_00045508(void)

{
  byte bVar1;
  int iVar2;
  long lVar3;
  undefined2 uVar4;
  int iVar5;
  short *psVar6;
  
  iVar5 = 0;
  psVar6 = (short *)0xc01d3e;
  sRam00401bea = 0;
  do {
    lVar3 = func_0x003d04f0(iVar5,uRam00c00110);
    if (lVar3 == 1) {
      func_0x003c5ff0();
      iVar2 = (int)sRam00401bea;
      *(short *)(iVar2 * 2 + 0x401938) = (short)iVar5;
      uVar4 = 0;
      if ((*psVar6 != 0) || (2 < sRam00401ff2)) {
        uVar4 = 3;
      }
      *(undefined2 *)(iVar2 * 2 + 0x401a90) = uVar4;
      func_0x003c6500();
      sRam00401bea = sRam00401bea + 1;
    }
    iVar5 = (iVar5 + 1) * 0x10000 >> 0x10;
    psVar6 = psVar6 + 1;
  } while (iVar5 < 0xab);
  func_0x003d16d8(0x401938,0x401a90,sRam00401bea);
  bVar1 = bRam00401bec;
  *(undefined2 *)(sRam00401bea * 2 + 0x401938) = 0x7fff;
  uRam00401be8 = *(undefined2 *)((uint)bVar1 * 2 + 0x401938);
  *(undefined2 *)(sRam00401bea * 2 + 0x401a90) = 3;
  sRam00401bea = sRam00401bea + 1;
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00045680(undefined2 param_1)

{
  long lVar1;
  
  lVar1 = func_0x003d04f0(param_1,uRam00c00286);
  if (lVar1 == 1) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  return;
}



void FUN_000456f0(short param_1)

{
  ushort uVar1;
  short sVar2;
  int iVar3;
  int iVar4;
  ushort uVar5;
  ushort *puVar6;
  short sVar7;
  uint uVar8;
  undefined2 uVar9;
  uint uVar10;
  short sVar11;
  uint uVar12;
  uint uVar13;
  
  uVar8 = 0;
  uVar12 = SEXT24(param_1);
  uVar13 = 0;
  iVar3 = 0;
  while( true ) {
    if (0 < *(short *)(iVar3 + 0xc082e8)) {
      *(short *)(iVar3 + 0x401bf0) = *(short *)(iVar3 + 0xc082e8);
      uVar13 = (int)((uVar13 + 1) * 0x10000) >> 0x10;
    }
    uVar8 = uVar8 + 1 & 0xffff;
    if (0xfd < uVar8) break;
    iVar3 = uVar8 << 1;
  }
  uVar10 = 0;
  func_0x003d0de8();
  uVar8 = uVar13;
  if (0 < (int)uVar13) {
    do {
      iVar3 = uVar10 * 2;
      uVar10 = uVar10 + 1 & 0xffff;
      *(undefined2 *)(iVar3 + 0xc082e8) = *(undefined2 *)(iVar3 + 0x401bf0);
    } while ((int)uVar10 < (int)uVar13);
  }
  for (; uVar8 = uVar8 & 0xffff, uVar8 < 0xfe; uVar8 = uVar8 + 1) {
    *(undefined2 *)(uVar8 * 2 + 0xc082e8) = 0;
  }
  sRam00401ff2 = 0;
  uVar8 = 0;
  iVar3 = 0;
  do {
    sVar7 = *(short *)(iVar3 + 0xc082e8);
    if (0 < sVar7) {
      iVar4 = sVar7 * 0x1c;
      sVar11 = 0;
      puVar6 = (ushort *)(iVar4 + 0x3e6f9c);
      uVar5 = *puVar6;
      iVar4 = *(int *)(iVar4 + 0x3e6f98);
      if (uVar5 != 0xffff) {
        while (uVar5 != 0x8000) {
          uVar1 = *puVar6;
          puVar6 = puVar6 + 1;
          uVar5 = *puVar6;
          if (uVar12 == uVar1) {
            sVar11 = 3;
          }
        }
      }
      if (iVar4 == 0xffff) {
LAB_000458f8:
        sVar11 = 3;
      }
      else {
        if ((iVar4 != 1) &&
           (uVar5 = *(ushort *)(uVar12 * 2 + 0xc00688), (iVar4 >> (uVar5 & 0x1f) & 1U) != 0)) {
          if (uVar5 == 10) {
            if ((iVar4 >> (*(ushort *)(uVar12 * 2 + 0xc043fc) + 0x1d & 0x1f) & 1U) != 0)
            goto LAB_000458f8;
          }
          else {
            sVar11 = 3;
          }
        }
      }
      if (sVar11 == 3) {
        uVar5 = *(ushort *)((sVar7 * 8 - (int)sVar7) * 4 + 0x3e6fac);
        uVar10 = uVar5 >> 8 & 0x3f;
        uVar5 = uVar5 & 0x3f00;
        if (*(short *)(uVar10 * 2 + 0x3ef0f0) == 0x82) {
          if (uVar5 == 0xe00) {
LAB_00045a3c:
            sVar2 = func_0x003977c0(uVar10 - 8 & 0xff);
            if ((ushort)(sVar2 - 1U) < 10) {
              sVar11 = 0;
            }
          }
          else {
            if (uVar5 < 0xe01) {
              if (uVar5 != 0xa00) {
                if (uVar5 < 0xa01) {
                  if ((uVar5 != 0x800) && (uVar5 != 0x900)) {
                    iVar4 = (int)sRam00401ff2;
                    goto LAB_00045a88;
                  }
                }
                else {
                  if (uVar5 != 0xc00) {
                    if (uVar5 < 0xc01) {
                      if (uVar5 == 0xb00) goto LAB_00045a3c;
                      iVar4 = (int)sRam00401ff2;
                    }
                    else {
                      if (uVar5 == 0xd00) goto LAB_00045a3c;
                      iVar4 = (int)sRam00401ff2;
                    }
                    goto LAB_00045a88;
                  }
                }
              }
              goto LAB_00045a3c;
            }
            if (uVar5 == 0x1100) goto LAB_00045a3c;
            if (uVar5 < 0x1101) {
              if ((uVar5 != 0xf00) && (uVar5 != 0x1000)) {
                iVar4 = (int)sRam00401ff2;
                goto LAB_00045a88;
              }
              goto LAB_00045a3c;
            }
            if (uVar5 != 0x1500) {
              if (uVar5 < 0x1501) {
                if (uVar5 == 0x1200) {
                  if (*(short *)(uVar12 * 2 + 0xc00130) != 6) {
                    sVar11 = 0;
                  }
                  goto LAB_00045a84;
                }
                iVar4 = (int)sRam00401ff2;
              }
              else {
                iVar4 = (int)sRam00401ff2;
              }
              goto LAB_00045a88;
            }
          }
LAB_00045a84:
          iVar4 = (int)sRam00401ff2;
        }
        else {
          if (uVar5 == 0x400) {
            if (*(short *)(uVar12 * 2 + 0xc01690) == 0) {
              sVar11 = 0;
            }
            goto LAB_00045a84;
          }
          iVar4 = (int)sRam00401ff2;
        }
      }
      else {
        iVar4 = (int)sRam00401ff2;
      }
LAB_00045a88:
      *(short *)(iVar4 * 2 + 0x401bf0) = sVar7;
      *(short *)(iVar4 * 2 + 0x401df0) = sVar11;
      *(undefined2 *)(iVar3 + 0xc082e8) = 0;
      sRam00401ff2 = sRam00401ff2 + 1;
    }
    uVar8 = uVar8 + 1 & 0xffff;
    iVar3 = uVar8 << 1;
    if (0xfd < uVar8) {
      iVar3 = (int)sRam00401ff2;
      *(undefined2 *)(iVar3 * 2 + 0x401bf0) = 0xffff;
      if ((0xfd < (int)uVar13) || (uVar9 = 3, *(short *)(uVar12 * 2 + 0xc01d3e) == 0)) {
        uVar9 = 0;
      }
      uVar8 = (uint)bRam00401ff4;
      sVar7 = sRam00401ff2 + 2;
      iVar4 = (int)((uint)(ushort)(sRam00401ff2 + 1) << 0x10) >> 0xf;
      *(undefined2 *)(iVar3 * 2 + 0x401df0) = uVar9;
      *(undefined2 *)(iVar4 + 0x401df0) = 3;
      *(undefined2 *)(iVar4 + 0x401bf0) = 0;
      if ((int)sVar7 <= (int)uVar8) {
        sRam00401ff2._0_1_ = (char)sVar7;
        bRam00401ff4 = (char)sRam00401ff2 - 1;
        uVar8 = (uint)bRam00401ff4;
      }
      iVar3 = sVar7 + -4;
      if (iVar3 < (int)(uint)bRam00401ff5) {
        iVar4 = 0;
        if (-1 < iVar3) {
          iVar4 = iVar3;
        }
        bRam00401ff5 = (byte)iVar4;
      }
      uRam00401ff0 = *(undefined2 *)(uVar8 * 2 + 0x401bf0);
      sRam00401ff2 = sVar7;
      return;
    }
  } while( true );
}



// WARNING: Control flow encountered bad instruction data

void FUN_00045e78(short param_1,short param_2)

{
  short sVar1;
  short sVar2;
  int iVar3;
  ushort *puVar4;
  ushort uVar5;
  int iVar6;
  int iVar7;
  
  iVar3 = param_2 * 0x1c;
  iVar7 = (int)param_1;
  uVar5 = *(ushort *)(iVar3 + 0x3e6fac);
  sVar2 = func_0x003c65d8(iVar7,(int)param_2);
  iVar6 = (uVar5 >> 8 & 0x3f) * 2;
  sVar1 = *(short *)(iVar6 + 0x3ef0f0);
  if (sVar1 != 0x82) {
    puVar4 = (ushort *)((sVar1 + iVar7) * 2 + 0xc00000);
    uVar5 = *puVar4 + sVar2;
    *puVar4 = uVar5;
    if ((long)(ulong)uVar5 <= (long)*(short *)(iVar6 + 0x3ef120)) {
      return;
    }
    *(undefined2 *)((*(short *)(iVar6 + 0x3ef0f0) + iVar7) * 2 + 0xc00000) =
         *(undefined2 *)(iVar6 + 0x3ef120);
    return;
  }
  uVar5 = *(ushort *)(iVar3 + 0x3e6fac) & 0x3f00;
  if (uVar5 != 0xe00) {
    if (uVar5 < 0xe01) {
      if (uVar5 != 0xa00) {
        if (uVar5 < 0xa01) {
          if ((uVar5 != 0x800) && (uVar5 != 0x900)) {
            return;
          }
        }
        else {
          if (uVar5 != 0xc00) {
            if (0xc00 < uVar5) {
              if (uVar5 != 0xd00) {
                return;
              }
              return;
            }
            if (uVar5 != 0xb00) {
              return;
            }
            return;
          }
        }
      }
    }
    else {
      if (uVar5 != 0x1100) {
        if (uVar5 < 0x1101) {
          if (uVar5 == 0xf00) {
            return;
          }
          if (uVar5 == 0x1000) {
            return;
          }
        }
        else {
          if (((uVar5 != 0x1500) && (uVar5 < 0x1501)) && (iVar7 = iVar7 * 2, uVar5 == 0x1200)) {
            *(undefined2 *)(iVar7 + 0xc00130) = 8;
            *(undefined2 *)(iVar7 + 0xc007de) = *(undefined2 *)((uint)uRam00c00110 * 2 + 0xc007de);
            *(undefined2 *)(iVar7 + 0xc0354a) = 0;
          }
        }
        return;
      }
    }
  }
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x000464f8: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00046500)

void FUN_000460e8(undefined param_1,short param_2,short param_3,short param_4,short param_5)

{
  short sVar1;
  int iVar2;
  int iVar3;
  undefined8 uVar4;
  undefined8 uVar5;
  int iVar6;
  short *psVar7;
  int iVar8;
  uint uVar9;
  int iVar10;
  uint uVar11;
  int iVar12;
  
  iVar6 = (int)param_2;
  iVar8 = (int)param_5;
  iVar10 = (int)param_4;
  iVar12 = (int)param_3;
  if (0 < iVar8) {
    uVar9 = *(ushort *)(iVar10 * 0x1c + 0x3e6fac) >> 8 & 0x3f;
    uVar11 = *(ushort *)(iVar8 * 0x1c + 0x3e6fac) >> 8 & 0x3f;
    iVar2 = func_0x003c65d8(sRam00401be8,iVar10);
    iVar3 = func_0x003c65d8(sRam00401be8,iVar8);
    if ((*(ushort *)(iVar8 * 0x1c + 0x3e6fac) & 0x4000) == 0) {
      if ((iVar10 == 0) || (uVar9 == uVar11)) {
        psVar7 = (short *)(uVar11 * 2 + 0x3ef0f0);
        sVar1 = *psVar7;
        uVar4 = func_0x003d0e88(sVar1,(int)(((uint)*(ushort *)
                                                    (((int)sVar1 + (int)sRam00401be8) * 2 + 0xc00000
                                                    ) + iVar2) * 0x10000) >> 0x10);
        sVar1 = *psVar7;
        uVar5 = func_0x003d0e88(sVar1,(int)(((uint)*(ushort *)
                                                    (((int)sVar1 + (int)sRam00401be8) * 2 + 0xc00000
                                                    ) + iVar3) * 0x10000) >> 0x10);
        func_0x00382ad8((iVar6 + -4) * 0x10000 >> 0x10,iVar12);
        func_0x00382b60(param_1,1,*(undefined4 *)(uVar11 * 4 + 0x3eef78),uVar4,uVar5);
        if (*(int *)(uVar11 * 4 + 0x3ef150) != 1) {
          return;
        }
        sRam003ed026 = param_3 + 3;
      }
      else {
        psVar7 = (short *)(uVar9 * 2 + 0x3ef0f0);
        sVar1 = *psVar7;
        uVar4 = func_0x003d0e88(sVar1,(int)(((uint)*(ushort *)
                                                    (((int)sVar1 + (int)sRam00401be8) * 2 + 0xc00000
                                                    ) + iVar2) * 0x10000) >> 0x10);
        sVar1 = *psVar7;
        uVar5 = func_0x003d0e88(sVar1,*(undefined2 *)
                                       (((int)sVar1 + (int)sRam00401be8) * 2 + 0xc00000));
        iVar6 = (iVar6 + -4) * 0x10000 >> 0x10;
        func_0x00382ad8(iVar6,iVar12);
        func_0x00382b60(param_1,1,*(undefined4 *)(uVar9 * 4 + 0x3eef78),uVar4,uVar5);
        if (*(int *)(uVar9 * 4 + 0x3ef150) == 1) {
          sRam003ed026 = param_3 + 3;
        }
        else {
          psVar7 = (short *)(uVar11 * 2 + 0x3ef0f0);
          sVar1 = *psVar7;
          uVar4 = func_0x003d0e88(sVar1,*(undefined2 *)
                                         (((int)sVar1 + (int)sRam00401be8) * 2 + 0xc00000));
          sVar1 = *psVar7;
          uVar5 = func_0x003d0e88(sVar1,(int)(((uint)*(ushort *)
                                                      (((int)sVar1 + (int)sRam00401be8) * 2 +
                                                      0xc00000) + iVar3) * 0x10000) >> 0x10);
          func_0x00382ad8(iVar6,(iVar12 + 0xc) * 0x10000 >> 0x10);
          func_0x00382b60(param_1,1,*(undefined4 *)(uVar11 * 4 + 0x3eef78),uVar4,uVar5);
          if (*(int *)(uVar11 * 4 + 0x3ef150) != 1) {
            return;
          }
          sRam003ed026 = param_3 + 0xf;
        }
      }
    }
    else {
      psVar7 = (short *)(uVar11 * 2 + 0x3ef0f0);
      sVar1 = *psVar7;
      if (uVar9 != uVar11) {
        iVar2 = 0;
      }
      uVar4 = func_0x003d0e88(sVar1,(int)(((uint)*(ushort *)
                                                  (((int)sVar1 + (int)sRam00401be8) * 2 + 0xc00000)
                                          + iVar2) * 0x10000) >> 0x10);
      sVar1 = *psVar7;
      uVar5 = func_0x003d0e88(sVar1,(int)((iVar3 + (uint)*(ushort *)
                                                          (((int)sVar1 + (int)sRam00401be8) * 2 +
                                                          0xc00000) + iVar2) * 0x10000) >> 0x10);
      func_0x00382ad8((iVar6 + -4) * 0x10000 >> 0x10,iVar12);
      func_0x00382b60(param_1,1,*(undefined4 *)(uVar11 * 4 + 0x3eef78),uVar4,uVar5);
      if (*(int *)(uVar11 * 4 + 0x3ef150) != 1) {
        return;
      }
      sRam003ed026 = param_3 + 3;
    }
SUB_00383a90:
    sRam003ed024 = param_2 + 0x4f;
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  if ((iVar8 == -1) && (0 < iVar10)) {
    uVar9 = *(ushort *)(iVar10 * 0x1c + 0x3e6fac) >> 8 & 0x3f;
    iVar8 = func_0x003c65d8(sRam00401be8,iVar10);
    iVar10 = uVar9 * 4;
    psVar7 = (short *)(uVar9 * 2 + 0x3ef0f0);
    sVar1 = *psVar7;
    uVar4 = func_0x003d0e88(sVar1,(int)(((uint)*(ushort *)
                                                (((int)sVar1 + (int)sRam00401be8) * 2 + 0xc00000) +
                                        iVar8) * 0x10000) >> 0x10);
    sVar1 = *psVar7;
    uVar5 = func_0x003d0e88(sVar1,*(undefined2 *)(((int)sVar1 + (int)sRam00401be8) * 2 + 0xc00000));
    func_0x00382ad8((iVar6 + -4) * 0x10000 >> 0x10,iVar12);
    func_0x00382b60(param_1,1,*(undefined4 *)(iVar10 + 0x3eef78),uVar4,uVar5);
    if (*(int *)(iVar10 + 0x3ef150) == 1) {
      sRam003ed026 = param_3 + 3;
      goto SUB_00383a90;
    }
  }
  return;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x000468c0: Changing call to branch
// WARNING: Possible PIC construction at 0x000468ec: Changing call to branch
// WARNING: Removing unreachable block (ram,0x000468c8)
// WARNING: Removing unreachable block (ram,0x000468f4)
// WARNING: Removing unreachable block (ram,0x00046924)
// WARNING: Removing unreachable block (ram,0x00383a90)

void FUN_00046798(undefined8 param_1,undefined2 param_2,short param_3,short param_4)

{
  short sVar1;
  int iVar2;
  short *psVar3;
  int iVar4;
  uint uVar5;
  
  iVar4 = (int)param_4;
  sVar1 = *(short *)(iVar4 * 2 + 0xc01d3e);
  if (0 < sVar1) {
    uVar5 = *(ushort *)(sVar1 * 0x1c + 0x3e6fac) >> 8 & 0x3f;
    iVar2 = func_0x003c65d8(iVar4,sVar1);
    psVar3 = (short *)(uVar5 * 2 + 0x3ef0f0);
    sVar1 = *psVar3;
    func_0x003d0e88(sVar1,*(undefined2 *)((sVar1 + iVar4) * 2 + 0xc00000));
    sVar1 = *psVar3;
    func_0x003d0e88(sVar1,(int)(((uint)*(ushort *)((sVar1 + iVar4) * 2 + 0xc00000) + iVar2) *
                               0x10000) >> 0x10);
    if (*(int *)(uVar5 * 8 + 0x3ef03c) == 0) {
      func_0x00382ad8(param_2,(param_3 + 8) * 0x10000 >> 0x10);
    }
    else {
      func_0x00382ad8(param_2,(int)param_3);
    }
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  return;
}



long FUN_000469d8(short param_1,short param_2)

{
  short sVar1;
  ushort uVar2;
  int iVar3;
  ushort *puVar4;
  long lVar5;
  
  iVar3 = (int)param_2;
  sVar1 = *(short *)(iVar3 * 2 + 0xc01d3e);
  puVar4 = (ushort *)((param_1 + iVar3) * 2 + 0xc00000);
  lVar5 = (long)(short)*puVar4;
  if ((0 < sVar1) &&
     (uVar2 = *(ushort *)(sVar1 * 0x1c + 0x3e6fac), iVar3 = func_0x003c65d8(iVar3,sVar1),
     (long)*(short *)((uVar2 >> 8 & 0x3f) * 2 + 0x3ef0f0) == (long)(int)param_1)) {
    lVar5 = (long)((int)(((uint)*puVar4 + iVar3) * 0x10000) >> 0x10);
    func_0x00382ab8(5,0);
  }
  return lVar5;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x00047218: Changing call to branch
// WARNING: Possible PIC construction at 0x000472bc: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00047220)
// WARNING: Removing unreachable block (ram,0x0004722c)
// WARNING: Removing unreachable block (ram,0x000472b0)
// WARNING: Removing unreachable block (ram,0x000472c4)
// WARNING: Removing unreachable block (ram,0x000472d0)
// WARNING: Removing unreachable block (ram,0x00047368)

void FUN_00046aa8(void)

{
  short *psVar1;
  long lVar2;
  
  func_0x00383000(7);
  if (bRam003ef1d0 < 7) {
                    // WARNING: Could not emulate address calculation at 0x00046afc
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003ef1d0 * 4 + 0x3fdf00))();
    return;
  }
  func_0x00382ab8(7,0);
  func_0x003b9a08(1,0x20);
  if (bRam003ef1d0 == 6) {
    lVar2 = (long)sRam003eeccc;
    if (lVar2 < sRam00401bea + -1) {
      bRam00401bec = (byte)sRam003eeccc;
      uRam00401be8 = *(undefined2 *)((uint)(byte)sRam003eeccc * 2 + 0x401938);
      sRam003eeccc = 0;
    }
    else {
      if ((lVar2 != sRam00401bea + -1) && (lVar2 != 0x3fff)) goto LAB_000470ac;
      bRam00401bec = (char)sRam003eecce + (char)sRam003eecc8;
      sRam003eeccc = 0x3fff;
    }
    bRam004018a0 = bRam004018a0 & 0xf1 | 4;
    bRam00401888 = bRam00401888 & 0xf1 | 4;
    bRam00401870 = bRam00401870 & 0xf1 | 4;
    bRam003eecd0 = bRam003eecd0 & 0xfe;
    cRam00401bed = (char)sRam003eecc8;
    uRam004018b4 = uRam004018ac;
    uRam004018b2 = uRam004018aa;
    uRam0040189c = uRam00401894;
    uRam0040189a = uRam00401892;
    uRam00401884 = uRam0040187c;
    uRam00401882 = uRam0040187a;
    bRam003ef1d0 = 1;
  }
LAB_000470ac:
  func_0x00382ab8(7,0);
  psVar1 = (short *)(((int)sRam003eecce + (int)sRam003eecc8) * 2 + 0x401938);
  if (sRam004018b8 != *psVar1) {
    sRam004018b8 = *psVar1;
    func_0x00396a98(0x42,0x107f300);
  }
  func_0x00382ad8((int)((uRam00401882 + 4) * 0x10000) >> 0x10,
                  (int)((uRam00401884 + 2) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fdee8);
  if (sRam004018b8 != 0x7fff) {
    func_0x00382ad8((int)((uRam00401882 + 4) * 0x10000) >> 0x10,
                    (int)((uRam00401884 + 0x16) * 0x10000) >> 0x10);
    func_0x00382b60(1,1,*(undefined4 *)(sRam004018b8 * 4 + 0x3e78c0));
    func_0x003c7098(1,(int)((uRam00401882 + 0x40) * 0x10000) >> 0x10,
                    (int)((uRam00401884 + 0x36) * 0x10000) >> 0x10);
  }
  sRam003ed0a4 = uRam00401882 + 0x90;
  sRam003ed066 = uRam00401884 + 0x5e;
  sRam003ed064 = uRam00401882 + 0x10;
  sRam003ed0a6 = sRam003ed066;
  func_0x00383a90(1,0x3ed058);
  func_0x00383a90(1,0x3ed098);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x00047b0c: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00047b14)
// WARNING: Removing unreachable block (ram,0x00047b90)
// WARNING: Removing unreachable block (ram,0x00047bf0)
// WARNING: Removing unreachable block (ram,0x00047bd0)
// WARNING: Removing unreachable block (ram,0x00047c0c)
// WARNING: Removing unreachable block (ram,0x00047c5c)

void FUN_000473a8(void)

{
  undefined2 uVar1;
  undefined2 uVar2;
  ushort uVar3;
  int iVar4;
  undefined2 *puVar5;
  long lVar6;
  long lVar7;
  undefined8 uVar8;
  
  func_0x00383000();
  if (bRam003ef218 < 7) {
                    // WARNING: Could not emulate address calculation at 0x000473fc
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003ef218 * 4 + 0x3fdf20))();
    return;
  }
  func_0x00382ab8(7,0);
  func_0x003b9a08(1,0x40,(int)((uRam004018d2 + 8) * 0x10000) >> 0x10,
                  (int)((uRam004018d4 + 0x74) * 0x10000) >> 0x10);
  uVar1 = uRam004018e2;
  uVar3 = uRam004018ca;
  if (bRam003ef218 == 6) {
    lVar6 = (long)sRam003eecdc;
    lVar7 = (long)(sRam00401ff2 + -2);
    if (lVar6 < lVar7) {
      bRam00401ff4 = (byte)sRam003eecdc;
      puVar5 = (undefined2 *)((uint)(byte)sRam003eecdc * 2 + 0x401bf0);
      uRam00401ff0 = *puVar5;
      bRam003eece0 = bRam003eece0 & 0xfe;
      bRam004018c0 = bRam004018c0 & 0xf1 | 4;
      bRam004018d8 = bRam004018d8 & 0xf1 | 4;
      cRam00401ff5 = (char)sRam003eecd8;
      uRam004018d4 = uRam004018cc;
      bRam003ef218 = 1;
      uRam004018ec = uRam004018e4;
      *puVar5 = 0;
      sRam003eecdc = 0;
    }
    else {
      if (lVar6 == lVar7) {
        bRam003eece0 = bRam003eece0 & 0xfe;
        bRam004018c0 = bRam004018c0 & 0xf1 | 4;
        bRam004018d8 = bRam004018d8 & 0xf1 | 4;
        uRam00401ff0 = 0xffff;
        bRam00401ff4 = (byte)sRam003eecdc;
        cRam00401ff5 = (char)sRam003eecd8;
        uRam004018d4 = uRam004018cc;
        bRam003ef218 = 1;
        uRam004018ec = uRam004018e4;
        sRam003eecdc = 0;
      }
      else {
        if ((lVar6 != sRam00401ff2 + -1) && (lVar6 != 0x3fff)) goto LAB_0004798c;
        bRam004018c0 = bRam004018c0 & 0xf1 | 4;
        bRam003eece0 = bRam003eece0 & 0xfe;
        bRam00401ff4 = (char)sRam003eecde + (char)sRam003eecd8;
        bRam004018d8 = bRam004018d8 & 0xf1 | 4;
        cRam00401ff5 = (char)sRam003eecd8;
        uRam004018d4 = uRam004018cc;
        bRam003ef218 = 1;
        uRam004018ec = uRam004018e4;
        sRam003eecdc = 0x3fff;
      }
    }
    uRam004018ea = uVar1;
    uRam004018d2 = uVar3;
  }
LAB_0004798c:
  func_0x00382ab8(7);
  iVar4 = ((int)sRam003eecde + (int)sRam003eecd8) * 2;
  uVar8 = 7;
  if (*(short *)(iVar4 + 0x401df0) != 3) {
    uVar8 = 9;
  }
  uVar1 = *(undefined2 *)(sRam00401be8 * 2 + 0xc01d3e);
  uVar2 = *(undefined2 *)(iVar4 + 0x401bf0);
  func_0x00382ab8(uVar8,0);
  func_0x003c69e8(1,(int)((uRam004018d2 + 0x46) * 0x10000) >> 0x10,
                  (int)((uRam004018d4 + 0x38) * 0x10000) >> 0x10,uVar1,uVar2);
  func_0x00382ab8(7,0);
  func_0x00382ad8((int)((uRam004018d2 + 0x76) * 0x10000) >> 0x10,
                  (int)((uRam004018d4 + 8) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,*(undefined4 *)(sRam00401be8 * 4 + 0x3e78c0));
  func_0x00382ad8((int)((uRam004018d2 + 0x76) * 0x10000) >> 0x10,
                  (int)((uRam004018d4 + 0x1a) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,*(undefined4 *)((uint)*(ushort *)(sRam00401be8 * 2 + 0xc01d3e) * 4 + 0x3e7798)
                 );
  sRam003ed0a4 = uRam004018d2 + 0x10;
  sRam003ed0a6 = uRam004018d4 + 0x66;
  func_0x00383a90(1,0x3ed098);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_000486c8(void)

{
  int iVar1;
  
  iVar1 = 0;
  do {
    func_0x003d1490(iVar1,0x2000);
    iVar1 = (iVar1 + 1) * 0x10000 >> 0x10;
  } while (iVar1 < 0xab);
  uRam004020a6 = 0;
  uRam004020a7 = 0;
  uRam0040235c = 0;
  uRam0040235d = 0;
  uRam00402614 = 0;
  uRam00402615 = 0;
  return;
}



void FUN_00048740(void)

{
  func_0x003c90b8();
  func_0x003c9218();
  uRam004020a0 = 3;
  if (sRam0040235a < 2) {
    uRam004020a0 = 0;
  }
  uRam004020a2 = 3;
  if (sRam00402612 < 2) {
    uRam004020a2 = 0;
  }
  uRam004020a4 = 3;
  return;
}



void FUN_000487b8(void)

{
  byte bVar1;
  int iVar2;
  long lVar3;
  uint uVar4;
  
  uVar4 = 0;
  uRam0040235a = 0;
  do {
    if ((uVar4 != uRam00c00110) && (lVar3 = func_0x003d04f0(uVar4,uRam00c00110), lVar3 == 1)) {
      iVar2 = (int)(short)uRam0040235a;
      uRam0040235a = uRam0040235a + 1;
      *(undefined2 *)(iVar2 * 2 + 0x402200) = 3;
      *(short *)(iVar2 * 2 + 0x4020a8) = (short)uVar4;
    }
    uVar4 = SEXT24((short)((short)uVar4 + 1));
  } while ((int)uVar4 < 0xab);
  func_0x003d16d8(0x4020a8,0x402200);
  bVar1 = bRam0040235c;
  *(undefined2 *)((short)uRam0040235a * 2 + 0x4020a8) = 0x7fff;
  *(undefined2 *)(((int)((uint)(ushort)(uRam0040235a + 1) << 0x10) >> 0xf) + 0x4020a8) = 0;
  uRam00402358 = *(undefined2 *)((uint)bVar1 * 2 + 0x4020a8);
  *(undefined2 *)((short)uRam0040235a * 2 + 0x402200) = 3;
  uRam0040235a = uRam0040235a + 1;
  return;
}



void FUN_00048918(void)

{
  long lVar1;
  int iVar2;
  int iVar3;
  short sVar4;
  int iVar5;
  uint uVar6;
  
  uVar6 = 0;
  sRam00402612 = 0;
  do {
    if ((uVar6 != uRam00c00110) && (lVar1 = func_0x003d0558(uVar6,uRam00c00110), lVar1 == 1)) {
      iVar5 = (int)sRam00402612;
      sRam00402612 = sRam00402612 + 1;
      *(undefined2 *)(iVar5 * 2 + 0x4024b8) = 3;
      *(short *)(iVar5 * 2 + 0x402360) = (short)uVar6;
    }
    uVar6 = SEXT24((short)((short)uVar6 + 1));
  } while ((int)uVar6 < 0xab);
  func_0x003d16d8(0x402360,0x4024b8,sRam00402612);
  sVar4 = sRam00402612 + 1;
  uVar6 = (uint)bRam00402614;
  iVar5 = (int)sVar4;
  *(undefined2 *)(sRam00402612 * 2 + 0x402360) = 0x7fff;
  *(undefined2 *)(sRam00402612 * 2 + 0x4024b8) = 3;
  if (iVar5 <= (int)uVar6) {
    sRam00402612._0_1_ = (char)sVar4;
    bRam00402614 = (char)sRam00402612 - 1;
    uVar6 = (uint)bRam00402614;
  }
  iVar3 = iVar5 + -10;
  if (iVar3 < (int)(uint)bRam00402615) {
    iVar2 = 0;
    if (-1 < iVar3) {
      iVar2 = iVar3;
    }
    bRam00402615 = (byte)iVar2;
  }
  sRam00402612 = sVar4;
  *(undefined2 *)(iVar5 * 2 + 0x402360) = 0;
  uRam00402610 = *(undefined2 *)(uVar6 * 2 + 0x402360);
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00048ac0(short param_1)

{
  long lVar1;
  
  if (((int)param_1 != (uint)uRam00c00110) &&
     (lVar1 = func_0x003d05d8((int)param_1,uRam00c00110), lVar1 == 1)) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  return;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x00049050: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00049058)

void FUN_00048b28(void)

{
  func_0x00383000(7);
  if (bRam003ef3fe < 7) {
                    // WARNING: Could not emulate address calculation at 0x00048b70
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003ef3fe * 4 + 0x3fdfe0))();
    return;
  }
  func_0x00382ab8(7);
  func_0x003b9a08(1,1,(int)((uRam0040200a + 8) * 0x10000) >> 0x10,
                  (int)((uRam0040200c + 8) * 0x10000) >> 0x10,0x3ef3e8,0x3ef3f8,0x4020a0,3);
  if (bRam003ef3fe == 6) {
    if (sRam003eecfc < 2) {
      cRam004020a6 = (char)sRam003eecfc;
    }
    else {
      if ((sRam003eecfc != 2) && (sRam003eecfc != 0x3fff)) goto LAB_00048fe8;
      cRam004020a6 = cRam003eecfe + cRam003eecf8;
      sRam003eecfc = 0x3fff;
    }
    bRam00402010 = bRam00402010 & 0xf1 | 4;
    bRam00401ff8 = bRam00401ff8 & 0xf1 | 4;
    bRam003eed00 = bRam003eed00 & 0xfe;
    cRam004020a7 = cRam003eecf8;
    uRam00402024 = uRam0040201c;
    uRam00402022 = uRam0040201a;
    uRam0040200c = uRam00402004;
    uRam0040200a = uRam00402002;
    bRam003ef3fe = 1;
  }
LAB_00048fe8:
  func_0x00382ab8(7,0);
  func_0x00382ad8((int)((uRam00402022 + 4) * 0x10000) >> 0x10,uRam00402024);
  func_0x00129d68(2);
  func_0x00382b60(1,1,0x3fdfd0,uRam00c0011c,uRam00c0011e,uRam00c00120);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



short FUN_00049088(short *param_1,undefined4 param_2,undefined4 param_3,byte *param_4,char *param_5,
                  short *param_6,int param_7,undefined param_8,undefined1 param_9,
                  undefined4 param_10)

{
  short sVar1;
  short *psVar2;
  long lVar3;
  
  func_0x00383000(7);
  if (bRam003ef3ff < 7) {
                    // WARNING: Could not emulate address calculation at 0x00049104
                    // WARNING: Treating indirect jump as call
    sVar1 = (**(code **)((uint)bRam003ef3ff * 4 + 0x3fe010))();
    return sVar1;
  }
  func_0x00382ab8(7);
  func_0x003b9a08(1);
  if (bRam003ef3ff == 6) {
    lVar3 = (long)param_1[2];
    if (lVar3 < *param_6 + -1) {
      *param_4 = *(byte *)(param_1 + 2);
      *param_5 = *(char *)param_1;
      sRam00402040 = *(short *)((uint)*param_4 * 2 + param_7);
      *(byte *)(param_1 + 4) = *(byte *)(param_1 + 4) & 0xfe;
      param_1[2] = 0;
    }
    else {
      if ((lVar3 != *param_6 + -1) && (lVar3 != 0x3fff)) goto LAB_000494a0;
      *param_4 = *(char *)(param_1 + 3) + *(char *)param_1;
      *param_5 = *(char *)param_1;
      param_1[2] = 0x3fff;
      *(byte *)(param_1 + 4) = *(byte *)(param_1 + 4) & 0xfe;
    }
    bRam003ef3ff = 1;
    uRam0040203a = uRam00402032;
    bRam00402028 = bRam00402028 & 0xf1 | 4;
    uRam0040203c = uRam00402034;
  }
LAB_000494a0:
  func_0x00382ab8(7,0);
  psVar2 = (short *)(((int)param_1[3] + (int)*param_1) * 2 + param_7);
  if (sRam00402040 == *psVar2) {
    func_0x00396a98(0x42,0x107f300,sRam00402040);
  }
  else {
    sRam00402040 = *psVar2;
    func_0x00396a98(0x42,0x107f300);
  }
  func_0x00382ad8((int)((uRam0040203a + 4) * 0x10000) >> 0x10,
                  (int)((uRam0040203c + 2) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,param_2);
  func_0x00382ad8((int)((uRam0040203a + 0x84) * 0x10000) >> 0x10,
                  (int)((uRam0040203c + 2) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,param_3);
  func_0x00382ad8((int)((uRam0040203a + 0xb0) * 0x10000) >> 0x10,
                  (int)((uRam0040203c + 8) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fe000);
  func_0x00382ad8((int)((uRam0040203a + 0xb8) * 0x10000) >> 0x10,
                  (int)((uRam0040203c + 2) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fe008,*param_6 + -1);
  sRam003ed064 = uRam0040203a + 0x14;
  sRam003ed066 = uRam0040203c + 0x18;
  func_0x00383a90(1,0x3ed058);
  func_0x00397c08(0,0x402028,uRam003ecf48);
  return sRam00402040;
}



void FUN_000496e0(void)

{
  if (sRam00402612 < 2) {
    uRam003eed32 = 0xff;
    uRam003eed2c = 0x3fff;
  }
  else {
    uRam00402610 = func_0x003c9988(0x3eed28,0x3fe048,0x3fe058,0x402614,0x402615,0x402612,0x402360,
                                   0x4024b8);
  }
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00049788(void)

{
  undefined8 uVar1;
  uint uVar2;
  
  func_0x00383000(7);
  if (bRam003ef400 < 5) {
                    // WARNING: Could not emulate address calculation at 0x000497dc
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003ef400 * 4 + 0x3fe080))();
    return;
  }
  func_0x00382ab8(7,0);
  uVar2 = 0;
  func_0x00382ad8((int)((uRam0040205a + 4) * 0x10000) >> 0x10,
                  (int)((uRam0040205c + 2) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,*(undefined4 *)((short)uRam00402358 * 4 + 0x3e78c0));
  func_0x00382ad8((int)((uRam0040205a + 0x82) * 0x10000) >> 0x10,
                  (int)((uRam0040205c + 2) * 0x10000) >> 0x10);
  uVar1 = func_0x003d0ae8(uRam00402358);
  func_0x00382c20(1,1,uVar1);
  if (*(short *)((short)uRam00402358 * 2 + 0xc046a8) != 0) {
    do {
      if (*(ushort *)
           ((uVar2 * 0x708 + (uint)*(ushort *)((short)uRam00402358 * 2 + 0xc04552)) * 2 + 0xc08812)
          < 0x50) {
        func_0x003973e0(1,1,(int)((uRam0040205a + 0x34) * 0x10000) >> 0x10,
                        (int)(((uint)uRam0040205c + uVar2 * 0xc + 0x20) * 0x10000) >> 0x10,
                        uVar2 & 0xff);
      }
      uVar2 = uVar2 + 1 & 0xffff;
    } while (uVar2 < *(ushort *)(((int)((uint)uRam00402358 << 0x10) >> 0xf) + 0xc046a8));
  }
  func_0x00382ad8((int)((uRam0040205a + 0x28) * 0x10000) >> 0x10,
                  (int)((uRam0040205c + 0x4c) * 0x10000) >> 0x10);
  uVar1 = func_0x003d12d8(uRam00402358);
  uVar1 = func_0x001383d0(uVar1);
  func_0x00382b60(1,1,0x3fe060,uVar1);
  func_0x00382ad8((int)((uRam0040205a + 0x28) * 0x10000) >> 0x10,
                  (int)((uRam0040205c + 0x54) * 0x10000) >> 0x10);
  uVar1 = func_0x003d0e88(0x299,*(undefined2 *)((short)uRam00402358 * 2 + 0xc00532));
  func_0x00382b60(1,1,0x3fe068,uVar1);
  func_0x00382ad8((int)((uRam0040205a + 0x28) * 0x10000) >> 0x10,
                  (int)((uRam0040205c + 0x5c) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fe070);
  uVar1 = func_0x003c72d8(0xa9d,uRam00402358);
  uVar1 = func_0x003d0e88(0xa9d,uVar1);
  func_0x00382b60(1,1,0x3fe078,uVar1);
  func_0x00382ab8(7,0);
  func_0x00382ad8((int)((uRam0040205a + 0x28) * 0x10000) >> 0x10,
                  (int)((uRam0040205c + 100) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fe070);
  uVar1 = func_0x003c72d8(0xd49,uRam00402358);
  uVar1 = func_0x003d0e88(0xd49,uVar1);
  func_0x00382b60(1,1,0x3fe078,uVar1);
  func_0x00382ab8(7,0);
  func_0x00382ad8((int)((uRam0040205a + 0x28) * 0x10000) >> 0x10,
                  (int)((uRam0040205c + 0x6c) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fe070);
  uVar1 = func_0x003c72d8(0xc9e,uRam00402358);
  func_0x003d0e88(0xc9e,uVar1);
  func_0x00382b60(1,1,0x3fe078);
  func_0x00382ab8(7,0);
  func_0x00382ad8((int)((uRam0040205a + 0x7c) * 0x10000) >> 0x10,
                  (int)((uRam0040205c + 0x5c) * 0x10000) >> 0x10);
  func_0x00382b60(1,1);
  sRam003ed124 = uRam0040205a + 4;
  sRam003ed0a4 = uRam0040205a + 0x7c;
  sRam003ed284 = uRam0040205a + 0x34;
  sRam003ed0e4 = uRam0040205a + 0x70;
  sRam003ed084 = uRam0040205a + 0x88;
  sRam003ed2a4 = uRam0040205a + 0xa6;
  sRam003ed2c4 = uRam0040205a + 0xbe;
  sRam003ed086 = uRam0040205c + 0x1a;
  sRam003ed126 = uRam0040205c + 0x52;
  sRam003ed146 = uRam0040205c + 0x5a;
  sRam003ed186 = uRam0040205c + 0x62;
  sRam003ed1a6 = uRam0040205c + 0x6a;
  sRam003ed0a6 = uRam0040205c + 0x56;
  sRam003ed1c6 = uRam0040205c + 0x72;
  sRam003ed0e6 = sRam003ed086;
  sRam003ed144 = sRam003ed124;
  sRam003ed184 = sRam003ed124;
  sRam003ed1a4 = sRam003ed124;
  sRam003ed1c4 = sRam003ed124;
  sRam003ed286 = sRam003ed086;
  sRam003ed2a6 = sRam003ed086;
  sRam003ed2c6 = sRam003ed086;
  func_0x00383a90(1,0x3ed278);
  func_0x00383a90(1,0x3ed0d8);
  func_0x00383a90(1,0x3ed078);
  func_0x00383a90(1,0x3ed298);
  func_0x00383a90(1,0x3ed2b8);
  func_0x00383a90(1,0x3ed118);
  func_0x00383a90(1,0x3ed138);
  func_0x00383a90(1,0x3ed178);
  func_0x00383a90(1,0x3ed198);
  func_0x00383a90(1,0x3ed1b8);
  func_0x00383a90(1,0x3ed098);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x0004ad98: Changing call to branch
// WARNING: Removing unreachable block (ram,0x0004ada0)

void FUN_0004a238(void)

{
  long lVar1;
  undefined8 uVar2;
  uint uVar3;
  
  func_0x00383000(7);
  if (6 < bRam003ef401) {
    func_0x00382ab8(7,0);
    lVar1 = func_0x00396ef8(uRam0040207a,uRam0040207c);
    if (bRam003ef401 == 2) {
      if (lVar1 == 1) {
        bRam00402068 = bRam00402068 & 0xf1 | 4;
        bRam00402080 = bRam00402080 & 0xf1 | 4;
        uRam0040207c = uRam00402074;
        bRam003ef401 = 1;
        uRam00402094 = uRam0040208c;
        uRam0040207a = uRam00402072;
        uRam00402092 = uRam0040208a;
      }
      else {
        if (lVar1 == 2) {
          func_0x00396a98(0x42,0x107ea00,uRam00c00110);
          func_0x003bba50(0,0,0xfffffffffffffed4,0xa0,400);
          uRam00402096 = func_0x00396fb8(0x53,8,1,uRam00c00112);
          bRam003ef401 = 3;
        }
      }
    }
    func_0x00382ab8(7,0);
    uVar3 = 0;
    func_0x00382ad8((int)((uRam00402092 + 4) * 0x10000) >> 0x10,
                    (int)((uRam00402094 + 2) * 0x10000) >> 0x10);
    func_0x00382b60(1,1,*(undefined4 *)((short)uRam00402610 * 4 + 0x3e78c0));
    func_0x00382ad8((int)((uRam00402092 + 0x82) * 0x10000) >> 0x10,
                    (int)((uRam00402094 + 2) * 0x10000) >> 0x10);
    func_0x00382b60(1,1,0x3fe0b8);
    if (*(short *)((short)uRam00402610 * 2 + 0xc046a8) != 0) {
      do {
        if (*(ushort *)
             ((uVar3 * 0x708 + (uint)*(ushort *)((short)uRam00402610 * 2 + 0xc04552)) * 2 + 0xc08812
             ) < 0x50) {
          func_0x003973e0(1,1,(int)((uRam00402092 + 0x34) * 0x10000) >> 0x10,
                          (int)(((uint)uRam00402094 + uVar3 * 0xc + 0x20) * 0x10000) >> 0x10,
                          uVar3 & 0xff);
        }
        uVar3 = uVar3 + 1 & 0xffff;
      } while (uVar3 < *(ushort *)(((int)((uint)uRam00402610 << 0x10) >> 0xf) + 0xc046a8));
    }
    func_0x00382ad8((int)((uRam00402092 + 0x28) * 0x10000) >> 0x10,
                    (int)((uRam00402094 + 0x4c) * 0x10000) >> 0x10);
    uVar2 = func_0x003d12d8(uRam00402610);
    uVar2 = func_0x001383d0(uVar2);
    func_0x00382b60(1,1,0x3fe060,uVar2);
    func_0x00382ad8((int)((uRam00402092 + 0x28) * 0x10000) >> 0x10,
                    (int)((uRam00402094 + 0x54) * 0x10000) >> 0x10);
    uVar2 = func_0x003d0e88(0x299,*(undefined2 *)((short)uRam00402610 * 2 + 0xc00532));
    func_0x00382b60(1,1,0x3fe068,uVar2);
    func_0x00382ad8((int)((uRam00402092 + 0x28) * 0x10000) >> 0x10,
                    (int)((uRam00402094 + 0x5c) * 0x10000) >> 0x10);
    func_0x00382b60(1,1,0x3fe070);
    uVar2 = func_0x003c72d8(0xa9d,uRam00402610);
    uVar2 = func_0x003d0e88(0xa9d,uVar2);
    func_0x00382b60(1,1,0x3fe078,uVar2);
    func_0x00382ab8(7,0);
    func_0x00382ad8((int)((uRam00402092 + 0x28) * 0x10000) >> 0x10,
                    (int)((uRam00402094 + 100) * 0x10000) >> 0x10);
    func_0x00382b60(1,1,0x3fe070);
    uVar2 = func_0x003c72d8(0xd49,uRam00402610);
    uVar2 = func_0x003d0e88(0xd49,uVar2);
    func_0x00382b60(1,1,0x3fe078,uVar2);
    func_0x00382ab8(7,0);
    func_0x00382ad8((int)((uRam00402092 + 0x28) * 0x10000) >> 0x10,
                    (int)((uRam00402094 + 0x6c) * 0x10000) >> 0x10);
    func_0x00382b60(1,1,0x3fe070);
    uVar2 = func_0x003c72d8(0xc9e,uRam00402610);
    func_0x003d0e88(0xc9e,uVar2);
    func_0x00382b60(1,1,0x3fe078);
    func_0x00382ab8(7,0);
    func_0x00382ad8((int)((uRam00402092 + 0x7c) * 0x10000) >> 0x10,
                    (int)((uRam00402094 + 0x5c) * 0x10000) >> 0x10);
    func_0x00382b60(1,1);
    sRam003ed124 = uRam00402092 + 4;
    sRam003ed0a4 = uRam00402092 + 0x7c;
    sRam003ed284 = uRam00402092 + 0x34;
    sRam003ed0e4 = uRam00402092 + 0x70;
    sRam003ed084 = uRam00402092 + 0x88;
    sRam003ed2a4 = uRam00402092 + 0xa6;
    sRam003ed2c4 = uRam00402092 + 0xbe;
    sRam003ed086 = uRam00402094 + 0x1a;
    sRam003ed126 = uRam00402094 + 0x52;
    sRam003ed146 = uRam00402094 + 0x5a;
    sRam003ed186 = uRam00402094 + 0x62;
    sRam003ed1a6 = uRam00402094 + 0x6a;
    sRam003ed0a6 = uRam00402094 + 0x56;
    sRam003ed1c6 = uRam00402094 + 0x72;
    sRam003ed0e6 = sRam003ed086;
    sRam003ed144 = sRam003ed124;
    sRam003ed184 = sRam003ed124;
    sRam003ed1a4 = sRam003ed124;
    sRam003ed1c4 = sRam003ed124;
    sRam003ed286 = sRam003ed086;
    sRam003ed2a6 = sRam003ed086;
    sRam003ed2c6 = sRam003ed086;
    func_0x00383a90(1,0x3ed278);
    func_0x00383a90(1,0x3ed0d8);
    func_0x00383a90(1,0x3ed078);
    func_0x00383a90(1,0x3ed298);
    func_0x00383a90(1,0x3ed2b8);
    func_0x00383a90(1,0x3ed118);
    func_0x00383a90(1,0x3ed138);
    func_0x00383a90(1,0x3ed178);
    func_0x00383a90(1,0x3ed198);
    func_0x00383a90(1,0x3ed1b8);
    func_0x00383a90(1,0x3ed098);
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
                    // WARNING: Could not emulate address calculation at 0x0004a28c
                    // WARNING: Treating indirect jump as call
  (**(code **)((uint)bRam003ef401 * 4 + 0x3fe0d0))();
  return;
}



void FUN_0004ade0(void)

{
  int iVar1;
  
  iVar1 = 0;
  do {
    func_0x003d1490(iVar1,0x1000);
    iVar1 = (iVar1 + 1) * 0x10000 >> 0x10;
  } while (iVar1 < 0xab);
  uRam004028ca = 0;
  uRam004028cb = 0;
  return;
}



void FUN_0004ae38(void)

{
  byte bVar1;
  int iVar2;
  long lVar3;
  int iVar4;
  
  iVar4 = 0;
  uRam004028c8 = 0;
  do {
    lVar3 = func_0x003d05d8();
    if (lVar3 != 0) {
      iVar2 = (int)(short)uRam004028c8;
      uRam004028c8 = uRam004028c8 + 1;
      *(undefined2 *)(iVar2 * 2 + 0x402770) = 3;
      *(short *)(iVar2 * 2 + 0x402618) = (short)iVar4;
    }
    iVar4 = (iVar4 + 1) * 0x10000 >> 0x10;
  } while (iVar4 < 0xab);
  func_0x003d16d8(0x402618,0x402770,uRam004028c8);
  bVar1 = bRam004028ca;
  *(undefined2 *)((short)uRam004028c8 * 2 + 0x402618) = 0x7fff;
  *(undefined2 *)(((int)((uint)(ushort)(uRam004028c8 + 1) << 0x10) >> 0xf) + 0x402618) = 1;
  uRam003ef404 = *(undefined2 *)((uint)bVar1 * 2 + 0x402618);
  *(undefined2 *)((short)uRam004028c8 * 2 + 0x402770) = 3;
  uRam004028c8 = uRam004028c8 + 1;
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004af80(undefined2 param_1)

{
  long lVar1;
  
  lVar1 = func_0x003d05d8(param_1,uRam00c00110);
  if (lVar1 == 1) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  return;
}



void FUN_0004afd8(void)

{
  if (cRam003ef402 == '\0') {
    func_0x003cb738();
    cRam003ef402 = '\x01';
  }
  uRam003ef404 = func_0x003c9988(0x3eed48,0x3fe0f0,0x3fe100,0x4028ca,0x4028cb,0x4028c8,0x402618,
                                 0x402770);
  if (cRam003eed52 == -1) {
    cRam003ef402 = '\0';
  }
  return;
}



void FUN_0004b090(void)

{
  int iVar1;
  
  iVar1 = 0;
  do {
    func_0x003d1490(iVar1,0x800);
    iVar1 = (iVar1 + 1) * 0x10000 >> 0x10;
  } while (iVar1 < 0xab);
  uRam003efea0 = 0;
  uRam003efea1 = 0;
  uRam003f0166 = 0;
  uRam003f0167 = 0;
  return;
}



void FUN_0004b0f8(void)

{
  byte bVar1;
  short sVar2;
  int iVar3;
  long lVar4;
  undefined2 uVar5;
  int iVar6;
  
  iVar6 = 0;
  sVar2 = 0;
  sRam003efe9e = 0;
  do {
    if ((0x17 < *(ushort *)(iVar6 + 0xc066b8)) && (*(short *)(iVar6 + 0xc068b8) == sRam00c00110)) {
      sRam003efe9c = sVar2;
      func_0x003cc7f8();
      iVar3 = (int)sRam003efe9e;
      *(short *)(iVar3 * 2 + 0x3efdf0) = sVar2;
      lVar4 = func_0x003ccaa8();
      uVar5 = 3;
      if (lVar4 != 1) {
        uVar5 = 0;
      }
      *(undefined2 *)(iVar3 * 2 + 0x3efe48) = uVar5;
      sRam003efe9e = sRam003efe9e + 1;
    }
    bVar1 = bRam003efea0;
    sVar2 = sVar2 + 1;
    iVar6 = iVar6 + 2;
  } while (sVar2 < 0x80);
  iVar3 = (int)sRam003efe9e;
  iVar6 = (int)((uint)(ushort)(sRam003efe9e + 1) << 0x10) >> 0xf;
  *(undefined2 *)(iVar3 * 2 + 0x3efdf0) = 0xffff;
  *(undefined2 *)(iVar6 + 0x3efdf0) = 0xfffe;
  sRam003efe9c = *(undefined2 *)((uint)bVar1 * 2 + 0x3efdf0);
  sRam003efe9e = sRam003efe9e + 2;
  *(undefined2 *)(iVar3 * 2 + 0x3efe48) = 3;
  *(undefined2 *)(iVar6 + 0x3efe48) = 3;
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004b288(void)

{
  *(undefined2 *)(sRam003efe9e * 2 + 0x3efdf0) = 0;
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Removing unreachable block (ram,0x0004bb6c)
// WARNING: Removing unreachable block (ram,0x0004baa8)
// WARNING: Removing unreachable block (ram,0x0004bbe4)

void FUN_0004b2c8(short *param_1,undefined8 param_2)

{
  short sVar1;
  short sVar2;
  short sVar3;
  uint uVar4;
  long lVar5;
  int iVar6;
  int iVar7;
  short *psVar8;
  long lVar9;
  int iVar10;
  
  func_0x00383000();
  if (bRam003efdc4 < 10) {
                    // WARNING: Could not emulate address calculation at 0x0004b328
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003efdc4 * 4 + 0x3fe120))();
    return;
  }
  func_0x00382ab8(7,0);
  func_0x003b9a08(3,0x200);
  if (bRam003efdc4 == 6) {
    lVar9 = (long)param_1[2];
    lVar5 = (long)(sRam003efe9e + -2);
    if (lVar9 < lVar5) {
      *(undefined2 *)((short)uRam004028f4 * 2 + 0x4028e8) =
           *(undefined2 *)(((int)(short)uRam004028f4 + 1U & 1) * 2 + 0x4028e8);
      bRam003efea0 = *(byte *)(param_1 + 2);
      cRam003efea1 = *(char *)param_1;
      uRam003efe9c = *(undefined2 *)((uint)bRam003efea0 * 2 + 0x3efdf0);
      sRam004028f0 = sRam004028e8 / 0xf + 1;
      *(byte *)(param_1 + 4) = *(byte *)(param_1 + 4) & 0xfe;
      param_1[2] = 0;
      bRam003efdc4 = 1;
      uRam003ed41c = 0;
    }
    else {
      if (lVar9 == lVar5) {
        bRam003efdc4 = 7;
        *(undefined2 *)(((int)(short)uRam004028f4 + 1U & 1) * 2 + 0x4028e8) =
             *(undefined2 *)((short)uRam004028f4 * 2 + 0x4028e8);
        sRam004028f0 = sRam004028e8 / 0xf + 1;
        *(byte *)(param_1 + 4) = *(byte *)(param_1 + 4) & 0xfe;
      }
      else {
        if ((lVar9 != sRam003efe9e + -1) && (lVar9 != 0x3fff)) goto LAB_0004bc70;
        *(undefined2 *)(((int)(short)uRam004028f4 + 1U & 1) * 2 + 0x4028e8) =
             *(undefined2 *)((short)uRam004028f4 * 2 + 0x4028e8);
        sRam004028f0 = sRam004028e8 / 0xf + 1;
        func_0x003979d8(0xffffffffffffffff,0x108,8);
        cRam003efea1 = *(char *)param_1;
        bRam003efdc4 = 1;
        bRam003efea0 = *(char *)(param_1 + 3) + cRam003efea1;
        uRam003ecf72 = 1;
        param_1[2] = 0x3fff;
        *(byte *)(param_1 + 4) = *(byte *)(param_1 + 4) & 0xfe;
      }
    }
    bRam004028d0 = bRam004028d0 & 0xf1 | 4;
    uRam004028e2 = uRam004028da;
    uRam004028e4 = uRam004028dc;
  }
LAB_0004bc70:
  lVar9 = 0;
  func_0x00382ab8(7,0);
  iVar10 = 0;
  func_0x00382ad8((int)((uRam004028e2 + 0x10) * 0x10000) >> 0x10,
                  (int)((uRam004028e4 + 2) * 0x10000) >> 0x10);
  func_0x00382b60(3,1,param_2);
  sRam003ed144 = uRam004028e2 + 0x40;
  sRam003ed146 = uRam004028e4 + 0x1a;
  func_0x00383a90(3,0x3ed138);
  func_0x00397c08(2);
  psVar8 = (short *)0x3ef528;
  do {
    if (0x17 < *(ushort *)(iVar10 + 0xc066b8)) {
      if (*(ushort *)(iVar10 + 0xc068b8) < 8) {
        iVar6 = (int)(short)*(ushort *)(iVar10 + 0xc068b8);
      }
      else {
        iVar6 = 8;
      }
      if (((int)param_1[3] + (int)*param_1 < sRam003efe9e + -2) &&
         (iVar7 = iVar6 * 0x20,
         lVar9 == *(short *)(((int)param_1[3] + (int)*param_1) * 2 + 0x3efdf0))) {
        sVar1 = psVar8[1];
        sVar3 = *psVar8 - *(short *)((short)uRam004028f4 * 2 + 0x4028e8);
        *(undefined2 *)(iVar7 + 0x3ef408) = 1;
        *(short *)(iVar7 + 0x3ef414) = sVar3 + -8;
        *(short *)(iVar7 + 0x3ef416) = sVar1 + -10;
        *(short *)(iVar7 + 0x3ef41c) = sVar3 + 0x17;
        *(short *)(iVar7 + 0x3ef41e) = sVar1 + 6;
      }
      else {
        iVar6 = iVar6 * 0x20;
        sVar1 = *psVar8;
        sVar3 = psVar8[1];
        sVar2 = *(short *)((short)uRam004028f4 * 2 + 0x4028e8);
        *(undefined2 *)(iVar6 + 0x3ef408) = 0;
        *(short *)(iVar6 + 0x3ef414) = (sVar1 - sVar2) + -4;
        *(short *)(iVar6 + 0x3ef416) = sVar3 + -4;
      }
      func_0x00383a90();
    }
    psVar8 = psVar8 + 2;
    lVar9 = (long)(((int)lVar9 + 1) * 0x10000 >> 0x10);
    iVar10 = iVar10 + 2;
  } while (lVar9 < 0x80);
  uVar4 = uRam004028f4 + 1 & 1;
  uRam0020d084 = 1;
  iRam0020d1c0 = (int)((float)(int)*(short *)((uVar4 ^ 1) * 2 + 0x4028e8) * 65536.0);
  uRam004028f4 = (short)uVar4;
  return;
}



void FUN_0004bef8(void)

{
  int iVar1;
  long lVar2;
  undefined2 uVar3;
  short sVar4;
  uint uVar5;
  int iVar6;
  
  iVar6 = 0;
  uVar5 = 0;
  sRam003f0164 = 0;
  do {
    if ((((uVar5 != uRam00c00110) && (lVar2 = func_0x003d04f0(uVar5,uRam00c00110), lVar2 == 1)) &&
        (*(short *)(iVar6 + 0xc00130) != 6)) &&
       (((uint)*(ushort *)(iVar6 + 0xc007de) == (int)sRam003efe9c + 0x8000U &&
        (lVar2 = func_0x003c72d8(0xd49), 9 < lVar2)))) {
      iVar1 = (int)sRam003f0164;
      *(short *)(iVar1 * 2 + 0x3efea8) = (short)uVar5;
      lVar2 = func_0x003d14e8(uVar5);
      uVar3 = 3;
      if (lVar2 != 1) {
        uVar3 = 0;
      }
      *(undefined2 *)(iVar1 * 2 + 0x3f0008) = uVar3;
      sRam003f0164 = sRam003f0164 + 1;
    }
    uVar5 = SEXT24((short)((short)uVar5 + 1));
    iVar6 = iVar6 + 2;
  } while ((int)uVar5 < 0xab);
  func_0x003d16d8(0x3efea8,0x3f0008,sRam003f0164);
  sVar4 = sRam003f0164 + 1;
  uVar5 = (uint)bRam003f0166;
  *(undefined2 *)(sRam003f0164 * 2 + 0x3efea8) = 0x7fff;
  *(undefined2 *)(sRam003f0164 * 2 + 0x3f0008) = 3;
  if ((int)sVar4 <= (int)uVar5) {
    sRam003f0164._0_1_ = (char)sVar4;
    bRam003f0166 = (char)sRam003f0164 - 1;
    uVar5 = (uint)bRam003f0166;
  }
  iVar6 = sVar4 + -10;
  if (iVar6 < (int)(uint)bRam003f0167) {
    iVar1 = 0;
    if (-1 < iVar6) {
      iVar1 = iVar6;
    }
    bRam003f0167 = (byte)iVar1;
  }
  uRam003f0162 = *(undefined2 *)(uVar5 * 2 + 0x3efea8);
  sRam003f0164 = sVar4;
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004c108(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Removing unreachable block (ram,0x0004c224)

bool FUN_0004c1c0(void)

{
  ushort uVar1;
  int iVar2;
  int iVar3;
  bool bVar4;
  
  bVar4 = false;
  func_0x003cca28();
  if (sRam003f02c0 != 0) {
    uVar1 = *(ushort *)(sRam003f0162 * 2 + 0xc01a92);
    iVar2 = func_0x001881e8();
    iVar3 = func_0x001881e8();
    bVar4 = (iVar2 % 10) * 10 + iVar3 % 10 <= (int)((uVar1 - 2) * 3);
  }
  return bVar4;
}



// WARNING: Removing unreachable block (ram,0x0004c2f8)
// WARNING: Removing unreachable block (ram,0x0004c368)

undefined4 FUN_0004c268(void)

{
  ushort uVar1;
  short sVar2;
  int iVar3;
  int iVar4;
  short *psVar5;
  uint uVar6;
  undefined4 uVar7;
  
  iVar3 = 0;
  psVar5 = (short *)0xc082e8;
  sVar2 = sRam00c082e8;
  while( true ) {
    psVar5 = psVar5 + 1;
    if (sVar2 == 0) {
      uVar1 = *(ushort *)(sRam003f0162 * 2 + 0xc01a92);
      iVar3 = func_0x001881e8();
      iVar4 = func_0x001881e8();
      uVar7 = 0;
      if (((iVar3 % 10) * 10 + iVar4 % 10 <= (int)((uint)uVar1 * 7)) &&
         (uVar1 = *(ushort *)(sRam003f0162 * 2 + 0xc007de), uVar6 = uVar1 & 0x7fff, uVar7 = 0,
         (uVar1 & 0x8000) != 0)) {
        if (uVar6 < 0x22) {
          iVar3 = func_0x001881e8();
          uRam003f02c2 = *(ushort *)((uVar6 * 0x1e + iVar3 % 0x1e) * 2 + 0x3ef5c8);
          if (0 < (int)((uint)uRam003f02c2 << 0x10)) {
            uVar7 = 1;
          }
        }
        else {
          uVar7 = 0;
        }
      }
      return uVar7;
    }
    iVar3 = (iVar3 + 1) * 0x10000 >> 0x10;
    if (0xfd < iVar3) break;
    sVar2 = *psVar5;
  }
  return 0;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004c3e0(void)

{
  *(undefined2 *)(sRam003f0164 * 2 + 0x3efea8) = 0;
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004c420(short *param_1,undefined8 param_2)

{
  short *psVar1;
  long lVar2;
  
  func_0x00383000(7);
  if (bRam003efdc5 < 7) {
                    // WARNING: Could not emulate address calculation at 0x0004c480
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003efdc5 * 4 + 0x3fe170))();
    return;
  }
  if ((uRam002afcb2 & 0x800) != 0) {
    func_0x003d16a8();
    func_0x003cc7f8();
  }
  func_0x00382ab8(7,0);
  func_0x003b9a08(1,0x100,(int)((uRam0040290a + 8) * 0x10000) >> 0x10,
                  (int)((uRam0040290c + 0x30) * 0x10000) >> 0x10,0x3e78c0,0x3efea8,0x3f0008,
                  sRam003f0164);
  if (bRam003efdc5 == 6) {
    lVar2 = (long)param_1[2];
    if (lVar2 < sRam003f0164 + -1) {
      bRam003f0166 = *(byte *)(param_1 + 2);
      cRam003f0167 = *(char *)param_1;
      uRam003efdc0 = 0;
      uRam003f0162 = *(undefined2 *)((uint)bRam003f0166 * 2 + 0x3efea8);
      *(byte *)(param_1 + 4) = *(byte *)(param_1 + 4) & 0xfe;
      param_1[2] = 0;
    }
    else {
      if ((lVar2 != sRam003f0164 + -1) && (lVar2 != 0x3fff)) goto LAB_0004c89c;
      cRam003f0167 = *(char *)param_1;
      bRam003f0166 = *(char *)(param_1 + 3) + cRam003f0167;
      param_1[2] = 0x3fff;
      *(byte *)(param_1 + 4) = *(byte *)(param_1 + 4) & 0xfe;
    }
    bRam003efdc5 = 1;
    bRam004028f8 = bRam004028f8 & 0xf1 | 4;
    uRam0040290a = uRam00402902;
    uRam0040290c = uRam00402904;
  }
LAB_0004c89c:
  func_0x00382ab8(7,0);
  psVar1 = (short *)(((int)param_1[3] + (int)*param_1) * 2 + 0x3efea8);
  if (sRam00402910 == *psVar1) {
    func_0x00396a98(0x42,0x107f300,sRam00402910);
  }
  else {
    sRam00402910 = *psVar1;
    func_0x00396a98(0x42,0x107f300);
  }
  func_0x00382ad8((int)((uRam0040290a + 4) * 0x10000) >> 0x10,
                  (int)((uRam0040290c + 2) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,param_2);
  func_0x00382ad8((int)((uRam0040290a + 0x84) * 0x10000) >> 0x10,
                  (int)((uRam0040290c + 2) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fe158);
  func_0x00382ad8((int)((uRam0040290a + 0xb0) * 0x10000) >> 0x10,
                  (int)((uRam0040290c + 8) * 0x10000) >> 0x10);
  func_0x00382b60(1,0,0x3fe160);
  func_0x00382ad8((int)((uRam0040290a + 0xb8) * 0x10000) >> 0x10,
                  (int)((uRam0040290c + 2) * 0x10000) >> 0x10);
  func_0x00382b60(1,1,0x3fe168);
  sRam003ed1a4 = uRam0040290a + 0xb4;
  sRam003ed064 = uRam0040290a + 0x14;
  sRam003ed066 = uRam0040290c + 0x18;
  sRam003ed144 = uRam0040290a + 0xa0;
  sRam003ed146 = sRam003ed066;
  sRam003ed1a6 = sRam003ed066;
  func_0x00383a90(1,0x3ed058);
  func_0x00383a90(1,0x3ed138);
  func_0x00383a90(1,0x3ed198);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004da00(void)

{
  if (bRam003efde8 < 5) {
                    // WARNING: Could not emulate address calculation at 0x0004da34
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003efde8 * 4 + 0x3fe220))();
    return;
  }
  func_0x00382ab8(7,0);
  if (sRam0040297e != -1) {
    func_0x003bbc08(sRam0040297e);
  }
  if (iRam00402988 != 1) {
    return;
  }
  sRam003efdd4 = sRam0040297a + 0x20;
  sRam003efdd6 = sRam0040297c + 0xf;
  func_0x00383a90(1,0x3efdc8);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004df00(void)

{
  int iVar1;
  
  if (bRam003efde9 == 1) {
    if ((short)uRam0040298e < -0x7f) {
      (*pcRam00c00048)();
      (*pcRam00c000ec)(0,9);
      uRam003ecf4b = uRam00c00110;
      func_0x003bba50(0,0,0xfffffffffffffed4,0xa0,400,0xa0);
      sRam0040298c = func_0x00396fb8(0x42,0x13,1,(undefined)uRam003f0162);
      bRam003efde9 = bRam003efde9 + 1;
      goto LAB_0004e178;
    }
    iVar1 = (uRam0040298e - 2) * 0x10000;
    uRam0040298e = (ushort)(uRam0040298e - 2);
  }
  else {
    if (bRam003efde9 < 2) {
      if (bRam003efde9 == 0) {
        func_0x00383000();
        func_0x00396a98(0x42,0x107ea00,uRam003f0162);
        sRam0040298c = -1;
        bRam003efde9 = bRam003efde9 + 1;
        uRam0040298e = 0;
      }
      goto LAB_0004e178;
    }
    if (bRam003efde9 == 2) {
      if (((bRam003ed480 & 8) != 0) && ((bRam003ed47c & 2) == 0)) {
        (*pcRam00c000ec)(1,2);
        bRam003ed480 = bRam003ed480 & 0xf1 | 4;
        bRam003efde9 = bRam003efde9 + 1;
        uRam003ed494 = uRam003ed48c;
        uRam003ed492 = uRam003ed48a;
      }
      goto LAB_0004e178;
    }
    if ((bRam003efde9 != 3) || ((bRam003ed480 & 8) == 0)) goto LAB_0004e178;
    if (-1 < (short)uRam0040298e) {
      (*pcRam00c00044)(0);
      uRam003eedc2 = 0xff;
      uRam003eedbc = 0x3fff;
      uRam003efdc0 = 1;
      bRam003efde9 = 0;
      goto LAB_0004e178;
    }
    uRam0040298e = uRam0040298e + 2;
    iVar1 = (uint)uRam0040298e << 0x10;
  }
  iVar1 = iVar1 >> 0x10;
  func_0x00147f88(0,0x3c,iVar1,iVar1,iVar1);
  func_0x00147f88(1,0xff00,0,0,0);
LAB_0004e178:
  func_0x00382ab8(7,0);
  if (sRam0040298c == -1) {
    return;
  }
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x0004e600: Changing call to branch
// WARNING: Removing unreachable block (ram,0x0004e608)

void FUN_0004e1c8(void)

{
  if (bRam003efdea < 6) {
                    // WARNING: Could not emulate address calculation at 0x0004e1fc
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003efdea * 4 + 0x3fe240))();
    return;
  }
  func_0x00382ab8(7,0);
  if ((sRam00402990 == -1) && (sRam00402992 == -1)) {
    return;
  }
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Removing unreachable block (ram,0x0004e8cc)
// WARNING: Removing unreachable block (ram,0x0004e718)
// WARNING: Removing unreachable block (ram,0x0004e878)
// WARNING: Removing unreachable block (ram,0x0004e7b8)

undefined8 FUN_0004e650(void)

{
  ushort uVar1;
  ushort uVar2;
  int iVar3;
  int iVar4;
  long lVar5;
  undefined8 uVar6;
  
  if (iRam003ecf44 == 1) {
    if ((uRam002afcb0 & 0x40) != 0) {
      uRam004029f2 = 2;
      return 1;
    }
    if ((uRam002afcb0 & 0x20) != 0) {
      uRam004029f2 = 1;
      return 0;
    }
    if ((uRam002afcb0 & 0x10) == 0) {
      uVar6 = 4;
    }
    else {
      uVar6 = 3;
    }
  }
  else {
    uVar1 = *(ushort *)(sRam003f0162 * 2 + 0xc01a92);
    uVar2 = *(ushort *)(sRam003f0162 * 2 + 0xc00532);
    iVar3 = func_0x001881e8();
    iVar4 = func_0x001881e8();
    lVar5 = func_0x003d0f90(*(ushort *)(sRam003f0162 * 2 + 0xc007de) & 0x7fff);
    if ((lVar5 < 0x32) && ((iVar3 % 10) * 10 + iVar4 % 10 <= (int)((uint)uVar1 * 5 + (uint)uVar2)))
    {
      uVar1 = *(ushort *)(sRam003f0162 * 2 + 0xc01a92);
      uVar2 = *(ushort *)(sRam003f0162 * 2 + 0xc00532);
      iVar3 = func_0x001881e8();
      iVar4 = func_0x001881e8();
      lVar5 = func_0x003d0f90(*(ushort *)(sRam003f0162 * 2 + 0xc007de) & 0x7fff);
      if ((lVar5 < 0x31) && ((iVar3 % 10) * 10 + iVar4 % 10 <= (int)((uVar1 - 2) * 3 + (uint)uVar2))
         ) {
        uRam004029f2 = 2;
        return 1;
      }
      uRam004029f2 = 1;
      return 0;
    }
    uVar1 = *(ushort *)(sRam003f0162 * 2 + 0xc01a92);
    uVar2 = *(ushort *)(sRam003f0162 * 2 + 0xc00532);
    iVar3 = func_0x001881e8();
    iVar4 = func_0x001881e8();
    uVar6 = 2;
    if ((int)((uVar1 - 8) * 2 + (uint)uVar2) < (iVar3 % 10) * 10 + iVar4 % 10) {
      iVar3 = func_0x001881e8();
      iVar4 = func_0x001881e8();
      uVar6 = 3;
      if (iVar3 % 6 + iVar4 % 6 + 2 < 7) {
        uVar6 = 4;
      }
    }
  }
  uRam004029f2 = 0;
  return uVar6;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004e918(void)

{
  *(undefined2 *)(sRam003efe9e * 2 + 0x3efdf0) = 1;
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004f068(void)

{
  if (bRam003f02c5 < 8) {
                    // WARNING: Could not emulate address calculation at 0x0004f098
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003f02c5 * 4 + 0x3fe2c0))();
    return;
  }
  func_0x00382ab8(7,0);
  if (sRam004029e0 != -1) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004f4f0(void)

{
  if (bRam003f02c6 < 6) {
                    // WARNING: Could not emulate address calculation at 0x0004f520
                    // WARNING: Treating indirect jump as call
    (**(code **)((uint)bRam003f02c6 * 4 + 0x3fe2e0))();
    return;
  }
  func_0x00382ab8(7,0);
  if (sRam004029e4 != -1) {
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004f8e0(int param_1,undefined param_2)

{
  int iVar1;
  
  if (bRam003f02c7 == 1) {
    if ((short)uRam004029f0 < -0x7f) {
      (*pcRam00c00048)();
      (*pcRam00c000ec)(1,1);
      func_0x003bba50(0,0,0xfffffffffffffed4,0xa0,400,0xa0);
      sRam004029ee = func_0x00396fb8(0x42,param_2,1,(undefined)uRam003f0162);
      bRam003f02c7 = bRam003f02c7 + 1;
      goto LAB_0004fb50;
    }
    iVar1 = (uRam004029f0 - 2) * 0x10000;
    uRam004029f0 = (ushort)(uRam004029f0 - 2);
  }
  else {
    if (bRam003f02c7 < 2) {
      if (bRam003f02c7 == 0) {
        func_0x00383000();
        func_0x00396a98(0x42,0x107ea00,uRam003f0162);
        sRam004029ee = -1;
        bRam003f02c7 = bRam003f02c7 + 1;
        uRam004029f0 = 0;
      }
      goto LAB_0004fb50;
    }
    if (bRam003f02c7 == 2) {
      if (((bRam003ed480 & 8) != 0) && ((bRam003ed47c & 2) == 0)) {
        (*pcRam00c000ec)(1,2);
        bRam003ed480 = bRam003ed480 & 0xf1 | 4;
        bRam003f02c7 = bRam003f02c7 + 1;
        uRam003ed494 = uRam003ed48c;
        uRam003ed492 = uRam003ed48a;
      }
      goto LAB_0004fb50;
    }
    if ((bRam003f02c7 != 3) || ((bRam003ed480 & 8) == 0)) goto LAB_0004fb50;
    if (-1 < (short)uRam004029f0) {
      (*pcRam00c00044)(0,0);
      *(undefined *)(param_1 + 10) = 0xff;
      *(undefined2 *)(param_1 + 4) = 0x3fff;
      uRam003efdc0 = 1;
      bRam003f02c7 = 0;
      goto LAB_0004fb50;
    }
    uRam004029f0 = uRam004029f0 + 2;
    iVar1 = (uint)uRam004029f0 << 0x10;
  }
  iVar1 = iVar1 >> 0x10;
  func_0x00147f88(0,0x3c,iVar1,iVar1,iVar1);
  func_0x00147f88(1,0xff00,0,0,0);
LAB_0004fb50:
  func_0x00382ab8(7,0);
  if (sRam004029ee == -1) {
    return;
  }
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004fbb0(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004fbd0(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_0004fd50(short param_1,short param_2)

{
  ushort uVar1;
  uint uVar2;
  int iVar3;
  uint uVar4;
  ushort *puVar5;
  uint uVar6;
  int iVar7;
  
  iVar7 = param_1 * 2;
  uVar6 = SEXT24(param_2);
  *(undefined2 *)(iVar7 + 0xc00130) = 5;
  *(short *)(iVar7 + 0xc00286) = param_2;
  *(undefined2 *)(iVar7 + 0xc003dc) = 0;
  func_0x003d0a90();
  puVar5 = (ushort *)(iVar7 + 0xc007de);
  if ((*puVar5 & 0x8000) == 0) {
    *puVar5 = *(ushort *)(uVar6 * 2 + 0xc007de);
    return;
  }
  iVar3 = (*puVar5 & 0x7fff) * 2;
  if ((0x17 < *(ushort *)(iVar3 + 0xc066b8)) && (*(ushort *)(iVar3 + 0xc068b8) == uVar6)) {
    return;
  }
  if ((uint)*(ushort *)(iVar3 + 0xc068b8) == (int)param_1) {
    *(ushort *)(iVar3 + 0xc068b8) = 0xab;
  }
  uVar1 = *(ushort *)(uVar6 * 2 + 0xc007de);
  if ((uVar1 & 0x8000) != 0) {
    *puVar5 = uVar1;
    return;
  }
  if (uRam00c066b8 < 0x18) {
    uVar2 = 1;
  }
  else {
    uVar2 = 1;
    if (uRam00c068b8 == uVar6) {
      *puVar5 = 0x8000;
      goto LAB_0004feb8;
    }
  }
  do {
    uVar4 = uVar2 & 0xffff;
    if (0x7f < uVar4) goto LAB_0004feb8;
    uVar2 = uVar4 + 1;
  } while ((*(ushort *)(uVar4 * 2 + 0xc066b8) < 0x18) ||
          (uVar2 = uVar4 + 1, *(ushort *)(uVar4 * 2 + 0xc068b8) != uVar6));
  *(short *)(iVar7 + 0xc007de) = (short)uVar4 + -0x8000;
LAB_0004feb8:
  *(undefined2 *)(iVar7 + 0xc007de) = *(undefined2 *)(uVar6 * 2 + 0xc007de);
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0004ff08(short param_1,undefined2 param_2)

{
  *(undefined2 *)(param_1 * 2 + 0xc00130) = 4;
  *(undefined2 *)(param_1 * 2 + 0xc00286) = param_2;
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



undefined4 FUN_000501e8(int param_1)

{
  ushort uVar1;
  int iVar2;
  uint uVar3;
  ushort *puVar4;
  undefined4 uVar5;
  
  iVar2 = (param_1 << 0x10) >> 0xf;
  puVar4 = (ushort *)(iVar2 + 0xc007de);
  if (*(short *)(iVar2 + 0xc00130) == 6) {
    uVar5 = 0x3fe2f8;
  }
  else {
    uVar1 = *puVar4;
    uVar3 = uVar1 & 0x7fff;
    if ((uVar1 & 0x8000) == 0) {
      func_0x00138518(0x4029f8,*(undefined4 *)
                                ((uint)*(ushort *)((uint)*puVar4 * 2 + 0xc052ae) * 4 + 0x3e78c0));
      func_0x00138560(0x4029f8,0x3fe300);
      return 0x4029f8;
    }
    if (*(ushort *)(uVar3 * 2 + 0xc066b8) < 0x18) {
      return 0x4029f8;
    }
    uVar5 = *(undefined4 *)(uVar3 * 4 + 0x3e7e30);
  }
  func_0x00138518(0x4029f8,uVar5);
  return 0x4029f8;
}



undefined4 FUN_000502e8(int param_1)

{
  ushort uVar1;
  int iVar2;
  uint uVar3;
  ushort *puVar4;
  
  iVar2 = (param_1 << 0x10) >> 0xf;
  puVar4 = (ushort *)(iVar2 + 0xc007de);
  if (*(short *)(iVar2 + 0xc00130) == 6) {
    func_0x00138518(0x402a28,0x3fe2f8);
  }
  else {
    uVar1 = *puVar4;
    uVar3 = uVar1 & 0x7fff;
    if ((uVar1 & 0x8000) == 0) {
      func_0x00138518(0x402a28,*(undefined4 *)
                                ((uint)*(ushort *)((uint)*puVar4 * 2 + 0xc052ae) * 4 + 0x3e7b78));
      func_0x00138560(0x402a28,0x3fe300);
    }
    else {
      if (0x17 < *(ushort *)(uVar3 * 2 + 0xc066b8)) {
        func_0x00138518(0x402a28,*(undefined4 *)(uVar3 * 4 + 0x3e7ed0));
        func_0x00138560(0x402a28,0x3fe308);
      }
    }
  }
  return 0x402a28;
}



undefined4 FUN_00050408(int param_1)

{
  ushort uVar1;
  int iVar2;
  uint uVar3;
  ushort *puVar4;
  undefined4 uVar5;
  
  iVar2 = (param_1 << 0x10) >> 0xf;
  puVar4 = (ushort *)(iVar2 + 0xc007de);
  if (*(short *)(iVar2 + 0xc00130) == 6) {
    uVar5 = 0x3fe2f8;
  }
  else {
    uVar1 = *puVar4;
    uVar3 = uVar1 & 0x7fff;
    if ((uVar1 & 0x8000) == 0) {
      func_0x00138518(0x402a58,*(undefined4 *)
                                ((uint)*(ushort *)((uint)*puVar4 * 2 + 0xc052ae) * 4 + 0x3e7b78));
      return 0x402a58;
    }
    if (*(ushort *)(uVar3 * 2 + 0xc066b8) < 0x18) {
      return 0x402a58;
    }
    uVar5 = *(undefined4 *)(uVar3 * 4 + 0x3e7ed0);
  }
  func_0x00138518(0x402a58,uVar5);
  return 0x402a58;
}



int FUN_00050808(short param_1)

{
  ushort uVar1;
  int iVar2;
  long lVar3;
  int iVar4;
  ushort *puVar5;
  int iVar6;
  
  iVar4 = param_1 * 2;
  iVar6 = 0;
  iVar2 = iVar6;
  if (0x17 < *(ushort *)(iVar4 + 0xc066b8)) {
    uVar1 = *(ushort *)(iVar4 + 0xc068b8);
    iVar2 = 0;
    if (uVar1 < 0xab) {
      iVar4 = 0;
      puVar5 = (ushort *)0xc007de;
      do {
        lVar3 = func_0x003d04f0(iVar4,uVar1);
        iVar4 = (iVar4 + 1) * 0x10000 >> 0x10;
        if ((lVar3 == 1) && ((uint)*puVar5 == (int)param_1 + 0x8000U)) {
          iVar6 = (iVar6 + 1) * 0x10000 >> 0x10;
        }
        puVar5 = puVar5 + 1;
        iVar2 = iVar6;
      } while (iVar4 < 0xab);
    }
  }
  return iVar2;
}



int FUN_000508f0(short param_1)

{
  ushort uVar1;
  int iVar2;
  long lVar3;
  int iVar4;
  ushort *puVar5;
  int iVar6;
  
  iVar4 = param_1 * 2;
  iVar6 = 0;
  iVar2 = iVar6;
  if (0x17 < *(ushort *)(iVar4 + 0xc066b8)) {
    uVar1 = *(ushort *)(iVar4 + 0xc068b8);
    iVar2 = 0;
    if (uVar1 < 0xab) {
      iVar4 = 0;
      puVar5 = (ushort *)0xc007de;
      do {
        lVar3 = func_0x003d0558(iVar4,uVar1);
        iVar4 = (iVar4 + 1) * 0x10000 >> 0x10;
        if ((lVar3 == 1) && ((uint)*puVar5 == (int)param_1 + 0x8000U)) {
          iVar6 = (iVar6 + 1) * 0x10000 >> 0x10;
        }
        puVar5 = puVar5 + 1;
        iVar2 = iVar6;
      } while (iVar4 < 0xab);
    }
  }
  return iVar2;
}



undefined4 FUN_000509d8(short param_1)

{
  int iVar1;
  int iVar2;
  
  iVar1 = func_0x003d13d8();
  iVar2 = param_1 * 2;
  iVar1 = iVar1 * 4 + -4;
  if ((uint)*(ushort *)(iVar2 + 0xc00286) != (int)param_1) {
    iVar1 = iVar1 + (uint)*(ushort *)(iVar2 + 0xc00688) * 0x14;
  }
  return *(undefined4 *)(iVar1 + 0x3e7fe8);
}



undefined4 FUN_00050a58(short param_1)

{
  int iVar1;
  int iVar2;
  
  iVar1 = func_0x003d13d8();
  iVar2 = param_1 * 2;
  iVar1 = iVar1 * 4 + -4;
  if ((uint)*(ushort *)(iVar2 + 0xc00286) != (int)param_1) {
    iVar1 = iVar1 + (uint)*(ushort *)(iVar2 + 0xc00688) * 0x14;
  }
  return *(undefined4 *)(iVar1 + 0x3e8118);
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x00050cd0: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00050cd8)
// WARNING: Removing unreachable block (ram,0x00050c44)

void FUN_00050c10(undefined2 param_1,undefined2 param_2,undefined8 param_3,undefined8 param_4,
                 int param_5)

{
  ushort uVar1;
  undefined8 uVar2;
  uint uVar3;
  int iVar4;
  
  iVar4 = (param_5 << 0x10) >> 0xf;
  uVar1 = *(ushort *)(iVar4 + 0xc01138);
  uVar3 = (uint)*(ushort *)(iVar4 + 0xc00fe2);
  if (uVar1 / 3 < uVar3) {
    uVar2 = 6;
    if (uVar1 >> 1 < uVar3) {
      uVar2 = 7;
    }
  }
  else {
    uVar2 = 2;
  }
  func_0x00382ab8(uVar2,0);
  func_0x00382ad8(param_1,param_2);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_00050dd8(short *param_1,undefined8 param_2,short param_3)

{
  short sVar1;
  undefined2 uVar2;
  int iVar3;
  short sVar4;
  undefined2 *puVar5;
  int iVar6;
  uint uVar7;
  short *psVar8;
  int iVar9;
  int iVar10;
  ushort *puVar11;
  int iVar12;
  undefined2 *puVar13;
  
  iVar12 = (int)param_3;
  psVar8 = (short *)0x402a88;
  puVar5 = (undefined2 *)0x402be0;
  puVar13 = (undefined2 *)param_2;
  if (sRam003f02c8 == 1) {
    iVar10 = 0;
    puVar11 = (ushort *)0xc066b8;
    do {
      if ((0x17 < *puVar11) && (iVar6 = 0, 0 < iVar12)) {
        iVar3 = 0;
        do {
          iVar9 = *(short *)(iVar3 + (int)param_1) * 2;
          if ((*(short *)(iVar9 + 0xc00130) != 6) &&
             ((uint)*(ushort *)(iVar9 + 0xc007de) == iVar10 + 0x8000U)) {
            *psVar8 = *(short *)(iVar3 + (int)param_1);
            psVar8 = psVar8 + 1;
            *puVar5 = *(undefined2 *)(iVar3 + (int)puVar13);
            puVar5 = puVar5 + 1;
          }
          iVar6 = (int)(short)((short)iVar6 + 1);
          iVar3 = iVar6 << 1;
        } while (iVar6 < iVar12);
      }
      iVar10 = (int)(short)((short)iVar10 + 1);
      puVar11 = puVar11 + 1;
    } while (iVar10 < 0x80);
    uVar7 = 0;
    puVar11 = (ushort *)0xc052ae;
    do {
      if ((*(ushort *)((uint)*puVar11 * 2 + 0xc00286) == uRam00c00110) && (iVar10 = 0, 0 < iVar12))
      {
        iVar6 = 0;
        do {
          iVar3 = *(short *)(iVar6 + (int)param_1) * 2;
          if ((*(short *)(iVar3 + 0xc00130) != 6) && (*(ushort *)(iVar3 + 0xc007de) == uVar7)) {
            *psVar8 = *(short *)(iVar6 + (int)param_1);
            psVar8 = psVar8 + 1;
            *puVar5 = *(undefined2 *)(iVar6 + (int)puVar13);
            puVar5 = puVar5 + 1;
          }
          iVar10 = (int)(short)((short)iVar10 + 1);
          iVar6 = iVar10 << 1;
        } while (iVar10 < iVar12);
      }
      uVar7 = SEXT24((short)((short)uVar7 + 1));
      puVar11 = puVar11 + 1;
    } while ((int)uVar7 < 0xab);
    iVar10 = 0;
    if (0 < iVar12) {
      iVar6 = 0;
      do {
        if (*(short *)(*(short *)(iVar6 + (int)param_1) * 2 + 0xc00130) == 6) {
          *psVar8 = *(short *)(iVar6 + (int)param_1);
          psVar8 = psVar8 + 1;
          *puVar5 = *(undefined2 *)(iVar6 + (int)puVar13);
          puVar5 = puVar5 + 1;
        }
        iVar10 = (int)(short)((short)iVar10 + 1);
        iVar6 = iVar10 << 1;
      } while (iVar10 < iVar12);
    }
  }
  else {
    if (sRam003f02c8 < 2) {
      if ((sRam003f02c8 == 0) && (iVar10 = 1, 1 < iVar12)) {
        do {
          iVar6 = (iVar10 + -1) * 0x10000 >> 0x10;
          sVar1 = param_1[iVar10];
          uVar2 = puVar13[iVar10];
          if (iVar6 < 0) {
LAB_00050f40:
            iVar3 = iVar6 << 1;
          }
          else {
            iVar3 = iVar6 * 2;
            iVar9 = sVar1 * 2;
            psVar8 = param_1 + iVar6;
            if (*(short *)(iVar9 + 0x402d38) < *(short *)(*psVar8 * 2 + 0x402d38)) {
              puVar5 = puVar13 + iVar6;
              sVar4 = *psVar8;
              while( true ) {
                iVar3 = iVar3 + -2;
                psVar8[1] = sVar4;
                psVar8 = psVar8 + -1;
                iVar6 = (iVar6 + -1) * 0x10000 >> 0x10;
                puVar5[1] = *puVar5;
                puVar5 = puVar5 + -1;
                if (iVar6 < 0) break;
                if (*(short *)(*psVar8 * 2 + 0x402d38) <= *(short *)(iVar9 + 0x402d38))
                goto LAB_00050f48;
                sVar4 = *psVar8;
              }
              goto LAB_00050f40;
            }
          }
LAB_00050f48:
          iVar10 = (int)(short)((short)iVar10 + 1);
          *(short *)((int)param_1 + iVar3 + 2) = sVar1;
          *(undefined2 *)((int)puVar13 + iVar3 + 2) = uVar2;
        } while (iVar10 < iVar12);
      }
      goto LAB_000512f8;
    }
    if (sRam003f02c8 != 2) {
      if ((sRam003f02c8 == 3) && (iVar10 = 0, 0 < iVar12)) {
        do {
          iVar6 = 0;
          if (0 < iVar12 + -1) {
            iVar3 = 0;
            do {
              psVar8 = (short *)(iVar3 + (int)param_1);
              puVar5 = (undefined2 *)(iVar3 + (int)puVar13);
              if (*(ushort *)(*psVar8 * 2 + 0xc02d46) <= *(ushort *)(*psVar8 * 2 + 0xc02e9c)) {
                sVar1 = psVar8[1];
                iVar3 = sVar1 * 2;
                uVar2 = puVar5[1];
                if (*(ushort *)(iVar3 + 0xc02e9c) < *(ushort *)(iVar3 + 0xc02d46)) {
                  psVar8[1] = *psVar8;
                  puVar5[1] = *puVar5;
                  *psVar8 = sVar1;
                  *puVar5 = uVar2;
                }
              }
              iVar6 = (iVar6 + 1) * 0x10000 >> 0x10;
              iVar3 = iVar6 << 1;
            } while (iVar6 < iVar12 + -1);
          }
          iVar10 = (iVar10 + 1) * 0x10000 >> 0x10;
        } while (iVar10 < iVar12);
      }
      goto LAB_000512f8;
    }
    uVar7 = 0x32;
    do {
      iVar10 = 0;
      if (0 < iVar12) {
        iVar6 = 0;
        do {
          if (*(ushort *)(*(short *)(iVar6 + (int)param_1) * 2 + 0xc00532) == uVar7) {
            *psVar8 = *(short *)(iVar6 + (int)param_1);
            psVar8 = psVar8 + 1;
            *puVar5 = *(undefined2 *)(iVar6 + (int)puVar13);
            puVar5 = puVar5 + 1;
          }
          iVar10 = (int)(short)((short)iVar10 + 1);
          iVar6 = iVar10 << 1;
        } while (iVar10 < iVar12);
      }
      uVar7 = (int)((uVar7 - 1) * 0x10000) >> 0x10;
    } while (-1 < (int)uVar7);
  }
  func_0x001873cc(param_1,0x402a88,iVar12 << 1);
  func_0x001873cc(param_2,0x402be0,iVar12 << 1);
LAB_000512f8:
  iVar10 = 0;
  if (0 < iVar12) {
    iVar6 = 0;
    psVar8 = param_1;
    do {
      sVar1 = *psVar8;
      if ((long)sVar1 == (ulong)uRam00c00110) {
        uVar2 = *(undefined2 *)(iVar6 + (int)puVar13);
        for (; 0 < iVar10; iVar10 = (iVar10 + -1) * 0x10000 >> 0x10) {
          puVar5 = (undefined2 *)(iVar6 + (int)puVar13);
          iVar6 = iVar6 + -2;
          *psVar8 = psVar8[-1];
          psVar8 = psVar8 + -1;
          *puVar5 = puVar5[-1];
        }
        *param_1 = sVar1;
        *puVar13 = uVar2;
        return;
      }
      psVar8 = psVar8 + 1;
      iVar10 = (iVar10 + 1) * 0x10000 >> 0x10;
      iVar6 = iVar6 + 2;
    } while (iVar10 < iVar12);
  }
  return;
}



undefined8 FUN_000513a8(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (uRam003f08c8 < 5) {
                    // WARNING: Could not emulate address calculation at 0x000513f0
                    // WARNING: Treating indirect jump as call
      uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fe320))();
      return uVar1;
    }
  }
  return 1;
}



undefined8 FUN_000514b0(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(0x3c);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x8d);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00051538(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(0x35);
  }
  else {
    uVar1 = 0;
    if ((param_1 == 1) && (uVar1 = 0, uRam003f08c8 < 0x17)) {
                    // WARNING: Could not emulate address calculation at 0x00051598
                    // WARNING: Treating indirect jump as call
      uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fe340))();
      return uVar1;
    }
  }
  return uVar1;
}



undefined4 FUN_00051708(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xbf);
      }
      else {
        if (sRam003f08c8 != 1) {
          return 1;
        }
        sVar1 = func_0x003de140(0x32,0xc0,1,1);
      }
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined8 FUN_000517a8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd278(2);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    sVar1 = func_0x003de140(0x78,0xdd,1,0);
LAB_00051888:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0xdc;
LAB_0005187c:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_00051888;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        func_0x003de3c0(3);
        sVar1 = func_0x003de140(0x1f,0xde,1,1);
        goto LAB_00051888;
      }
      if (sRam003f08c8 == 3) {
        uVar2 = 0xdf;
        goto LAB_0005187c;
      }
    }
    uVar3 = 1;
    func_0x003d0848(2);
    func_0x003dd390(2);
    func_0x003dd3e0(0x10);
    func_0x003dd3e0(0x11);
    func_0x003dd3e0(0x12);
    func_0x003dd3e0(0x13);
  }
  return uVar3;
}



undefined8 FUN_000518e0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd228(2);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    func_0x003de3c0(3);
    sVar1 = func_0x003de140(0x1f,0xde,1,1);
LAB_00051998:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0xe7;
        goto LAB_0005198c;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0xdf;
LAB_0005198c:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_00051998;
      }
    }
    uVar3 = 1;
    func_0x003dd390(2);
    func_0x003dd3e0(0x10);
    func_0x003dd3e0(0x11);
    func_0x003dd3e0(0x12);
    func_0x003dd3e0(0x13);
  }
  return uVar3;
}



undefined8 FUN_000519e8(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(3);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 5) {
                    // WARNING: Could not emulate address calculation at 0x00051a48
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fe3a0))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(3);
      func_0x003dd3e0(0x14);
      func_0x003dd3e0(0x15);
      func_0x003dd3e0(0x16);
      func_0x003dd3e0(0x17);
    }
  }
  return uVar1;
}



undefined8 FUN_00051b38(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(4);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 6) {
                    // WARNING: Could not emulate address calculation at 0x00051b98
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fe3c0))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(4);
      func_0x003dd3e0(0x18);
      func_0x003dd3e0(0x19);
      func_0x003dd3e0(0x1a);
      func_0x003dd3e0(0x1b);
    }
  }
  return uVar1;
}



undefined8 FUN_00051c90(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(5);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 5) {
                    // WARNING: Could not emulate address calculation at 0x00051cf0
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fe3e0))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(5);
      func_0x003dd3e0(0x1c);
      func_0x003dd3e0(0x1d);
      func_0x003dd3e0(0x1e);
      func_0x003dd3e0(0x1f);
    }
  }
  return uVar1;
}



undefined8 FUN_00051df0(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(6);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 0xb) {
                    // WARNING: Could not emulate address calculation at 0x00051e58
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fe400))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(6);
      func_0x003dd3e0(0x20);
      func_0x003dd3e0(0x21);
      func_0x003dd3e0(0x22);
      func_0x003dd3e0(0x23);
    }
  }
  return uVar1;
}



undefined8 FUN_000520d8(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(7);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 9) {
                    // WARNING: Could not emulate address calculation at 0x00052138
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fe450))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(7);
      uRam00c01ea2 = uRam00c01ea2 & 0xff00 | 0x8a;
      uRam00c04560 = 0xb2;
      func_0x003dd3e0(0x24);
      func_0x003dd3e0(0x25);
      func_0x003dd3e0(0x26);
      func_0x003dd3e0(0x27);
    }
  }
  return uVar1;
}



undefined8 FUN_000522a0(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(0x2a);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x3df);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003dd390(0x2a);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00052338(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x407);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_000523b0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x4bb);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00052428(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (uRam003f08c8 < 8) {
                    // WARNING: Could not emulate address calculation at 0x00052480
                    // WARNING: Treating indirect jump as call
      uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fe480))();
      return uVar1;
    }
    func_0x003d0848(0x7c);
    func_0x003dd390(0x7c);
  }
  return 1;
}



undefined8 FUN_00052598(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (uRam003f08c8 < 0xc) {
                    // WARNING: Could not emulate address calculation at 0x000525dc
                    // WARNING: Treating indirect jump as call
      uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fe4a0))();
      return uVar1;
    }
    func_0x003dd798();
  }
  return 1;
}



undefined4 FUN_00052758(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  long lVar3;
  undefined8 uVar4;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        lVar3 = func_0x003dd250(2);
        if (lVar3 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar4 = 0x7f0;
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar4 = 0x7ef;
          if (sRam003f08c8 != 0) {
            return 1;
          }
        }
        else {
          uVar4 = 0x7f1;
          if (sRam003f08c8 != 2) {
            return 1;
          }
        }
      }
      sVar1 = func_0x003ddb60(uVar4);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



bool FUN_00052820(short param_1)

{
  bool bVar1;
  short sVar2;
  int iVar3;
  
  bVar1 = false;
  if (param_1 == 0) {
    sVar2 = 8;
    iVar3 = 0x10;
    do {
      if ((*(short *)(iVar3 + 0xc00130) == 2) && (*(short *)(iVar3 + 0xc04954) != 0)) {
        bVar1 = true;
        func_0x003d0848(sVar2);
        func_0x003dd390(sVar2);
      }
      sVar2 = sVar2 + 1;
      iVar3 = iVar3 + 2;
    } while (sVar2 < 0x2b);
  }
  else {
    bVar1 = false;
    if (param_1 == 1) {
      bVar1 = sRam003f08c8 == 0;
      if (bVar1) {
        sVar2 = func_0x003ddb60(0x8cb);
        sRam003f08c8 = sRam003f08c8 + sVar2;
      }
      bVar1 = !bVar1;
    }
  }
  return bVar1;
}



undefined8 FUN_00052910(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  long lVar3;
  undefined4 uVar4;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(0x2c);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        uVar4 = 0xc59;
      }
      else {
        if (sRam003f08c8 != 1) {
          func_0x003dd390(0x2c);
          func_0x003dd3e0(0x37);
          return 1;
        }
        lVar3 = func_0x003dd228(0x37);
        if (lVar3 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar4 = 0xc5a;
      }
      sVar1 = func_0x003ddb60(uVar4);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_000529f0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0xd5d;
      }
      else {
        if (sRam003f08c8 != 1) {
          func_0x003d0848(0x3e);
          func_0x003dd390(0x3e);
          func_0x003dd438(0x3e,9,0);
          return 1;
        }
        uVar2 = 0xd5e;
      }
      sVar1 = func_0x003ddb60(uVar2);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_00052a98(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xd85);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(0x3e);
        func_0x003dd390(0x3e);
        func_0x003dd438(0x3e,9,0);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00052b38(short param_1)

{
  short sVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    uVar3 = 1;
  }
  else {
    uVar3 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        uVar3 = 0xddf;
      }
      else {
        if (sRam003f08c8 != 1) {
          return 1;
        }
        lVar2 = func_0x003dd200(3);
        if (lVar2 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar3 = 0xde0;
      }
      sVar1 = func_0x003ddb60(uVar3);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar3 = 0;
    }
  }
  return uVar3;
}



undefined8 FUN_00052be8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd250(0x27);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    sVar1 = func_0x003de140(0x1a,0xe1c,1,1);
LAB_00052c98:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0xe1b;
        goto LAB_00052c8c;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0xe1d;
LAB_00052c8c:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_00052c98;
      }
    }
    uVar3 = 1;
    func_0x003dd3e0(0x27);
  }
  return uVar3;
}



undefined4 FUN_00052cc8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xfbf);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00052d40(short param_1)

{
  short sVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    lVar2 = func_0x003dd8a8(0x194);
    if ((lVar2 != 1) && (lVar2 = func_0x003dd8a8(0x195), lVar2 != 1)) {
      return 0;
    }
    lVar2 = func_0x003dd8a8(0x9a);
    uVar3 = 0;
    if (lVar2 == 1) {
      uVar3 = 1;
    }
  }
  else {
    uVar3 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xfdd);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar3 = 0;
      }
      else {
        uVar3 = 1;
      }
    }
  }
  return uVar3;
}



undefined4 FUN_00052df0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x100f);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(0x6a);
        func_0x003dd390(0x6a);
        uRam00c01cbc = 100;
        uRam00c00f60 = 3;
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



bool FUN_00052e98(short param_1)

{
  bool bVar1;
  short sVar2;
  long lVar3;
  
  if (param_1 == 0) {
    lVar3 = func_0x00125200();
    if (lVar3 == 0) {
      lVar3 = func_0x00125218();
      if (lVar3 == 0) {
        lVar3 = func_0x001251b8();
        if (lVar3 == 0) {
          lVar3 = func_0x001251d0();
          if (lVar3 == 0) {
            lVar3 = func_0x001251a8();
            bVar1 = false;
            if ((lVar3 == 0) && (bVar1 = false, 2 < uRam00c0011e)) {
              if (sRam00c00120 == 0) {
                bVar1 = false;
              }
              else {
                func_0x00124ec8();
                bVar1 = true;
              }
            }
          }
          else {
            bVar1 = false;
          }
        }
        else {
          bVar1 = false;
        }
      }
      else {
        bVar1 = false;
      }
    }
    else {
      bVar1 = false;
    }
  }
  else {
    bVar1 = false;
    if (param_1 == 1) {
      bVar1 = sRam003f08c8 == 0;
      if (bVar1) {
        sVar2 = func_0x003ddb60(0x2329);
        sRam003f08c8 = sRam003f08c8 + sVar2;
      }
      bVar1 = !bVar1;
    }
  }
  return bVar1;
}



undefined4 FUN_00052f90(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  undefined8 uVar4;
  undefined8 uVar5;
  undefined8 uVar6;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        uVar3 = 6;
        uVar4 = 2;
        uVar5 = 1;
        uVar6 = 0;
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar3 = 5;
          if (sRam003f08c8 != 0) {
            return 1;
          }
          uVar4 = 1;
          uVar5 = 0;
          uVar6 = 0;
        }
        else {
          if (sRam003f08c8 == 2) {
            uVar3 = 7;
            uVar4 = 3;
            uVar5 = 1;
            uVar6 = 0;
          }
          else {
            uVar3 = 8;
            if (sRam003f08c8 != 3) {
              return 1;
            }
            uVar4 = 4;
            uVar5 = 1;
            uVar6 = 1;
          }
        }
      }
      sVar1 = func_0x003de140(uVar3,uVar4,uVar5,uVar6);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_00053088(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x6f);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00053100(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  short *psVar3;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(0x5c);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 9) {
                    // WARNING: Could not emulate address calculation at 0x00053168
                    // WARNING: Treating indirect jump as call
        uVar2 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fe900))();
        return uVar2;
      }
      func_0x003dd390(0x5c);
      func_0x003dd3e0(0x5d);
      func_0x003dd3e0(0x5e);
      func_0x003dd3e0(0x5f);
      func_0x003dd3e0(0x60);
      func_0x003dd3e0(0x61);
      func_0x003dd3e0(0x62);
      func_0x003dd3e0(99);
      sVar1 = 0;
      psVar3 = (short *)0xc00286;
      do {
        if (*psVar3 == 0x5c) {
          func_0x003d0848(sVar1);
          func_0x003dd390(sVar1);
        }
        sVar1 = sVar1 + 1;
        psVar3 = psVar3 + 1;
      } while (sVar1 < 0xab);
      uVar2 = 1;
    }
  }
  return uVar2;
}



undefined4 FUN_00053340(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        sVar1 = func_0x003de140(0x67,0xb6,1,1);
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar2 = 0xb5;
          if (sRam003f08c8 != 0) {
            return 1;
          }
        }
        else {
          uVar2 = 0xb7;
          if (sRam003f08c8 != 2) {
            return 1;
          }
        }
        sVar1 = func_0x003ddb60(uVar2);
      }
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_00053400(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined4 uVar3;
  
  uVar2 = 0;
  if (param_1 == 0) {
    return 1;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    sVar1 = func_0x003de140(0x68,0xca,1,1);
LAB_000534a8:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar3 = 0xc9;
        goto LAB_0005349c;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar3 = 0xcb;
LAB_0005349c:
        sVar1 = func_0x003ddb60(uVar3);
        goto LAB_000534a8;
      }
    }
    uVar2 = 1;
    func_0x003dd438(0x98,4,100);
    func_0x003dd438(0x2b,10,100);
    func_0x003dd438(0x5b,10,100);
    func_0x003dd438(0x3f,9,100);
    uRam00c00304 = 1;
    uRam00c003b6 = 1;
    uRam00c002dc = 1;
    uRam00c0033c = 1;
    func_0x003df260(0x3f0428,4,uRam00c007e0,0x80);
  }
  return uVar2;
}



undefined8 FUN_00053550(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(2);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xfb);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_000535d8(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(3);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x15f);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00053660(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(4);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x1c3);
      }
      else {
        if (sRam003f08c8 != 1) {
          return 1;
        }
        sVar1 = func_0x003de140(0x7c,0x1c4,1,1);
      }
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined8 FUN_00053710(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(5);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x227);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00053798(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(6);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x28b);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00053820(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228();
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x2ef);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uRam00c01ea2 = uRam00c01ea2 & 0xff00 | 0x8a;
        uRam00c04560 = 0xb2;
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_000538d8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x3fd);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00053950(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (uRam003f08c8 < 5) {
                    // WARNING: Could not emulate address calculation at 0x000539a8
                    // WARNING: Treating indirect jump as call
      uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fe930))();
      return uVar1;
    }
    func_0x003d0848(0x7c);
    func_0x003dd390(0x7c);
  }
  return 1;
}



undefined4 FUN_00053a80(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0x7ef;
      }
      else {
        uVar2 = 0x7f2;
        if (sRam003f08c8 != 1) {
          return 1;
        }
      }
      sVar1 = func_0x003ddb60(uVar2);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



bool FUN_00053b08(short param_1)

{
  bool bVar1;
  short sVar2;
  int iVar3;
  
  bVar1 = false;
  if (param_1 == 0) {
    sVar2 = 8;
    iVar3 = 0x10;
    do {
      if ((*(short *)(iVar3 + 0xc00130) == 2) && (*(short *)(iVar3 + 0xc04954) != 0)) {
        bVar1 = true;
        func_0x003d0848(sVar2);
        func_0x003dd390(sVar2);
      }
      sVar2 = sVar2 + 1;
      iVar3 = iVar3 + 2;
    } while (sVar2 < 0x2b);
  }
  else {
    bVar1 = false;
    if (param_1 == 1) {
      bVar1 = sRam003f08c8 == 0;
      if (bVar1) {
        sVar2 = func_0x003ddb60(0x8cb);
        sRam003f08c8 = sRam003f08c8 + sVar2;
      }
      bVar1 = !bVar1;
    }
  }
  return bVar1;
}



undefined4 FUN_00053bf8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  undefined8 uVar4;
  undefined8 uVar5;
  undefined8 uVar6;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        uVar3 = 2;
        uVar4 = 2;
        uVar5 = 1;
        uVar6 = 0;
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar3 = 9;
          if (sRam003f08c8 != 0) {
            return 1;
          }
          uVar4 = 1;
          uVar5 = 0;
          uVar6 = 0;
        }
        else {
          if (sRam003f08c8 == 2) {
            uVar3 = 10;
            uVar4 = 3;
            uVar5 = 1;
            uVar6 = 0;
          }
          else {
            uVar3 = 0xb;
            if (sRam003f08c8 != 3) {
              return 1;
            }
            uVar4 = 4;
            uVar5 = 1;
            uVar6 = 1;
          }
        }
      }
      sVar1 = func_0x003de140(uVar3,uVar4,uVar5,uVar6);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_00053cf0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x6f);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00053d68(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(100);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 5) {
                    // WARNING: Could not emulate address calculation at 0x00053dc8
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3febd0))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(100);
      func_0x003dd3e0(0x65);
      func_0x003dd3e0(0x66);
      func_0x003dd3e0(0x67);
    }
  }
  return uVar1;
}



undefined8 FUN_00053eb0(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(3);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 9) {
                    // WARNING: Could not emulate address calculation at 0x00053f10
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3febf0))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(3);
      func_0x003dd3e0(0x14);
      func_0x003dd3e0(0x15);
      func_0x003dd3e0(0x16);
      func_0x003dd3e0(0x17);
    }
  }
  return uVar1;
}



undefined8 FUN_00054078(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd228(4);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    func_0x003de3c0(3);
    sVar1 = func_0x003de140(0x21,0x1a6,1,1);
LAB_00054130:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0x1a5;
        goto LAB_00054124;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0x1a7;
LAB_00054124:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_00054130;
      }
    }
    uVar3 = 1;
    func_0x003dd390(4);
    func_0x003dd3e0(0x18);
    func_0x003dd3e0(0x19);
    func_0x003dd3e0(0x1a);
    func_0x003dd3e0(0x1b);
  }
  return uVar3;
}



undefined8 FUN_00054180(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  long lVar3;
  undefined4 uVar4;
  int iVar5;
  int iVar6;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd278(4);
    return uVar2;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    sVar1 = func_0x003de140(0x7c,0x1b0,1,0);
    goto LAB_00054264;
  }
  if (sRam003f08c8 < 2) {
    uVar4 = 0x1af;
    if (sRam003f08c8 == 0) {
LAB_00054258:
      sVar1 = func_0x003ddb60(uVar4);
LAB_00054264:
      sRam003f08c8 = sRam003f08c8 + sVar1;
      return 0;
    }
  }
  else {
    if (sRam003f08c8 == 2) {
      func_0x003de3c0(3);
      sVar1 = func_0x003de140(0x21,0x1a6,1,1);
      goto LAB_00054264;
    }
    uVar4 = 0x1a7;
    if (sRam003f08c8 == 3) goto LAB_00054258;
  }
  if ((uRam00c007e6 & 0x8000) == 0) {
    iVar5 = 0;
    if (sRam00c003e4 == 1) {
      iVar6 = 0;
      do {
        lVar3 = func_0x003d05d8(iVar5,4);
        iVar5 = (iVar5 + 1) * 0x10000 >> 0x10;
        if ((lVar3 == 1) && (*(ushort *)(iVar6 + 0xc007de) == uRam00c007e6)) {
          *(undefined2 *)(iVar6 + 0xc00286) = 2;
        }
        iVar6 = iVar6 + 2;
      } while (iVar5 < 0xab);
      sRam00c003e4 = 1;
      goto LAB_000543bc;
    }
  }
  else {
    if (sRam00c003e4 == 2) {
      iVar5 = 0;
      *(undefined2 *)((uRam00c007e6 & 0x7fff) * 2 + 0xc068b8) = 2;
      iVar6 = 0;
      do {
        lVar3 = func_0x003d05d8(iVar5,4);
        iVar5 = (iVar5 + 1) * 0x10000 >> 0x10;
        if ((lVar3 == 1) && (*(ushort *)(iVar6 + 0xc007de) == uRam00c007e6)) {
          *(undefined2 *)(iVar6 + 0xc00286) = 2;
        }
        iVar6 = iVar6 + 2;
      } while (iVar5 < 0xab);
      sRam00c003e4 = 2;
      goto LAB_000543bc;
    }
  }
  func_0x003d0848(4);
  func_0x003dd390(4);
LAB_000543bc:
  func_0x003dd3e0(0x18);
  func_0x003dd3e0(0x19);
  func_0x003dd3e0(0x1a);
  func_0x003dd3e0(0x1b);
  return 1;
}



undefined8 FUN_00054400(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(5);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 8) {
                    // WARNING: Could not emulate address calculation at 0x00054460
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fec20))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(5);
      func_0x003dd3e0(0x1c);
      func_0x003dd3e0(0x1d);
      func_0x003dd3e0(0x1e);
      func_0x003dd3e0(0x1f);
    }
  }
  return uVar1;
}



undefined8 FUN_000545a8(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(5);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x21d);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00054630(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(5);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 8) {
                    // WARNING: Could not emulate address calculation at 0x00054690
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fec40))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(5);
      func_0x003dd3e0(0x1c);
      func_0x003dd3e0(0x1d);
      func_0x003dd3e0(0x1e);
      func_0x003dd3e0(0x1f);
    }
  }
  return uVar1;
}



undefined8 FUN_000547d8(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(6);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 10) {
                    // WARNING: Could not emulate address calculation at 0x00054840
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fec60))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(6);
      func_0x003d0848(0x20);
      func_0x003dd390(0x20);
      func_0x003d0848(0x21);
      func_0x003dd390(0x21);
      func_0x003d0848(0x22);
      func_0x003dd390(0x22);
      func_0x003d0848(0x23);
      func_0x003dd390(0x23);
    }
  }
  return uVar1;
}



undefined8 FUN_00054ac8(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd250(6);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 7) {
                    // WARNING: Could not emulate address calculation at 0x00054b28
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fecb0))();
        return uVar1;
      }
      uVar1 = 1;
    }
  }
  return uVar1;
}



undefined8 FUN_00054be8(short param_1)

{
  short sVar1;
  long lVar2;
  undefined4 uVar3;
  undefined8 uVar4;
  
  uVar4 = 0;
  if (param_1 == 0) {
    uVar4 = func_0x003dd228(7);
    return uVar4;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    func_0x003de3c0(3);
    sVar1 = func_0x003de140(0x1c,0xaa2,1,1);
LAB_00054d08:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar3 = 0x2d1;
        goto LAB_00054cfc;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        lVar2 = func_0x003dd8a8(0x16);
        if (lVar2 == 1) {
          sVar1 = func_0x003de140(0x24,0xaa3,1,1);
          sRam003f08c8 = sRam003f08c8 + sVar1;
          func_0x003dd8d8(0x4b);
          return 0;
        }
        sRam003f08c8 = sRam003f08c8 + 1;
        return 0;
      }
      if (sRam003f08c8 == 3) {
        uVar3 = 0x2d4;
LAB_00054cfc:
        sVar1 = func_0x003ddb60(uVar3);
        goto LAB_00054d08;
      }
    }
    uRam00c01ea2 = uRam00c01ea2 & 0xff00 | 0x8a;
    uRam00c04560 = 0xb2;
    uVar4 = 1;
    func_0x003dd390(7);
    func_0x003dd3e0(0x24);
    func_0x003dd3e0(0x25);
    func_0x003dd3e0(0x26);
    func_0x003dd3e0(0x27);
  }
  return uVar4;
}



undefined8 FUN_00054d80(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd250(7);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        func_0x003de3c0(3);
        sVar1 = func_0x003de140(0x24,0x2f0,1,1);
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar3 = 0x2ef;
          if (sRam003f08c8 != 0) {
            return 1;
          }
        }
        else {
          uVar3 = 0x2f1;
          if (sRam003f08c8 != 2) {
            return 1;
          }
        }
        sVar1 = func_0x003ddb60(uVar3);
      }
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined8 FUN_00054e58(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(0x2a);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x3df);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003dd390(0x2a);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00054ef0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x407);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00054f68(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x4bb);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00054fe0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  long lVar3;
  undefined4 uVar4;
  
  uVar2 = 0;
  if (param_1 == 0) {
LAB_000550e8:
    uVar2 = 1;
  }
  else {
    if (param_1 != 1) {
      return 0;
    }
    if (sRam003f08c8 == 1) {
      sVar1 = func_0x003de140(0x71,0x566,1,1);
    }
    else {
      if (sRam003f08c8 < 2) {
        if (sRam003f08c8 != 0) goto LAB_00055080;
        uVar4 = 0x565;
      }
      else {
        if (sRam003f08c8 != 2) {
LAB_00055080:
          func_0x003d0848(0x7c);
          func_0x003dd390(0x7c);
          lVar3 = func_0x003dd8a8(0x70);
          if (lVar3 != 0) {
            return 1;
          }
          lVar3 = func_0x003dd8a8(0x74);
          if (lVar3 != 0) {
            return 1;
          }
          func_0x003dd8d8(0x98);
          func_0x003dd8d8(0x9a);
          func_0x003dd8d8(0x9b);
          func_0x003dd8d8(0x9c);
          func_0x003dd8d8(0xf0);
          func_0x003dd8d8(0xf1);
          func_0x003dd8d8(0xf2);
          goto LAB_000550e8;
        }
        uVar4 = 0x567;
      }
      sVar1 = func_0x003ddb60(uVar4);
    }
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  return uVar2;
}



undefined8 FUN_00055108(short param_1)

{
  long lVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    lVar1 = func_0x003dd8a8(0x97);
    if (lVar1 != 1) {
      return 0;
    }
    lVar1 = func_0x003dd8a8(0x9b);
    if ((lVar1 != 1) && (lVar1 = func_0x003dd8a8(0x9e), lVar1 != 1)) {
      return 0;
    }
  }
  else {
    if (param_1 != 1) {
      return 0;
    }
    if (uRam003f08c8 < 0xb) {
                    // WARNING: Could not emulate address calculation at 0x000551a0
                    // WARNING: Treating indirect jump as call
      uVar2 = (**(code **)((short)uRam003f08c8 * 4 + 0x3fecd0))();
      return uVar2;
    }
    func_0x003dd798();
  }
  return 1;
}



undefined4 FUN_00055370(short param_1)

{
  short sVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    return 1;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    lVar2 = func_0x003dd250(2);
    if (lVar2 != 1) {
LAB_00055410:
      sRam003f08c8 = sRam003f08c8 + 1;
      return 0;
    }
    uVar3 = 0x7f0;
  }
  else {
    if (sRam003f08c8 < 2) {
      uVar3 = 0x7ef;
      if (sRam003f08c8 != 0) {
        return 1;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        lVar2 = func_0x003dd250(0);
        if (lVar2 != 1) goto LAB_00055410;
        uVar3 = 0x7f1;
      }
      else {
        uVar3 = 0x7f2;
        if (sRam003f08c8 != 3) {
          return 1;
        }
      }
    }
  }
  sVar1 = func_0x003ddb60(uVar3);
  sRam003f08c8 = sRam003f08c8 + sVar1;
  return 0;
}



bool FUN_00055458(short param_1)

{
  bool bVar1;
  short sVar2;
  int iVar3;
  
  bVar1 = false;
  if (param_1 == 0) {
    sVar2 = 8;
    iVar3 = 0x10;
    do {
      if ((*(short *)(iVar3 + 0xc00130) == 2) && (*(short *)(iVar3 + 0xc04954) != 0)) {
        bVar1 = true;
        func_0x003d0848(sVar2);
        func_0x003dd390(sVar2);
      }
      sVar2 = sVar2 + 1;
      iVar3 = iVar3 + 2;
    } while (sVar2 < 0x2b);
  }
  else {
    bVar1 = false;
    if (param_1 == 1) {
      bVar1 = sRam003f08c8 == 0;
      if (bVar1) {
        sVar2 = func_0x003ddb60(0x8cb);
        sRam003f08c8 = sRam003f08c8 + sVar2;
      }
      bVar1 = !bVar1;
    }
  }
  return bVar1;
}



undefined4 FUN_00055548(short param_1)

{
  short sVar1;
  short *psVar2;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (sRam003f08c8 == 0) {
      sVar1 = func_0x003ddb60(0x961);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      return 0;
    }
    for (psVar2 = (short *)0x3f0430; *psVar2 != -1; psVar2 = psVar2 + 1) {
      func_0x003d0848();
      *(undefined2 *)(*psVar2 * 2 + 0xc00130) = 1;
      *(short *)(*psVar2 * 2 + 0xc00286) = *psVar2;
    }
  }
  return 1;
}



undefined4 FUN_00055610(short param_1)

{
  short sVar1;
  short *psVar2;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (sRam003f08c8 == 0) {
      sVar1 = func_0x003ddb60(0x97f);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      return 0;
    }
    func_0x003d0848(0);
    func_0x003dd390(0);
    func_0x00397738(1,0,10);
    uRam00c00934 = 2;
    sRam00c00a8a = sRam003e825c * 10;
    for (psVar2 = (short *)0x3f0440; *psVar2 != -1; psVar2 = psVar2 + 1) {
      func_0x003d0848();
      *(undefined2 *)(*psVar2 * 2 + 0xc00130) = 1;
      *(short *)(*psVar2 * 2 + 0xc00286) = *psVar2;
    }
  }
  return 1;
}



undefined4 FUN_00055720(short param_1)

{
  short sVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    uVar3 = 1;
  }
  else {
    uVar3 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        uVar3 = 0xddf;
      }
      else {
        if (sRam003f08c8 != 1) {
          return 1;
        }
        lVar2 = func_0x003dd200(3);
        if (lVar2 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar3 = 0xde0;
      }
      sVar1 = func_0x003ddb60(uVar3);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar3 = 0;
    }
  }
  return uVar3;
}



undefined8 FUN_000557d0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd250(0x27);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    sVar1 = func_0x003de140(0x1a,0xe1c,1,1);
LAB_00055880:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0xe1b;
        goto LAB_00055874;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0xe1d;
LAB_00055874:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_00055880;
      }
    }
    uVar3 = 1;
    func_0x003dd3e0(0x27);
  }
  return uVar3;
}



undefined4 FUN_000558b0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xfbf);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(0x6a);
        func_0x003dd390(0x6a);
        uRam00c01cbc = 100;
        uRam00c00f60 = 3;
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00055958(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  undefined8 uVar4;
  undefined8 uVar5;
  undefined8 uVar6;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        uVar3 = 2;
        uVar4 = 2;
        uVar5 = 1;
        uVar6 = 0;
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar3 = 0xc;
          if (sRam003f08c8 != 0) {
            return 1;
          }
          uVar4 = 1;
          uVar5 = 0;
          uVar6 = 0;
        }
        else {
          if (sRam003f08c8 == 2) {
            uVar3 = 0xe;
            uVar4 = 3;
            uVar5 = 1;
            uVar6 = 0;
          }
          else {
            uVar3 = 0xf;
            if (sRam003f08c8 != 3) {
              return 1;
            }
            uVar4 = 4;
            uVar5 = 1;
            uVar6 = 1;
          }
        }
      }
      sVar1 = func_0x003de140(uVar3,uVar4,uVar5,uVar6);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_00055a50(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        sVar1 = func_0x003de140(0x20,0x7a,1,1);
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar2 = 0x79;
          if (sRam003f08c8 != 0) {
            return 1;
          }
        }
        else {
          uVar2 = 0x7b;
          if (sRam003f08c8 != 2) {
            return 1;
          }
        }
        sVar1 = func_0x003ddb60(uVar2);
      }
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined8 FUN_00055b10(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd228(2);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    func_0x003de3c0(3);
    sVar1 = func_0x003de140(0x1f,0xde,1,1);
LAB_00055bc8:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0xdd;
        goto LAB_00055bbc;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0xf3;
LAB_00055bbc:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_00055bc8;
      }
    }
    uVar3 = 1;
    func_0x003dd390(2);
    func_0x003dd3e0(0x10);
    func_0x003dd3e0(0x11);
    func_0x003dd3e0(0x12);
    func_0x003dd3e0(0x13);
  }
  return uVar3;
}



undefined8 FUN_00055c18(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd228(0);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    func_0x003de3c0(3);
    sVar1 = func_0x003de140(4,0x142,1,1);
LAB_00055cd0:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0x141;
        goto LAB_00055cc4;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0x143;
LAB_00055cc4:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_00055cd0;
      }
    }
    uVar3 = 1;
    func_0x003dd390(0);
    func_0x003dd3e0(8);
    func_0x003dd3e0(9);
    func_0x003dd3e0(10);
    func_0x003dd3e0(0xb);
  }
  return uVar3;
}



undefined8 FUN_00055d20(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd228(4);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    func_0x003de3c0(3);
    sVar1 = func_0x003de140(0x21,0x1a6,1,1);
LAB_00055dd8:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0x1a5;
        goto LAB_00055dcc;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0x1a7;
LAB_00055dcc:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_00055dd8;
      }
    }
    uVar3 = 1;
    func_0x003dd390(4);
    func_0x003dd3e0(0x18);
    func_0x003dd3e0(0x19);
    func_0x003dd3e0(0x1a);
    func_0x003dd3e0(0x1b);
  }
  return uVar3;
}



undefined8 FUN_00055e28(short param_1)

{
  short sVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    uVar3 = func_0x003dd228(5);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    func_0x003de3c0(3);
    sVar1 = func_0x003de140(0x22,0x20a,1,1);
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 != 0) {
LAB_00055f28:
        func_0x003dd390(5);
        func_0x003dd3e0(0x1c);
        func_0x003dd3e0(0x1d);
        func_0x003dd3e0(0x1e);
        func_0x003dd3e0(0x1f);
        return 1;
      }
      uVar3 = 0x209;
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar3 = 0x20b;
      }
      else {
        if (sRam003f08c8 != 3) goto LAB_00055f28;
        lVar2 = func_0x003dd8a8(0x48);
        if (lVar2 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar3 = 0x20c;
      }
    }
    sVar1 = func_0x003ddb60(uVar3);
  }
  sRam003f08c8 = sRam003f08c8 + sVar1;
  return 0;
}



undefined8 FUN_00055f70(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(6);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 8) {
                    // WARNING: Could not emulate address calculation at 0x00055fd0
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3ff110))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(6);
      func_0x003dd3e0(0x20);
      func_0x003dd3e0(0x21);
      func_0x003dd3e0(0x22);
      func_0x003dd3e0(0x23);
    }
  }
  return uVar1;
}



undefined8 FUN_00056118(short param_1)

{
  short sVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    uVar3 = func_0x003dd228(7);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    func_0x003de3c0(3);
    sVar1 = func_0x003de140(0x24,0x2d2,1,1);
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 != 0) {
LAB_00056218:
        func_0x003dd390(7);
        uRam00c01ea2 = uRam00c01ea2 & 0xff00 | 0x8a;
        uRam00c04560 = 0xb2;
        func_0x003dd3e0(0x24);
        func_0x003dd3e0(0x25);
        func_0x003dd3e0(0x26);
        func_0x003dd3e0(0x27);
        return 1;
      }
      uVar3 = 0x2d1;
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar3 = 0x2d3;
      }
      else {
        if (sRam003f08c8 != 3) goto LAB_00056218;
        lVar2 = func_0x003dd8a8(0x34);
        if (lVar2 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar3 = 0x2d4;
      }
    }
    sVar1 = func_0x003ddb60(uVar3);
  }
  sRam003f08c8 = sRam003f08c8 + sVar1;
  return 0;
}



undefined8 FUN_00056280(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(0x2a);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x3df);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003dd390(0x2a);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00056318(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x407);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00056390(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x4bb);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00056408(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (uRam003f08c8 < 8) {
                    // WARNING: Could not emulate address calculation at 0x00056460
                    // WARNING: Treating indirect jump as call
      uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3ff130))();
      return uVar1;
    }
    func_0x003d0848(0x7c);
    func_0x003dd390(0x7c);
  }
  return 1;
}



undefined8 FUN_00056578(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (uRam003f08c8 < 0xc) {
                    // WARNING: Could not emulate address calculation at 0x000565bc
                    // WARNING: Treating indirect jump as call
      uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3ff150))();
      return uVar1;
    }
    func_0x003dd798();
  }
  return 1;
}



undefined4 FUN_00056738(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  long lVar3;
  undefined8 uVar4;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        lVar3 = func_0x003dd250(2);
        if (lVar3 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar4 = 0x7f0;
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar4 = 0x7ef;
          if (sRam003f08c8 != 0) {
            return 1;
          }
        }
        else {
          uVar4 = 0x7f1;
          if (sRam003f08c8 != 2) {
            return 1;
          }
        }
      }
      sVar1 = func_0x003ddb60(uVar4);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



bool FUN_00056800(short param_1)

{
  bool bVar1;
  short sVar2;
  int iVar3;
  
  bVar1 = false;
  if (param_1 == 0) {
    sVar2 = 8;
    iVar3 = 0x10;
    do {
      if ((*(short *)(iVar3 + 0xc00130) == 2) && (*(short *)(iVar3 + 0xc04954) != 0)) {
        bVar1 = true;
        func_0x003d0848(sVar2);
        func_0x003dd390(sVar2);
      }
      sVar2 = sVar2 + 1;
      iVar3 = iVar3 + 2;
    } while (sVar2 < 0x2b);
  }
  else {
    bVar1 = false;
    if (param_1 == 1) {
      bVar1 = sRam003f08c8 == 0;
      if (bVar1) {
        sVar2 = func_0x003ddb60(0x8cb);
        sRam003f08c8 = sRam003f08c8 + sVar2;
      }
      bVar1 = !bVar1;
    }
  }
  return bVar1;
}



bool FUN_000568f0(short param_1)

{
  ushort uVar1;
  bool bVar2;
  short sVar3;
  int iVar4;
  int iVar5;
  undefined8 uVar6;
  ushort uVar7;
  short *psVar8;
  ushort *puVar9;
  
  sVar3 = sRam00c082e8;
  if (param_1 == 0) {
    sVar3 = 0;
    iVar5 = 0;
    puVar9 = (ushort *)0xc007de;
    uVar1 = uRam00c007de;
    while( true ) {
      puVar9 = puVar9 + 1;
      uVar7 = uVar1 & 0x7fff;
      iVar4 = (int)((uint)uVar1 << 0x10) >> 0xf;
      if ((((uVar1 & 0x8000) != 0) ||
          (uVar7 = *(ushort *)(iVar4 + 0xc05002), uVar7 == *(ushort *)(iVar4 + 0xc05158))) &&
         (uVar7 == 0xb)) {
        sVar3 = sVar3 + 1;
      }
      iVar5 = (iVar5 + 1) * 0x10000 >> 0x10;
      if (7 < iVar5) break;
      uVar1 = *puVar9;
    }
    bVar2 = sVar3 == 8;
  }
  else {
    bVar2 = false;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        uVar6 = 0x8fd;
        if (sRam00c01d44 != 0x17) {
          uVar6 = 0x8fe;
        }
        sVar3 = func_0x003ddb60(uVar6);
        sRam003f08c8 = sRam003f08c8 + sVar3;
        bVar2 = false;
      }
      else {
        bVar2 = true;
        if (sRam00c01d44 != 0x17) {
          if (sRam00c082e8 == 0x17) {
            sRam00c082e8 = sRam00c01d44;
            sRam00c01d44 = sVar3;
          }
          else {
            iVar5 = 1;
            do {
              iVar5 = (int)(short)iVar5;
              psVar8 = (short *)(iVar5 * 2 + 0xc082e8);
              if (0xfd < iVar5) {
                return true;
              }
              sVar3 = *psVar8;
              iVar5 = iVar5 + 1;
            } while (sVar3 != 0x17);
            *psVar8 = sRam00c01d44;
            bVar2 = true;
            sRam00c01d44 = sVar3;
          }
        }
      }
    }
  }
  return bVar2;
}



undefined4 FUN_00056aa0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xd5d);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(0x3e);
        func_0x003dd390(0x3e);
        func_0x003dd438(0x3e,9,0);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00056b40(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xd85);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(0x3e);
        func_0x003dd390(0x3e);
        func_0x003dd438(0x3e,9,0);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00056be0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xddf);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00056c58(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd250(0x27);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    sVar1 = func_0x003de140(0x1a,0xe1c,1,1);
LAB_00056d08:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0xe1b;
        goto LAB_00056cfc;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0xe1d;
LAB_00056cfc:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_00056d08;
      }
    }
    uVar3 = 1;
    func_0x003dd3e0(0x27);
  }
  return uVar3;
}



undefined4 FUN_00056d38(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xfbf);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00056db0(short param_1)

{
  short sVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    lVar2 = func_0x003dd8a8(0x194);
    if ((lVar2 != 1) && (lVar2 = func_0x003dd8a8(0x195), lVar2 != 1)) {
      return 0;
    }
    lVar2 = func_0x003dd8a8(0x9a);
    uVar3 = 0;
    if (lVar2 == 1) {
      uVar3 = 1;
    }
  }
  else {
    uVar3 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xfdd);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar3 = 0;
      }
      else {
        uVar3 = 1;
      }
    }
  }
  return uVar3;
}



undefined4 FUN_00056e60(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x100f);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(0x6a);
        func_0x003dd390(0x6a);
        uRam00c01cbc = 100;
        uRam00c00f60 = 3;
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00056f08(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  undefined8 uVar4;
  undefined8 uVar5;
  undefined8 uVar6;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        uVar3 = 2;
        uVar4 = 2;
        uVar5 = 1;
        uVar6 = 0;
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar3 = 0x10;
          if (sRam003f08c8 != 0) {
            return 1;
          }
          uVar4 = 1;
          uVar5 = 0;
          uVar6 = 0;
        }
        else {
          if (sRam003f08c8 == 2) {
            uVar3 = 0x11;
            uVar4 = 3;
            uVar5 = 1;
            uVar6 = 0;
          }
          else {
            uVar3 = 0x12;
            if (sRam003f08c8 != 3) {
              return 1;
            }
            uVar4 = 4;
            uVar5 = 1;
            uVar6 = 1;
          }
        }
      }
      sVar1 = func_0x003de140(uVar3,uVar4,uVar5,uVar6);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined8 FUN_00057000(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(2);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        func_0x003de3c0(3);
        sVar1 = func_0x003de140(0x1f,0xde,1,1);
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar3 = 0xdd;
          if (sRam003f08c8 != 0) {
            return 1;
          }
        }
        else {
          uVar3 = 0xdf;
          if (sRam003f08c8 != 2) {
            return 1;
          }
        }
        sVar1 = func_0x003ddb60(uVar3);
      }
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_000570d8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xf1);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00057150(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(3);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        func_0x003de3c0(3);
        sVar1 = func_0x003de140(0x20,0x142,1,1);
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar3 = 0x141;
          if (sRam003f08c8 != 0) {
            return 1;
          }
        }
        else {
          uVar3 = 0x143;
          if (sRam003f08c8 != 2) {
            return 1;
          }
        }
        sVar1 = func_0x003ddb60(uVar3);
      }
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_00057228(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x155);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_000572a0(short param_1)

{
  short sVar1;
  short *psVar2;
  int iVar3;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    iVar3 = 0;
    if (sRam003f08c8 == 0) {
      sVar1 = func_0x003ddb60(0x1af);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      return 0;
    }
    for (; iVar3 < 0xab; iVar3 = (int)(short)((short)iVar3 + 1)) {
      psVar2 = (short *)(iVar3 * 2 + 0xc00286);
      if (*psVar2 == 0) {
        *psVar2 = 4;
      }
    }
    iVar3 = 0;
    psVar2 = (short *)0xc068b8;
    do {
      if (*psVar2 == 0) {
        *psVar2 = 4;
      }
      iVar3 = (iVar3 + 1) * 0x10000 >> 0x10;
      psVar2 = psVar2 + 1;
    } while (iVar3 < 0x80);
    iVar3 = 0;
    do {
      func_0x003dd3e0(iVar3);
      iVar3 = (iVar3 + 1) * 0x10000 >> 0x10;
    } while (iVar3 < 0xc);
    iVar3 = 0x10;
    do {
      func_0x003dd3e0(iVar3);
      iVar3 = (iVar3 + 1) * 0x10000 >> 0x10;
    } while (iVar3 < 0x28);
  }
  return 1;
}



undefined8 FUN_000573d0(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(5);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        func_0x003de3c0(3);
        sVar1 = func_0x003de140(0x22,0x20a,1,1);
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar3 = 0x209;
          if (sRam003f08c8 != 0) {
            return 1;
          }
        }
        else {
          uVar3 = 0x20b;
          if (sRam003f08c8 != 2) {
            return 1;
          }
        }
        sVar1 = func_0x003ddb60(uVar3);
      }
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_000574a8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x21d);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00057520(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(6);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        sVar1 = func_0x003de140(0x77,0x26e,1,1);
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar3 = 0x26d;
          if (sRam003f08c8 != 0) {
            return 1;
          }
        }
        else {
          uVar3 = 0x26f;
          if (sRam003f08c8 != 2) {
            return 1;
          }
        }
        sVar1 = func_0x003ddb60(uVar3);
      }
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_000575f0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x281);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00057668(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(7);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 5) {
                    // WARNING: Could not emulate address calculation at 0x000576c8
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3ff540))();
        return uVar1;
      }
      uVar1 = 1;
      uRam00c01ea2 = uRam00c01ea2 & 0xff00 | 0x8a;
      uRam00c04560 = 0xb2;
    }
  }
  return uVar1;
}



undefined4 FUN_000577a0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x2e5);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00057818(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(0x2a);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x3df);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003dd390(0x2a);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_000578b0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x407);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00057928(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x4bb);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_000579a0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x565);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(0x7c);
        func_0x003dd390(0x7c);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00057a30(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (uRam003f08c8 < 0xc) {
                    // WARNING: Could not emulate address calculation at 0x00057a74
                    // WARNING: Treating indirect jump as call
      uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3ff560))();
      return uVar1;
    }
    func_0x003dd798();
  }
  return 1;
}



undefined4 FUN_00057bf0(short param_1)

{
  short sVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    return 1;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    lVar2 = func_0x003dd250(2);
    if (lVar2 != 1) {
LAB_00057c90:
      sRam003f08c8 = sRam003f08c8 + 1;
      return 0;
    }
    uVar3 = 0x7f0;
  }
  else {
    if (sRam003f08c8 < 2) {
      uVar3 = 0x7ef;
      if (sRam003f08c8 != 0) {
        return 1;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        lVar2 = func_0x003dd250(5);
        if (lVar2 != 1) goto LAB_00057c90;
        uVar3 = 0x7f1;
      }
      else {
        uVar3 = 0x7f2;
        if (sRam003f08c8 != 3) {
          return 1;
        }
      }
    }
  }
  sVar1 = func_0x003ddb60(uVar3);
  sRam003f08c8 = sRam003f08c8 + sVar1;
  return 0;
}



bool FUN_00057cd8(short param_1)

{
  bool bVar1;
  short sVar2;
  int iVar3;
  
  bVar1 = false;
  if (param_1 == 0) {
    sVar2 = 8;
    iVar3 = 0x10;
    do {
      if ((*(short *)(iVar3 + 0xc00130) == 2) && (*(short *)(iVar3 + 0xc04954) != 0)) {
        bVar1 = true;
        func_0x003d0848(sVar2);
        func_0x003dd390(sVar2);
      }
      sVar2 = sVar2 + 1;
      iVar3 = iVar3 + 2;
    } while (sVar2 < 0x2b);
  }
  else {
    bVar1 = false;
    if (param_1 == 1) {
      bVar1 = sRam003f08c8 == 0;
      if (bVar1) {
        sVar2 = func_0x003ddb60(0x8cb);
        sRam003f08c8 = sRam003f08c8 + sVar2;
      }
      bVar1 = !bVar1;
    }
  }
  return bVar1;
}



undefined4 FUN_00057dc8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xd5d);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(0x3e);
        func_0x003dd390(0x3e);
        func_0x003dd438(0x3e,9,0);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00057e68(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xd85);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(0x3e);
        func_0x003dd390(0x3e);
        func_0x003dd438(0x3e,9,0);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00057f08(short param_1)

{
  short sVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    uVar3 = 1;
  }
  else {
    uVar3 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        uVar3 = 0xddf;
      }
      else {
        if (sRam003f08c8 != 1) {
          return 1;
        }
        lVar2 = func_0x003dd200(3);
        if (lVar2 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar3 = 0xde0;
      }
      sVar1 = func_0x003ddb60(uVar3);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar3 = 0;
    }
  }
  return uVar3;
}



undefined4 FUN_00057fb8(short param_1)

{
  short sVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    lVar2 = func_0x003dd8a8(0x17);
    if (lVar2 == 1) {
      return 1;
    }
    lVar2 = func_0x003dd8a8(0x21);
    if (lVar2 == 1) {
      return 1;
    }
    lVar2 = func_0x003dd8a8(0x35);
    if (lVar2 == 1) {
      return 1;
    }
    lVar2 = func_0x003dd8a8(0x3f);
    if (lVar2 == 1) {
      return 1;
    }
    lVar2 = func_0x003dd8a8(0x49);
    if (lVar2 == 1) {
      return 1;
    }
    lVar2 = func_0x003dd8a8(0x19);
    if (lVar2 == 1) {
      return 1;
    }
    lVar2 = func_0x003dd8a8(0x23);
    if (lVar2 == 1) {
      return 1;
    }
    lVar2 = func_0x003dd8a8(0x37);
    if (lVar2 != 1) {
      lVar2 = func_0x003dd8a8(0x41);
      if (lVar2 == 1) {
        return 1;
      }
      lVar2 = func_0x003dd8a8(0x4b);
      if (lVar2 != 1) {
        return 0;
      }
      return 1;
    }
    return 1;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    sVar1 = func_0x003de140(0x2e,0xeda,1,0);
  }
  else {
    if (sRam003f08c8 < 2) {
      uVar3 = 0xed9;
      if (sRam003f08c8 != 0) {
        return 1;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        sVar1 = func_0x003de140(0x2f,0xedb,1,1);
        goto LAB_0005812c;
      }
      uVar3 = 0xedc;
      if (sRam003f08c8 != 3) {
        return 1;
      }
    }
    sVar1 = func_0x003ddb60(uVar3);
  }
LAB_0005812c:
  sRam003f08c8 = sRam003f08c8 + sVar1;
  return 0;
}



undefined4 FUN_00058150(short param_1)

{
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    func_0x003d0848(0x58);
    uRam00c001e0 = 1;
    func_0x003d0848(0x5b);
    uRam00c001e6 = 1;
  }
  return 1;
}



undefined4 FUN_000581b0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        sVar1 = func_0x003de140(0x30,0xf02,1,1);
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar2 = 0xf01;
          if (sRam003f08c8 != 0) {
            return 1;
          }
        }
        else {
          uVar2 = 0xf03;
          if (sRam003f08c8 != 2) {
            return 1;
          }
        }
        sVar1 = func_0x003ddb60(uVar2);
      }
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_00058270(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xfbf);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_000582e8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xfdd);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00058360(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  undefined8 uVar4;
  undefined8 uVar5;
  undefined8 uVar6;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        uVar3 = 0x14;
        uVar4 = 2;
        uVar5 = 1;
        uVar6 = 0;
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar3 = 0x13;
          if (sRam003f08c8 != 0) {
            return 1;
          }
          uVar4 = 1;
          uVar5 = 0;
          uVar6 = 0;
        }
        else {
          if (sRam003f08c8 == 2) {
            uVar3 = 2;
            uVar4 = 3;
            uVar5 = 1;
            uVar6 = 0;
          }
          else {
            uVar3 = 0x15;
            if (sRam003f08c8 != 3) {
              return 1;
            }
            uVar4 = 4;
            uVar5 = 1;
            uVar6 = 1;
          }
        }
      }
      sVar1 = func_0x003de140(uVar3,uVar4,uVar5,uVar6);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_00058458(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x6f);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_000584d0(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(0x50);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 8) {
                    // WARNING: Could not emulate address calculation at 0x00058538
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3ff9e0))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(0x50);
      func_0x003dd3e0(0x51);
      func_0x003dd3e0(0x52);
      func_0x003dd3e0(0x53);
    }
  }
  return uVar1;
}



undefined8 FUN_000586a0(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(2);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 5) {
                    // WARNING: Could not emulate address calculation at 0x00058708
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3ffa00))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(2);
      func_0x003dd3e0(0x10);
      func_0x003dd3e0(0x11);
      func_0x003dd3e0(0x12);
      func_0x003dd3e0(0x13);
    }
  }
  return uVar1;
}



undefined8 FUN_000588e8(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(3);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 6) {
                    // WARNING: Could not emulate address calculation at 0x00058948
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3ffa40))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(3);
      func_0x003dd3e0(0x14);
      func_0x003dd3e0(0x15);
      func_0x003dd3e0(0x16);
      func_0x003dd3e0(0x17);
    }
  }
  return uVar1;
}



undefined8 FUN_00058a58(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd228(4);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    func_0x003de3c0(3);
    sVar1 = func_0x003de140(0x21,0x1a6,1,1);
LAB_00058b10:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0x1a5;
        goto LAB_00058b04;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0x1a7;
LAB_00058b04:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_00058b10;
      }
    }
    uVar3 = 1;
    func_0x003dd390(4);
    func_0x003dd3e0(0x18);
    func_0x003dd3e0(0x19);
    func_0x003dd3e0(0x1a);
    func_0x003dd3e0(0x1b);
  }
  return uVar3;
}



undefined8 FUN_00058b60(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd228(0);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    func_0x003de3c0(3);
    sVar1 = func_0x003de140(4,0x214,1,1);
LAB_00058c18:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0x213;
        goto LAB_00058c0c;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0x215;
LAB_00058c0c:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_00058c18;
      }
    }
    uVar3 = 1;
    func_0x003dd390(0);
  }
  return uVar3;
}



undefined8 FUN_00058c48(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(6);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 10) {
                    // WARNING: Could not emulate address calculation at 0x00058cb0
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3ffa60))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(6);
      func_0x003dd3e0(0x20);
      func_0x003dd3e0(0x21);
      func_0x003dd3e0(0x22);
      func_0x003dd3e0(0x23);
    }
  }
  return uVar1;
}



undefined4 FUN_00058f20(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        func_0x003de3c0(3);
        sVar1 = func_0x003de140(0x24,0x304,1,1);
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar3 = 0x303;
          if (sRam003f08c8 != 0) {
            return 1;
          }
        }
        else {
          if (sRam003f08c8 == 2) {
            uVar3 = 0x305;
          }
          else {
            uVar3 = 0x306;
            if (sRam003f08c8 != 3) {
              return 1;
            }
          }
        }
        sVar1 = func_0x003ddb60(uVar3);
      }
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined8 FUN_00058ff8(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(0x2a);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x3df);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003dd390(0x2a);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00059090(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x407);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00059108(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x4bb);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00059180(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (uRam003f08c8 < 8) {
                    // WARNING: Could not emulate address calculation at 0x000591d8
                    // WARNING: Treating indirect jump as call
      uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3ffab0))();
      return uVar1;
    }
    func_0x003d0848(0x7c);
    func_0x003dd390(0x7c);
  }
  return 1;
}



undefined8 FUN_000592f0(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228();
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x5bf);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003dd390();
        func_0x003dd390();
        func_0x003dd3e0();
        func_0x003dd3e0();
        func_0x003dd3e0();
        func_0x003dd3e0();
        func_0x003dd3e0();
        func_0x003dd3e0();
        uRam00c01ea2 = uRam00c01ea2 & 0xff00 | 0x8a;
        uRam00c04560 = 0xb2;
        uRam00c0013e = 6;
        uRam00c03558 = 2;
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00059400(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (uRam003f08c8 < 0xc) {
                    // WARNING: Could not emulate address calculation at 0x0005944c
                    // WARNING: Treating indirect jump as call
      uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3ffad0))();
      return uVar1;
    }
    func_0x003dd798();
  }
  return 1;
}



undefined4 FUN_00059648(short param_1)

{
  short sVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    return 1;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    lVar2 = func_0x003dd250(2);
    if (lVar2 != 1) {
LAB_000596e8:
      sRam003f08c8 = sRam003f08c8 + 1;
      return 0;
    }
    uVar3 = 0x7f0;
  }
  else {
    if (sRam003f08c8 < 2) {
      uVar3 = 0x7ef;
      if (sRam003f08c8 != 0) {
        return 1;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        lVar2 = func_0x003dd250(0);
        if (lVar2 != 1) goto LAB_000596e8;
        uVar3 = 0x7f1;
      }
      else {
        uVar3 = 0x7f2;
        if (sRam003f08c8 != 3) {
          return 1;
        }
      }
    }
  }
  sVar1 = func_0x003ddb60(uVar3);
  sRam003f08c8 = sRam003f08c8 + sVar1;
  return 0;
}



bool FUN_00059730(short param_1)

{
  bool bVar1;
  short sVar2;
  int iVar3;
  
  bVar1 = false;
  if (param_1 == 0) {
    sVar2 = 8;
    iVar3 = 0x10;
    do {
      if ((*(short *)(iVar3 + 0xc00130) == 2) && (*(short *)(iVar3 + 0xc04954) != 0)) {
        bVar1 = true;
        func_0x003d0848(sVar2);
        func_0x003dd390(sVar2);
      }
      sVar2 = sVar2 + 1;
      iVar3 = iVar3 + 2;
    } while (sVar2 < 0x2b);
  }
  else {
    bVar1 = false;
    if (param_1 == 1) {
      bVar1 = sRam003f08c8 == 0;
      if (bVar1) {
        sVar2 = func_0x003ddb60(0x8cb);
        sRam003f08c8 = sRam003f08c8 + sVar2;
      }
      bVar1 = !bVar1;
    }
  }
  return bVar1;
}



undefined4 FUN_00059820(short param_1)

{
  short sVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    uVar3 = 1;
  }
  else {
    uVar3 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        uVar3 = 0xddf;
      }
      else {
        if (sRam003f08c8 != 1) {
          return 1;
        }
        lVar2 = func_0x003dd200(3);
        if (lVar2 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar3 = 0xde0;
      }
      sVar1 = func_0x003ddb60(uVar3);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar3 = 0;
    }
  }
  return uVar3;
}



undefined8 FUN_000598d0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd250(0x27);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    sVar1 = func_0x003de140(0x1a,0xe1c,1,1);
LAB_00059980:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0xe1b;
        goto LAB_00059974;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0xe1d;
LAB_00059974:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_00059980;
      }
    }
    uVar3 = 1;
    func_0x003dd3e0(0x27);
  }
  return uVar3;
}



undefined4 FUN_000599b0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  long lVar3;
  undefined8 uVar4;
  
  uVar2 = 0;
  if (param_1 == 0) {
    return 1;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    lVar3 = func_0x003dd8a8(0x191);
    if (lVar3 == 0) {
      uVar4 = 0xfca;
    }
    else {
      lVar3 = func_0x003dd8a8(0x192);
      if (lVar3 != 0) {
        sRam003f08c8 = sRam003f08c8 + 1;
        return 0;
      }
      uVar4 = 0xfca;
    }
LAB_00059a64:
    sVar1 = func_0x003ddb60(uVar4);
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar4 = 0xfc9;
        goto LAB_00059a64;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar4 = 0xfcb;
        goto LAB_00059a64;
      }
    }
    uVar2 = 1;
    func_0x003d0848(0x6a);
    func_0x003dd390(0x6a);
    uRam00c01cbc = 100;
    uRam00c00f60 = 3;
    func_0x003dd8d8(0x191);
    func_0x003dd8d8(0x192);
  }
  return uVar2;
}



undefined4 FUN_00059ad0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  undefined8 uVar4;
  undefined8 uVar5;
  undefined8 uVar6;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        uVar3 = 2;
        uVar4 = 2;
        uVar5 = 1;
        uVar6 = 0;
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar3 = 0x16;
          if (sRam003f08c8 != 0) {
            return 1;
          }
          uVar4 = 1;
          uVar5 = 0;
          uVar6 = 0;
        }
        else {
          if (sRam003f08c8 == 2) {
            uVar3 = 0x17;
            uVar4 = 3;
            uVar5 = 1;
            uVar6 = 0;
          }
          else {
            uVar3 = 0x18;
            if (sRam003f08c8 != 3) {
              return 1;
            }
            uVar4 = 4;
            uVar5 = 1;
            uVar6 = 1;
          }
        }
      }
      sVar1 = func_0x003de140(uVar3,uVar4,uVar5,uVar6);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_00059bc8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x6f);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00059c40(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(0x43);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 5) {
                    // WARNING: Could not emulate address calculation at 0x00059ca0
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3ffe40))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(0x43);
      func_0x003dd3e0(0x44);
      func_0x003dd3e0(0x4a);
      func_0x003dd3e0(0x4b);
    }
  }
  return uVar1;
}



undefined4 FUN_00059da0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  long lVar3;
  undefined8 uVar4;
  
  uVar2 = 0;
  if (param_1 == 0) {
    return 1;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    lVar3 = func_0x003d05d8(100,2);
    if (lVar3 != 1) {
      sRam003f08c8 = sRam003f08c8 + 1;
      return 0;
    }
    uVar4 = 0xfc;
LAB_00059e54:
    sVar1 = func_0x003ddb60(uVar4);
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar4 = 0xfb;
        goto LAB_00059e54;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar4 = 0xfd;
        goto LAB_00059e54;
      }
    }
    uVar2 = 1;
    func_0x003d0848(2);
    uRam00c00134 = 1;
  }
  return uVar2;
}



undefined4 FUN_00059ea0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x10f);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_00059f18(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(3);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x141);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(3);
        uRam00c00136 = 1;
        func_0x003d0848(0x14);
        func_0x003dd390(0x14);
        func_0x003dd3e0(0x15);
        func_0x003dd3e0(0x16);
        func_0x003dd3e0(0x17);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_00059fe8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x1d7);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(0x18);
        func_0x003dd390(0x18);
        func_0x003d0848(0x19);
        func_0x003dd390(0x19);
        func_0x003d0848(0x1a);
        func_0x003dd390(0x1a);
        func_0x003d0848(0x1b);
        func_0x003dd390(0x1b);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_0005a0a8(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  long lVar3;
  undefined4 uVar4;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(5);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        uVar4 = 0x231;
      }
      else {
        if (sRam003f08c8 != 1) {
          func_0x003d0848(5);
          uRam00c0013a = 1;
          return 1;
        }
        lVar3 = func_0x003dd8a8(0x1b);
        if (lVar3 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar4 = 0x232;
      }
      sVar1 = func_0x003ddb60(uVar4);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_0005a188(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x26d);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_0005a200(short param_1)

{
  short sVar1;
  long lVar2;
  short *psVar3;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (sRam003f08c8 == 0) {
      sVar1 = func_0x003ddb60(0x281);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      return 0;
    }
    psVar3 = (short *)0x3f0450;
    while (*psVar3 != -1) {
      lVar2 = func_0x003dd228(*psVar3);
      if (lVar2 == 1) {
        func_0x003d0848(*psVar3);
        func_0x003d0650(*psVar3,0);
        psVar3 = psVar3 + 1;
      }
      else {
        psVar3 = psVar3 + 1;
      }
    }
  }
  return 1;
}



undefined4 FUN_0005a2b8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x29f);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_0005a330(short param_1)

{
  short sVar1;
  short *psVar2;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (sRam003f08c8 == 0) {
      sVar1 = func_0x003ddb60(0x2b3);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      return 0;
    }
    for (psVar2 = (short *)0x3f0460; *psVar2 != -1; psVar2 = psVar2 + 1) {
      func_0x003d0848(*psVar2);
      func_0x003dd390();
      *(undefined2 *)(*psVar2 * 2 + 0xc00130) = 6;
      *(undefined2 *)(*psVar2 * 2 + 0xc0354a) = 2;
    }
  }
  return 1;
}



undefined8 FUN_0005a400(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(0x2a);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x3df);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003dd390(0x2a);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_0005a498(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x407);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_0005a510(short param_1)

{
  short sVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    lVar2 = func_0x003dd8a8(0x38);
    if (lVar2 == 1) {
      lVar2 = func_0x003dd8a8(0x19);
      if (lVar2 == 1) {
        uVar3 = 1;
      }
      else {
        lVar2 = func_0x003dd8a8(0x21);
        if (lVar2 == 1) {
          uVar3 = 1;
        }
        else {
          lVar2 = func_0x003dd8a8(0x2c);
          if (lVar2 == 1) {
            uVar3 = 1;
          }
          else {
            lVar2 = func_0x003dd8a8(0x40);
            if (lVar2 == 1) {
              uVar3 = 1;
            }
            else {
              lVar2 = func_0x003dd8a8(0x48);
              uVar3 = 0;
              if (lVar2 == 1) {
                uVar3 = 1;
              }
            }
          }
        }
      }
    }
    else {
      uVar3 = 0;
    }
  }
  else {
    uVar3 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x4bb);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar3 = 0;
      }
      else {
        uVar3 = 1;
      }
    }
  }
  return uVar3;
}



undefined4 FUN_0005a5f0(short param_1)

{
  short sVar1;
  short sVar2;
  undefined4 uVar3;
  short *psVar4;
  int iVar5;
  undefined4 uVar6;
  
  uVar6 = 0;
  if (param_1 == 0) {
LAB_0005a7d4:
    uVar6 = 1;
  }
  else {
    if (param_1 != 1) {
      return 0;
    }
    if (sRam003f08c8 == 1) {
      sVar2 = func_0x003de140(0x71,0x5d4,1,1);
    }
    else {
      if (sRam003f08c8 < 2) {
        if (sRam003f08c8 != 0) goto LAB_0005a698;
        uVar3 = 0x5d3;
      }
      else {
        if (sRam003f08c8 != 2) {
LAB_0005a698:
          func_0x003d0848(0x7c);
          func_0x003dd390(0x7c);
          psVar4 = (short *)0x3f0470;
          sVar1 = sRam003f0470;
          sVar2 = sRam003f0470;
          while (sVar2 != -1) {
            func_0x003d0848(sVar1);
            func_0x003dd390(*psVar4);
            *(undefined2 *)(*psVar4 * 2 + 0xc00130) = 6;
            sVar2 = *psVar4;
            psVar4 = psVar4 + 1;
            *(undefined2 *)(sVar2 * 2 + 0xc0354a) = 2;
            sVar1 = *psVar4;
            sVar2 = *psVar4;
          }
          iVar5 = 8;
          uRam00c01ea2 = uRam00c01ea2 & 0xff00 | 0x8a;
          uRam00c04560 = 0xb2;
          do {
            func_0x003dd3e0(iVar5);
            iVar5 = (iVar5 + 1) * 0x10000 >> 0x10;
          } while (iVar5 < 0x28);
          iVar5 = 0;
          psVar4 = (short *)0xc00286;
          do {
            if (*psVar4 == 1) {
              *psVar4 = 6;
            }
            iVar5 = (iVar5 + 1) * 0x10000 >> 0x10;
            psVar4 = psVar4 + 1;
          } while (iVar5 < 0xab);
          iVar5 = 0;
          psVar4 = (short *)0xc068b8;
          do {
            if (*psVar4 == 1) {
              *psVar4 = 6;
            }
            iVar5 = (iVar5 + 1) * 0x10000 >> 0x10;
            psVar4 = psVar4 + 1;
          } while (iVar5 < 0x80);
          goto LAB_0005a7d4;
        }
        uVar3 = 0x5d5;
      }
      sVar2 = func_0x003ddb60(uVar3);
    }
    sRam003f08c8 = sRam003f08c8 + sVar2;
  }
  return uVar6;
}



undefined8 FUN_0005a7f8(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (uRam003f08c8 < 5) {
                    // WARNING: Could not emulate address calculation at 0x0005a83c
                    // WARNING: Treating indirect jump as call
      uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x3ffe60))();
      return uVar1;
    }
    func_0x003dd798();
  }
  return 1;
}



undefined4 FUN_0005a8d8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  long lVar3;
  undefined8 uVar4;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        lVar3 = func_0x003dd250(2);
        if (lVar3 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar4 = 0x7f0;
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar4 = 0x7ef;
          if (sRam003f08c8 != 0) {
            return 1;
          }
        }
        else {
          uVar4 = 0x7f1;
          if (sRam003f08c8 != 2) {
            return 1;
          }
        }
      }
      sVar1 = func_0x003ddb60(uVar4);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



bool FUN_0005a9a0(short param_1)

{
  bool bVar1;
  short sVar2;
  int iVar3;
  
  bVar1 = false;
  if (param_1 == 0) {
    sVar2 = 8;
    iVar3 = 0x10;
    do {
      if ((*(short *)(iVar3 + 0xc00130) == 2) && (*(short *)(iVar3 + 0xc04954) != 0)) {
        bVar1 = true;
        func_0x003dd390(sVar2);
      }
      sVar2 = sVar2 + 1;
      iVar3 = iVar3 + 2;
    } while (sVar2 < 0x2b);
  }
  else {
    bVar1 = false;
    if (param_1 == 1) {
      bVar1 = sRam003f08c8 == 0;
      if (bVar1) {
        sVar2 = func_0x003ddb60(0x8cb);
        sRam003f08c8 = sRam003f08c8 + sVar2;
      }
      bVar1 = !bVar1;
    }
  }
  return bVar1;
}



undefined4 FUN_0005aa80(short param_1)

{
  short sVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    uVar3 = 1;
  }
  else {
    uVar3 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        uVar3 = 0xddf;
      }
      else {
        if (sRam003f08c8 != 1) {
          return 1;
        }
        lVar2 = func_0x003dd200(3);
        if (lVar2 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar3 = 0xde0;
      }
      sVar1 = func_0x003ddb60(uVar3);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar3 = 0;
    }
  }
  return uVar3;
}



undefined8 FUN_0005ab30(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd250(0x27);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    sVar1 = func_0x003de140(0x1a,0xe1c,1,1);
LAB_0005abe0:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0xe1b;
        goto LAB_0005abd4;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0xe1d;
LAB_0005abd4:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_0005abe0;
      }
    }
    uVar3 = 1;
    func_0x003dd3e0(0x27);
  }
  return uVar3;
}



undefined4 FUN_0005ac10(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xfbf);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_0005ac88(short param_1)

{
  short sVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    lVar2 = func_0x003dd8a8(0x194);
    if ((lVar2 != 1) && (lVar2 = func_0x003dd8a8(0x195), lVar2 != 1)) {
      return 0;
    }
    lVar2 = func_0x003dd8a8(0x9a);
    uVar3 = 0;
    if (lVar2 == 1) {
      uVar3 = 1;
    }
  }
  else {
    uVar3 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xfdd);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar3 = 0;
      }
      else {
        uVar3 = 1;
      }
    }
  }
  return uVar3;
}



undefined8 FUN_0005ad38(short param_1)

{
  short sVar1;
  long lVar2;
  undefined8 uVar3;
  undefined8 uVar4;
  
  uVar4 = 0;
  if (param_1 == 0) {
    uVar4 = func_0x003dd228(0x68);
    return uVar4;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    lVar2 = func_0x003dd228(0x69);
    if (lVar2 != 1) {
LAB_0005ae08:
      sRam003f08c8 = sRam003f08c8 + 1;
      return 0;
    }
    uVar3 = 0x101a;
LAB_0005adec:
    sVar1 = func_0x003ddb60(uVar3);
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar3 = 0x1019;
        goto LAB_0005adec;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        lVar2 = func_0x003dd228(0x6a);
        if (lVar2 != 1) goto LAB_0005ae08;
        uVar3 = 0x101b;
        goto LAB_0005adec;
      }
    }
    uVar4 = 1;
    func_0x003dd390(0x68);
    func_0x003dd3e0(0x69);
    func_0x003dd3e0(0x6a);
  }
  return uVar4;
}



undefined4 FUN_0005ae50(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  undefined8 uVar4;
  undefined8 uVar5;
  undefined8 uVar6;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        uVar3 = 0x1a;
        uVar4 = 2;
        uVar5 = 1;
        uVar6 = 0;
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar3 = 0x19;
          if (sRam003f08c8 != 0) {
            return 1;
          }
          uVar4 = 1;
          uVar5 = 0;
          uVar6 = 0;
        }
        else {
          if (sRam003f08c8 == 2) {
            uVar3 = 0x1b;
            uVar4 = 3;
            uVar5 = 1;
            uVar6 = 0;
          }
          else {
            uVar3 = 0x1c;
            if (sRam003f08c8 != 3) {
              return 1;
            }
            uVar4 = 4;
            uVar5 = 1;
            uVar6 = 1;
          }
        }
      }
      sVar1 = func_0x003de140(uVar3,uVar4,uVar5,uVar6);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined4 FUN_0005af48(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 1) {
        func_0x003de3c0(3);
        sVar1 = func_0x003de140(0x24,0x7a,1,1);
      }
      else {
        if (sRam003f08c8 < 2) {
          uVar2 = 0x79;
          if (sRam003f08c8 != 0) {
            return 1;
          }
        }
        else {
          uVar2 = 0x7b;
          if (sRam003f08c8 != 2) {
            return 1;
          }
        }
        sVar1 = func_0x003ddb60(uVar2);
      }
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar2 = 0;
    }
  }
  return uVar2;
}



undefined8 FUN_0005b010(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(0x11);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xdd);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_0005b098(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(2);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 6) {
                    // WARNING: Could not emulate address calculation at 0x0005b100
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x400250))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(2);
      func_0x003dd3e0(0x10);
      func_0x003dd3e0(0x11);
      func_0x003dd3e0(0x12);
      func_0x003dd3e0(0x13);
    }
  }
  return uVar1;
}



undefined8 FUN_0005b300(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd278(2);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 7) {
                    // WARNING: Could not emulate address calculation at 0x0005b368
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x400290))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003d0848(2);
      func_0x003dd390(2);
      func_0x003dd3e0(0x10);
      func_0x003dd3e0(0x11);
      func_0x003dd3e0(0x12);
      func_0x003dd3e0(0x13);
    }
  }
  return uVar1;
}



undefined8 FUN_0005b590(short param_1)

{
  short sVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (param_1 == 0) {
    uVar3 = func_0x003dd228(3);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    func_0x003de3c0(3);
    sVar1 = func_0x003de140(0x20,0x142,1,1);
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 != 0) {
LAB_0005b690:
        func_0x003dd390(3);
        func_0x003dd3e0(0x14);
        func_0x003dd3e0(0x15);
        func_0x003dd3e0(0x16);
        func_0x003dd3e0(0x17);
        return 1;
      }
      uVar3 = 0x141;
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar3 = 0x143;
      }
      else {
        if (sRam003f08c8 != 3) goto LAB_0005b690;
        lVar2 = func_0x003dd200(0x28);
        if (lVar2 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar3 = 0x14ca;
      }
    }
    sVar1 = func_0x003ddb60(uVar3);
  }
  sRam003f08c8 = sRam003f08c8 + sVar1;
  return 0;
}



undefined8 FUN_0005b6d8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd228(4);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    func_0x003de3c0(3);
    sVar1 = func_0x003de140(0x21,0x1a6,1,1);
LAB_0005b790:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0x1a5;
        goto LAB_0005b784;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0x1a7;
LAB_0005b784:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_0005b790;
      }
    }
    uVar3 = 1;
    func_0x003dd390(4);
    func_0x003d0848(0x18);
    func_0x003dd390(0x18);
    func_0x003d0848(0x19);
    func_0x003dd390(0x19);
    func_0x003dd3e0(0x1a);
    func_0x003dd3e0(0x1b);
  }
  return uVar3;
}



undefined8 FUN_0005b7f0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined8 uVar3;
  
  uVar3 = 0;
  if (param_1 == 0) {
    uVar3 = func_0x003dd228(5);
    return uVar3;
  }
  if (param_1 != 1) {
    return 0;
  }
  if (sRam003f08c8 == 1) {
    func_0x003de3c0(3);
    sVar1 = func_0x003de140(0x22,0x20a,1,1);
LAB_0005b8a8:
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  else {
    if (sRam003f08c8 < 2) {
      if (sRam003f08c8 == 0) {
        uVar2 = 0x209;
        goto LAB_0005b89c;
      }
    }
    else {
      if (sRam003f08c8 == 2) {
        uVar2 = 0x20b;
LAB_0005b89c:
        sVar1 = func_0x003ddb60(uVar2);
        goto LAB_0005b8a8;
      }
    }
    uVar3 = 1;
    func_0x003dd390(5);
    func_0x003d0848(0x1c);
    func_0x003dd390(0x1c);
    func_0x003dd3e0(0x1d);
    func_0x003dd3e0(0x1e);
    func_0x003dd3e0(0x1f);
  }
  return uVar3;
}



undefined8 FUN_0005b900(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 == 0) {
    uVar1 = func_0x003dd228(6);
  }
  else {
    uVar1 = 0;
    if (param_1 == 1) {
      if (uRam003f08c8 < 10) {
                    // WARNING: Could not emulate address calculation at 0x0005b968
                    // WARNING: Treating indirect jump as call
        uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x4002d0))();
        return uVar1;
      }
      uVar1 = 1;
      func_0x003dd390(6);
      func_0x003dd3e0(0x20);
      func_0x003dd3e0(0x21);
      func_0x003dd3e0(0x22);
      func_0x003dd3e0(0x23);
    }
  }
  return uVar1;
}



undefined4 FUN_0005bbf8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x2d1);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_0005bc70(short param_1)

{
  short sVar1;
  long lVar2;
  short *psVar3;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (sRam003f08c8 == 0) {
      sVar1 = func_0x003ddb60(0x303);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      return 0;
    }
    psVar3 = (short *)0x3f0480;
    while (*psVar3 != -1) {
      lVar2 = func_0x003dd228(*psVar3);
      if (lVar2 == 1) {
        func_0x003d0848(*psVar3);
        func_0x003d0650(*psVar3,0);
        psVar3 = psVar3 + 1;
      }
      else {
        psVar3 = psVar3 + 1;
      }
    }
  }
  return 1;
}



undefined4 FUN_0005bd28(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x349);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_0005bda0(short param_1)

{
  short sVar1;
  long lVar2;
  undefined8 uVar3;
  short *psVar4;
  undefined4 uVar5;
  
  uVar5 = 0;
  if (param_1 == 0) {
LAB_0005be74:
    uVar5 = 1;
  }
  else {
    if (param_1 != 1) {
      return 0;
    }
    if (sRam003f08c8 == 0) {
      uVar3 = 0x35d;
    }
    else {
      if (sRam003f08c8 != 1) {
        for (psVar4 = (short *)0x3f0490; *psVar4 != -1; psVar4 = psVar4 + 1) {
          func_0x003d0848(*psVar4);
          func_0x003dd390();
          *(undefined2 *)(*psVar4 * 2 + 0xc00130) = 6;
          *(undefined2 *)(*psVar4 * 2 + 0xc0354a) = 2;
        }
        goto LAB_0005be74;
      }
      lVar2 = func_0x003dd8a8(0x4c);
      uVar3 = 0x35e;
      if (lVar2 != 1) {
        uVar3 = 0x35f;
      }
    }
    sVar1 = func_0x003ddb60(uVar3);
    sRam003f08c8 = sRam003f08c8 + sVar1;
  }
  return uVar5;
}



undefined8 FUN_0005be98(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(0x2a);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x3df);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003dd390(0x2a);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_0005bf30(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x407);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_0005bfa8(short param_1)

{
  short sVar1;
  long lVar2;
  short *psVar3;
  int iVar4;
  undefined4 uVar5;
  
  if (param_1 == 0) {
    psVar3 = (short *)0x3f04a0;
    iVar4 = 0;
    sVar1 = sRam003f04a0;
    if (sRam003f04a0 != -1) {
      while( true ) {
        psVar3 = psVar3 + 1;
        lVar2 = func_0x003dd8a8(sVar1);
        if (lVar2 == 1) {
          iVar4 = (iVar4 + 1) * 0x10000 >> 0x10;
        }
        if (*psVar3 == -1) break;
        sVar1 = *psVar3;
      }
    }
    uVar5 = 0;
    if (0 < iVar4) {
      lVar2 = func_0x003dd8a8(0xc);
      if (lVar2 == 1) {
        lVar2 = func_0x003dd8a8(0x35);
        uVar5 = 0;
        if (lVar2 == 1) {
          uVar5 = 1;
        }
      }
      else {
        uVar5 = 0;
      }
    }
  }
  else {
    uVar5 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x4bb);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar5 = 0;
      }
      else {
        uVar5 = 1;
      }
    }
  }
  return uVar5;
}



undefined4 FUN_0005c0c0(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0x5bf);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(1);
        uRam00c00132 = 1;
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined8 FUN_0005c160(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (uRam003f08c8 < 0xb) {
                    // WARNING: Could not emulate address calculation at 0x0005c1a4
                    // WARNING: Treating indirect jump as call
      uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x400320))();
      return uVar1;
    }
    func_0x003dd798();
  }
  return 1;
}



undefined8 FUN_0005c2f8(short param_1)

{
  undefined8 uVar1;
  
  if (param_1 != 0) {
    if (param_1 != 1) {
      return 0;
    }
    if (uRam003f08c8 < 5) {
                    // WARNING: Could not emulate address calculation at 0x0005c350
                    // WARNING: Treating indirect jump as call
      uVar1 = (**(code **)((short)uRam003f08c8 * 4 + 0x400350))();
      return uVar1;
    }
    uRam00c01ea2 = (uRam00c01ea2 & 0xff00) + 0x8a;
    uRam00c04560 = 0xb2;
    uRam00c0013a = 6;
    uRam00c03554 = 2;
    func_0x003dd8d8(7);
  }
  return 1;
}



bool FUN_0005c450(short param_1)

{
  bool bVar1;
  short sVar2;
  int iVar3;
  
  bVar1 = false;
  if (param_1 == 0) {
    sVar2 = 8;
    iVar3 = 0x10;
    do {
      if ((*(short *)(iVar3 + 0xc00130) == 2) && (*(short *)(iVar3 + 0xc04954) != 0)) {
        bVar1 = true;
        func_0x003d0848(sVar2);
        func_0x003dd390(sVar2);
      }
      sVar2 = sVar2 + 1;
      iVar3 = iVar3 + 2;
    } while (sVar2 < 0x2b);
  }
  else {
    bVar1 = false;
    if (param_1 == 1) {
      bVar1 = sRam003f08c8 == 0;
      if (bVar1) {
        sVar2 = func_0x003ddb60(0x8cb);
        sRam003f08c8 = sRam003f08c8 + sVar2;
      }
      bVar1 = !bVar1;
    }
  }
  return bVar1;
}



undefined8 FUN_0005c540(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd228(0x41);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xc45);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003dd390(0x41);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_0005c5d8(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xd5d);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(0x3e);
        func_0x003dd390(0x3e);
        func_0x003dd438(0x3e,9,0);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_0005c678(short param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (param_1 == 0) {
    uVar2 = 1;
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xd85);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        func_0x003d0848(0x3e);
        func_0x003dd390(0x3e);
        func_0x003dd438(0x3e,9,0);
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_0005c718(short param_1)

{
  short sVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    uVar3 = 1;
  }
  else {
    uVar3 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        uVar3 = 0xddf;
      }
      else {
        if (sRam003f08c8 != 1) {
          return 1;
        }
        lVar2 = func_0x003dd200(3);
        if (lVar2 != 1) {
          sRam003f08c8 = sRam003f08c8 + 1;
          return 0;
        }
        uVar3 = 0xde0;
      }
      sVar1 = func_0x003ddb60(uVar3);
      sRam003f08c8 = sRam003f08c8 + sVar1;
      uVar3 = 0;
    }
  }
  return uVar3;
}



undefined4 FUN_0005c7c8(short param_1)

{
  short sVar1;
  long lVar2;
  undefined4 uVar3;
  
  if (param_1 == 0) {
    lVar2 = func_0x003dd8a8(400);
    if (lVar2 == 1) {
      lVar2 = func_0x003dd8a8(0x191);
      if (lVar2 == 1) {
        uVar3 = 1;
      }
      else {
        lVar2 = func_0x003dd8a8(0x192);
        uVar3 = 0;
        if (lVar2 == 1) {
          uVar3 = 1;
        }
      }
    }
    else {
      uVar3 = 0;
    }
  }
  else {
    uVar3 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xfbf);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar3 = 0;
      }
      else {
        uVar3 = 1;
      }
    }
  }
  return uVar3;
}



undefined8 FUN_0005c878(short param_1)

{
  short sVar1;
  undefined8 uVar2;
  
  if (param_1 == 0) {
    uVar2 = func_0x003dd200(0x6a);
  }
  else {
    uVar2 = 0;
    if (param_1 == 1) {
      if (sRam003f08c8 == 0) {
        sVar1 = func_0x003ddb60(0xfe7);
        sRam003f08c8 = sRam003f08c8 + sVar1;
        uVar2 = 0;
      }
      else {
        uVar2 = 1;
      }
    }
  }
  return uVar2;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0005c900(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0005ca90(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_0005cb38(short param_1,short param_2,short param_3)

{
  uint uVar1;
  int iVar2;
  int iVar3;
  uint uVar4;
  uint uVar5;
  
  uVar4 = (int)param_2 - 1U & 0xff;
  uVar5 = (int)param_1 & 0xff;
  uVar1 = func_0x003977c0(uVar4,uVar5);
  if (10 < (uVar1 & 0xffff)) {
    func_0x00397738(uVar4,uVar5,1);
    uVar1 = 1;
  }
  iVar3 = param_1 * 2;
  iVar2 = uVar1 * *(ushort *)(param_2 * 2 + 0x3e8258);
  *(short *)(iVar3 + 0xc00934) = param_2;
  if (param_3 < iVar2) {
    iVar2 = (int)param_3;
  }
  *(short *)(iVar3 + 0xc00a8a) = (short)iVar2;
  return;
}



// WARNING: Removing unreachable block (ram,0x0005cf0c)

void FUN_0005ce98(int param_1)

{
  int iVar1;
  int iVar2;
  uint uVar3;
  int iVar4;
  
  uVar3 = 0;
  iVar4 = 0;
  do {
    if ((0x17 < *(ushort *)(iVar4 + 0xc066b8)) && (uVar3 != 2)) {
      iVar1 = func_0x001881e8(param_1);
      iVar2 = func_0x001881e8();
      param_1 = (int)(short)uVar3;
      if (iVar1 % 6 + iVar2 % 6 + 2 < 5) {
        param_1 = func_0x003d0f90();
        if (1 < param_1) {
          param_1 = (param_1 + -1) * 0x10000 >> 0x10;
        }
        *(undefined2 *)(iVar4 + 0xc069b8) = *(undefined2 *)(param_1 * 2 + 0x3e8578);
      }
    }
    uVar3 = uVar3 + 1 & 0xffff;
    iVar4 = uVar3 << 1;
  } while (uVar3 < 0x80);
  return;
}



// WARNING: Removing unreachable block (ram,0x0005d144)

undefined4 FUN_0005d048(uint param_1,long param_2)

{
  long lVar1;
  byte *pbVar2;
  undefined4 uVar3;
  
  uVar3 = 0;
  lVar1 = (*pcRam00c000bc)();
  if (lVar1 == 1) {
    func_0x00382ad8();
    func_0x00382b60();
  }
  if (sRam003f08da == 1) {
    if ((*(byte *)((uint)bRam003ed441 * 0x82c + 0x3ed47c) & 2) != 0) goto LAB_0005d21c;
    sRam003f08da = 2;
    if (param_2 != 0) {
      pbVar2 = (byte *)((uint)bRam003ed441 * 0x82c + 0x3ed480);
      *pbVar2 = *pbVar2 & 0xf1 | 4;
      *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed492) =
           *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed48a);
      *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed494) =
           *(undefined2 *)((uint)bRam003ed441 * 0x82c + 0x3ed48c);
      goto LAB_0005d21c;
    }
  }
  else {
    if (sRam003f08da < 2) {
      if (sRam003f08da == 0) {
        if (param_2 == 0) {
          func_0x003bba50(1,0,0x3c,0xa0,0x3c,0xa0);
        }
        else {
          func_0x003bba50(1,0,0xfffffffffffffed4,0xa0,400,0xa0);
        }
        uRam003f08dc = func_0x00396fb8(0x45,(param_1 & 0xffff) / 100 & 0xff,(param_1 & 0xffff) % 100
                                       ,0);
        sRam003f08da = sRam003f08da + 1;
      }
      goto LAB_0005d21c;
    }
    if ((sRam003f08da != 2) || ((*(byte *)((uint)bRam003ed441 * 0x82c + 0x3ed480) & 8) == 0))
    goto LAB_0005d21c;
  }
  sRam003f08da = 0;
  uVar3 = 1;
LAB_0005d21c:
  lVar1 = func_0x00124e80();
  if (lVar1 == 0) {
    func_0x003bbc08(uRam003f08dc);
  }
  return uVar3;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0005d260(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



undefined4 FUN_0005d300(void)

{
  undefined4 uVar1;
  
  if (sRam003f08d8 == 0) {
    func_0x003979d8(1,0x10c,8);
    func_0x003ddba0();
    sRam003f08d8 = sRam003f08d8 + 1;
    uVar1 = 0;
  }
  else {
    uVar1 = 0;
    if (sRam003f08d8 == 1) {
      if (sRam003ed41c < sRam003ed418) {
        func_0x00147f88(0,uRam003ed420,0,0,0);
        sRam003ed41c = 0;
        sRam003f08d8 = 0;
        uVar1 = 1;
      }
      else {
        func_0x001481b0(0,sRam003ed418,sRam003ed418,sRam003ed418);
        sRam003ed41c = sRam003ed41c - sRam003ed418;
        uVar1 = 0;
      }
    }
  }
  return uVar1;
}



undefined4 FUN_0005d3f8(void)

{
  undefined4 uVar1;
  int iVar2;
  
  if (sRam003f08d8 == 0) {
    func_0x003979d8(0xffffffffffffffff,0x10c,8);
    sRam003f08d8 = sRam003f08d8 + 1;
    uVar1 = 0;
  }
  else {
    uVar1 = 0;
    if (sRam003f08d8 == 1) {
      if (sRam003ed41c < (short)uRam003ed418) {
        func_0x00147f88(0,uRam003ed420,0xffffffffffffff01,0xffffffffffffff01,0xffffffffffffff01);
        sRam003ed41c = 0;
        func_0x003ddbd0();
        sRam003f08d8 = 0;
        uVar1 = 1;
      }
      else {
        iVar2 = (int)((uint)uRam003ed418 * -0x10000) >> 0x10;
        func_0x001481b0(0,iVar2,iVar2,iVar2);
        sRam003ed41c = sRam003ed41c - uRam003ed418;
        uVar1 = 0;
      }
    }
  }
  return uVar1;
}



void FUN_0005d500(void)

{
  undefined auStack320 [8];
  undefined local_138;
  undefined local_137;
  undefined local_136;
  undefined local_135;
  undefined local_134;
  undefined local_133;
  undefined local_132;
  undefined local_a0 [4];
  undefined local_9c;
  undefined local_9b;
  undefined local_9a;
  undefined local_99;
  undefined4 local_94;
  
  func_0x00143ed0(auStack320);
  local_138 = 0;
  local_137 = 0;
  local_136 = 0;
  local_135 = 0;
  local_134 = 0;
  local_133 = 0;
  local_132 = 0;
  func_0x00143f50(auStack320);
  func_0x00143ef8(local_a0);
  local_9b = 3;
  local_99 = 2;
  local_9a = 1;
  local_94 = 0x1100000;
  local_a0[0] = 1;
  local_9c = 0;
  func_0x001445e0(8,local_a0);
  func_0x00144e48(0x3f0888);
  func_0x001475c0(4,0);
  func_0x001475c0(8,4);
  func_0x003ddbd0();
  func_0x001485c8();
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0005d5c0(void)

{
  func_0x003965e0();
  func_0x00396780();
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_0005d718(void)

{
  undefined *puVar1;
  ulong *puVar2;
  uint uVar3;
  uint uVar4;
  undefined local_d0 [4];
  undefined local_cc;
  undefined local_cb;
  undefined local_ca;
  undefined local_c9;
  undefined4 local_c4 [33];
  undefined auStack64 [8];
  undefined local_38;
  undefined local_37;
  undefined local_36;
  undefined local_35;
  undefined local_34;
  undefined local_33;
  undefined local_32;
  undefined auStack48 [8];
  undefined8 uStack40;
  
  puVar1 = auStack48 + 7;
  uVar3 = (uint)puVar1 & 7;
  *(ulong *)(puVar1 + -uVar3) =
       *(ulong *)(puVar1 + -uVar3) & -1 << (uVar3 + 1) * 8 | uRam004007a0 >> (7 - uVar3) * 8;
  auStack48 = uRam004007a0;
  uVar3 = (int)&uStack40 + 7U & 7;
  puVar2 = (ulong *)(((int)&uStack40 + 7U) - uVar3);
  *puVar2 = *puVar2 & -1 << (uVar3 + 1) * 8 | uRam004007a8 >> (7 - uVar3) * 8;
  uStack40 = uRam004007a8;
  func_0x00143ed0(auStack64);
  local_38 = 0;
  local_37 = 0;
  local_36 = 0;
  local_35 = 0;
  local_34 = 0;
  local_33 = 0;
  local_32 = 0;
  func_0x00143f50(auStack64);
  func_0x00143ef8();
  local_cb = 3;
  local_c9 = 2;
  local_ca = 1;
  local_d0[0] = 1;
  local_cc = 0;
  uVar3 = 0;
  do {
    uVar4 = uVar3 + 1 & 0xffff;
    local_c4[uVar3] = 0x1140000;
    uVar3 = uVar4;
  } while (uVar4 < 0x10);
  func_0x001445e0(4,local_d0);
  func_0x00144e48(auStack48);
  func_0x001475c0(4,5);
  func_0x001475c0(8,4);
  uRam0020d084 = 1;
  uRam0020d160 = uRam0020d160 | 0x101;
  func_0x003ddbd0();
  func_0x001485c8();
  return;
}



void FUN_0005d840(ulong param_1,undefined8 param_2,int param_3,int param_4)

{
  long lVar1;
  
  uRam00402e90 = (undefined2)(param_1 & 0xffff);
  lVar1 = (*pcRam00c000bc)(param_1,param_2,param_3 << 0x10,param_4 << 0x10);
  if (lVar1 == 1) {
    func_0x00382ad8(0xa8,0x90);
    func_0x00382b60(4,1,0x4007b0,param_1 & 0xffff);
  }
  if (6 < uRam003f08de) {
    return;
  }
                    // WARNING: Could not emulate address calculation at 0x0005d8e4
                    // WARNING: Treating indirect jump as call
  (**(code **)((short)uRam003f08de * 4 + 0x4007d0))();
  return;
}



void FUN_0005dac0(short param_1)

{
  if ((long)sRam003f08cc != (long)(int)param_1) {
    if (iRam003f08d0 == 1) {
      (*pcRam00c00048)();
    }
    sRam003f08cc = param_1;
    (*pcRam00c00044)(0,(long)(int)param_1 & 0xffff);
  }
  iRam003f08d0 = 1;
  return;
}



void FUN_0005db58(void)

{
  if (iRam003f08d0 == 1) {
    (*pcRam00c00048)();
  }
  uRam003f08cc = 0xffff;
  iRam003f08d0 = 0;
  return;
}



byte * FUN_0005dba8(byte *param_1,ushort *param_2,ulong param_3)

{
  int *piVar1;
  short sVar2;
  long lVar3;
  short *psVar4;
  byte *pbVar5;
  int iVar6;
  int iVar7;
  int iVar8;
  
  iVar7 = 0;
  uRam003f08d0 = 0;
  if ((long)param_3 < 0) {
    if (*(int *)(param_1 + 0x20) != 0) {
      iVar8 = 0;
      pbVar5 = param_1;
      do {
        iVar6 = 0;
        sVar2 = 0;
        psVar4 = (short *)(param_1 + (iVar8 + iVar7) * 4 + 0x12);
        do {
          if ((*psVar4 != 0) && (lVar3 = func_0x003dd8a8(*psVar4), lVar3 == 1)) {
            iVar6 = -1;
            break;
          }
          sVar2 = sVar2 + 1;
          psVar4 = psVar4 + 1;
        } while (sVar2 < 4);
        if (iVar6 == 0) {
          sVar2 = 0;
          psVar4 = (short *)(param_1 + (iVar8 + iVar7) * 4);
          do {
            psVar4 = psVar4 + 1;
            if (*psVar4 != 0) {
              lVar3 = func_0x003dd8a8(*psVar4);
              if (lVar3 == 1) {
                iVar6 = (iVar6 + 1) * 0x10000 >> 0x10;
              }
            }
            sVar2 = sVar2 + 1;
          } while (sVar2 < 8);
          if (((int)(uint)*pbVar5 <= iVar6) &&
             (lVar3 = (**(code **)(pbVar5 + 0x20))(0,pbVar5), lVar3 == 1)) {
            uRam003f08cc = *param_2;
            if ((int)((uint)uRam003f08cc << 0x10) < 0) {
              uRam003f08cc = 0;
            }
            (*pcRam00c00044)(0,uRam003f08cc);
            uRam003f08d0 = 1;
            return pbVar5;
          }
        }
        piVar1 = (int *)(pbVar5 + 0x44);
        iVar8 = iVar8 + 8;
        iVar7 = (iVar7 + 1) * 0x10000 >> 0x10;
        param_2 = param_2 + 1;
        pbVar5 = pbVar5 + 0x24;
        if (*piVar1 == 0) {
          return (byte *)0x0;
        }
      } while( true );
    }
  }
  else {
    iVar7 = *(int *)(param_1 + 0x20);
    while (iVar7 != 0) {
      if (((ulong)*(ushort *)(param_1 + 0x1c) & 0xfff) == param_3) {
        uRam003f08cc = *param_2;
        if ((int)((uint)uRam003f08cc << 0x10) < 0) {
          uRam003f08cc = 0;
        }
        (*pcRam00c00044)(0,uRam003f08cc);
        uRam003f08d0 = 1;
        return param_1;
      }
      param_2 = param_2 + 1;
      iVar7 = *(int *)(param_1 + 0x44);
      param_1 = param_1 + 0x24;
    }
  }
  uRam003f08d0 = 0;
  return (byte *)0x0;
}



undefined4 FUN_0005de20(int param_1)

{
  undefined4 uVar1;
  long lVar2;
  
  lVar2 = (**(code **)(param_1 + 0x20))();
  uVar1 = 0;
  if (lVar2 == 1) {
    bRam003edca8 = bRam003edca8 & 0xfe;
    bRam003ed47c = bRam003ed47c & 0xfe;
    func_0x003dd8d8(*(undefined2 *)(param_1 + 0x1a));
    if (iRam003f08d0 == 1) {
      (*pcRam00c00068)();
      iRam003f08d0 = 0;
    }
    func_0x001392a0();
    uVar1 = 1;
  }
  return uVar1;
}



long FUN_0005dec8(undefined8 param_1,undefined8 param_2)

{
  long lVar1;
  long lVar2;
  
  lVar1 = func_0x003de4a8(*(undefined4 *)((uint)uRam00c00100 * 4 + 0x3eeb60),
                          *(undefined4 *)((uint)uRam00c00100 * 4 + 0x3f08a8),param_2);
  lVar2 = lVar1;
  if (lVar1 != 0) {
    uRam003f08c8 = 0;
    lVar2 = -1;
    if (((long)*(short *)((int)lVar1 + 0x1c) & 0xf000U) == 0) {
      lVar2 = 1;
    }
  }
  iRam003f08d4 = (int)lVar1;
  return lVar2;
}



// WARNING: Control flow encountered bad instruction data

void FUN_0005df38(void)

{
  long lVar1;
  
  lVar1 = (*pcRam00c000bc)();
  if (lVar1 == 1) {
    func_0x00382ad8(0x18,0x60);
    func_0x00382b60(4,1,0x4007f0,*(ushort *)(iRam003f08d4 + 0x1c) & 0xfff);
    if (iRam003f08d0 == 1) {
      func_0x00382ad8(0xe0,0x60);
      func_0x00382b60(4,1,0x400800,uRam003f08cc);
    }
  }
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_0005e1f8(int param_1,ulong param_2,int param_3,uint param_4,uint param_5,int param_6,
                 undefined2 param_7)

{
  ushort uVar1;
  int iVar2;
  ushort *puVar3;
  
  iVar2 = param_3 - param_4;
  do {
    if (*(ushort *)((iVar2 * 4 + param_4) * 2 + param_6) == param_2) {
      iVar2 = param_3 - param_4;
      goto LAB_0005e238;
    }
    param_4 = param_4 + 1 & 0xff;
    param_3 = param_4 * 8;
    iVar2 = param_4 * 7;
  } while (param_4 < param_5);
  iVar2 = param_4 * 7;
LAB_0005e238:
  iVar2 = (iVar2 * 4 + param_4) * 2;
  puVar3 = (ushort *)(iVar2 + param_1 + 0x2c82);
  uVar1 = *puVar3;
  if ((1 < uVar1) && (uVar1 != 5)) {
    *puVar3 = 4;
    *(undefined2 *)(param_1 + iVar2 + 0x2c84) = param_7;
    puVar3 = (ushort *)(param_4 * 2 + 0xc08558);
    *puVar3 = *puVar3 | 0x80;
  }
  return;
}



undefined4 FUN_0005e2a0(undefined8 param_1,short param_2)

{
  bool bVar1;
  short sVar2;
  uint uVar3;
  uint uVar4;
  int iVar5;
  undefined2 *puVar6;
  ushort *puVar7;
  short *psVar8;
  uint uVar9;
  int iVar10;
  
  func_0x003de9c8();
  uVar9 = 0;
  if (uRam00c08500 != 0) {
    if (sRam003e2a38 == param_2) {
      uVar4 = 0;
    }
    else {
      uVar3 = 1;
      do {
        uVar9 = uVar3 & 0xff;
        uVar4 = uVar3 & 0xff;
        if (uRam00c08500 <= uVar4) goto LAB_0005e3c8;
        uVar3 = uVar9 + 1;
      } while (*(short *)(uVar9 * 0x3a + 0x3e2a38) != param_2);
    }
    if (uVar4 < uRam00c08500) {
      puVar7 = (ushort *)(uVar9 * 2 + 0xc08518);
      *puVar7 = *puVar7 | 0x80;
      psVar8 = (short *)(uVar9 * 0x3a + 0x3e2a46);
      sVar2 = *psVar8;
      if (sVar2 < 0x7f) {
        if (sVar2 != 0) {
          sVar2 = sVar2 + -1;
          *psVar8 = sVar2;
          goto LAB_0005e3ac;
        }
      }
      else {
LAB_0005e3ac:
        if (sVar2 != 0) {
          return 1;
        }
      }
      iVar10 = uVar9 * 7;
      iVar5 = 0x3e2a38;
      goto LAB_0005e4a4;
    }
  }
LAB_0005e3c8:
  uVar9 = 0;
  if (uRam00c08540 == 0) {
    iVar10 = 0;
  }
  else {
    iVar10 = 0;
    if (sRam003e2c80 != param_2) {
      uVar4 = 1;
      do {
        uVar9 = uVar4 & 0xff;
        iVar10 = uVar9 * 8;
        if (uRam00c08540 <= uVar9) break;
        uVar4 = uVar9 + 1;
      } while (*(short *)(uVar9 * 0x3a + 0x3e2c80) != param_2);
    }
  }
  puVar7 = (ushort *)(uVar9 * 2 + 0xc08558);
  *puVar7 = *puVar7 | 0x80;
  psVar8 = (short *)(iVar10 * 8 + uVar9 * -6 + 0x3e2c8e);
  sVar2 = *psVar8;
  if (sVar2 < 0x7f) {
    bVar1 = sVar2 != 0;
    sVar2 = sVar2 + -1;
    if (bVar1) {
      *psVar8 = sVar2;
      goto LAB_0005e490;
    }
  }
  else {
LAB_0005e490:
    if (sVar2 != 0) {
      return 1;
    }
  }
  iVar10 = iVar10 - uVar9;
  iVar5 = 0x3e2c80;
LAB_0005e4a4:
  puVar6 = (undefined2 *)((iVar10 * 4 + uVar9) * 2 + iVar5 + 2);
  *puVar6 = 0;
  puVar6[8] = 0;
  return 0;
}



void FUN_0005e4e0(undefined2 param_1,int param_2)

{
  int iVar1;
  uint uVar2;
  uint uVar3;
  
  uVar2 = 0;
  uVar3 = (uint)*(ushort *)(param_2 * 2 + 0xc00004);
  iVar1 = param_2;
  if (uVar3 != 0) {
    do {
      func_0x003de9c8(param_1,*(undefined2 *)(iVar1 * 2 + 0xc00006));
      uVar2 = uVar2 + 1 & 0xffff;
      iVar1 = uVar2 + param_2;
    } while (uVar2 < uVar3);
  }
  return;
}



void FUN_0005e568(uint param_1)

{
  bool bVar1;
  byte bVar2;
  uint uVar3;
  uint uVar4;
  byte *pbVar5;
  int iVar6;
  ulong uVar7;
  int iVar8;
  uint uVar9;
  byte *pbVar10;
  uint uVar11;
  byte local_30 [16];
  
  pbVar5 = local_30;
  uVar4 = 0;
  uVar11 = param_1 & 0xffff;
  if (uRam00c08500 != 0) {
    if (uRam003e2a38 == uVar11) {
      uVar9 = 0;
    }
    else {
      uVar3 = 1;
      do {
        uVar4 = uVar3 & 0xff;
        uVar9 = uVar3 & 0xff;
        if (uRam00c08500 <= uVar9) goto LAB_0005e618;
        uVar3 = uVar4 + 1;
      } while (*(ushort *)(uVar4 * 0x3a + 0x3e2a38) != uVar11);
    }
    if (uVar9 < uRam00c08500) {
      iVar8 = 0x3e2a38;
      iVar6 = uVar4 * 7;
      goto LAB_0005e69c;
    }
  }
LAB_0005e618:
  uVar4 = 0;
  if (uRam00c08540 == 0) {
    iVar6 = 0;
  }
  else {
    if (uRam003e2c80 == uVar11) {
      iVar6 = 0;
    }
    else {
      uVar4 = 1;
      while( true ) {
        uVar4 = uVar4 & 0xff;
        iVar6 = uVar4 * 8;
        if (uRam00c08540 <= uVar4) break;
        if (*(ushort *)(uVar4 * 0x3a + 0x3e2c80) == uVar11) {
          iVar6 = uVar4 * 7;
          goto LAB_0005e698;
        }
        uVar4 = uVar4 + 1;
      }
    }
  }
  iVar6 = iVar6 - uVar4;
LAB_0005e698:
  iVar8 = 0x3e2c80;
LAB_0005e69c:
  iVar8 = (iVar6 * 4 + uVar4) * 2 + iVar8;
  uVar4 = 0;
  do {
    uVar7 = func_0x003de8f8(iVar8,uVar4);
    pbVar10 = local_30 + uVar4;
    uVar4 = uVar4 + 1 & 0xff;
    if (uVar7 < 0xb) {
      *pbVar10 = (byte)uVar7;
    }
    else {
      *pbVar10 = 0;
    }
  } while (uVar4 < 10);
  uVar9 = 0;
  uVar4 = 0;
  do {
    if (*pbVar5 < local_30[uVar9]) {
      uVar4 = uVar9;
    }
    uVar9 = uVar9 + 1 & 0xff;
    pbVar5 = local_30 + uVar4;
  } while (uVar9 < 10);
  *(undefined2 *)(iVar8 + 8) = 0;
  *(short *)(iVar8 + 6) = (short)uVar4 + 1;
  bVar2 = 1;
  do {
    bVar1 = bVar2 < 10;
    bVar2 = bVar2 + 1;
  } while (bVar1);
  iVar6 = uVar11 * 2;
  *(undefined2 *)(iVar8 + 0x20) = 0;
  *(undefined2 *)(iVar8 + 10) = 0;
  *(undefined2 *)(iVar8 + 0x1e) = 0;
  *(undefined2 *)(iVar6 + 0xc023ec) = 0;
  *(undefined2 *)(iVar6 + 0xc01fea) = 0;
  *(undefined2 *)(iVar6 + 0xc02296) = 0;
  *(undefined2 *)(iVar6 + 0xc02140) = 0;
  return;
}



void FUN_0005e7b8(int param_1)

{
  short sVar1;
  undefined2 uVar2;
  int iVar3;
  undefined2 *puVar4;
  short *psVar5;
  int iVar6;
  uint uVar7;
  
  uVar7 = (uint)*(ushort *)(param_1 * 2 + 0xc00004);
  if (param_1 == 0x427e) {
    iVar3 = 0x3e2a38;
  }
  else {
    iVar3 = 0x3e2c80;
  }
  iVar6 = 0;
  if (uVar7 != 0) {
    psVar5 = (short *)(iVar3 + 2);
    puVar4 = (undefined2 *)(param_1 * 2 + 0xc00006);
    sVar1 = *psVar5;
    while( true ) {
      psVar5 = psVar5 + 0x1d;
      uVar2 = *puVar4;
      puVar4 = puVar4 + 1;
      if ((sVar1 == 4) || (sVar1 == 0)) {
        func_0x003dee68(uVar2);
      }
      iVar6 = (int)(char)((char)iVar6 + '\x01');
      if ((int)uVar7 <= iVar6) break;
      sVar1 = *psVar5;
    }
  }
  return;
}



void FUN_0005e880(int param_1)

{
  undefined2 uVar1;
  long lVar2;
  int iVar3;
  undefined2 *puVar4;
  int iVar5;
  uint uVar6;
  
  uVar6 = (uint)*(ushort *)(param_1 * 2 + 0xc00004);
  if (param_1 == 0x427e) {
    iVar3 = 0x3e2a38;
  }
  else {
    iVar3 = 0x3e2c80;
  }
  iVar5 = 0;
  if (uVar6 != 0) {
    puVar4 = (undefined2 *)(param_1 * 2 + 0xc00006);
    do {
      uVar1 = *puVar4;
      puVar4 = puVar4 + 1;
      lVar2 = func_0x003de970(uVar1);
      if (lVar2 == 0) {
        *(undefined2 *)(iVar3 + 0x12) = *(undefined2 *)(iVar3 + 0x24);
      }
      iVar5 = (iVar5 + 1) * 0x1000000 >> 0x18;
      iVar3 = iVar3 + 0x3a;
    } while (iVar5 < (int)uVar6);
  }
  return;
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x0005e940: Changing call to branch
// WARNING: Removing unreachable block (ram,0x0005e948)

void FUN_0005e930(void)

{
  func_0x003df0b8(0x427e);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Possible PIC construction at 0x0005fc9c: Changing call to branch
// WARNING: Possible PIC construction at 0x0005ffb8: Changing call to branch
// WARNING: Possible PIC construction at 0x00060250: Changing call to branch
// WARNING: Possible PIC construction at 0x000603a0: Changing call to branch
// WARNING: Removing unreachable block (ram,0x00060258)
// WARNING: Removing unreachable block (ram,0x00060274)
// WARNING: Removing unreachable block (ram,0x00060288)
// WARNING: Removing unreachable block (ram,0x000602bc)
// WARNING: Removing unreachable block (ram,0x000602f8)
// WARNING: Removing unreachable block (ram,0x00060328)
// WARNING: Removing unreachable block (ram,0x0006032c)
// WARNING: Removing unreachable block (ram,0x0006033c)
// WARNING: Removing unreachable block (ram,0x00060334)
// WARNING: Removing unreachable block (ram,0x00060388)
// WARNING: Removing unreachable block (ram,0x0005ffc0)
// WARNING: Removing unreachable block (ram,0x0005fca4)
// WARNING: Removing unreachable block (ram,0x0005fcd0)
// WARNING: Removing unreachable block (ram,0x0005fd78)
// WARNING: Removing unreachable block (ram,0x0005fce4)
// WARNING: Removing unreachable block (ram,0x0005fe14)
// WARNING: Removing unreachable block (ram,0x0005fe30)
// WARNING: Removing unreachable block (ram,0x000603a8)

void FUN_0005ee08(void)

{
  int iVar1;
  long lVar2;
  undefined8 uVar3;
  ushort *puVar4;
  uint uVar5;
  
  if (4 < bRam003f0900) {
    func_0x00382ab8(7,0);
    func_0x00382ad8((int)((uRam00402eb2 + 4) * 0x10000) >> 0x10,
                    (int)((uRam00402eb4 + 2) * 0x10000) >> 0x10);
    func_0x00129d68(1);
    lVar2 = func_0x001252e8(sRam00402f16);
    if (lVar2 == 1) {
      func_0x00382b60(1,1,uRam003e7b70);
    }
    else {
      if (lVar2 < 2) {
        if (lVar2 == 0) {
          func_0x00382b60(1,1,*(undefined4 *)(sRam00402f16 * 4 + 0x3e78c0));
        }
      }
      else {
        if (lVar2 == 2) {
          func_0x00382b60(1,1,uRam003e7b6c);
        }
      }
    }
    func_0x00382ad8((int)((uRam00402eb2 + 0x44) * 0x10000) >> 0x10,
                    (int)((uRam00402eb4 + 0x1a) * 0x10000) >> 0x10);
    func_0x00129d68(2);
    lVar2 = func_0x001252e8(sRam00402f16);
    if (lVar2 == 0) {
      uVar5 = (uint)*(ushort *)(sRam00402f16 * 2 + 0xc00286);
      if (uVar5 < 8) {
        uVar3 = func_0x001383d0(*(undefined4 *)(uVar5 * 4 + 0x3e82e0));
      }
      else {
        if (uVar5 == 0x7e) {
          uVar3 = func_0x001383d0(0x400860);
        }
        else {
          uVar3 = func_0x001383d0(0x400870);
        }
      }
      func_0x00382b60(1,1,0x400858,uVar3);
    }
    func_0x00382ad8((int)((uRam00402eb2 + 0x44) * 0x10000) >> 0x10,
                    (int)((uRam00402eb4 + 0x22) * 0x10000) >> 0x10);
    func_0x00129d68(2);
    lVar2 = func_0x001252e8(sRam00402f16);
    if (lVar2 == 0) {
      uVar3 = func_0x003d1358(sRam00402f16);
      uVar3 = func_0x001383d0(uVar3);
      func_0x00382b60(1,1,0x400858,uVar3);
    }
    func_0x00382ad8((int)((uRam00402eb2 + 0x44) * 0x10000) >> 0x10,
                    (int)((uRam00402eb4 + 0x2a) * 0x10000) >> 0x10);
    func_0x00129d68(2);
    uVar3 = func_0x003d0e88(0x299,*(undefined2 *)(sRam00402f16 * 2 + 0xc00532));
    func_0x00382b60(1,1,0x400878,uVar3);
    func_0x00382ad8((int)((uRam00402eb2 + 0x44) * 0x10000) >> 0x10,
                    (int)((uRam00402eb4 + 0x32) * 0x10000) >> 0x10);
    func_0x00129d68(2);
    func_0x00382b60(1,1,0x400880,uRam00402f34,*(undefined2 *)(sRam00402f16 * 2 + 0xc01138));
    func_0x00382ad8((int)((uRam00402eb2 + 0x44) * 0x10000) >> 0x10,
                    (int)((uRam00402eb4 + 0x3a) * 0x10000) >> 0x10);
    func_0x00129d68(2);
    func_0x00382b60(1,1,0x400880,uRam00402f36);
    func_0x00382ad8((int)((uRam00402eb2 + 8) * 0x10000) >> 0x10,
                    (int)((uRam00402eb4 + 0x46) * 0x10000) >> 0x10);
    func_0x00129d68(2);
    func_0x00382b60(1,1,0x400888,*(undefined2 *)(sRam00402f16 * 2 + 0xc02bf0));
    func_0x00382ad8((int)((uRam00402eb2 + 0x40) * 0x10000) >> 0x10);
    func_0x00129d68(2);
    puVar4 = (ushort *)(sRam00402f16 * 2 + 0xc00532);
    if (*puVar4 < 0x32) {
      iVar1 = (uint)*puVar4 * 2 + 2;
    }
    else {
      iVar1 = 100;
    }
    func_0x00382b60(1,1,0x400890,*(undefined2 *)(iVar1 + 0x3e7f80));
    sRam003ed124 = uRam00402eb2 + 0x35;
    sRam003ed0e6 = uRam00402eb4 + 0x3e;
    sRam003ed106 = uRam00402eb4 + 0x1e;
    sRam003ed0c4 = uRam00402eb2 + 0x36;
    sRam003ed126 = uRam00402eb4 + 0x26;
    sRam003ed146 = uRam00402eb4 + 0x2e;
    sRam003ed0c6 = uRam00402eb4 + 0x36;
    sRam003ed0e4 = sRam003ed0c4;
    sRam003ed104 = sRam003ed0c4;
    sRam003ed144 = sRam003ed124;
    func_0x00383a90(1,0x3ed0f8);
    func_0x00383a90(1,0x3ed118);
    func_0x00383a90(1,0x3ed138);
    func_0x00383a90(1,0x3ed0b8);
    func_0x00383a90(1,0x3ed0d8);
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
                    // WARNING: Could not emulate address calculation at 0x0005ee54
                    // WARNING: Treating indirect jump as call
  (**(code **)((uint)bRam003f0900 * 4 + 0x4008e0))();
  return;
}



int FUN_00060570(int param_1,ushort *param_2,char param_3,uint param_4)

{
  ushort uVar1;
  int iVar2;
  uint uVar3;
  int iVar4;
  uint uVar5;
  int iVar6;
  
  uVar5 = 0;
  iVar4 = 0;
  iVar6 = 0;
  if (0 < param_3) {
    uVar1 = *param_2;
    while( true ) {
      iVar2 = (uint)*(ushort *)((param_1 + (uint)uVar1) * 2 + 0xc00006) * 2;
      uVar3 = *(ushort *)(iVar2 + 0xc00934) & 0xf;
      uVar3 = ((uint)*(ushort *)(iVar2 + 0xc036a0) +
              (uint)*(byte *)(uVar3 + (bRam00c084f2 & 0x1f) * 0xd + 0x3e8768) *
              (uint)*(ushort *)(iVar2 + 0xc00a8a)) *
              (uint)*(byte *)((uint)*(ushort *)((param_4 & 0xffff) * 2 + 0xc00934) + uVar3 * 0xd +
                             0x3e8978) >> 0xe;
      param_2 = param_2 + 1;
      if (uVar5 < uVar3) {
        iVar6 = iVar4;
        uVar5 = uVar3;
      }
      iVar4 = (iVar4 + 1) * 0x1000000 >> 0x18;
      if (param_3 <= iVar4) break;
      uVar1 = *param_2;
    }
  }
  return iVar6;
}



// WARNING: Removing unreachable block (ram,0x00060940)

ulong FUN_000606c8(void)

{
  uint uVar1;
  int iVar2;
  int iVar3;
  uint uVar4;
  uint uVar5;
  ulong uVar6;
  uint uVar7;
  uint uVar8;
  
  uVar7 = 0;
  uVar8 = 0;
  uVar1 = (uint)bRam00c084f2;
  if (uRam00c08500 != 0) {
    iVar2 = 0;
    do {
      if ((*(ushort *)(iVar2 + 0xc08518) & 3) == 3) {
        iVar3 = (uint)*(byte *)(iVar2 + 0xc08502) * 2;
        uVar4 = (uint)*(ushort *)(iVar3 + 0xc036a0) +
                ((int)((uint)*(byte *)((*(byte *)(iVar3 + 0xc00934) & 0xf) + (uVar1 & 0x1f) * 0xd +
                                      0x3e8768) * (uint)*(ushort *)(iVar3 + 0xc00a8a)) >> 7);
        *(undefined2 *)(iVar2 + 0x402f48) = (short)uVar4;
        uVar8 = uVar8 + (uVar4 & 0xffff);
      }
      else {
        *(undefined2 *)(iVar2 + 0x402f48) = 0;
      }
      uVar7 = uVar7 + 1 & 0xff;
      iVar2 = uVar7 << 1;
    } while (uVar7 < uRam00c08500);
  }
  uVar7 = 0;
  uVar4 = 0;
  if (uRam00c08540 != 0) {
    iVar2 = 0;
    do {
      if ((*(ushort *)(iVar2 + 0xc08558) & 3) == 3) {
        iVar3 = (uint)*(byte *)(iVar2 + 0xc08542) * 2;
        uVar5 = (uint)*(ushort *)(iVar3 + 0xc036a0) +
                ((int)((uint)*(byte *)((*(byte *)(iVar3 + 0xc00934) & 0xf) + (uVar1 & 0x1f) * 0xd +
                                      0x3e8768) * (uint)*(ushort *)(iVar3 + 0xc00a8a)) >> 7);
        *(undefined2 *)(iVar2 + 0x402f60) = (short)uVar5;
        uVar4 = uVar4 + (uVar5 & 0xffff);
      }
      else {
        *(undefined2 *)(iVar2 + 0x402f60) = 0;
      }
      uVar7 = uVar7 + 1 & 0xff;
      iVar2 = uVar7 << 1;
    } while (uVar7 < uRam00c08540);
  }
  uVar6 = 0xffffffffffffffff;
  if (uVar8 * 3 <= uVar4 * 10) {
    if (uVar4 * 10 < uVar8 << 3) {
      if (uVar4 == 0) {
        trap(7);
      }
      iVar2 = func_0x001881e8();
      uVar6 = 0xffffffffffffffff;
      if (0xa0U - (int)(uVar8 * 100) / (int)uVar4 <= (uint)((iVar2 * 100) / 0x7fffffff)) {
        uVar6 = 0;
      }
    }
    else {
      uVar6 = (ulong)(uVar4 < uVar8) ^ 1;
    }
  }
  return uVar6;
}



undefined2 FUN_00060980(void)

{
  short sVar1;
  int iVar2;
  int iVar3;
  int iVar4;
  int iVar5;
  int unaff_s2_lo;
  ushort *puVar6;
  int iVar7;
  ushort local_90 [16];
  undefined2 auStack112 [16];
  
  puVar6 = local_90;
  iVar2 = func_0x003e0d48(0x427e,local_90);
  func_0x003e0dd0(0x427e,local_90,iVar2,0x402f48);
  iVar3 = func_0x003e0d48(0x429e,auStack112);
  for (iVar7 = 0; iVar4 = unaff_s2_lo << 1, iVar7 < iVar2; iVar7 = (iVar7 + 1) * 0x1000000 >> 0x18)
  {
    sVar1 = *(short *)((uint)*puVar6 * 2 + 0xc08502);
    unaff_s2_lo = func_0x003e0e70(0x429e,auStack112,iVar3,sVar1);
    iVar4 = unaff_s2_lo << 1;
    if ((sVar1 == sRam00c084fe) || (iVar5 = unaff_s2_lo, iVar3 == 1)) break;
    for (; iVar5 < iVar3 + -1; iVar5 = (iVar5 + 1) * 0x1000000 >> 0x18) {
      auStack112[iVar5] = auStack112[iVar5 + 1];
    }
    iVar3 = (int)(char)(iVar3 + -1);
    puVar6 = puVar6 + 1;
  }
  return *(undefined2 *)((int)auStack112 + iVar4);
}



void FUN_00060b30(long param_1,undefined8 param_2,short param_3)

{
  ushort uVar1;
  int iVar2;
  long lVar3;
  ushort *puVar4;
  int iVar5;
  
  lVar3 = func_0x003e13d0();
  iVar5 = (int)lVar3;
  if (param_1 == 0x427e) {
    iVar2 = 0x3e2a38;
  }
  else {
    iVar2 = 0x3e2c80;
  }
  if (-1 < lVar3) {
    *(short *)(iVar5 * 0x3a + iVar2 + 2) = param_3;
    if ((param_3 == 4) || (param_3 == 0)) {
      puVar4 = (ushort *)(((int)param_1 + iVar5) * 2 + 0xc0001c);
      uVar1 = *puVar4 | 0x80;
    }
    else {
      puVar4 = (ushort *)(((int)param_1 + iVar5) * 2 + 0xc0001c);
      uVar1 = *puVar4 & 0xff7f;
    }
    *puVar4 = uVar1;
  }
  return;
}



undefined2 FUN_00060bf0(long param_1,undefined2 param_2)

{
  undefined2 uVar1;
  long lVar2;
  int iVar3;
  
  lVar2 = func_0x003e13d0(param_1,param_2);
  uVar1 = 0;
  if (param_1 == 0x427e) {
    iVar3 = 0x3e2a38;
  }
  else {
    iVar3 = 0x3e2c80;
  }
  if (-1 < lVar2) {
    uVar1 = *(undefined2 *)((int)lVar2 * 0x3a + iVar3 + 2);
  }
  return uVar1;
}



// WARNING: Control flow encountered bad instruction data

void FUN_000626d6(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_000725e6(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_000756de(void)

{
                    // WARNING: Treating indirect jump as call
  (*(code *)0x0)();
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_00075714(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0007576c(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00075a76(void)

{
  SYNC(0);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00076016(void)

{
  ulong in_v1;
  ulong unaff_s4;
  
  if (unaff_s4 <= in_v1) {
    trap(1);
  }
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0007669a(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



void FUN_0007757e(void)

{
                    // WARNING: Treating indirect jump as call
  (*(code *)0x0)();
  return;
}



// WARNING: Control flow encountered bad instruction data

void FUN_000780be(void)

{
  trap(0);
  trap(0x20c00);
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00078d16(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_000790a2(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00079946(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_00079e9a(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data

void FUN_0007a416(void)

{
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}



// WARNING: Control flow encountered bad instruction data
// WARNING: Globals starting with '_' overlap smaller symbols at the same address

void FUN_0007a5ee(int param_1,undefined8 param_2,int param_3,undefined4 *param_4,undefined8 param_5,
                 undefined8 param_6,int param_7,undefined param_8,undefined param_9,
                 undefined param_10,undefined4 param_11)

{
  byte bVar1;
  byte bVar2;
  short sVar3;
  undefined4 uVar4;
  undefined8 in_zero;
  undefined4 uVar5;
  int iVar6;
  int iVar7;
  undefined4 in_zero_udw;
  undefined4 in_register_0000000c;
  uint uVar8;
  undefined4 in_t0_udw;
  undefined4 in_register_0000008c;
  int in_t7_lo;
  undefined8 unaff_s0;
  undefined4 in_s0_udw;
  undefined4 in_register_0000010c;
  int unaff_s2_lo;
  int unaff_s3_lo;
  int unaff_s4_lo;
  int unaff_s5_lo;
  undefined4 in_t8_udw;
  undefined4 in_register_0000018c;
  int in_k0_lo;
  int unaff_retaddr;
  
  *param_4 = (int)unaff_s0;
  param_4[1] = (int)((ulong)unaff_s0 >> 0x20);
  param_4[2] = in_s0_udw;
  param_4[3] = in_register_0000010c;
  *(int *)(param_7 + 0x3b) = (int)param_5;
  *(int *)(param_7 + 0x3f) = (int)((ulong)param_5 >> 0x20);
  *(undefined4 *)(param_7 + 0x43) = in_t0_udw;
  *(undefined4 *)(param_7 + 0x47) = in_register_0000008c;
  uVar4 = (undefined4)in_zero;
  *(undefined4 *)(unaff_s2_lo + 0x3b) = uVar4;
  uVar5 = (undefined4)((ulong)in_zero >> 0x20);
  *(undefined4 *)(unaff_s2_lo + 0x3f) = uVar5;
  *(undefined4 *)(unaff_s2_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s2_lo + 0x47) = in_register_0000000c;
  *(undefined4 *)(unaff_s2_lo + 0x3b) = uVar4;
  *(undefined4 *)(unaff_s2_lo + 0x3f) = uVar5;
  *(undefined4 *)(unaff_s2_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s2_lo + 0x47) = in_register_0000000c;
  *(undefined4 *)(unaff_s2_lo + 0x3b) = uVar4;
  *(undefined4 *)(unaff_s2_lo + 0x3f) = uVar5;
  *(undefined4 *)(unaff_s2_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s2_lo + 0x47) = in_register_0000000c;
  sVar3 = *(short *)(param_1 + 0x3b);
  iVar6 = (int)(sVar3 >> 0xf);
  *(int *)(unaff_s2_lo + 0x3b) = (int)sVar3;
  *(int *)(unaff_s2_lo + 0x3f) = iVar6;
  *(undefined4 *)(unaff_s2_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s2_lo + 0x47) = in_register_0000000c;
  *(int *)(unaff_s2_lo + 0x3b) = (int)sVar3;
  *(int *)(unaff_s2_lo + 0x3f) = iVar6;
  *(undefined4 *)(unaff_s2_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s2_lo + 0x47) = in_register_0000000c;
  *(int *)(unaff_s2_lo + 0x3b) = (int)sVar3;
  *(int *)(unaff_s2_lo + 0x3f) = iVar6;
  *(undefined4 *)(unaff_s2_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s2_lo + 0x47) = in_register_0000000c;
  iRam0007a629 = *(int *)(in_t7_lo + 0x3b);
  iVar6 = *(int *)(unaff_retaddr + 0x3b);
  bVar1 = *(byte *)(param_3 + 0x3b);
  bVar2 = *(byte *)(unaff_s4_lo + 0x3b);
  *(uint *)(unaff_s4_lo + 0x3b) = (uint)bVar1;
  *(undefined4 *)(unaff_s4_lo + 0x3f) = 0;
  *(undefined4 *)(unaff_s4_lo + 0x43) = in_t0_udw;
  *(undefined4 *)(unaff_s4_lo + 0x47) = in_register_0000008c;
  *(int *)(unaff_s4_lo + 0x3b) = iVar6;
  iVar7 = iVar6 >> 0x1f;
  *(int *)(unaff_s4_lo + 0x3f) = iVar7;
  *(undefined4 *)(unaff_s4_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s4_lo + 0x47) = in_register_0000000c;
  *(int *)(unaff_s3_lo + 0x3b) = iRam0007a629;
  iRam0007a62d = iRam0007a629 >> 0x1f;
  *(int *)(unaff_s3_lo + 0x3f) = iRam0007a62d;
  *(undefined4 *)(unaff_s3_lo + 0x43) = in_t8_udw;
  *(undefined4 *)(unaff_s3_lo + 0x47) = in_register_0000018c;
  *(int *)(unaff_s5_lo + 0x3b) = iVar6;
  *(int *)(unaff_s5_lo + 0x3f) = iVar7;
  *(undefined4 *)(unaff_s5_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s5_lo + 0x47) = in_register_0000000c;
  *(int *)(unaff_s5_lo + 0x3b) = iVar6;
  *(int *)(unaff_s5_lo + 0x3f) = iVar7;
  *(undefined4 *)(unaff_s5_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s5_lo + 0x47) = in_register_0000000c;
  *(int *)(unaff_s5_lo + 0x3b) = iVar6;
  *(int *)(unaff_s5_lo + 0x3f) = iVar7;
  *(undefined4 *)(unaff_s5_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s5_lo + 0x47) = in_register_0000000c;
  *(int *)(unaff_s5_lo + 0x3b) = iVar6;
  *(int *)(unaff_s5_lo + 0x3f) = iVar7;
  *(undefined4 *)(unaff_s5_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s5_lo + 0x47) = in_register_0000000c;
  *(int *)(unaff_s5_lo + 0x3b) = iVar6;
  *(int *)(unaff_s5_lo + 0x3f) = iVar7;
  *(undefined4 *)(unaff_s5_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s5_lo + 0x47) = in_register_0000000c;
  *(int *)(unaff_s5_lo + 0x3b) = iVar6;
  *(int *)(unaff_s5_lo + 0x3f) = iVar7;
  *(undefined4 *)(unaff_s5_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s5_lo + 0x47) = in_register_0000000c;
  *(int *)(unaff_s5_lo + 0x3b) = iVar6;
  *(int *)(unaff_s5_lo + 0x3f) = iVar7;
  *(undefined4 *)(unaff_s5_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s5_lo + 0x47) = in_register_0000000c;
  *(int *)(unaff_s5_lo + 0x3b) = iVar6;
  *(int *)(unaff_s5_lo + 0x3f) = iVar7;
  *(undefined4 *)(unaff_s5_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s5_lo + 0x47) = in_register_0000000c;
  *(int *)(unaff_s5_lo + 0x3b) = iVar6;
  *(int *)(unaff_s5_lo + 0x3f) = iVar7;
  *(undefined4 *)(unaff_s5_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s5_lo + 0x47) = in_register_0000000c;
  *(int *)(unaff_s5_lo + 0x3b) = iVar6;
  *(int *)(unaff_s5_lo + 0x3f) = iVar7;
  *(undefined4 *)(unaff_s5_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s5_lo + 0x47) = in_register_0000000c;
  *(int *)(unaff_s5_lo + 0x3b) = iVar6;
  *(int *)(unaff_s5_lo + 0x3f) = iVar7;
  *(undefined4 *)(unaff_s5_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s5_lo + 0x47) = in_register_0000000c;
  _FUN_0007a5ee = (uint)bVar2;
  *(uint *)(unaff_s4_lo + 0x3b) = _FUN_0007a5ee;
  *(undefined4 *)(unaff_s4_lo + 0x3f) = 0;
  *(undefined4 *)(unaff_s4_lo + 0x43) = in_s0_udw;
  *(undefined4 *)(unaff_s4_lo + 0x47) = in_register_0000010c;
  *(int *)(unaff_s4_lo + 0x3b) = iRam0007a629;
  *(int *)(unaff_s4_lo + 0x3f) = iRam0007a62d;
  *(undefined4 *)(unaff_s4_lo + 0x43) = in_t8_udw;
  *(undefined4 *)(unaff_s4_lo + 0x47) = in_register_0000018c;
  *(int *)(unaff_s5_lo + 0x3b) = iVar6;
  *(int *)(unaff_s5_lo + 0x3f) = iVar7;
  *(undefined4 *)(unaff_s5_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(unaff_s5_lo + 0x47) = in_register_0000000c;
                    // WARNING: Read-only address (ram,0x0007a5f6) is written
                    // WARNING: Read-only address (ram,0x0007a5fa) is written
  uRam0007a5f2 = 0;
                    // WARNING: Read-only address (ram,0x0007a629) is written
                    // WARNING: Read-only address (ram,0x0007a62d) is written
                    // WARNING: Read-only address (ram,0x0007a631) is written
                    // WARNING: Read-only address (ram,0x0007a635) is written
                    // WARNING: Read-only address (ram,0x0007a629) is written
                    // WARNING: Read-only address (ram,0x0007a62d) is written
                    // WARNING: Read-only address (ram,0x0007a631) is written
                    // WARNING: Read-only address (ram,0x0007a635) is written
  uVar8 = (uint)bVar1;
  uRam0007a5f6 = in_s0_udw;
  uRam0007a5fa = in_register_0000010c;
  *(uint *)(in_k0_lo + 0x3b) = uVar8;
  *(undefined4 *)(in_k0_lo + 0x3f) = 0;
  *(undefined4 *)(in_k0_lo + 0x43) = in_t0_udw;
  *(undefined4 *)(in_k0_lo + 0x47) = in_register_0000008c;
  *(uint *)(in_k0_lo + 0x3b) = uVar8;
  *(undefined4 *)(in_k0_lo + 0x3f) = 0;
  *(undefined4 *)(in_k0_lo + 0x43) = in_t0_udw;
  *(undefined4 *)(in_k0_lo + 0x47) = in_register_0000008c;
  *(uint *)(in_k0_lo + 0x3b) = uVar8;
  *(undefined4 *)(in_k0_lo + 0x3f) = 0;
  *(undefined4 *)(in_k0_lo + 0x43) = in_t0_udw;
  *(undefined4 *)(in_k0_lo + 0x47) = in_register_0000008c;
  *(uint *)(in_k0_lo + 0x3b) = uVar8;
  *(undefined4 *)(in_k0_lo + 0x3f) = 0;
  *(undefined4 *)(in_k0_lo + 0x43) = in_t0_udw;
  *(undefined4 *)(in_k0_lo + 0x47) = in_register_0000008c;
  *(uint *)(in_k0_lo + 0x3b) = uVar8;
  *(undefined4 *)(in_k0_lo + 0x3f) = 0;
  *(undefined4 *)(in_k0_lo + 0x43) = in_t0_udw;
  *(undefined4 *)(in_k0_lo + 0x47) = in_register_0000008c;
  *(uint *)(in_k0_lo + 0x3b) = uVar8;
  *(undefined4 *)(in_k0_lo + 0x3f) = 0;
  *(undefined4 *)(in_k0_lo + 0x43) = in_t0_udw;
  *(undefined4 *)(in_k0_lo + 0x47) = in_register_0000008c;
  *(uint *)(in_k0_lo + 0x3b) = uVar8;
  *(undefined4 *)(in_k0_lo + 0x3f) = 0;
  *(undefined4 *)(in_k0_lo + 0x43) = in_t0_udw;
  *(undefined4 *)(in_k0_lo + 0x47) = in_register_0000008c;
  *(uint *)(in_k0_lo + 0x3b) = uVar8;
  *(undefined4 *)(in_k0_lo + 0x3f) = 0;
  *(undefined4 *)(in_k0_lo + 0x43) = in_t0_udw;
  *(undefined4 *)(in_k0_lo + 0x47) = in_register_0000008c;
  uVar8 = (uint)bVar1;
  *(uint *)(in_k0_lo + 0x3b) = uVar8;
  *(undefined4 *)(in_k0_lo + 0x3f) = 0;
  *(undefined4 *)(in_k0_lo + 0x43) = in_t0_udw;
  *(undefined4 *)(in_k0_lo + 0x47) = in_register_0000008c;
  *(uint *)(in_k0_lo + 0x3b) = uVar8;
  *(undefined4 *)(in_k0_lo + 0x3f) = 0;
  *(undefined4 *)(in_k0_lo + 0x43) = in_t0_udw;
  *(undefined4 *)(in_k0_lo + 0x47) = in_register_0000008c;
  *(uint *)(in_k0_lo + 0x3b) = uVar8;
  *(undefined4 *)(in_k0_lo + 0x3f) = 0;
  *(undefined4 *)(in_k0_lo + 0x43) = in_t0_udw;
  *(undefined4 *)(in_k0_lo + 0x47) = in_register_0000008c;
                    // WARNING: Read-only address (ram,0x0007a629) is written
                    // WARNING: Read-only address (ram,0x0007a62d) is written
                    // WARNING: Read-only address (ram,0x0007a631) is written
                    // WARNING: Read-only address (ram,0x0007a635) is written
  uRam0007a631 = in_t8_udw;
  uRam0007a635 = in_register_0000018c;
  *(undefined4 *)(in_k0_lo + 0x3b) = 0;
  *(undefined4 *)(in_k0_lo + 0x3f) = 0;
  *(undefined4 *)(in_k0_lo + 0x43) = in_zero_udw;
  *(undefined4 *)(in_k0_lo + 0x47) = in_register_0000000c;
  *(uint *)(in_k0_lo + 0x3b) = uVar8;
  *(undefined4 *)(in_k0_lo + 0x3f) = 0;
  *(undefined4 *)(in_k0_lo + 0x43) = in_t0_udw;
  *(undefined4 *)(in_k0_lo + 0x47) = in_register_0000008c;
                    // WARNING: Bad instruction - Truncating control flow here
  halt_baddata();
}


