# Live protocol additions for the bounded experiment runner (PLAN.md §9.1)

Status: **specification only**. Nothing here is implemented. `Integration/Common/SeLiveProtocol.h`,
`se_export.c`, `Mednafen/apply.py` and `Drivers/Live` are unchanged by M0; this document is what M4
implements. Baseline: protocol **v25** at commit `2f94527` plus the M0 commits.

The runner (PLAN.md §9.2) installs its own breakpoints and tracepoints, resumes the emulator, and later
must be able to say *which of its instruments caused what* and *whether anything was lost*. v25 cannot
say either. This document lists exactly what is missing, and defines the smallest additive wire change
that supplies it, version-gated so that nothing that works today changes.

## 1. What v25 does today (verified)

| Claim in PLAN.md | Evidence in the tree | Verified |
|---|---|---|
| No per-install acknowledgement | `BKP`/`TRC` are handled at `se_export.c:1955-1974` and `2071-2083`: the payload is received, `SePublishBreakpoints` / `SePublishTracepoints` (`:980-996`) copy it into a one-slot mailbox, and the request is answered like any other, with a snapshot. Nothing in the reply says whether, or when, the set was installed. The emulate thread installs it later, in `SeApplyPendingInstalls` (`:1099-1142`), at a frame gate, the snapshot, or the halt gate. A set not yet taken when the next arrives is **replaced**. | yes |
| Installs are applied on the emulate thread | `se_export.c:959-978` (the mailbox comment) and `:1099` | yes |
| The stop report has no breakpoint identity | The stop is one packed 64-bit word, `seq<<36 \| reason<<33 \| cpu<<32 \| pc` (`se_export.c:229-238`); the control block carries reason, CPU, PC and `stop_seq` (`:2128-2136`). `SeLiveProtocol.h` says a watchpoint hit by an SH-2 instruction is reported as `SE_LIVE_STOP_EXEC_BP` with the accessor's PC, and an SCU-DMA hit as `SE_LIVE_STOP_DMA_WATCH` with the PC where the CPU stood. | yes |
| The emulator cannot say which descriptor matched | The Mednafen glue's `SeSsBpHook(uint32 PC, bool bpoint)` (`apply.py:264`) receives only the PC and one boolean. Installs lose identity: `SsDbgAddExecBp` and `SsDbgAddMemBp` (`apply.py:339-360`) add address ranges to Mednafen's debugger, ignore the `cpu` field, and add every data watchpoint in both the cached and cache-through images. | yes |
| Dropped tracepoint events are counted but never sent | `sEvDropped` is declared at `se_export.c:323` ("reported once") and incremented at `:1468`. It is read nowhere else. | yes |
| Last-client disconnect drops all breakpoints and tracepoints and resumes | `SeOnClientDisconnect` (`se_export.c:917-944`): publishes empty sets, clears queued events (`sEvHead = sEvCount = 0`), cancels steps, clears the stop, clears `sPaused`. It runs when `sClients` reaches 0 (`:1881`). | yes |
| The client closes with RUN | `LiveDriver.cpp:1643-1666`: when `pausedByUs`, a best-effort `RUN` before the socket closes. | yes |
| Server poke counters reset only at server start, client copies per connection | `se_export.c:2568` (`sPokesApplied = sPokesDropped = 0` in `SeExportInit`); `LiveDriver.cpp:1313-1314` zeroes the cached copies in `ForgetConnection`. | yes |
| Several clients may be connected | `sClients` is a counter (`se_export.c:901, 1870, 1881`); the comments on the BKP/TRC buffers say "two server threads can be serving at once". The mailboxes and the event ring are shared, not per client. | yes |

Two further facts the design depends on, found while matching the plan to the code:

1. **The control block is capped at 64 bytes by the client.** `ReadSnapshot` rejects a reply whose
   control section length exceeds 64 (`LiveDriver.cpp:969-972`, `ct > 64u`). v25's block is 56
   bytes. The three fields below plus the epoch make 72, so the cap must be raised in the same
   change. The header already carries the length (`SeLiveProtocol.h`: header +40, `SE_CT`), and the
   client already reads fields by `ct >=` offset, so growth is the established pattern.
2. **A newer server is refused, an older one is not.** The client returns false on
   `version > SE_LIVE_VERSION` (`LiveDriver.cpp:945-948`) and accepts any server from v3 up. So a v25
   client never talks to a v26 server, and a v26 client can talk to a v25 server; the new verbs must be
   gated on the *server's* version, exactly as `MinVerFor` (`LiveDriver.cpp:214-224`) already does for
   `TRC`, `WRS`, `LST`, `REW` and `ESV`.

