-- ============================================================
--  EAfrontMenu_2016_2024.ms
--  Menueeintrag fuer 3ds Max 2016 bis 2024 (altes menuMan-API)
--
--  Ab 2025 ist menuMan abgeloest - dafuer gibt es
--  EAfrontMenu_2025_2027.ms. Die PackageContents.xml gibt jeder
--  Max-Fassung nur ihr passendes Skript; laufen beide, entsteht
--  das Menue doppelt.
--
--  Ergebnis: Hauptmenue -> "EAfront Tool" -> "SWBF2 importieren"
-- ============================================================

(
  local sMenuName  = "EAfront Tool"
  local sMacroCat  = "EAfront Tool"
  -- Eintraege: Titel und MacroScript (0.38.0: zweiter Eintrag Animationen)
  local aItems = #(#("Import SWBF2", "EAfrontImport_Open"), #("SWBF2 Animations", "EAfrontImport_Anim"))
  -- Titles up to 0.44.0 were German; remove those entries so they do not stay twice.
  local aOld = #("SWBF2 importieren", "SWBF2 Animationen")

  local mnuExisting = menuMan.findMenu sMenuName
  if (mnuExisting == undefined) then
  (
    local mnuBar = menuMan.getMainMenuBar()
    mnuExisting = menuMan.createMenu sMenuName
    local itmSub = menuMan.createSubMenuItem sMenuName mnuExisting
    mnuBar.addItem itmSub (mnuBar.numItems())
  )
  for i = mnuExisting.numItems() to 1 by -1 do
  (
    local itm = mnuExisting.getItem i
    if (findItem aOld (itm.getTitle()) > 0) then mnuExisting.removeItemByPosition i
  )
  for aItem in aItems do
  (
    local bFound = false
    for i = 1 to mnuExisting.numItems() while (not bFound) do
    (
      local itm = mnuExisting.getItem i
      if ((itm.getTitle()) == aItem[1]) then bFound = true
    )
    if (not bFound) then
    (
      local actNew = menuMan.createActionItem aItem[2] sMacroCat
      actNew.setTitle aItem[1]
      actNew.setUseCustomTitle true
      mnuExisting.addItem actNew -1
    )
  )
  menuMan.updateMenuBar()

  format "EAfront Tool: menu registered (menuMan).\n"
)
