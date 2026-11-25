// *****************************************************************************
// KotyParapetu - automaticke doplneni vysky parapetu do kot otvoru
// *****************************************************************************

#include "SillDimensions.hpp"

#include "HashSet.hpp"
#include "HashTable.hpp"

#include <cmath>
#include <cstring>

namespace SillDim {

namespace {

// --- Nastaveni ---------------------------------------------------------------

constexpr Int32		PrefsVersion		= 1;

// Textova data ulozena v textu (oznaceni "naseho" textu)
constexpr char		UserDataMagic[4]	= { 'K', 'P', 'A', '1' };
constexpr short		UserDataVersion		= 1;

// Odhad sirky jedne cislice vzhledem k vysce pisma (jen kdyz mereni textu selze)
constexpr double	DigitWidthFactor	= 0.62;

constexpr double	Eps					= 1.0e-6;

struct SillTextUserData {
	char		magic[4];
	API_Guid	dimGuid;
	API_Guid	openingGuid;
};

struct OurText {
	API_Guid	textGuid;
	API_Guid	dimGuid;
	API_Guid	openingGuid;
};

struct DesiredText {
	API_Guid		openingGuid;
	GS::UniString	content;
	API_Coord		loc;
	API_AnchorID	anchor;
	double			angle;
	double			size;
	short			pen;
	short			font;
	API_AttributeIndex	layer;
	short			floorInd;
};

bool						enabled			= true;
bool						processing		= false;

GS::HashSet<GS::Guid>		pendingDims;
GS::HashSet<GS::Guid>		pendingOpenings;
GS::HashSet<GS::Guid>		pendingWalls;
bool						pendingCleanup	= false;


GS::Guid ToGS (const API_Guid& guid)
{
	return APIGuid2GSGuid (guid);
}


bool IsOpeningType (API_ElemTypeID typeID)
{
	return typeID == API_WindowID || typeID == API_DoorID;
}


API_ElemType MakeType (API_ElemTypeID typeID)
{
	API_ElemType type;
	type.typeID = typeID;
	return type;
}

// --- Preference --------------------------------------------------------------

void LoadPreferences ()
{
	Int32	version	= 0;
	GSSize	nBytes	= 0;
	if (ACAPI_GetPreferences (&version, &nBytes, nullptr) != NoError || version != PrefsVersion || nBytes != sizeof (bool))
		return;
	bool value = true;
	if (ACAPI_GetPreferences (&version, &nBytes, &value) == NoError)
		enabled = value;
}


void SavePreferences ()
{
	ACAPI_SetPreferences (PrefsVersion, sizeof (bool), &enabled);
}

// --- Podlazi -----------------------------------------------------------------

GS::HashTable<short, double> GetStoryLevels ()
{
	GS::HashTable<short, double> levels;
	API_StoryInfo storyInfo {};
	if (ACAPI_ProjectSetting_GetStorySettings (&storyInfo) != NoError)
		return levels;

	if (storyInfo.data != nullptr) {
		const Int32 count = storyInfo.lastStory - storyInfo.firstStory + 1;
		for (Int32 i = 0; i < count; ++i) {
			const API_StoryType& story = (*storyInfo.data)[i];
			levels.Put (story.index, story.level);
		}
		BMKillHandle (reinterpret_cast<GSHandle*> (&storyInfo.data));
	}
	return levels;
}


double LevelOf (const GS::HashTable<short, double>& levels, short storyIndex)
{
	const double* level = levels.GetPtr (storyIndex);
	return level != nullptr ? *level : 0.0;
}

// --- Vyska parapetu od urovne podlazi otvoru -----------------------------------

// Vraci false, kdyz se parapet neda urcit
bool CalcSillHeight (const API_Guid& openingGuid, const GS::HashTable<short, double>& levels, double& sill, double& openingHeight)
{
	API_Element opening {};
	opening.header.guid = openingGuid;
	if (ACAPI_Element_Get (&opening) != NoError || !IsOpeningType (opening.header.type.typeID))
		return false;

	const API_WindowType& wd = (opening.header.type.typeID == API_WindowID) ? opening.window : opening.door;
	const API_OpeningBaseType& base = wd.openingBase;

	openingHeight = base.height;

	API_Element wall {};
	wall.header.guid = wd.owner;
	const bool hasWall = (wd.owner != APINULLGuid) && ACAPI_Element_Get (&wall) == NoError && wall.header.type.typeID == API_WallID;

	const double ownLevel		= LevelOf (levels, opening.header.floorInd);
	const double wallBottom		= hasWall ? (LevelOf (levels, wall.header.floorInd) + wall.wall.bottomOffset - ownLevel) : 0.0;
	const double wallTop		= hasWall ? (wallBottom + wall.wall.height) : 0.0;
	const double linkedStory	= LevelOf (levels, base.verticalLink.linkValue) - ownLevel;

	switch (base.verticalLink.linkType) {
		case API_LinkSillToWallBottom:		sill = wallBottom + wd.lower;						break;
		case API_LinkSillToStory:			sill = linkedStory + wd.lower;						break;
		case API_LinkHeaderToWallBottom:	sill = wallBottom + wd.lower - base.height;			break;
		case API_LinkHeaderToStory:			sill = linkedStory + wd.lower - base.height;		break;
		case API_LinkSillToWallTop:			sill = wallTop + wd.lower;							break;
		case API_LinkHeaderToWallTop:		sill = wallTop + wd.lower - base.height;			break;
		default:							sill = wallBottom + wd.lower;						break;
	}
	return true;
}


GS::UniString FormatMillimeters (double meters)
{
	return GS::UniString::Printf ("%.0f", std::round (meters * 1000.0));
}

// --- Nase texty --------------------------------------------------------------

bool ReadUserData (const API_Guid& textGuid, SillTextUserData& data)
{
	API_Elem_Head head {};
	head.guid = textGuid;
	API_ElementUserData userData {};
	if (ACAPI_Element_GetUserData (&head, &userData) != NoError)
		return false;

	bool ok = false;
	if (userData.dataHdl != nullptr && userData.dataVersion == UserDataVersion &&
		BMGetHandleSize (userData.dataHdl) == static_cast<GSSize> (sizeof (SillTextUserData)))
	{
		std::memcpy (&data, *userData.dataHdl, sizeof (SillTextUserData));
		ok = std::memcmp (data.magic, UserDataMagic, sizeof (UserDataMagic)) == 0;
	}
	BMKillHandle (&userData.dataHdl);
	return ok;
}


void WriteUserData (const API_Guid& textGuid, const API_Guid& dimGuid, const API_Guid& openingGuid)
{
	SillTextUserData data {};
	std::memcpy (data.magic, UserDataMagic, sizeof (UserDataMagic));
	data.dimGuid		= dimGuid;
	data.openingGuid	= openingGuid;

	API_ElementUserData userData {};
	userData.dataVersion	= UserDataVersion;
	userData.platformSign	= GS::Act_Platform_Sign;
	userData.flags			= 0;
	userData.dataHdl		= BMAllocateHandle (sizeof (SillTextUserData), ALLOCATE_CLEAR, 0);
	if (userData.dataHdl == nullptr)
		return;
	std::memcpy (*userData.dataHdl, &data, sizeof (SillTextUserData));

	API_Elem_Head head {};
	head.guid = textGuid;
	ACAPI_Element_GetHeader (&head);
	ACAPI_Element_SetUserData (&head, &userData);
	BMKillHandle (&userData.dataHdl);
}


GS::Array<OurText> CollectOurTexts ()
{
	GS::Array<OurText> result;
	GS::Array<API_Guid> texts;
	if (ACAPI_Element_GetElemList (MakeType (API_TextID), &texts) != NoError)
		return result;

	for (const API_Guid& textGuid : texts) {
		SillTextUserData data {};
		if (ReadUserData (textGuid, data))
			result.Push ({ textGuid, data.dimGuid, data.openingGuid });
	}
	return result;
}


bool ElementExists (const API_Guid& guid)
{
	API_Elem_Head head {};
	head.guid = guid;
	return ACAPI_Element_GetHeader (&head) == NoError;
}

// --- Vypocet pozadovanych textu pro jednu kotu ---------------------------------

struct Vec { double x, y; };

Vec		Sub (const API_Coord& a, const API_Coord& b)	{ return { a.x - b.x, a.y - b.y }; }
double	Dot (const Vec& a, const Vec& b)				{ return a.x * b.x + a.y * b.y; }
double	Len (const Vec& v)								{ return std::sqrt (Dot (v, v)); }
Vec		Scale (const Vec& v, double s)					{ return { v.x * s, v.y * s }; }
API_Coord Add (const API_Coord& p, const Vec& v)		{ API_Coord r; r.x = p.x + v.x; r.y = p.y + v.y; return r; }


double OffsetAfterHeightText (const GS::UniString& heightText, short font, double sizeMM, double drawingScale);


GS::Array<DesiredText> ComputeDesiredTexts (const API_Element& dim, const API_ElementMemo& memo, const GS::HashTable<short, double>& levels, double drawingScale)
{
	GS::Array<DesiredText> result;
	if (memo.dimElems == nullptr)
		return result;

	const Int32 nElems = static_cast<Int32> (BMGetHandleSize (reinterpret_cast<GSHandle> (memo.dimElems)) / sizeof (API_DimElem));
	if (nElems < 2)
		return result;

	const API_DimElem* elems = *memo.dimElems;
	GS::HashSet<GS::Guid> doneOpenings;

	for (Int32 k = 0; k + 1 < nElems; ++k) {
		const API_DimElem& a = elems[k];
		const API_DimElem& b = elems[k + 1];

		const API_Guid& openingGuid = a.base.base.guid;
		if (openingGuid == APINULLGuid || openingGuid != b.base.base.guid || !IsOpeningType (a.base.base.type.typeID))
			continue;
		if (doneOpenings.Contains (ToGS (openingGuid)))
			continue;

		double sill = 0.0, openingHeight = 0.0;
		if (!CalcSillHeight (openingGuid, levels, sill, openingHeight))
			continue;

		const GS::UniString sillText = FormatMillimeters (sill);
		if (sillText == "0" || sillText == "-0")
			continue;	// nulovy parapet (napr. dvere) se nepise
		doneOpenings.Add (ToGS (openingGuid));

		// Smer kotovaci cary
		Vec dir = Sub (b.pos, a.pos);
		double dirLen = Len (dir);
		if (dirLen < Eps) {
			dir = { dim.dimension.direction.x, dim.dimension.direction.y };
			dirLen = Len (dir);
			if (dirLen < Eps)
				continue;
		}
		dir = Scale (dir, 1.0 / dirLen);
		const Vec normal = { -dir.y, dir.x };

		API_Coord mid;
		mid.x = (a.pos.x + b.pos.x) / 2.0;
		mid.y = (a.pos.y + b.pos.y) / 2.0;

		// Text sirky otvoru - ten, ktery lezi mezi obema body
		const API_NoteType* note = &a.note;
		{
			const double tA = Dot (Sub (a.note.pos, a.pos), dir);
			if (tA < -Eps || tA > dirLen + Eps)
				note = &b.note;
		}

		const double textHeight = note->noteSize / 1000.0 * drawingScale;	// vyska pisma v modelu
		double offset = Dot (Sub (note->pos, mid), normal);				// vzdalenost textu sirky od cary
		const double upSign = (offset < 0.0) ? -1.0 : 1.0;
		offset = std::fabs (offset);
		if (offset < Eps)
			offset = 0.5 * textHeight;

		// Smer cteni textu
		Vec reading = { std::cos (note->noteAngle), std::sin (note->noteAngle) };
		if (Dot (reading, dir) < 0.0)
			reading = Scale (reading, -1.0);

		// Stred vysky otvoru pod carou = zrcadlo textu sirky (predpoklad: pozice textu = spodni okraj textu)
		const Vec below = Scale (normal, -upSign);
		const API_Coord lowerLineMid = Add (mid, Scale (below, offset + 0.5 * textHeight));

		DesiredText desired;
		desired.openingGuid	= openingGuid;
		desired.angle		= note->noteAngle;
		desired.size		= note->noteSize;
		desired.pen			= note->notePen;
		desired.font		= note->noteFont;
		desired.layer		= dim.header.layer;
		desired.floorInd	= dim.header.floorInd;

		if (dim.dimension.detailedHoleDim) {
			// "1500 (900)" - za vyskou otvoru jedna mezera a hned zavorka
			const double offsetAlong = OffsetAfterHeightText (FormatMillimeters (openingHeight), note->noteFont, note->noteSize, drawingScale);
			desired.content	= GS::UniString ("(") + sillText + ")";
			desired.anchor	= APIAnc_LM;
			desired.loc		= Add (lowerLineMid, Scale (reading, offsetAlong));
		} else {
			// Kota bez vysky otvoru - jen "(900)" uprostred pod carou
			desired.content	= GS::UniString ("(") + sillText + ")";
			desired.anchor	= APIAnc_MM;
			desired.loc		= lowerLineMid;
		}
		result.Push (desired);
	}
	return result;
}

// --- Mereni delky textu -------------------------------------------------------

// Delka radku textu v modelovych jednotkach (s ohledem na meritko)
double MeasureText (const GS::UniString& text, short font, double sizeMM, double drawingScale)
{
	GS::UniString lineText = text;

	API_TextLinePars pars {};
	pars.index			= 0;
	pars.drvScaleCorr	= true;
	pars.wFont			= font;
	pars.wFace			= 0;
	pars.wSlant			= 1.5707963267948966;	// PI/2 = rovne pismo
	pars.wSize			= sizeMM;
	pars.lineUniStr		= &lineText;

	double length = 0.0;
	if (ACAPI_Element_GetTextLineLength (&pars, &length) == NoError && length > Eps)
		return length;

	// nouzovy odhad
	return text.GetLength () * DigitWidthFactor * sizeMM / 1000.0 * drawingScale;
}


// Vzdalenost od stredu textu vysky otvoru k zacatku "(" = polovina vysky otvoru + jedna mezera
double OffsetAfterHeightText (const GS::UniString& heightText, short font, double sizeMM, double drawingScale)
{
	const double heightWidth = MeasureText (heightText, font, sizeMM, drawingScale);

	// sirka mezery: rozdil "1 1" a "11" (samotna mezera na konci by se mohla oriznout)
	double spaceWidth = MeasureText ("1 1", font, sizeMM, drawingScale) - MeasureText ("11", font, sizeMM, drawingScale);
	if (spaceWidth < Eps)
		spaceWidth = 0.3 * sizeMM / 1000.0 * drawingScale;

	return 0.5 * heightWidth + spaceWidth;
}

// --- Vytvoreni / zmena / smazani textu ------------------------------------------

bool TextMatches (const API_Guid& textGuid, const DesiredText& desired)
{
	API_Element text {};
	text.header.guid = textGuid;
	if (ACAPI_Element_Get (&text) != NoError)
		return false;

	API_ElementMemo memo {};
	bool sameContent = false;
	if (ACAPI_Element_GetMemo (textGuid, &memo, APIMemoMask_TextContent) == NoError && memo.textContent != nullptr)
		sameContent = (*memo.textContent == desired.content);
	ACAPI_DisposeElemMemoHdls (&memo);

	const double tol = 1.0e-5;
	return sameContent &&
		std::fabs (text.text.loc.x - desired.loc.x) < tol &&
		std::fabs (text.text.loc.y - desired.loc.y) < tol &&
		std::fabs (text.text.angle - desired.angle) < tol &&
		std::fabs (text.text.size - desired.size) < tol &&
		text.text.anchor == desired.anchor &&
		text.text.pen == desired.pen &&
		text.text.font == desired.font &&
		text.header.layer == desired.layer &&
		text.header.floorInd == desired.floorInd;
}


void FillText (API_Element& text, const DesiredText& desired)
{
	text.header.layer		= desired.layer;
	text.header.floorInd	= desired.floorInd;
	text.text.loc			= desired.loc;
	text.text.anchor		= desired.anchor;
	text.text.angle			= desired.angle;
	text.text.size			= desired.size;
	text.text.pen			= desired.pen;
	text.text.font			= desired.font;
	text.text.just			= APIJust_Left;
	text.text.nonBreaking	= true;
	text.text.multiStyle	= false;
	text.text.usedContour	= false;
	text.text.usedFill		= false;
}


void CreateText (const API_Guid& dimGuid, const DesiredText& desired)
{
	API_Element		text {};
	API_ElementMemo	memo {};
	text.header.type = MakeType (API_TextID);
	if (ACAPI_Element_GetDefaults (&text, &memo) != NoError) {
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}
	ACAPI_DisposeElemMemoHdls (&memo);

	FillText (text, desired);

	API_ElementMemo newMemo {};
	newMemo.textContent = new GS::UniString (desired.content);
	if (ACAPI_Element_Create (&text, &newMemo) == NoError)
		WriteUserData (text.header.guid, dimGuid, desired.openingGuid);
	ACAPI_DisposeElemMemoHdls (&newMemo);
}


void UpdateText (const API_Guid& textGuid, const DesiredText& desired)
{
	if (TextMatches (textGuid, desired))
		return;

	API_Element text {};
	text.header.guid = textGuid;
	if (ACAPI_Element_Get (&text) != NoError)
		return;

	FillText (text, desired);

	API_Element mask;
	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
	ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, floorInd);
	ACAPI_ELEMENT_MASK_SET (mask, API_TextType, loc);
	ACAPI_ELEMENT_MASK_SET (mask, API_TextType, anchor);
	ACAPI_ELEMENT_MASK_SET (mask, API_TextType, angle);
	ACAPI_ELEMENT_MASK_SET (mask, API_TextType, size);
	ACAPI_ELEMENT_MASK_SET (mask, API_TextType, pen);
	ACAPI_ELEMENT_MASK_SET (mask, API_TextType, font);
	ACAPI_ELEMENT_MASK_SET (mask, API_TextType, just);
	ACAPI_ELEMENT_MASK_SET (mask, API_TextType, nonBreaking);

