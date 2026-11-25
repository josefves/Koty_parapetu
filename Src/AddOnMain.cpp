// *****************************************************************************
// KotyParapetu - hlavni soubor doplnku
// Menu: Moznosti > Koty parapetu (podnabidka)
//   1) Automaticky doplnovat vysku parapetu (prepinac)
//   2) Prepocitat vsechny koty parapetu
// *****************************************************************************

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "RS.hpp"

#include "ResourceIds.hpp"
#include "SillDimensions.hpp"

namespace {

constexpr short MenuItemToggle		= 1;
constexpr short MenuItemRecalculate	= 2;
constexpr short MenuItemDiagnose	= 3;


void UpdateToggleMenuCheck ()
{
	API_MenuItemRef	itemRef {};
	GSFlags			itemFlags {};

	itemRef.menuResID = ID_MENU;
	itemRef.itemIndex = MenuItemToggle;

	ACAPI_MenuItem_GetMenuItemFlags (&itemRef, &itemFlags);
	if (SillDim::IsEnabled ())
		itemFlags |= API_MenuItemChecked;
	else
		itemFlags &= ~API_MenuItemChecked;
	ACAPI_MenuItem_SetMenuItemFlags (&itemRef, &itemFlags);
}


GS::UniString GetUIString (Int32 index)
{
	GS::UniString result;
	RSGetIndString (&result, ID_UI_STRINGS, index, ACAPI_GetOwnResModule ());
	return result;
}


GSErrCode MenuCommandHandler (const API_MenuParams* menuParams)
{
	if (menuParams->menuItemRef.menuResID != ID_MENU)
		return NoError;

	switch (menuParams->menuItemRef.itemIndex) {
		case MenuItemToggle:
			SillDim::SetEnabled (!SillDim::IsEnabled ());
			UpdateToggleMenuCheck ();
			// po zapnuti srovnat vsechny koty, po vypnuti texty odstranit a vratit vysky otvoru Archicadu
			ACAPI_CallUndoableCommand (GetUIString (1), [] () -> GSErrCode {
				return SillDim::IsEnabled () ? SillDim::RecalculateAll () : SillDim::RemoveAll ();
			});
			break;

		case MenuItemRecalculate:
			ACAPI_CallUndoableCommand (GetUIString (1), [] () -> GSErrCode {
				return SillDim::RecalculateAll ();
			});
			break;

		case MenuItemDiagnose:
			SillDim::DiagnoseSelection ();
			break;

		default:
			break;
	}
	return NoError;
}


GSErrCode ProjectEventHandler (API_NotifyEventID notifID, Int32)
{
	switch (notifID) {
		case APINotify_New:
		case APINotify_NewAndReset:
		case APINotify_Open:
			SillDim::OnProjectOpened ();
			UpdateToggleMenuCheck ();
			break;
		default:
			break;
	}
	return NoError;
}

}	// namespace

// =============================================================================
// Povinne funkce doplnku
// =============================================================================

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, ID_ADDON_INFO, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, ID_ADDON_INFO, 2, ACAPI_GetOwnResModule ());

	// Preload: doplnek musi byt nacteny stale, aby mohl sledovat zmeny
	return APIAddon_Preload;
}


GSErrCode RegisterInterface (void)
{
	// Moznosti > Koty parapetu > (polozky): prvni retezec = nazev podnabidky, dalsi = polozky
	return ACAPI_MenuItem_RegisterMenu (ID_MENU, 0, MenuCode_Options1, MenuFlag_Default);
}


GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (ID_MENU, MenuCommandHandler);
	if (err != NoError)
		return err;

	err = ACAPI_ProjectOperation_CatchProjectEvent (APINotify_New | APINotify_NewAndReset | APINotify_Open, ProjectEventHandler);
	if (err != NoError)
		return err;

	SillDim::StartObserving ();
	SillDim::OnProjectOpened ();
	UpdateToggleMenuCheck ();
	return NoError;
}


GSErrCode FreeData (void)
{
	return NoError;
}
