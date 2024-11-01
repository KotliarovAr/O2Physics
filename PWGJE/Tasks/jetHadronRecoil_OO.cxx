// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \brief: hadron-jet correlation analysis for OO collisions
/// \author: Kotliarov Artem <artem.kotliarov@cern.ch>

#include <vector>
#include "TRandom3.h"
#include "TVector2.h"

#include "Framework/ASoA.h"
#include "Framework/AnalysisDataModel.h"
#include "Framework/AnalysisTask.h"
#include "Framework/O2DatabasePDGPlugin.h"
#include "Framework/HistogramRegistry.h"
#include "Framework/runDataProcessing.h"

#include "CommonConstants/MathConstants.h"
#include "Common/Core/TrackSelection.h"
#include "Common/Core/TrackSelectionDefaults.h"
#include "Common/Core/RecoDecay.h"
#include "Common/DataModel/EventSelection.h"
#include "Common/DataModel/TrackSelectionTables.h"

#include "PWGJE/Core/FastJetUtilities.h"
#include "PWGJE/Core/JetFinder.h"
#include "PWGJE/Core/JetFindingUtilities.h"
#include "PWGJE/DataModel/Jet.h"

#include "PWGJE/Core/JetDerivedDataUtilities.h"

#include "EventFiltering/filterTables.h"

using namespace o2;
using namespace o2::framework;
using namespace o2::framework::expressions;

// Shorthand notations
using filtered_Coll = soa::Filtered<soa::Join<JetCollisions, aod::BkgChargedRhos, aod::EvSels>>::iterator; // Why we need aod::EvSels? Event selection is done with "JetCollisions" table
using filtered_Coll_PartLevel = soa::Filtered<JetMcCollisions>::iterator;
using filtered_Coll_DetLevel_to_GetWeight = soa::Filtered<soa::Join<JetCollisionsMCD, aod::BkgChargedRhos, aod::EvSels>>::iterator; // We need weight which is obtained through the Coll_PartLevel --> JetCollisionsMCD = o2::soa::Join<JetCollisions, o2::aod::JMcCollisionLbs>;

using filtered_Jets = soa::Filtered<soa::Join<aod::ChargedJets, aod::ChargedJetConstituents>>;
using filtered_Jets_DetLevel = soa::Filtered<soa::Join<aod::ChargedMCDetectorLevelJets, aod::ChargedMCDetectorLevelJetConstituents>>;
using filtered_Jets_PartLevel = soa::Filtered<soa::Join<aod::ChargedMCParticleLevelJets, aod::ChargedMCParticleLevelJetConstituents>>;

using filtered_MatchedJets_DetLevel = soa::Filtered<soa::Join<aod::ChargedMCDetectorLevelJets, aod::ChargedMCDetectorLevelJetConstituents, aod::ChargedMCDetectorLevelJetsMatchedToChargedMCParticleLevelJets>>;
using filtered_MatchedJets_PartLevel = soa::Filtered<soa::Join<aod::ChargedMCParticleLevelJets, aod::ChargedMCParticleLevelJetConstituents, aod::ChargedMCParticleLevelJetsMatchedToChargedMCDetectorLevelJets>>;

using filtered_Tracks = soa::Filtered<JetTracks>;

struct jetHadronRecoil_OO {

  // List of configurable parameters
  Configurable<std::string> evSel{"evSel", "sel8", "Choose event selection"};
  Configurable<std::string> trkSel{"trkSel", "globalTracks", "Set track selection"};
  Configurable<float> vertexZCut{"vertexZCut", 10., "Accepted z-vertex range"};
  Configurable<float> frac_sig{"frac_sig", 0.5, "Fraction of events to use for Signal TT"};

  Configurable<float> trkPtMin{"trkPtMin", 0.15, "Minimum pT of acceptanced tracks"};
  Configurable<float> trkPtMax{"trkPtMax", 100., "Maximum pT of acceptanced tracks"};

  Configurable<float> trkPhiMin{"trkPhiMin", -7., "Minimum phi angle of acceptanced tracks"};
  Configurable<float> trkPhiMax{"trkPhiMax", 7., "Maximum phi angle of acceptanced tracks"};

