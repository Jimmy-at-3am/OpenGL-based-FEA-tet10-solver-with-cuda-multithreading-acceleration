# PolyFEA Web Platform Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the local, deployment-ready PolyFEA public website, Firebase browser-account flow, Cloudflare Worker API, D1 persistence, private user dashboard, and owner dashboard while keeping the download as an explicit placeholder.

**Architecture:** An isolated `website/` Vinext application serves React pages and App Router API handlers on Cloudflare Workers. Firebase handles browser email/password identity; the Worker verifies Firebase ID tokens with Google JWKS. D1 stores profiles, link credentials, automatic usage, and separately sourced manual entries. The desktop client consumes only the documented HTTP contract.

**Tech Stack:** TypeScript, React 19, Vinext, Cloudflare Workers, D1, Drizzle ORM, Firebase Web SDK, `jose`, Node test runner, ESLint.

## Global Constraints

- Use the `sites:sites-building` skill before creating or changing `website/` and retain the Sites starter's Vinext/Cloudflare structure.
- Do not copy, archive, expose, or link the C++ source tree, executable, installer, or GitHub Release asset. The public download action must say `Windows download coming soon` and be non-downloadable.
- Keep Firebase password handling entirely inside Firebase. D1 stores no password, Firebase refresh token, or plaintext desktop credential.
- Require a checked 13+ attestation and verified Firebase email before `/api/profile/bootstrap` creates a profile.
- Keep recorded launches, recorded simulations, and self-reported simulations separate in schema, API, and UI. The displayed combined total is recorded simulations plus self-reported simulations; launches are excluded.
- Use `crypto.randomUUID()` or Web Crypto random bytes for opaque identifiers. Hash link codes and device tokens with HMAC-SHA-256 using `TOKEN_HASH_SECRET`; never log their plaintext.
- Browser authorization is ownership by exact Firebase UID. Owner authorization is exact membership in `ADMIN_FIREBASE_UIDS`; email addresses never grant owner access.
- Return successes as `{ "apiVersion": 1, "data": T }` and errors as `{ "apiVersion": 1, "error": { "code": string, "message": string } }` without secrets or stack traces.
- Preserve the approved contract in `docs/superpowers/specs/2026-09-02-polyfea-website-telemetry-design.md`.
- This plan ends with a tested local build. Production configuration and publishing are handled by `2026-09-02-polyfea-production-integration.md`.

---

### Task 1: Initialize the Isolated Site and Freeze Shared Contracts

**Files:**
- Create: `website/` from the Sites Vinext starter
- Create: `website/lib/contracts.ts`
- Create: `website/lib/errors.ts`
- Create: `website/lib/env.ts`
- Create: `website/.env.example`
- Modify: `website/package.json`
- Modify: `website/.gitignore`
- Modify: `website/.openai/hosting.json`
- Modify: `website/worker/index.ts`
- Create: `website/tests/contracts.test.ts`

**Interfaces:**
- `AutomaticEvent = { eventId, type, occurredAt, appVersion }`
- `UsageSummary = { recordedLaunches, recordedSimulations, selfReportedSimulations, combinedSimulations }`
- `ApiErrorBody = { error: { code, message } }`
- `requireEnv(name): string` and `optionalEnv(name): string | undefined`

- [ ] **Step 1: Initialize with Sites and remove the starter presentation**

Run the Sites initializer into `website/`, keep `worker/index.ts`, `vite.config.ts`, `db/`, and `.openai/hosting.json`, and remove starter page content and `_sites-preview` skeleton once the PolyFEA shell exists. Set `.openai/hosting.json` to:

```json
{
  "d1": "DB",
  "r2": null
}
```

Do not create a second framework or a root-level Node project.

- [ ] **Step 2: Add dependencies and deterministic scripts**

Add runtime dependencies `firebase` and `jose`, plus the `tsx` development dependency. Add scripts:

```json
{
  "test:unit": "tsx --test tests/*.test.ts",
  "test:render": "npm run build && node --test tests/rendered-html.test.mjs",
  "test": "npm run test:unit && npm run test:render",
  "check": "npm run lint && npm run test",
  "db:generate": "drizzle-kit generate"
}
```

Commit the generated lockfile. Ignore `.env*` while explicitly retaining `.env.example`, plus `.wrangler/`, `.next/`, and `dist/`.

- [ ] **Step 3: Write failing contract tests**

