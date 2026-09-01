#ifndef MACRO_ARTIFACTSCAN_C
#define MACRO_ARTIFACTSCAN_C

// Stage 1 of the boundary-artifact characterization: a fine-grained eta scan
// (default step 0.005, vs. baseline_scan's 0.02) at the same two phi values
// as baseline_scan/MaterialScan.C, recording not just per-subsystem X0/
// lambdaI totals but also a per-*leaf-volume* breakdown for the four
// subsystems worth reading individually (EMCAL/MAGNET/IHCAL/OHCAL -- same
// choice as PlotSubsystemBudget.C).
//
// This is what let baseline_scan/README.md's eta=0 and |eta|=0.4 OHCal
// artifacts get pinned down to a specific volume by hand (a per-step debug
// dump compared by eye between a few rays); FindJumps.C automates that:
// it scans the per-subsystem series for adjacent-bin outliers, then uses
// the per-volume rows written here to diff exactly which volume changed.

#include <GlobalVariables.C>
#include "G4Setup_sPHENIX.C"
#include "../common/SubsystemClassifier.h"

#include <fun4all/Fun4AllServer.h>
#include <phool/recoConsts.h>

#include <Geant4/G4LogicalVolume.hh>
#include <Geant4/G4Material.hh>
#include <Geant4/G4Navigator.hh>
#include <Geant4/G4SystemOfUnits.hh>
#include <Geant4/G4ThreeVector.hh>
#include <Geant4/G4TouchableHistory.hh>
#include <Geant4/G4TransportationManager.hh>
#include <Geant4/G4VPhysicalVolume.hh>

#include <TSystem.h>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

R__LOAD_LIBRARY(libg4detectors.so)
R__LOAD_LIBRARY(libfun4all.so)

