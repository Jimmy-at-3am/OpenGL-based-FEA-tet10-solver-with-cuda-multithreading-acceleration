# PolyFEA Desktop Telemetry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add optional, privacy-conscious PolyFEA account linking and reliable launch/successful-simulation recording to the Windows desktop app without coupling networking to FEA computation or changing any solver behavior.

**Architecture:** New C++17 telemetry modules own credentials, queueing, HTTP, client state, and the small account overlay. `main.cpp` supplies only lifecycle facts: interactive process start and successful completion of explicitly countable simulations. Credentials use Windows DPAPI; automatic events use an idempotent JSONL queue and background WinHTTP delivery. Unlinked use remains fully functional and uncounted.

**Tech Stack:** C++17, Windows DPAPI/Crypt32, WinHTTP, nlohmann/json already used by the repository, CMake/CTest, existing SimpleUI/OpenGL frontend.

## Global Constraints

- Read and preserve `docs/superpowers/specs/2026-09-02-polyfea-website-telemetry-design.md`.
- Do not change FEA algorithms, solver inputs, success criteria, mesh generation, model formats, or headless behavior.
- The FEA domain layer must not include telemetry headers or know about accounts, HTTP, Firebase, Cloudflare, tokens, or queues.
- Telemetry initializes only inside `runInteractive()`. `--run`, `--regress`, and `--dump-ui` must perform zero telemetry I/O.
- No link means no events are created, persisted, or sent. Linking during a running interactive session creates at most one `app_launch` for that process.
- Count `simulation_completed` only when a job is explicitly marked `countsAsSimulation`, `okResult == true`, and `wasCancelled == false`.
- Never record model names, paths, geometry, material values, loads, mesh sizes, solver settings, results, IP addresses, or free-form notes in automatic desktop events.
- Keep the device token in a DPAPI-protected file and the event queue in a separate file under `%LOCALAPPDATA%\PolyFEA`.
- Do not package, upload, or publish the application in this plan.
- The separate Apple UI plan is not implemented yet. Execute this telemetry plan against the current UI first. When the Apple UI plan is implemented later, its control migration must preserve the single `drawTelemetryPanel` call and rehome only the `ACCOUNT` entry button into the new title bar.

---

### Task 1: Define Pure Telemetry Types and Event Semantics

**Files:**
- Create: `include/TelemetryTypes.h`
- Create: `src/TelemetryTypes.cpp`
- Create: `tests/telemetry_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `telemetry::EventType { AppLaunch, SimulationCompleted }`
- `telemetry::Event { eventId, type, occurredAt, appVersion }`
- `telemetry::LinkCode normalizeLinkCode(std::string_view)`
- `std::string serializeEvent(const Event&)`
- `std::optional<Event> parseEvent(std::string_view)`

- [ ] **Step 1: Register a failing GL-free CTest target**

Add `telemetry_tests` with only pure telemetry sources and nlohmann/json includes. Apply `/W4 /WX` on MSVC and `-Wall -Wextra -Wpedantic -Werror` elsewhere. Test invalid/valid UUIDs, exact event names, ISO timestamps, serialization round trip, and link-code normalization.

- [ ] **Step 2: Define the minimal wire representation**

Serialize exactly:

```json
{
  "eventId": "uuid",
  "type": "app_launch",
  "occurredAt": "UTC ISO-8601",
  "appVersion": "bounded string"
}
```

The only second event type is `simulation_completed`. Reject unknown fields when reading local queued events so corrupt lines are quarantined instead of silently broadened.

- [ ] **Step 3: Implement code normalization**

Trim ASCII whitespace, remove internal spaces/hyphens, uppercase, accept exactly eight characters from `23456789ABCDEFGHJKLMNPQRSTUVWXYZ`, and return an invalid result otherwise. This contains no UI or network dependency.

- [ ] **Step 4: Verify and commit pure types**

```powershell
& .\build.bat configure
& .\build.bat build
ctest --test-dir build -R telemetry_tests --output-on-failure
```

Expected: `telemetry_tests` passes and the main application still builds.

```powershell
git add CMakeLists.txt include/TelemetryTypes.h src/TelemetryTypes.cpp tests/telemetry_tests.cpp
git commit -m "Define desktop telemetry event contract"
```

---

### Task 2: Add Local Paths and DPAPI-Protected Device Credentials

**Files:**
- Create: `include/TelemetryTokenStore.h`
- Create: `src/TelemetryTokenStore.cpp`
- Modify: `tests/telemetry_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `telemetry::StoragePaths resolveStoragePaths()`
- `telemetry::TokenStore::save(DeviceCredential)`
- `telemetry::TokenStore::load(): std::optional<DeviceCredential>`
- `telemetry::TokenStore::clear()`

