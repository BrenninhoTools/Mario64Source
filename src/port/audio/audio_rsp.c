#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "port_audio_abi.h"

#define DMEM_SIZE 0x2000
#define DMEM_MASK (DMEM_SIZE - 1)
#define A_INIT_FLAG 0x01
#define A_LOOP_FLAG 0x02
#define A_LEFT_FLAG 0x02
#define A_VOL_FLAG 0x04
#define A_AUX_FLAG 0x08

#define ROUND_UP_8(v) (((v) + 7) & ~7)
#define ROUND_UP_16(v) (((v) + 15) & ~15)
#define ROUND_UP_32(v) (((v) + 31) & ~31)
#define ROUND_UP_64(v) (((v) + 63) & ~63)
#define ROUND_DOWN_16(v) ((v) & ~15)

static const int16_t sResampleTable[64][4] = {
    { 3129, 26285, 3398, -33 },
    { 2873, 26262, 3679, -40 },
    { 2628, 26217, 3971, -48 },
    { 2394, 26150, 4276, -56 },
    { 2173, 26061, 4592, -65 },
    { 1963, 25950, 4920, -74 },
    { 1764, 25817, 5260, -84 },
    { 1576, 25663, 5611, -95 },
    { 1399, 25487, 5974, -106 },
    { 1233, 25291, 6347, -118 },
    { 1077, 25075, 6732, -130 },
    { 932, 24838, 7127, -143 },
    { 796, 24583, 7532, -156 },
    { 671, 24309, 7947, -170 },
    { 554, 24016, 8371, -184 },
    { 446, 23706, 8804, -198 },
    { 347, 23379, 9246, -212 },
    { 257, 23036, 9696, -226 },
    { 174, 22678, 10153, -240 },
    { 99, 22304, 10618, -254 },
    { 31, 21917, 11088, -268 },
    { -30, 21517, 11564, -280 },
    { -84, 21104, 12045, -293 },
    { -132, 20679, 12531, -304 },
    { -173, 20244, 13020, -314 },
    { -210, 19799, 13512, -323 },
    { -241, 19345, 14006, -330 },
    { -267, 18882, 14501, -336 },
    { -289, 18413, 14997, -340 },
    { -306, 17937, 15493, -341 },
    { -320, 17456, 15988, -340 },
    { -330, 16970, 16480, -337 },
    { -337, 16480, 16970, -330 },
    { -340, 15988, 17456, -320 },
    { -341, 15493, 17937, -306 },
    { -340, 14997, 18413, -289 },
    { -336, 14501, 18882, -267 },
    { -330, 14006, 19345, -241 },
    { -323, 13512, 19799, -210 },
    { -314, 13020, 20244, -173 },
    { -304, 12531, 20679, -132 },
    { -293, 12045, 21104, -84 },
    { -280, 11564, 21517, -30 },
    { -268, 11088, 21917, 31 },
    { -254, 10618, 22304, 99 },
    { -240, 10153, 22678, 174 },
    { -226, 9696, 23036, 257 },
    { -212, 9246, 23379, 347 },
    { -198, 8804, 23706, 446 },
    { -184, 8371, 24016, 554 },
    { -170, 7947, 24309, 671 },
    { -156, 7532, 24583, 796 },
    { -143, 7127, 24838, 932 },
    { -130, 6732, 25075, 1077 },
    { -118, 6347, 25291, 1233 },
    { -106, 5974, 25487, 1399 },
    { -95, 5611, 25663, 1576 },
    { -84, 5260, 25817, 1764 },
    { -74, 4920, 25950, 1963 },
    { -65, 4592, 26061, 2173 },
    { -56, 4276, 26150, 2394 },
    { -48, 3971, 26217, 2628 },
    { -40, 3679, 26262, 2873 },
    { -33, 3398, 26285, 3129 },
};

static union {
    uint8_t u8[DMEM_SIZE + 64];
    int16_t s16[(DMEM_SIZE + 64) / 2];
} sDmem;

static struct {
    uint16_t in;
    uint16_t out;
    uint16_t nbytes;
    uint16_t dryRight;
    uint16_t wetLeft;
    uint16_t wetRight;
    int16_t vol[2];
    int16_t target[2];
    int32_t rate[2];
    int16_t dry;
    int16_t wet;
    int16_t adpcmTable[8][2][8];
    int16_t *loopState;
    uint16_t envVol[2];
    uint16_t envRate[2];
    uint16_t envVolWet;
    uint16_t envRateWet;
    int16_t filter[8];
    uint16_t filterCount;
} sRsp;

