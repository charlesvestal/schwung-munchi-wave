/* surface_test.cpp -- Move controls in, WAVE state out, no device.
 * Turns every knob on both pages (and the Shift gestures) through Surface as
 * the SPI loop delivers them, checks the labelled parameter moved, and prints
 * the screen as ASCII. */
#include "../src/standalone/surface.h"
#include <stdio.h>

using namespace munchi;

static MunchiWave eng;
static int16_t in[256], out[256];

static void run_ms(int ms)
{
    for(int i = 0; i < ms * 441 / 1280 + 1; i++)
        eng.ProcessHostBlock(in, out, 128);
}

static void dump(Surface &s)
{
    const uint8_t *px = s.TestScreen();
    for(int y = 0; y < 64; y += 2)
    {
        for(int x = 0; x < 128; x++)
        {
            int a = px[y * 128 + x], b = px[(y + 1) * 128 + x];
            putchar(a && b ? '#' : a ? '"' : b ? '.' : ' ');
        }
        putchar('\n');
    }
}

int main(int argc, char **argv)
{
    eng.Init();
    eng.StartCard(argc > 2 ? argv[2] : argv[1], argc > 2 ? argv[1] : "");
    Surface s(eng);
    run_ms(1600);
    int fails = 0;
    auto turn = [&](int knob, int d) {
        s.HandleInternal(0xB0, knob == 8 ? 79 : 71 + knob, d > 0 ? d : 128 + d);
        run_ms(20);
    };
    auto expect = [&](const char *what, float a, float b) {
        bool ok = a != b;
        printf("%-24s %.3f -> %.3f %s\n", what, a, b, ok ? "ok" : "DID NOT MOVE");
        fails += !ok;
    };
    myEngine &x = eng.Eng();
    float v;
    v = x.getTable(); turn(0, 4); expect("p1 k1 table", v, x.getTable());
    v = x.getCycle(); turn(1, 5); expect("p1 k2 frame", v, x.getCycle());
    v = eng.EncValue(1, 3); turn(2, 5); expect("p1 k3 filter", v, eng.EncValue(1, 3));
    v = eng.Resonance(); turn(3, -5); expect("p1 k4 resonance", v, eng.Resonance());
    v = eng.EncValue(0, 1); turn(4, 5); expect("p1 k5 attack", v, eng.EncValue(0, 1));
    v = eng.EncValue(0, 2); turn(5, 5); expect("p1 k6 release", v, eng.EncValue(0, 2));
    v = eng.EncValue(0, 3); turn(6, 5); expect("p1 k7 space", v, eng.EncValue(0, 3));
    v = eng.Clock().getTempo(); turn(7, 5); expect("p1 k8 tempo", v, eng.Clock().getTempo());
    v = eng.EncValue(0, 5); turn(8, -5); expect("volume knob", v, eng.EncValue(0, 5));
    dump(s);
    s.HandleInternal(0xB0, 63, 127);
    v = eng.EncValue(0, 0); turn(0, 5); expect("p2 k1 pitch", v, eng.EncValue(0, 0));
    v = eng.EncValue(1, 1); turn(1, 5); expect("p2 k2 pitch lfo", v, eng.EncValue(1, 1));
    v = eng.PitchLfoRate(); turn(2, 5); expect("p2 k3 pitch lfo rate", v, eng.PitchLfoRate());
    v = eng.EncValue(1, 2); turn(3, 5); expect("p2 k4 filter lfo", v, eng.EncValue(1, 2));
    v = eng.FilterLfoRate(); turn(4, 5); expect("p2 k5 filter lfo rate", v, eng.FilterLfoRate());
    v = eng.DelayTime(); turn(5, 5); expect("p2 k6 delay time", v, eng.DelayTime());
    v = eng.EncValue(1, 5); turn(6, 5); expect("p2 k7 pan", v, eng.EncValue(1, 5));
    v = eng.FinalComp(); turn(7, 5); expect("p2 k8 comp", v, eng.FinalComp());
    dump(s);
    s.HandleInternal(0xB0, 49, 127);
    run_ms(20);
    v = eng.EncValue(0, 0); turn(0, 10); expect("shift k1 pitch step", v, eng.EncValue(0, 0));
    s.HandleInternal(0xB0, 62, 127);
    v = eng.Clock().divPos(); turn(7, 12); run_ms(400); expect("shift k8 step length", v, eng.Clock().divPos());
    s.HandleInternal(0xB0, 49, 0);
    printf("%s\n", fails ? "FAILED" : "all knobs move what they say");
    return fails;
}
