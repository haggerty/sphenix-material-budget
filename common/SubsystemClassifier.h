#ifndef MATBUDGET_COMMON_SUBSYSTEMCLASSIFIER_H
#define MATBUDGET_COMMON_SUBSYSTEMCLASSIFIER_H

// Shared subsystem classification logic used by both baseline_scan/ and
// smearing_scan/ ray-tracing macros -- see either macro for how it's used.

#include <Geant4/G4VPhysicalVolume.hh>
#include <Geant4/G4VTouchable.hh>

#include <string>

namespace MatScan
{
  // Classify a single volume name into one of the 8 requested subsystems.
  // Order matters: more specific / higher-priority checks first.
  inline std::string ClassifyName(const std::string &name)
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
  inline std::string ClassifyTouchable(const G4VTouchable *touch)
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

#endif