- [ ] **Step 1: Write failing storage tests using a temporary root**

Inject an explicit test directory. Cover missing credential, save/load round trip, replacement, clear, corrupt DPAPI blob, and queue-file separation. Assert diagnostic strings never contain the plaintext token.

- [ ] **Step 2: Resolve production paths safely**

Use `SHGetKnownFolderPath(FOLDERID_LocalAppData)` and create `%LOCALAPPDATA%\PolyFEA`. Use fixed filenames `device-credential.dpapi` and `events.jsonl`; quarantines use the fixed prefix `events.quarantine.` plus a UTC timestamp and `.jsonl`. Never derive a path from account email, UID, model name, or token.

- [ ] **Step 3: Protect and persist the credential**

Store `{ deviceToken, deviceId, linkedAt, apiBaseUrl }` as UTF-8 JSON encrypted with `CryptProtectData(..., CRYPTPROTECT_UI_FORBIDDEN)` and decrypt with `CryptUnprotectData`. Write to a sibling temporary file, flush, and replace atomically. Clear removes only the credential file; it does not delete unrelated application data.

- [ ] **Step 4: Link Windows libraries and verify**

Add `crypt32` and `shell32` only on `WIN32`. Run the focused CTest and main build. Expected: DPAPI round trip passes for the current Windows user and no plaintext token appears in the file.

- [ ] **Step 5: Commit protected storage**

```powershell
git add CMakeLists.txt include/TelemetryTokenStore.h src/TelemetryTokenStore.cpp tests/telemetry_tests.cpp
git commit -m "Protect linked device credentials with DPAPI"
```

---

### Task 3: Implement the Durable Idempotent JSONL Queue

**Files:**
- Create: `include/TelemetryQueue.h`
- Create: `src/TelemetryQueue.cpp`
- Modify: `tests/telemetry_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `Queue::append(const QueuedEvent&)`, where `QueuedEvent` contains a local-only `deviceSessionId` plus the wire `Event`
- `Queue::peekBatch(std::size_t maxEvents, std::size_t maxBytes)`
- `Queue::acknowledge(const std::vector<std::string>& eventIds)`
- `Queue::quarantineInvalidLines()`
- `Queue::clear()`

- [ ] **Step 1: Write failing crash/retry tests**

Cover ordered append, restart recovery, batches capped at 100 events and 64 KiB, partial acknowledgement, duplicate local event ID rejection, truncated final line recovery, corrupt middle-line quarantine, and atomic queue rewrite. Prove a batch selects only entries matching the active device-session ID, so pending events are never reassigned after relinking. Test with no credential to prove the client never calls `append` while unlinked.

- [ ] **Step 2: Implement append-only event creation**

Write one compact JSON object and newline per event, flush before returning, and limit the queue to 10,000 events. If full, reject the new append with a local status message; do not erase oldest records silently.

- [ ] **Step 3: Implement acknowledgement and quarantine**

After a successful API response, remove only accepted or duplicate IDs. Rewrite remaining lines to `events.jsonl.tmp`, flush, then replace the queue. Move invalid complete lines to `events.quarantine.<UTC-timestamp>.jsonl` without credentials. Ignore only an incomplete final line and preserve it until a subsequent recovery decision.

- [ ] **Step 4: Verify and commit queueing**

Run `ctest --test-dir build -R telemetry_tests --output-on-failure`. Expected: every filesystem and retry test passes without network access.

```powershell
git add CMakeLists.txt include/TelemetryQueue.h src/TelemetryQueue.cpp tests/telemetry_tests.cpp
git commit -m "Add durable desktop telemetry queue"
```

---

### Task 4: Add Injectable HTTP Transport and Background Delivery

**Files:**
- Create: `include/TelemetryTransport.h`
- Create: `src/WinHttpTelemetryTransport.cpp`
- Create: `include/TelemetryClient.h`
- Create: `src/TelemetryClient.cpp`
- Modify: `tests/telemetry_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `ITelemetryTransport::exchangeLinkCode(...)`
- `ITelemetryTransport::postEvents(...)`
- `TelemetryClient::startInteractiveSession()`
- `TelemetryClient::link(code, label)`
- `TelemetryClient::recordSuccessfulSimulation()`
- `TelemetryClient::unlink()`
- `TelemetryClient::status()`
- `TelemetryClient::shutdown()`

