/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Contains the hooks for the extended ScrollClass.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "scrollext_hooks.h"

#include "attackmove.h"
#include "building.h"
#include "cell.h"
#include "extension_globals.h"
#include "hooker.h"
#include "house.h"
#include "map.h"
#include "mouse.h"
#include "objecttype.h"
#include "options.h"
#include "optionsext.h"
#include "particlesys.h"
#include "scroll.h"
#include "syringe.h"
#include "techno.h"
#include "tibsun_defines.h"
#include "tibsun_functions.h"
#include "tibsun_globals.h"
#include "tspp.h"
#include "vinifera_defines.h"
#include "windialog.h"
#include "wwkeyboard.h"


/**
 *  A fake class for implementing new member functions which allow
 *  access to the "this" pointer of the intended class.
 *
 *  @note: This must not contain a constructor or destructor!
 *  @note: All functions must be prefixed with "_" to prevent accidental virtualization.
 */
class ScrollClassExt final : public ScrollClass
{
public:
    /**
     *  Fetches what is under the mouse cursor at the tactical pixel position "xy"
     *  (the cell, coordinate and selectable object, plus whether the location is
     *  fogged or shrouded). Returns false if the position is not over the map.
     *
     *  This is the function ScrollClass::Message_Handler uses for all mouse clicks.
     */
    bool _Get_Mouse_Info(const Point2D& xy, Cell& cell, Coord& coord, ObjectClass*& object, bool& fogged, bool& shrouded);

    /**
     *  Fetches the action the currently selected objects would perform on the given cell/object.
     */
    ActionType _What_Action(const Cell& cell, ObjectClass* object, bool check_fog);
};

DEFINE_IMPLEMENTATION(bool ScrollClassExt::_Get_Mouse_Info(const Point2D&, Cell&, Coord&, ObjectClass*&, bool&, bool&), 0x005E8600);
DEFINE_IMPLEMENTATION(ActionType ScrollClassExt::_What_Action(const Cell&, ObjectClass*, bool), 0x005E8910);


/**
 *  Is this action an order for the currently selected objects (as opposed to a
 *  change of the selection or an action of an interface mode, like selling,
 *  waypoint placement or super weapon targeting)?
 *
 *  Used by the right-click orders control scheme.
 *
 *  @author: Vinifera contributors
 */
bool Is_Unit_Order_Action(ActionType action)
{
    switch (action) {
        case ACTION_MOVE:
        case ACTION_NOMOVE:
        case ACTION_ENTER:
        case ACTION_SELF:
        case ACTION_ATTACK:
        case ACTION_HARVEST:
        case ACTION_CAPTURE:
        case ACTION_SABOTAGE:
        case ACTION_TOTE:
        case ACTION_GUARD_AREA:
        case ACTION_HEAL:
        case ACTION_DAMAGE:
        case ACTION_GREPAIR:
        case ACTION_NO_DEPLOY:
        case ACTION_NO_ENTER:
        case ACTION_NO_GREPAIR:
        case ACTION_ENTER_TUNNEL:
        case ACTION_NO_ENTER_TUNNEL:
        case ACTION_RALLY_TO_POINT:
        case ACTION_ATTACK_SUPPORT:
            return true;

        default:
            return false;
    }
}


/**
 *  Set when ScrollClass::Message_Handler is about to call DisplayClass::Mouse_Right_Release
 *  for a right-click on the tactical map (with a valid position). The function is also called
 *  for right-clicks on the radar, but then without any position.
 */
static bool _IsTacticalRightRelease = false;


/**
 *  Fetches and resets whether the current DisplayClass::Mouse_Right_Release call
 *  is for a right-click on the tactical map.
 *
 *  @author: Vinifera contributors
 */
bool ScrollClassExtension_Take_Tactical_Right_Release()
{
    bool value = _IsTacticalRightRelease;
    _IsTacticalRightRelease = false;
    return value;
}


/**
 *  Right-click orders control scheme: issues the order for the currently
 *  selected objects at the tactical pixel position "xy", exactly like the
 *  original left mouse button release handler does.
 *
 *  Returns true if the click was consumed (the selection must then be kept).
 *  Returns false if the classic behaviour (unselect all) should be performed.
 *
 *  @author: Vinifera contributors
 */