	API_ElementMemo memo {};
	memo.textContent = new GS::UniString (desired.content);
	ACAPI_Element_Change (&text, &mask, &memo, APIMemoMask_TextContent, true);
	ACAPI_DisposeElemMemoHdls (&memo);
}


void SyncDimension (const API_Guid& dimGuid, const GS::Array<OurText>& existing,
					const GS::HashTable<short, double>& levels, double drawingScale, GS::Array<API_Guid>& toDelete)
{
	GS::Array<DesiredText> desired;

	API_Element dim {};
	dim.header.guid = dimGuid;
	if (ACAPI_Element_Get (&dim) == NoError && dim.header.type.typeID == API_DimensionID) {
		API_ElementMemo memo {};
		if (ACAPI_Element_GetMemo (dimGuid, &memo) == NoError)
			desired = ComputeDesiredTexts (dim, memo, levels, drawingScale);
		ACAPI_DisposeElemMemoHdls (&memo);
	}

	GS::HashSet<GS::Guid> usedTexts;
	for (const DesiredText& d : desired) {
		const OurText* match = nullptr;
		for (const OurText& t : existing) {
			if (t.openingGuid == d.openingGuid && !usedTexts.Contains (ToGS (t.textGuid))) {
				match = &t;
				break;
			}
		}
		if (match != nullptr) {
			usedTexts.Add (ToGS (match->textGuid));
			UpdateText (match->textGuid, d);
		} else {
			CreateText (dimGuid, d);
		}
	}

	// Texty, ktere uz nepatri k zadnemu otvoru kot (nebo duplikaty po kopirovani)
	for (const OurText& t : existing) {
		if (!usedTexts.Contains (ToGS (t.textGuid)))
			toDelete.Push (t.textGuid);
	}
}

// --- Zpracovani ----------------------------------------------------------------

// Kotuje kota nektery z otvoru, nebo otvor ve stene ze seznamu?
bool DimensionReferences (const API_Guid& dimGuid, const GS::HashSet<GS::Guid>& openings, const GS::HashSet<GS::Guid>& walls,
						  GS::HashTable<GS::Guid, API_Guid>& ownerCache)
{
	API_ElementMemo memo {};
	if (ACAPI_Element_GetMemo (dimGuid, &memo) != NoError || memo.dimElems == nullptr) {
		ACAPI_DisposeElemMemoHdls (&memo);
		return false;
	}

	bool found = false;
	const Int32 nElems = static_cast<Int32> (BMGetHandleSize (reinterpret_cast<GSHandle> (memo.dimElems)) / sizeof (API_DimElem));
	for (Int32 k = 0; k < nElems && !found; ++k) {
		const API_Base& base = (*memo.dimElems)[k].base.base;
		if (!IsOpeningType (base.type.typeID) || base.guid == APINULLGuid)
			continue;
		const GS::Guid key = ToGS (base.guid);
		if (openings.Contains (key)) {
			found = true;
			break;
		}
		if (!walls.IsEmpty ()) {
			if (!ownerCache.ContainsKey (key)) {
				API_Element opening {};
				opening.header.guid = base.guid;
				API_Guid owner = APINULLGuid;
				if (ACAPI_Element_Get (&opening) == NoError)
					owner = (opening.header.type.typeID == API_WindowID) ? opening.window.owner : opening.door.owner;
				ownerCache.Add (key, owner);
			}
			if (walls.Contains (ToGS (ownerCache.Get (key))))
				found = true;
		}
	}
	ACAPI_DisposeElemMemoHdls (&memo);
	return found;
}


// Hlavni prepocet. allDims = prepocitat vse.
void Process (bool allDims)
{
	if (processing)
		return;
	processing = true;

	// Kota a jeji texty jsou v pudorysu - pracuje se vzdy v databazi pudorysu
	API_DatabaseInfo origDb {};
	ACAPI_Database_GetCurrentDatabase (&origDb);
	bool switched = false;
	if (origDb.typeID != APIWind_FloorPlanID) {
		API_DatabaseInfo planDb {};
		planDb.typeID = APIWind_FloorPlanID;
		switched = ACAPI_Database_ChangeCurrentDatabase (&planDb) == NoError;
		if (!switched) {
			processing = false;
			return;
		}
	}

	const GS::HashTable<short, double> levels = GetStoryLevels ();
	double drawingScale = 100.0;
	ACAPI_Drawing_GetDrawingScale (&drawingScale);
	if (drawingScale < Eps)
		drawingScale = 100.0;

	const GS::Array<OurText> ourTexts = CollectOurTexts ();

	// Ktere koty se maji prepocitat
	GS::HashSet<GS::Guid>		dimsToSync;
	GS::Array<API_Guid>			dimsOrdered;
	auto addDim = [&] (const API_Guid& guid) {
		if (guid != APINULLGuid && dimsToSync.Add (ToGS (guid)))
			dimsOrdered.Push (guid);
	};

	GS::Array<API_Guid> allDimensions;
	if (allDims || !pendingOpenings.IsEmpty () || !pendingWalls.IsEmpty ())
		ACAPI_Element_GetElemList (MakeType (API_DimensionID), &allDimensions);

	if (allDims) {
		for (const API_Guid& guid : allDimensions)
			addDim (guid);
	} else {
		for (const GS::Guid& guid : pendingDims)
			addDim (GSGuid2APIGuid (guid));

		if (!pendingOpenings.IsEmpty () || !pendingWalls.IsEmpty ()) {
			GS::HashTable<GS::Guid, API_Guid> ownerCache;
			for (const API_Guid& guid : allDimensions) {
				if (!dimsToSync.Contains (ToGS (guid)) && DimensionReferences (guid, pendingOpenings, pendingWalls, ownerCache))
					addDim (guid);
			}
		}
	}

	// Texty k otvorum, ktere jsou nove navazane na smazane koty/otvory
	GS::Array<API_Guid> toDelete;
	if (allDims || pendingCleanup) {
		for (const OurText& t : ourTexts) {
			if (!ElementExists (t.dimGuid))
				toDelete.Push (t.textGuid);
			else if (!dimsToSync.Contains (ToGS (t.dimGuid)) && !ElementExists (t.openingGuid))
				addDim (t.dimGuid);
		}
	}

	// Texty podle kot
	GS::HashTable<GS::Guid, GS::Array<OurText>> textsByDim;
	for (const OurText& t : ourTexts) {
		const GS::Guid key = ToGS (t.dimGuid);
		if (!textsByDim.ContainsKey (key))
			textsByDim.Add (key, GS::Array<OurText> ());
		textsByDim[key].Push (t);
	}

	for (const API_Guid& dimGuid : dimsOrdered) {
		const GS::Guid key = ToGS (dimGuid);
		const GS::Array<OurText> existing = textsByDim.ContainsKey (key) ? textsByDim.Get (key) : GS::Array<OurText> ();
		SyncDimension (dimGuid, existing, levels, drawingScale, toDelete);
	}

	if (!toDelete.IsEmpty ())
		ACAPI_Element_Delete (toDelete);

	if (switched)
		ACAPI_Database_ChangeCurrentDatabase (&origDb);

	pendingDims.Clear ();
	pendingOpenings.Clear ();
	pendingWalls.Clear ();
	pendingCleanup = false;
	processing = false;
}


void AttachObserversOfType (API_ElemTypeID typeID)
{
	GS::Array<API_Guid> guids;
	if (ACAPI_Element_GetElemList (MakeType (typeID), &guids) != NoError)
		return;
	for (const API_Guid& guid : guids)
		ACAPI_Element_AttachObserver (guid);
}

}	// namespace

