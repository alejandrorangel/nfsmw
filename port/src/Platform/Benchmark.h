// Native benchmark mode (NFSMW_BENCHMARK=<scenario> or --benchmark <scenario>).
// See BenchmarkNative.cpp; the Mac side (lockstep presents, GPU times,
// screenshots) is in Mac/BenchmarkMac.mm.
#pragma once

#include <cstdint>

// Main thread, first thing in main(): reads the scenario from the arguments
// or the environment. Everything else is a no-op unless this found one.
void PortBenchmarkConfigure(int argc, char **argv);
bool PortBenchmarkActive();

// Game thread, before the game's main: the SkipFE switches for the scenario.
void PortBenchmarkInit();

// Main loop: the fixed real time step of a frame in ms (1000/60), 0 when off.
float PortBenchmarkFixedFrameMs();
// The display's vsync for the benchmark (off unless NFSMW_BENCHMARK_VSYNC=1).
bool PortBenchmarkVSync(bool requested);

// The real keyboard/mouse/pad are replaced by the scenario's scripted input
// (InputMac.mm); false when the benchmark is off.
struct PortInputState;
bool PortBenchmarkInput(PortInputState *state);
// The scenario's typed characters (WM_CHAR codes), like PortInputTakeChars.
int PortBenchmarkTakeChars(int *out, int max);

// eDisplayFrame, after the frame's draws are published; render_start_ns is
// when the frame's render build began (CLOCK_UPTIME_RAW).
void PortBenchmarkEndFrame(uint64_t render_start_ns);

// Mac side (BenchmarkMac.mm).
void PortMacBenchmarkSetupView(void *mtk_view);      // main thread: the game's MTKView
void PortMacBenchmarkDrawFrame(int frame, const char *png_path); // game thread: draw this frame now
bool PortMacBenchmarkGPUTime(int frame, double *ms); // after PortMacBenchmarkFinish
bool PortMacBenchmarkEncodeTime(int frame, double *ms); // the main thread's draw; after PortMacBenchmarkFinish
// Game thread, before it publishes a frame: waits until the renderer has
// taken the last one (it is encoded while the next one is built).
void PortMacBenchmarkBeforePublish();
void PortMacBenchmarkFinish();                       // waits for the GPU and the PNG writes
