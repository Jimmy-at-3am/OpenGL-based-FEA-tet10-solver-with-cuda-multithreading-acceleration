#pragma once

#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// SolverStatus — live "what is the solver doing right now" telemetry.
// -----------------------------------------------------------------------------
// A single global registry of solve stages, modelled on the COMSOL progress
// window: a shallow tree of named stages, each with its own progress, the
// device it runs on, and a wall-clock duration. Stages are registered QUEUED
// up-front so the viewport can show what is still coming, then flipped ACTIVE
// and DONE as the solver walks the pipeline.
//
// Threading: the compute worker writes, the render thread snapshots once a
// frame. Every entry point takes the internal mutex, so all of them are safe
// from any thread. Writes are rare (a few hundred per solve) so the lock is
// never contended in practice.
//
// The headless ScenarioRunner links this too; nothing there reads the registry,
// so the calls just maintain a small bounded vector and cost nothing.
//
// NOTE ON LABELS: SimpleUI's vector font only renders A-Z 0-9 space . : - _ % /
// and >. Any other character advances the cursor but draws nothing, so stage
// labels must stay inside that set or they will render as gaps.
// -----------------------------------------------------------------------------
namespace SolverStatus {

enum class Device { None, CPU, GPU };
enum class State  { Queued, Active, Done, Failed, Cancelled };

struct Stage {
    int         id       = -1;
    int         depth    = 0;       // 0 = top level, nests downward
    std::string label;
    std::string detail;             // free-form right-hand annotation
    Device      device   = Device::None;
    State       state    = State::Queued;
    float       progress = -1.0f;   // <0 = indeterminate
    int         threads  = 0;       // OpenMP team size; 0 = not applicable
    double      tStart   = 0.0;     // seconds since reset(); valid once Active
    double      tEnd     = 0.0;     // valid once finished
};

// Drops every stage and restarts the run clock. Called when a compute job starts.
void reset(const std::string& runLabel);

// Registers a stage in QUEUED state and returns its id.
int  enqueue(const std::string& label, int depth, Device dev);

// QUEUED -> ACTIVE, stamping the start time. threads = OpenMP team size (0 = n/a).
void begin(int id, int threads = 0);

// enqueue() + begin() for stages that only become known once reached.
int  beginNow(const std::string& label, int depth, Device dev, int threads = 0);

void progress(int id, float frac);            // <0 = indeterminate
void detail(int id, const std::string& text);
void end(int id, State st = State::Done);

// Marks every unfinished stage CANCELLED (used when the user hits the X).
void cancelAll();

// Marks every unfinished stage FAILED (used when a solve exits early).
void failAll();

// Render-thread view. Both out-params are optional.
std::vector<Stage> snapshot(std::string* runLabel, double* runElapsed);

// Seconds since the last reset().
double now();

} // namespace SolverStatus