- [ ] **Step 1: Write failing client state-machine tests with a fake transport**

Cover unlinked startup producing no event; linked startup producing exactly one launch; linking mid-session producing one launch; repeated link call producing no second launch; successful delivery; offline retention; HTTP 429 honoring `Retry-After`; 401/403 changing status to `RelinkRequired`; duplicate acknowledgements; shutdown during an in-flight request; old-session events remaining bound after relink; unlink retaining but suspending old pending events; and retry backoff reset after success.

- [ ] **Step 2: Define endpoint injection**

Add a CMake cache string `POLYFEA_TELEMETRY_API_URL` defaulting to the empty string and compile it into the application. Empty means `Account service not configured`; it must not affect core use. Tests construct `TelemetryClient` with a fake URL and transport. Production integration supplies the deployed HTTPS origin before any distributable build.

- [ ] **Step 3: Implement WinHTTP transport**

Use HTTPS only except injected loopback URLs in tests. Set finite connect/send/receive timeouts, `Content-Type: application/json`, `Authorization: Device <token>` for events, and no bearer credential for one-time code exchange. Parse only bounded response bodies. Do not follow redirects to a different origin and do not log headers or response bodies containing credentials.

- [ ] **Step 4: Implement one worker thread and retry policy**

The render thread enqueues facts and reads a mutex-protected status snapshot; it never blocks on HTTP. The client worker sends batches of at most 100/64 KiB. Retry transient DNS/TLS/connect errors, 408, 429, and 5xx with capped exponential backoff of 2s, 5s, 15s, 60s, then 5 minutes plus bounded jitter. Treat schema 4xx as quarantined client errors; treat 401/403 as relink-required and retain unsent events bound to their original device session. Relinking never uploads old-session entries under the new token.

- [ ] **Step 5: Verify and commit delivery**

Add `winhttp` on Windows. Run focused tests and the full build. Expected: fake-transport tests are deterministic and no test reaches the internet.

```powershell
git add CMakeLists.txt include/TelemetryTransport.h src/WinHttpTelemetryTransport.cpp include/TelemetryClient.h src/TelemetryClient.cpp tests/telemetry_tests.cpp
git commit -m "Add background desktop telemetry delivery"
```

---

### Task 5: Add a Standalone Optional Account Overlay

**Files:**
- Create: `include/TelemetryPanel.h`
- Create: `src/TelemetryPanel.cpp`
- Modify: `include/SimpleUI.h`
- Modify: `src/main.cpp`
- Modify: `tests/telemetry_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `TelemetryPanelState { open, editingCode, codeBuffer, deviceLabel, message }`
- `drawTelemetryPanel(SimpleUI&, TelemetryPanelState&, TelemetryClient&, frameInput)`
- `handleTelemetryCharacter(TelemetryPanelState&, unsigned int)`

- [ ] **Step 1: Write failing pure panel-input tests**

Test accepted alphabet, uppercase conversion, backspace, paste normalization, eight-character cap, submit enabled state, Escape close, unlink confirmation state, and that displayed status contains no device token.

- [ ] **Step 2: Implement the overlay as its own module**

Use existing `SimpleUI` primitives and a small `ACCOUNT` button. The overlay states are: service not configured, unlinked, linking, linked, offline/queued count, relink required, and unlink confirmation. Explain that linking is optional, unlinked use is not counted, and manual counts are entered on the website. Never ask for email or password in the desktop app.

- [ ] **Step 3: Integrate character focus without disturbing G-code input**

Route character/backspace/paste to the account code only while the overlay owns focus; otherwise preserve the existing showcase magnitude editor exactly. Closing the overlay releases its focus. Submit calls `TelemetryClient::link` asynchronously.

- [ ] **Step 4: Isolate the future Apple UI migration seam**

Render `ACCOUNT` in the current panel header and keep the overlay invocation in one named helper call: `drawTelemetryPanel(ui, telemetryPanel, telemetryClient, frameInput)`. Add a source-contract assertion for that call. The later Apple frontend migration may move the entry button, but must retain this helper and must not move storage, queue, client, or solver hooks into UI design code.

- [ ] **Step 5: Verify and commit the overlay**

Run telemetry CTest and build. Expected: overlay input tests pass, existing magnitude input tests/build behavior remain intact, and the account UI remains optional.

```powershell
git add CMakeLists.txt include/TelemetryPanel.h src/TelemetryPanel.cpp include/SimpleUI.h src/main.cpp tests/telemetry_tests.cpp
git commit -m "Add optional desktop account overlay"
```

---

### Task 6: Wire Exact Interactive Launch and Simulation-Completion Facts

**Files:**
- Modify: `src/main.cpp:228-306`
- Modify: `src/main.cpp:520-623`
- Modify: `src/main.cpp:982-1435`
- Modify: `tests/telemetry_tests.cpp`
- Create: `tests/telemetry_source_contract_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Add `bool countsAsSimulation = false` to `ComputeJob`
- Add the same explicit argument to `startComputeJob`
- One `TelemetryClient` lifetime scoped to `runInteractive()`

