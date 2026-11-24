"""What the profiler's bus buckets (F8) must agree with: the pinned Mednafen's own address decode.

BUS_RANGES is the expected classification of every SH-2 external-bus address (27 bits), written from
the decoder, not from apply.py's table: ss.cpp's CS0 decode (BusRW_DB_CS0) and scu.inc's
SCU_FromSH2_BusRW_DB / ABus_Read / BBusRW_DB. Each entry is (low, high, bucket); a bucket is a
SE_PERF_DEV_* name, or "X:<SE_PERF_XDEV_* name>" for the buckets that live in SE_PERF_BUS_ACCESS_X.
SCU_DECODER_WINDOWS are the `if(A >= lo && A <= hi)` conditions in scu.inc those ranges rest on;
check_component.py proves each is still literally there in the checkout, so a Mednafen bump that moves
a window fails the check instead of silently mis-attributing traffic.
Used by Integration/tests/check_apply_hooks.py (the table, compiled) and check_component.py (the text)."""

BUS_RANGES = [
    (0x00000000, 0x000FFFFF, "SE_PERF_DEV_BIOS"),
    (0x00100000, 0x0017FFFF, "SE_PERF_DEV_SMPC"),
    (0x00180000, 0x001FFFFF, "SE_PERF_DEV_BACKUP"),
    (0x00200000, 0x003FFFFF, "SE_PERF_DEV_WRAM_L"),
    (0x00400000, 0x01FFFFFF, "SE_PERF_DEV_CS0_OTHER"),
    (0x02000000, 0x03FFFFFF, "SE_PERF_DEV_ABUS_CS0"),
    (0x04000000, 0x04FFFFFF, "SE_PERF_DEV_ABUS_CS1"),
    (0x05000000, 0x057FFFFF, "X:SE_PERF_XDEV_ABUS_DUMMY"),    # "A-bus Dummy": answered, no device
    (0x05800000, 0x058FFFFF, "SE_PERF_DEV_ABUS_CD"),          # "A-Bus CS2"
    (0x05900000, 0x059FFFFF, "X:SE_PERF_XDEV_UNMAPPED"),      # falls through every SCU window
    (0x05A00000, 0x05BFFFFF, "SE_PERF_DEV_SCSP"),
    (0x05C00000, 0x05D7FFFF, "SE_PERF_DEV_VDP1"),             # VRAM, frame buffer, registers
    (0x05D80000, 0x05DFFFFF, "X:SE_PERF_XDEV_UNMAPPED"),
    (0x05E00000, 0x05FBFFFF, "SE_PERF_DEV_VDP2"),             # VRAM, CRAM, registers
    (0x05FC0000, 0x05FDFFFF, "X:SE_PERF_XDEV_UNMAPPED"),
    (0x05FE0000, 0x05FEFFFF, "SE_PERF_DEV_SCU"),
    (0x05FF0000, 0x05FFFFFF, "X:SE_PERF_XDEV_UNMAPPED"),      # SCU debug/test area: not decoded
    (0x06000000, 0x07FFFFFF, "SE_PERF_DEV_WRAM_H"),
]

SCU_DECODER_WINDOWS = [
    (0x05C00000, 0x05D7FFFF),   # VDP1
    (0x05E00000, 0x05FBFFFF),   # VDP2
    (0x05A00000, 0x05BFFFFF),   # SCSP
    (0x05000000, 0x057FFFFF),   # A-bus dummy
    (0x05800000, 0x058FFFFF),   # A-bus CS2
    (0x02000000, 0x04FFFFFF),   # A-bus CS0 + CS1
    (0x05FE0000, 0x05FEFFFF),   # SCU registers
]
