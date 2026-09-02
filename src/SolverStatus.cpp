#include "SolverStatus.h"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace SolverStatus {
namespace {

// Keep recent completed history without allowing telemetry to grow without
// bound. Unfinished stages are never discarded; if all slots are occupied by
// unfinished work, enqueue() declines the new row and returns -1.
constexpr size_t kMaxStages = 96;

std::mutex          g_mtx;
std::vector<Stage>  g_stages;
std::string         g_runLabel;
int                 g_nextId = 1;
std::chrono::steady_clock::time_point g_t0 = std::chrono::steady_clock::now();

double elapsedLocked() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count();
}

Stage* findLocked(int id) {
    for (auto& s : g_stages)
        if (s.id == id) return &s;
    return nullptr;
}

bool finished(State s) {
    return s == State::Done || s == State::Failed || s == State::Cancelled;
}

void pruneFinishedLocked(size_t targetSize) {
    if (g_stages.size() <= targetSize) return;
    const size_t excess = g_stages.size() - targetSize;
    size_t dropped = 0;
    g_stages.erase(
        std::remove_if(g_stages.begin(), g_stages.end(),
                       [&](const Stage& s) {
                           if (dropped >= excess || !finished(s.state)) return false;
                           ++dropped;
                           return true;
                       }),
        g_stages.end());
}

void finishAllLocked(State state) {
    const double t = elapsedLocked();
    for (auto& s : g_stages) {
        if (finished(s.state)) continue;
        if (s.state == State::Queued) s.tStart = t;
        s.state = state;
        s.tEnd  = t;
    }
}

} // namespace

void reset(const std::string& runLabel) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_stages.clear();
    g_runLabel = runLabel;
    g_t0       = std::chrono::steady_clock::now();
    g_nextId   = 1;
}

int enqueue(const std::string& label, int depth, Device dev) {
    std::lock_guard<std::mutex> lk(g_mtx);
    pruneFinishedLocked(kMaxStages - 1);
    if (g_stages.size() >= kMaxStages) return -1;
    const int id = g_nextId++;
    Stage s;
    s.id     = id;
    s.depth  = depth;
    s.label  = label;
    s.device = dev;
    s.state  = State::Queued;
    g_stages.push_back(std::move(s));
    return id;
}

void begin(int id, int threads) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (Stage* s = findLocked(id)) {
        s->state    = State::Active;
        s->threads  = threads;
        s->tStart   = elapsedLocked();
        s->progress = -1.0f;
    }
}

int beginNow(const std::string& label, int depth, Device dev, int threads) {
    const int id = enqueue(label, depth, dev);
    begin(id, threads);
    return id;
}

void progress(int id, float frac) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (Stage* s = findLocked(id)) s->progress = frac;
}

void detail(int id, const std::string& text) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (Stage* s = findLocked(id)) s->detail = text;
}

void end(int id, State st) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (Stage* s = findLocked(id)) {
        // A stage that was never begun (cancelled while still queued) has no
        // start stamp; give it a zero-length span rather than a bogus one.
        const double t = elapsedLocked();
        if (s->state == State::Queued) s->tStart = t;
        s->state = st;
        s->tEnd  = t;
        if (st == State::Done && s->progress >= 0.0f) s->progress = 1.0f;
    }
}

void cancelAll() {
    std::lock_guard<std::mutex> lk(g_mtx);
    finishAllLocked(State::Cancelled);
}

void failAll() {
    std::lock_guard<std::mutex> lk(g_mtx);
    finishAllLocked(State::Failed);
}

std::vector<Stage> snapshot(std::string* runLabel, double* runElapsed) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (runLabel)   *runLabel   = g_runLabel;
    if (runElapsed) *runElapsed = elapsedLocked();
    return g_stages;
}

double now() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return elapsedLocked();
}

} // namespace SolverStatus