  Configurable<float> trkEtaCut{"trkEtaCut", 0.9, "Eta acceptance of TPC"};
  Configurable<float> jetR{"jetR", 0.4, "Jet cone radius"};

  // List of configurable parameters for MC
  Configurable<float> pTHatExponent{"pTHatExponent", 6.0, "exponent of the event weight for the calculation of pTHat"};
  Configurable<float> pTHatMaxMCD{"pTHatMaxMCD", 999.0, "maximum fraction of hard scattering for jet acceptance in detector MC"};
  Configurable<float> pTHatMaxMCP{"pTHatMaxMCP", 999.0, "maximum fraction of hard scattering for jet acceptance in particle MC"};

  // Parameters for recoil jet selection
  Configurable<uint8_t> pT_TTref_min{"pT_TTref_min", 5, "Minimum pT of reference TT"};
  Configurable<uint8_t> pT_TTref_max{"pT_TTref_max", 7, "Maximum pT of reference TT"};
  Configurable<uint8_t> pT_TTsig_min{"pT_TTsig_min", 20, "Minimum pT of signal TT"};
  Configurable<uint8_t> pT_TTsig_max{"pT_TTsig_max", 50, "Maximum pT of signal TT"};
  Configurable<float> recoilRegion{"recoilRegion", 0.6, "Width of recoil region"};

  // List of configurable parameters for histograms
  Configurable<uint16_t> hist_jetPt{"hist_jetPt", 100, "Maximum value of jet pT shown in histograms"};

  // Axes specification
  AxisSpec pT{hist_jetPt, 0.0, hist_jetPt, "#it{p}_{T} (GeV/#it{c})"};
  AxisSpec jet_pT_corr{hist_jetPt + 20, -20., hist_jetPt, "#it{p}_{T, jet}^{ch, corr} (GeV/#it{c})"};
  AxisSpec phi_angle{40, 0.0, TMath::TwoPi(), "#varphi (rad)"};
  AxisSpec deltaPhi_angle{52, 0.0, TMath::Pi(), "#Delta#varphi (rad)"};
  AxisSpec pseudorap{40, -1., 1., "#eta"};
  AxisSpec rhoArea{60, 0.0, 30., "#rho #times #A_{jet}"};

  Preslice<filtered_MatchedJets_PartLevel> PartJetsPerCollision = aod::jet::mcCollisionId;

  TRandom3* rand = new TRandom3(0);

  // Declare filter on collision Z vertex

  Filter collisionFilter = nabs(aod::jcollision::posZ) < vertexZCut;
  Filter collisionFilterMC = nabs(aod::jmccollision::posZ) < vertexZCut;

  // Declare filters on accepted tracks
  Filter trackFilter = (aod::jtrack::pt > trkPtMin && aod::jtrack::pt < trkPtMax && nabs(aod::jtrack::eta) < trkEtaCut);

  // Declare filter on jets
  Filter jetRadiusFilter = aod::jet::r == nround(jetR.node() * 100.);

  HistogramRegistry spectra;

  int eventSelection = -1;
  int trackSelection = -1;