// =============================================================================

bool IsEnabled ()
{
	return enabled;
}


void SetEnabled (bool value)
{
	enabled = value;
	SavePreferences ();
}


void StartObserving ()
{
	LoadPreferences ();

	const API_ElemTypeID watched[] = { API_DimensionID, API_WindowID, API_DoorID, API_WallID, API_TextID };
	for (API_ElemTypeID typeID : watched) {
		API_ToolBoxItem item {};
		item.type = MakeType (typeID);
		ACAPI_Element_CatchNewElement (&item, ElementEventHandler);
	}
	ACAPI_Element_InstallElementObserver (ElementEventHandler);
}


void OnProjectOpened ()
{
	pendingDims.Clear ();
	pendingOpenings.Clear ();
	pendingWalls.Clear ();
	pendingCleanup = false;

	API_DatabaseInfo origDb {};
	ACAPI_Database_GetCurrentDatabase (&origDb);
	bool switched = false;
	if (origDb.typeID != APIWind_FloorPlanID) {
		API_DatabaseInfo planDb {};
		planDb.typeID = APIWind_FloorPlanID;
		switched = ACAPI_Database_ChangeCurrentDatabase (&planDb) == NoError;
	}

	AttachObserversOfType (API_DimensionID);
	AttachObserversOfType (API_WindowID);
	AttachObserversOfType (API_DoorID);
	AttachObserversOfType (API_WallID);

	if (switched)
		ACAPI_Database_ChangeCurrentDatabase (&origDb);
}


