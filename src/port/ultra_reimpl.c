#include <ultra64.h>
#include <string.h>

#include <SDL.h>

#include "port.h"

#define EEPROM_SIZE 512
#define EEPROM_FILE "sm64_save.eep"

#if defined(VERSION_EU)
u32 osTvType = 0;
#else
u32 osTvType = 1;
#endif
u32 osResetType = 0;
u64 osClockRate = 62500000;
u32 osMemSize = 0x800000;
u8 osAppNmiBuffer[64];

static OSMesgQueue *sEventQueues[OS_NUM_EVENTS];
static OSMesg sEventMsgs[OS_NUM_EVENTS];
static u8 sEeprom[EEPROM_SIZE];
static bool sEepromLoaded;
static u64 sTimeBase;

void osInitialize(void) {
}

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msg, s32 count) {
    mq->mtqueue = NULL;
    mq->fullqueue = NULL;
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msg;
}

s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag) {
    s32 index;
    (void) flag;
    if (mq == NULL || mq->validCount >= mq->msgCount) {
        return -1;
    }
    index = (mq->first + mq->validCount) % mq->msgCount;
    mq->msg[index] = msg;
    mq->validCount++;
    return 0;
}

s32 osJamMesg(OSMesgQueue *mq, OSMesg msg, s32 flag) {
    (void) flag;
    if (mq == NULL || mq->validCount >= mq->msgCount) {
        return -1;
    }
    mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
    mq->msg[mq->first] = msg;
    mq->validCount++;
    return 0;
}

s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag) {
    (void) flag;
    if (mq == NULL || mq->validCount == 0) {
        return -1;
    }
    if (msg != NULL) {
        *msg = mq->msg[mq->first];
    }
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    return 0;
}

void osSetEventMesg(OSEvent e, OSMesgQueue *mq, OSMesg msg) {
    if (e < OS_NUM_EVENTS) {
        sEventQueues[e] = mq;
        sEventMsgs[e] = msg;
    }
}

void osCreateThread(OSThread *thread, OSId id, void (*entry)(void *), void *arg, void *sp, OSPri pri) {
    (void) thread;
    (void) id;
    (void) entry;
    (void) arg;
    (void) sp;
    (void) pri;
}

void osStartThread(OSThread *thread) {
    (void) thread;
}

void osStopThread(OSThread *thread) {
    (void) thread;
}

void osSetThreadPri(OSThread *thread, OSPri pri) {
    (void) thread;
    (void) pri;
}

OSPri osGetThreadPri(OSThread *thread) {
    (void) thread;
    return 0;
}

OSId osGetThreadId(OSThread *thread) {
    (void) thread;
    return 0;
}

void osCreateViManager(OSPri pri) {
    (void) pri;
}

void osViSetMode(OSViMode *mode) {
    (void) mode;
}

void osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount) {
    (void) mq;
    (void) msg;
    (void) retraceCount;
}

void osViBlack(u8 active) {
    (void) active;
}

void osViSetSpecialFeatures(u32 func) {
    (void) func;
}

void osViSwapBuffer(void *vaddr) {
    (void) vaddr;
}

void osCreatePiManager(OSPri pri, OSMesgQueue *cmdQ, OSMesg *cmdBuf, s32 cmdMsgCnt) {
    (void) pri;
    (void) cmdQ;
    (void) cmdBuf;
    (void) cmdMsgCnt;
}

s32 osPiStartDma(OSIoMesg *mb, s32 priority, s32 direction, uintptr_t devAddr, void *vAddr, size_t nbytes,
                 OSMesgQueue *mq) {
    (void) priority;
    if (direction == OS_READ) {
        memcpy(vAddr, (const void *) devAddr, nbytes);
    } else {
        memcpy((void *) devAddr, vAddr, nbytes);
    }
    if (mq != NULL) {
        osSendMesg(mq, (OSMesg) mb, OS_MESG_NOBLOCK);
    }
    return 0;
}

s32 osEPiStartDma(OSPiHandle *pihandle, OSIoMesg *mb, s32 direction) {
    (void) pihandle;
    return osPiStartDma(mb, mb->hdr.pri, direction, mb->devAddr, mb->dramAddr, mb->size, mb->hdr.retQueue);
}

OSPiHandle *osCartRomInit(void) {
    static OSPiHandle handle;
    return &handle;
}

OSPiHandle *osDriveRomInit(void) {
    static OSPiHandle handle;
    return &handle;
}

static u64 perf_now(void) {
    u64 c = SDL_GetPerformanceCounter();
    u64 f = SDL_GetPerformanceFrequency();
    return (u64) ((double) c * (46875000.0 / (double) f));
}

OSTime osGetTime(void) {
    return perf_now() - sTimeBase;
}

void osSetTime(OSTime time) {
    sTimeBase = perf_now() - time;
}

u32 osGetCount(void) {
    return (u32) perf_now();
}

