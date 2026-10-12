#include "RetroAchievementsManager.h"

#include "soh/ShipInit.hpp"

namespace ShipwrightRA {
namespace {

void InitializeRetroAchievements() {
    RetroAchievementsManager::Instance().Initialize();
}

static RegisterShipInitFunc registerRetroAchievements(InitializeRetroAchievements);

} // namespace
} // namespace ShipwrightRA
