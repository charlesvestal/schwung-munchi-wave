/* render.cpp -- offline harness for the ported TAPE engine.
 *
 * Loads a WAVE card folder exactly as the device does, then
 * drives the engine with a scripted sequence and writes the line-out mix to
 * a WAV. Built and run on the development host; nothing here touches SPI.
 *
 *   render <card_dir> <script> <out.wav>
 *
 * The script is one command per line, times in milliseconds of engine time:
 *   <ms> mode keys|kit      <ms> bank <0-4>        <ms> slot <1-15>
 *   <ms> on <key> [vel]     <ms> off <key>         (key = CHOMPI button id)
 *   <ms> note <midi> [vel]  <ms> noteoff <midi>    (MIDI-in path, 48..72)
 *   <ms> set <name> <0..1>  <ms> end
 */
#include "../src/engine/munchi_wave.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <math.h>
#include <unistd.h>

using namespace munchi;

int main(int argc, char **argv)
{
    if(argc < 4)
    {
        fprintf(stderr, "usage: render <card_dir> <script> <out.wav>\n");
        return 2;
    }
    MunchiWave *eng = new MunchiWave();
    if(eng->Init() != 0)
    {
        fprintf(stderr, "engine init failed\n");
        return 1;
    }
    // argv[1] is a card folder: copied to a scratch card (MUNCHI_CARD, or
    // the folder itself) the way the device seeds its card
    const char *card = getenv("MUNCHI_CARD");
    eng->StartCard(card ? card : argv[1], card ? argv[1] : "");
    fprintf(stderr, "tables loaded: %d\n", eng->TablesLoaded());

    FILE *sf = fopen(argv[2], "r");
    if(!sf)
    {
        fprintf(stderr, "no script\n");
        return 1;
    }
    struct Cmd
    {
        uint32_t ms;
        char     op[32];
        char     a[64];
        float    b;
    };
    std::vector<Cmd> cmds;
    char             line[256];
    while(fgets(line, sizeof(line), sf))
    {
        Cmd c;
        memset(&c, 0, sizeof(c));
        c.b = -1.f;
        if(line[0] == '#' || line[0] == '\n')
            continue;
        if(sscanf(line, "%u %31s %63s %f", &c.ms, c.op, c.a, &c.b) >= 2)
            cmds.push_back(c);
    }
    fclose(sf);

    std::vector<int16_t> out;
    size_t               ci = 0;
    uint32_t             end_ms = cmds.empty() ? 1000 : cmds.back().ms;
    // run in Move-sized blocks (128 frames at 44.1k) like the device
    int16_t  in_block[128 * 2] = {0};
    double   tone_hz = 0.0, tone_ph = 0.0;
    int16_t  out_block[128 * 2];
    double   t_ms = 0.0;
    while(t_ms <= end_ms)
    {
        while(ci < cmds.size() && cmds[ci].ms <= t_ms)
        {
            Cmd &c = cmds[ci++];
            if(!strcmp(c.op, "end"))
                break;
            if(!strcmp(c.op, "tone"))
                tone_hz = atof(c.a);
            else if(!strcmp(c.op, "wait"))
                usleep((useconds_t)(atof(c.a) * 1000)); // let the worker run
            else
                eng->ScriptCommand(c.op, c.a, c.b);
        }
        for(int i = 0; i < 128; i++)
        {
            int16_t v = tone_hz > 0 ? (int16_t)(sin(tone_ph) * 8000) : 0;
            tone_ph += 2 * M_PI * tone_hz / 44100.0;
            in_block[i * 2] = in_block[i * 2 + 1] = v;
        }
        eng->ProcessHostBlock(in_block, out_block, 128);
        out.insert(out.end(), out_block, out_block + 256);
        t_ms += 128.0 * 1000.0 / 44100.0;
    }

    eng->StopCard();

    // write 44.1k output (header rate 44100)
    FILE *of = fopen(argv[3], "wb");
    uint32_t data_len = (uint32_t)(out.size() * 2);
    uint8_t  h[44] = {'R', 'I', 'F', 'F'};
    uint32_t v;
    v = 36 + data_len; memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    uint16_t s = 1; memcpy(h + 20, &s, 2);
    s = 2; memcpy(h + 22, &s, 2);
    v = 44100; memcpy(h + 24, &v, 4);
    v = 44100 * 4; memcpy(h + 28, &v, 4);
    s = 4; memcpy(h + 32, &s, 2);
    s = 16; memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data_len, 4);
    fwrite(h, 1, 44, of);
    fwrite(out.data(), 2, out.size(), of);
    fclose(of);

    int16_t peak = 0;
    double  sum  = 0;
    for(int16_t x : out)
    {
        int16_t a = x < 0 ? -x : x;
        if(a > peak)
            peak = a;
        sum += (double)x * x;
    }
    fprintf(stderr, "frames=%zu peak=%d (%.1f dBFS) rms=%.1f dBFS\n",
            out.size() / 2, peak, 20 * log10((peak + 1) / 32768.0),
            10 * log10(sum / (out.size() ? out.size() : 1) / (32768.0 * 32768.0) + 1e-12));
    return 0;
}
