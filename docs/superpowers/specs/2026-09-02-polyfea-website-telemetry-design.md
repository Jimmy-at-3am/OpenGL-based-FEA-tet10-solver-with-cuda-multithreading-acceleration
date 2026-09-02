# PolyFEA Website, Accounts, and Usage Telemetry Design

**Status:** Approved design

**Date:** 2026-09-02

**Audience:** Implementation contributors and the project owner

## 1. Goal

Create a public, zero-cost website that introduces PolyFEA to high-school
students, provides small user accounts, and reports usage per account. The
first implementation publishes the website but deliberately does not publish
an executable, source archive, GitHub Release, or other downloadable package.
The download surface is a clearly labelled placeholder until the owner
authorizes packaging in a later phase.

The website and telemetry design must not interfere with unlinked use of the
desktop application. Account linking is optional. An unlinked user receives
the complete PolyFEA experience and produces no usage telemetry.

## 2. Approved Product Decisions

- Accounts are restricted to users aged 13 or older.
- Firebase email/password authentication provides browser accounts, email
  verification, password reset, and account deletion.
- Cloudflare hosts the website, API, and D1 database.
- The public download button says **Windows download coming soon** and has no
  package URL.
- Linking PolyFEA to an account is optional.
- A linked account records two automatic measures separately:
  - one application launch per desktop process;
  - one completed interactive FEA simulation per successful solve job.
- Unlinked activity is not recorded or estimated.
- Users can enter self-reported run counts manually.
- Automatic and self-reported counts remain separate at every storage, API,
  and presentation layer. A combined total may be shown only when explicitly
  labelled as a combined total.
- Each user can see only their own usage and devices.
- An explicitly configured owner account can see aggregate statistics and a
  per-user table.
- There is no public leaderboard.
- Telemetry never uploads models, filenames, geometry, material settings,
  loads, mesh data, solver results, screenshots, hardware identifiers, or
  precise location.

## 3. Success Criteria for the First Implementation

The first implementation is complete when all of the following are true:

1. A public, responsive website explains PolyFEA in language accessible to a
   high-school student and uses a real PolyFEA result image.
2. The download call to action is visibly a placeholder and cannot initiate a
   download.
3. A person aged 13 or older can create and verify an email/password account,
   sign in, reset their password, and delete their account data.
4. A signed-in user can generate a short-lived, single-use desktop link code.
5. PolyFEA remains fully usable without linking an account.
6. A linked PolyFEA installation can queue launch and successful-simulation
   events offline and synchronize them later without duplicate counting.
7. Failed solves, cancelled solves, meshing-only jobs, automated scenarios,
   regression runs, and duplicate retries do not increase the completed
   simulation count.
8. A user dashboard shows recorded launches, recorded simulations,
   self-reported runs, and a clearly labelled combined total.
9. The owner dashboard shows aggregate and per-user usage without exposing it
   publicly.
10. Authorization, ownership isolation, retry behavior, event provenance,
    accessibility, and the existing network-free regression harness are
    verified by tests.
11. No executable, source archive, repository copy, release asset, or hidden
    package endpoint is published by the website deployment.

## 4. Scope Boundaries

### Included

- Public landing, privacy, terms, account, user-dashboard, and admin routes.
- Firebase browser authentication.
- Cloudflare Worker API and D1 persistence.
- Optional desktop account linking by one-time code.
- Local token protection and offline event queuing on Windows.
- Automatic and manual count provenance.
- Website deployment and operational documentation.

### Excluded from this phase

- Creating or uploading a Windows release package.
- Copying the full source tree into the website project.
- Publishing a GitHub Release or direct package URL.
- Mandatory sign-in, online-only operation, subscriptions, payments, social
  features, classrooms, comments, or a public leaderboard.
- Telemetry about engineering inputs or results.
- Claims that client-originated telemetry is cryptographically tamper-proof.

## 5. Architecture

### 5.1 Components

1. **Website (`website/`)**
   - Public educational landing page.
   - Firebase browser authentication.
   - Private student dashboard and owner dashboard.
   - Server/API routes deployed with the site.

2. **Firebase Authentication**
   - Owns email/password credentials, verification state, password-reset flow,
     and browser ID tokens.
   - Passwords never pass through PolyFEA, Cloudflare D1, or application logs.

