#ifndef MACRO_MATERIALSCAN_C
#define MACRO_MATERIALSCAN_C

#include <GlobalVariables.C>
#include "G4Setup_sPHENIX.C"

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

namespace MatScan
{
  // Classify a single volume name into one of the 8 requested subsystems.
  // Order matters: more specific / higher-priority checks first.
  std::string ClassifyName(const std::string &name)
  {
    if (name.find("MVTX") != std::string::npos)
    {
      return "MVTX";
    }
    if (name.find("siactive") != std::string::npos ||
        name.find("siinactive") != std::string::npos ||
        name.find("si_glue") != std::string::npos ||
        name.find("si_support") != std::string::npos ||
        name.find("hdi_kapton") != std::string::npos ||
        name.find("hdi_copper") != std::string::npos ||
        name.find("hdikapton") != std::string::npos ||
        name.find("hdicopper") != std::string::npos ||
        name.find("fphx") != std::string::npos ||
        name.find("ladder") != std::string::npos ||
        name.find("stave") != std::string::npos ||
        name.find("rohacell") != std::string::npos)
    {
      return "INTT";
    }
    if (name.find("tpc_") != std::string::npos || name.find("TPC_ENDCAP") != std::string::npos)
    {
      return "TPC";
    }
    if (name.find("CEMC") != std::string::npos)
    {
      return "EMCAL";
    }
    if (name == "CRYOSTAT" || name == "CRYOINT" || name == "THERM" ||
        name == "THERMVAC" || name == "COILSUP" || name == "COIL" ||
        name == "CONNECTOR" || name == "BusbarD")
    {
      return "MAGNET";
    }
    // case-sensitive on purpose: IHCal's "Hcal_envelope" (lowercase cal) must
    // never substring-match OHCal's "OHCal_envelope" (capital Cal)
    if (name == "Hcal_envelope" || name == "IHCalEnvelope")
    {
      return "IHCAL";
    }
    if (name == "OHCal_envelope" || name == "OHCal" ||
        name.find("HCAL_SPT_N1") != std::string::npos)
    {
      return "OHCAL";
    }
    if (name.find("PIPE") != std::string::npos || name.find("FLANGE") != std::string::npos)
    {
      return "PIPE";
    }
    return "";
  }

  // Classify a navigation step by walking the full ancestry (deepest volume
  // out to world), so daughters placed inside a named envelope (IHCal, OHCal,
  // magnet cryostat, ...) are correctly attributed even if their own local
  // name doesn't carry a recognizable keyword.
  std::string ClassifyTouchable(const G4VTouchable *touch)
  {
    for (G4int d = touch->GetHistoryDepth(); d >= 0; --d)
    {
      G4VPhysicalVolume *vol = touch->GetVolume(d);
      if (!vol)
      {
        continue;
      }
      std::string tag = ClassifyName(vol->GetName());
      if (!tag.empty())
      {
        return tag;
      }
    }
    return "OTHER";
  }
}  // namespace MatScan

