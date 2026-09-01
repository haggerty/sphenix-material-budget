#ifndef MACRO_PLOTSUBSYSTEMBUDGET_C
#define MACRO_PLOTSUBSYSTEMBUDGET_C

// Reads the existing material_scan.csv (no rescan needed) and draws EMCAL,
// IHCAL, MAGNET, OHCAL as individually-readable overlaid lines (not stacked)
// for both X0 and lambdaI vs eta -- the stacked plots in PlotMaterialScan.C
// make it easy to see the total budget but hard to read off any one
// subsystem's own value, since each system sits on top of the others.

#include <TCanvas.h>
#include <TGraph.h>
#include <TLegend.h>
#include <TStyle.h>
#include <TMultiGraph.h>
#include <TAxis.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>

int PlotSubsystemBudget(const std::string &infile = "material_scan.csv")
{
  gStyle->SetOptStat(0);

  const std::vector<std::string> subsystems = {"EMCAL", "MAGNET", "IHCAL", "OHCAL"};
  const std::map<std::string, int> colors = {
      {"EMCAL", kOrange + 1},
      {"MAGNET", kViolet + 1},
      {"IHCAL", kRed + 1},
      {"OHCAL", kMagenta + 2}};

  const std::vector<double> phiList = {0.0, M_PI / 2.0};
  const std::vector<std::string> phiLabel = {"#phi = 0", "#phi = #pi/2"};

  // points[phi_index][subsystem]["X0"|"Lambda"] -> (eta, value) pairs
  std::map<int, std::map<std::string, std::map<std::string, std::vector<std::pair<double, double>>>>> points;

  auto phiIndex = [&](double phi) -> int
  {
    for (size_t i = 0; i < phiList.size(); ++i)
    {
      if (std::fabs(phi - phiList[i]) < 1e-3) return static_cast<int>(i);
    }
    return -1;
  };

  std::ifstream fin(infile);
  if (!fin.good())
  {
    std::cout << "PlotSubsystemBudget: cannot open " << infile << std::endl;
    return 1;
  }
  std::string line;
  std::getline(fin, line);  // header
  while (std::getline(fin, line))
  {
    if (line.empty()) continue;
    std::stringstream ss(line);
    std::string phiStr, etaStr, sub, x0Str, lamStr;
    std::getline(ss, phiStr, ',');
    std::getline(ss, etaStr, ',');
    std::getline(ss, sub, ',');
    std::getline(ss, x0Str, ',');
    std::getline(ss, lamStr, ',');
    double phi = std::stod(phiStr);
    double eta = std::stod(etaStr);
    double x0 = std::stod(x0Str);
    double lam = std::stod(lamStr);
    int ip = phiIndex(phi);
    if (ip < 0) continue;
    if (colors.count(sub) == 0) continue;  // only the 4 requested subsystems
    points[ip][sub]["X0"].emplace_back(eta, x0);
    points[ip][sub]["Lambda"].emplace_back(eta, lam);
  }

  for (auto &phiKv : points)
  {
    for (auto &subKv : phiKv.second)
    {
      for (auto &qKv : subKv.second)
      {
        std::sort(qKv.second.begin(), qKv.second.end());
      }
    }
  }

  auto buildCanvas = [&](const std::string &quantity, const std::string &yTitle,
                          const std::string &outbase)
  {
    TCanvas *c = new TCanvas(("c_" + quantity).c_str(), quantity.c_str(), 1400, 650);
    c->Divide(2, 1);
    for (size_t ip = 0; ip < phiList.size(); ++ip)
    {
      c->cd(static_cast<int>(ip) + 1);
      gPad->SetLeftMargin(0.12);
      gPad->SetRightMargin(0.04);
      TMultiGraph *mg = new TMultiGraph(Form("mg_%s_phi%zu", quantity.c_str(), ip),
                                         (phiLabel[ip] + ";#eta;" + yTitle).c_str());
      TLegend *leg = new TLegend(0.14, 0.68, 0.42, 0.89);
      leg->SetBorderSize(0);
      leg->SetFillStyle(0);
      leg->SetTextSize(0.035);
      for (const auto &sub : subsystems)
      {
        const auto &pts = points[static_cast<int>(ip)][sub][quantity];
        TGraph *g = new TGraph(static_cast<int>(pts.size()));
        for (size_t i = 0; i < pts.size(); ++i)
        {
          g->SetPoint(static_cast<int>(i), pts[i].first, pts[i].second);
        }
        g->SetLineColor(colors.at(sub));
        g->SetLineWidth(2);
        g->SetMarkerColor(colors.at(sub));
        g->SetMarkerStyle(20);
        g->SetMarkerSize(0.5);
        mg->Add(g, "lp");
        leg->AddEntry(g, sub.c_str(), "lp");
      }
      mg->Draw("A");
      mg->GetXaxis()->SetTitle("#eta");
      mg->GetYaxis()->SetTitle(yTitle.c_str());
      leg->Draw();
    }
    c->SaveAs((outbase + ".pdf").c_str());
    c->SaveAs((outbase + ".png").c_str());
  };

  buildCanvas("X0", "X_{0} (per subsystem)", "subsystem_X0_vs_eta");
  buildCanvas("Lambda", "#lambda_{I} (per subsystem)", "subsystem_lambdaI_vs_eta");

  std::cout << "PlotSubsystemBudget: wrote subsystem_X0_vs_eta.{pdf,png} and subsystem_lambdaI_vs_eta.{pdf,png}" << std::endl;
  return 0;
}
#endif