static uint8_t *dmem_u8(uint32_t addr) {
    return &sDmem.u8[addr & DMEM_MASK];
}

static int16_t *dmem_s16(uint32_t addr) {
    return (int16_t *) dmem_u8(addr & ~1u);
}

static int16_t clamp16(int32_t v) {
    if (v < -32768) {
        return -32768;
    }
    if (v > 32767) {
        return 32767;
    }
    return (int16_t) v;
}

static void dmem_copy_in(uint32_t addr, const void *src, size_t n) {
    addr &= DMEM_MASK;
    if (addr + n > DMEM_SIZE) {
        n = DMEM_SIZE - addr;
    }
    memcpy(&sDmem.u8[addr], src, n);
}

static void dmem_copy_out(void *dst, uint32_t addr, size_t n) {
    addr &= DMEM_MASK;
    if (addr + n > DMEM_SIZE) {
        n = DMEM_SIZE - addr;
    }
    memcpy(dst, &sDmem.u8[addr], n);
}

void port_aClearBuffer(uint16_t addr, int nbytes) {
    uint32_t a = addr & DMEM_MASK;
    size_t n = (size_t) ROUND_UP_16(nbytes);
    if (a + n > DMEM_SIZE) {
        n = DMEM_SIZE - a;
    }
    memset(&sDmem.u8[a], 0, n);
}

void port_aSetBuffer(uint8_t flags, uint16_t in, uint16_t out, uint16_t nbytes) {
    if (flags & A_AUX_FLAG) {
        sRsp.dryRight = in;
        sRsp.wetLeft = out;
        sRsp.wetRight = nbytes;
    } else {
        sRsp.in = in;
        sRsp.out = out;
        sRsp.nbytes = nbytes;
    }
}

void port_aDMEMMove(uint16_t in, uint16_t out, int nbytes) {
    size_t n = (size_t) ROUND_UP_16(nbytes);
    uint32_t i = in & DMEM_MASK;
    uint32_t o = out & DMEM_MASK;
    if (i + n > DMEM_SIZE) {
        n = DMEM_SIZE - i;
    }
    if (o + n > DMEM_SIZE) {
        n = DMEM_SIZE - o;
    }
    memmove(&sDmem.u8[o], &sDmem.u8[i], n);
}

void port_aLoadADPCM(int nbytes, const void *book) {
    size_t n = (size_t) nbytes;
    if (n > sizeof(sRsp.adpcmTable)) {
        n = sizeof(sRsp.adpcmTable);
    }
    memcpy(sRsp.adpcmTable, book, n);
}

void port_aSetLoop(void *state) {
    sRsp.loopState = (int16_t *) state;
}

void port_aADPCMdec(uint8_t flags, void *state) {
    uint8_t *in = dmem_u8(sRsp.in);
    int16_t *out = dmem_s16(sRsp.out);
    int nbytes = ROUND_UP_32(sRsp.nbytes);
    int16_t *st = (int16_t *) state;

    if (flags & A_INIT_FLAG) {
        memset(out, 0, 16 * sizeof(int16_t));
    } else if ((flags & A_LOOP_FLAG) && sRsp.loopState != NULL) {
        memcpy(out, sRsp.loopState, 16 * sizeof(int16_t));
    } else {
        memcpy(out, st, 16 * sizeof(int16_t));
    }
    out += 16;
    while (nbytes > 0) {
        int shift = *in >> 4;
        int tableIndex = *in++ & 0xf;
        int16_t (*tbl)[8] = sRsp.adpcmTable[tableIndex & 7];
        int i;
        for (i = 0; i < 2; i++) {
            int16_t ins[8];
            int16_t prev1 = out[-1];
            int16_t prev2 = out[-2];
            int j, k;
            for (j = 0; j < 4; j++) {
                int hi = (int32_t) ((uint32_t) (*in >> 4) << 28) >> 28;
                int lo = (int32_t) ((uint32_t) (*in & 0xf) << 28) >> 28;
                in++;
                ins[j * 2] = (int16_t) (hi << shift);
                ins[j * 2 + 1] = (int16_t) (lo << shift);
            }
            for (j = 0; j < 8; j++) {
                int32_t acc = tbl[0][j] * prev2 + tbl[1][j] * prev1 + (ins[j] << 11);
                for (k = 0; k < j; k++) {
                    acc += tbl[1][(j - k) - 1] * ins[k];
                }
                acc >>= 11;
                *out++ = clamp16(acc);
            }
        }
        nbytes -= 16 * (int) sizeof(int16_t);
    }
    memcpy(st, out - 16, 16 * sizeof(int16_t));
}

