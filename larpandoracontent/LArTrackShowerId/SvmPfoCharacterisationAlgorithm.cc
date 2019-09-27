/**
 *  @file   larpandoracontent/LArTrackShowerId/SvmPfoCharacterisationAlgorithm.cc
 *
 *  @brief  Implementation of the svm pfo characterisation algorithm class.
 *
 *  $Log: $
 */
#include "Pandora/AlgorithmHeaders.h"

#include "larpandoracontent/LArHelpers/LArFileHelper.h"
#include "larpandoracontent/LArHelpers/LArMCParticleHelper.h"
#include "larpandoracontent/LArHelpers/LArMonitoringHelper.h"
#include "larpandoracontent/LArHelpers/LArClusterHelper.h"
#include "larpandoracontent/LArHelpers/LArPfoHelper.h"
#include "larpandoracontent/LArHelpers/LArMvaHelper.h"
#include <vector>
#include "larpandoracontent/LArHelpers/LArInteractionTypeHelper.h"

#include "larpandoracontent/LArTrackShowerId/SvmPfoCharacterisationAlgorithm.h"

#include "TMVA/Tools.h"
#include "TMVA/Reader.h"

using namespace pandora;

namespace lar_content
{

SvmPfoCharacterisationAlgorithm::SvmPfoCharacterisationAlgorithm() :
    m_trainingSetMode(false),
    m_enableProbability(true),
    m_useThreeDInformation(true),
    m_minProbabilityCut(0.5f),
    m_minCaloHitsCut(5),
    m_filePathEnvironmentVariable("FW_SEARCH_PATH"),
    m_writeToTree(true),
    m_eventNumber(-1)
{
}
//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode SvmPfoCharacterisationAlgorithm::Run()
{
   ++m_eventNumber;

   //std::cout << "event number is: " << m_eventNumber << std::endl;
   return PfoCharacterisationBaseAlgorithm::Run();
}


//------------------------------------------------------------------------------------------------------------------------------------------

SvmPfoCharacterisationAlgorithm::~SvmPfoCharacterisationAlgorithm()
{
  if (m_writeToTree)
    {
      std::string m_treeName = "new_numu_nue_comp_pur_minus_2";
      std::string m_fileName = "new_numu_nue_comp_pur_minus_2.root";
      //std::cout << "file name is: " <<  m_fileName << " ----" << std::endl;
      //std::cout << "tree name is: " <<  m_treeName << " ----" << std::endl;	
      PandoraMonitoringApi::SaveTree(this->GetPandora(), m_treeName.c_str(), m_fileName.c_str(), "UPDATE");
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
bool SvmPfoCharacterisationAlgorithm::IsClearTrack(const Cluster *const pCluster) const
{
    if (pCluster->GetNCaloHits() < m_minCaloHitsCut)
        return false;

    const LArMvaHelper::MvaFeatureVector featureVector(LArMvaHelper::CalculateFeatures(m_featureToolVector, this, pCluster));

    if (m_trainingSetMode)
    {
        bool isTrueTrack(false);

        try
        {
            const MCParticle *const pMCParticle(MCParticleHelper::GetMainMCParticle(pCluster));
            isTrueTrack = ((PHOTON != pMCParticle->GetParticleId()) && (E_MINUS != std::abs(pMCParticle->GetParticleId())));
        }
        catch (const StatusCodeException &) {}
        LArMvaHelper::ProduceTrainingExample(m_trainingOutputFile, isTrueTrack, featureVector);
        return isTrueTrack;
    }

    if (!m_enableProbability)
    {
        return LArMvaHelper::Classify(m_supportVectorMachine, featureVector);
    }
    else
    {
        return (LArMvaHelper::CalculateProbability(m_supportVectorMachine, featureVector) > m_minProbabilityCut);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

bool SvmPfoCharacterisationAlgorithm::IsClearTrack(const pandora::ParticleFlowObject *const pPfo) const
{
    static unsigned int pfoNumber = -1;

    ++pfoNumber;
 
    //std::cout << "pfoNumber: " << pfoNumber << std::endl;
 
    if (!LArPfoHelper::IsThreeD(pPfo))
    {
        if (m_enableProbability)
        {
            object_creation::ParticleFlowObject::Metadata metadata;
            metadata.m_propertiesToAdd["TrackScore"] = -1.f;
            PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::ParticleFlowObject::AlterMetadata(*this, pPfo, metadata));
        }
        return (pPfo->GetParticleId() == MU_MINUS);
    }

    ClusterList wClusterList;
    LArPfoHelper::GetClusters(pPfo, TPC_VIEW_W, wClusterList);

    //charge related features are only calculated using hits in W view
    // This won't work unless use 3D info is set to true - dev purposes only
    const PfoCharacterisationFeatureTool::FeatureToolVector &chosenFeatureToolVector(wClusterList.empty() ? m_featureToolVectorNoChargeInfo : m_featureToolVectorThreeD);

    std::string m_treeName = "new_numu_nue_comp_pur_minus_2"; // TODO This should be a member variable

    // Purity, completeness
    // ATTN Assume your Pfos of interest are in a PfoList called myPfoList

    // Input lists
    const PfoList myPfoList(1, pPfo);

    const MCParticleList *pMCParticleList = nullptr;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetList(*this, m_mcParticleListName, pMCParticleList));

    const CaloHitList *pCaloHitList = nullptr;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetList(*this, m_caloHitListName, pCaloHitList));

    // Mapping target MCParticles -> truth associated Hits
    LArMCParticleHelper::MCContributionMap targetMCParticleToHitsMap;
    LArMCParticleHelper::PrimaryParameters mcPrimaryParameters;
    mcPrimaryParameters.m_selectInputHits = false;
    LArMCParticleHelper::SelectReconstructableMCParticles(pMCParticleList, pCaloHitList, mcPrimaryParameters, LArMCParticleHelper::IsBeamNeutrinoFinalState, targetMCParticleToHitsMap);

    LArMCParticleHelper::PfoContributionMap pfoToHitsMap;
    LArMCParticleHelper::GetPfoToReconstructable2DHitsMap(myPfoList, targetMCParticleToHitsMap, pfoToHitsMap);

    // Last step
    LArMCParticleHelper::PfoToMCParticleHitSharingMap pfoToMCHitSharingMap;
    LArMCParticleHelper::MCParticleToPfoHitSharingMap mcToPfoHitSharingMap;
    LArMCParticleHelper::GetPfoMCParticleHitSharingMaps(pfoToHitsMap, {targetMCParticleToHitsMap}, pfoToMCHitSharingMap, mcToPfoHitSharingMap);

	const CaloHitList &allHitsInPfo(pfoToHitsMap.at(pPfo));
	//std::cout << "We got a pfo, isNeutrino " << LArPfoHelper::IsNeutrino(pPfo) << ", isNeutrinoFinalState " << LArPfoHelper::IsNeutrinoFinalState(pPfo) << ", nHits " << allHitsInPfo.size()
	//          << " (U: " << LArMonitoringHelper::CountHitsByType(TPC_VIEW_U, allHitsInPfo) << ", V: " << LArMonitoringHelper::CountHitsByType(TPC_VIEW_V, allHitsInPfo) << ", W: " << LArMonitoringHelper::CountHitsByType(TPC_VIEW_W, allHitsInPfo) << ") " << std::endl;
	const int nHitsInPfoTotal(allHitsInPfo.size())/*, nHitsInPfoU(LArMonitoringHelper::CountHitsByType(TPC_VIEW_U, allHitsInPfo)), nHitsInPfoV(LArMonitoringHelper::CountHitsByType(TPC_VIEW_V, allHitsInPfo)), nHitsInPfoW(LArMonitoringHelper::CountHitsByType(TPC_VIEW_W, allHitsInPfo))*/;

	int nHitsInBestMCParticleTotal(-1),/* nHitsInBestMCParticleU(-1), nHitsInBestMCParticleV(-1), nHitsInBestMCParticleW(-1),*/ bestMCParticlePdgCode(0), bestMCParticleIsTrack(-1);
	int nHitsSharedWithBestMCParticleTotal(-1)/*, nHitsSharedWithBestMCParticleU(-1), nHitsSharedWithBestMCParticleV(-1), nHitsSharedWithBestMCParticleW(-1)*/;
        CartesianVector threeDVertexPosition(0.f, 0.f, 0.f); // Mousam Vertex
        float mcEnergy = 0.f;
	const LArMCParticleHelper::MCParticleToSharedHitsVector &mcParticleToSharedHitsVector(pfoToMCHitSharingMap.at(pPfo));

	for (const LArMCParticleHelper::MCParticleCaloHitListPair &mcParticleCaloHitListPair : mcParticleToSharedHitsVector)
	{
	    const pandora::MCParticle *const pAssociatedMCParticle(mcParticleCaloHitListPair.first);

	    const CaloHitList &allMCHits(targetMCParticleToHitsMap.at(pAssociatedMCParticle));
	    //std::cout << "Associated MCParticle: " << pAssociatedMCParticle->GetParticleId() << ", nTotalHits " << allMCHits.size()
	    //          << " (U: " << LArMonitoringHelper::CountHitsByType(TPC_VIEW_U, allMCHits) << ", V: " << LArMonitoringHelper::CountHitsByType(TPC_VIEW_V, allMCHits) << ", W: " << LArMonitoringHelper::CountHitsByType(TPC_VIEW_W, allMCHits) << ") " << std::endl;

	    const CaloHitList &associatedMCHits(mcParticleCaloHitListPair.second);
	    //std::cout << "Shared with MCParticle: " << pAssociatedMCParticle->GetParticleId() << ", nSharedHits " << associatedMCHits.size()
	    //          << " (U: " << LArMonitoringHelper::CountHitsByType(TPC_VIEW_U, associatedMCHits) << ", V: " << LArMonitoringHelper::CountHitsByType(TPC_VIEW_V, associatedMCHits) << ", W: " << LArMonitoringHelper::CountHitsByType(TPC_VIEW_W, associatedMCHits) << ") " << std::endl;

	    // This is the current best matched MCParticle, to be stored
	    //std::cout << "associatedMCHits.size() " << associatedMCHits.size() << ", nHitsSharedWithBestMCParticleTotal " << nHitsSharedWithBestMCParticleTotal << ", check " << (static_cast<int>(associatedMCHits.size()) > nHitsSharedWithBestMCParticleTotal) << std::endl;

	    if (static_cast<int>(associatedMCHits.size()) > nHitsSharedWithBestMCParticleTotal)
	    {
		 nHitsSharedWithBestMCParticleTotal = associatedMCHits.size();
		 /*nHitsSharedWithBestMCParticleU = LArMonitoringHelper::CountHitsByType(TPC_VIEW_U, associatedMCHits);
		 nHitsSharedWithBestMCParticleV = LArMonitoringHelper::CountHitsByType(TPC_VIEW_V, associatedMCHits);
		 nHitsSharedWithBestMCParticleW = LArMonitoringHelper::CountHitsByType(TPC_VIEW_W, associatedMCHits);*/

		 nHitsInBestMCParticleTotal = allMCHits.size();
		 /*nHitsInBestMCParticleU = LArMonitoringHelper::CountHitsByType(TPC_VIEW_U, allMCHits);
		 nHitsInBestMCParticleV = LArMonitoringHelper::CountHitsByType(TPC_VIEW_V, allMCHits);
		 nHitsInBestMCParticleW = LArMonitoringHelper::CountHitsByType(TPC_VIEW_W, allMCHits);*/

		 bestMCParticlePdgCode = pAssociatedMCParticle->GetParticleId();               
		 bestMCParticleIsTrack = ((PHOTON != pAssociatedMCParticle->GetParticleId()) && (E_MINUS != std::abs(pAssociatedMCParticle->GetParticleId())) ? 1 : 0);
		 threeDVertexPosition = pAssociatedMCParticle->GetVertex(); // Mousam Vertex
         mcEnergy = pAssociatedMCParticle->GetEnergy();
	    }
	}

    //---------------------------------------------Get Energy---------------------------------------------------   
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "mcEnergy", mcEnergy);  

    //----------------------------------------------Get Momentum------------------------------------------------
    
    CartesianVector momentumVector(0.f, 0.f, 0.f);
    
    momentumVector = pPfo->GetMomentum();

    float momentumX = momentumVector.GetX();
    float momentumY = momentumVector.GetY();
    float momentumZ = momentumVector.GetZ();

    //std::cout << "momentum X: " << momentumX << std::endl;
    //std::cout << "momentum Y: " << momentumY << std::endl;
    //std::cout << "momentum Z: " << momentumZ << std::endl;

    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "pfoMomentumX", momentumX);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "pfoMomentumY", momentumY);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "pfoMomentumZ", momentumZ);    

    //-------------------------------isPrimary---------------------------------------------------------------

    int isPrimary(0);
    float nAllDaughter(0.f);
    CaloHitList nHits3DParentList;    
    size_t nHits3DDaughter(0);
    PfoList newPfoList(1, pPfo);
    size_t nHits3DParent(1.f);
    PfoList allDaughtersPfoList;
    float nHits3DDaughterTotal(0);

    isPrimary = (LArPfoHelper::IsNeutrinoFinalState(pPfo) ? 1 : 0);

    //std::cout << "isPrimary: " << isPrimary << std::endl;
    //std::cout << "nAllDaughter1: " << nAllDaughter << std::endl;

    if (isPrimary == 1)
    {
        LArPfoHelper::GetCaloHits(pPfo, TPC_3D, nHits3DParentList);
        nHits3DParent = nHits3DParentList.size();
        LArPfoHelper::GetAllDownstreamPfos(newPfoList, allDaughtersPfoList);
        nAllDaughter = allDaughtersPfoList.size() - 1;
		//std::cout << "nAllDaughter: " << nAllDaughter << std::endl;
        if (nAllDaughter > 0)
        {
		allDaughtersPfoList.pop_front();
			for(const ParticleFlowObject *const pDaughterPfo : allDaughtersPfoList)
				{
                	CaloHitList nHits3DDaughterList;
                	LArPfoHelper::GetCaloHits(pDaughterPfo, TPC_3D, nHits3DDaughterList);
                    nHits3DDaughter = nHits3DDaughterList.size();
                    nHits3DDaughterTotal += nHits3DDaughter;
                    //std::cout << "nHits3DDaughter: " << nHits3DDaughter << std::endl;
					//std::cout << "pdgCodeDaughter: " <<  pDaughterPfo->GetParticleId() << std::endl;
				}
        //std::cout << "nAllDaughter2: " << nAllDaughter << std::endl;
    	}
    	else if (nAllDaughter == 0)
		{
     	nHits3DDaughter = 0;
    	}
	
        //std::cout << "Ratio: " << (static_cast<double>(nHits3DDaughterTotal))/(static_cast<double>(nHits3DParent)) << std::endl;
   
    }	
    
    
    float daughterParentNhitsRatio = (static_cast<double>(nHits3DDaughterTotal))/(static_cast<double>(nHits3DParent));
    //std::cout << "nHits3DDaughterTotal: " << nHits3DDaughterTotal << std::endl;
	//std::cout << "nHits3DParent: " << nHits3DParent << std::endl;
    
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "nHits3DParent", static_cast<float>(nHits3DParent));
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "nHits3DDaughterTotal", nHits3DDaughterTotal);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "daughterParentNhitsRatio", daughterParentNhitsRatio);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "isPrimary", isPrimary);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "nAllDaughter", nAllDaughter);
    //std::cout << "nAllDaughter: " << nAllDaughter << std::endl;   
        
    //-------------------------------Interaction-Type-writing---------------------------------------------------------------------------------

    //std::cout << "clown" << std::endl;
    
    std::string interType; 

    MCParticleVector mcPrimaryVector;
    MCParticleList mcPrimaryList;
    for (const auto &mapEntry : targetMCParticleToHitsMap) mcPrimaryVector.push_back(mapEntry.first);
    for (const MCParticle *const pMCPrimary : mcPrimaryVector)
    {
    mcPrimaryList.push_back(pMCPrimary);
    }
   
    const LArInteractionTypeHelper::InteractionType interactionTypeStr(LArInteractionTypeHelper::GetInteractionType(mcPrimaryList));
    
    //std::cout << "Interaction Types: " << LArInteractionTypeHelper::ToString(interactionType) << std::endl;
    //std::cout << "Interaction Type number: " << interactionTypeStr << std::endl;
    interType = LArInteractionTypeHelper::ToString(interactionTypeStr);
    std::cout << "interType: " << interType << std::endl;
    int interactionType = static_cast<int>(interactionTypeStr);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "interactionType", interactionType);

    //------------------------------Vertex-----------------------------------------------------------------------
    float xVertexPos = threeDVertexPosition.GetX();
    float yVertexPos = threeDVertexPosition.GetY();
    float zVertexPos = threeDVertexPosition.GetZ();

    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "xVertexPos", xVertexPos);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "yVertexPos", yVertexPos);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "zVertexPos", zVertexPos);

    //----------------------------isInFiducialVolume------------------------------------------------------------

    int isInFiducialVolume(0);

    //isInFiducialVolume = (((abs(xVertexPos) >= 340) || (abs(yVertexPos) <= 584) || zVertexPos <= 200 || zVertexPos >= 1194) ? 1 : 0);
    isInFiducialVolume = (((((xVertexPos <= -20) && (xVertexPos >= -340)) || ((xVertexPos <= 340) && (xVertexPos >= 20))) && ((abs(yVertexPos) <= 584)) && (zVertexPos >= 200 || zVertexPos <= 1194)) ? 1 : 0);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "isInFiducialVolume", isInFiducialVolume);
    
    //-----------------------------------------------------------------------------------------------------------

    const float completeness((nHitsInBestMCParticleTotal > 0) ? static_cast<float>(nHitsSharedWithBestMCParticleTotal) / static_cast<float>(nHitsInBestMCParticleTotal) : 0.f);
    const float purity((nHitsInPfoTotal > 0) ? static_cast<float>(nHitsSharedWithBestMCParticleTotal) / static_cast<float>(nHitsInPfoTotal) : 0.f);
    int pdgCode = bestMCParticlePdgCode;
    //std::cout << "pdgcode: " << pdgCode << std::endl;

    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "Completeness", completeness);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "Purity", purity);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "pdgCode", pdgCode);

    //std::cout << "potato" << std::endl;
	//std::cout << "Done loop: nHitsInPfoTotal " << nHitsInPfoTotal << ", nHitsSharedWithBestMCParticleTotal " << nHitsSharedWithBestMCParticleTotal << ", nHitsInBestMCParticleTotal " << nHitsInBestMCParticleTotal << std::endl;
	//std::cout << "Completeness " << completeness << ", Purity " << purity << std::endl;

    const int TrueTrackInt = bestMCParticleIsTrack;
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "TrueTrackInt", TrueTrackInt);

    // End purity, completeness

    //--------------------------------------Event and Pfo number writing-(uncomment)-------------------------------------------------------------------------
    //std::cout << "this" << std::endl;   
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "eventNumber", m_eventNumber);
    //std::cout << "that" << std::endl;
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "pfoNumber", static_cast<int>(pfoNumber));
    //std::cout << "where" << std::endl;
    //-----------------------------------Writing 3D hit position in tree - Mousam----------------------------------------------------------------
    
    CaloHitList uCaloList;
    LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_U, uCaloList);
    std::vector<float> uViewXHitVector;
    std::vector<float> uViewYHitVector;
    std::vector<float> uViewZHitVector;
    
    int nUEvent(0);
    for (const CaloHit *const pCaloHit : uCaloList)
      {
	//uHitVector.push_back(make_pair(uCaloList.GetX(), uCaloList.GetZ()));
	float x = pCaloHit->GetPositionVector().GetX();
    float y = pCaloHit->GetPositionVector().GetY();
	float z = pCaloHit->GetPositionVector().GetZ();
	
	uViewXHitVector.push_back(x);
	uViewYHitVector.push_back(y);        
	uViewZHitVector.push_back(z);
	++nUEvent;
      };
    
    CaloHitList vCaloList;
    LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_V, vCaloList);
    std::vector<float> vViewXHitVector;
    std::vector<float> vViewYHitVector;
    std::vector<float> vViewZHitVector;

    int nVEvent(0);
    for (const CaloHit *const pCaloHit : vCaloList)
      {
	float x = pCaloHit->GetPositionVector().GetX();
    float y = pCaloHit->GetPositionVector().GetY();
	float z = pCaloHit->GetPositionVector().GetZ();
	
	vViewXHitVector.push_back(x);
	vViewYHitVector.push_back(y);
	vViewZHitVector.push_back(z);
	++nVEvent;
      };
    
    CaloHitList wCaloList;
    LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_W, wCaloList);
    std::vector<float> wViewXHitVector;
    std::vector<float> wViewYHitVector;
    std::vector<float> wViewZHitVector;

    int nWEvent(0);
    for (const CaloHit *const pCaloHit : wCaloList)
      {
	float x = pCaloHit->GetPositionVector().GetX();
        float y = pCaloHit->GetPositionVector().GetY();
	float z = pCaloHit->GetPositionVector().GetZ();
	
	wViewXHitVector.push_back(x);
	wViewYHitVector.push_back(y);
        wViewZHitVector.push_back(z);
	++nWEvent;
      };


    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "uViewXHitVector", &uViewXHitVector);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "uViewYHitVector", &uViewYHitVector);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "uViewZHitVector", &uViewZHitVector);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "vViewXHitVector", &vViewXHitVector);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "vViewYHitVector", &uViewYHitVector);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "vViewZHitVector", &vViewZHitVector);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "wViewXHitVector", &wViewXHitVector);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "wViewYHitVector", &uViewYHitVector);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "wViewZHitVector", &wViewZHitVector);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "nUEvent", nUEvent);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "nVEvent", nVEvent);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "nWEvent", nWEvent);

    //---------------------------------------------------------------------------------------------------------------------------------

    // Start variable writing
    const LArMvaHelper::MvaFeatureVector featureVector(LArMvaHelper::CalculateFeatures(chosenFeatureToolVector, this, pPfo));

    const LArMvaHelper::MvaFeatureVector featureVectorOfType(LArMvaHelper::CalculateFeaturesOfType<ThreeDPCAVariablesFeatureTool>(chosenFeatureToolVector, this, pPfo));

    float conc(featureVectorOfType.at(0).IsInitialized() ? featureVectorOfType.at(0).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "conc", conc);

    float conc2(featureVectorOfType.at(1).IsInitialized() ? featureVectorOfType.at(1).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "conc2", conc2);
    
    float conic(featureVectorOfType.at(2).IsInitialized() ? featureVectorOfType.at(2).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "conic", conic);
    
    float densY(featureVectorOfType.at(3).IsInitialized() ? featureVectorOfType.at(3).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "densY", densY);
    
    float densZ(featureVectorOfType.at(4).IsInitialized() ? featureVectorOfType.at(4).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "densZ", densZ);
    
    float densYRatio(featureVectorOfType.at(5).IsInitialized() ? featureVectorOfType.at(5).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "densYRatio", densYRatio);
    
    float densZRatio(featureVectorOfType.at(6).IsInitialized() ? featureVectorOfType.at(6).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "densZRatio", densZRatio);
    
    float nSegDoubleHits(featureVectorOfType.at(7).IsInitialized() ? featureVectorOfType.at(7).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "nSegDoubleHits", nSegDoubleHits);
    
    float nSegDoubleHitsRatio(featureVectorOfType.at(8).IsInitialized() ? featureVectorOfType.at(8).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "nSegDoubleHitsRatio", nSegDoubleHitsRatio);
    
    float nHits(featureVectorOfType.at(9).IsInitialized() ? featureVectorOfType.at(9).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "nHits", nHits);

    //const LArMvaHelper::MvaFeatureVector curvFeatureVectorOfType(LArMvaHelper::CalculateFeaturesOfType<TwoDCurvatureFeatureTool>(chosenFeatureToolVector, this, pPfo));
    
    //float curv(-1.);//curvFeatureVectorOfType.at(0).Get());
    //PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "curv", curv);

    const LArMvaHelper::MvaFeatureVector threeDLinearFitFeatureVectorOfType(LArMvaHelper::CalculateFeaturesOfType<ThreeDLinearFitFeatureTool>(chosenFeatureToolVector, this, pPfo));
    
    float length(threeDLinearFitFeatureVectorOfType.at(0).IsInitialized() ? threeDLinearFitFeatureVectorOfType.at(0).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "length", length);

    float diff(threeDLinearFitFeatureVectorOfType.at(1).IsInitialized() ? threeDLinearFitFeatureVectorOfType.at(1).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "diff", diff);

    float gap(threeDLinearFitFeatureVectorOfType.at(2).IsInitialized() ? threeDLinearFitFeatureVectorOfType.at(2).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "gap", gap);

    float rms(threeDLinearFitFeatureVectorOfType.at(3).IsInitialized() ? threeDLinearFitFeatureVectorOfType.at(3).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "rms", rms);

    const LArMvaHelper::MvaFeatureVector threeDVertexDistanceFeatureVectorOfType(LArMvaHelper::CalculateFeaturesOfType<ThreeDVertexDistanceFeatureTool>(chosenFeatureToolVector, this, pPfo));
    
    float vertexDistance(threeDVertexDistanceFeatureVectorOfType.at(0).IsInitialized() ? threeDVertexDistanceFeatureVectorOfType.at(0).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "vertexDistance", vertexDistance);

    const LArMvaHelper::MvaFeatureVector threeDOpeningAngleFeatureVectorOfType(LArMvaHelper::CalculateFeaturesOfType<ThreeDOpeningAngleFeatureTool>(chosenFeatureToolVector, this, pPfo));
    
    float diffAngle(threeDOpeningAngleFeatureVectorOfType.at(0).IsInitialized() ? threeDOpeningAngleFeatureVectorOfType.at(0).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "diffAngle", diffAngle);

    const LArMvaHelper::MvaFeatureVector threeDPCAFeatureVectorOfType(LArMvaHelper::CalculateFeaturesOfType<ThreeDPCAFeatureTool>(chosenFeatureToolVector, this, pPfo));
    
    float pca1(threeDPCAFeatureVectorOfType.at(0).IsInitialized() ? threeDPCAFeatureVectorOfType.at(0).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "pca1", pca1);

    float pca2(threeDPCAFeatureVectorOfType.at(1).IsInitialized() ? threeDPCAFeatureVectorOfType.at(1).Get() : -std::numeric_limits<float>::max());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "pca2", pca2);

    int isChargeInfoAvailable(!wClusterList.empty());
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "isChargeInfoAvailable", isChargeInfoAvailable);

    float charge1(-std::numeric_limits<float>::max()), charge2(-std::numeric_limits<float>::max());

    if (!wClusterList.empty())
    {
        const LArMvaHelper::MvaFeatureVector threeDChargeFeatureVectorOfType(LArMvaHelper::CalculateFeaturesOfType<ThreeDChargeFeatureTool>(chosenFeatureToolVector, this, pPfo));
        charge1 = (threeDChargeFeatureVectorOfType.at(0).IsInitialized() ? threeDChargeFeatureVectorOfType.at(0).Get() : -std::numeric_limits<float>::max());
        charge2 = (threeDChargeFeatureVectorOfType.at(1).IsInitialized() ? threeDChargeFeatureVectorOfType.at(1).Get() : -std::numeric_limits<float>::max());
    }
    
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "charge1", charge1);
    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "charge2", charge2);
    
    //std::cout << "finished" << std::endl;
    PandoraMonitoringApi::FillTree(this->GetPandora(), m_treeName.c_str()); 


    // TODO Apply trained BDT output
    // TODO FInalise variables, training, etc.
    TMVA::Reader *reader = new TMVA::Reader( "!Color:!Silent" );
    //reader->AddVariable("conc", &conc);hn had a quick look at it together and we both feel like these types of pfos should be removed from our BDT training. We think that by either introducing a cut on high completeness and purity or just straight out removing DIS events from the training sample would get rid of these pfos. We would like your input on how to handle these types of events. If you are intere
    //reader->AddVariable("conc2", &conc2);
    //reader->AddVariable("conic", &conic);
    //reader->AddVariable("densY", &densY);
    //reader->AddVariable("densZ", &densZ);
    //reader->AddVariable("densYRatio", &densYRatio);
    //reader->AddVariable("densZRatio", &densZRatio);
    //reader->AddVariable("nSegDoubleHits", &nSegDoubleHits);
    //reader->AddVariable("nSegDoubleHitsRatio", &nSegDoubleHitsRatio);
    reader->AddVariable("length", &length);
    reader->AddVariable("diff", &diff);
    reader->AddVariable("gap", &gap);
    reader->AddVariable("rms", &rms);
    reader->AddVariable("vertexDistance", &vertexDistance);
    reader->AddVariable("diffAngle", &diffAngle);
    reader->AddVariable("pca1", &pca1);
    reader->AddVariable("pca2", &pca2);
    reader->AddVariable("charge1", &charge1);
    reader->AddVariable("charge2", &charge2);
    //reader->AddVariable("nAllDaughter", &nAllDaughter);
    //reader->AddVariable("nHits3DDaughterTotal", &nHits3DDaughterTotal);
    //reader->AddVariable("daughterParentNhitsRatio", &daughterParentNhitsRatio);

    reader->BookMVA("BDT method", "/storage/epp2/phrwdg/Dune/newVarPandora/tmva/dataset/weights/track_shower_ubooneVar_numu_nue_vertex_merged_BDT.weights.xml");
    const float bdtValue(reader->EvaluateMVA("BDT method"));

    PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "bdtValue", bdtValue);
    PandoraMonitoringApi::FillTree(this->GetPandora(), m_treeName.c_str());
    delete reader;

    // End variable writing

    // TODO Use BDT output to return from this method, so as to evaluate final output performance
    // TODO Tune this value
    return (bdtValue > -0.2);



    // Display interesting pfo
    /*if (length > 148 && length < 149 && abs(pdgCode) == 2212 && gap > 0.1441 && gap < 0.1442 && bdtValue > 0.071 && bdtValue < 0.072)
    {
      std::cout << "The length value is: " << length << std::endl;
      std::cout << "The vertexDistance value is: " << vertexDistance << std::endl;
      std::cout << "The conc2 value is: " << conc2 << std::endl;
      std::cout << "The conic value is: " << conic << std::endl;
      std::cout << "The densY value is: " << densY << std::endl;
      std::cout << "The densZ value is: " << densZ << std::endl;
      std::cout << "The densYRatio value is: " << densYRatio << std::endl;
      std::cout << "The densZRatio value is: " << densZRatio << std::endl;
      std::cout << "The nSegDoubleHits value is: " << nSegDoubleHits << std::endl;
      std::cout << "The nSegDoubleHitsRatio value is: " << nSegDoubleHitsRatio << std::endl;
      const PfoList myPfoList1(1, pPfo);
        PandoraMonitoringApi::SetEveDisplayParameters(this->GetPandora(), true, DETECTOR_VIEW_XZ, -1.f, -1.f, 1.f);
        PandoraMonitoringApi::VisualizeParticleFlowObjects(this->GetPandora(), &myPfoList1, "MyPfoList", RED, true, false);
        PandoraMonitoringApi::ViewEvent(this->GetPandora());
    }*/
    // End display interesting pfo

    if (m_trainingSetMode)
    {
/*      bool isTrueTrack(false);
        bool isMainMCParticleSet(false);

        try
        {
            const MCParticle *const pMCParticle(LArMCParticleHelper::GetMainMCParticle(pPfo));
            isTrueTrack = ((PHOTON != pMCParticle->GetParticleId()) && (E_MINUS != std::abs(pMCParticle->GetParticleId())));
            isMainMCParticleSet = (pMCParticle->GetParticleId() != 0);
        }
        catch (const StatusCodeException &) {}
*/
        const bool isTrueTrack(1 == bestMCParticleIsTrack);
        const bool isMainMCParticleSet(0 != bestMCParticlePdgCode);

        if (isMainMCParticleSet)
        {
            std::string outputFile;
            outputFile.append(m_trainingOutputFile);
            const std::string end=((wClusterList.empty()) ? "noChargeInfo.txt" : ".txt");
            outputFile.append(end);
            // LArMvaHelper::ProduceTrainingExample(outputFile, isTrueTrack, featureVector); // TODO Need this for sklearn training
        }

        return isTrueTrack;
    } // training mode

    //check for failures in the calculation of features, i.e. not initialized features
    for (const LArMvaHelper::MvaFeature &featureValue : featureVector)
    {
        if (!featureValue.IsInitialized())
        {
            if (m_enableProbability)
            {
                object_creation::ParticleFlowObject::Metadata metadata;
                metadata.m_propertiesToAdd["TrackScore"] = -1.f;
                PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::ParticleFlowObject::AlterMetadata(*this, pPfo, metadata));
            }
            return (pPfo->GetParticleId() == MU_MINUS);
        }
    }

    //if no failures, proceed with svm classification
    if (!m_enableProbability)
    {
        return LArMvaHelper::Classify((wClusterList.empty() ? m_supportVectorMachineNoChargeInfo : m_supportVectorMachine), featureVector);
    }
    else
    {
        const double score(LArMvaHelper::CalculateProbability((wClusterList.empty() ? m_supportVectorMachineNoChargeInfo : m_supportVectorMachine), featureVector));
        object_creation::ParticleFlowObject::Metadata metadata;
        metadata.m_propertiesToAdd["TrackScore"] = score;
        PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::ParticleFlowObject::AlterMetadata(*this, pPfo, metadata));
        return (m_minProbabilityCut <= score);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode SvmPfoCharacterisationAlgorithm::ReadSettings(const TiXmlHandle xmlHandle)
{
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "TrainingSetMode", m_trainingSetMode));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinCaloHitsCut", m_minCaloHitsCut));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "UseThreeDInformation", m_useThreeDInformation));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "FilePathEnvironmentVariable", m_filePathEnvironmentVariable));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "SvmFileName", m_svmFileName));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "SvmName", m_svmName));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "WriteToTree", m_writeToTree)); // added by Mousam

    /*PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "TreeName", m_treeName)); // added by Mousam
    
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "FileName", m_fileName)); // added by Mousam*/

    if (m_useThreeDInformation)
    {
        PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
            "SvmFileNameNoChargeInfo", m_svmFileNameNoChargeInfo));

        PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
            "SvmNameNoChargeInfo", m_svmNameNoChargeInfo));
    }

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "EnableProbability",  m_enableProbability));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinProbabilityCut", m_minProbabilityCut));

    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, XmlHelper::ReadValue(xmlHandle, "CaloHitListName", m_caloHitListName));
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, XmlHelper::ReadValue(xmlHandle, "MCParticleListName", m_mcParticleListName));

    if (m_trainingSetMode)
    {
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, XmlHelper::ReadValue(xmlHandle, "TrainingOutputFileName", m_trainingOutputFile));
    }
    else
    {
        if (m_svmFileName.empty() || m_svmName.empty())
        {
            std::cout << "SvmPfoCharacterisationAlgorithm: SvmFileName and SvmName must be set if in classification mode " << std::endl;
            return STATUS_CODE_INVALID_PARAMETER;
        }

        const std::string fullSvmFileName(LArFileHelper::FindFileInPath(m_svmFileName, m_filePathEnvironmentVariable));
        m_supportVectorMachine.Initialize(fullSvmFileName, m_svmName);

        if (m_useThreeDInformation)
        {
            if (m_svmFileNameNoChargeInfo.empty() || m_svmNameNoChargeInfo.empty())
            {
                std::cout << "SvmPfoCharacterisationAlgorithm: SvmFileName and SvmName must be set if in classification mode for no charge info in 3D mode " << std::endl;
                return STATUS_CODE_INVALID_PARAMETER;
            }
            const std::string fullSvmFileNameNoChargeInfo(LArFileHelper::FindFileInPath(m_svmFileNameNoChargeInfo, m_filePathEnvironmentVariable));
            m_supportVectorMachineNoChargeInfo.Initialize(fullSvmFileNameNoChargeInfo, m_svmNameNoChargeInfo);
        }
    }

    AlgorithmToolVector algorithmToolVector;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, XmlHelper::ProcessAlgorithmToolList(*this, xmlHandle, "FeatureTools", algorithmToolVector));

    if (m_useThreeDInformation)
    {
        AlgorithmToolVector algorithmToolVectorNoChargeInfo;
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, XmlHelper::ProcessAlgorithmToolList(*this, xmlHandle, "FeatureToolsNoChargeInfo", algorithmToolVectorNoChargeInfo));
        for (AlgorithmTool *const pAlgorithmTool : algorithmToolVector)
            PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, LArMvaHelper::AddFeatureToolToVector(pAlgorithmTool, m_featureToolVectorThreeD));
        for (AlgorithmTool *const pAlgorithmTool : algorithmToolVectorNoChargeInfo)
            PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, LArMvaHelper::AddFeatureToolToVector(pAlgorithmTool, m_featureToolVectorNoChargeInfo));
    }
    else
    {
        for (AlgorithmTool *const pAlgorithmTool : algorithmToolVector)
            PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, LArMvaHelper::AddFeatureToolToVector(pAlgorithmTool, m_featureToolVector));
    }

    return PfoCharacterisationBaseAlgorithm::ReadSettings(xmlHandle);
}

} // namespace lar_content