Also noted, not a gap: the header comment on `SE_LIVE_EVENTS_MAX` says excess events are "dropped with a
marker". They are not: the reply carries `min(count, 64)` events and the rest stay queued
(`se_export.c:2173-2200`). The only server-side loss is ring overflow (`SE_EVQ_CAP` 128, `:1466-1471`).

## 2. Version gate

```
SE_LIVE_VERSION            26u
SE_LIVE_MINVER_INSTALL_GEN 26u    /* BK2, TR2, the v26 control fields, stop detail and event stamping */
```

New capability bits (control block `caps`, +52). A bit says the **build** really implements the feature,
the same discipline as `SE_LIVE_CAP_EXEC_BP` and friends, so a server compiled with stub hooks does not
claim it:

```
SE_LIVE_CAP_INSTALL_ACK  256u   /* applied_gen fields are meaningful: the install hooks ran */
SE_LIVE_CAP_STOP_DETAIL  512u   /* the stop detail section can name what a halt matched */
SE_LIVE_CAP_EVENT_STAMP 1024u   /* trace events carry the installed generation and the run epoch */
```

**Unattended Run is enabled only when** the negotiated version is >= 26 **and** all three bits are set
**and** `SE_LIVE_CAP_EXEC_BP`, `SE_LIVE_CAP_TRACEPOINTS` (and `SE_LIVE_CAP_MEM_WATCH` for a watchpoint
experiment) are set. Anything less: setup can still be imported and applied by hand (PLAN.md §9.1).

Client changes this implies, all in `Drivers/Live`:

* `MinVerFor` gains `BK2` and `TR2` (v26). A request below the floor is refused before it is sent, so an
  old server never sees a payload verb it would treat as `GET` without draining it (the desync failure
  the table exists to prevent).
* The control-length clamp becomes `ct > 128u` (room for later growth), and `ReadSnapshot` reads the new
  fields behind `ct >= 72`.
* A v26 client that finds a v25 server uses `BKP`/`TRC` as today and reports installs as *unconfirmed*.

## 3. Control block: v26 layout (72 bytes)

Little-endian u32 unless noted. Offsets 0-55 are v25's, unchanged; verified against
`se_export.c:2110-2150` and `LiveDriver.cpp:1189-1231`.

| Offset | Size | Field | Since |
|---|---|---|---|
| +0 | 4 | `paused` | v3 |
| +4 | 8 | `frame` (u64) | v3 |
| +12 | 4 | stop `reason` (`SE_LIVE_STOP_*`) | v5 |
| +16 | 4 | stop `cpu` | v5 |
| +20 | 4 | stop `pc` | v5 |
| +24 | 4 | `restore_done` | v19 |
| +28 | 4 | `restore_failed` | v19 |
| +32 | 4 | `latest_frame` | v20 |
| +36 | 4 | `step_pending` | v20 |
| +40 | 4 | `stop_seq` (28 bits, wrapping) | v21 |
| +44 | 4 | `pokes_applied` | v23 |
| +48 | 4 | `pokes_dropped` | v23 |
| +52 | 4 | `caps` | v23 |
| **+56** | 4 | **`bkp_applied_gen`**: generation of the breakpoint set the emulate thread most recently installed; 0 when none has been since the server started or since the last all-clients-left clear | v26 |
| **+60** | 4 | **`trc_applied_gen`**: the same for tracepoints | v26 |
| **+64** | 4 | **`events_dropped`**: trace events the server dropped because its ring was full; monotonic for the server run, wraps at 2^32 | v26 |
| **+68** | 4 | **`run_epoch`**: a non-zero value chosen at `SeExportInit` and constant until the server process stops; a different value means a different server run | v26 |

`SE_LIVE_CONTROL_LEN` becomes 72. A v26 client reads +56..+71 only when `ct >= 72`.

### 3.1 `run_epoch`

`SeExportInit` picks a non-zero 32-bit value from the best entropy the platform offers (a
time-and-address mix is enough; it is a *distinguisher*, not a secret). It never changes while the server
runs, across client connects and disconnects. Its job is to catch the case no counter can: the emulator
was restarted and counters returned to values that look like progress. A client that sees `run_epoch`
differ from the one it recorded treats **every** earlier generation, counter and event as belonging to
another run (the existing `connGeneration` reasoning in `LiveDriver`, applied to the server rather than
the socket).