3. **Cloudflare Worker API**
   - Verifies Firebase ID tokens for browser requests.
   - Issues and exchanges one-time desktop link codes.
   - Authenticates opaque desktop device tokens.
   - Validates, rate-limits, and stores telemetry and manual entries.
   - Enforces student ownership and owner-only administration server-side.

4. **Cloudflare D1**
   - Stores profiles, link-code state, device sessions, automatic usage events,
     manual entries, and manual-entry revision history.

5. **PolyFEA telemetry module**
   - Isolated from solver and model code behind a small interface.
   - Protects the device token with Windows Data Protection.
   - Queues events without blocking rendering or solving.
   - Synchronizes on a background worker when connectivity is available.

### 5.2 Dependency boundaries

- The FEA domain core remains unaware of HTTP, Firebase, Cloudflare, accounts,
  and JSON transport.
- The interactive application reports only lifecycle outcomes to the telemetry
  module.
- Headless scenarios and regression tests do not initialize telemetry.
- Website components use a typed API client rather than reading D1 directly.
- All D1 access is confined to server-side repository modules using prepared
  statements.

## 6. Identity and Linking Flows

### 6.1 Browser account creation

1. The user supplies an email and password and affirmatively confirms that
   they are at least 13 years old.
2. Firebase creates the account and sends a verification email.
3. Private dashboard and desktop-link actions require a verified email.
4. On first verified dashboard access, the API creates a D1 profile containing
   the Firebase UID, normalized email, and age-attestation timestamp.

The age checkbox is not preselected. The site does not solicit birth date,
school, grade, legal name, address, or phone number.

### 6.2 Desktop linking

1. A verified, signed-in user requests a link code from the dashboard.
2. The Worker returns an eight-character code that excludes visually ambiguous
   characters. It expires after ten minutes and can be used once.
3. The user opens PolyFEA's account panel and enters the code.
4. PolyFEA exchanges the code over HTTPS for a random 256-bit opaque device
   token.
5. D1 stores only a cryptographic hash of the device token. Windows stores the
   token encrypted with Data Protection under the current Windows user.
6. The dashboard lists linked sessions by user-editable device label, created
   time, and last-seen time. It never exposes the token.

The website never asks the user to copy a password into PolyFEA.

### 6.3 Revocation and relinking

- A user can revoke any linked session from the dashboard.
- Revoked or invalid tokens stop synchronization but never disable PolyFEA.
- Pending events are bound to the device session that created them. They are
  never reassigned automatically to a different account or new device session.
- If an old session cannot be restored, the user may add the missing quantity
  as a clearly labelled self-reported entry.

## 7. Counting Semantics

### 7.1 Recorded application launches

- At most one `app_launch` event is created per interactive desktop process.
- If PolyFEA starts already linked, the event is created during startup after
  local token recovery.
- If an unlinked process becomes linked during that same session, one launch
  event is created when linking succeeds.
- Headless modes (`--run`, `--regress`, and `--dump-ui`) never create launch
  events.

### 7.2 Recorded completed simulations

A `simulation_completed` event is created only when an interactive compute job
explicitly marked as an FEA solve reaches finalization with:

- `okResult == true`; and
- `wasCancelled == false`.

Linear, nonlinear, adaptive, showcase-fracture, and brittle-fracture jobs count
once each when successful. Meshing, slicing, toolpath preparation, cancelled
jobs, failed jobs, regression scenarios, and event-upload retries do not count.

The interactive compute-job descriptor gains a `countsAsSimulation` property so
counting is explicit at the UI orchestration boundary rather than inferred from
button labels or implemented inside `FEASolver`.

### 7.3 Self-reported runs

- A signed-in user may create a manual entry with a positive integer quantity,
  an optional date, and an optional short note.
- Manual entries can be edited or deleted by their owner.
- Each mutation writes a revision record so the owner view can distinguish a
  correction from automatic activity.
- Manual quantities never create automatic events.

### 7.4 Terminology

- **Recorded launches:** accepted `app_launch` events.
- **Recorded simulations:** accepted `simulation_completed` events.
- **Self-reported runs:** sum of current manual-entry quantities.
- **Combined total:** recorded simulations plus self-reported runs. Launches are
  not added to this total because a launch is not a simulation.

The UI uses **recorded**, not **verified**, because an open-source desktop
client can be modified. Authentication, schema validation, rate limits, and
idempotency make events account-linked and duplicate-safe; they cannot prove
that every client report corresponds to physical execution of the official
binary.

## 8. Data Model