Test that `parseAutomaticEventBatch` rejects zero events, 101 events, unknown types, missing UUIDs, invalid timestamps, and bodies over 64 KiB; accepts exactly 1 and 100 valid events; and computes:

```ts
combinedSimulations === recordedSimulations + selfReportedSimulations
```

Run `npm run test:unit` from `website/`. Expected: failure because parsers are not implemented.

- [ ] **Step 4: Implement the shared contract and error envelope**

Define event types exactly as `app_launch` and `simulation_completed`. Validate UUID strings, ISO-8601 timestamps, app version length 1–40, and batches from the original request bytes before JSON parsing exceeds 64 KiB. Require `apiVersion: 1` in request bodies and response envelopes. Define error codes used by every route: `unauthorized`, `forbidden`, `invalid_request`, `not_found`, `conflict`, `rate_limited`, and `internal_error`.

- [ ] **Step 5: Extend the Worker environment type**

Declare `DB`, `ASSETS`, `IMAGES`, `FIREBASE_PROJECT_ID`, `ADMIN_FIREBASE_UIDS`, and `TOKEN_HASH_SECRET` in `worker/index.ts`. Keep secrets out of the repository. `.env.example` documents names with empty values and explains that local tests inject values.

- [ ] **Step 6: Verify and commit the foundation**

```powershell
Push-Location website
npm run test:unit
npm run build
Pop-Location
```

Expected: both commands exit `0` and no download artifact exists under `website/public/`.

```powershell
git add website
git commit -m "Initialize PolyFEA web platform contracts"
```

---

### Task 2: Implement the D1 Schema and Repository Layer

**Files:**
- Modify: `website/db/schema.ts`
- Modify: `website/db/index.ts`
- Create: `website/db/repository.ts`
- Create: `website/drizzle/0000_polyfea_usage.sql`
- Create: `website/tests/repository.test.ts`
- Modify: `website/package.json`

**Interfaces:**
- `UsageRepository` methods for profiles, link codes, device sessions, events, manual entries, summaries, deletion, and admin queries
- `withTransaction<T>(work): Promise<T>` for one-time code exchange and manual-entry revision writes

- [ ] **Step 1: Write failing repository behavior tests**

Use a repository test double backed by an in-memory map to lock behavior independently from D1. Cover:

- duplicate `usage_events.event_id` returns the existing acceptance result without incrementing counts;
- a link code can be consumed once and only before expiry;
- revoking a device rejects future events;
- creating, editing, and deleting manual entries writes immutable revision rows;
- deleting a profile cascades every row owned by the UID;
- user summaries never include another UID;
- admin pagination is stable by `(created_at, firebase_uid)`.

Run `npm run test:unit`. Expected: failure because `UsageRepository` does not exist.

- [ ] **Step 2: Define the seven D1 tables and indexes**

Implement the six approved domain tables—`profiles`, `link_codes`, `device_sessions`, `usage_events`, `manual_entries`, `manual_entry_revisions`—plus implementation-only `rate_limit_buckets`. Use UTC ISO-8601 text for timestamps. Give every domain table a foreign key to `profiles.firebase_uid` with `ON DELETE CASCADE`. Do not cascade revision rows when a single manual entry is deleted; revisions retain the opaque `manual_entry_id` and remain until the owning profile is deleted. Use these critical constraints:

```sql
CHECK (event_type IN ('app_launch', 'simulation_completed'))
CHECK (quantity BETWEEN 1 AND 10000)
UNIQUE (event_id)
UNIQUE (code_hash)
UNIQUE (token_hash)
```

Index event/device ownership and recency, manual-entry ownership and recency, link-code expiry, normalized email, and rate-limit window expiry. Store `source_ip_hash`, never raw IP.

- [ ] **Step 3: Implement typed repository operations**

Keep all SQL in `db/repository.ts`; routes must not embed SQL. One-time link exchange runs in one transaction: fetch an unused, unexpired code, mark `used_at`, and insert a device session. During normal authenticated writes, delete consumed or expired link-code rows older than 24 hours. Each manual mutation writes one revision: `create` and `update` store the resulting snapshot, while `delete` stores the final snapshot before removing the current row. Summary SQL returns zero-valued fields when no events exist.

- [ ] **Step 4: Generate and inspect the migration**

Run `npm run db:generate`; compare generated SQL to the checked-in migration, enable `PRAGMA foreign_keys = ON`, and ensure profile deletion removes domain rows while revision history remains scoped to that profile until deletion.

- [ ] **Step 5: Verify and commit persistence**

