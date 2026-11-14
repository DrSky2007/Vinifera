/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Implements the attack-move order.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "attackmove.h"

#include "cell.h"
#include "display.h"
#include "event.h"
#include "eventext.h"
#include "extension.h"
#include "extension_globals.h"
#include "foot.h"
#include "footext.h"
#include "house.h"
#include "mouse.h"
#include "options.h"
#include "tacticalext.h"
#include "target.h"
#include "techno.h"
#include "tibsun_defines.h"
#include "tibsun_globals.h"
#include "tspp.h"
#include "vinifera_defines.h"
#include "weapontype.h"
#include "wwkeyboard.h"

#include <algorithm>
#include <cstring>


/**
 *  Is the local player currently picking an attack-move destination?
 */
bool AttackMoveClass::IsModeActive = false;


/**
 *  A fake class for implementing new member functions which allow
 *  access to the "this" pointer of the intended class.
 *
 *  @note: This must not contain a constructor or destructor!
 *  @note: All functions must be prefixed with "_" to prevent accidental virtualization.
 */
class DisplayClassAttackMoveExt final : public DisplayClass
{
public:
    /**
     *  Gives the order of the specified action to all currently selected objects.
     *  This is what DisplayClass::Mouse_Left_Release calls for all regular orders
     *  (it also handles group moves, waypoints and voice responses).
     */
    void _Click_Selected_Objects(ObjectClass* object, Cell cell, ActionType action);
};

DEFINE_IMPLEMENTATION(void DisplayClassAttackMoveExt::_Click_Selected_Objects(ObjectClass*, Cell, ActionType), 0x0047B210);


/**
 *  Is the local player currently picking an attack-move destination?
 *
 *  @author: Vinifera contributors
 */
bool AttackMoveClass::Is_Mode_Active()
{
    return IsModeActive;
}


/**
 *  Turns the attack-move destination picking mode on or off.
 *
 *  @param control  0 = off, 1 = on, -1 = toggle.
 *
 *  @author: Vinifera contributors
 */
void AttackMoveClass::Mode_Control(int control)
{
    bool mode = IsModeActive;

    switch (control) {
        case 0:
            mode = false;
            break;

        case -1:
            mode = !IsModeActive;
            break;

        case 1:
            mode = true;
            break;
    }

    /**
     *  The mode is only available if any of the selected objects can attack-move.
     */
    if (mode && (Map.PendingObject != nullptr || !Is_Selection_Able())) {
        mode = false;
    }

    if (mode == IsModeActive) {
        return;
    }

    if (mode) {

        /**
         *  Leave all other special interface modes.
         */
        Map.IsRepairMode = false;
        Map.IsSellMode = false;
        Map.IsPowerMode = false;
        if (Map.IsWaypointMode) {
            Map.Waypoint_Mode_Control(0);
        }
        Map.TargettingType = SUPER_NONE;
        if (TacticalMapExtension != nullptr) {
            TacticalMapExtension->IsBeaconPlacementMode = false;
        }

        IsModeActive = true;

    } else {
        IsModeActive = false;
    }

    Map.Set_Default_Mouse(MOUSE_NORMAL, false);
}


/**
 *  Turns off the attack-move destination picking mode without any other side effects.
 *  Used when another special interface mode is entered.
 *
 *  @author: Vinifera contributors
 */
void AttackMoveClass::Clear_Mode()
{
    IsModeActive = false;
}


/**
 *  Can any of the currently selected objects perform an attack-move order?
 *
 *  @author: Vinifera contributors
 */
bool AttackMoveClass::Is_Selection_Able()
{
    for (int i = 0; i < CurrentObjects.Count(); ++i) {
        ObjectClass* object = CurrentObjects[i];
        if (object != nullptr && object->Is_Techno() && object->Can_Player_Move()) {
            if (Can_Attack_Move(static_cast<TechnoClass*>(object))) {
                return true;
            }
        }
    }

    return false;
}


/**
 *  Gives the attack-move order to the currently selected objects.
 *
 *  All selected objects are given a regular move order first (exactly like a
 *  regular move click, so group moves and voice responses work as usual). Then
 *  the move order events of all objects able to attack-move are turned into
 *  attack-move events, which perform the same move order and additionally flag
 *  the object as attack-moving. This keeps the number of events the same as for
 *  a regular move order.
 *
 *  @author: Vinifera contributors
 */