int MaterialScan(const std::string &outfile = "material_scan.csv", bool debugEta0 = false)
{
  Fun4AllServer *se = Fun4AllServer::instance();
  se->Verbosity(0);
  recoConsts *rc = recoConsts::instance();
  rc->set_IntFlag("RUNNUMBER", 1);
  rc->set_StringFlag("CDB_GLOBALTAG", CDB::global_tag);
  rc->set_uint64Flag("TIMESTAMP", CDB::timestamp);

  // Use vacuum for the world background so only real detector material
  // contributes (G4_World.C comment: "use G4_Galactic for material scan").
  G4WORLD::WorldMaterial = "G4_Galactic";

  // Exactly the 8 requested subsystems.
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

  // Build the Geant4 geometry (calls PHG4Reco::InitRun on all registered
  // subsystems) without running any events or needing an input generator.
  se->BeginRun(1);

  G4VPhysicalVolume *world = G4TransportationManager::GetTransportationManager()
                                 ->GetNavigatorForTracking()
                                 ->GetWorldVolume();
  if (!world)
  {
    std::cout << "MaterialScan: no world volume after BeginRun, aborting" << std::endl;
    gSystem->Exit(1);
  }

  G4Navigator *nav = new G4Navigator();
  nav->SetWorldVolume(world);

  const double etaMin = -1.1;
  const double etaMax = 1.1;
  const double etaStep = 0.02;
  const std::vector<double> phiList = {0.0, M_PI / 2.0};
  const double maxR = 500.0 * cm;     // generous cap, well past OHCal's outer radius
  const long maxSteps = 2000000;

  std::ofstream fout(outfile);
  fout << std::setprecision(15);
  fout << "phi,eta,subsystem,sumX0,sumLambdaI\n";

  // Diagnostic: total step length (cm) attributed to each distinct
  // unclassified ("OTHER") volume name, across the whole scan.
  std::map<std::string, double> otherLengthByName;

  const int nEta = static_cast<int>(std::round((etaMax - etaMin) / etaStep)) + 1;

  for (double phi : phiList)
  {
    for (int ie = 0; ie < nEta; ++ie)
    {
      double eta = etaMin + ie * etaStep;
      // Sample each bin slightly off its exact center for the ray direction.
      // sPHENIX's calorimeters are built as projective, tilted-tower north/
      // south halves meeting exactly at z=0 (eta=0): a ray fired at exactly
      // eta=0 runs precisely along the row boundary between the two halves'
      // tilted towers and can graze past all of them for the full radial
      // depth (confirmed by a per-step dump: at eta=0 the ray sat in the
      // vacuum-filled CEMC sector *mother* volume for the entire ~20 cm EMCal
      // depth without ever entering a tower solid, vs. ~19-22 X0 at every
      // neighboring eta). This is a measure-zero navigation degeneracy, not
      // real detector material, so each nominal grid eta is offset by half a
      // bin step before computing the ray direction (a 5% offset was tried
      // first and wasn't enough to clear the row boundary); the unshifted
      // eta is still what gets written out/plotted.
      double etaRay = eta + 0.5 * etaStep;
      double theta = 2.0 * std::atan(std::exp(-etaRay));
      G4ThreeVector dir(std::sin(theta) * std::cos(phi),
                         std::sin(theta) * std::sin(phi),
                         std::cos(theta));
      dir = dir.unit();
      // Nudge off z=0: sPHENIX's calorimeters/TPC are built as two z-symmetric
      // halves meeting exactly at z=0, and eta=0 gives dir_z=0 exactly, which
      // lands the ray precisely on that seam -- a Geant4 navigator degeneracy
      // (confirmed: eta=0 alone gave a bogus ~0.38 X0 through EMCal vs. the
      // smooth ~18-26 X0 at every neighboring eta). 1 micron is negligible
      // next to any real detector feature but breaks the exact-boundary case.
      G4ThreeVector pos(0.0, 0.0, 1.0e-4 * cm);

      std::map<std::string, double> sumX0;
      std::map<std::string, double> sumLambdaI;

      G4VPhysicalVolume *vol = nav->LocateGlobalPointAndSetup(pos, &dir, false);
      double traveled = 0.0;
      long nsteps = 0;

      while (vol && traveled < maxR && nsteps < maxSteps)
      {
        ++nsteps;
        G4Material *mat = vol->GetLogicalVolume()->GetMaterial();
        G4double safety = 0.0;
        G4double step = nav->ComputeStep(pos, dir, maxR - traveled, safety);
        if (step >= maxR)
        {
          step = maxR - traveled;  // clamp to remaining budget, exit loop after
        }
        if (step <= 0.0)
        {
          // Shouldn't normally happen; nudge forward a hair to avoid stalling
          // exactly on a boundary.
          step = 1.0e-6 * cm;
        }

        G4TouchableHistory *touch = nav->CreateTouchableHistory();
        std::string tag = MatScan::ClassifyTouchable(touch);
        delete touch;
        if (debugEta0 && eta == 0.0 && phi == 0.0 && traveled / cm > 85.0 && traveled / cm < 140.0)
        {
          std::cout << "  r=" << traveled / cm << " cm  step=" << step / cm
                    << " cm  vol=" << vol->GetName()
                    << " mat=" << mat->GetName()
                    << " tag=" << tag << std::endl;
        }
        double radlen = mat->GetRadlen();
        double nuclIntLen = mat->GetNuclearInterLength();
        if (radlen > 0)
        {
          sumX0[tag] += step / radlen;
        }
        if (nuclIntLen > 0)
        {
          sumLambdaI[tag] += step / nuclIntLen;
        }
        if (tag == "OTHER" && vol->GetName() != "World")
        {
          std::string leafName = vol->GetName();
          otherLengthByName[leafName] += step / cm;
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
        fout << phi << "," << eta << "," << tag << ","
             << sumX0[tag] << "," << sumLambdaI[tag] << "\n";
      }
    }
    std::cout << "MaterialScan: finished phi = " << phi << std::endl;
  }
  fout.close();

  std::cout << "=== MaterialScan: unclassified (OTHER) volumes, summed step length (cm) ===" << std::endl;
  if (otherLengthByName.empty())
  {
    std::cout << "  none -- every step was classified into one of the 8 subsystems" << std::endl;
  }
  else
  {
    for (auto &kv : otherLengthByName)
    {
      std::cout << "  " << kv.first << " : " << kv.second << " cm" << std::endl;
    }
  }

  std::cout << "MaterialScan: wrote " << outfile << std::endl;
  return 0;
}
#endif
