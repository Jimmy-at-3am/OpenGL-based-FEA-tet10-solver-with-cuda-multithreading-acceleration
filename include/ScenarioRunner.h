// =============================================================================
//  ScenarioRunner.h  --  headless scenario runner.
//
//  A thin CLI wrapper over the EXACT functions the interactive UI calls
//  (FEAModel geometry/mesh + FEASolver solve). It loads a JSON scenario, runs
//  geometry -> mesh -> solve -> visualise, writes a machine-readable JSON report
//  (mesh stats, solver telemetry, fracture totals, probe values, screenshot
//  image statistics) plus PNG screenshots, and returns a meaningful exit code.
//
//  TOP RULE: never fork a parallel pipeline. Every stage here
//  calls the same FEAModel / FEASolver method the corresponding UI button does.
//
//  Exit codes:
//    0 = ran, all asserts pass
//    1 = ran, >= 1 assert failed
//    2 = could not run (parse error, missing file, GL init failure, exception)
// =============================================================================
#pragma once

#include <string>

namespace ScenarioRunner {

// Runs one scenario file. Creates a hidden GL context, executes the pipeline,
// writes `report.json` to outPath and PNGs to shotsDir (created if missing).
// Returns the harness exit code (0/1/2). A report is ALWAYS written when the
// scenario parsed far enough to run; parse/IO failures return 2 with a message
// to stderr (and, when outPath is set, a minimal error report).
int runScenario(const std::string& scenarioPath,
                const std::string& outPath,
                const std::string& shotsDir);

// Runs every scenarios/*.json plus existing regression/*.txt sentinels, prints
// a one-line PASS/FAIL table, and returns the aggregate exit code
// (0 only if all pass; 1 if any assert failed; 2 if any could not run).
int runRegress();

} // namespace ScenarioRunner