  void init(InitContext const&)
  {

    eventSelection = jetderiveddatautilities::initialiseEventSelection(static_cast<std::string>(evSel));
    trackSelection = jetderiveddatautilities::initialiseTrackSelection(static_cast<std::string>(trkSel));

    // List of raw distributions
    spectra.add("vertexZ", "z vertex of collisions", kTH1F, {{60, -12., 12.}});

    spectra.add("hTrackPtEtaPhi", "Charact. of tracks", kTH3F, {pT, pseudorap, phi_angle});
    spectra.add("hNtrig", "Total number of selected triggers per class", kTH1F, {{2, 0.0, 2.}}); // Can we set name for bins?
    spectra.add("hTTRef_per_event", "Number of TT_{Ref} per event", kTH1F, {{10, 0.0, 10.}});
    spectra.add("hTTSig_per_event", "Number of TT_{Sig} per event", kTH1F, {{5, 0.0, 5.}});

    spectra.add("hJetPtEtaPhiRhoArea", "Charact. of inclusive jets", kTHnSparseF, {pT, pseudorap, phi_angle, rhoArea});

    spectra.add("hDPhi_JetPt_Corr_TTRef", "Events w. TT_{Ref}: #Delta#varphi & #it{p}_{T, jet}^{ch}", kTH2F, {deltaPhi_angle, jet_pT_corr});
    spectra.add("hDPhi_JetPt_Corr_TTSig", "Events w. TT_{Sig}: #Delta#varphi & #it{p}_{T, jet}^{ch}", kTH2F, {deltaPhi_angle, jet_pT_corr});
    spectra.add("hDPhi_JetPt_TTRef", "Events w. TT_{Ref}: #Delta#varphi & #it{p}_{T, jet}^{ch}", kTH2F, {deltaPhi_angle, pT});
    spectra.add("hDPhi_JetPt_TTSig", "Events w. TT_{Sig}: #Delta#varphi & #it{p}_{T, jet}^{ch}", kTH2F, {deltaPhi_angle, pT});

    spectra.add("hRecoil_JetPt_Corr_TTRef", "Events w. TT_{Ref}: #it{p}_{T} of recoil jets", kTH1F, {jet_pT_corr});
    spectra.add("hRecoil_JetPt_Corr_TTSig", "Events w. TT_{Sig}: #it{p}_{T} of recoil jets", kTH1F, {jet_pT_corr});
    spectra.add("hRecoil_JetPt_TTRef", "Events w. TT_{Ref}: #it{p}_{T} of recoil jets", kTH1F, {pT});
    spectra.add("hRecoil_JetPt_TTSig", "Events w. TT_{Sig}: #it{p}_{T} of recoil jets", kTH1F, {pT});

    spectra.add("hDPhi_JetPt_RhoArea_TTRef", "Events w. TT_{Ref}: #Delta#varphi & jet pT & #rho #times A_{jet}", kTH3F, {deltaPhi_angle, pT, rhoArea});
    spectra.add("hDPhi_JetPt_RhoArea_TTSig", "Events w. TT_{Sig}: #Delta#varphi & jet pT & #rho #times A_{jet}", kTH3F, {deltaPhi_angle, pT, rhoArea});

    // List of MC particle level distributions
    spectra.add("hPartPtEtaPhi", "Charact. of particles", kTH3F, {pT, pseudorap, phi_angle});
    spectra.add("hNtrig_Part", "Total number of selected triggers per class", kTH1F, {{2, 0.0, 2.}});
    spectra.add("hTTRef_per_event_Part", "Number of TT_{Ref} per event", kTH1F, {{10, 0.0, 10.}});
    spectra.add("hTTSig_per_event_Part", "Number of TT_{Sig} per event", kTH1F, {{5, 0.0, 5.}});

    spectra.add("hJetPtEtaPhiRhoArea_Part", "Charact. of inclusive part. level jets", kTHnSparseF, {pT, pseudorap, phi_angle, rhoArea});

    spectra.add("hDPhi_JetPt_Corr_TTRef_Part", "Events w. TT_{Ref}: #Delta#varphi & #it{p}_{T, jet}^{ch}", kTH2F, {deltaPhi_angle, jet_pT_corr});
    spectra.add("hDPhi_JetPt_Corr_TTSig_Part", "Events w. TT_{Sig}: #Delta#varphi & #it{p}_{T, jet}^{ch}", kTH2F, {deltaPhi_angle, jet_pT_corr});
    spectra.add("hDPhi_JetPt_TTRef_Part", "Events w. TT_{Ref}: #Delta#varphi & #it{p}_{T, jet}^{ch}", kTH2F, {deltaPhi_angle, pT});
    spectra.add("hDPhi_JetPt_TTSig_Part", "Events w. TT_{Sig}: #Delta#varphi & #it{p}_{T, jet}^{ch}", kTH2F, {deltaPhi_angle, pT});

    spectra.add("hRecoil_JetPt_Corr_TTRef_Part", "Events w. TT_{Ref}: #it{p}_{T} of recoil jets", kTH1F, {jet_pT_corr});
    spectra.add("hRecoil_JetPt_Corr_TTSig_Part", "Events w. TT_{Sig}: #it{p}_{T} of recoil jets", kTH1F, {jet_pT_corr});
    spectra.add("hRecoil_JetPt_TTRef_Part", "Events w. TT_{Ref}: #it{p}_{T} of recoil jets", kTH1F, {pT});
    spectra.add("hRecoil_JetPt_TTSig_Part", "Events w. TT_{Sig}: #it{p}_{T} of recoil jets", kTH1F, {pT});

    spectra.add("hDPhi_JetPt_RhoArea_TTRef_Part", "Events w. TT_{Ref}: #Delta#varphi & jet pT & #rho #times A_{jet}", kTH3F, {deltaPhi_angle, pT, rhoArea});
    spectra.add("hDPhi_JetPt_RhoArea_TTSig_Part", "Events w. TT_{Sig}: #Delta#varphi & jet pT & #rho #times A_{jet}", kTH3F, {deltaPhi_angle, pT, rhoArea});

    // Response matrices, jet pT & jet phi resolution
    spectra.add("hJetPt_PartLevel_vs_DetLevel", "Correlation jet pT at part. vs. det. levels", kTH2F, {pT, pT});
    spectra.add("hJetPt_PartLevel_vs_DetLevel_RecoilJets", "Correlation recoil jet pT at part. vs. det. levels", kTH2F, {pT, pT});

    spectra.add("hMissedJets_pT", "Part. level jets w/o matched pair", kTH1F, {pT});
    spectra.add("hFakeJets_pT", "Det. level jets w/o matched pair", kTH1F, {pT});

    spectra.add("hJetPt_resolution", "Jet p_{T} relative resolution as a func. of jet p_{T, part}", kTH2F, {{90, -1., 2.}, pT});
    spectra.add("hJetPhi_resolution", "#varphi resolution as a func. of jet p_{T, part}", kTH2F, {{100, -1., 1.}, pT});
  }