### 3.2 `events_dropped`

Incremented in `SeExportQueueTraceEvent` at the existing overflow branch (`se_export.c:1466-1471`), under
the lock already held there, and **never reset** during a run. It replaces the unread `sEvDropped`.
It counts only server-side ring losses. The per-reply cap of 64 loses nothing (the rest stay queued),
and a disconnect clearing the ring (`:933`) is not a drop: nobody was left to deliver to.

Client-side losses are the client's to count and are not on the wire. `LiveDriver` today discards the
oldest events once its own queue passes 4096 (`LiveDriver.cpp:1617`) and clears the queue when a
connection ends (`:1554`); v26 adds a monotonic `eventsLostClient` beside it, exposed through the driver
API. The runner reports `events_dropped` delta and `eventsLostClient` delta over the run interval, and
**either being non-zero downgrades the run's event evidence from complete to incomplete** (it does not
fail the run: the evidence says so).

## 4. Verbs: `BK2` and `TR2`

`BKP` and `TRC` are unchanged and remain accepted. They install a set exactly as today and stamp it
generation 0, which the runner can never confirm (so a v26 client uses them only against a v25 server).

New verbs, 4 bytes each like all others:

```
#define SE_LIVE_VERB_BKPTS2 "BK2\n"
#define SE_LIVE_VERB_TRACE2 "TR2\n"
```

Request: the usual 8-byte frame (`verb` + `arg`), then a payload.

```
arg      = descriptor count N          (<= SE_LIVE_MAX_BKPT_DESCS = 1024 for BK2, <= SE_LIVE_MAX_TRACE_DESCS = 256 for TR2)
payload  = generation (u32 LE)         chosen by the sender; non-zero
           reserved   (u32 LE)         must be 0; keeps the descriptors 8-byte aligned and leaves room
           N descriptors               exactly the existing 12-byte BKP / 16-byte TRC descriptors
```

Server behaviour, in `se_export.c`:

1. Over-limit counts are handled as today (`:1963-1968`, `:2074-2079`): the client is dropped, because
   `N * len` can wrap in 32 bits and a drain would misalign the stream.
2. `generation == 0` in a BK2/TR2 is malformed: the set is installed (the sender is not punished) but
   stamped 0, and the server logs it (`SeExportLog`).
3. `SePublishBreakpoints(descs, count)` gains a `generation` argument and stores it with the box
   (`sBpBoxGen`). A set replaced before the emulate thread takes it is **never acknowledged**; only the
   set that is taken is.
4. In `SeApplyPendingInstalls`, after `sClearBps()` and the loop (breakpoints), or after
   `sSetTracepoints(n, ...)` (tracepoints), the emulate thread stores the taken box's generation into
   `sBkpAppliedGen` / `sTrcAppliedGen` **under the state lock**, so the control block's snapshot of them
   and the installed tables agree. The hooks have returned by then: the ack means *the install hooks
   ran*, which `SE_LIVE_CAP_INSTALL_ACK` promises the build can only claim if they are real.
5. `SeOnClientDisconnect` publishes empty sets with generation 0, so after the clear is applied both
   `*_applied_gen` read 0. The run epoch does not change.

Client behaviour, in `LiveDriver`:

* The wire generation is assigned **when a set is sent**, by the poll thread, from a per-connection
  counter starting at 1 (the sets are latest-wins: `bkptsDirty` coalesces, `LiveDriver.cpp:1403-1415`, so
  a generation is never burned on a set that is superseded before it ships). The driver records
  `wireGeneration -> {which store generations it carried, descriptor list}` for the runner.
* A set is **installed** once a reply arrives whose `*_applied_gen` equals it, and the reply's
  `run_epoch` is the one recorded when the connection began. Until then it is *pending*. If a later
  reply shows a *different* non-zero generation than any this connection sent, another client replaced
  the set: the install state is *unknown* and the runner refuses to run (§6).
* Acknowledgements may arrive late, out of order or not at all; the driver reports the latest applied
  generation it has seen and never infers an install from elapsed time.

**Why a client-chosen generation and not a server-assigned one:** the plan (§9.1 item 1) has the
client stamp what it sends, which lets the runner prove "the set I built is the set that is running"
without a round trip to learn a number. The cost is that two senders can collide; §6 makes that a
refusal, not a race.