void AttackMoveClass::Issue_Order(const Cell& cell)
{
    /**
     *  The mode is only kept active if the player holds the selection
     *  modifier key (Shift by default), to give multiple orders.
     */
    if (!Keyboard->Down(Options.KeySelect1) && !Keyboard->Down(Options.KeySelect2)) {
        Mode_Control(0);
    }

    if (!Is_Selection_Able()) {
        Mode_Control(0);
        return;
    }

    int first_event = OutList.Count;

    /**
     *  Regular move order for the whole selection.
     */
    reinterpret_cast<DisplayClassAttackMoveExt*>(&Map)->_Click_Selected_Objects(nullptr, cell, ACTION_MOVE);
    TechnoClass::Reset_Action_Line_Timer();

    /**
     *  Turn the move order events of the objects able to attack-move into attack-move events.
     */
    for (int i = first_event; i < OutList.Count; ++i) {
        EventClass& event = OutList[i];

        if (event.Type != EVENT_MEGAMISSION || event.Data.MegaMission.Mission != MISSION_MOVE) {
            continue;
        }

        if (event.Data.MegaMission.Destination.Kind() != RTTI_CELL) {
            continue;
        }

        TechnoClass* techno = event.Data.MegaMission.Whom.As_Techno();
        if (!Can_Attack_Move(techno)) {
            continue;
        }

        xTargetClass whom = event.Data.MegaMission.Whom;
        xTargetClass where = event.Data.MegaMission.Destination;

        EventClassExt& attack_move = reinterpret_cast<EventClassExt&>(event);
        attack_move.Type = static_cast<EventType>(EXT_EVENT_ATTACK_MOVE);
        std::memset(&attack_move.Data, 0, sizeof(attack_move.Data));
        attack_move.Data.NavCom.Whom = whom;
        attack_move.Data.NavCom.Where = where;
    }
}


/**
 *  Can this object perform attack-move orders at all?
 *
 *  @note: This is used by the game logic, so it must not depend on the local player!
 *
 *  @author: Vinifera contributors
 */
bool AttackMoveClass::Can_Attack_Move(const TechnoClass* techno)
{
    if (techno == nullptr || !techno->Is_Foot()) {
        return false;
    }

    /**
     *  Aircraft use their own movement logic, they just perform a regular move.
     */
    if (techno->RTTI == RTTI_AIRCRAFT) {
        return false;
    }

    return techno->Is_Weapon_Equipped();
}


/**
 *  Starts an attack-move for this object. This is called when the attack-move
 *  event is executed, right after the move order of the event was performed.
 *
 *  @author: Vinifera contributors
 */
void AttackMoveClass::Start(FootClass* foot)
{
    /**
     *  The move order failed, nothing to do.
     */
    if (foot->NavCom == nullptr) {
        return;
    }

    FootClassExtension* ext = Extension::Fetch(foot);

    ext->IsAttackMoving = true;
    ext->IsAttackMoveEngaged = false;
    ext->AttackMoveCell = Cell(foot->NavCom->Center_Coord());
    ext->AttackMoveEngageCoord = COORD_NONE;
}


/**
 *  Cancels any attack-move this object might be performing. This is called
 *  whenever the object receives another order.
 *
 *  @author: Vinifera contributors
 */
void AttackMoveClass::Cancel(TechnoClass* techno)
{
    if (techno == nullptr || !techno->Is_Foot()) {
        return;
    }

    FootClassExtension* ext = Extension::Fetch(static_cast<FootClass*>(techno));

    ext->IsAttackMoving = false;
    ext->IsAttackMoveEngaged = false;
}


/**
 *  Called from FootClass::Mission_Move. Looks for enemies in weapon range of an
 *  attack-moving object and makes the object stop and fight if it finds one.
 *
 *  @return  True if the object is attack-moving (the original passive
 *           target acquisition must then be skipped).
 *
 *  @author: Vinifera contributors
 */