void osMapTLB(s32 index, OSPageMask pm, void *vaddr, u32 evenpaddr, u32 oddpaddr, s32 asid) {
    (void) index;
    (void) pm;
    (void) vaddr;
    (void) evenpaddr;
    (void) oddpaddr;
    (void) asid;
}

void osUnmapTLBAll(void) {
}

void osInvalDCache(void *vaddr, size_t nbytes) {
    (void) vaddr;
    (void) nbytes;
}

void osInvalICache(void *vaddr, size_t nbytes) {
    (void) vaddr;
    (void) nbytes;
}

void osWritebackDCache(void *vaddr, size_t nbytes) {
    (void) vaddr;
    (void) nbytes;
}

void osWritebackDCacheAll(void) {
}

uintptr_t osVirtualToPhysical(void *addr) {
    return (uintptr_t) addr;
}

void osSpTaskLoad(OSTask *task) {
    (void) task;
}

void osSpTaskStartGo(OSTask *task) {
    (void) task;
}

void osSpTaskYield(void) {
}

OSYieldResult osSpTaskYielded(OSTask *task) {
    (void) task;
    return 0;
}

s32 osContInit(OSMesgQueue *mq, u8 *bitpattern, OSContStatus *status) {
    int i;
    u8 bits = 0;
    (void) mq;
    port_input_init();
    for (i = 0; i < MAXCONTROLLERS; i++) {
        status[i].type = CONT_TYPE_NORMAL;
        status[i].status = 0;
        status[i].errnum = 0;
    }
    bits = 1;
    if (port_input_pad_connected(1)) {
        bits |= 2;
    }
    *bitpattern = bits;
    return 0;
}

s32 osContStartReadData(OSMesgQueue *mq) {
    port_input_update();
    osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}

void osContGetReadData(OSContPad *pad) {
    int i;
    for (i = 0; i < MAXCONTROLLERS; i++) {
        struct PortPadState st;
        port_input_get_pad(i, &st);
        pad[i].button = st.buttons;
        pad[i].stick_x = st.stickX;
        pad[i].stick_y = st.stickY;
        pad[i].errnum = 0;
    }
}

static void eeprom_load(void) {
    if (sEepromLoaded) {
        return;
    }
    memset(sEeprom, 0, sizeof(sEeprom));
    port_read_file(EEPROM_FILE, sEeprom, sizeof(sEeprom));
    sEepromLoaded = true;
}

s32 osEepromProbe(OSMesgQueue *mq) {
    (void) mq;
    eeprom_load();
    return EEPROM_TYPE_4K;
}

s32 osEepromLongRead(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes) {
    size_t off = (size_t) address * 8;
    (void) mq;
    eeprom_load();
    if (off + (size_t) nbytes > sizeof(sEeprom)) {
        return -1;
    }
    memcpy(buffer, sEeprom + off, (size_t) nbytes);
    return 0;
}

s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes) {
    size_t off = (size_t) address * 8;
    (void) mq;
    eeprom_load();
    if (off + (size_t) nbytes > sizeof(sEeprom)) {
        return -1;
    }
    memcpy(sEeprom + off, buffer, (size_t) nbytes);
    return port_write_file(EEPROM_FILE, sEeprom, sizeof(sEeprom)) ? 0 : -1;
}

s32 osEepromRead(OSMesgQueue *mq, u8 address, u8 *buffer) {
    return osEepromLongRead(mq, address, buffer, 8);
}

s32 osEepromWrite(OSMesgQueue *mq, u8 address, u8 *buffer) {
    return osEepromLongWrite(mq, address, buffer, 8);
}

s32 osAiSetFrequency(u32 freq) {
    port_audio_set_frequency(freq);
    return (s32) freq;
}

u32 osAiGetLength(void) {
    return port_audio_buffered_bytes();
}

s32 osAiSetNextBuffer(void *buf, u32 size) {
    if (port_audio_enabled()) {
        port_audio_queue(buf, size);
    }
    return 0;
}

u32 osAiGetStatus(void) {
    return 0;
}

#if defined(VERSION_SH) || defined(VERSION_CN)
s32 osMotorInit(OSMesgQueue *mq, OSPfs *pfs, int channel) {
    (void) mq;
    if (pfs != NULL) {
        pfs->channel = channel;
    }
    return port_input_pad_connected(channel) ? 0 : 1;
}

s32 __osMotorAccess(OSPfs *pfs, s32 flag) {
    port_input_rumble(pfs != NULL ? pfs->channel : 0, flag != 0);
    return 0;
}

s32 osMotorStart(OSPfs *pfs) {
    return __osMotorAccess(pfs, 1);
}

s32 osMotorStop(OSPfs *pfs) {
    return __osMotorAccess(pfs, 0);
}
#endif

OSThread *__osGetCurrFaultedThread(void) {
    return NULL;
}

void osSyncPrintf(const char *fmt, ...) {
    (void) fmt;
}