```powershell
Push-Location website
npm run test:unit
npm run build
Pop-Location
```

Expected: repository tests pass and the Worker bundle resolves `DB` through `db/index.ts`.

```powershell
git add website/db website/drizzle website/tests website/package.json website/package-lock.json
git commit -m "Add D1 usage persistence model"
```

---

### Task 3: Add Firebase Verification, Authorization, and Durable Rate Limits

**Files:**
- Create: `website/lib/firebase-admin.ts`
- Create: `website/lib/auth.ts`
- Create: `website/lib/crypto.ts`
- Create: `website/lib/rate-limit.ts`
- Create: `website/tests/auth.test.ts`
- Create: `website/tests/rate-limit.test.ts`

**Interfaces:**
- `verifyFirebaseIdToken(token, projectId): Promise<{ uid, email, emailVerified }>`
- `requireBrowserIdentity(request): Promise<BrowserIdentity>`
- `requireOwner(identity): void`
- `hashCredential(value, secret): Promise<string>`
- `consumeLimit({ action, scopeHash, limit, windowMs }): Promise<{ allowed, retryAfterSeconds }>`

- [ ] **Step 1: Write token and authorization failure tests**

Inject a test JWKS and fixed clock. Cover valid RS256 token; expired token; wrong issuer; wrong audience; missing UID; unverified email; malformed Authorization header; and owner allowlist exact match. Assert that email equality alone never grants owner access.

- [ ] **Step 2: Implement Firebase JWT verification**

Use `jose` with Google's secure-token JWKS endpoint. Require algorithm `RS256`, issuer `https://securetoken.google.com/<project-id>`, audience `<project-id>`, non-empty `sub`, and matching `user_id`. Cache JWKS with the library's bounded remote-JWKS cache. Do not use a Firebase Admin private key.

- [ ] **Step 3: Implement credential hashing and constant-time comparison**

Generate link codes from the unambiguous alphabet `23456789ABCDEFGHJKLMNPQRSTUVWXYZ` and device tokens from 32 random bytes. Store only `HMAC-SHA-256(secret, value)` encoded as lowercase hex. Compare hashes in constant time where application comparison is needed.

- [ ] **Step 4: Write and implement D1-backed fixed-window limits**

Test exact boundaries and rollover for:

- link-code issuance: 5 per UID per hour;
- link-code exchange: 30 per source hash per hour;
- telemetry batches: 120 per device per minute;
- manual mutations: 60 per UID per hour.

Update `rate_limit_buckets` atomically with an upsert keyed by action, scope hash, and window start. Hash request source identifiers before persistence and expire old buckets during writes.

- [ ] **Step 5: Verify and commit identity controls**

```powershell
Push-Location website
npm run test:unit
npm run lint
Pop-Location
```

Expected: authentication and rate-limit tests pass; lint reports no unsafe token logging.

```powershell
git add website/lib website/tests
git commit -m "Add Firebase authorization and API limits"
```

---

### Task 4: Implement Profile, Link-Code, and Device Endpoints

**Files:**
- Create: `website/app/api/profile/bootstrap/route.ts`
- Create: `website/app/api/me/link-codes/route.ts`
- Create: `website/app/api/me/devices/route.ts`
- Create: `website/app/api/me/devices/[id]/route.ts`
- Create: `website/app/api/device/link/route.ts`
- Create: `website/tests/linking-api.test.ts`

**Interfaces:**
- `POST /api/profile/bootstrap`
- `POST /api/me/link-codes`
- `GET /api/me/devices`
- `PATCH /api/me/devices/:id`
- `DELETE /api/me/devices/:id`
- `POST /api/device/link`

- [ ] **Step 1: Write failing route tests with injected identity, clock, and repository**

Cover bootstrap requiring verified email and `age13Plus === true`; normalized lowercase email; an eight-character code expiring after 10 minutes; plaintext code returned once; consumed/expired code rejection; device ID/token returned once; listing only owned devices; owned label edit; idempotent owned revocation; and cross-user edit/deletion returning `404`.

- [ ] **Step 2: Implement profile bootstrap**

Accept `{ age13Plus: true }` only. Upsert the authenticated UID, normalized verified email, `age_attested_at`, created timestamp, and updated timestamp. A false/missing attestation returns `invalid_request`; an unverified email returns `forbidden`.

- [ ] **Step 3: Implement code issuance and exchange**