bool AttackMoveClass::Mission_Move_AI(FootClass* foot)
{
    FootClassExtension* ext = Extension::Fetch(foot);

    if (!ext->IsAttackMoving) {
        return false;
    }

    /**
     *  We have already switched to the attack mission, it just did not start yet.
     */
    if (ext->IsAttackMoveEngaged) {
        return true;
    }

    /**
     *  The object is no longer heading to the attack-move destination (for example,
     *  it was given another order by a script), so forget about the attack-move.
     */
    if (foot->NavCom == nullptr || Cell(foot->NavCom->Center_Coord()) != ext->AttackMoveCell) {
        ext->IsAttackMoving = false;
        return false;
    }

    /**
     *  Look for an enemy within weapon range.
     */
    if (foot->TarCom == nullptr) {
        Coord coord = foot->Center_Coord();
        foot->Target_Something_Nearby(coord, THREAT_RANGE);
    }

    /**
     *  Found something, stop and fight it.
     */
    if (foot->TarCom != nullptr) {
        ext->IsAttackMoveEngaged = true;
        ext->AttackMoveEngageCoord = foot->Center_Coord();

        foot->Assign_Destination(nullptr);
        foot->Assign_Mission(MISSION_ATTACK);
    }

    return true;
}


/**
 *  Called from FootClass::Mission_Attack. Continues the attack-move once the
 *  target has been defeated, or once it lured the object too far away.
 *
 *  @return  True if the original attack mission logic must be skipped.
 *
 *  @author: Vinifera contributors
 */
bool AttackMoveClass::Mission_Attack_AI(FootClass* foot)
{
    FootClassExtension* ext = Extension::Fetch(foot);

    if (!ext->IsAttackMoving) {
        return false;
    }

    if (!ext->IsAttackMoveEngaged) {

        /**
         *  We are about to continue moving (the move mission just did not start yet),
         *  don't let the attack mission put the object into idle mode in the meantime.
         */
        if (foot->MissionQueue == MISSION_MOVE) {
            return true;
        }

        return false;
    }

    if (foot->TarCom != nullptr) {

        /**
         *  Keep fighting, unless the target lured us too far away from our path.
         */
        int leash = ext->Get_Sight_Range() * CELL_LEPTON_W;

        WeaponTypeClass* weapon = foot->Get_Primary_Weapon();
        if (weapon != nullptr) {
            leash = std::max<int>(leash, weapon->Range);
        }

        leash += CELL_LEPTON_W * 2;

        if (foot->Distance(ext->AttackMoveEngageCoord) <= leash) {
            return false;
        }
    }

    /**
     *  Target defeated (or out of reach), continue to the destination.
     */
    Resume(foot);

    return true;
}


/**
 *  Called from FootClass::Mission_Guard when the object has nothing to do.
 *  Either continues an attack-move that was interrupted, or finishes the
 *  attack-move once the object has stopped moving.
 *
 *  @author: Vinifera contributors
 */
void AttackMoveClass::Mission_Guard_AI(FootClass* foot)
{
    FootClassExtension* ext = Extension::Fetch(foot);

    if (!ext->IsAttackMoving) {
        return;
    }

    /**
     *  Another mission is about to start (for example, the move mission of the
     *  attack-move order itself), so wait for it.
     */
    if (foot->MissionQueue != MISSION_NONE) {
        return;
    }

    /**
     *  The object went idle while fighting, continue to the destination.
     */
    if (ext->IsAttackMoveEngaged) {
        Resume(foot);
        return;
    }

    /**
     *  The object has stopped moving (it arrived, or could not get any further),
     *  so the attack-move order is complete.
     */
    ext->IsAttackMoving = false;
}


/**
 *  Continues moving to the attack-move destination.
 *
 *  @author: Vinifera contributors
 */
void AttackMoveClass::Resume(FootClass* foot)
{
    FootClassExtension* ext = Extension::Fetch(foot);

    ext->IsAttackMoveEngaged = false;
    ext->AttackMoveEngageCoord = COORD_NONE;

    foot->Assign_Target(nullptr);
    foot->Assign_Destination(&Map[ext->AttackMoveCell]);
    foot->Assign_Mission(MISSION_MOVE);
}