void port_aS8Dec(uint8_t flags, void *state) {
    uint8_t *in = dmem_u8(sRsp.in);
    int16_t *out = dmem_s16(sRsp.out);
    int nbytes = ROUND_UP_32(sRsp.nbytes);
    int16_t *st = (int16_t *) state;

    if (flags & A_INIT_FLAG) {
        memset(out, 0, 16 * sizeof(int16_t));
    } else if ((flags & A_LOOP_FLAG) && sRsp.loopState != NULL) {
        memcpy(out, sRsp.loopState, 16 * sizeof(int16_t));
    } else {
        memcpy(out, st, 16 * sizeof(int16_t));
    }
    out += 16;
    while (nbytes > 0) {
        int i;
        for (i = 0; i < 16; i++) {
            *out++ = (int16_t) ((int8_t) *in++ * 256);
        }
        nbytes -= 16 * (int) sizeof(int16_t);
    }
    memcpy(st, out - 16, 16 * sizeof(int16_t));
}

void port_aResample(uint8_t flags, uint16_t pitch, void *state) {
    int16_t *st = (int16_t *) state;
    int16_t tmp[16];
    int16_t *inInitial = dmem_s16(sRsp.in);
    int16_t *in = inInitial;
    int16_t *out = dmem_s16(sRsp.out);
    int nbytes = ROUND_UP_16(sRsp.nbytes);
    uint32_t accum;
    int i;

    if (flags & A_INIT_FLAG) {
        memset(tmp, 0, sizeof(tmp));
    } else {
        memcpy(tmp, st, sizeof(tmp));
    }
    if (flags & 2) {
        memcpy(in - 8, tmp + 8, 8 * sizeof(int16_t));
        in -= tmp[5] / (int) sizeof(int16_t);
    }
    in -= 4;
    accum = (uint16_t) tmp[4];
    memcpy(in, tmp, 4 * sizeof(int16_t));
    do {
        for (i = 0; i < 8; i++) {
            const int16_t *c = sResampleTable[(accum * 64) >> 16];
            int32_t sample = ((in[0] * c[0] + 0x4000) >> 15) + ((in[1] * c[1] + 0x4000) >> 15)
                             + ((in[2] * c[2] + 0x4000) >> 15) + ((in[3] * c[3] + 0x4000) >> 15);
            *out++ = clamp16(sample);
            accum += (uint32_t) pitch << 1;
            in += accum >> 16;
            accum &= 0xffff;
        }
        nbytes -= 8 * (int) sizeof(int16_t);
    } while (nbytes > 0);

    st[4] = (int16_t) accum;
    memcpy(st, in, 4 * sizeof(int16_t));
    i = (int) ((in - inInitial + 4) & 7);
    in -= i;
    if (i != 0) {
        i = -8 - i;
    }
    st[5] = (int16_t) i;
    memcpy(st + 8, in, 8 * sizeof(int16_t));
}

struct Ramp {
    int32_t value;
    int32_t target;
    int32_t step;
};

static int16_t ramp_step(struct Ramp *r) {
    bool reached;
    r->value += r->step;
    reached = r->step <= 0 ? r->value <= r->target : r->value >= r->target;
    if (reached) {
        r->value = r->target;
        r->step = 0;
    }
    return (int16_t) (r->value >> 16);
}

