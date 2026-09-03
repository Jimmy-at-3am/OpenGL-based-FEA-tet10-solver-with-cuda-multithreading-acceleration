# PolyFEA Production Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Configure Firebase and Cloudflare, deploy the tested PolyFEA web/API platform, connect a local desktop build to it, and verify the complete account/counting flow while deliberately publishing no PolyFEA application or source download.

**Architecture:** The completed `website/` build is deployed through Sites to Cloudflare Workers with D1. A user-owned Firebase project provides email/password identity and verified-email tokens. Cloudflare secrets identify the Firebase project, owner UID, and credential-hashing key. A locally built desktop client points to the deployed HTTPS origin for end-to-end verification only.

**Tech Stack:** Sites hosting workflow, Cloudflare Workers/D1, Firebase Authentication, Vinext site, Windows PolyFEA desktop app, PowerShell verification scripts.

## Global Constraints

- Execute only after the web-platform and desktop-telemetry plans pass their local release gates.
- Use `sites:sites-building` to reopen/validate the site, then `sites:sites-hosting` for all publishing and hosting changes.
- Firebase and Cloudflare account/project creation requires the user's authorized account context. Never invent project IDs, owner UIDs, domains, or secrets.
- Treat any credential pasted into chat or terminal output as compromised; stop, redact it from artifacts, and ask the user to rotate it.
- Do not publish an executable, installer, source archive, repository download, GitHub Release asset, or direct download URL. Do not modify GitHub Releases.
- A public account launch requires privacy/terms pages, 13+ attestation, verified email, deletion flow, and owner-access verification to pass first.
- Keep operational evidence free of passwords, Firebase tokens, link codes, device tokens, and user event payloads.

---

### Task 1: Re-run Both Local Release Gates and Capture the Deployment Baseline

**Files:**
- Create: `docs/deployment/polyfea-web-baseline.md`

**Interfaces:**
- Baseline contains commit hash, test commands, migration hash, public asset inventory, and pre-deployment regression result

- [ ] **Step 1: Require a clean, known source state**

Run `git status --short`, `git rev-parse HEAD`, and `git log -5 --oneline`. If unrelated user changes exist, preserve them and use a scoped worktree or stop before deployment; never stage them.

- [ ] **Step 2: Run the web gate**

```powershell
Push-Location website
npm ci
npm run check
Pop-Location
```

Expected: install, lint, unit/render tests, negative download test, and production build all exit `0`.

- [ ] **Step 3: Run the desktop gate**

```powershell
& .\build.bat configure
& .\build.bat build
ctest --test-dir build --output-on-failure
Push-Location build
& .\FEAPreProcessor.exe --regress all
$regressionExit = $LASTEXITCODE
Pop-Location
exit $regressionExit
```

Expected: tests/regressions exit `0` and no network call occurs from headless modes.

- [ ] **Step 4: Record only non-secret evidence and commit**

Record command results, commit hash, D1 migration SHA-256, and the list of `website/public/` files. Explicitly record `download artifacts: none`.

```powershell
git add docs/deployment/polyfea-web-baseline.md
git commit -m "Record PolyFEA deployment baseline"
```

---

### Task 2: Configure the User-Owned Firebase Project

**Files:**
- Create: `docs/deployment/firebase-checklist.md`
- Modify locally only: `website/.env.local` (ignored)

**Interfaces:**
- Required public values: API key, auth domain, project ID, app ID
- Required server value: exact Firebase project ID
- Required owner identity: exact verified Firebase UID

- [ ] **Step 1: Create or select the production Firebase project with the user**

Record the project display name and project ID in the checklist, not credentials. Keep the project on its no-cost plan unless the user explicitly authorizes a paid upgrade.

- [ ] **Step 2: Enable only email/password authentication**

Enable Email/Password, require email verification in application flow, and add only the Sites preview/production domains needed for this app. Do not enable anonymous, phone, social, or custom-token providers.

- [ ] **Step 3: Configure public client values locally**

Write the Firebase web values to ignored `website/.env.local`. Confirm `git check-ignore website/.env.local` succeeds and `git diff --cached` contains no environment value.

- [ ] **Step 4: Establish the owner UID**

Create/sign in to the designated owner account, verify its email, retrieve its Firebase UID through the authorized console/session, and record only the UID in the deployment checklist. Test that a second ordinary account has a different UID and no owner role.

- [ ] **Step 5: Verify account lifecycle in a local preview**