  // Used to fill histograms with raw and MC det. level data
  template <typename CollIterator, typename JetTable, typename TrackTable>
  void fillHistograms(CollIterator const& collision, JetTable const& jets, TrackTable const& tracks, float weight = 1.)
  {

    bool bSig_Ev = false;
    std::vector<double> phi_of_TT_cand;
    double phi_TT = 0.;
    int nTT = 0;

    auto dice = rand->Rndm();
    if (dice < frac_sig)
      bSig_Ev = true;

    for (const auto& track : tracks) {
      if (!jetderiveddatautilities::selectTrack(track, trackSelection))
        continue;

      spectra.fill(HIST("hTrackPtEtaPhi"), track.pt(), track.eta(), track.phi(), weight);

      // Search for TT candidates
      if (bSig_Ev && (track.pt() > pt_TTsig_min && track.pt() < pT_TTsig_max)) {
        phi_of_TT_cand.push_back(track.phi());
        ++nTT;
      }

      if (!bSig_Ev && (track.pt() > pt_TTref_min && track.pt() < pt_TTref_max)) {
        phi_of_TT_cand.push_back(track.phi());
        ++nTT;
      }
    }

    if (nTT > 0) { // at least 1 TT

      int iTrig = rand->Integer(nTT);
      phi_TT = phi_of_TT_cand[iTrig];

      if (bSig_Ev) {
        spectra.fill(HIST("hNtrig"), 1.5, weight);
        spectra.fill(HIST("hTTSig_per_event"), nTT, weight);
      } else {
        spectra.fill(HIST("hNtrig"), 0.5, weight);
        spectra.fill(HIST("hTTRef_per_event"), nTT, weight);
      }
    }

    for (const auto& jet : jets) {
      spectra.fill(HIST("hJetPtEtaPhiRhoArea"), jet.pt(), jet.eta(), jet.phi(), collision.rho() * jet.area(), weight);

      if (nTT > 0) {
        auto dphi = fabs(TVector2::Phi_mpi_pi(jet.phi() - phi_TT));
        bool isRecoil = (TMath::Pi() - recoilRegion) < dphi;

        if (bSig_Ev) {

          spectra.fill(HIST("hDPhi_JetPt_Corr_TTSig"), dphi, jet.pt() - collision.rho() * jet.area(), weight);
          spectra.fill(HIST("hDPhi_JetPt_TTSig"), dphi, jet.pt(), weight);
          spectra.fill(HIST("hDPhi_JetPt_RhoArea_TTSig"), dphi, jet.pt(), collision.rho() * jet.area(), weight);

          if (isRecoil) {
            spectra.fill(HIST("hRecoil_JetPt_Corr_TTSig"), jet.pt() - collision.rho() * jet.area(), weight);
            spectra.fill(HIST("hRecoil_JetPt_TTSig"), jet.pt(), weight);
          }

        } else {
          spectra.fill(HIST("hDPhi_JetPt_Corr_TTRef"), dphi, jet.pt() - collision.rho() * jet.area(), weight);
          spectra.fill(HIST("hDPhi_JetPt_TTRef"), dphi, jet.pt(), weight);
          spectra.fill(HIST("hDPhi_JetPt_RhoArea_TTRef"), dphi, jet.pt(), collision.rho() * jet.area(), weight);

          if (isRecoil) {
            spectra.fill(HIST("hRecoil_JetPt_Corr_TTRef"), jet.pt() - collision.rho() * jet.area(), weight);
            spectra.fill(HIST("hRecoil_JetPt_TTRef"), jet.pt(), weight);
          }
        }
      }
    }
  }

