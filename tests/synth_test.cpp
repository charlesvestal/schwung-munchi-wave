/* synth_test.cpp -- the Munchi Wave sound generator, driven as the chain host
 * drives it: create, wait for the card, read the contract, play, render,
 * round-trip the state. Writes the audio to a WAV.
 *
 *   synth_test <module dir holding card/> <out.wav>
 */
#include "../src/synth/wave_synth.cpp"
#include <unistd.h>
#include <vector>

static int fails = 0;
static void check(const char *what, bool ok)
{
    printf("%-36s %s\n", what, ok ? "ok" : "FAILED");
    fails += !ok;
}

int main(int argc, char **argv)
{
    if(argc < 3)
        return 2;
    plugin_api_v2_t *api = move_plugin_init_v2(nullptr);
    void            *inst = api->create_instance(argv[1], nullptr);
    std::vector<int16_t> audio;
    int16_t              block[256];
    auto render = [&](int ms) {
        for(int i = 0; i < ms * 441 / 1280 + 1; i++)
        {
            api->render_block(inst, block, 128);
            audio.insert(audio.end(), block, block + 256);
        }
    };
    // a state restore arrives before the card is in (the host does this)
    api->set_param(inst, "filter", "0.300");
    for(int i = 0; i < 200 && !((WaveSynth *)inst)->loaded.load(); i++)
        usleep(10000);
    check("card loaded", ((WaveSynth *)inst)->loaded.load());
    render(50);

    static char buf[65536];
    int n = api->get_param(inst, "chain_params", buf, sizeof(buf));
    check("chain_params answers", n > 0);
    FILE *f = fopen((std::string(argv[2]) + ".chain_params.json").c_str(), "w");
    if(f) { fwrite(buf, 1, n, f); fclose(f); }
    n = api->get_param(inst, "ui_hierarchy", buf, sizeof(buf));
    check("ui_hierarchy answers", n > 0);
    f = fopen((std::string(argv[2]) + ".ui_hierarchy.json").c_str(), "w");
    if(f) { fwrite(buf, 1, n, f); fclose(f); }
    api->get_param(inst, "preset_count", buf, sizeof(buf));
    printf("presets: %s\n", buf);
    check("factory presets parsed", atoi(buf) > 1);
    api->get_param(inst, "filter", buf, sizeof(buf));
    check("early write kept", fabsf(atof(buf) - .3f) < 1e-3);

    // play: a chord, then a single note on preset 3
    uint8_t on[3] = {0x90, 60, 100}, off[3] = {0x80, 60, 0};
    for(int nn : {60, 64, 67})
    {
        on[1] = (uint8_t)nn;
        api->on_midi(inst, on, 3, 0);
    }
    render(800);
    for(int nn : {60, 64, 67})
    {
        off[1] = (uint8_t)nn;
        api->on_midi(inst, off, 3, 0);
    }
    render(400);
    api->set_param(inst, "preset", "3");
    api->get_param(inst, "preset_name", buf, sizeof(buf));
    printf("preset 3: %s\n", buf);
    on[1] = 48;
    api->on_midi(inst, on, 3, 0);
    render(1000);
    off[1] = 48;
    api->on_midi(inst, off, 3, 0);
    render(500);

    // state round trip
    api->set_param(inst, "table", "4");
    api->set_param(inst, "space", "0.6");
    api->set_param(inst, "pitch_lfo_on", "Off");
    n = api->get_param(inst, "state", buf, sizeof(buf));
    std::string state(buf, n);
    printf("state: %s\n", state.c_str());
    void *inst2 = api->create_instance(argv[1], nullptr);
    api->set_param(inst2, "state", state.c_str());
    for(int i = 0; i < 200 && !((WaveSynth *)inst2)->loaded.load(); i++)
        usleep(10000);
    api->render_block(inst2, block, 128);
    n = api->get_param(inst2, "state", buf, sizeof(buf));
    check("state round-trips", std::string(buf, n) == state);
    WaveSynth *w2 = (WaveSynth *)inst2;
    check("table applied to the engine", w2->engine.getTable() == 3);
    check("lfo switch applied", !w2->engine.getPitchLfoOn());
    api->destroy_instance(inst2);
    api->destroy_instance(inst);

    // peak / rms
    int16_t pk = 0;
    double  sum = 0;
    for(int16_t x : audio)
    {
        int16_t a = x < 0 ? -x : x;
        pk  = a > pk ? a : pk;
        sum += (double)x * x;
    }
    printf("peak %.1f dBFS, rms %.1f dBFS\n", 20 * log10((pk + 1) / 32768.0),
           10 * log10(sum / audio.size() / (32768.0 * 32768.0) + 1e-12));
    check("it makes sound", pk > 1000);
    FILE *of = fopen(argv[2], "wb");
    uint32_t dl = audio.size() * 2, v;
    uint8_t  h[44] = {'R', 'I', 'F', 'F'};
    v = 36 + dl; memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    uint16_t s16 = 1; memcpy(h + 20, &s16, 2);
    s16 = 2; memcpy(h + 22, &s16, 2);
    v = 44100; memcpy(h + 24, &v, 4);
    v = 44100 * 4; memcpy(h + 28, &v, 4);
    s16 = 4; memcpy(h + 32, &s16, 2);
    s16 = 16; memcpy(h + 34, &s16, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &dl, 4);
    fwrite(h, 1, 44, of);
    fwrite(audio.data(), 2, audio.size(), of);
    fclose(of);
    printf("%s\n", fails ? "FAILED" : "synth ok");
    return fails;
}