Use the production Firebase project against the local site: create a 13+ test account, receive verification email, confirm pre-verification bootstrap is blocked, verify email, bootstrap profile, sign in/out, and delete the test identity after D1 cleanup is exercised in staging.

- [ ] **Step 6: Commit the non-secret checklist**

```powershell
git add docs/deployment/firebase-checklist.md
git commit -m "Document Firebase production configuration"
```

---

### Task 3: Provision D1, Apply the Migration, and Configure Worker Secrets

**Files:**
- Modify: `website/.openai/hosting.json` only if Sites assigns a different binding name
- Create: `docs/deployment/cloudflare-checklist.md`

**Interfaces:**
- D1 binding `DB`
- Secret/config names `FIREBASE_PROJECT_ID`, `ADMIN_FIREBASE_UIDS`, `TOKEN_HASH_SECRET`

- [ ] **Step 1: Use Sites to initialize hosting state**

Follow `sites:sites-hosting` to create/select the Sites project and D1 database. Preserve the application binding name `DB`. Record project/database identifiers in the checklist only when they are non-secret.

- [ ] **Step 2: Generate hashing material outside tracked files**

Generate at least 32 random bytes for `TOKEN_HASH_SECRET` using an approved secret-entry mechanism. Enter it directly into the hosting secret interface; never print, redirect, or store it in the repository.

- [ ] **Step 3: Configure production values**

Set the Firebase project ID, comma-separated exact owner UID allowlist, and hash secret through Sites/Cloudflare secret configuration. Configure public Firebase web values using the hosting environment mechanism intended for build-time public variables.

- [ ] **Step 4: Apply and verify the D1 migration**

Apply `website/drizzle/0000_polyfea_usage.sql` to the bound production D1 database. Query only schema metadata and empty aggregate counts. Expected: seven tables plus migration metadata; all user/event counts are zero before controlled tests.

- [ ] **Step 5: Verify no secret entered source control**

Search staged/tracked content for the exact secret names and known test credentials; names may appear in documentation/code, values may not. Inspect `git diff --cached` before committing checklist changes.

- [ ] **Step 6: Commit the non-secret hosting record**

```powershell
git add website/.openai/hosting.json docs/deployment/cloudflare-checklist.md
git commit -m "Record Cloudflare hosting configuration"
```

Stage `.openai/hosting.json` only if Sites legitimately changed its non-secret binding configuration.

---

### Task 4: Deploy the Website and API With the Download Placeholder

**Files:**
- No application source changes expected
- Update: `docs/deployment/cloudflare-checklist.md`

**Interfaces:**
- Production HTTPS origin
- Public `/`, `/privacy`, `/terms`, `/account`
- Protected `/dashboard`, `/admin`, and `/api/*`

- [ ] **Step 1: Build immediately before publish**

```powershell
Push-Location website
npm ci
npm run check
Pop-Location
```

Expected: clean success from the exact commit being deployed.

- [ ] **Step 2: Publish through Sites hosting**

Use the hosting workflow's deploy action and record the resulting HTTPS origin and deployment identifier. Do not use an improvised `wrangler deploy` path when Sites provides the project lifecycle.

- [ ] **Step 3: Verify public pages and headers**

Fetch the public routes and confirm HTTPS, successful status, security headers, page titles, screenshot asset, privacy/terms text, and `Windows download coming soon`. Confirm there is no executable/archive content type, download route, release URL, directory listing, or source map exposing repository paths.

- [ ] **Step 4: Verify authentication boundaries**

Unauthenticated `/api/me/summary` returns `401`; an ordinary verified user can read only their own data; the same user receives `403` from admin endpoints; the exact owner UID succeeds. Search errors reveal no user existence beyond authorized scope.

- [ ] **Step 5: Record deployment evidence**

Store only origin, deployment ID, timestamps, response statuses, and test case outcomes in the checklist. Do not store tokens or response bodies containing personal data.

- [ ] **Step 6: Commit the deployment record**

```powershell
git add docs/deployment/cloudflare-checklist.md
git commit -m "Record PolyFEA website deployment"
```

---

### Task 5: Point a Local Desktop Build at Production and Run End-to-End Tests

**Files:**
- Create: `docs/deployment/end-to-end-results.md`
- Modify locally: CMake cache/build output only

**Interfaces:**
- `POLYFEA_TELEMETRY_API_URL=<production-origin>`
- Controlled owner and ordinary test accounts
- One test device session