  /*
  Currently, we don't have possibility to estimate bgkd for particle MC
  Nima could implement functionality to do that
  */
  /// \TODO: Wait until Nima will add the code for rho estimation

  template </*typename C, */ typename JetTable, typename ParticleTable>
  void fillMCPHistograms(/*C const& collision, */ JetTable const& jets, ParticleTable const& particles, float weight = 1.)
  {

    bool bSig_Ev = false;
    std::vector<double> phi_of_TT_cand;
    double phi_TT = 0.;
    int nTT = 0;
    float pTHat = 10. / (std::pow(weight, 1.0 / pTHatExponent));

    auto dice = rand->Rndm();
    if (dice < frac_sig)
      bSig_Ev = true;

    for (const auto& particle : particles) {

      // Need charge and primary particles
      bool bIsParticleNeutral = (static_cast<uint8_t>(particle.e()) == 0);
      if (bIsParticleNeutral || (!particle.isPhysicalPrimary()))
        continue;

      if (bSig_Ev && (particle.pt() > pt_TTsig_min && particle.pt() < pT_TTsig_max)) {
        phi_of_TT_cand.push_back(particle.phi());
        ++nTT;
      }

      if (!bSig_Ev && (particle.pt() > pt_TTref_min && particle.pt() < pt_TTref_max)) {
        phi_of_TT_cand.push_back(particle.phi());
        ++nTT;
      }
      spectra.fill(HIST("hPartPtEtaPhi"), particle.pt(), particle.eta(), particle.phi(), weight);
    }

    if (nTT > 0) {

      int iTrig = rand->Integer(nTT);
      phi_TT = phi_of_TT_cand[iTrig];

      if (bSig_Ev) {
        spectra.fill(HIST("hNtrig_Part"), 1.5, weight);
        spectra.fill(HIST("hTTSig_per_event_Part"), nTT, weight);
      } else {
        spectra.fill(HIST("hNtrig_Part"), 0.5, weight);
        spectra.fill(HIST("hTTRef_per_event_Part"), nTT, weight);
      }
    }

    for (const auto& jet : jets) {

      if (jet.pt() > pTHatMaxMCP * pTHat)
        continue;

      spectra.fill(HIST("hJetPtEtaPhiRhoArea_Part"), jet.pt(), jet.eta(), jet.phi(), /*collision.rho() */ jet.area(), weight);

      if (nTT > 0) {

        auto dphi = fabs(TVector2::Phi_mpi_pi(jet.phi() - phi_TT));
        bool isRecoil = (TMath::Pi() - recoilRegion) < dphi;

        if (bSig_Ev) {

          spectra.fill(HIST("hDPhi_JetPt_Corr_TTSig_Part"), dphi, jet.pt() /* - collision.rho() * jet.area()*/, weight);
          spectra.fill(HIST("hDPhi_JetPt_TTSig_Part"), dphi, jet.pt(), weight);
          spectra.fill(HIST("hDPhi_JetPt_RhoArea_TTSig_Part"), dphi, jet.pt(), /*collision.rho() */ jet.area(), weight);

          if (isRecoil) {
            spectra.fill(HIST("hRecoil_JetPt_Corr_TTSig_Part"), jet.pt() /*- collision.rho() * jet.area()*/, weight);
            spectra.fill(HIST("hRecoil_JetPt_TTSig_Part"), jet.pt(), weight);
          }

        } else {

          spectra.fill(HIST("hDPhi_JetPt_Corr_TTRef_Part"), dphi, jet.pt() /*- collision.rho() * jet.area()*/, weight);
          spectra.fill(HIST("hDPhi_JetPt_TTRef_Part"), dphi, jet.pt(), weight);
          spectra.fill(HIST("hDPhi_JetPt_RhoArea_TTRef_Part"), dphi, jet.pt(), /*collision.rho() */ jet.area(), weight);

          if (isRecoil) {
            spectra.fill(HIST("hRecoil_JetPt_Corr_TTRef_Part"), jet.pt() /*- collision.rho() * jet.area()*/, weight);
            spectra.fill(HIST("hRecoil_JetPt_TTRef_Parht"), jet.pt(), weight);
          }
        }
      }
    }
  }

