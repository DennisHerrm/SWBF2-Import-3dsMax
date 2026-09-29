-- ============================================================
--  EAfrontMenu_2025_2027.ms
--  Menueeintrag fuer 3ds Max 2025 und neuer
--
--  Das Menuesystem wurde in 3ds Max 2025 komplett neu geschrieben,
--  menuMan ist abgeloest. Menues entstehen nur noch INNERHALB des
--  Callbacks #cuiRegisterMenus; ausserhalb lassen sie sich lesen,
--  aber nicht anlegen. Der Callback liefert den CuiMenuManager
--  ueber callbacks.notificationParam().
--
--  647394 ist die von Max vorgegebene Action-Table-Kennung fuer
--  MacroScripts. Der "permanentId" ist Makroname, Backtick,
--  Kategorie - genau so steht es in der Autodesk-Doku und im
--  Beispielpaket.
--
--  LAEUFT ALS PRE-START-UP-SKRIPT (seit 0.33.0). Die Startreihenfolge laut
--  Autodesk: Schritt 3 Pre-Start-Up-Skripte der Pakete, dann (vor den
--  MacroScripts) #cuiRegisterMenus, und erst ganz am Ende die
--  Post-Start-Up-Skripte. Als post-start-up meldete sich dieser Callback
--  also ZU SPAET an (so auch Larry Minton, Autodesk). Dass das Menue bis
--  0.32.0 trotzdem erschien, war nicht dokumentiertes Verhalten.
--  Der Callback selbst braucht keine fertige Oberflaeche.
--
--  DIE GUIDS SIND EIGENE, NEUE. Sie duerfen sich mit keinem
--  anderen Paket ueberschneiden: ab 2025 LEGT CreateSubMenu an,
--  statt zu suchen, und bei belegter GUID kommt undefined zurueck.
--  Deshalb hat "EAfront Tool" andere Kennungen als "DH Tools".
--
--  Ergebnis: Hauptmenue -> "EAfront Tool" -> "SWBF2 importieren"
-- ============================================================

(
  local sSubMenuGuid   = "D41F7A62-9C05-4E38-B7A1-06E52D9C4F83"
  local sActionGuid    = "1B93C0E7-4AD6-4F21-98C4-72FA0B6E5D19"
  local sAnimGuid      = "6C2E9A41-3F7B-4D08-A5E2-91B7D4C3F062"
  local sMenuName      = "EAfront Tool"
  local sMacroName     = "EAfrontImport_Open"
  local sMacroCategory = "EAfront Tool"

  local iMacroActionTable = 647394

  fn EAfrontRegisterMenu =
  (
    local mgr = callbacks.notificationParam()

    if (mgr == undefined) then
    (
      format "[EAfront] menu: no MenuManager in the callback.\n"
    )
    else
    (
      try
      (
        local barMain = mgr.mainMenuBar
        local mnuSub  = barMain.CreateSubMenu sSubMenuGuid sMenuName

        if (mnuSub == undefined) then
        (
          format "[EAfront] CreateSubMenu returned undefined - GUID already taken?\n"
        )
        else
        (
          local sPermanentId = sMacroName + "`" + sMacroCategory
          mnuSub.CreateAction sActionGuid iMacroActionTable sPermanentId
          mnuSub.CreateAction sAnimGuid iMacroActionTable ("EAfrontImport_Anim`" + sMacroCategory)
          format "EAfront Tool: menu registered (cuiRegisterMenus)\n"
        )
      )
      catch
      (
        format "[EAfront] menu could not be created: %\n" (getCurrentException())
      )
    )
  )

  -- Erst abmelden, dann anmelden. Sonst haengt der Callback nach
  -- einem erneuten Ausfuehren doppelt drin und das Menue entsteht
  -- zweimal.
  callbacks.removeScripts id:#eafrontImportMenu
  callbacks.addScript #cuiRegisterMenus EAfrontRegisterMenu id:#eafrontImportMenu
)