All identifiers are opaque random IDs unless they are Firebase UIDs. Timestamps
are stored in UTC.

### `profiles`

- `firebase_uid` TEXT PRIMARY KEY
- `email` TEXT NOT NULL
- `age_attested_at` TEXT NOT NULL
- `created_at` TEXT NOT NULL
- `updated_at` TEXT NOT NULL

### `link_codes`

- `id` TEXT PRIMARY KEY
- `firebase_uid` TEXT NOT NULL
- `code_hash` TEXT NOT NULL UNIQUE
- `expires_at` TEXT NOT NULL
- `used_at` TEXT NULL
- `created_at` TEXT NOT NULL

Expired and consumed codes are periodically deleted during normal API writes.

### `device_sessions`

- `id` TEXT PRIMARY KEY
- `firebase_uid` TEXT NOT NULL
- `token_hash` TEXT NOT NULL UNIQUE
- `label` TEXT NOT NULL
- `created_at` TEXT NOT NULL
- `last_seen_at` TEXT NOT NULL
- `revoked_at` TEXT NULL

### `usage_events`

- `event_id` TEXT PRIMARY KEY
- `firebase_uid` TEXT NOT NULL
- `device_session_id` TEXT NOT NULL
- `event_type` TEXT NOT NULL, restricted to `app_launch` or
  `simulation_completed`
- `occurred_at` TEXT NOT NULL
- `received_at` TEXT NOT NULL
- `app_version` TEXT NOT NULL

The primary key makes upload retries idempotent. User/time and
device-session/time indexes support dashboard queries without scanning the full
table.

### `manual_entries`

- `id` TEXT PRIMARY KEY
- `firebase_uid` TEXT NOT NULL
- `quantity` INTEGER NOT NULL, restricted to 1 through 10,000
- `occurred_on` TEXT NULL
- `note` TEXT NULL, limited to 200 Unicode code points
- `created_at` TEXT NOT NULL
- `updated_at` TEXT NOT NULL

### `manual_entry_revisions`

- `id` TEXT PRIMARY KEY
- `manual_entry_id` TEXT NOT NULL
- `firebase_uid` TEXT NOT NULL
- `operation` TEXT NOT NULL, restricted to `create`, `update`, or `delete`
- `quantity` INTEGER NOT NULL
- `occurred_on` TEXT NULL
- `note` TEXT NULL
- `created_at` TEXT NOT NULL

D1 migrations are versioned in the repository. Foreign-key behavior and query
indexes are declared explicitly and validated with representative query plans.

## 9. API Surface

Browser endpoints require `Authorization: Bearer <Firebase ID token>`. Desktop
endpoints require an opaque device token in an authorization scheme dedicated
to PolyFEA. Every response uses a small, versioned JSON envelope and stable
machine-readable error code.

### Browser account endpoints

- `POST /api/profile/bootstrap` — record verified identity and 13+ attestation.
- `GET /api/me/summary` — current user's counts and recent activity.
- `GET /api/me/devices` — list current user's linked sessions.
- `DELETE /api/me/devices/:id` — revoke an owned session.
- `POST /api/me/link-codes` — issue a single-use code.
- `GET /api/me/manual-entries` — list owned entries.
- `POST /api/me/manual-entries` — create an entry.
- `PATCH /api/me/manual-entries/:id` — edit an owned entry.
- `DELETE /api/me/manual-entries/:id` — delete an owned entry.
- `DELETE /api/me` — remove D1-owned account data before the browser completes
  Firebase account deletion.

### Desktop endpoints

- `POST /api/device/link` — exchange a valid code for one device token.
- `POST /api/device/events` — accept 1 through 100 automatic events in a
  request no larger than 64 KiB.

### Owner endpoints

- `GET /api/admin/summary` — aggregate users, linked users, launches,
  simulations, and self-reported quantities.
- `GET /api/admin/users` — paginated, searchable per-user rows.
- `GET /api/admin/users/:uid` — one user's counts, devices, and provenance.

Owner authorization is an exact allowlist of Firebase UIDs supplied as a
hosted secret. No browser value, email-domain match, or D1-editable profile flag
can grant owner access.

## 10. Offline Queue and Error Behavior

- Network work runs off the render and solver threads.
- Local events are appended before transmission and removed only after an
  explicit server acknowledgement.