- [ ] **Step 1: Configure a local test build**

Reconfigure with the exact production HTTPS origin through `POLYFEA_TELEMETRY_API_URL`. Keep the generated executable under ignored `build/`; do not copy it to `website/`, `assets/`, a release directory, or GitHub.

- [ ] **Step 2: Verify optional/unlinked behavior**

Start the app unlinked, exercise the UI, and run a successful simulation. Confirm the account UI says unlinked and the ordinary user's dashboard counts do not change. Close the process cleanly.

- [ ] **Step 3: Verify linking and automatic provenance**

Generate a link code in the browser, link the desktop once, and confirm the code cannot be reused. Confirm one recorded launch appears. Run one successful simulation and confirm one recorded simulation; cancel one and force one failing job in the test fixture, confirming neither increments. Restart once and confirm exactly one additional launch.

- [ ] **Step 4: Verify offline and retry behavior**

Temporarily block only the test process through its injectable fake/offline mode, create a successful event, verify queued status, restore connectivity, and confirm one idempotent delivery. Do not modify system-wide firewall or DNS settings.

- [ ] **Step 5: Verify manual provenance and totals**

Add a self-reported quantity, edit it, and verify the dashboard/admin detail retains separate labels and revision history. Confirm:

```text
Combined simulations = Recorded simulations + Self-reported simulations
```

Recorded launches remain outside the combined total.

- [ ] **Step 6: Verify revocation and deletion**

Revoke the device in the browser, confirm the desktop enters relink-required state, and confirm future events are rejected. Delete the test account in D1-then-Firebase order and query authorized admin aggregates to prove its rows are gone.

- [ ] **Step 7: Run headless isolation once more**

Run `--dump-ui` and `--regress all` while monitoring the controlled API test logs. Expected: zero requests from both commands.

- [ ] **Step 8: Record sanitized evidence and commit**

Record case names, timestamps, expected/actual counts, and pass/fail only. Replace emails with roles (`owner-test`, `ordinary-test`) and omit all credentials/codes/tokens.

```powershell
git add docs/deployment/end-to-end-results.md
git commit -m "Verify PolyFEA account and usage flow"
```

---

### Task 6: Final Privacy, Cost, and No-Package Release Gate

**Files:**
- Create: `docs/deployment/operations-runbook.md`
- Update: `docs/deployment/polyfea-web-baseline.md`

**Interfaces:**
- Owner export/deletion/runbook procedures
- Final first-release acceptance record

- [ ] **Step 1: Audit data minimization and retention**

Inspect production schema and logs to prove automatic events contain only approved fields, raw IPs are absent, credentials are hashed/protected, operational logs omit secrets, and account deletion cascades D1-owned data. Record the configured shortest practical platform log retention.

- [ ] **Step 2: Verify free-tier operating posture**

Record Firebase and Cloudflare plan names, current usage dashboards, quota-alert availability, and a response procedure for nearing a limit. Do not enable billing or paid upgrades without a new explicit user decision.

- [ ] **Step 3: Document owner operations**

Document how to verify service health, inspect aggregate counts, revoke a device, remove a user at their request, rotate `TOKEN_HASH_SECRET` with explicit consequences for existing device links, apply future migrations, and roll back a site deployment. Do not include secret values.

- [ ] **Step 4: Prove no application package was published**

Check tracked files, `website/public/`, deployed routes/assets, Sites deployment inventory, and GitHub Releases. Expected: no new executable, installer, source archive, repository bundle, or release asset; the public CTA remains `Windows download coming soon`.

- [ ] **Step 5: Complete the acceptance matrix**

Mark all design success criteria: public introduction, 13+ verified account, private dashboard, owner dashboard, optional linking, launch/simulation separation, manual provenance, combined formula, offline retry, revocation, deletion, headless isolation, and no package. Any failed item blocks declaring the release complete.

- [ ] **Step 6: Run final checks and commit the runbook**

```powershell
Push-Location website
npm run check
Pop-Location
ctest --test-dir build --output-on-failure
git status --short
```

Expected: all checks pass and only the intended runbook/baseline edits remain.

```powershell
git add docs/deployment/operations-runbook.md docs/deployment/polyfea-web-baseline.md
git commit -m "Add PolyFEA web operations runbook"
```

At this point the website, accounts, dashboards, and counting service are live. The application download remains intentionally unavailable until a separate packaging/release request is approved.
