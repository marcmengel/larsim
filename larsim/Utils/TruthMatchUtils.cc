/**
 * @file larsim/Utils/TruthMatchUtils.cc
 *
 * @brief Implementation of the TruthMatchUtils functions
 *
 * @author Dom Brailsford (d.brailsford@lancaster.ac.uk)
 *
 * $Log: $
 */

//STL
#include <utility>
//ART
#include "canvas/Utilities/Exception.h"
#include "messagefacility/MessageLogger/MessageLogger.h"
//LARSoft
#include "larsim/MCCheater/BackTrackerService.h"

#include "TruthMatchUtils.h"

namespace
{

using namespace TruthMatchUtils;
IDToEDepositMap::const_iterator MaxEDepElementInMap(const IDToEDepositMap &idToEDepMap)
{
    IDToEDepositMap::const_iterator highestContribIt(std::max_element(idToEDepMap.begin(), idToEDepMap.end(),
        [](const std::pair<G4ID, EDeposit>& a, const std::pair<G4ID, EDeposit>& b)->bool{ return
        std::nextafter(a.second, std::numeric_limits<EDeposit>::lowest()) <= b.second && std::nextafter(a.second, std::numeric_limits<EDeposit>::max()) >= b.second
        ? 1 : a.second < b.second; } ));

    if (idToEDepMap.end() == highestContribIt)
    {
     throw art::Exception(art::errors::LogicError)
        << "TruthMatchUtils did not manage to find a max element in the g4 ID to energy deposit map.  The map size is: "
        << idToEDepMap.size();
    }

    return highestContribIt;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------------------------

TruthMatchUtils::G4ID TruthMatchUtils::TrueParticleID(const art::Ptr<recob::Hit> &pHit, const bool rollupUnsavedIDs)
{
    IDToEDepositMap idToEDepMap;
    TruthMatchUtils::FillG4IDToEnergyDepositMap(idToEDepMap, pHit, rollupUnsavedIDs);
    if (idToEDepMap.empty())
        return kNoG4ID;

    return MaxEDepElementInMap(idToEDepMap)->first;
}

//------------------------------------------------------------------------------------------------------------------------------------------

TruthMatchUtils::G4ID TruthMatchUtils::TrueParticleIDFromTotalTrueEnergy(const std::vector<art::Ptr<recob::Hit> >& pHits,
    const bool rollupUnsavedIDs)
{
    IDToEDepositMap idToEDepMap;
    for (const art::Ptr<recob::Hit> &pHit : pHits)
        TruthMatchUtils::FillG4IDToEnergyDepositMap(idToEDepMap, pHit, rollupUnsavedIDs);

    if (idToEDepMap.empty())
        return kNoG4ID;

    return MaxEDepElementInMap(idToEDepMap)->first;
}

//------------------------------------------------------------------------------------------------------------------------------------------

TruthMatchUtils::G4ID TruthMatchUtils::TrueParticleIDFromTotalRecoCharge(const std::vector<art::Ptr<recob::Hit> >& pHits,
    const bool rollupUnsavedIDs)
{
    IDToEDepositMap idToChargeDepMap;
    for (const art::Ptr<recob::Hit> &pHit : pHits)
    {
        const G4ID g4ID(TruthMatchUtils::TrueParticleID(pHit, rollupUnsavedIDs));
        const EDeposit recoCharge(static_cast<EDeposit>(pHit->Integral()));
        auto [iterator, inserted] = idToChargeDepMap.try_emplace(g4ID, recoCharge);
        if (!inserted)
            iterator->second += recoCharge;
    }

    if (idToChargeDepMap.empty())
        return kNoG4ID;

    return MaxEDepElementInMap(idToChargeDepMap)->first;
}

//------------------------------------------------------------------------------------------------------------------------------------------

TruthMatchUtils::G4ID TruthMatchUtils::TrueParticleIDFromTotalRecoHits(const std::vector<art::Ptr<recob::Hit> >& pHits,
    const bool rollupUnsavedIDs)
{
    std::map<G4ID, unsigned int> idToHitCountMap;
    for (const art::Ptr<recob::Hit> &pHit : pHits)
    {
        const G4ID g4ID(TruthMatchUtils::TrueParticleID(pHit, rollupUnsavedIDs));
        auto [iterator, inserted] = idToHitCountMap.try_emplace(g4ID, 1);
        if (!(inserted))
            iterator->second++;
    }

    if (idToHitCountMap.empty())
        return kNoG4ID;

    std::map<unsigned int, std::vector<G4ID> > hitCountToIDMap;
    for (auto const& [g4ID, hitCount] : idToHitCountMap)
    {
        auto [iterator, inserted] = hitCountToIDMap.try_emplace(hitCount, std::vector<G4ID>{g4ID});
        if (!inserted)
            iterator->second.emplace_back(g4ID);
    }

    if (hitCountToIDMap.empty())
    {
     throw art::Exception(art::errors::LogicError)
        << "TruthMatchUtils::TrueParticleIDFromTotalRecoHits - Did not fill the hit count to g4 ID vector map"
        << "(map size == " << hitCountToIDMap.size() << ")."  << "  The G4 ID to hit count map size is "
        << idToHitCountMap.size() << ".  The hit count to G4 ID vector map should not be empty in this case."
        << "  Something has gone wrong.";
    }

    std::map<unsigned int, std::vector<G4ID> >::const_reverse_iterator lastElementIt(hitCountToIDMap.rbegin());
    unsigned int nMaxContributingIDs(lastElementIt->second.size());

    if (0 == nMaxContributingIDs)
    {
     throw art::Exception(art::errors::LogicError)
        << "TruthMatchUtils::TrueParticleIDFromTotalRecoHits - Counted a max number of contributing hits ("
        << lastElementIt->first << " hits) but did not find any G4 IDs.  Something has gone wrong.";
    }
    else if (1 < nMaxContributingIDs)
    {
        mf::LogInfo("TruthMatchUtils::TrueParticleIDFromTotalRecoHits") << "There are " << nMaxContributingIDs
            << " particles which tie for highest number of contributing hits (" << lastElementIt->first
            <<" hits).  Using TruthMatchUtils::TrueParticleIDFromTotalTrueEnergy instead." << std::endl;
        return TruthMatchUtils::TrueParticleIDFromTotalTrueEnergy(pHits,rollupUnsavedIDs);
    }

    return lastElementIt->second.front();
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TruthMatchUtils::FillG4IDToEnergyDepositMap(IDToEDepositMap &idToEDepMap, const art::Ptr<recob::Hit> &pHit,
    const bool rollupUnsavedIDs)
{
    const art::ServiceHandle<cheat::BackTrackerService> btServ;
    const std::vector<sim::TrackIDE> trackIDEs(btServ->HitToTrackIDEs(pHit));
    if (trackIDEs.empty())
        return;

    for (const sim::TrackIDE &trackIDE : trackIDEs) {
        const G4ID g4ID(static_cast<G4ID>(rollupUnsavedIDs ? std::abs(trackIDE.trackID) : trackIDE.trackID));
        const EDeposit eDep(static_cast<EDeposit>(trackIDE.energy));
        auto [iterator, inserted] = idToEDepMap.try_emplace(g4ID, eDep);
        if (!inserted)
            iterator->second += eDep;
    }

    if (idToEDepMap.empty())
    {
     throw art::Exception(art::errors::LogicError)
        << "TruthMatchUtils::FillG4IDToEnergyDepositMap did not fill the IDToEDepositMap map (map size == " << idToEDepMap.size() << ")."
        << "  The sim::TrackIDE vector size is " << trackIDEs.size() << ".  The IDToEDepositMap should not be empty when the"
        << " sim::TrackIDE vector is also not empty.  Something has gone wrong.";
    }

    return;
}