GSErrCode ElementEventHandler (const API_NotifyElementType* elemType)
{
	if (elemType == nullptr || processing)
		return NoError;

	switch (elemType->notifID) {
		case APINotifyElement_EndEvents:
			if (enabled && (!pendingDims.IsEmpty () || !pendingOpenings.IsEmpty () || !pendingWalls.IsEmpty () || pendingCleanup))
				Process (false);
			return NoError;

		case APINotifyElement_New:
		case APINotifyElement_Copy:
		case APINotifyElement_Change:
		case APINotifyElement_Edit:
		case APINotifyElement_Delete:
			break;

		default:
			return NoError;		// Undo/Redo apod. - databazi se nesmi menit
	}

	const API_ElemTypeID	typeID	= elemType->elemHead.type.typeID;
	const API_Guid&			guid	= elemType->elemHead.guid;
	const bool				created	= elemType->notifID == APINotifyElement_New || elemType->notifID == APINotifyElement_Copy;
	const bool				deleted	= elemType->notifID == APINotifyElement_Delete;

	if (created && typeID != API_TextID)
		ACAPI_Element_AttachObserver (guid);

	if (!enabled)
		return NoError;

	switch (typeID) {
		case API_DimensionID:
			if (deleted)
				pendingCleanup = true;
			else
				pendingDims.Add (ToGS (guid));
			break;

		case API_WindowID:
		case API_DoorID:
			if (deleted)
				pendingCleanup = true;
			else
				pendingOpenings.Add (ToGS (guid));
			break;

		case API_WallID:
			if (!deleted)
				pendingWalls.Add (ToGS (guid));
			else
				pendingCleanup = true;
			break;

		case API_TextID:
			if (created) {
				// Zkopirovany text parapetu - kotu prepocitat, duplikat se smaze
				SillTextUserData data {};
				if (ReadUserData (guid, data))
					pendingDims.Add (ToGS (data.dimGuid));
			}
			break;

		default:
			break;
	}
	return NoError;
}


GSErrCode RecalculateAll ()
{
	Process (true);
	return NoError;
}

}
