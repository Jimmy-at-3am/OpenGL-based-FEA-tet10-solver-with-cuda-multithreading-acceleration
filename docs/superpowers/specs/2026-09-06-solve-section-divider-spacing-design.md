# Solve Section Divider Spacing Design

## Scope

Fix the visible collision between the `GCODE SHOWCASE` heading and its blue
divider in the Solve inspector. Preserve the approved Apple-inspired visual
system and every existing control, value, callback, and computation path.

## Approved design

Use a stacked section header rather than placing the divider in the heading's
text band:

```text
GCODE SHOWCASE       label baseline
                     6 px baseline-to-divider separation
------------------   1.5 px System Blue divider
                     14 px divider-to-next-baseline separation
LOAD: ...            next content baseline
```

The effective heading size remains governed by the existing 11 px essential
text floor. For the current 9.5 px request, the layout therefore uses an
11 px label, a divider 6 px below its baseline, and a following-content
baseline 14 px below the divider. The subsequent Solve content moves down by
approximately 7 px; horizontal alignment and panel width do not change.

## Architecture

Add a small GL-free section-heading geometry contract to `UIDesign`. The
contract receives the section top and requested label size and returns the
label baseline, divider Y position, and following-content baseline. The live
G-code Solve panel consumes that contract, preventing its draw sequence from
reintroducing the collision when the accessibility text floor is active.

No rendering backend, input handling, job state, meshing, solver, load, or
result code changes.

## Verification

Before implementation, add a focused production-contract test with literal,
hand-checked geometry. It must fail against the current code and assert that:

- the 9.5 px request resolves through the 11 px text floor;
- the divider is below the heading baseline with the approved separation;
- the next content baseline remains below the divider.

Then rebuild the application, run the anchored UI/action/physics tests, run the
font-fallback gate, and run the unchanged headless regression suite. Visual
capture is optional because the local native OpenGL capture path remains
platform-limited; no blind GUI input is required.

## Self-review

- No placeholders or unresolved choices.
- The geometry, drawing order, and expected offsets are explicit.
- Scope is limited to the reported Solve-section overlap.
- The test exercises a production layout contract rather than parsing source.