void port_aEnvMixer(uint8_t flags, void *state) {
    int16_t *save = (int16_t *) state;
    int16_t *in = dmem_s16(sRsp.in);
    int16_t *dl = dmem_s16(sRsp.out);
    int16_t *dr = dmem_s16(sRsp.dryRight);
    int16_t *wl = dmem_s16(sRsp.wetLeft);
    int16_t *wr = dmem_s16(sRsp.wetRight);
    bool aux = (flags & A_AUX_FLAG) != 0;
    struct Ramp ramps[2];
    int32_t expSeq[2];
    int32_t expRates[2];
    int16_t dry = sRsp.dry;
    int16_t wet = sRsp.wet;
    int count = ROUND_UP_16(sRsp.nbytes) / 2;
    int y, x, ptr = 0;

    if (flags & A_INIT_FLAG) {
        ramps[0].value = sRsp.vol[0] << 16;
        ramps[1].value = sRsp.vol[1] << 16;
        ramps[0].target = sRsp.target[0] << 16;
        ramps[1].target = sRsp.target[1] << 16;
        expRates[0] = sRsp.rate[0];
        expRates[1] = sRsp.rate[1];
        expSeq[0] = sRsp.vol[0] * sRsp.rate[0];
        expSeq[1] = sRsp.vol[1] * sRsp.rate[1];
    } else {
        wet = save[0];
        dry = save[2];
        memcpy(&ramps[0].target, save + 4, 4);
        memcpy(&ramps[1].target, save + 6, 4);
        memcpy(&expRates[0], save + 8, 4);
        memcpy(&expRates[1], save + 10, 4);
        memcpy(&expSeq[0], save + 12, 4);
        memcpy(&expSeq[1], save + 14, 4);
        memcpy(&ramps[0].value, save + 16, 4);
        memcpy(&ramps[1].value, save + 18, 4);
    }
    ramps[0].step = ramps[0].target - ramps[0].value;
    ramps[1].step = ramps[1].target - ramps[1].value;

    for (y = 0; y < count; y += 8) {
        int j;
        for (j = 0; j < 2; j++) {
            if (ramps[j].step != 0) {
                expSeq[j] = (int32_t) (((int64_t) expSeq[j] * (int64_t) expRates[j]) >> 16);
                ramps[j].step = (expSeq[j] - ramps[j].value) >> 3;
            }
        }
        for (x = 0; x < 8; x++) {
            int16_t lv = ramp_step(&ramps[0]);
            int16_t rv = ramp_step(&ramps[1]);
            int16_t g0 = clamp16((lv * dry + 0x4000) >> 15);
            int16_t g1 = clamp16((rv * dry + 0x4000) >> 15);
            int16_t s = in[ptr];
            dl[ptr] = clamp16(dl[ptr] + ((s * g0) >> 15));
            dr[ptr] = clamp16(dr[ptr] + ((s * g1) >> 15));
            if (aux) {
                int16_t g2 = clamp16((lv * wet + 0x4000) >> 15);
                int16_t g3 = clamp16((rv * wet + 0x4000) >> 15);
                wl[ptr] = clamp16(wl[ptr] + ((s * g2) >> 15));
                wr[ptr] = clamp16(wr[ptr] + ((s * g3) >> 15));
            }
            ptr++;
        }
    }

    save[0] = wet;
    save[2] = dry;
    memcpy(save + 4, &ramps[0].target, 4);
    memcpy(save + 6, &ramps[1].target, 4);
    memcpy(save + 8, &expRates[0], 4);
    memcpy(save + 10, &expRates[1], 4);
    memcpy(save + 12, &expSeq[0], 4);
    memcpy(save + 14, &expSeq[1], 4);
    memcpy(save + 16, &ramps[0].value, 4);
    memcpy(save + 18, &ramps[1].value, 4);
}

void port_aInterleave(uint16_t left, uint16_t right) {
    int count = ROUND_UP_16(sRsp.nbytes) / 2;
    int16_t *l = dmem_s16(left);
    int16_t *r = dmem_s16(right);
    int16_t *d = dmem_s16(sRsp.out);
    int16_t tmp[2048];
    int i;
    if (count > 1024) {
        count = 1024;
    }
    for (i = 0; i < count; i++) {
        tmp[i * 2] = l[i];
        tmp[i * 2 + 1] = r[i];
    }
    memcpy(d, tmp, (size_t) count * 4);
}

void port_aLoadBuffer(const void *src) {
    dmem_copy_in(sRsp.in, src, (size_t) ROUND_UP_8(sRsp.nbytes));
}

void port_aSaveBuffer(void *dst) {
    dmem_copy_out(dst, sRsp.out, (size_t) ROUND_UP_8(sRsp.nbytes));
}

void port_aMix(uint8_t flags, int16_t gain, uint16_t in, uint16_t out) {
    int count = ROUND_UP_32(sRsp.nbytes) / 2;
    int16_t *src = dmem_s16(in);
    int16_t *dst = dmem_s16(out);
    int i;
    (void) flags;
    for (i = 0; i < count; i++) {
        dst[i] = clamp16(dst[i] + ((src[i] * gain) >> 15));
    }
}

