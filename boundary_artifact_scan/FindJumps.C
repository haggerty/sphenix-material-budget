#ifndef MACRO_FINDJUMPS_C
#define MACRO_FINDJUMPS_C

// Stage 2 of the boundary-artifact characterization. Reads the two CSVs
// from ArtifactScan.C and:
//   1. For each (phi, subsystem), walks the fine-grained eta series looking
//      for adjacent-bin swings in X0 that are outliers relative to that
//      series' own typical bin-to-bin variation (a MAD-based threshold, so
//      a naturally noisy sawtooth-y subsystem doesn't drown a smooth one in
//      false positives, floored by an absolute minimum so a perfectly
//      smooth series doesn't flag its own numerical noise).
//   2. For each flagged jump, looks up the per-volume breakdown on both
//      sides (from the volume CSV) and reports the volume(s) whose own
//      contribution changed the most -- automating the by-hand technique
//      used to pin down the eta=0 and |eta|=0.4 OHCal artifacts in
//      baseline_scan/README.md.
//
// Output: artifacts_catalog.csv, sorted by |deltaX0| descending, plus a
// human-readable top-N summary on stdout.

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace FindJumpsDetail
{
  struct SubRow
  {
    std::string phiStr, etaStr;
    double eta, x0, lam;
  };

  struct VolRow
  {
    std::string volume;
    double x0, lam;
  };

  std::vector<std::string> Split(const std::string &line)
  {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, ',')) out.push_back(tok);
    return out;
  }
}  // namespace FindJumpsDetail

