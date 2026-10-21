#ifndef PORT_AUDIO_ABI_H
#define PORT_AUDIO_ABI_H

#include <stdint.h>

void port_aClearBuffer(uint16_t addr, int nbytes);
void port_aSetBuffer(uint8_t flags, uint16_t in, uint16_t out, uint16_t nbytes);
void port_aDMEMMove(uint16_t in, uint16_t out, int nbytes);
void port_aLoadADPCM(int nbytes, const void *book);
void port_aSetLoop(void *state);
void port_aADPCMdec(uint8_t flags, void *state);
void port_aResample(uint8_t flags, uint16_t pitch, void *state);

void port_aEnvMixer(uint8_t flags, void *state);
void port_aInterleave(uint16_t left, uint16_t right);
void port_aLoadBuffer(const void *src);
void port_aSaveBuffer(void *dst);
void port_aMix(uint8_t flags, int16_t gain, uint16_t in, uint16_t out);
void port_aSetVolume(uint8_t flags, uint16_t v, uint16_t t, uint16_t r);
void port_aSetVolume32(uint8_t flags, uint16_t v, uint32_t rate);

void port_aS8Dec(uint8_t flags, void *state);
void port_aAddMixer(uint16_t count, uint16_t in, uint16_t out);
void port_aLoadBufferSh(const void *src, uint16_t dst, uint16_t nbytes);
void port_aSaveBufferSh(uint16_t src, void *dst, uint16_t nbytes);
void port_aDuplicate(uint8_t copies, uint16_t in, uint16_t out);
void port_aDMEMMove2(uint8_t times, uint16_t in, uint16_t out, uint16_t nbytes);
void port_aResampleZoh(uint16_t pitch, uint16_t startFract);
void port_aDownsampleHalf(uint16_t nSamples, uint16_t in, uint16_t out);
void port_aMixSh(uint16_t nbytes, int16_t gain, uint16_t in, uint16_t out);
void port_aEnvSetup1(uint8_t volWet, uint16_t rateWet, uint16_t rateLeft, uint16_t rateRight);
void port_aEnvSetup2(uint16_t volLeft, uint16_t volRight);
void port_aEnvMixerSh(uint16_t in, uint16_t nSamples, int swapReverb, int negLeft, int negRight, uint16_t dryLeft,
                      uint16_t dryRight, uint16_t wetLeft, uint16_t wetRight);
void port_aInterleaveSh(uint16_t out, uint16_t left, uint16_t right, uint16_t nbytes);
void port_aFilter(uint8_t flags, uint16_t countOrBuf, void *addr);
void port_aHiLoGain(uint8_t gain, uint16_t nbytes, uint16_t buf);
void port_aUnknown25(uint8_t f, uint16_t nbytes, uint16_t out, uint16_t in);

#undef aADPCMdec
#undef aPoleFilter
#undef aClearBuffer
#undef aEnvMixer
#undef aInterleave
#undef aLoadBuffer
#undef aMix
#undef aPan
#undef aResample
#undef aSaveBuffer
#undef aSegment
#undef aSetBuffer
#undef aSetVolume
#undef aSetLoop
#undef aDMEMMove
#undef aLoadADPCM
#undef aSetVolume32
#undef aS8Dec
#undef aAddMixer
#undef aDuplicate
#undef aDMEMMove2
#undef aResampleZoh
#undef aDownsampleHalf
#undef aEnvSetup1
#undef aEnvSetup2
#undef aFilter
#undef aHiLoGain
#undef aUnknown25

#define aADPCMdec(pkt, f, s) port_aADPCMdec((uint8_t) (f), (void *) (s))
#define aPoleFilter(pkt, f, g, s) do { } while (0)
#define aClearBuffer(pkt, d, c) port_aClearBuffer((uint16_t) (d), (int) (c))
#define aResample(pkt, f, p, s) port_aResample((uint8_t) (f), (uint16_t) (p), (void *) (s))
#define aSegment(pkt, s, b) do { } while (0)
#define aSetBuffer(pkt, f, i, o, c) port_aSetBuffer((uint8_t) (f), (uint16_t) (i), (uint16_t) (o), (uint16_t) (c))
#define aSetLoop(pkt, a) port_aSetLoop((void *) (a))
#define aDMEMMove(pkt, i, o, c) port_aDMEMMove((uint16_t) (i), (uint16_t) (o), (int) (c))
#define aLoadADPCM(pkt, c, d) port_aLoadADPCM((int) (c), (const void *) (d))