  template <typename DetLevelJets, typename PartLevelJets>
  void fillMatchedHistograms(DetLevelJets const& jets_det_level, PartLevelJets const& jets_part_level, float weight = 1.0)
  {

    float pTHat = 10. / (std::pow(weight, 1.0 / pTHatExponent));

    for (const auto& jet_det_level : jets_det_level) {
      if (jet_det_level.pt() > pTHatMaxMCD * pTHat)
        continue;

      if (jet_det_level.has_matchedJetGeo()) {

        const auto jets_matched_part_level = jet_det_level.template matchedJetGeo_as<std::decay_t<PartLevelJets>>(); // we can add "matchedJetPt_as"

        for (const auto& jet_matched_part_level : jets_matched_part_level) {

          /*
          Which histos we want:
          1) det pT vs. part. pT for inclusive jets (corrected for rho*A and not)
          2) det pT vs. part. pT for recoil jets
          3) same as (1) and (2) but 4D with dphi parts
          4) distribution of fake and miss jets
          5) pT and phi resolutions
          */

          spectra.fill(HIST("hJetPt_PartLevel_vs_DetLevel"), jet_det_level.pt(), jet_matched_part_level.pt(), weight);
          spectra.fill(HIST("hJetPt_resolution"), (jet_det_level.pt() - jet_matched_part_level.pt()) / jet_matched_part_level.pt(), jet_matched_part_level.pt(), weight);
          spectra.fill(HIST("hJetPhi_resolution"), jet_det_level.phi() - jet_matched_part_level.phi(), jet_matched_part_level.pt(), weight);
        }
      } else {
        spectra.fill(HIST("hFakeJets_pT"), jet_det_level.pt(), weight);
      }
    }

    // Missed jets
    for (const auto& jet_part_level : jets_part_level) {
      if (!jet_part_level.has_matchedJetGeo()) {
        spectra.fill(HIST("hMissedJets_pT"), jet_part_level.pt(), weight);
      }
    }
  }

  void processData(filtered_Coll const& collision,
                   filtered_Jets const& jets,
                   filtered_Tracks const& tracks)
  {

    bool bEventSkip = (!collision.selection_bit(aod::evsel::kNoTimeFrameBorder)) || (!jetderiveddatautilities::selectCollision(collision, eventSelection));
    if (bEventSkip)
      return;

    spectra.fill(HIST("vertexZ"), collision.posZ());
    fillHistograms(collision, jets, tracks);
  }
  PROCESS_SWITCH(jetHadronRecoil_OO, processData, "process data", true);

  void processMC_DetLevel(filtered_Coll const& collision,
                          filtered_Jets_DetLevel const& jets,
                          filtered_Tracks const& tracks)
  {
    bool bEventSkip = (!collision.selection_bit(aod::evsel::kNoTimeFrameBorder)) || (!jetderiveddatautilities::selectCollision(collision, eventSelection));
    if (bEventSkip)
      return;

    spectra.fill(HIST("vertexZ"), collision.posZ());
    fillHistograms(collision, jets, tracks);
  }
  PROCESS_SWITCH(jetHadronRecoil_OO, processMC_DetLevel, "process MC detector level", false);