- Unacknowledged events are never silently deleted or reassigned.
- Successful acknowledgements compact the queue.
- Retries use bounded exponential backoff with jitter.
- A transient network, DNS, TLS, rate-limit, or server error changes account
  status to `waiting to sync`; it does not block application behavior.
- An invalid or revoked token changes status to `relink required` and suspends
  uploads for that session.
- A malformed local queue is moved to a timestamped quarantine file. PolyFEA
  starts normally and explains that some pending counts need attention.
- The dashboard distinguishes `0` from `temporarily unavailable` and does not
  invent totals when an API request fails.

## 11. Security and Privacy Controls

- Firebase ID tokens are verified server-side for signature, issuer, audience,
  expiry, UID, and verified-email state.
- Link codes and device tokens are generated with a cryptographically secure
  random source and stored only as hashes.
- Link codes expire after ten minutes and work once. Issuance is limited to
  five codes per account per hour; exchange attempts are limited to thirty per
  request source per hour without retaining a raw IP address in D1.
- Request bodies have strict content type, size, field, enum, timestamp, batch,
  and string-length limits.
- D1 queries use prepared statements and ownership predicates.
- Manual-entry and device mutations require both record identity and current
  Firebase UID to match.
- API error messages do not reveal whether another user's record exists.
- Logs omit passwords, Firebase tokens, link codes, device tokens, model data,
  and manual-entry notes.
- Firebase client configuration is treated as public routing configuration;
  authorization depends on verified tokens and server rules, not secrecy of the
  web API key.
- Hosted secrets contain only values that must remain private, including the
  owner UID allowlist and token-hashing key material.
- Privacy and terms pages disclose Firebase and Cloudflare as processors, the
  collected fields, purpose, retention behavior, account deletion, and the 13+
  restriction in plain language.

Retention is purpose-limited and explicit: usage events, current manual
entries, their revision history, and non-secret device-session metadata remain
until account deletion; consumed or expired link-code rows are removed within
24 hours by cleanup on normal API traffic; device tokens are never retained in
plaintext; and operational logs use the shortest retention available on the
selected free tier. Account deletion removes all D1 rows owned by the Firebase
UID before the browser requests deletion of the Firebase identity.

## 12. Website Information Architecture

### Public landing page

1. **Hero:** “See how parts bend before you build them.”
2. A real PolyFEA deformation-heatmap screenshot.
3. A four-step explanation: load a model, create a mesh, apply material and
   forces, inspect deformation.
4. Student-relevant examples: robotics parts, printed brackets, beams, and
   competition mechanisms.
5. Capability explanations for STL/STEP/3MF input, CPU/CUDA solvers, real-time
   visualization, printed-layer analysis, and automated scenarios.
6. Credibility and limitations: MIT license, no commercial FEA solver, and an
   educational-not-certified-engineering disclaimer.
7. FAQ and initial Windows requirements.
8. A disabled **Windows download coming soon** call to action with no package
   link.

### Account surfaces

- Register, verify email, sign in, reset password, sign out, and delete account.
- Confirm 13+ status during registration.
- Show recorded launches, recorded simulations, self-reported runs, and the
  combined simulation total in separate labelled cards.
- Generate app-link codes, label/revoke device sessions, and manage manual
  entries.

### Owner surface

- Aggregate cards for accounts, linked accounts, launches, simulations, and
  self-reported quantities.
- Searchable, paginated per-user table.
- Per-user provenance view without impersonation or editing another user's
  counts.

### Legal surfaces

- Privacy page.
- Terms and educational-use disclaimer.
- Clear contact path for data questions and deletion problems.

## 13. Visual Direction and Accessibility

The site mirrors the application's visual language without copying its dense
desktop layout:

- light plotting-canvas background for content;
- charcoal technical panels;
- cyan primary accents;
- restrained red, green, and blue axis references;
- a stress-heatmap gradient used only where it communicates magnitude;
- strong typography and real product imagery instead of stock photography.

The result should feel precise, inventive, and welcoming rather than childish
or like a generic software landing-page template.

WCAG 2.1 AA is the acceptance target. Required behavior includes keyboard
navigation, semantic headings and landmarks, visible focus, labelled controls,
useful error text, sufficient contrast, reduced-motion support, responsive
layouts, and no information communicated by color alone.

## 14. Desktop Integration

The desktop work introduces focused modules rather than expanding the already
large `main.cpp` with networking details:

- `TelemetryClient` — public account/link/status/event interface.
- `TelemetryTransport` — HTTPS request boundary, injectable for tests.
- `TelemetryQueue` — append, acknowledge, retry, compact, and quarantine.
- `TelemetryTokenStore` — Windows Data Protection and local path ownership.
- A small SimpleUI account panel for code entry, link status, sync status, and
  unlink action.

Only narrow orchestration hooks enter `main.cpp`: initialize interactive
telemetry, emit one launch, mark specific solve jobs as countable, report a
successful finalize, draw the account panel, and shut down the background
worker cleanly.

Local state lives below the current Windows user's local application-data
directory in a PolyFEA-specific folder. The protected token and pending-event
queue are separate files so queue recovery never exposes authentication data.

## 15. Testing Strategy

### Worker and D1

- Firebase-token acceptance and rejection cases.
- Verified-email and 13+ bootstrap enforcement.
- Cross-user ownership denial for every record route.
- Owner allowlist enforcement.
- Link-code expiry, single use, hashing, guessing limits, and replay denial.
- Device-token hashing, revocation, and last-seen updates.
- Duplicate event IDs counted once across repeated batches.
- Event-type, timestamp, version, body-size, and batch-size validation.
- Manual create/update/delete provenance and aggregate calculations.
- Account-data deletion coverage.

### Website

- Registration, verification, sign-in, reset, and deletion states.
- Empty, loading, error, unlinked, linked, and relink-required dashboard states.
- Correct separation of recorded, self-reported, and combined totals.
- Owner route denial for ordinary users.
- Download placeholder has no download URL.
- Responsive layouts, keyboard traversal, focus management, semantics, and
  contrast.

### Desktop

- Token-store success and failure through an abstraction, without logging the
  token.
- Launch emitted at most once per interactive process.
- Successful countable job emits one simulation event.
- Failure, cancellation, meshing, and headless paths emit none.
- Offline append, successful acknowledgement, retry, duplicate retry, invalid
  token, relink boundary, and corrupt-queue quarantine.
- Existing `--regress all` remains network-free and produces no telemetry.

### End-to-end

A local test environment creates a test account, issues and exchanges a code,
uploads events twice, verifies idempotent totals, adds a manual entry, verifies
student/owner views, revokes the device, and confirms subsequent uploads fail
without affecting the application.

## 16. Deployment and Operations

- `website/` is an isolated web project with its own package manifest, lockfile,
  hosting configuration, D1 schema, migrations, environment example, and
  operational README.
- Hosted configuration supplies Firebase public web settings, Firebase project
  identity, owner Firebase UID allowlist, and private hashing material.
- Local development uses emulated or test services and never production user
  records.
- Free-tier usage and quota failures are surfaced in operational documentation.
- D1 data can be exported so a future provider change does not trap usage
  history.
- The repository remains the source of truth for site source and migrations.
- A third-party provider cannot guarantee “free forever”; the design targets
  current no-cost tiers and preserves migration paths.

Before public account deployment, the owner must provide or authorize creation
of a Firebase project, enable email/password sign-in, configure authorized
domains and verification email behavior, and identify the Firebase UID that
receives owner access.

## 17. Rollout Order

1. Scaffold and validate the isolated website project.
2. Implement D1 schema, migrations, API repositories, and authorization.
3. Implement Firebase account flows and private dashboards.
4. Complete the public educational landing and legal pages.
5. Implement the isolated Windows telemetry modules and SimpleUI link panel.
6. Add unit, integration, accessibility, and end-to-end tests.
7. Configure hosted Firebase and Cloudflare values.
8. Deploy and verify the public website, account flows, ownership isolation,
   telemetry synchronization, and the download placeholder.
9. Stop. Do not package or publish PolyFEA until the owner explicitly starts a
   later release-packaging phase.

## 18. Authoritative References

- Firebase pricing and no-cost plan: <https://firebase.google.com/pricing>
- Firebase Authentication REST API:
  <https://firebase.google.com/docs/reference/rest/auth>
- Firebase ID-token verification:
  <https://firebase.google.com/docs/auth/admin/verify-id-tokens>
- Cloudflare Workers pricing:
  <https://developers.cloudflare.com/workers/platform/pricing/>
- Cloudflare D1 prepared statements:
  <https://developers.cloudflare.com/d1/worker-api/prepared-statements/>
- GitHub Releases behavior and limits:
  <https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases>
- FTC COPPA FAQ:
  <https://www.ftc.gov/business-guidance/resources/complying-coppa-frequently-asked-questions>
