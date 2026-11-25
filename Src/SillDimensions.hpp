// *****************************************************************************
// KotyParapetu - automaticke doplneni vysky parapetu do kot otvoru
//
// Ke kazdemu otvoru (okno, dvere, prazdny otvor) v kote pudorysu doplnek vytvori
// text "(900)" s vyskou parapetu od urovne podlazi. Text se umisti pod kotovaci
// caru za vysku otvoru a pri kazde zmene kot, otvoru nebo sten se sam prepocita.
// *****************************************************************************

#ifndef SILLDIMENSIONS_HPP
#define SILLDIMENSIONS_HPP

#include "APIEnvir.h"
#include "ACAPinc.h"

namespace SillDim {

bool		IsEnabled ();
void		SetEnabled (bool enabled);

// Zaregistruje sledovani novych prvku a pripoji pozorovatele k existujicim
void		StartObserving ();

// Volano po otevreni / zalozeni projektu
void		OnProjectOpened ();

// Handler notifikaci prvku
GSErrCode	ElementEventHandler (const API_NotifyElementType* elemType);

// Vypise informace o vybranych kotach (pro ladeni)
void		DiagnoseSelection ();

// Prepocet vsech kot v projektu (spoustet uvnitr ACAPI_CallUndoableCommand)
GSErrCode	RecalculateAll ();

}

#endif // SILLDIMENSIONS_HPP
