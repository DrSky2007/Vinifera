/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Implements the attack-move order.
 *
 *          Objects given an attack-move order move to the destination like
 *          with a normal move order, but stop to fight any enemy that comes
 *          into weapon range on the way. Once the enemy is defeated (or has
 *          left the area), they continue moving to their destination.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include "always.h"
#include "tibsun_defines.h"


class Cell;
class FootClass;
class ObjectClass;
class TechnoClass;


class AttackMoveClass
{
public:
    /**
     *  User interface (local player only, must never touch the game state).
     */
    static bool Is_Mode_Active();
    static void Mode_Control(int control);
    static void Clear_Mode();
    static bool Is_Selection_Able();
    static void Issue_Order(const Cell& cell);

    /**
     *  Game logic (executed in sync on all machines).
     */
    static bool Can_Attack_Move(const TechnoClass* techno);
    static void Start(FootClass* foot);
    static void Cancel(TechnoClass* techno);
    static bool Mission_Move_AI(FootClass* foot);
    static bool Mission_Attack_AI(FootClass* foot);
    static void Mission_Guard_AI(FootClass* foot);

private:
    static void Resume(FootClass* foot);

private:
    /**
     *  Is the local player currently picking an attack-move destination?
     */
    static bool IsModeActive;
};