#if !defined(VERSION_SH) && !defined(VERSION_CN)

#define aEnvMixer(pkt, f, s) port_aEnvMixer((uint8_t) (f), (void *) (s))
#define aInterleave(pkt, l, r) port_aInterleave((uint16_t) (l), (uint16_t) (r))
#define aLoadBuffer(pkt, s) port_aLoadBuffer((const void *) (s))
#define aMix(pkt, f, g, i, o) port_aMix((uint8_t) (f), (int16_t) (g), (uint16_t) (i), (uint16_t) (o))
#define aPan(pkt, f, d, s) do { } while (0)
#define aSaveBuffer(pkt, s) port_aSaveBuffer((void *) (s))
#define aSetVolume(pkt, f, v, t, r) port_aSetVolume((uint8_t) (f), (uint16_t) (v), (uint16_t) (t), (uint16_t) (r))
#define aSetVolume32(pkt, f, v, tr) port_aSetVolume32((uint8_t) (f), (uint16_t) (v), (uint32_t) (tr))

#else

#define aS8Dec(pkt, f, s) port_aS8Dec((uint8_t) (f), (void *) (s))
#define aAddMixer(pkt, s, d, c) port_aAddMixer((uint16_t) (c), (uint16_t) (s), (uint16_t) (d))
#define aLoadBuffer(pkt, s, d, c) port_aLoadBufferSh((const void *) (s), (uint16_t) (d), (uint16_t) (c))
#define aSaveBuffer(pkt, s, d, c) port_aSaveBufferSh((uint16_t) (s), (void *) (d), (uint16_t) (c))
#define aDuplicate(pkt, s, d, c) port_aDuplicate((uint8_t) (c), (uint16_t) (s), (uint16_t) (d))
#define aDMEMMove2(pkt, t, i, o, c) port_aDMEMMove2((uint8_t) (t), (uint16_t) (i), (uint16_t) (o), (uint16_t) (c))
#define aResampleZoh(pkt, pitch, startFract) port_aResampleZoh((uint16_t) (pitch), (uint16_t) (startFract))
#define aDownsampleHalf(pkt, nSamples, i, o) port_aDownsampleHalf((uint16_t) (nSamples), (uint16_t) (i), (uint16_t) (o))
#define aMix(pkt, g, i, o, c) port_aMixSh((uint16_t) (c), (int16_t) (g), (uint16_t) (i), (uint16_t) (o))
#define aEnvSetup1(pkt, initialVolReverb, rampReverb, rampLeft, rampRight) \
    port_aEnvSetup1((uint8_t) (initialVolReverb), (uint16_t) (rampReverb), (uint16_t) (rampLeft), (uint16_t) (rampRight))
#define aEnvSetup2(pkt, initialVolLeft, initialVolRight) \
    port_aEnvSetup2((uint16_t) (initialVolLeft), (uint16_t) (initialVolRight))
#define aEnvMixer(pkt, inBuf, nSamples, swapReverb, negLeft, negRight, dryLeft, dryRight, wetLeft, wetRight) \
    port_aEnvMixerSh((uint16_t) (inBuf), (uint16_t) (nSamples), (swapReverb), (negLeft), (negRight), \
                     (uint16_t) (dryLeft), (uint16_t) (dryRight), (uint16_t) (wetLeft), (uint16_t) (wetRight))
#define aInterleave(pkt, o, l, r, c) port_aInterleaveSh((uint16_t) (o), (uint16_t) (l), (uint16_t) (r), (uint16_t) (c))
#define aFilter(pkt, f, countOrBuf, addr) port_aFilter((uint8_t) (f), (uint16_t) (countOrBuf), (void *) (addr))
#define aHiLoGain(pkt, g, buflen, i) port_aHiLoGain((uint8_t) (g), (uint16_t) (buflen), (uint16_t) (i))
#define aUnknown25(pkt, f, c, o, i) port_aUnknown25((uint8_t) (f), (uint16_t) (c), (uint16_t) (o), (uint16_t) (i))

#endif

#endif