int ArtifactScan(const std::string &subsystemOutfile = "artifact_scan_subsystem.csv",
                  const std::string &volumeOutfile = "artifact_scan_volume.csv",
                  double etaStep = 0.005)
{
  Fun4AllServer *se = Fun4AllServer::instance();
  se->Verbosity(0);
  recoConsts *rc = recoConsts::instance();
  rc->set_IntFlag("RUNNUMBER", 1);
  rc->set_StringFlag("CDB_GLOBALTAG", CDB::global_tag);
  rc->set_uint64Flag("TIMESTAMP", CDB::timestamp);

  G4WORLD::WorldMaterial = "G4_Galactic";

  Enable::PIPE = true;
  Enable::MVTX = true;
  Enable::INTT = true;
  Enable::TPC = true;
  Enable::CEMC = true;
  Enable::MAGNET = true;
  Enable::HCALIN = true;
  Enable::HCALOUT = true;

  G4Init();
  G4Setup();
  se->BeginRun(1);

  G4VPhysicalVolume *world = G4TransportationManager::GetTransportationManager()
                                 ->GetNavigatorForTracking()
                                 ->GetWorldVolume();
  if (!world)
  {
    std::cout << "ArtifactScan: no world volume after BeginRun, aborting" << std::endl;
    gSystem->Exit(1);
  }

  G4Navigator *nav = new G4Navigator();
  nav->SetWorldVolume(world);

  const double etaMin = -1.1;
  const double etaMax = 1.1;
  const std::vector<double> phiList = {0.0, M_PI / 2.0};
  const double maxR = 500.0 * cm;
  const long maxSteps = 2000000;

  // Only these four get per-volume rows -- same choice as
  // baseline_scan/PlotSubsystemBudget.C, since they're the ones close
  // enough together to be hard to read individually, and where both known
  // artifacts so far have shown up.
  const std::set<std::string> volumeDetailTags = {"EMCAL", "MAGNET", "IHCAL", "OHCAL"};

  const int nEta = static_cast<int>(std::round((etaMax - etaMin) / etaStep)) + 1;

  std::ofstream foutSub(subsystemOutfile);
  foutSub << std::setprecision(15);
  foutSub << "phi,eta,subsystem,sumX0,sumLambdaI\n";

  std::ofstream foutVol(volumeOutfile);
  foutVol << std::setprecision(15);
  foutVol << "phi,eta,subsystem,volume,sumX0,sumLambdaI\n";

  for (double phi : phiList)
  {
    for (int ie = 0; ie < nEta; ++ie)
    {
      double eta = etaMin + ie * etaStep;
      // Same half-bin-offset convention as baseline_scan/MaterialScan.C,
      // so this fine scan is a strict refinement of the same method (and
      // will independently rediscover the same eta=0 EMCal seam as a
      // built-in sanity check on the jump-detection logic).
      double etaRay = eta + 0.5 * etaStep;
      double theta = 2.0 * std::atan(std::exp(-etaRay));
      G4ThreeVector dir(std::sin(theta) * std::cos(phi),
                         std::sin(theta) * std::sin(phi),
                         std::cos(theta));
      dir = dir.unit();
      G4ThreeVector pos(0.0, 0.0, 1.0e-4 * cm);

      std::map<std::string, double> sumX0, sumLambdaI;
      std::map<std::string, double> volX0, volLambdaI;  // key: tag + "\x1f" + volumeName

      G4VPhysicalVolume *vol = nav->LocateGlobalPointAndSetup(pos, &dir, false);
      double traveled = 0.0;
      long nsteps = 0;

      while (vol && traveled < maxR && nsteps < maxSteps)
      {
        ++nsteps;
        G4Material *mat = vol->GetLogicalVolume()->GetMaterial();
        G4double safety = 0.0;
        G4double step = nav->ComputeStep(pos, dir, maxR - traveled, safety);
        if (step >= maxR) step = maxR - traveled;
        if (step <= 0.0) step = 1.0e-6 * cm;

        G4TouchableHistory *touch = nav->CreateTouchableHistory();
        std::string tag = MatScan::ClassifyTouchable(touch);
        delete touch;

        double radlen = mat->GetRadlen();
        double nuclIntLen = mat->GetNuclearInterLength();
        double x0 = radlen > 0 ? step / radlen : 0.0;
        double lam = nuclIntLen > 0 ? step / nuclIntLen : 0.0;

        sumX0[tag] += x0;
        sumLambdaI[tag] += lam;

        if (volumeDetailTags.count(tag) && (x0 > 0.0 || lam > 0.0))
        {
          std::string key = tag + "\x1f" + vol->GetName();
          volX0[key] += x0;
          volLambdaI[key] += lam;
        }

        pos += step * dir;
        traveled += step;
        nav->SetGeometricallyLimitedStep();
        vol = nav->LocateGlobalPointAndSetup(pos, &dir, true);
      }

      std::set<std::string> tags;
      for (auto &kv : sumX0) tags.insert(kv.first);
      for (auto &kv : sumLambdaI) tags.insert(kv.first);
      for (const auto &tag : tags)
      {
        foutSub << phi << "," << eta << "," << tag << ","
                << sumX0[tag] << "," << sumLambdaI[tag] << "\n";
      }

      std::set<std::string> volKeys;
      for (auto &kv : volX0) volKeys.insert(kv.first);
      for (auto &kv : volLambdaI) volKeys.insert(kv.first);
      for (const auto &key : volKeys)
      {
        size_t sep = key.find('\x1f');
        std::string tag = key.substr(0, sep);
        std::string volName = key.substr(sep + 1);
        foutVol << phi << "," << eta << "," << tag << "," << volName << ","
                << volX0[key] << "," << volLambdaI[key] << "\n";
      }
    }
    std::cout << "ArtifactScan: finished phi = " << phi << std::endl;
  }
  foutSub.close();
  foutVol.close();

  std::cout << "ArtifactScan: wrote " << subsystemOutfile << " and " << volumeOutfile << std::endl;
  return 0;
}
#endif
