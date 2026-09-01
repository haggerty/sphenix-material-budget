# sPHENIX Material Budget Development Logbook

Dated entries recording what was requested and what was done, in enough
detail that someone later can judge what happened and why — not a full
transcript. Entries are in chronological order (oldest first); append
new ones at the end.

This logbook was started on 2026-08-31, covering the project's first
three commits from that same day; entries from here on are recorded
from the actual working sessions as the project continues.

---

## 2026-08-31 — Initial material budget scan (X0, lambdaI vs eta)

Built the project from scratch: sPHENIX has no existing material-budget
tool (checked both `macros` and `coresoftware`), so `MaterialScan.C`
constructs the real Geant4 geometry for 8 subsystems (beampipe, MVTX,
INTT, TPC, EMCal, solenoid magnet, IHCal, OHCal) via `PHG4Reco` and
`Fun4AllServer::BeginRun()`, then walks a `G4Navigator` radially outward
from the vertex across a grid of η ∈ [-1.1, 1.1] (step 0.02) at φ = 0
and π/2, accumulating X₀ and λ_I per step and classifying each step
into a subsystem by its full volume ancestry.

Hit and diagnosed a real Geant4 navigation degeneracy along the way: a
ray fired at exactly η=0 runs along the z=0 seam between sPHENIX's
z-symmetric, projective EMCal tower halves and can graze past all of
them for the full radial depth, reading as ~0.38 X₀ instead of the
~19–22 X₀ every neighboring η sees. Fixed by offsetting each grid η by
half a bin step before computing the ray direction, while still writing
out the unshifted η — documented in the README as "a genuine geometric
gotcha."

`PlotMaterialScan.C` reads the resulting CSV and produces stacked X₀-
and λ_I-vs-η plots (φ=0 and π/2 side by side), stacked in true physical
radial order.

**Files added:** `MaterialScan.C`, `PlotMaterialScan.C`, `README.md`,
`material_scan.csv`, `X0_vs_eta.{pdf,png}`, `lambdaI_vs_eta.{pdf,png}`,
`.gitignore`
**Commit:** `536b813`

---

## 2026-08-31 — Unstacked per-subsystem overlay plots

John asked to find the thickness of EMCAL, IHCAL, MAGNET, and OHCAL
separately. Clarifying what "thickness" meant surfaced that the real
need wasn't physical radial cm at all — it was X₀ and λ_I per
subsystem, individually readable, which the existing stacked plot
couldn't provide: EMCAL and OHCAL's large contributions buried IHCAL
and MAGNET's much smaller ones underneath them in the stack.

Added `PlotSubsystemBudget.C`, which reads the same `material_scan.csv`
(no rescan needed) and overlays EMCAL/MAGNET/IHCAL/OHCAL as four
separate lines instead of stacking them, for both X₀ and λ_I vs η.
Confirmed the result is actually readable: at φ=0, OHCAL runs
~35–50 X₀ / ~4–5.5 λ_I, EMCAL ~18–26 X₀ / ~0.8–1.2 λ_I, and IHCAL/
MAGNET — previously indistinguishable — sit close together at
~0.5–2 X₀ / ~0.3–0.7 λ_I.

**Files added:** `PlotSubsystemBudget.C`,
`subsystem_X0_vs_eta.{pdf,png}`, `subsystem_lambdaI_vs_eta.{pdf,png}`
**Files changed:** `README.md`
**Commit:** `99b1bce`

---

## 2026-08-31 — README caveat for the new overlay's scope

Added a caveat noting `PlotSubsystemBudget.C` deliberately covers only
EMCAL/MAGNET/IHCAL/OHCAL — the four subsystems close enough together in
the stacked plots to be hard to read individually — and omits PIPE/
MVTX/INTT/TPC, which are already readable at the bottom of the stack.

**Files changed:** `README.md`
**Commit:** `00b6497`