Issue one code after the UID rate limit, store its hash, and return `{ code, expiresAt }`. Exchange `{ apiVersion: 1, code, deviceLabel, appVersion }` after source rate limiting. Normalize code by trimming spaces and uppercasing. Limit device label to 64 Unicode code points and app version to 40. The transaction consumes the code and creates exactly one device session; return `{ deviceId, deviceToken, linkedAt }`, with the new plaintext token present only in that response.

- [ ] **Step 4: Implement device listing and revocation**

Return ID, label, created time, last-seen time, and revoked state—never token hash. `PATCH` accepts a 1–64-code-point label and updates only an active owned device. `DELETE` updates `revoked_at` only when `firebase_uid` matches the caller.

- [ ] **Step 5: Verify and commit linking**

```powershell
Push-Location website
npm run test:unit
npm run build
Pop-Location
```

Expected: all linking tests pass and route bundles compile for the Worker runtime.

```powershell
git add website/app/api website/tests
git commit -m "Implement optional desktop account linking"
```

---

### Task 5: Implement Event, Manual Entry, Summary, and Deletion Endpoints

**Files:**
- Create: `website/app/api/device/events/route.ts`
- Create: `website/app/api/me/summary/route.ts`
- Create: `website/app/api/me/manual-entries/route.ts`
- Create: `website/app/api/me/manual-entries/[id]/route.ts`
- Create: `website/app/api/me/route.ts`
- Create: `website/app/api/admin/summary/route.ts`
- Create: `website/app/api/admin/users/route.ts`
- Create: `website/app/api/admin/users/[uid]/route.ts`
- Create: `website/tests/usage-api.test.ts`
- Create: `website/tests/admin-api.test.ts`

**Interfaces:**
- `POST /api/device/events`
- `GET /api/me/summary`
- `GET /api/me/manual-entries`
- `POST /api/me/manual-entries`
- `PATCH /api/me/manual-entries/:id`
- `DELETE /api/me/manual-entries/:id`
- `DELETE /api/me`
- `GET /api/admin/summary`
- `GET /api/admin/users`
- `GET /api/admin/users/:uid`
- Event response `{ acceptedEventIds: string[], duplicateEventIds: string[] }`
- Summary response with the four `UsageSummary` fields and provenance-separated recent activity

- [ ] **Step 1: Write failing event-ingestion tests**

Cover missing/revoked device bearer token, 0/101 events, oversized body, valid 1/100 event batches, duplicate replay, mixed duplicate/new batch, unknown type, future timestamp beyond 10 minutes, app launch counting, and simulation-completed counting. A retried `eventId` must not increment any count twice.

- [ ] **Step 2: Implement desktop authentication and event ingestion**

Use `Authorization: Device <plaintext-token>`, hash it, resolve an active device, enforce its rate limit, validate the raw byte limit and event array, insert with `ON CONFLICT(event_id) DO NOTHING`, and update `last_seen_at`. Never accept manual quantities through this endpoint.

- [ ] **Step 3: Write failing manual-entry and summary tests**

Cover quantity boundaries 1 and 10,000; rejection at 0 and 10,001; note length at 200 code points; optional `occurredOn` as a valid `YYYY-MM-DD` or null; create/edit/delete ownership; retained revision history; and exact total formulas. Assert that launches are absent from `combinedSimulations`.

- [ ] **Step 4: Implement manual entry and user summary routes**

Create entries with server timestamps and a `create` revision. `PATCH /api/me/manual-entries/:id` changes quantity/note and writes the resulting snapshot as an `update` revision. `DELETE` writes a `delete` revision with the final snapshot and removes only the current entry; revisions remain until profile deletion. Return recorded and self-reported activity as separate arrays with a `provenance` discriminator.

- [ ] **Step 5: Implement owner-only aggregate routes**

`/api/admin/summary` returns profile count, linked-profile count, recorded launches, recorded simulations, self-reported simulations, and combined simulations. `/api/admin/users` accepts `cursor`, `limit` 1–100, and normalized email search. Detail returns one profile's summary, devices, events, manual entries, and revisions. Apply `requireOwner` before repository access.

- [ ] **Step 6: Implement D1-owned account deletion**

`DELETE /api/me` verifies the caller, deletes the profile transactionally, and returns `204`. It does not call Firebase; the browser invokes Firebase account deletion only after this route succeeds.

- [ ] **Step 7: Verify and commit the complete API**

```powershell
Push-Location website
npm run test:unit
npm run lint
npm run build
Pop-Location
```

