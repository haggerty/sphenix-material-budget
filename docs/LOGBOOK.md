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

---

## 2026-08-31 — Add development logbook

John asked for a dated logbook like `haggerty/TrackCaloMatch`'s
`docs/LOGBOOK.md`, to track the project's evolution. Added this file,
covering the three prior commits, in the same format, and linked it from
the README.

**Files added:** `docs/LOGBOOK.md`
**Files changed:** `README.md`
**Commit:** `f3bfd17`

---

## 2026-09-01 — Vertex/φ-sector smearing scan, repo reorg, and a real Geant4 bug

John asked for a second study: smear the ray's starting vertex along z
(Gaussian, σ=10 cm — the real sPHENIX luminous region) and vary φ over
about one HCal sector (2π/64), to see how the idealized single-ray picture
shifts once the real luminous region and sector structure are accounted
for. He also asked to keep the original study intact by splitting the repo
into per-study subdirectories.

**Repo reorg.** Moved the existing scan into `baseline_scan/` (`git mv`,
preserving history) and factored the shared subsystem-classification logic
(previously duplicated inline in `MaterialScan.C`) out into
`common/SubsystemClassifier.h`, included by both studies. Verified the
reorg was lossless by rerunning `MaterialScan.C` from its new location and
diffing the output CSV byte-for-byte against the pre-reorg version.

**New scan macro.** Added `smearing_scan/VertexPhiSmearScan.C`: for each η
point, fires `nSamples` rays with `z_vtx ~ Gaussian(0, 10cm)` and
`phi ~ Uniform` over a 2π/64-wide window centered on each of two regions
(SECTOR at φ≈0, CHIMNEY at φ≈π/2, mirroring the baseline scan's two φ
points), aggregating mean±RMS per subsystem rather than writing out every
ray. `PlotVertexPhiSmear.C` plots EMCAL/MAGNET/IHCAL/OHCAL as a mean line
with a shaded RMS band per region.

**Found and fixed a real Geant4 navigation bug.** The randomized starting
z (unlike the baseline scan, which always starts at z≈0) triggered a
reproducible SIGSEGV: a null voxel-header pointer inside
`G4ParameterisedNavigation::ParamVoxelLocate`, confirmed via a gdb
backtrace after first misdiagnosing it as a stale-navigator-state issue (a
"fresh navigator per ray" fix did not help) and then as a shell-quoting
issue (gdb was initially debugging the `root` wrapper script and silently
detaching from the real `root.exe` child on fork). Root-cause bisection
showed the crash isn't a rare edge case — it hits ~34% of all rays in the
SECTOR region (φ≈0) across the *entire* η range, for z-offsets as small as
under 1cm, while the CHIMNEY region (φ≈π/2) is entirely unaffected (0
crashes in the full production run). A first fix attempt sandboxed each
ray in its own forked child process reporting results over a pipe; this
fought ROOT/`TSystem`'s own `SIGCHLD` handling and gave unreliable
results (reads came back short even for children that had exited cleanly
with the full result already written). Replaced it with an in-process
`sigsetjmp`/`siglongjmp` guard around each ray's navigation, safe to
recover from since the fault is a null-pointer *read* (confirmed by the
backtrace and `dmesg`'s `error 4`) and each ray already gets a fresh,
disposable `G4Navigator`.

**External review.** Shared the project with Chris Pinkenburg (sPHENIX),
who pointed to the collaboration's own
[`tutorials/materialscan`](https://github.com/sPHENIX-Collaboration/tutorials/tree/master/materialscan)
tool (built on Geant4's `G4MaterialScanner`, scanning only from a fixed
vertex) — missed by the original "no existing tool" check since it lives
outside `macros`/`coresoftware`. He also flagged two features in the
baseline scan's OHCal curve as likely unphysical: an upward jump at η=0
("we have a gap in the scintillators at eta=0 but not in the steel...that
jump cannot be real") and a jump at |η|≈0.4. Traced the η=0 jump to its
exact cause with a targeted debug dump comparing three rays: the two
"spiking" rows pick up one extra ~1.8cm slice of OHCal outer steel
(`av_13_impr_23_OuterHCalSector_Steel_pv_0`) that a clean reference ray
(η=0.1) never touches, while the scintillator contribution is actually
*slightly lower* near η=0 as expected — confirming Chris's read that the
real (scintillator-gap) effect is present but swamped by an unrelated
grazing/edge-of-volume navigation artifact, the same class of bug as the
already-documented EMCal η=0 seam but not cured by the existing half-bin
offset for this OHCal boundary. The |η|≈0.4 jump remains unexplained
(likely the same class of effect, not yet traced to a specific volume).
Documented both as caveats in `baseline_scan/README.md`.

**Production run.** Ran the full scan (200 samples/η/region, ~44k rays
total); 7,527 of 22,200 SECTOR rays dropped to the navigator bug above
(~34%, matching the smaller test), 0 CHIMNEY drops, no η bin lost
entirely. Generated and reviewed the mean±RMS plots.

**Files added:** `common/SubsystemClassifier.h`, `baseline_scan/README.md`,
`smearing_scan/VertexPhiSmearScan.C`, `smearing_scan/PlotVertexPhiSmear.C`,
`smearing_scan/README.md`, `smearing_scan/vertex_phi_smear.csv`,
`smearing_scan/smear_X0_vs_eta.{pdf,png}`,
`smearing_scan/smear_lambdaI_vs_eta.{pdf,png}`
**Files moved:** `MaterialScan.C`, `PlotMaterialScan.C`,
`PlotSubsystemBudget.C`, `material_scan.csv`, `X0_vs_eta.{pdf,png}`,
`lambdaI_vs_eta.{pdf,png}`, `subsystem_X0_vs_eta.{pdf,png}`,
`subsystem_lambdaI_vs_eta.{pdf,png}` → `baseline_scan/`
**Files changed:** `README.md`
**Commit:** `58c340f`

---

## 2026-09-01 — Traced the |eta|~0.4 OHCal jump

Followed up on the |η|≈0.4 OHCal jump left unexplained in the previous
entry. Extended the same per-step debug-dump technique used for the η=0
seam to a run of neighboring rays across η=0.30–0.42, and found the same
class of effect at a different boundary: the *outermost* OHCal steel
plate's path length swings sharply between adjacent η bins 0.02 apart
(e.g. η=0.36 crosses it in two sub-steps totaling 1.76 X₀-equivalent path
length, η=0.38 crosses the same plate in one ~2cm-shorter sub-step, 0.68
X₀-equivalent) — a grazing/edge-of-volume sensitivity in the ray's exit
angle through that outer boundary, not a real detector feature, same
underlying phenomenon as the η=0 seam.

**Files changed:** `baseline_scan/README.md`
