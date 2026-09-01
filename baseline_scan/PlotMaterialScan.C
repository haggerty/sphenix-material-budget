#ifndef MACRO_PLOTMATERIALSCAN_C
#define MACRO_PLOTMATERIALSCAN_C

#include <TCanvas.h>
#include <TH1F.h>
#include <TLegend.h>
#include <THStack.h>
#include <TStyle.h>
#include <TSystem.h>
#include <TAxis.h>

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>

int PlotMaterialScan(const std::string &infile = "material_scan.csv")
{
  gStyle->SetOptStat(0);

  // Physical radial order (innermost first): this fixes the stacking order.
  const std::vector<std::string> subsystems = {
      "PIPE", "MVTX", "INTT", "TPC", "EMCAL", "MAGNET", "IHCAL", "OHCAL"};

  const std::map<std::string, int> colors = {
      {"PIPE", kGray + 2},
      {"MVTX", kAzure + 1},
      {"INTT", kTeal + 1},
      {"TPC", kSpring - 1},
      {"EMCAL", kOrange + 1},
      {"MAGNET", kViolet + 1},
      {"IHCAL", kRed + 1},
      {"OHCAL", kMagenta + 2}};

  const double etaMin = -1.1;
  const double etaMax = 1.1;
  const double etaStep = 0.02;
  const int nBins = static_cast<int>(std::round((etaMax - etaMin) / etaStep)) + 1;
  const double loEdge = etaMin - etaStep / 2.0;
  const double hiEdge = etaMax + etaStep / 2.0;

  const std::vector<double> phiList = {0.0, M_PI / 2.0};
  const std::vector<std::string> phiLabel = {"#phi = 0", "#phi = #pi/2"};

  // hists[phi_index]["X0"|"Lambda"][subsystem] -> TH1F*
  std::map<int, std::map<std::string, std::map<std::string, TH1F *>>> hists;

  auto phiIndex = [&](double phi) -> int
  {
    for (size_t i = 0; i < phiList.size(); ++i)
    {
      if (std::fabs(phi - phiList[i]) < 1e-3) return static_cast<int>(i);
    }
    return -1;
  };

  for (size_t ip = 0; ip < phiList.size(); ++ip)
  {
    for (const auto &sub : subsystems)
    {
      TH1F *hX0 = new TH1F(Form("h_X0_%s_phi%zu", sub.c_str(), ip),
                            "", nBins, loEdge, hiEdge);
      TH1F *hLam = new TH1F(Form("h_Lambda_%s_phi%zu", sub.c_str(), ip),
                             "", nBins, loEdge, hiEdge);
      hX0->SetFillColor(colors.at(sub));
      hX0->SetLineColor(kBlack);
      hLam->SetFillColor(colors.at(sub));
      hLam->SetLineColor(kBlack);
      hists[ip]["X0"][sub] = hX0;
      hists[ip]["Lambda"][sub] = hLam;
    }
  }

  std::ifstream fin(infile);
  if (!fin.good())
  {
    std::cout << "PlotMaterialScan: cannot open " << infile << std::endl;
    return 1;
  }
  std::string line;
  std::getline(fin, line);  // header
  long nrows = 0;
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
    if (hists[ip]["X0"].count(sub))
    {
      hists[ip]["X0"][sub]->Fill(eta, x0);
      hists[ip]["Lambda"][sub]->Fill(eta, lam);
    }
    // "OTHER" rows (if any) are intentionally not plotted -- see MaterialScan.C
    // diagnostic printout for their content.
    ++nrows;
  }
  std::cout << "PlotMaterialScan: read " << nrows << " data rows from " << infile << std::endl;

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
      THStack *stack = new THStack(Form("stack_%s_phi%zu", quantity.c_str(), ip),
                                    (phiLabel[ip] + ";#eta;" + yTitle).c_str());
      TLegend *leg = new TLegend(0.14, 0.62, 0.42, 0.89);
      leg->SetBorderSize(0);
      leg->SetFillStyle(0);
      leg->SetTextSize(0.03);
      for (const auto &sub : subsystems)
      {
        TH1F *h = hists[ip][quantity][sub];
        stack->Add(h);
        leg->AddEntry(h, sub.c_str(), "f");
      }
      stack->Draw("hist");
      stack->GetXaxis()->SetTitle("#eta");
      stack->GetYaxis()->SetTitle(yTitle.c_str());
      stack->Draw("hist");
      leg->Draw();
    }
    c->SaveAs((outbase + ".pdf").c_str());
    c->SaveAs((outbase + ".png").c_str());
  };

  buildCanvas("X0", "Cumulative X_{0}", "X0_vs_eta");
  buildCanvas("Lambda", "Cumulative #lambda_{I}", "lambdaI_vs_eta");

  std::cout << "PlotMaterialScan: wrote X0_vs_eta.{pdf,png} and lambdaI_vs_eta.{pdf,png}" << std::endl;
  return 0;
}
#endif
