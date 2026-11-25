# Kóty parapetů – doplněk pro Archicad 29/30 (Windows)

Automaticky doplňuje do kót otvorů výšku parapetu podle české zvyklosti:

```
        900
 ──────────────────
    1500 (900)
```

- nahoře šířka otvoru (Archicad),
- dole výška otvoru (Archicad, volba kóty „Zobrazit i výšku otvorů“),
- **v závorce výška parapetu od úrovně podlaží** – doplňuje tento doplněk.

## Jak to funguje

- U každého okna, dveří i prázdného otvoru v kótě půdorysu vytvoří doplněk text `(900)`
  se stejným písmem, velikostí, perem a hladinou jako kóta.
- Text se sám přepočítá a přesune, když:
  - nakreslíte novou kótu přes otvor,
  - změníte nebo posunete okno/dveře (parapet, výšku, umístění),
  - změníte stěnu (např. odsazení spodní hrany),
  - kótu posunete, upravíte nebo smažete (text se smaže s ní).
- Otvory s nulovým parapetem (např. dveře) se nepopisují.
- Pokud kóta výšku otvoru nezobrazuje, text `(900)` se umístí doprostřed pod kótovací čáru.

## Menu Možnosti › Kóty parapetů

- **Automaticky doplňovat výšku parapetu** – zapnutí/vypnutí automatiky (pamatuje si se).
- **Přepočítat všechny kóty parapetů** – srovná všechny kóty v projektu, např. u starších
  projektů, po změně měřítka nebo výšek podlaží.

## Omezení první verze

- Doplněk musí být načtený u každého, kdo projekt upravuje. Bez něj texty zůstanou,
  ale nebudou se přepočítávat (pomůže pak „Přepočítat všechny kóty parapetů“).
- Po změně výšky podlaží nebo měřítka půdorysu použijte „Přepočítat všechny kóty parapetů“.
- Otvory nástroje *Otvor* (Opening) zatím nejsou podporovány, jen okna a dveře
  (včetně objektu „Prázdný otvor“, který je oknem).

## Sestavení

Stejně jako u palety vlastností: nahrát do repozitáře na GitHubu, v záložce **Actions**
počkat na zelenou fajfku a stáhnout artefakt `KotyParapetu-AC30-Windows` (nebo AC29).