void port_aSetVolume(uint8_t flags, uint16_t v, uint16_t t, uint16_t r) {
    if (flags & A_AUX_FLAG) {
        sRsp.dry = (int16_t) v;
        sRsp.wet = (int16_t) r;
    } else {
        int lr = (flags & A_LEFT_FLAG) ? 0 : 1;
        if (flags & A_VOL_FLAG) {
            sRsp.vol[lr] = (int16_t) v;
        } else {
            sRsp.target[lr] = (int16_t) v;
            sRsp.rate[lr] = (int32_t) (((uint32_t) t << 16) | r);
        }
    }
}

void port_aSetVolume32(uint8_t flags, uint16_t v, uint32_t rate) {
    port_aSetVolume(flags, v, (uint16_t) (rate >> 16), (uint16_t) (rate & 0xffff));
}

void port_aAddMixer(uint16_t count, uint16_t in, uint16_t out) {
    int n = ROUND_UP_64(ROUND_DOWN_16((int) count)) / 2;
    int16_t *src = dmem_s16(in);
    int16_t *dst = dmem_s16(out);
    int i;
    for (i = 0; i < n; i++) {
        dst[i] = clamp16(dst[i] + src[i]);
    }
}

void port_aLoadBufferSh(const void *src, uint16_t dst, uint16_t nbytes) {
    dmem_copy_in(dst, src, (size_t) ROUND_DOWN_16(nbytes));
}

void port_aSaveBufferSh(uint16_t src, void *dst, uint16_t nbytes) {
    dmem_copy_out(dst, src, (size_t) ROUND_DOWN_16(nbytes));
}

void port_aDuplicate(uint8_t copies, uint16_t in, uint16_t out) {
    uint8_t tmp[128];
    uint32_t o = out;
    int i;
    memcpy(tmp, dmem_u8(in), 128);
    for (i = 0; i < copies; i++) {
        dmem_copy_in(o, tmp, 128);
        o += 128;
    }
}

void port_aDMEMMove2(uint8_t times, uint16_t in, uint16_t out, uint16_t nbytes) {
    int n = ROUND_UP_32((int) nbytes);
    uint32_t o = out;
    int i;
    for (i = 0; i < times; i++) {
        port_aDMEMMove(in, (uint16_t) o, n);
        o += (uint32_t) n;
    }
}

void port_aResampleZoh(uint16_t pitch, uint16_t startFract) {
    int16_t *in = dmem_s16(sRsp.in);
    int16_t *out = dmem_s16(sRsp.out);
    int n = ROUND_UP_8(sRsp.nbytes) / 2;
    uint32_t pos = startFract;
    uint32_t step = (uint32_t) pitch << 2;
    int i;
    for (i = 0; i < n; i++) {
        out[i] = in[pos >> 17];
        pos += step;
    }
}

void port_aDownsampleHalf(uint16_t nSamples, uint16_t in, uint16_t out) {
    int16_t *src = dmem_s16(in);
    int16_t *dst = dmem_s16(out);
    int n = ROUND_UP_8((int) nSamples);
    int i;
    for (i = 0; i < n; i++) {
        dst[i] = src[i * 2];
    }
}

void port_aMixSh(uint16_t nbytes, int16_t gain, uint16_t in, uint16_t out) {
    int n = ROUND_UP_32(ROUND_DOWN_16((int) nbytes)) / 2;
    int16_t *src = dmem_s16(in);
    int16_t *dst = dmem_s16(out);
    int i;
    if (gain == -0x8000) {
        for (i = 0; i < n; i++) {
            dst[i] = clamp16(dst[i] - src[i]);
        }
        return;
    }
    for (i = 0; i < n; i++) {
        dst[i] = clamp16((dst[i] * 0x7fff + src[i] * gain + 0x4000) >> 15);
    }
}

void port_aEnvSetup1(uint8_t volWet, uint16_t rateWet, uint16_t rateLeft, uint16_t rateRight) {
    sRsp.envVolWet = (uint16_t) (volWet << 8);
    sRsp.envRateWet = rateWet;
    sRsp.envRate[0] = rateLeft;
    sRsp.envRate[1] = rateRight;
}

void port_aEnvSetup2(uint16_t volLeft, uint16_t volRight) {
    sRsp.envVol[0] = volLeft;
    sRsp.envVol[1] = volRight;
}