- [ ] **Step 1: Write failing source-contract and completion-gate tests**

The source contract must prove telemetry initialization occurs within `runInteractive`, no telemetry symbol appears in the `--run`/`--regress` dispatch block, `ComputeJob` contains `countsAsSimulation`, and completion checks all three conditions. A pure helper test locks:

```cpp
shouldCountSimulation(true, true, false) == true
shouldCountSimulation(false, true, false) == false
shouldCountSimulation(true, false, false) == false
shouldCountSimulation(true, true, true) == false
```

- [ ] **Step 2: Mark only solver jobs as countable**

Pass `countsAsSimulation = true` for showcase fracture, linear static, nonlinear, adaptive, and brittle fracture jobs. Pass `false` for TetGen, toolpath meshing, and all non-solver background jobs. Do not infer from job titles.

- [ ] **Step 3: Record completion at the authoritative finalize boundary**

In the existing `g_job.done` branch, snapshot `countsAsSimulation`, `okResult`, and `wasCancelled`; invoke the existing finalizer unchanged; then call `recordSuccessfulSimulation()` only when the pure gate returns true. One job produces at most one event UUID.

- [ ] **Step 4: Scope launch and shutdown to interactive mode**

Construct/start the client after `runInteractive()` has created its local state and before the render loop. Queue the launch once only if a credential already exists. Call `shutdown()` during every interactive exit path after joining compute work and before destroying UI/window resources.

- [ ] **Step 5: Verify headless isolation and solver preservation**

```powershell
& .\build.bat build
ctest --test-dir build -R "telemetry_tests|telemetry_source_contract_tests" --output-on-failure
ctest --test-dir build --output-on-failure
Push-Location build
& .\FEAPreProcessor.exe --regress all
$regressionExit = $LASTEXITCODE
Pop-Location
exit $regressionExit
```

Expected: all tests and regressions exit `0`; fake transport sees no calls from headless commands; solver regression count/results match the pre-change baseline.

- [ ] **Step 6: Commit lifecycle wiring**

```powershell
git add CMakeLists.txt src/main.cpp tests/telemetry_tests.cpp tests/telemetry_source_contract_tests.cpp
git commit -m "Record successful interactive PolyFEA runs"
```

---

### Task 7: Run the Desktop Telemetry Release Gate Without Packaging

**Files:**
- Create: `docs/telemetry-development.md`
- Modify: `.gitignore`

**Interfaces:**
- Documented local fake-server/manual procedure
- No distributable artifact produced or committed

- [ ] **Step 1: Document privacy, configuration, and recovery**

Document the endpoint compile setting, exact automatic fields, excluded data, DPAPI path, queue path, unlink behavior, relink-required state, and how to clear a corrupted queue without touching models or results. State that manual entries exist only on the web dashboard.

- [ ] **Step 2: Run a loopback integration smoke test**

Build a test-only executable or Node fixture server bound to `127.0.0.1` that implements link and event endpoints. Verify link, one launch, one successful simulation event, duplicate retry acknowledgement, offline queue, restart recovery, revocation, and unlink. The fixture must use fake tokens and temporary storage.

- [ ] **Step 3: Run the complete desktop gate**

```powershell
& .\build.bat configure
& .\build.bat build
ctest --test-dir build --output-on-failure
Push-Location build
& .\FEAPreProcessor.exe --regress all
$regressionExit = $LASTEXITCODE
Pop-Location
git status --short
exit $regressionExit
```

Expected: all checks exit `0`; no token is printed; no `.exe`, installer, archive, or release asset is staged.

- [ ] **Step 4: Commit documentation only**

```powershell
git add docs/telemetry-development.md .gitignore
git commit -m "Document desktop telemetry operation"
```

The desktop is then ready to point at the deployed API during production integration, but remains unpackaged.
