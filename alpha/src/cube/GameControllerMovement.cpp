// Moves the local player once per update step while no screen captures the keys.
#include "cube/DefaultBlocks.h"
#include "cube/GameController.h"
#include "cube/Creature.h"
#include "cube/CreatureIds.h"
#include "plasma/Engine.h"
#include "plasma/Node.h"

namespace cube {

// The player's movement (anonymous: this file's own): forces in blocks per second squared, speeds
// in blocks per second, times in milliseconds.
namespace {
enum {
    WalkForce = 80
};
enum {
    GlideForce = 8
};
enum {
    DodgeSpeed = 40
}; // horizontal
enum {
    DodgeRise = 5
};
enum {
    TargetRangeSquared = 10000
}; // 100 blocks
enum {
    JumpSpeed = 2
}; // above the horizontal speed
enum {
    MaximumRiseSpeed = 10
};
}

// Applies the movement keys (W, S, D, A) and the movement stick to the creature the player steers
// (its pet while it rides one): it climbs along a wall, dodges with the middle mouse button, swims
// along the camera or walks along the camera's heading. Then the lock-on key picks a target among
// the enemies on the screen, and space jumps. Any movement closes the inventory and the other
// screens.
void GameController::moveCharacter(int milliseconds)
{
    if (m_engine->getFocusWidget())
        return;
    bool cursorVisible = isCursorVisible();
    float force = WalkForce;
    if (m_player->isGliding())
        force = GlideForce;
    plasma::Matrix<float> yaw;
    yaw.identity();
    yaw.rotateZ(m_cameraRotation[2]);
    bool dodge = !cursorVisible && m_inputs[InputMouseMiddle] && m_player->m_stamina >= 0.25f;

    // The target is lost when it dies or is more than 100 blocks away.
    if (m_target) {
        if (m_target->m_entityData.hp <= 0.0f ||
            plasma::Vector<3, float>(m_target->m_entityData.position - m_player->m_entityData.position)
                    .squaredLength() > TargetRangeSquared)
            m_target = 0;
    }
    m_player->m_entityData.binaryToggles.faceAim = 0;
    Creature *creature = m_player;
    if (m_player->m_petId)
        creature = m_world.creatureByID(m_player->m_petId);
    Creature::EntityData &entity = creature->m_entityData;

    // Bit 0 of the toggles (lamp) at a wall: climbing.
    if (m_inputs[InputKeyW]) {
        if (entity.binaryToggles.climbing && (entity.flags & (1 << CreatureFlagAtWall))) {
            entity.acceleration[2] += force * 0.2f;
        } else if (dodge) {
            if ((entity.rollTime <= 0 || entity.currentAbility != AbilityNone) &&
                (entity.flags & ((1 << CreatureFlagOnGround) | (1 << CreatureFlagInWater)))) {
                entity.velocity = yaw.transformDirection(plasma::Vector<3, float>(0.0f, -1.0f, 0.0f)) * DodgeSpeed +
                                  plasma::Vector<3, float>(0.0f, 0.0f, DodgeRise);
                entity.rollTime = RollDurationMs;
                creature->m_stamina -= 0.25f;
                entity.currentAbility = AbilityNone;
            }
        } else if ((entity.flags & (1 << CreatureFlagInWater)) && entity.currentAbility != AbilitySailBoat) {
            entity.acceleration +=
                m_cameraMatrix.transformDirection(plasma::Vector<3, float>(0.0f, 0.0f, 1.0f)) * force;
        } else if (!entity.binaryToggles.gliding || (entity.flags & (1 << CreatureFlagOnGround)) ||
                   creature->m_stamina > 0.0f) {
            entity.acceleration += yaw.transformDirection(plasma::Vector<3, float>(0.0f, -1.0f, 0.0f)) * force;
        }
    }
    if (m_inputs[InputKeyS]) {
        if (entity.binaryToggles.climbing && (entity.flags & (1 << CreatureFlagAtWall))) {
            entity.acceleration[2] -= force * 0.2f;
        } else if (dodge) {
            if ((entity.rollTime <= 0 || entity.currentAbility != AbilityNone) &&
                (entity.flags & ((1 << CreatureFlagOnGround) | (1 << CreatureFlagInWater)))) {
                entity.velocity = yaw.transformDirection(plasma::Vector<3, float>(0.0f, -1.0f, 0.0f)) * -DodgeSpeed +
                                  plasma::Vector<3, float>(0.0f, 0.0f, DodgeRise);
                entity.rollTime = RollDurationMs;
                creature->m_stamina -= 0.25f;
                entity.currentAbility = AbilityNone;
            }
        } else if ((entity.flags & (1 << CreatureFlagInWater)) && entity.currentAbility != AbilitySailBoat) {
            entity.acceleration +=
                m_cameraMatrix.transformDirection(plasma::Vector<3, float>(0.0f, 0.0f, -1.0f)) * force;
        } else if (!entity.binaryToggles.gliding || (entity.flags & (1 << CreatureFlagOnGround)) ||
                   creature->m_stamina > 0.0f) {
            entity.acceleration =
                entity.acceleration - yaw.transformDirection(plasma::Vector<3, float>(0.0f, -1.0f, 0.0f)) * force;
        }
    }
    if (m_inputs[InputKeyD]) {
        if (entity.binaryToggles.climbing && (entity.flags & (1 << CreatureFlagAtWall))) {
            entity.acceleration =
                entity.acceleration -
                creature->m_wallNormal.cross(plasma::Vector<3, float>(0.0f, 0.0f, 1.0f)) * (force * 0.2f);
        } else if (dodge) {
            if ((entity.rollTime <= 0 || entity.currentAbility != AbilityNone) &&
                (entity.flags & ((1 << CreatureFlagOnGround) | (1 << CreatureFlagInWater)))) {
                entity.velocity = yaw.transformDirection(plasma::Vector<3, float>(1.0f, 0.0f, 0.0f)) * -DodgeSpeed +
                                  plasma::Vector<3, float>(0.0f, 0.0f, DodgeRise);
                entity.rollTime = RollDurationMs;
                creature->m_stamina -= 0.25f;
                entity.currentAbility = AbilityNone;
            }
        } else if ((entity.flags & (1 << CreatureFlagInWater)) && entity.currentAbility != AbilitySailBoat) {
            entity.acceleration +=
                m_cameraMatrix.transformDirection(plasma::Vector<3, float>(-1.0f, 0.0f, 0.0f)) * force;
        } else if (!entity.binaryToggles.gliding || (entity.flags & (1 << CreatureFlagOnGround)) ||
                   creature->m_stamina > 0.0f) {
            entity.acceleration =
                entity.acceleration - yaw.transformDirection(plasma::Vector<3, float>(1.0f, 0.0f, 0.0f)) * force;
        }
    }
    if (m_inputs[InputKeyA]) {
        if (entity.binaryToggles.climbing && (entity.flags & (1 << CreatureFlagAtWall))) {
            entity.acceleration +=
                creature->m_wallNormal.cross(plasma::Vector<3, float>(0.0f, 0.0f, 1.0f)) * (force * 0.2f);
        } else if (dodge) {
            if ((entity.rollTime <= 0 || entity.currentAbility != AbilityNone) &&
                (entity.flags & ((1 << CreatureFlagOnGround) | (1 << CreatureFlagInWater)))) {
                entity.velocity = yaw.transformDirection(plasma::Vector<3, float>(1.0f, 0.0f, 0.0f)) * DodgeSpeed +
                                  plasma::Vector<3, float>(0.0f, 0.0f, DodgeRise);
                entity.rollTime = RollDurationMs;
                creature->m_stamina -= 0.25f;
                entity.currentAbility = AbilityNone;
            }
        } else if ((entity.flags & (1 << CreatureFlagInWater)) && entity.currentAbility != AbilitySailBoat) {
            entity.acceleration +=
                m_cameraMatrix.transformDirection(plasma::Vector<3, float>(1.0f, 0.0f, 0.0f)) * force;
        } else if (!entity.binaryToggles.gliding || (entity.flags & (1 << CreatureFlagOnGround)) ||
                   creature->m_stamina > 0.0f) {
            entity.acceleration += yaw.transformDirection(plasma::Vector<3, float>(1.0f, 0.0f, 0.0f)) * force;
        }
    }
    // The stick's axes run from -32768 to 32767.
    if (m_moveStick[0] * m_moveStick[0] + m_moveStick[1] * m_moveStick[1] != 0) {
        if (entity.binaryToggles.climbing && (entity.flags & (1 << CreatureFlagAtWall))) {
            entity.acceleration +=
                creature->m_wallNormal.cross(plasma::Vector<3, float>(0.0f, 0.0f, -m_moveStick[0] / 32768.0f)) *
                (force * 0.2f);
            entity.acceleration[2] += force * 0.2f * m_moveStick[1] / 32768.0f;
        } else if (dodge) {
            if ((entity.rollTime <= 0 || entity.currentAbility != AbilityNone) &&
                (entity.flags & ((1 << CreatureFlagOnGround) | (1 << CreatureFlagInWater)))) {
                entity.velocity = yaw.transformDirection(plasma::Vector<3, float>(-m_moveStick[0] / 32768.0f,
                                                                                  -m_moveStick[1] / 32768.0f, 0.0f)) *
                                      DodgeSpeed +
                                  plasma::Vector<3, float>(0.0f, 0.0f, DodgeRise);
                entity.rollTime = RollDurationMs;
                creature->m_stamina -= 0.25f;
                entity.currentAbility = AbilityNone;
            }
        } else if (entity.flags & (1 << CreatureFlagInWater)) {
            entity.acceleration += m_cameraMatrix.transformDirection(plasma::Vector<3, float>(
                                       -m_moveStick[0] / 32768.0f, 0.0f, m_moveStick[1] / 32768.0f)) *
                                   force;
        } else if (!entity.binaryToggles.gliding || (entity.flags & (1 << CreatureFlagOnGround)) ||
                   creature->m_stamina > 0.0f) {
            entity.acceleration += yaw.transformDirection(plasma::Vector<3, float>(-m_moveStick[0] / 32768.0f,
                                                                                   -m_moveStick[1] / 32768.0f, 0.0f)) *
                                   force;
        }
    }

    // The lock-on key (an input WinMain never sets) picks the nearest enemy on the screen within 200
    // blocks of the camera, or, while one is locked, the next farther one; pressed on the only one, it
    // unlocks.
    static bool lockWasDown;
    if (m_inputs[InputTargetLock] && !lockWasDown) {
        float nearestDistance = TargetRangeSquared;
        Creature *nearest = 0;
        for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin();
             it != m_world.m_creatures.end(); ++it) {
            Creature *other = it->second;
            if (!other ||
                (other->m_entityData.hostilityType != HostilityEnemy &&
                 !other->m_entityData.binaryToggles.combatSideOverride) ||
                other->m_entityData.hp <= 0.0f)
                continue;
            plasma::Vector<3, float> point(other->m_entityData.position +
                                           plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, 0));
            plasma::Vector<3, float> view = m_viewMatrix * point;
            if (view[2] <= 0.0f)
                continue;
            plasma::Vector<3, float> screen = m_targetingProjectionMatrix * view;
            if (screen[0] < -1.0f || screen[0] > 1.0f || screen[1] < -1.0f || screen[1] > 1.0f)
                continue;
            float distance = (other->m_entityData.position - m_player->m_entityData.position).squaredLength();
            if (distance < nearestDistance &&
                m_world.inRange(other->m_entityData.position, m_cameraPosition, true, 200.0f)) {
                nearestDistance = distance;
                nearest = other;
            }
        }
        float targetDistance = 0.0f;
        float nextDistance = TargetRangeSquared;
        Creature *next = 0;
        if (m_target)
            targetDistance = (m_target->m_entityData.position - m_player->m_entityData.position).squaredLength();
        for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin();
             it != m_world.m_creatures.end(); ++it) {
            Creature *other = it->second;
            if (!other ||
                (other->m_entityData.hostilityType != HostilityEnemy &&
                 !other->m_entityData.binaryToggles.combatSideOverride) ||
                other->m_entityData.hp <= 0.0f)
                continue;
            plasma::Vector<3, float> point(other->m_entityData.position +
                                           plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, 0));
            plasma::Vector<3, float> view = m_viewMatrix * point;
            if (view[2] <= 0.0f)
                continue;
            plasma::Vector<3, float> screen = m_targetingProjectionMatrix * view;
            if (screen[0] < -1.0f || screen[0] > 1.0f || screen[1] < -1.0f || screen[1] > 1.0f)
                continue;
            float distance = (other->m_entityData.position - m_player->m_entityData.position).squaredLength();
            if (distance < nextDistance && distance >= targetDistance &&
                m_world.inRange(other->m_entityData.position, m_cameraPosition, true, 200.0f) && other != m_target) {
                nextDistance = distance;
                next = other;
            }
        }
        if (nearest == m_target)
            m_target = 0;
        else
            m_target = nearest;
        if (next)
            m_target = next;
    }
    lockWasDown = m_inputs[InputTargetLock];
    if (entity.currentAbility == Ability52)
        entity.currentAbility = AbilityNone;

    // Space: a jump from the ground (or a roll) within the first 200 ms of holding it, faster with
    // more horizontal speed; then 100 ms of rising while it is held. In water it lifts the creature.
    static bool jumpWasDown;
    static int jumpHeldTime;
    static int jumpRiseTime;
    if (m_inputs[InputKeySpace]) {
        jumpHeldTime += milliseconds;
        if (jumpHeldTime > 0) {
            if (jumpHeldTime < 200 && ((entity.flags & (1 << CreatureFlagOnGround)) || entity.rollTime != 0) &&
                entity.velocity[2] <= 0.0f) {
                entity.velocity[2] = std::min<float>(
                    sqrt(entity.velocity[0] * entity.velocity[0] + entity.velocity[1] * entity.velocity[1]) + JumpSpeed,
                    MaximumRiseSpeed);
                jumpRiseTime = 100;
            }
            if ((entity.flags & (1 << CreatureFlagInWater)) && entity.velocity[2] < MaximumRiseSpeed) {
                entity.velocity[2] += milliseconds * 0.001f;
                jumpRiseTime = 100;
            }
        }
    } else {
        jumpHeldTime = 0;
    }
    if (jumpRiseTime > 0 && m_inputs[InputKeySpace] && entity.velocity[2] < MaximumRiseSpeed) {
        entity.velocity[2] += milliseconds * 0.08f;
        if (entity.velocity[2] >= MaximumRiseSpeed)
            entity.velocity[2] = MaximumRiseSpeed;
    }
    jumpRiseTime = std::max(jumpRiseTime - milliseconds, 0);
    jumpWasDown = m_inputs[InputKeySpace];

    if (m_player->m_entityData.acceleration.squaredLength() > 0.0f) {
        m_inventoryNode->setVisible(false);
        m_craftingNode->setVisible(false);
        m_tradePanel->setVisible(false);
        m_voxelPanel->setVisible(false);
        m_enchantPanel->setVisible(false);
        m_adaptionPanel->setVisible(false);
        m_skillsOpen = false;
        m_gameMenuOpen = false;
        m_systemPanel->setVisible(false);
    }
}

}