int FindJumps(const std::string &subsystemInfile = "artifact_scan_subsystem.csv",
              const std::string &volumeInfile = "artifact_scan_volume.csv",
              const std::string &outCatalog = "artifacts_catalog.csv",
              double kMAD = 6.0,
              double minAbsX0 = 0.05,
              int topN = 20)
{
  using namespace FindJumpsDetail;

  // subsystem[phi][subsystem] -> rows, in file order (already eta-ordered
  // by construction since ArtifactScan.C writes them that way).
  std::map<std::string, std::map<std::string, std::vector<SubRow>>> subsystem;

  {
    std::ifstream fin(subsystemInfile);
    if (!fin.good())
    {
      std::cout << "FindJumps: cannot open " << subsystemInfile << std::endl;
      return 1;
    }
    std::string line;
    std::getline(fin, line);  // header
    while (std::getline(fin, line))
    {
      if (line.empty()) continue;
      auto f = Split(line);
      if (f.size() < 5) continue;
      SubRow r;
      r.phiStr = f[0];
      r.etaStr = f[1];
      r.eta = std::stod(f[1]);
      r.x0 = std::stod(f[3]);
      r.lam = std::stod(f[4]);
      subsystem[r.phiStr][f[2]].push_back(r);
    }
  }

  // volume[phi|eta|subsystem] -> per-volume rows.
  std::map<std::string, std::vector<VolRow>> volume;
  {
    std::ifstream fin(volumeInfile);
    if (!fin.good())
    {
      std::cout << "FindJumps: cannot open " << volumeInfile << std::endl;
      return 1;
    }
    std::string line;
    std::getline(fin, line);  // header
    while (std::getline(fin, line))
    {
      if (line.empty()) continue;
      auto f = Split(line);
      if (f.size() < 6) continue;
      std::string key = f[0] + "|" + f[1] + "|" + f[2];
      VolRow r;
      r.volume = f[3];
      r.x0 = std::stod(f[4]);
      r.lam = std::stod(f[5]);
      volume[key].push_back(r);
    }
  }

  struct Jump
  {
    std::string phiStr, subsystem;
    double etaBefore, etaAfter, x0Before, x0After, deltaX0, deltaLam;
    std::string topVolume;
    double topVolumeDelta;
  };
  std::vector<Jump> jumps;

  for (auto &phiKv : subsystem)
  {
    const std::string &phiStr = phiKv.first;
    for (auto &subKv : phiKv.second)
    {
      const std::string &sub = subKv.first;
      auto &rows = subKv.second;  // already eta-ordered
      if (rows.size() < 3) continue;

      std::vector<double> diffs(rows.size() - 1);
      for (size_t i = 0; i + 1 < rows.size(); ++i)
      {
        diffs[i] = rows[i + 1].x0 - rows[i].x0;
      }
      std::vector<double> absDiffs(diffs.size());
      for (size_t i = 0; i < diffs.size(); ++i) absDiffs[i] = std::fabs(diffs[i]);
      std::vector<double> sorted = absDiffs;
      std::sort(sorted.begin(), sorted.end());
      double mad = sorted.empty() ? 0.0 : sorted[sorted.size() / 2];
      double threshold = std::max(kMAD * mad, minAbsX0);

      for (size_t i = 0; i < diffs.size(); ++i)
      {
        if (std::fabs(diffs[i]) <= threshold) continue;

        Jump j;
        j.phiStr = phiStr;
        j.subsystem = sub;
        j.etaBefore = rows[i].eta;
        j.etaAfter = rows[i + 1].eta;
        j.x0Before = rows[i].x0;
        j.x0After = rows[i + 1].x0;
        j.deltaX0 = diffs[i];
        j.deltaLam = rows[i + 1].lam - rows[i].lam;
        j.topVolume = "";
        j.topVolumeDelta = 0.0;

        std::string keyBefore = phiStr + "|" + rows[i].etaStr + "|" + sub;
        std::string keyAfter = phiStr + "|" + rows[i + 1].etaStr + "|" + sub;
        std::map<std::string, double> before, after;
        if (volume.count(keyBefore))
          for (auto &v : volume[keyBefore]) before[v.volume] += v.x0;
        if (volume.count(keyAfter))
          for (auto &v : volume[keyAfter]) after[v.volume] += v.x0;

        std::set<std::string> names;
        for (auto &kv : before) names.insert(kv.first);
        for (auto &kv : after) names.insert(kv.first);
        for (const auto &name : names)
        {
          double d = (after.count(name) ? after[name] : 0.0) -
                     (before.count(name) ? before[name] : 0.0);
          if (std::fabs(d) > std::fabs(j.topVolumeDelta))
          {
            j.topVolumeDelta = d;
            j.topVolume = name;
          }
        }

        jumps.push_back(j);
      }
    }
  }

  std::sort(jumps.begin(), jumps.end(), [](const Jump &a, const Jump &b)
            { return std::fabs(a.deltaX0) > std::fabs(b.deltaX0); });

  std::ofstream fout(outCatalog);
  fout << std::setprecision(10);
  fout << "phi,subsystem,etaBefore,etaAfter,X0Before,X0After,deltaX0,deltaLambdaI,"
          "topVolume,topVolumeDeltaX0\n";
  for (const auto &j : jumps)
  {
    fout << j.phiStr << "," << j.subsystem << "," << j.etaBefore << "," << j.etaAfter
         << "," << j.x0Before << "," << j.x0After << "," << j.deltaX0 << ","
         << j.deltaLam << "," << j.topVolume << "," << j.topVolumeDelta << "\n";
  }
  fout.close();

  std::cout << "FindJumps: " << jumps.size() << " jumps flagged, wrote " << outCatalog
             << std::endl;
  std::cout << "\nTop " << std::min<int>(topN, jumps.size()) << " by |deltaX0|:\n";
  std::cout << std::left << std::setw(8) << "phi" << std::setw(8) << "sub"
             << std::setw(10) << "etaBefore" << std::setw(10) << "etaAfter"
             << std::setw(12) << "deltaX0" << "topVolume" << std::endl;
  for (int i = 0; i < std::min<int>(topN, jumps.size()); ++i)
  {
    const auto &j = jumps[i];
    std::cout << std::left << std::setw(8) << j.phiStr << std::setw(8) << j.subsystem
               << std::setw(10) << j.etaBefore << std::setw(10) << j.etaAfter
               << std::setw(12) << j.deltaX0 << j.topVolume << std::endl;
  }

  return 0;
}
#endif
