// Combat behavior selects attacks and movement in response to nearby creatures.
#include "cube/DefaultBlocks.h"
#include "cube/CombatBehavior.h"
#include "cube/Creature.h"
#include "cube/CreatureIds.h"
#include "cube/RaceIds.h"
#include "cube/SpriteIds.h"
#include "cube/World.h"
#include "cube/ServerUpdates.h"
#include <stdlib.h>

namespace cube {

namespace {
enum {
    AbilityIdleResetMs = 10000,
    PotionDrinkLockMs = 3000,
    PetFoodComboLimit = 5,
    TamingAngerDurationMs = 20000,
    SpecialSkillCooldownMs = 20000,
    GolemSmashHorizontalVelocityScale = 2,
    FlyingAttackDecisionDelayMs = 2000,
    DodgeRandomDivisor = 10,
};
enum DodgeDirection {
    DodgeForward = 0,
    DodgeBackward = 1,
    DodgeSidePositive = 2,
    DodgeSideNegative = 3,
    DodgeDirectionCount = 4
};
}

CombatBehavior::CombatBehavior(float distance)
    : m_specialSkillDelay(SpecialSkillCooldownMs),
      m_targetAcquisitionRadius(distance),
      m_pursuitTime(0),
      m_targetDelay(0)
{}

bool CombatBehavior::Update(Creature *creature, World *world, int elapsed, ServerUpdates *updates)
{
    Creature::EntityData &entity = creature->m_entityData;
    entity.binaryToggles.faceAim = false;
    if (entity.currentAbility != AbilitySit && entity.currentAbility != AbilityOfferPetFood &&
        entity.currentAbility != AbilitySleep && entity.currentAbility != AbilitySneak &&
        entity.timeSinceAbility > AbilityIdleResetMs)
        entity.currentAbility = AbilityNone;
    if (entity.rollTime != 0 || (entity.currentAbility == AbilityDrink && entity.timeSinceAbility < PotionDrinkLockMs))
        return true;

    if (entity.hp < creature->maxHP() * 0.25f && entity.timeSinceAbility > creature->attackDuration()) {
        std::vector<std::vector<ItemStack>> &tabs = creature->m_inventory.m_inventoryTabs;
        for (std::vector<std::vector<ItemStack>>::iterator tab = tabs.begin(); tab != tabs.end(); ++tab) {
            for (std::vector<ItemStack>::iterator stack = tab->begin(); stack != tab->end(); ++stack) {
                if (stack->quantity && stack->item.category == ItemCategoryConsumable &&
                    stack->item.id == ConsumableLifePotion) {
                    entity.currentAbility = AbilityDrink;
                    entity.timeSinceAbility = 0;
                    entity.actionItem = stack->item;
                    stack->removeOne();
                    float x = rand() * 4.0f / float(RAND_MAX) - 2.0f;
                    float y = rand() * 4.0f / float(RAND_MAX) - 2.0f;
                    entity.acceleration = plasma::Vector<3, float>(x, y, 0);
                    return true;
                }
            }
        }
    }

    // Pets inherit their owner's target priorities.
    Creature *target = world->creatureByID(creature->maxAggroTarget());
    if (entity.hostilityType == HostilityPet) {
        Creature *owner = world->creatureByID(entity.ownerId);
        if (owner) {
            target = 0;
            if (owner->m_entityData.currentAbility == AbilityRidePet)
                return false;
            float weight = 0;
            for (std::map<long long, float>::iterator it = owner->m_hitCounts.begin(); it != owner->m_hitCounts.end();
                 ++it) {
                if (it->second > weight && world->creatureByID(it->first)) {
                    target = world->creatureByID(it->first);
                    weight = it->second;
                }
            }
        }
    }
    if (!target && entity.hp < creature->maxHP() * 0.5f && entity.hostilityType == HostilityFriendlyNpc) {
        entity.currentAbility = AbilityDrink;
        entity.timeSinceAbility = 0;
        entity.actionItem.category = ItemCategoryConsumable;
        entity.actionItem.id = ConsumableGinsengSoup;
        entity.actionItem.level = (short)entity.level;
        entity.acceleration = plasma::Vector<3, float>(0, 0, 0);
        return true;
    }
    m_specialSkillDelay -= elapsed;
    if (m_specialSkillDelay < 0)
        m_specialSkillDelay = 0;
    bool tamingAnger = false;
    for (std::list<Creature::Buff>::iterator it = creature->m_buffs.begin(); it != creature->m_buffs.end(); ++it) {
        if (it->type == BuffTamingAnger) {
            tamingAnger = true;
            break;
        }
    }
    if (!target && !(entity.hostilityType == HostilityPet && entity.ownerId != 0) && m_targetDelay <= 0) {
        float nearestDistance = m_targetAcquisitionRadius * m_targetAcquisitionRadius;
        // Each other creature is considered in turn (with the fast creature updates only those in the
        // cells within the acquisition radius).
        auto consider = [&](Creature *other) {
            if (!other || other == creature || other->m_entityData.hp <= 0 ||
                other->m_entityData.hostilityType == HostilityPet)
                return;
            const Creature::EntityData &candidate = other->m_entityData;
            plasma::Vector<3, float> offset(candidate.position - entity.position);
            float distance = offset.squaredLength();
            if (other->m_stealth > 0) {
                float visibility = 1.0f - (float)pow(1.5, (float)(candidate.level - entity.level)) * other->m_stealth;
                if (visibility < 0.1f)
                    visibility = 0.1f;
                distance /= visibility * visibility;
            }
            if (!tamingAnger && distance < nearestDistance &&
                (world->areEnemies(creature, other) || (entity.hostilityType == HostilityPet && entity.ownerId == 0 &&
                                                        candidate.currentAbility == AbilityOfferPetFood &&
                                                        candidate.equipment.pet.category == ItemCategoryPetFood &&
                                                        candidate.equipment.pet.id == entity.race)) &&
                world->inRange(entity.position, candidate.position, true, 200.0f) &&
                (!creature->isPeaceful() || (candidate.currentAbility == AbilityOfferPetFood &&
                                             candidate.equipment.pet.category == ItemCategoryPetFood &&
                                             candidate.equipment.pet.id == entity.race))) {
                nearestDistance = distance;
                target = other;
            }
        };
        static std::vector<std::pair<long long, Creature *>> s_nearby;
        if (world->m_fastCreatureUpdates &&
            world->nearbyCreatures((float)entity.position[0], (float)entity.position[1], m_targetAcquisitionRadius,
                                   s_nearby)) {
            for (size_t i = 0; i < s_nearby.size(); i++)
                consider(s_nearby[i].second);
        } else {
            for (std::map<long long, Creature *>::iterator it = world->m_creatures.begin();
                 it != world->m_creatures.end(); ++it)
                consider(it->second);
        }
        if (target) {
            creature->m_aggro[target->m_id] = 1.0f;
            world->onAttack(target, creature, updates);
        }
    }
    entity.binaryToggles.fastMovement =
        target &&
        (entity.currentAbility == AbilityNone ||
         entity.timeSinceAbility > creature->holdTime() / 2 + creature->anticipationTime() + creature->strikeTime());
    m_targetDelay -= elapsed;
    if (m_targetDelay < 0)
        m_targetDelay = 0;
    if (!target) {
        m_pursuitTime = 0;
        return false;
    }

    Creature::EntityData &enemy = target->m_entityData;
    plasma::Vector<3, float> offset(enemy.position - entity.position);
    float range = entity.appearance.size[0] * 2.0f + enemy.appearance.size[0];
    bool inSight = world->inRange(entity.position, enemy.position, true, 200.0f);
    if (creature->isAttackingFromRange() && inSight &&
        ((entity.currentAbility != AbilityExplosion && entity.currentAbility != AbilityCyclone &&
          entity.currentAbility != AbilityLava) ||
         entity.timeSinceAbility >= creature->anticipationTime()))
        range = 50.0f;
    if (entity.race == RaceRockling || entity.race == RaceGolem || entity.race == RaceSnowGolem ||
        entity.race == RaceEmberGolem)
        range = 20.0f;
    if (entity.currentAbility == AbilityFireRay || entity.currentAbility == AbilitySupportRay ||
        entity.currentAbility == AbilityThrowRock)
        range = 50.0f;
    if (entity.currentAbility == AbilityShellSpin)
        range = entity.appearance.size[0] + enemy.appearance.size[0];
    int specialSkill = 0;
    if (m_specialSkillDelay == 0 && !creature->m_specialSkills.empty()) {
        specialSkill = creature->m_specialSkills[rand() % creature->m_specialSkills.size()];
        if (specialSkill == Ability93 || specialSkill == AbilityQuake || specialSkill == AbilityHeadbutt ||
            specialSkill == AbilityHornCharge)
            range = 20.0f;
        if (specialSkill == AbilityCyclone || specialSkill == AbilityLava)
            range = entity.appearance.size[0] * 2.0f + enemy.appearance.size[0];
    }
    if (!inSight)
        range = 1.0f;

    // Stop to feed, aim or attack once the target is visible and in reach.
    if (!(offset.squaredLength() > range * range) && inSight) {
        m_pursuitTime = 0;
        creature->clearPath();
        creature->unused13e0 = 0;
        entity.acceleration = plasma::Vector<3, float>(0, 0, 0);
        if (!tamingAnger && enemy.currentAbility == AbilityOfferPetFood &&
            enemy.equipment.pet.category == ItemCategoryPetFood && enemy.equipment.pet.id == entity.race &&
            !(entity.hostilityType == HostilityPet && entity.ownerId != 0) && offset.squaredLength() <= range * range) {
            creature->m_opponentId = target->m_id;
            if (entity.timeSinceAbility > creature->attackDuration()) {
                if (entity.currentAbility != AbilityPetFood)
                    entity.hitCombo = 0;
                if (entity.hitCombo < PetFoodComboLimit) {
                    entity.currentAbility = AbilityPetFood;
                    entity.timeSinceAbility = 0;
                } else {
                    bool found = false;
                    Creature::Buff buff;
                    for (std::list<Creature::Buff>::iterator it = creature->m_buffs.begin();
                         it != creature->m_buffs.end(); ++it) {
                        if (it->type == BuffTameness) {
                            buff = *it;
                            found = true;
                        }
                    }
                    if (found) {
                        buff.timeRemaining = 0;
                        buff.strength = 0;
                        world->applyBuff(creature, buff, updates);
                    }
                    buff.type = BuffTamingAnger;
                    buff.timeRemaining = TamingAngerDurationMs;
                    buff.strength = 0;
                    buff.sourceId = target->m_id;
                    world->applyBuff(creature, buff, updates);
                }
            }
            return true;
        }
        plasma::Vector<3, float> aim(enemy.position - entity.position);
        entity.binaryToggles.faceAim = true;
        float distance = aim.squaredLength();
        if (specialSkill) {
            m_specialSkillDelay = SpecialSkillCooldownMs;
            entity.currentAbility = (unsigned char)specialSkill;
            entity.timeSinceAbility = 0;
        } else if (entity.race == RaceMammoth) {
            if (entity.timeSinceAbility > creature->attackDuration()) {
                std::vector<int> abilities;
                abilities.push_back(AbilityHornCharge);
                abilities.push_back(AbilityStomp);
                abilities.push_back(AbilityScream);
                entity.currentAbility = (unsigned char)abilities[rand() % abilities.size()];
                entity.timeSinceAbility = 0;
            }
        } else if (entity.race == RaceRuneGiant) {
            if (entity.timeSinceAbility > creature->attackDuration()) {
                std::vector<int> abilities;
                abilities.push_back(AbilityLeftStab);
                abilities.push_back(AbilityRightStab);
                abilities.push_back(AbilityKick);
                if (entity.appearance.headModel == SpriteRuneGiantHeadLaser)
                    abilities.push_back(AbilityFireRay);
                else
                    abilities.push_back(AbilityCyclone);
                entity.currentAbility = (unsigned char)abilities[rand() % abilities.size()];
                entity.timeSinceAbility = 0;
            }
            if (entity.currentAbility == AbilityFireRay)
                aim = plasma::Vector<3, float>(enemy.position - creature->sightPosition());
        } else if (entity.race == RaceRockling || entity.race == RaceGolem || entity.race == RaceSnowGolem ||
                   entity.race == RaceEmberGolem) {
            if (entity.timeSinceAbility > creature->attackDuration()) {
                std::vector<int> abilities;
                abilities.push_back(AbilityThrowRock);
                abilities.push_back(AbilitySmash);
                abilities.push_back(AbilityDashForward);
                entity.currentAbility = (unsigned char)abilities[rand() % abilities.size()];
                entity.timeSinceAbility = 0;
                if (entity.currentAbility == AbilitySmash) {
                    entity.velocity = plasma::Vector<3, float>((enemy.position - entity.position) *
                                                               Fixed(GolemSmashHorizontalVelocityScale));
                    entity.velocity[2] = 15.0f;
                }
            }
        } else if (entity.race == RaceSaurian) {
            if (entity.timeSinceAbility > creature->attackDuration()) {
                std::vector<int> abilities;
                if (entity.appearance.headModel == SpriteMonsterHeadReptileHorn1 ||
                    entity.appearance.headModel == SpriteMonsterHeadReptileHorn2 ||
                    entity.appearance.headModel == SpriteMonsterHeadReptileHorn3)
                    abilities.push_back(AbilityHornCharge);
                else
                    abilities.push_back(AbilityHeadbutt);
                if (entity.appearance.tailModel == SpriteMonsterTailReptileSpike1 ||
                    entity.appearance.tailModel == SpriteMonsterTailReptileSpike2)
                    abilities.push_back(AbilityTailStrike);
                if (entity.appearance.handsModel == SpriteMonsterFootReptileClaw1 ||
                    entity.appearance.handsModel == SpriteMonsterFootReptileClaw2)
                    abilities.push_back(AbilityBite);
                else
                    abilities.push_back(AbilityStomp);
                if (entity.appearance.chestModel == SpriteMonsterBodyReptileSpiked1 ||
                    entity.appearance.chestModel == SpriteMonsterBodyReptileSpiked2) {
                    abilities.push_back(AbilityDashLeft);
                    abilities.push_back(AbilityDashRight);
                }
                if (entity.appearance.chestModel == SpriteMonsterBodyReptileCrest1 ||
                    entity.appearance.chestModel == SpriteMonsterBodyReptileCrest2 ||
                    entity.appearance.chestModel == SpriteMonsterBodyReptileCrest3 ||
                    entity.appearance.chestModel == SpriteMonsterBodyReptileCrest4 ||
                    entity.appearance.chestModel == SpriteMonsterBodyReptileCrest5)
                    abilities.push_back(AbilityDashForward);
                if (entity.currentAbility != AbilityShellBlock && entity.currentAbility != AbilityShellSpin &&
                    entity.appearance.chestModel == SpriteMonsterBodyReptileShell && rand() % 2)
                    abilities.push_back(AbilityShellBlock);
                if (entity.currentAbility != AbilityShellBlock && entity.currentAbility != AbilityShellSpin &&
                    entity.appearance.chestModel == SpriteMonsterBodyReptileShellSpiked && rand() % 2)
                    abilities.push_back(AbilityShellSpin);
                if (entity.currentAbility != AbilityScream)
                    abilities.push_back(AbilityScream);
                if (!abilities.empty()) {
                    entity.currentAbility = (unsigned char)abilities[rand() % abilities.size()];
                    entity.timeSinceAbility = 0;
                }
            }
        } else if (creature->isCaster()) {
            if (distance < 3600.0f) {
                if (entity.mp > 0.5f && rand() % 2)
                    creature->performSpecialAttack();
                else
                    creature->performAttack();
                if (entity.currentAbility == AbilityFireSwirl || entity.currentAbility == AbilityFireVortex ||
                    entity.currentAbility == AbilityWaterSwirl || entity.currentAbility == AbilityWaterVortex) {
                    if (entity.timeSinceAbility - elapsed <= creature->anticipationTime())
                        creature->m_abilityTarget = target->m_drawnPosition;
                    else
                        aim = plasma::Vector<3, float>(creature->m_abilityTarget - entity.position);
                }
                if (entity.currentAbility == AbilitySupportRay && distance > 25.0f) {
                    float angle = entity.timeSinceAbility * 0.005f;
                    aim[0] += (float)cos(angle) * 1.5f;
                    aim[1] += (float)sin(angle) * 1.5f;
                }
            }
        } else if (entity.race == RaceDragon) {
            if (distance < 64.0f) {
                entity.attackRotation = plasma::Vector<3, float>(enemy.position - entity.position);
                entity.binaryToggles.faceAim = true;
                if (entity.timeSinceAbility > FlyingAttackDecisionDelayMs) {
                    if (rand() % 2 && !(entity.position[2] - enemy.position[2] > 8.0f)) {
                        entity.velocity[2] = 30.0f;
                    } else {
                        entity.timeSinceAbility = 0;
                        entity.currentAbility = AbilityForwardRoll;
                        entity.velocity = plasma::Vector<3, float>(enemy.position - entity.position);
                        if (entity.velocity.squaredLength() > 0) {
                            entity.velocity.normalize();
                            entity.velocity *= 10.0f;
                        }
                    }
                }
            } else if (distance < 3600.0f && entity.timeSinceAbility > creature->attackDuration()) {
                entity.timeSinceAbility = 0;
                entity.currentAbility = AbilityFireball;
                entity.attackRotation = plasma::Vector<3, float>(
                    enemy.position - entity.position +
                    plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0, 0, enemy.appearance.size[2] * 0.4f)));
            }
        } else if ((entity.appearance.appearanceFlags & (1 << AppearanceFlagFlying)) &&
                   (entity.appearance.appearanceFlags & (1 << AppearanceFlagBiteAttack))) {
            entity.binaryToggles.faceAim = false;
            if (distance < 64.0f) {
                entity.attackRotation = plasma::Vector<3, float>(enemy.position - entity.position);
                entity.binaryToggles.faceAim = true;
                if (entity.timeSinceAbility > FlyingAttackDecisionDelayMs) {
                    if (rand() % 2 && !(entity.position[2] - enemy.position[2] > 2.0f)) {
                        entity.velocity[2] = 5.0f;
                    } else {
                        entity.timeSinceAbility = 0;
                        entity.currentAbility = AbilityForwardRoll;
                        entity.velocity = plasma::Vector<3, float>(enemy.position - entity.position);
                        if (entity.velocity.squaredLength() > 0) {
                            entity.velocity.normalize();
                            entity.velocity *= 10.0f;
                        }
                    }
                }
            }
        } else if (entity.race == RaceSnoutBeetle || entity.equipment.weaponRight.id == WeaponBow ||
                   entity.equipment.weaponRight.id == WeaponCrossbow ||
                   entity.equipment.weaponRight.id == WeaponBoomerang) {
            if (distance < 3600.0f) {
                if (entity.currentAbility != AbilityChargedShot && entity.currentAbility != AbilitySalvo &&
                    entity.currentAbility != AbilityChargeBoomerang && entity.rollTime == 0 &&
                    rand() % DodgeRandomDivisor == 0 && !creature->isBoss() && entity.appearance.size[0] < 2.0f &&
                    entity.timeSinceAbility >= creature->attackDuration() &&
                    (entity.flags & ((1 << CreatureFlagOnGround) | (1 << CreatureFlagInWater)))) {
                    plasma::Vector<3, float> direction(enemy.position - entity.position);
                    direction[2] = 0;
                    if (direction.squaredLength() > 0) {
                        direction.normalize();
                        switch (rand() % DodgeDirectionCount) {
                        case DodgeForward:
                            entity.velocity = direction * 20.0f;
                            break;
                        case DodgeBackward:
                            entity.velocity = direction * -20.0f;
                            break;
                        case DodgeSidePositive:
                            entity.velocity = direction.cross(plasma::Vector<3, float>(0, 0, 1)) * 20.0f;
                            break;
                        case DodgeSideNegative:
                            entity.velocity = direction.cross(plasma::Vector<3, float>(0, 0, 1)) * -20.0f;
                            break;
                        }
                        entity.velocity[2] += 5.0f;
                        entity.rollTime = RollDurationMs;
                    }
                } else if (entity.mp < 0.8 && entity.currentAbility != AbilityChargedShot &&
                           entity.currentAbility != AbilitySalvo && entity.currentAbility != AbilityChargeBoomerang) {
                    creature->performAttack();
                } else if (entity.rollTime == 0 && entity.timeSinceAbility > creature->attackDuration()) {
                    if (entity.equipment.weaponRight.category && entity.equipment.weaponRight.id == WeaponCrossbow) {
                        if (entity.currentAbility == AbilityChargedShot && entity.charge >= entity.mp) {
                            entity.currentAbility = AbilityShoot;
                            entity.timeSinceAbility = 0;
                        } else {
                            if (entity.currentAbility != AbilityChargedShot)
                                entity.timeSinceAbility = 0;
                            entity.currentAbility = AbilityChargedShot;
                        }
                    } else if (entity.equipment.weaponRight.category &&
                               entity.equipment.weaponRight.id == WeaponBoomerang) {
                        if (entity.currentAbility == AbilityChargeBoomerang && entity.charge >= entity.mp) {
                            entity.currentAbility = AbilityThrowBoomerang;
                            entity.timeSinceAbility = 0;
                        } else {
                            if (entity.currentAbility != AbilityChargeBoomerang)
                                entity.timeSinceAbility = 0;
                            entity.currentAbility = AbilityChargeBoomerang;
                        }
                    } else if ((entity.equipment.weaponRight.category &&
                                entity.equipment.weaponRight.id == WeaponBow) ||
                               entity.race == RaceSnoutBeetle) {
                        if (entity.currentAbility == AbilitySalvo && entity.charge >= entity.mp) {
                            entity.currentAbility = AbilitySalvoRelease;
                            entity.timeSinceAbility = 0;
                        } else {
                            if (entity.currentAbility != AbilitySalvo)
                                entity.timeSinceAbility = 0;
                            entity.currentAbility = AbilitySalvo;
                        }
                    }
                }
            }
        } else if (entity.appearance.appearanceFlags & (1 << AppearanceFlagBiteAttack)) {
            if (entity.timeSinceAbility > creature->attackDuration()) {
                entity.currentAbility = entity.race == RaceTurtle && creature->canUseSkill(AbilityShellSpin)
                                            ? AbilityShellSpin
                                            : AbilityBite;
                entity.timeSinceAbility = 0;
            }
        } else if (entity.appearance.appearanceFlags & (1 << AppearanceFlagHandAttacks)) {
            creature->performAttack();
        } else {
            float meleeRange = (entity.appearance.size[0] + enemy.appearance.size[0]) * 3.0f;
            if (distance < meleeRange * meleeRange) {
                if (entity.currentAbility != AbilityCrush && entity.currentAbility != AbilitySwirl &&
                    entity.currentAbility != AbilityBlock && entity.rollTime == 0 && rand() % DodgeRandomDivisor == 0 &&
                    !creature->isBoss() && entity.appearance.size[0] < 2.0f &&
                    entity.timeSinceAbility >= creature->attackDuration() &&
                    (entity.flags & ((1 << CreatureFlagOnGround) | (1 << CreatureFlagInWater)))) {
                    plasma::Vector<3, float> direction(enemy.position - entity.position);
                    direction[2] = 0;
                    if (direction.squaredLength() > 0) {
                        direction.normalize();
                        switch (rand() % DodgeDirectionCount) {
                        case DodgeForward:
                            entity.velocity = direction * 20.0f;
                            break;
                        case DodgeBackward:
                            entity.velocity = direction * -20.0f;
                            break;
                        case DodgeSidePositive:
                            entity.velocity = direction.cross(plasma::Vector<3, float>(0, 0, 1)) * 20.0f;
                            break;
                        case DodgeSideNegative:
                            entity.velocity = direction.cross(plasma::Vector<3, float>(0, 0, 1)) * -20.0f;
                            break;
                        }
                        entity.velocity[2] += 5.0f;
                        entity.rollTime = RollDurationMs;
                    }
                } else if (entity.mp < 0.8 && entity.currentAbility != AbilityCrush &&
                           entity.currentAbility != AbilitySwirl && entity.currentAbility != AbilityBlock) {
                    creature->performAttack();
                } else if (entity.rollTime == 0 && entity.timeSinceAbility > creature->attackDuration()) {
                    if (entity.equipment.weaponRight.id == WeaponLongsword) {
                        entity.currentAbility = AbilityPerforate;
                        entity.timeSinceAbility = 0;
                    } else if (entity.equipment.weaponRight.category && !entity.equipment.weaponLeft.category) {
                        if (entity.currentAbility == AbilityCrush && entity.charge >= entity.mp) {
                            if (entity.specialization == SpecializationGuardian) {
                                entity.currentAbility = AbilityGuardianCrushStrike;
                                entity.timeSinceAbility = 0;
                            } else {
                                entity.currentAbility = AbilityCrushStrike;
                                entity.timeSinceAbility = creature->anticipationTime();
                            }
                        } else if (creature->canUseSkill(AbilitySmash)) {
                            entity.velocity[2] += 20.0f;
                            entity.currentAbility = AbilitySmash;
                            entity.timeSinceAbility = 0;
                        } else {
                            if (entity.currentAbility != AbilityCrush)
                                entity.timeSinceAbility = 0;
                            entity.currentAbility = AbilityCrush;
                        }
                    } else if (entity.equipment.weaponLeft.id == WeaponShield) {
                        if (entity.currentAbility == AbilityBlock && entity.charge >= entity.mp) {
                            entity.currentAbility = AbilityBlockStrike;
                            entity.timeSinceAbility = 0;
                        } else {
                            if (entity.currentAbility != AbilityBlock)
                                entity.timeSinceAbility = 0;
                            entity.currentAbility = AbilityBlock;
                        }
                    } else if (entity.equipment.weaponLeft.id == WeaponDagger) {
                        entity.currentAbility = AbilityAmbush;
                        entity.timeSinceAbility = 0;
                    } else if (entity.equipment.weaponLeft.id == WeaponFist) {
                        entity.currentAbility = AbilityKick;
                        entity.timeSinceAbility = 0;
                    } else if (entity.equipment.weaponRight.category) {
                        if (entity.currentAbility == AbilitySwirl && entity.charge >= entity.mp) {
                            entity.currentAbility = AbilitySwirlStrike;
                            entity.timeSinceAbility = creature->anticipationTime();
                        } else if (creature->canUseSkill(AbilitySmash)) {
                            entity.velocity[2] += 20.0f;
                            entity.currentAbility = AbilitySmash;
                            entity.timeSinceAbility = 0;
                        } else {
                            if (entity.currentAbility != AbilitySwirl)
                                entity.timeSinceAbility = 0;
                            entity.currentAbility = AbilitySwirl;
                        }
                    } else if (creature->canUseSkill(AbilitySmash)) {
                        entity.velocity[2] += 20.0f;
                        entity.currentAbility = AbilitySmash;
                        entity.timeSinceAbility = 0;
                    } else {
                        creature->performSpecialAttack();
                    }
                }
            }
        }
        entity.attackRotation = aim;
        return true;
    }

    // Give up after a sustained failed pursuit, then delay target acquisition.
    m_pursuitTime += elapsed;
    if (m_pursuitTime > 20000) {
        creature->m_aggro.clear();
        creature->clearPath();
        m_targetDelay = 3000;
        m_pursuitTime = 0;
        return false;
    }
    if (entity.currentAbility == AbilityFireRay || entity.currentAbility == AbilitySupportRay)
        entity.currentAbility = AbilityNone;
    if (!(entity.flags & (1 << CreatureFlagInWater)))
        offset[2] = 0;
    if (offset.squaredLength() > 0)
        offset.normalize();
    float acceleration = 80.0f;
    if (range * range * 4.0f > offset.squaredLength())
        acceleration = 40.0f;
    entity.acceleration = offset * acceleration;
    if (inSight && world->canWalkStraight(creature, entity.position, enemy.position, range) &&
        !(entity.flags & (1 << CreatureFlagMovementBlocked))) {
        creature->clearPath();
        return true;
    }
    creature->m_aggro[target->m_id] *= 0.9f;
    if (creature->m_path.empty() &&
        (enemy.flags & ((1 << CreatureFlagOnGround) | (1 << CreatureFlagInWater) | (1 << CreatureFlagAtWall)))) {
        creature->m_pathGoal = enemy.position;
        creature->m_pathGoal[2] -= Fixed(enemy.appearance.size[2] * 0.5f - 0.1f);
        creature->m_pathStart = entity.position;
        creature->m_pathStart[2] -= Fixed(entity.appearance.size[2] * 0.5f - 0.1f);
        creature->m_pathRadius = entity.appearance.size[0] + enemy.appearance.size[0];
        world->beginPathSearch(creature);
    }
    if (!creature->m_pathNodes.empty() && creature->m_pathStart != creature->m_pathGoal) {
        for (int step = 0; step < PathSearchRoundsPerUpdate; ++step) {
            if ((int)creature->m_path.size() > PathSearchLengthLimit)
                break;
            if (!creature->m_path.empty()) {
                plasma::Vector<3, int> delta = creature->m_path.back() - toBlock(creature->m_pathGoal);
                if (entity.appearance.size[0] * entity.appearance.size[0] >= (float)delta.squaredLength())
                    break;
            }
            world->continuePathSearch(creature);
            world->finishPathSearch(creature);
        }
    }
    return true;
}

// A clone keeps the countdown and distance, but starts fresh runtime state.
Behavior *CombatBehavior::clone()
{
    return new CombatBehavior(*this);
}

}