bool ScrollClassExtension_Right_Click_Order(const Point2D& xy, bool on_tactical_map)
{
    if (!OptionsExtension->IsRightClickOrders) {
        return false;
    }

    /**
     *  Nothing is selected, so there is nobody to give the order to.
     */
    if (CurrentObjects.Count() <= 0) {
        return false;
    }

    /**
     *  The click did not happen on the tactical map (for example, it was on the radar),
     *  so there is no location to give the order to. Keep the selection.
     */
    if (!on_tactical_map) {
        return true;
    }

    /**
     *  The button was released outside of the tactical view (for example, over the sidebar).
     */
    if (xy.X < 0 || xy.Y < 0 || xy.X >= TacticalRect.Width || xy.Y >= TacticalRect.Height) {
        return true;
    }

    ScrollClassExt* scroll = reinterpret_cast<ScrollClassExt*>(&Map);

    Cell cell;
    Coord coord;
    ObjectClass* object = nullptr;
    bool fogged = false;
    bool shrouded = false;

    /**
     *  Not over the tactical map, ignore the click but keep the selection.
     */
    if (!scroll->_Get_Mouse_Info(xy, cell, coord, object, fogged, shrouded)) {
        return true;
    }

    /**
     *  Same parameters the original left button release handler uses.
     */
    ActionType action = scroll->_What_Action(cell, object, false);

    /**
     *  Only actual orders are performed on right-click. Things like selecting
     *  another object are left to the left mouse button.
     */
    if (Is_Unit_Order_Action(action) || action == EXT_ACTION_ATTACK_MOVE) {

        /**
         *  The original handler passes the coordinate under the mouse as the first argument.
         *  This also gives the attack-move order while in attack-move mode.
         */
        Map.DisplayClass::Mouse_Left_Release(reinterpret_cast<Point2D&>(coord), cell, object, action, false);
    }

    return true;
}


bool Passes_Cloak_Check(TechnoClass* techno)
{
    if (PlayerPtr->Is_Ally(techno)) {
        return true;
    }

    Coord coord = techno->Center_Coord();
    if (Map[coord].Sensed_By(PlayerPtr->HeapID)) {
        return true;
    }

    return false;
}


/**
 *  Fixes a bug where mouse input is not handled on cloaked objects of allied players.
 *  Sadly, the bug appears to be caused by a function that is inlined or copy-pasted
 *  in multiple places, so we need multiple patches.
 *
 *  Author: Rampastring
 */
DEFINE_HOOK(0x005E8840, _ScrollClass_Input_Allied_Cloaked_Object_Patch1, 0)
{
    GET(TechnoClass*, techno, ESI);

    if (Passes_Cloak_Check(techno)) {
        // Target object is cloaked but visible to us (allied or sensed),
        // handle mouse input on it normally.
        return 0x005E886B;
    }

    // Target object is cloaked and not visible to us.
    return 0x005E887F;
}


/**
 *  Fixes a bug where mouse input is not handled on cloaked buildings of allied players.
 *
 *  Author: Rampastring
 */
DEFINE_HOOK(0x005E88AA, _ScrollClass_Input_Allied_Cloaked_Object_Patch2, 0)
{
    GET(BuildingClass*, building, EDI);

    if (Passes_Cloak_Check(building)) {
        // Target object is visible to us (allied or sensed), handle mouse input on it normally.
        return 0x005E88D6;
    }

    // Target object is cloaked and not visible to us.
    return 0x005E88E6;
}


/**
 *  Fixes a bug where mouse input is not handled on cloaked objects of allied players.
 *  While this is in TacticalClass, the bug is identical to the bugs in ScrollClass
 *  and has an identical fix, which is why we include this hack here.
 *
 *  Author: Rampastring
 */
DEFINE_HOOK(0x006167E3, _Tactical_Get_Object_At_Cell_Allied_Cloaked_Object_Patch, 0)
{
    GET(TechnoClass*, techno, EAX);

    if (Passes_Cloak_Check(techno)) {
        // Target object is visible to us (allied or sensed), handle mouse input on it normally.
        return 0x0061680F;
    }

    // Target object is cloaked and not visible to us.
    return 0x00616811;
}


/**
 *  Blocks the map from receiving inputs while any Windows dialogs are open.
 *
 *  Author: ZivDero
 */
DEFINE_HOOK(0x005E934E, _ScrollClass_Message_Handler_Block_Inputs_In_Dialogs_Patch, 6)
{
    if (WSDialogCount != 0) {
        return 0x005E961E;
    }
    return 0;
}


/**
 *  Right-click orders control scheme: the left mouse button no longer issues
 *  orders. A left-click that would have resulted in an order instead selects
 *  the object under the cursor, or unselects everything if there is none.
 *
 *  This patches ScrollClass::Message_Handler (WM_LBUTTONUP) right after
 *  the action under the mouse has been computed.
 *
 *  @author: Vinifera contributors
 */
