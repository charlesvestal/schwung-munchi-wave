/* wave_card.cpp -- see wave_card.h. */
#include "wave_card.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <strings.h>
#include <algorithm>
#include <vector>

namespace munchi
{

static std::string read_file(const std::string &p)
{
    std::string s;
    if(FILE *f = fopen(p.c_str(), "rb"))
    {
        char   buf[65536];
        size_t n;
        while((n = fread(buf, 1, sizeof(buf), f)) > 0)
            s.append(buf, n);
        fclose(f);
    }
    return s;
}

/* ---- presets.json (PresetManager.h, v3) ----------------------------------- */

void WavePresets::Init(const float *defaults)
{
    for(int s = 0; s < 15; s++)
    {
        slotValid[s] = false;
        for(int c = 0; c < kControls; c++)
            slotValues[s][c] = defaults[c];
    }
    updated = false;
}

bool WavePresets::IsValid(size_t slot)
{
    slot -= 1;
    if(slot >= kSlots)
        return false;
    return slotValid[slot];
}

float WavePresets::GetValue(size_t slot, size_t control)
{
    slot -= 1;
    if(slot > kSlots || control >= kControls)
        return 0xff;
    if(!slotValid[slot])
        return 0xff;
    return slotValues[slot][control];
}

void WavePresets::SetValue(float value, size_t slot, size_t control)
{
    slot -= 1;
    if(slot > kSlots || control >= kControls)
        return;
    slotValues[slot][control] = value;
}

void WavePresets::Invalidate(uint8_t slot)
{
    slot -= 1;
    if(slot >= kSlots)
        return;
    slotValid[slot] = false;
    updated         = true;
}

void WavePresets::Save(size_t slot)
{
    updated             = true;
    slotValid[slot - 1] = true;
}

void WavePresets::Copy(uint8_t src, uint8_t dst)
{
    dst -= 1;
    src -= 1;
    if(src > kSlots || dst > kSlots)
        return;
    for(int i = 0; i < kControls; i++)
        slotValues[dst][i] = slotValues[src][i];
    slotValid[dst] = slotValid[src];
    updated        = true;
}

bool WavePresets::Parse(const char *json)
{
    struct Tok
    {
        int   depth;
        bool  is_bool;
        float num;
        int   inum;
    };
    std::vector<Tok> toks;
    int              depth = 0;
    for(const char *p = json; *p; p++)
    {
        if(*p == '[')
            depth++;
        else if(*p == ']')
            depth--;
        else if(*p == 't' || *p == 'f')
        {
            toks.push_back({depth, true, *p == 't' ? 1.f : 0.f, 0});
            while(*p && *p != ',' && *p != ']')
                p++;
            p--;
        }
        else if(*p == '-' || (*p >= '0' && *p <= '9'))
        {
            char *e;
            float v = strtof(p, &e);
            toks.push_back({depth, false, v, atoi(p)});
            p = e - 1;
        }
    }
    int    ctrl = (toks.size() && toks.back().depth == 1) ? kControls : 7;
    size_t idx  = 0;
    for(int s = 0; s < kSlots; s++)
    {
        if(idx + ctrl + 1 > toks.size())
            return false;
        bool v = toks[idx + ctrl].is_bool && toks[idx + ctrl].num > 0.f;
        if(v)
            for(int c = 0; c < ctrl; c++)
            {
                if(c == 0) // the firmware rounds pitch down a hair (nextafterf)
                    slotValues[s][c] = nextafterf(toks[idx + c].inum * 0.001f, -INFINITY);
                else if(c != 1 && c != 2 && c != 13)
                    slotValues[s][c] = .001f * toks[idx + c].num;
                else
                    slotValues[s][c] = toks[idx + c].num;
            }
        slotValid[s] = v;
        idx += ctrl + 1;
    }
    return true;
}

std::string WavePresets::Serialize() const
{
    std::string out = "[";
    char        tmp[32];
    for(int s = 0; s < kSlots; s++)
    {
        out += "[";
        for(int c = 0; c < kControls; c++)
        {
            if(c != 1 && c != 2 && c != 13)
                snprintf(tmp, sizeof(tmp), "%d,", int(slotValues[s][c] * 1000));
            else
                snprintf(tmp, sizeof(tmp), "%d,", int(slotValues[s][c]));
            out += tmp;
        }
        out += slotValid[s] ? "true]," : "false],";
    }
    out += "3]";
    return out;
}

/* One wavetable: WAVE read 33 x 2048 float32 raw from byte 136 of the file.
 * This finds the "data" chunk instead, so a table saved by any editor loads,
 * and converts 16/24/32-bit PCM as well as float. Missing frames are silent,
 * as a short read was on the hardware. */
bool LoadWavetable(const std::string &path, float (*dst)[MAX_SAMPLES_PER_CYCLE])
{
    std::string s = read_file(path);
    if(s.size() < 44 || memcmp(s.data(), "RIFF", 4) || memcmp(s.data() + 8, "WAVE", 4))
        return false;
    const uint8_t *b = (const uint8_t *)s.data();
    int            fmt = 3, ch = 1, bits = 32;
    size_t         off = 12, data = 0, data_len = 0;
    auto rd16 = [&](size_t o) { return (int)(b[o] | (b[o + 1] << 8)); };
    auto rd32 = [&](size_t o) {
        return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16)
               | ((uint32_t)b[o + 3] << 24);
    };
    while(off + 8 <= s.size())
    {
        uint32_t sz = rd32(off + 4);
        if(!memcmp(b + off, "fmt ", 4) && off + 24 <= s.size())
        {
            fmt  = rd16(off + 8);
            ch   = rd16(off + 10);
            bits = rd16(off + 22);
            if(fmt == 0xFFFE && off + 34 <= s.size())
                fmt = rd16(off + 32);
        }
        else if(!memcmp(b + off, "data", 4))
        {
            data     = off + 8;
            data_len = std::min((size_t)sz, s.size() - data);
            break;
        }
        off += 8 + sz + (sz & 1);
    }
    if(!data || ch < 1 || !(fmt == 1 || fmt == 3))
        return false;
    const size_t bps = bits / 8, frames = data_len / (bps * ch);
    for(size_t i = 0; i < (size_t)CYCLES * MAX_SAMPLES_PER_CYCLE; i++)
    {
        float v = 0.f;
        if(i < frames)
        {
            const uint8_t *p = b + data + i * bps * ch;
            if(fmt == 3 && bits == 32)
                memcpy(&v, p, 4);
            else if(bits == 16)
                v = (int16_t)rd16(p - b) / 32768.f;
            else if(bits == 24)
                v = ((int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16
                               | (uint32_t)p[2] << 24) >> 8) / 8388608.f;
            else if(bits == 32)
                v = (int32_t)rd32(p - b) / 2147483648.f;
        }
        dst[i / MAX_SAMPLES_PER_CYCLE][i % MAX_SAMPLES_PER_CYCLE] = v;
    }
    return true;
}

int LoadWavetableFolder(const std::string &dir, float (*mem)[MAX_SAMPLES_PER_CYCLE], int max_tables)
{
    std::vector<std::string> names;
    if(DIR *d = opendir(dir.c_str()))
    {
        while(struct dirent *e = readdir(d))
        {
            const char *n = e->d_name;
            size_t      l = strlen(n);
            if(n[0] != '.' && l > 4 && !strcasecmp(n + l - 4, ".wav"))
                names.push_back(n);
        }
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    int loaded = 0;
    for(size_t i = 0; i < names.size() && loaded < max_tables; i++)
        if(LoadWavetable(dir + "/" + names[i], &mem[loaded * CYCLES]))
            loaded++;
    return loaded;
}

} // namespace munchi
