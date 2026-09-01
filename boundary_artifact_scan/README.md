# Boundary-artifact characterization

`baseline_scan/README.md` documents two ray-vs-volume-boundary grazing
artifacts found by hand (η=0 in EMCal/OHCal, |η|≈0.4 in OHCal). Actually
fixing them would mean tracking down and patching the underlying sPHENIX
geometry construction — bigger than the scope of this repo. This
subdirectory instead automates the by-hand technique used to find those two,
to characterize *how many* such artifacts exist and *where*, without
attempting to fix any of them.

## Method

**Stage 1 — `ArtifactScan.C`.** A fine-grained η scan (default step 0.005,
vs. `baseline_scan`'s 0.02) at the same two φ values, using the same
half-bin-offset ray convention as `baseline_scan/MaterialScan.C`. Writes two
CSVs: per-subsystem X₀/λ_I totals (`artifact_scan_subsystem.csv`, same
shape as the baseline scan's output), and a per-*leaf-volume* breakdown
(`artifact_scan_volume.csv`) restricted to EMCAL/MAGNET/IHCAL/OHCAL — the
same four subsystems `PlotSubsystemBudget.C` singles out, and where both
known artifacts showed up.

**Stage 2 — `FindJumps.C`.** For each (φ, subsystem), walks the fine η
series and flags adjacent-bin swings in X₀ that are outliers relative to
that series' own typical bin-to-bin variation — a threshold of
`max(6 × median absolute adjacent diff, 0.05 X₀)`, so a naturally noisy
subsystem doesn't drown a smooth one in false positives, floored so a
perfectly smooth series doesn't flag its own numerical noise. For every
flagged jump, looks up the per-volume breakdown on both sides and reports
whichever volume's own contribution changed the most — automating exactly
the by-hand diff used to pin down the two known artifacts.

Output: `artifacts_catalog.csv`, one row per flagged jump, sorted by
`|deltaX0|` descending.

## Running

```bash
cd boundary_artifact_scan
export ROOT_INCLUDE_PATH="$PWD/../macros/common:$PWD/../macros/detectors/sPHENIX:$ROOT_INCLUDE_PATH"
root -b -q 'ArtifactScan.C()'
root -b -q 'FindJumps.C("artifact_scan_subsystem.csv", "artifact_scan_volume.csv")'
```

## Findings

362 jumps flagged in total. By size:

| \|ΔX₀\| | count | what it is |
|---|---|---|
| > 10 | 6 | the known η=0 EMCal seam, plus a new chimney-sector artifact |
| 1–10 | 177 | mostly EMCal tower-to-tower transitions, plus more of the chimney artifact and the known |η|≈0.4 OHCal jump |
| 0.2–1 | 42 | smaller instances of the same |
| 0.05–0.2 | 137 | ordinary tower/plate granularity texture |

**The known η=0 seam reappears, at a smaller scale.** Both φ regions show a
~20 X₀ swing in EMCAL between η=-0.01 and η=0.005 (`CEMC_0_Tower_992` /
`CEMC_0_Tower_1012`) — the same seam documented in `baseline_scan/README.md`,
rediscovered independently by this automated scan (a useful sanity check on
the detection logic). Notably, it shows up *despite* the half-bin-offset
mitigation, because that offset is sized relative to the grid step
(0.0025 here vs. 0.01 for `baseline_scan`'s coarser grid) — the mitigation
only pushes the ray far enough from the exact seam to clear it at the grid
resolution it was tuned for, not the seam's actual angular width. A finer
grid re-exposes it at a smaller offset.

**A new, larger artifact in the chimney sector.** The single biggest
non-seam jump is in OHCAL's φ≈π/2 (chimney) region, around η≈-0.75:
two consecutive ~11–16 X₀ swings, both attributed to
`av_14_impr_1_OuterHCalChimneySector_Steel_pv_0` — a steel volume specific
to the chimney/service-penetration sector that neither of the two by-hand
investigations had looked at. Not yet characterized further than "this
volume's path length swings by 10+ X₀ within a 0.005 η step around here."

**The |η|≈0.4 OHCal jump, and a sibling at |η|≈0.74–0.75.** The
already-documented jump appears here too, and there's a second, similarly-
sized (~5.8 X₀) pair of jumps at |η|≈0.74–0.75 in `av_13_impr_23_OuterHCalSector_Steel_pv_0`
(the ordinary, non-chimney sector's outer steel), present in *both* φ
regions — i.e. a real feature of the sector geometry itself, not specific to
the chimney cutout.

**Edge-of-acceptance jumps in EMCal near |η|≈1.0–1.1.** Several ~5–7 X₀
single-tower swings cluster near the edge of EMCal's projective coverage
(e.g. η=1.035→1.04, η=-1.09→-1.085). Not yet determined whether these are
the same grazing/edge-of-volume effect or a genuine feature of running out
of projective towers near the acceptance boundary.

**Everything below ~1 X₀** is consistent with ordinary tower/plate
granularity (adjacent EMCal towers or OHCal tiles legitimately differing by
a modest amount) rather than a distinct navigation artifact, and isn't
itemized further here — see `artifacts_catalog.csv` for the full list.

## Caveats

- This characterizes; it does not fix. None of the underlying sPHENIX
  geometry construction was touched.
- The threshold (`kMAD=6`, floor `0.05 X₀`) is a judgment call, tuned by
  inspecting the resulting catalog rather than derived from first
  principles — a stricter or looser threshold would shift the boundary
  between "flagged" and "ordinary granularity" in the tables above.
- Only EMCAL/MAGNET/IHCAL/OHCAL get per-volume detail (see Method); a jump
  in PIPE/MVTX/INTT/TPC would still show up in the subsystem-level catalog
  row but without a `topVolume` attribution.
- Only φ=0 and φ=π/2 are scanned, matching the baseline scan; artifacts
  specific to other φ values (there are 64 HCal-sector-widths around the
  full azimuth) would not be found here.