Expected: API tests pass, no route exposes hashes, and all authorization branches are covered.

```powershell
git add website/app/api website/db website/tests
git commit -m "Complete usage and administration API"
```

---

### Task 6: Build the Public High-School-Focused Website and Legal Pages

**Files:**
- Modify: `website/app/layout.tsx`
- Modify: `website/app/page.tsx`
- Modify: `website/app/globals.css`
- Create: `website/app/components/SiteHeader.tsx`
- Create: `website/app/components/HeroDemo.tsx`
- Create: `website/app/components/FeatureCard.tsx`
- Create: `website/app/privacy/page.tsx`
- Create: `website/app/terms/page.tsx`
- Copy: `assets/pictures/benchmark_test.png` to `website/public/polyfea-benchmark.png`
- Create: `website/public/og-polyfea.png` using the one image-generation request required by the Sites workflow
- Modify: `website/tests/rendered-html.test.mjs`

**Interfaces:**
- Public routes `/`, `/privacy`, `/terms`
- Non-download CTA with accessible disabled semantics

- [ ] **Step 1: Write failing rendered-HTML assertions**

Assert that `/` includes `PolyFEA`, `See how parts bend before you build them`, a plain-language finite-element explanation, the four learning steps, `Windows download coming soon`, account link, screenshot alt text, FAQ, Windows requirements, privacy link, and terms link. Assert there is no anchor with `download`, no `.exe`, `.zip`, repository archive, or GitHub Release asset URL.

- [ ] **Step 2: Implement the information hierarchy**

Build a calm educational landing page in this order: hero and one-sentence value proposition; real benchmark screenshot; four steps—load a model, create a mesh, choose material and forces, inspect deformation; student examples; capability cards for STL/3MF/STEP, CPU/CUDA, real-time visualization, printed-layer analysis, and automated scenarios; MIT/no-commercial-solver/educational-use limitations; FAQ and initial Windows requirements; account value; download placeholder; privacy/terms/contact footer. Write for high-school readers without presenting PolyFEA as a toy or claiming solver accuracy beyond repository evidence.

- [ ] **Step 3: Implement the visual system and accessibility states**

Use a restrained graphite/white palette with one cyan-blue accent, strong display typography, generous space, real controls rather than decorative pills, visible keyboard focus, minimum 44px interactive targets, semantic headings, reduced-motion support, and contrast meeting WCAG AA. The download placeholder must render as non-interactive text or a disabled button with `aria-disabled="true"`, not as a dead link.

- [ ] **Step 4: Add the existing product image and one social card**

Copy the existing benchmark screenshot without editing its scientific content. Use exactly one image-generation request for a non-data-bearing Open Graph card after copy is final; do not fabricate a simulation output. Add descriptive alt text to the real screenshot and empty alt text only to decorative imagery.

- [ ] **Step 5: Write privacy and terms content**

Disclose Firebase and Cloudflare, collected identifiers and counts, optional linking, uncounted unlinked use, self-reported provenance, retention/account deletion, age 13+ requirement, and the fact that open-source clients cannot make usage data tamper-proof. State that the software download is not yet published. Use the repository's GitHub Issues page as the visible contact path for data and deletion problems until the owner supplies a dedicated support address.

- [ ] **Step 6: Preview, test, and commit public pages**

Start `npm run dev`, wait for the first meaningful Sites preview, then run:

```powershell
npm run test
npm run lint
```

Expected: pages render, static assertions pass, and no downloadable app/source asset exists.

```powershell
git add website/app website/public website/tests
git commit -m "Build PolyFEA educational landing site"
```

---

### Task 7: Implement Firebase Browser Auth and the Private User Dashboard

**Files:**
- Create: `website/lib/firebase-client.ts`
- Create: `website/lib/api-client.ts`
- Create: `website/app/account/page.tsx`
- Create: `website/app/account/AccountClient.tsx`
- Create: `website/app/dashboard/page.tsx`
- Create: `website/app/dashboard/DashboardClient.tsx`
- Create: `website/app/components/MetricCard.tsx`
- Create: `website/app/components/ManualEntryForm.tsx`
- Create: `website/app/components/DeviceList.tsx`
- Create: `website/tests/dashboard-copy.test.ts`

**Interfaces:**
- Firebase `createUserWithEmailAndPassword`, verification-email, sign-in, password reset, sign-out, reauthentication, and account deletion
- Authenticated API client that refreshes and sends Firebase ID tokens

