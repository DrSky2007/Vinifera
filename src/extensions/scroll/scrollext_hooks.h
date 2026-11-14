/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Contains the hooks for the extended ScrollClass.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include "point.h"
#include "tibsun_defines.h"


bool Is_Unit_Order_Action(ActionType action);
bool ScrollClassExtension_Take_Tactical_Right_Release();
bool ScrollClassExtension_Right_Click_Order(const Point2D& xy, bool on_tactical_map);

void ScrollClassExtension_Hooks();
