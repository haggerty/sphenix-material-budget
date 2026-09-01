#ifndef MACRO_VERTEXPHISMEARSCAN_C
#define MACRO_VERTEXPHISMEARSCAN_C

// Monte Carlo companion to baseline_scan/MaterialScan.C. Instead of one
// idealized ray per (eta, phi), this fires many rays per eta point with:
//   - z_vertex ~ Gaussian(0, zSigmaCm) -- the real sPHENIX luminous region,
//     rather than a fixed vertex at z=0.
//   - phi ~ Uniform over one HCal sector width (2*pi/64) centered on each of
//     two representative regions: a normal sector (phi=0) and the chimney/
//     service-penetration sector (phi=pi/2), matching the two phi values the
//     baseline scan used as single points.
// Pseudorapidity eta is defined relative to each ray's own (smeared) vertex,
// exactly as a real track's eta would be -- so this reflects the true
// as-installed material budget seen by tracks from the real luminous region,
// not just the idealized eta=0-vertex case.
//
// Only aggregate mean/RMS per (region, eta, subsystem) are written out (not
// every individual ray), since that's what PlotVertexPhiSmear.C plots as a
// mean line with an RMS band per subsystem.
//
// Unlike the baseline scan (which always starts at z~0), this scan's
// randomized starting z can land the very first navigator locate call inside
// a Geant4 parameterised volume region whose voxel acceleration structure
// was never built for that region, dereferencing a null voxel-header pointer
// (G4ParameterisedNavigation::ParamVoxelLocate, pHead=0x0) -- confirmed via
// gdb backtrace, and it recurs for a meaningful fraction (order 15%) of rays
// across the eta range, not just one isolated case. This is a real bug/
// limitation in Geant4's navigation of the sPHENIX geometry, not something
// fixable from this macro. It's a raw SIGSEGV so try/catch can't catch it;
// an earlier fork-per-ray sandboxing attempt fought ROOT/TSystem's own
// SIGCHLD handling (a known ROOT gotcha) and gave unreliable results. Instead
// each ray installs a SIGSEGV handler and wraps the navigation in
// sigsetjmp/siglongjmp: since the fault is a null-pointer *read* (confirmed
// by the backtrace and dmesg's "error 4"), no memory gets corrupted before
// the jump, so recovering is safe as long as the navigator object that was
// mid-call is discarded afterward -- which happens naturally since a fresh
// G4Navigator is created for every ray anyway.

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

#include <TRandom3.h>
#include <TSystem.h>

#include <csetjmp>
#include <csignal>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

R__LOAD_LIBRARY(libg4detectors.so)
R__LOAD_LIBRARY(libfun4all.so)

namespace VertexPhiSmearDetail
{
  sigjmp_buf g_rayJmpBuf;

  void RaySignalHandler(int /*sig*/)
  {
    siglongjmp(g_rayJmpBuf, 1);
  }
}  // namespace VertexPhiSmearDetail