DEFINE_HOOK(0x005E94BC, _ScrollClass_Message_Handler_Right_Click_Orders_Left_Release_Patch, 5)
{
    GET(ActionType, action, EAX);
    REF_STACK(ObjectClass*, object, 0x30);

    if (OptionsExtension->IsRightClickOrders
        && !Map.IsRubberBand
        && Map.PendingObjectPtr == nullptr
        && Is_Unit_Order_Action(action)) {

        /**
         *  While the selection modifier key (Shift by default) is held, the click
         *  must not change the existing selection.
         */
        bool is_select_key_down = Keyboard->Down(Options.KeySelect1) || Keyboard->Down(Options.KeySelect2);

        if (is_select_key_down) {
            object = nullptr; // Also prevents DisplayClass::Mouse_Left_Release from selecting the object.
            action = ACTION_NONE;

        } else if (object != nullptr && object->Class_Of() != nullptr && object->Class_Of()->IsSelectable) {
            action = ACTION_SELECT;

        } else {
            Unselect_All();
            action = ACTION_NONE;
        }

        R->EAX(action);
    }

    /**
     *  Execute the stolen bytes and continue to DisplayClass::Mouse_Left_Release.
     */
    return 0;
}


/**
 *  ScrollClass::Message_Handler (WM_RBUTTONUP) passes an uninitialized point
 *  to DisplayClass::Mouse_Right_Release (the original function never used it).
 *  Fill it with the actual tactical mouse position of the click, like the other
 *  mouse button handlers do, so the right-click orders control scheme can use it.
 *
 *  @author: Vinifera contributors
 */
DEFINE_HOOK(0x005E95D2, _ScrollClass_Message_Handler_Right_Release_Point_Patch, 9)
{
    GET_STACK(LONG *, lparam, 0x34);
    LEA_STACK(Point2D *, point, 0x10);

    LONG value = *lparam;
    point->X = static_cast<short>(LOWORD(value)) - TacticalRect.X;
    point->Y = static_cast<short>(HIWORD(value)) - TacticalRect.Y;

    _IsTacticalRightRelease = true;

    /**
     *  Execute the stolen bytes and continue to DisplayClass::Mouse_Right_Release.
     */
    return 0;
}


/**
 *  Right-click orders control scheme for the radar (minimap):
 *  - Left-click always moves the tactical view to the clicked location,
 *    even if objects are selected (it never gives orders).
 *  - Right-click gives the order (or the attack-move order) to the selected
 *    objects at the clicked location.
 *
 *  This patches RadarClass::RTacticalClass::Action right after the action of
 *  the selected objects for the location under the mouse has been computed.
 *
 *  @author: Vinifera contributors
 */
DEFINE_HOOK(0x005B932D, _RadarClass_RTacticalClass_Action_Right_Click_Orders_Patch, 5)
{
    enum {
        LEFTPRESS = 0x01,
        LEFTRELEASE = 0x04,
        LEFTUP = 0x08,
        RIGHTRELEASE = 0x40
    };

    GET(ActionType, action, EAX);
    LEA_STACK(Cell *, cell, 0x0C);
    GET_STACK(ObjectClass *, object, 0x10);
    GET_STACK(bool, shadow, 0x14);
    REF_STACK(unsigned, flags, 0x48);

    if (!OptionsExtension->IsRightClickOrders) {
        return 0;
    }

    bool is_attack_move = AttackMoveClass::Is_Mode_Active() && AttackMoveClass::Is_Selection_Able();

    /**
     *  Right-click: give the order.
     */
    if (flags & RIGHTRELEASE) {

        if (is_attack_move) {
            AttackMoveClass::Issue_Order(*cell);

        } else if (Is_Unit_Order_Action(action)) {
            Coord coord = Map[*cell].Center_Coord();
            Map.DisplayClass::Mouse_Left_Release(reinterpret_cast<Point2D &>(coord), *cell, object, action, true);
            TechnoClass::Reset_Action_Line_Timer();
        }

        /**
         *  Don't let the original code unselect everything.
         */
        flags &= ~RIGHTRELEASE;
    }

    /**
     *  Left-click: move the tactical view to the clicked location.
     *  The original code for this expects the cell in the same place, and EBX to be 0.
     */
    if (flags & LEFTPRESS) {
        return 0x005B9357;
    }

    /**
     *  Left-clicks never give orders.
     */
    flags &= ~LEFTRELEASE;

    /**
     *  Show the attack-move cursor while in attack-move mode
     *  (the original code would show the regular order cursor).
     */
    if (is_attack_move) {
        if (flags & LEFTUP) {
            Map.DisplayClass::Mouse_Left_Up(*cell, shadow, object, static_cast<ActionType>(EXT_ACTION_ATTACK_MOVE), true);
        }
        return 0x005B948A;
    }

    /**
     *  Execute the stolen bytes and continue with the original code
     *  (which shows the order cursor for the location under the mouse).
     */
    return 0;
}


/**
 *  Main function for patching the hooks.
 */
void ScrollClassExtension_Hooks()
{

}