## 5. Stop detail section and trace event layout

### 5.1 Stop detail (new trailing section)

Appended after the v17 emulator-slot inventory, the last trailing block today. Read by a client only
when the server reports version >= 26 (the same rule every trailing section follows).

```
u32 length                 0 when no halt is being reported, or the build cannot say
                           (SE_LIVE_CAP_STOP_DETAIL clear). Otherwise 20 + bitmap bytes, rounded up to a multiple of 4.
u32 stop_seq               the stop this describes; equals control +40 in the same reply
u32 bkp_gen                bkp_applied_gen at the moment the halt was evaluated
u32 match_state            SE_LIVE_MATCH_*: see below
u32 descriptor_count       n: the descriptors in that installed set (<= 1024)
u8  matched[ceil(n/8)]     bit i of byte i/8 (LSB first) = descriptor i of the installed set, in BK2 order
```

`SE_LIVE_MATCH_NONE 0` (no breakpoint was involved: a user pause or an instruction step; `matched` empty),
`SE_LIVE_MATCH_EXACT 1`, `SE_LIVE_MATCH_AMBIGUOUS 2`, `SE_LIVE_MATCH_UNKNOWN 3`.

The detail is latched **with the stop, under the same sequence number**, and sent only if its `stop_seq`
equals the stop being reported. This is the rule the control block already applies to the stop word
(`se_export.c:2114-2127`: a stop is shown only with a ring slot published after it was latched), extended
to the detail, so a reply can never pair one halt's registers with another's match. The stop word itself
is full (`seq28 \| reason3 \| cpu1 \| pc32`), which is why the detail is a separate record keyed by
`stop_seq` rather than more bits.