  void processMC_DetLevel_Weighted(filtered_Coll_DetLevel_to_GetWeight const& collision,
                                   JetMcCollisions const&,
                                   filtered_Jets_DetLevel const& jets,
                                   filtered_Tracks const& tracks)
  {
    bool bEventSkip = (!collision.selection_bit(aod::evsel::kNoTimeFrameBorder)) || (!jetderiveddatautilities::selectCollision(collision, eventSelection));
    if (bEventSkip)
      return;

    /// \TODO: should we implement function to check whether Collision was reconstructed (has_mcCollision() function). Example: https://github.com/AliceO2Group/O2Physics/blob/1cba330514ab47c15c0095d8cee9633723d8e2a7/PWGJE/Tasks/v0qa.cxx#L166?
    auto weight = collision.mcCollision().weight(); // "mcCollision" where is defined?
    spectra.fill(HIST("vertexZ"), collision.posZ(), weight);
    fillHistograms(collision, jets, tracks, weight);
  }
  PROCESS_SWITCH(jetHadronRecoil_OO, processMC_DetLevel_Weighted, "process MC detector level with event weight", false);

  void processMC_PartLevel(filtered_Coll_PartLevel const& collision,
                           filtered_Jets_PartLevel const& jets,
                           JetParticles const& particles)
  {
    spectra.fill(HIST("vertexZ"), collision.posZ());
    fillMCPHistograms(jets, particles);
  }
  PROCESS_SWITCH(jetHadronRecoil_OO, processMC_PartLevel, "process MC particle level", false);

  void processMC_PartLevel_Weighted(filtered_Coll_PartLevel const& collision,
                                    filtered_Jets_PartLevel const& jets,
                                    JetParticles const& particles)
  {
    auto weight = collision.weight();
    spectra.fill(HIST("vertexZ"), collision.posZ(), weight);
    fillMCPHistograms(jets, particles, weight);
  }
  PROCESS_SWITCH(jetHadronRecoil_OO, processMC_PartLevel_Weighted, "process MC particle level with event weight", false);

  void processJetsMCPMCDMatched(soa::Filtered<JetCollisionsMCD>::iterator const& collision,
                                filtered_MatchedJets_DetLevel const& mcdjets,
                                filtered_MatchedJets_PartLevel const& mcpjets)
  {
    bool bEventSkip = (!collision.selection_bit(aod::evsel::kNoTimeFrameBorder)) || (!jetderiveddatautilities::selectCollision(collision, eventSelection));
    if (bEventSkip)
      return;

    auto mcpjetsPerMCCollision = mcpjets.sliceBy(PartJetsPerCollision, collision.mcCollisionId());
    fillMatchedHistograms(mcdjets, mcpjetsPerMCCollision);
  }
  PROCESS_SWITCH(jetHadronRecoil_OO, processJetsMCPMCDMatched, "process MC matching of inclusive jets (no weight)", false);

  void processJetsMCPMCDMatchedWeighted(soa::Filtered<JetCollisionsMCD>::iterator const& collision,
                                        JetMcCollisions const&,
                                        filtered_MatchedJets_DetLevel const& mcdjets,
                                        filtered_MatchedJets_PartLevel const& mcpjets)
  {
    bool bEventSkip = (!collision.selection_bit(aod::evsel::kNoTimeFrameBorder)) || (!jetderiveddatautilities::selectCollision(collision, eventSelection));
    if (bEventSkip)
      return;

    auto mcpjetsPerMCCollision = mcpjets.sliceBy(PartJetsPerCollision, collision.mcCollisionId());
    auto weight = collision.mcCollision().weight();

    fillMatchedHistograms(mcdjets, mcpjetsPerMCCollision, weight);
  }
  PROCESS_SWITCH(jetHadronRecoil_OO, processJetsMCPMCDMatchedWeighted, "process MC matching of inclusive jets (weighted)", false);

