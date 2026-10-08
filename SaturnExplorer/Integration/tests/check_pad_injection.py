"""The Mednafen patch's controller injection must press the intended button on each pad type.

The Digital Control Pad and the 3D Control Pad lay their input buffers out differently
(input/gamepad.cpp vs input/3dpad.cpp: UP is bit 4 on one and bit 0 on the other, bit 4 is B on the
3D pad, and its analog shoulders sit RIGHT first at bytes 6..9). The injected C++ never compiles in
this repo's build, so this extracts the SMPC injection block from apply.py, compiles it against stub
Mednafen types, drives it with each SE_PAD_* button on each device, and checks the buffer the device
would receive. Usage: check_pad_injection.py <apply.py> <scratch dir> [c++ compiler]"""
import importlib.util
import os
import subprocess
import sys

apply_py, work = sys.argv[1], sys.argv[2]
spec = importlib.util.spec_from_file_location("se_apply", apply_py)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
block = mod.SMPC_INPUT_STATE

HARNESS = r'''
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
typedef uint8_t uint8; typedef uint16_t uint16; typedef int32_t int32; typedef uint32_t uint32;
typedef int8_t int8;
struct IODevice { uint8 got[10]; virtual void UpdateInput(const uint8* d, const int32) { memcpy(got, d, 10); } };
struct Devs { IODevice none, gamepad, threedpad, mouse, wheel, mission, dualmission, gun, keyboard, jpkeyboard; };
static Devs PossibleDevices[2];
static IODevice* VirtualPorts[2];
static uint8* VirtualPortsDPtr[2];
extern "C" int SsDbgQueryKeyMap(unsigned, int out[13]) { for (int i = 0; i < 13; i++) out[i] = -1; return 0; }
extern "C" void SeExportLog(const char*) {}
#include "block.inc"

static int bad = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); bad++; } } while (0)

int main()
{
 /* SE_PAD_*: up down left right a b c x y z l r start */
 const uint16 se[13] = { 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
                         0x0100, 0x0200, 0x0400, 0x0800, 0x1000 };
 const char* nm[13] = { "UP","DOWN","LEFT","RIGHT","A","B","C","X","Y","Z","L","R","START" };
 /* Expected data-buffer bit from each device's IDII entry order; -1 = analog / absent. */
 const int pad[13] = { 4,5,6,7, 10,8,9, 2,1,0, 15,3, 11 };
 const int d3[13]  = { 0,1,2,3, 6,4,5, 10,9,8, -1,-1, 7 };
 for (int dev = 0; dev < 2; dev++)
 {
  VirtualPorts[0] = dev ? &PossibleDevices[0].threedpad : &PossibleDevices[0].gamepad;
  for (int b = 0; b < 13; b++)
  {
   uint8 host[10]; memset(host, 0, sizeof(host));
   VirtualPortsDPtr[0] = host;
   SMPC_SetInjectedInput(0, se[b]);
   memset(PossibleDevices[0].gamepad.got, 0, 10); memset(PossibleDevices[0].threedpad.got, 0, 10);
   SeSMPCUpdateInput(0, 0);
   const uint8* g = VirtualPorts[0]->got;
   const uint16 bits = (uint16)(g[0] | (g[1] << 8));
   if (!dev)
   {
    CHECK(bits == (uint16)(1u << pad[b]), "digital pad %s -> 0x%04X, want bit %d", nm[b], bits, pad[b]);
   }
   else if (d3[b] >= 0)
   {
    CHECK(bits == (uint16)(1u << d3[b]), "3D pad %s -> 0x%04X, want bit %d", nm[b], bits, d3[b]);
    CHECK(!g[6] && !g[7] && !g[8] && !g[9], "3D pad %s drove an analog shoulder", nm[b]);
   }
   else
   {
    CHECK(bits == 0, "3D pad %s set digital bits 0x%04X", nm[b], bits);
    const bool right = (se[b] == 0x0800);
    CHECK(g[right ? 6 : 8] == 0xFF && g[right ? 7 : 9] == 0xFF, "3D pad %s: wrong shoulder bytes", nm[b]);
    CHECK(!g[right ? 8 : 6] && !g[right ? 9 : 7], "3D pad %s drove the other shoulder", nm[b]);
   }
  }
  /* Released: nothing injected, host passes through untouched. */
  uint8 host[10] = { 0x21, 0x10, 1,2,3,4,5,6,7,8 };
  VirtualPortsDPtr[0] = host;
  SMPC_SetInjectedInput(0, 0);
  SeSMPCUpdateInput(0, 0);
  CHECK(memcmp(VirtualPorts[0]->got, host, 10) == 0, "device %d: release altered the host buffer", dev);
  /* Held host input survives an injection, and the 3D pad's mode bit is left alone. */
  SMPC_SetInjectedInput(0, 0x0010);   /* A */
  SeSMPCUpdateInput(0, 0);
  const uint16 want = (uint16)(0x1021u | (1u << (dev ? 6 : 10)));
  const uint8* g = VirtualPorts[0]->got;
  CHECK((uint16)(g[0] | (g[1] << 8)) == want, "device %d: merge with host input gave 0x%04X, want 0x%04X",
        dev, (unsigned)(g[0] | (g[1] << 8)), want);
 }
 return bad ? 1 : 0;
}
'''
os.makedirs(work, exist_ok=True)
open(os.path.join(work, "block.inc"), "w").write(block)
src = os.path.join(work, "harness.cpp")
open(src, "w").write(HARNESS)
exe = os.path.join(work, "harness")
cc = subprocess.run([sys.argv[3] if len(sys.argv) > 3 else "g++", "-std=c++17", "-Wall", "-I", work, src, "-o", exe],
                    capture_output=True, text=True)
if cc.returncode:
    print(cc.stderr)
    sys.exit(2)
run = subprocess.run([exe], capture_output=True, text=True)
print(run.stdout, end="")
sys.exit(run.returncode)
