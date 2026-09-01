#ifndef MACRO_PLOTVERTEXPHISMEAR_C
#define MACRO_PLOTVERTEXPHISMEAR_C

// Reads vertex_phi_smear.csv (from VertexPhiSmearScan.C) and draws EMCAL,
// MAGNET, IHCAL, OHCAL as a mean line with a shaded +/-1 RMS band per
// subsystem, for both the SECTOR and CHIMNEY regions -- the smeared-vertex
// analogue of baseline_scan/PlotSubsystemBudget.C's unstacked overlay.

#include <TCanvas.h>
#include <TGraph.h>
#include <TGraphErrors.h>
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

int PlotVertexPhiSmear(const std::string &infile = "vertex_phi_smear.csv")
{
  gStyle->SetOptStat(0);

  const std::vector<std::string> subsystems = {"EMCAL", "MAGNET", "IHCAL", "OHCAL"};
  const std::map<std::string, int> colors = {
      {"EMCAL", kOrange + 1},
      {"MAGNET", kViolet + 1},
      {"IHCAL", kRed + 1},
      {"OHCAL", kMagenta + 2}};

  const std::vector<std::string> regions = {"SECTOR", "CHIMNEY"};
  const std::vector<std::string> regionLabel = {
      "SECTOR (#phi #approx 0)", "CHIMNEY (#phi #approx #pi/2)"};

  struct Point
  {
    double eta, mean, rms;
  };
  // points[region][subsystem]["X0"|"Lambda"] -> points, sorted by eta
  std::map<std::string, std::map<std::string, std::map<std::string, std::vector<Point>>>> points;

  std::ifstream fin(infile);
  if (!fin.good())
  {
    std::cout << "PlotVertexPhiSmear: cannot open " << infile << std::endl;
    return 1;
  }
  std::string line;
  std::getline(fin, line);  // header
  while (std::getline(fin, line))
  {
    if (line.empty()) continue;
    std::stringstream ss(line);
    std::string region, etaStr, sub, nStr, meanX0Str, rmsX0Str, meanLamStr, rmsLamStr;
    std::getline(ss, region, ',');
    std::getline(ss, etaStr, ',');
    std::getline(ss, sub, ',');
    std::getline(ss, nStr, ',');
    std::getline(ss, meanX0Str, ',');
    std::getline(ss, rmsX0Str, ',');
    std::getline(ss, meanLamStr, ',');
    std::getline(ss, rmsLamStr, ',');
    if (colors.count(sub) == 0) continue;  // only the 4 requested subsystems

    double eta = std::stod(etaStr);
    points[region][sub]["X0"].push_back({eta, std::stod(meanX0Str), std::stod(rmsX0Str)});
    points[region][sub]["Lambda"].push_back({eta, std::stod(meanLamStr), std::stod(rmsLamStr)});
  }

  for (auto &regionKv : points)
  {
    for (auto &subKv : regionKv.second)
    {
      for (auto &qKv : subKv.second)
      {
        std::sort(qKv.second.begin(), qKv.second.end(),
                  [](const Point &a, const Point &b) { return a.eta < b.eta; });
      }
    }
  }

  auto buildCanvas = [&](const std::string &quantity, const std::string &yTitle,
                          const std::string &outbase)
  {
    TCanvas *c = new TCanvas(("c_" + quantity).c_str(), quantity.c_str(), 1400, 650);
    c->Divide(2, 1);
    for (size_t ir = 0; ir < regions.size(); ++ir)
    {
      c->cd(static_cast<int>(ir) + 1);
      gPad->SetLeftMargin(0.12);
      gPad->SetRightMargin(0.04);
      TMultiGraph *mg = new TMultiGraph(Form("mg_%s_%s", quantity.c_str(), regions[ir].c_str()),
                                         (regionLabel[ir] + ";#eta;" + yTitle).c_str());
      TLegend *leg = new TLegend(0.14, 0.68, 0.42, 0.89);
      leg->SetBorderSize(0);
      leg->SetFillStyle(0);
      leg->SetTextSize(0.035);
      for (const auto &sub : subsystems)
      {
        const auto &pts = points[regions[ir]][sub][quantity];
        int n = static_cast<int>(pts.size());

        // Shaded +/-1 RMS band.
        TGraphErrors *band = new TGraphErrors(n);
        for (int i = 0; i < n; ++i)
        {
          band->SetPoint(i, pts[i].eta, pts[i].mean);
          band->SetPointError(i, 0.0, pts[i].rms);
        }
        band->SetFillColorAlpha(colors.at(sub), 0.30);
        band->SetLineColorAlpha(colors.at(sub), 0.0);
        mg->Add(band, "3");

        // Mean line on top of the band.
        TGraph *meanLine = new TGraph(n);
        for (int i = 0; i < n; ++i)
        {
          meanLine->SetPoint(i, pts[i].eta, pts[i].mean);
        }
        meanLine->SetLineColor(colors.at(sub));
        meanLine->SetLineWidth(2);
        mg->Add(meanLine, "L");
        leg->AddEntry(meanLine, sub.c_str(), "l");
      }
      mg->Draw("A");
      mg->GetXaxis()->SetTitle("#eta");
      mg->GetYaxis()->SetTitle(yTitle.c_str());
      leg->Draw();
    }
    c->SaveAs((outbase + ".pdf").c_str());
    c->SaveAs((outbase + ".png").c_str());
  };

  buildCanvas("X0", "X_{0} (mean #pm RMS, per subsystem)", "smear_X0_vs_eta");
  buildCanvas("Lambda", "#lambda_{I} (mean #pm RMS, per subsystem)", "smear_lambdaI_vs_eta");

  std::cout << "PlotVertexPhiSmear: wrote smear_X0_vs_eta.{pdf,png} and smear_lambdaI_vs_eta.{pdf,png}" << std::endl;
  return 0;
}
#endif