- [ ] **Step 1: Write failing copy and provenance tests**

Assert visible labels are exactly `Recorded launches`, `Recorded simulations`, `Self-reported simulations`, and `Combined simulations`. Assert the combined helper text says recorded simulations plus self-reported simulations and never implies verification. Assert a manual form labels the data as self-reported.

- [ ] **Step 2: Configure the public Firebase client**

Initialize once from the five public environment values. Export no server secret. The API client calls `user.getIdToken()` immediately before browser requests and maps structured API errors to safe user messages.

- [ ] **Step 3: Implement signup and sign-in**

Signup requires email, password, checked 13+ attestation, and acceptance of privacy/terms. After Firebase account creation, send verification email. Provide password reset through Firebase's email reset flow. After the user verifies and signs in, call `/api/profile/bootstrap`. Do not bootstrap or show the dashboard for an unverified email.

- [ ] **Step 4: Implement the dashboard**

Load summary, devices, and manual entries. Render the four metric cards separately. Add link-code generation with a 10-minute expiry display and a warning that the code is single use. Add device-label editing and revocation. Add create/edit/delete manual entries with quantity 1–10,000 and note up to 200 code points.

- [ ] **Step 5: Implement account deletion ordering**

Require a recent Firebase sign-in, call `DELETE /api/me`, then call Firebase `deleteUser`. If the Firebase deletion step requires reauthentication, explain that D1 usage data has already been removed and guide the user through finishing Firebase identity deletion.

- [ ] **Step 6: Verify and commit the account experience**

```powershell
Push-Location website
npm run check
Pop-Location
```

Expected: copy tests pass, the production bundle contains only Firebase public routing configuration, and no admin UI appears for ordinary users.

```powershell
git add website/app website/lib website/tests
git commit -m "Add Firebase accounts and usage dashboard"
```

---

### Task 8: Implement the Owner Dashboard

**Files:**
- Create: `website/app/admin/page.tsx`
- Create: `website/app/admin/AdminClient.tsx`
- Create: `website/app/components/UserUsageTable.tsx`
- Create: `website/tests/admin-ui.test.ts`

**Interfaces:**
- Owner-only UI consuming `/api/admin/summary`, `/api/admin/users`, and `/api/admin/users/:uid`

- [ ] **Step 1: Write failing owner-view tests**

Assert the page contains aggregate cards, searchable/paginated user rows, provenance-separated detail, no public leaderboard language, and a forbidden state that reveals no data.

- [ ] **Step 2: Implement the protected owner page**

Require a signed-in Firebase user, let the server enforce UID allowlisting, and show a neutral unauthorized view on `403`. Render aggregate metrics, email search, stable cursor pagination, and a selected user's devices, automatic events, manual entries, and revisions. Never show token/code hashes.

- [ ] **Step 3: Verify and commit owner reporting**

```powershell
Push-Location website
npm run check
Pop-Location
```

Expected: owner UI tests pass and an ordinary authenticated fixture cannot read admin responses.

```powershell
git add website/app/admin website/app/components website/tests
git commit -m "Add private owner usage dashboard"
```

---

### Task 9: Run the Local Web-Platform Release Gate

**Files:**
- Create: `website/README.md`
- Create: `website/tests/no-download-artifact.test.ts`
- Modify: `website/package.json`

**Interfaces:**
- `npm run check` is the single local release gate
- README documents local environment, migration, test, and boundary rules

- [ ] **Step 1: Add the negative packaging test**

Recursively scan `website/public/` and rendered anchor targets. Fail for `.exe`, `.msi`, `.zip`, `.7z`, source archives, or GitHub Release asset URLs. Permit only public web assets such as images, SVG, fonts, and metadata.

- [ ] **Step 2: Document local operation without credentials**

Document Node requirement, `npm ci`, local D1 setup, environment names, migration command, tests, and how to inject fake identity/JWKS in tests. State that real account flows require production Firebase configuration and that the current download is intentionally absent.

- [ ] **Step 3: Run the complete gate**

```powershell
Push-Location website
npm ci
npm run check
Pop-Location
git status --short
```

Expected: install, lint, all unit/render tests, and production build exit `0`; status contains only intended documentation/test changes; no app package exists.

- [ ] **Step 4: Commit the local release gate**

```powershell
git add website/README.md website/tests website/package.json website/package-lock.json
git commit -m "Document and verify PolyFEA web platform"
```

The web/API implementation is then ready for the production-integration plan, but it is not yet published.
