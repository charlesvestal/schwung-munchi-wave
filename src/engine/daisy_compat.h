/* daisy_compat.h -- the slice of libDaisy that CHOMPI TAPE's DSP touches.
 *
 * The TAPE firmware's DSP sources (src/engine/tape/) were written against
 * libDaisy (MIT, Electrosmith). Munchi runs on Linux, so this header supplies
 * the handful of libDaisy names those files use, with the same semantics:
 *
 *   FIFO<T, N>         ring buffer (util/FIFO.h)
 *   System::GetNow()   millisecond clock -- here ENGINE time, not wall time
 *   f2s16 / s162f      the int16 conversions from daisy_core.h, verbatim
 *   WAV_FormatTypeDef  the 44-byte canonical WAV header (util/wav_format.h)
 *
 * System::GetNow() is driven by the engine's sample counter rather than the
 * wall clock, so every timing rule in the firmware (the looper's 10 ms button
 * debounce and 2 s clear hold, the voice cache timer) runs in audio time and
 * is deterministic under the offline test harness.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "daisysp/dsp.h"
#include "daisysp/adsr.h"
#include "daisysp/svf.h"
#include "daisysp/dcblock.h"
#include "daisysp/delayline.h"
#include "daisysp/oscillator.h"

#define S162F_SCALE 3.05185094759971922971282082583086642048402356028931546983245338297677541e-05f
#define F2S16_SCALE 32767.0f
#ifndef FBIPMIN
#define FBIPMIN -1.f
#define FBIPMAX 1.f
#endif

namespace daisy
{
inline float s162f(int32_t x) { return (float)x * S162F_SCALE; }

inline int32_t f2s16(float x)
{
    x = x <= FBIPMIN ? FBIPMIN : x;
    x = x >= FBIPMAX ? FBIPMAX : x;
    return (int32_t)(x * F2S16_SCALE);
}

/** Engine clock. Advanced by the engine once per processed block. */
struct System
{
    static uint32_t now_ms;
    static inline uint32_t GetNow() { return now_ms; }
};

/** Fixed-capacity FIFO with the libDaisy interface the firmware uses. */
template <typename T, size_t kCapacity>
class FIFO
{
  public:
    FIFO() : in_(0), out_(0), count_(0) {}
    void   Clear() { in_ = out_ = count_ = 0; }
    bool   IsEmpty() const { return count_ == 0; }
    bool   IsFull() const { return count_ == kCapacity; }
    size_t GetNumElements() const { return count_; }

    bool PushBack(const T &v)
    {
        if(count_ == kCapacity)
            return false;
        buf_[in_] = v;
        in_       = (in_ + 1) % kCapacity;
        count_++;
        return true;
    }

    T PopFront()
    {
        if(count_ == 0)
            return T();
        T v  = buf_[out_];
        out_ = (out_ + 1) % kCapacity;
        count_--;
        return v;
    }

    void PopFrontMany(size_t n)
    {
        if(n > count_)
            n = count_;
        out_ = (out_ + n) % kCapacity;
        count_ -= n;
    }

  private:
    T      buf_[kCapacity];
    size_t in_, out_, count_;
};

typedef struct
{
    uint32_t ChunkId;
    uint32_t FileSize;
    uint32_t FileFormat;
    uint32_t SubChunk1ID;
    uint32_t SubChunk1Size;
    uint16_t AudioFormat;
    uint16_t NbrChannels;
    uint32_t SampleRate;
    uint32_t ByteRate;
    uint16_t BlockAlign;
    uint16_t BitPerSample;
    uint32_t SubChunk2ID;
    uint32_t SubChunk2Size;
} WAV_FormatTypeDef;

} // namespace daisy

using daisy::f2s16;
using daisy::s162f;