int VertexPhiSmearScan(const std::string &outfile = "vertex_phi_smear.csv",
                        int nSamples = 200,
                        double zSigmaCm = 10.0,
                        UInt_t seed = 12345)
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

  // Exactly the 8 subsystems from the baseline scan.
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
    std::cout << "VertexPhiSmearScan: no world volume after BeginRun, aborting" << std::endl;
    gSystem->Exit(1);
  }

  struct sigaction sa;
  sa.sa_handler = VertexPhiSmearDetail::RaySignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGSEGV, &sa, nullptr);

  const double etaMin = -1.1;
  const double etaMax = 1.1;
  const double etaStep = 0.02;
  const int nEta = static_cast<int>(std::round((etaMax - etaMin) / etaStep)) + 1;

  // One HCal sector, in radians: 2*pi/64.
  const double sectorWidth = 2.0 * M_PI / 64.0;
  const std::vector<double> phiCenters = {0.0, M_PI / 2.0};
  const std::vector<std::string> regionLabel = {"SECTOR", "CHIMNEY"};

  const std::vector<std::string> subsystems = {
      "PIPE", "MVTX", "INTT", "TPC", "EMCAL", "MAGNET", "IHCAL", "OHCAL", "OTHER"};

  const double maxR = 500.0 * cm;  // generous cap, well past OHCal's outer radius
  const long maxSteps = 2000000;

  TRandom3 rng(seed);

  std::ofstream fout(outfile);
  fout << std::setprecision(15);
  fout << "region,eta,subsystem,nsamples,meanX0,rmsX0,meanLambdaI,rmsLambdaI\n";

  long totalDropped = 0;

  for (size_t ir = 0; ir < phiCenters.size(); ++ir)
  {
    double phiCenter = phiCenters[ir];
    for (int ie = 0; ie < nEta; ++ie)
    {
      double eta = etaMin + ie * etaStep;
      double theta = 2.0 * std::atan(std::exp(-eta));

      // Running sum / sum-of-squares per subsystem across the successfully
      // completed rays at this (region, eta), to get mean and RMS without
      // storing every individual ray's result. nGood may end up less than
      // nSamples if any rays were dropped to a navigator crash (see above).
      std::map<std::string, double> sumX0, sumX0Sq;
      std::map<std::string, double> sumLam, sumLamSq;
      for (const auto &sub : subsystems)
      {
        sumX0[sub] = sumX0Sq[sub] = sumLam[sub] = sumLamSq[sub] = 0.0;
      }
      int nGood = 0;

      for (int is = 0; is < nSamples; ++is)
      {
        double zVtx = rng.Gaus(0.0, zSigmaCm);
        double phi = phiCenter + rng.Uniform(-0.5 * sectorWidth, 0.5 * sectorWidth);

        if (sigsetjmp(VertexPhiSmearDetail::g_rayJmpBuf, 1) != 0)
        {
          // A SIGSEGV during this ray jumped back here. The G4Navigator
          // created below is abandoned (leaked, harmlessly -- see file
          // header) rather than reused, since its internal state may be
          // partially mutated by the aborted call.
          std::cout << "VertexPhiSmearScan: dropped ray region=" << regionLabel[ir]
                     << " eta=" << eta << " sample=" << is << " zVtx=" << zVtx
                     << " phi=" << phi << " (SIGSEGV in navigator)" << std::endl;
          ++totalDropped;
          continue;
        }

        G4ThreeVector dir(std::sin(theta) * std::cos(phi),
                           std::sin(theta) * std::sin(phi),
                           std::cos(theta));
        dir = dir.unit();
        // Tiny epsilon nudge off the exact vertex z, same rationale as the
        // baseline scan's eta=0 fix: guards against the ray landing exactly
        // on the z=0 tower-symmetry seam in the vanishingly unlikely case
        // the Gaussian draws zVtx extremely close to 0.
        G4ThreeVector pos(0.0, 0.0, zVtx * cm + 1.0e-4 * cm);

        std::map<std::string, double> rayX0, rayLam;
        for (const auto &sub : subsystems)
        {
          rayX0[sub] = rayLam[sub] = 0.0;
        }

        G4Navigator *nav = new G4Navigator();
        nav->SetWorldVolume(world);
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
            step = maxR - traveled;
          }
          if (step <= 0.0)
          {
            step = 1.0e-6 * cm;
          }

          G4TouchableHistory *touch = nav->CreateTouchableHistory();
          std::string tag = MatScan::ClassifyTouchable(touch);
          delete touch;

          double radlen = mat->GetRadlen();
          double nuclIntLen = mat->GetNuclearInterLength();
          if (radlen > 0)
          {
            rayX0[tag] += step / radlen;
          }
          if (nuclIntLen > 0)
          {
            rayLam[tag] += step / nuclIntLen;
          }

          pos += step * dir;
          traveled += step;
          nav->SetGeometricallyLimitedStep();
          vol = nav->LocateGlobalPointAndSetup(pos, &dir, true);
        }
        delete nav;

        for (const auto &sub : subsystems)
        {
          sumX0[sub] += rayX0[sub];
          sumX0Sq[sub] += rayX0[sub] * rayX0[sub];
          sumLam[sub] += rayLam[sub];
          sumLamSq[sub] += rayLam[sub] * rayLam[sub];
        }
        ++nGood;
      }

      if (nGood == 0)
      {
        std::cout << "VertexPhiSmearScan: WARNING all rays dropped for region="
                   << regionLabel[ir] << " eta=" << eta << ", skipping row" << std::endl;
        continue;
      }

      for (const auto &sub : subsystems)
      {
        double meanX0 = sumX0[sub] / nGood;
        double meanLam = sumLam[sub] / nGood;
        double varX0 = sumX0Sq[sub] / nGood - meanX0 * meanX0;
        double varLam = sumLamSq[sub] / nGood - meanLam * meanLam;
        double rmsX0 = varX0 > 0 ? std::sqrt(varX0) : 0.0;
        double rmsLam = varLam > 0 ? std::sqrt(varLam) : 0.0;
        fout << regionLabel[ir] << "," << eta << "," << sub << "," << nGood << ","
             << meanX0 << "," << rmsX0 << "," << meanLam << "," << rmsLam << "\n";
      }
    }
    std::cout << "VertexPhiSmearScan: finished region = " << regionLabel[ir] << std::endl;
  }
  fout.close();

  std::cout << "VertexPhiSmearScan: wrote " << outfile << " (" << totalDropped
             << " rays dropped to navigator crashes)" << std::endl;
  return 0;
}
#endif