What the glue can honestly report in v26 (Mednafen; each emulator's patcher defines its own):

| Halt | `match_state` | `matched` |
|---|---|---|
| user pause, instruction step | `NONE` | empty |
| PC equals the address of one or more enabled execution descriptors, and **no** data watchpoint is installed | `EXACT` | those descriptors, every one at that address (so a user breakpoint and a runner breakpoint at one address are both named, and the runner sees the halt as mixed) |
| a data watchpoint hit by an SH-2 instruction, or a PC match while data watchpoints are also installed | `AMBIGUOUS` | the **candidate superset**: every enabled data descriptor, plus any execution descriptor at the PC. `SeSsBpHook` receives `bpoint` only; narrowing needs the accessed effective address, which v26 does not ask the glue to recompute |
| an SCU-DMA watchpoint (`SE_LIVE_STOP_DMA_WATCH`) | `UNKNOWN` | empty: the PC is where the CPU stood, and no instruction made the access |
| the glue cannot tell (older glue, a stub) | `UNKNOWN`, or `length 0` | empty |

The runner's use of this (PLAN.md §9.2 "Halt attribution"): a halt is the runner's **only if every
descriptor in `matched` is runner-owned** — which holds for `EXACT` and for `AMBIGUOUS` when the whole
candidate set is the runner's. A user-owned descriptor in the set, `UNKNOWN`, or a stale `bkp_gen` stops
the run with that cause; a set naming both owners is *mixed* and is never runner success. Ownership is
SE's own mapping from descriptor index to owner UUID for that wire generation (§4); the server never
sees owners.

### 5.2 Trace event (v26: 112 bytes)

v25 event, 104 bytes (`SE_LIVE_EVENT_LEN`): `id(4) cpu(4) frame(4)` + 23 registers. Verified against
`se_export.c:2173-2200` and `LiveDriver.cpp:1006-1026`. v26 appends 8 bytes:

| Offset | Size | Field |
|---|---|---|
| +0 | 4 | `id` (the descriptor's id, as sent) |
| +4 | 4 | `cpu` |
| +8 | 4 | `frame` (low 32 bits of the emulator frame counter) |
| +12 | 92 | 23 registers, `se_sh2_regs` order: `r[0..15], pc, pr, sr, gbr, vbr, mach, macl` |
| **+104** | 4 | **`trc_gen`**: the tracepoint-set generation installed when the event fired |
| **+108** | 4 | **`run_epoch`**: as in the control block |

`SE_LIVE_EVENT_LEN` becomes 112 for a v26 reply; the count and the cap of 64 per reply are unchanged. A
v26 client sizes event records by the server's version.

Stamping happens in `SeExportQueueTraceEvent`, under the lock it already takes, from the *current*
`sTrcAppliedGen`. Installs and events both run on the emulate thread, so the stamp is exact: an event
fired before a new set was taken carries the old generation even if the reply that delivers it comes after
the new set is acknowledged. That is the property the runner needs: it accepts an event only if
`trc_gen` equals the generation of its own run **and** `run_epoch` is the run's, so events queued under an
earlier configuration are never mistaken for experiment evidence.

The existing disconnect clear (`sEvHead = sEvCount = 0`) stays: it is why a later client does not inherit
events, and with stamping it is belt-and-braces rather than the only defence.

## 6. Runner rules these fields enable (consumed in M4, restated here so the contract is whole)

1. **Install** — send BK2/TR2 with a fresh generation; state *AwaitingInstall* until
   `*_applied_gen == sent` with the recorded `run_epoch`. A bound on the wait is a runner limit, not a
   protocol one.
2. **Collision** — an applied generation the connection never sent: *unknown*; refuse to run. (Two
   controlling clients are not supported by the runner. The protocol does not forbid a second *viewer*.)
3. **Halt** — attribute per §5.1; compare `bkp_gen` with the runner's.
4. **Events** — accept `trc_gen == runner's` and `run_epoch == recorded` only; report `events_dropped` and
   `eventsLostClient` deltas.
5. **Cleanup** — send empty BK2/TR2 with new generations; cleanup is *complete* once both
   `*_applied_gen` equal them (PLAN.md §9.2). An unacknowledged removal is *unconfirmed*.
6. **Counter resets** — `run_epoch` change or any monotonic counter moving backwards means a new server
   run: the run ends *Interrupted*, with nothing carried over.

Not guaranteed by anything in this document (PLAN.md §9.1, last paragraph): that the emulator stops after
the connection is lost. The server's own disconnect behaviour (§1) governs that: it drops every
instrument and **resumes**, which is why the runner reports *Interrupted – emulator resumed by server*.

## 7. Hit-time memory (PLAN.md §9.1 item 5)

Unchanged in v26. A tracepoint event carries registers only; memory named in a tracepoint format is read
by the client from the *next* snapshot. Results never present such a value as the memory at the hit. A
later revision could capture requested words at the hit; that is a separate protocol item, not part of
this one.

## 8. Test plan for M4

Written here so the spec is checkable now. Unit and fake-transport tests live beside
`FrontEnd/tests/FakeLiveServer.h` (POSIX only); the in-process real-server tests follow the pattern of
`LiveHaltRaceTests` / `LiveExporterRobustnessTests`.

* **Wire**: BK2/TR2 with N = 0, 1, max, max+1 (client dropped), generation 0, non-zero reserved; the 72-byte
  control block read by a v26 client and by a v25 client path (`ct == 56`) of the same code; the event
  record at 104 and 112 bytes selected by server version; the stop detail section present, absent
  (`length 0`), and stale (`stop_seq` mismatch ignored).
* **Ordering**: generation acknowledged late, out of order, missing, from a replaced connection, and from
  a second sender; mailbox replacement (a set superseded before it is taken is never acknowledged).
* **Attribution**: exact, ambiguous (runner-only superset accepted; mixed rejected), unknown (DMA),
  `bkp_gen` stale.
* **Events**: queued under an earlier generation (rejected), under another `run_epoch` (rejected), ring
  overflow moving `events_dropped`, driver queue overflow moving `eventsLostClient`.
* **Lifecycle**: disconnect clears both applied generations to 0 without changing `run_epoch`; server
  restart changes it; cleanup unacknowledged reads *unconfirmed*.
* **Compatibility**: v26 client against a v25 server uses BKP/TRC, never sends BK2/TR2, reports installs
  unconfirmed and disables Unattended Run; a v25 client refuses a v26 server as today.
* **Real emulator** (manual, `Docs/FunctionalityVerification/`): patched Mednafen on Windows and macOS.

## 9. Files M4 touches

`Integration/Common/SeLiveProtocol.h` (version, caps, verbs, layouts), `se_export.c` / `se_export.h`
(mailbox generation, applied generations, `run_epoch`, `events_dropped`, event stamping, stop detail
latch), `Integration/Mednafen/apply.py` and `se_mednafen_glue.c` (stop detail from the glue, cap bits),
`Integration/Yabause` (cap bits only; its glue reports `match_state UNKNOWN`), `Drivers/Live/src`
(`MinVerFor`, control clamp, parsing, per-connection generations, `eventsLostClient`, API), and tests.
