# sPHENIX Material Budget

Radiation length (X₀) and nuclear interaction length (λ_I) traversed by a
particle from the vertex, as a function of pseudorapidity η, for the full
sPHENIX detector — broken down per subsystem in the order material is
actually added going outward in radius: beampipe, MVTX, INTT, TPC, EMCal,
solenoid magnet, IHCal, OHCal. Built directly on the real Geant4 detector
geometry (via `PHG4Reco`) rather than an analytic approximation.

Three studies live here, sharing the subsystem-classification logic in
[`common/`](common/):

- **[`baseline_scan/`](baseline_scan/)** — the idealized case: one ray per
  (η, φ) from a fixed vertex at z=0, at two representative φ values.
- **[`smearing_scan/`](smearing_scan/)** — a Monte Carlo companion that
  instead smears the vertex position (Gaussian, σ=10 cm in z) and φ (uniform
  over one HCal sector), to see how much the idealized picture shifts once
  the real luminous region and sector structure are accounted for. This also
  surfaced a real Geant4 navigation bug — see that directory's README.
- **[`boundary_artifact_scan/`](boundary_artifact_scan/)** — characterizes
  (without attempting to fix) the ray-vs-volume-boundary grazing artifacts
  found in `baseline_scan`: a fine-grained η scan plus automated outlier
  detection, catalogued by which volume is responsible.

See each subdirectory's README for method, caveats, and how to run it.

## Requirements

- An sPHENIX offline software environment (this was built/run against release
  `ana.568` on the SDCC cluster).
- A checkout of the [sPHENIX macros repo](https://github.com/sPHENIX-Collaboration/macros),
  used only for its per-detector `G4Setup_sPHENIX.C` geometry-construction
  helpers, cloned at this repo's root (shared by both studies):

  ```
  git clone --depth 1 https://github.com/sPHENIX-Collaboration/macros.git
  ```

## Caveats

- Only the 8 named subsystems are built; MBD, EPD, ZDC/beamline,
  micromegas/TPOT, and the plug door are deliberately excluded, so this is
  the material budget of those 8 systems, not the full as-installed detector.
- sPHENIX's own [tutorials repo](https://github.com/sPHENIX-Collaboration/tutorials/tree/master/materialscan)
  has a material-scan tool built on Geant4's `G4MaterialScanner`, independent
  of this one — worth cross-checking against, since it only ever scans from a
  fixed vertex and so wouldn't reproduce the smearing-scan's navigation-bug
  finding, but would help separate real detector features from artifacts of
  this repo's own hand-rolled navigator loop.

## Development log

`docs/LOGBOOK.md` records the evolution of this project: what was
requested and what was done, with commit references, in chronological order.