void port_aEnvMixerSh(uint16_t inAddr, uint16_t nSamples, int swapReverb, int negLeft, int negRight, uint16_t dryLeft,
                      uint16_t dryRight, uint16_t wetLeft, uint16_t wetRight) {
    int16_t *in = dmem_s16(inAddr);
    int16_t *dry[2];
    int16_t *wet[2];
    int16_t negs[2];
    int swapped[2];
    int n = ROUND_UP_16((int) nSamples);
    uint16_t vols[2];
    uint16_t volWet = sRsp.envVolWet;
    int i, j;
    dry[0] = dmem_s16(dryLeft);
    dry[1] = dmem_s16(dryRight);
    wet[0] = dmem_s16(wetLeft);
    wet[1] = dmem_s16(wetRight);
    negs[0] = negLeft ? -1 : 0;
    negs[1] = negRight ? -1 : 0;
    swapped[0] = swapReverb ? 1 : 0;
    swapped[1] = swapReverb ? 0 : 1;
    vols[0] = sRsp.envVol[0];
    vols[1] = sRsp.envVol[1];
    do {
        for (i = 0; i < 8; i++) {
            int16_t samples[2];
            samples[0] = *in;
            samples[1] = *in;
            in++;
            for (j = 0; j < 2; j++) {
                samples[j] = (int16_t) (((samples[j] * (int32_t) vols[j]) >> 16) ^ negs[j]);
            }
            for (j = 0; j < 2; j++) {
                *dry[j] = clamp16(*dry[j] + samples[j]);
                dry[j]++;
                *wet[j] = clamp16(*wet[j] + ((samples[swapped[j]] * (int32_t) volWet) >> 16));
                wet[j]++;
            }
        }
        vols[0] = (uint16_t) (vols[0] + sRsp.envRate[0]);
        vols[1] = (uint16_t) (vols[1] + sRsp.envRate[1]);
        volWet = (uint16_t) (volWet + sRsp.envRateWet);
        n -= 8;
    } while (n > 0);
}

void port_aInterleaveSh(uint16_t out, uint16_t left, uint16_t right, uint16_t nbytes) {
    int count = ROUND_UP_8((int) nbytes) / 2;
    int16_t *l = dmem_s16(left);
    int16_t *r = dmem_s16(right);
    int16_t *d = dmem_s16(out);
    int16_t tmp[2048];
    int i;
    if (count > 1024) {
        count = 1024;
    }
    for (i = 0; i < count; i++) {
        tmp[i * 2] = l[i];
        tmp[i * 2 + 1] = r[i];
    }
    memcpy(d, tmp, (size_t) count * 4);
}

void port_aFilter(uint8_t flags, uint16_t countOrBuf, void *addr) {
    if (flags > A_INIT_FLAG) {
        sRsp.filterCount = (uint16_t) ROUND_UP_16((int) countOrBuf);
        memcpy(sRsp.filter, addr, sizeof(sRsp.filter));
    } else {
        int16_t *state = (int16_t *) addr;
        int16_t tmp[16];
        int count = sRsp.filterCount;
        int16_t *buf = dmem_s16(countOrBuf);
        int i, j;
        if (flags == A_INIT_FLAG) {
            memset(tmp, 0, sizeof(tmp));
        } else {
            memcpy(tmp, state, 8 * sizeof(int16_t));
        }
        while (count > 0) {
            memcpy(tmp + 8, buf, 8 * sizeof(int16_t));
            for (i = 0; i < 8; i++) {
                int64_t sample = 0x4000;
                for (j = 0; j < 8; j++) {
                    sample += (int64_t) tmp[i + 1 + j] * sRsp.filter[7 - j];
                }
                buf[i] = clamp16((int32_t) (sample >> 15));
            }
            memcpy(tmp, tmp + 8, 8 * sizeof(int16_t));
            buf += 8;
            count -= 8 * (int) sizeof(int16_t);
        }
        memcpy(state, tmp, 8 * sizeof(int16_t));
    }
}

void port_aHiLoGain(uint8_t gain, uint16_t nbytes, uint16_t buf) {
    int n = ROUND_UP_32((int) nbytes) / 2;
    int16_t *b = dmem_s16(buf);
    int i;
    for (i = 0; i < n; i++) {
        b[i] = clamp16((b[i] * (int32_t) gain) >> 4);
    }
}

void port_aUnknown25(uint8_t f, uint16_t nbytes, uint16_t out, uint16_t in) {
    int n = ROUND_UP_64((int) nbytes) / 2;
    int16_t *src = dmem_s16((uint32_t) in + f);
    int16_t *dst = dmem_s16(out);
    int16_t tbl[32];
    int i;
    memcpy(tbl, src, sizeof(tbl));
    for (i = 0; i < n; i++) {
        dst[i] = clamp16((dst[i] * (int32_t) tbl[i % 32]) >> 15);
    }
}