  /*void processRecoilJetsMCPMCDMatched(soa::Filtered<JetCollisionsMCD>::iterator const& collision,
                                      soa::Filtered<soa::Join<aod::ChargedMCDetectorLevelJets, aod::ChargedMCDetectorLevelJetConstituents, aod::ChargedMCDetectorLevelJetsMatchedToChargedMCParticleLevelJets>> const& mcdjets,
                                      soa::Filtered<soa::Join<aod::Charged1MCDetectorLevelJets, aod::Charged1MCDetectorLevelJetConstituents>> const& mcdjetsWTA,
                                      soa::Filtered<soa::Join<aod::Charged1MCParticleLevelJets, aod::Charged1MCParticleLevelJetConstituents>> const& mcpjetsWTA,
                                      JetTracks const& tracks,
                                      JetParticles const&,
                                      JetMcCollisions const&,
                                      soa::Filtered<soa::Join<aod::ChargedMCParticleLevelJets, aod::ChargedMCParticleLevelJetConstituents, aod::ChargedMCParticleLevelJetsMatchedToChargedMCDetectorLevelJets>> const& mcpjets)
  {
    if (!jetderiveddatautilities::selectCollision(collision, eventSelection)) {
      return;
    }
    const auto& mcpjetsWTACut = mcpjetsWTA.sliceBy(PartJetsPerCollision, collision.mcCollisionId());
    bool ishJetEvent = false;
    for (auto& track : tracks) {
      if (track.pt() < pt_TTsig_max && track.pt() > pt_TTsig_min) {
        ishJetEvent = true;
        break;
      }
    }
    if (ishJetEvent) {
      for (const auto& mcdjet : mcdjets) {
        fillMatchedHistograms(mcdjet, mcdjetsWTA, mcpjetsWTACut, mcpjets);
      }
    }
  }
  PROCESS_SWITCH(jetHadronRecoil_OO, processRecoilJetsMCPMCDMatched, "process MC matched (recoil jets)", false);

  void processRecoilJetsMCPMCDMatchedWeighted(soa::Filtered<JetCollisionsMCD>::iterator const& collision,
                                              soa::Filtered<soa::Join<aod::ChargedMCDetectorLevelJets, aod::ChargedMCDetectorLevelJetConstituents, aod::ChargedMCDetectorLevelJetsMatchedToChargedMCParticleLevelJets, aod::ChargedMCDetectorLevelJetEventWeights>> const& mcdjets,
                                              soa::Filtered<soa::Join<aod::Charged1MCDetectorLevelJets, aod::Charged1MCDetectorLevelJetConstituents>> const& mcdjetsWTA,
                                              soa::Filtered<soa::Join<aod::Charged1MCParticleLevelJets, aod::Charged1MCParticleLevelJetConstituents>> const& mcpjetsWTA,
                                              JetTracks const& tracks,
                                              JetParticles const&,
                                              JetMcCollisions const&,
                                              soa::Filtered<soa::Join<aod::ChargedMCParticleLevelJets, aod::ChargedMCParticleLevelJetConstituents, aod::ChargedMCParticleLevelJetsMatchedToChargedMCDetectorLevelJets, aod::ChargedMCParticleLevelJetEventWeights>> const& mcpjets)
  {
    if (!jetderiveddatautilities::selectCollision(collision, eventSelection)) {
      return;
    }
    const auto& mcpjetsWTACut = mcpjetsWTA.sliceBy(PartJetsPerCollision, collision.mcCollisionId());
    bool ishJetEvent = false;
    for (auto& track : tracks) {
      if (track.pt() < pt_TTsig_max && track.pt() > pt_TTsig_min) {
        ishJetEvent = true;
        break;
      }
    }
    if (ishJetEvent) {
      for (const auto& mcdjet : mcdjets) {
        fillMatchedHistograms(mcdjet, mcdjetsWTA, mcpjetsWTACut, mcpjets, mcdjet.eventWeight());
      }
    }
  }
  PROCESS_SWITCH(jetHadronRecoil_OO, processRecoilJetsMCPMCDMatchedWeighted, "process MC matched with event weights (recoil jets)", false);*/
};

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc) { return WorkflowSpec{adaptAnalysisTask<jetHadronRecoil_OO>(cfgc, TaskName{"jetHadronRecoil_OO"})}; }
