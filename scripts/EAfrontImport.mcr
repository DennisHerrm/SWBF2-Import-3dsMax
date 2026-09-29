-- ============================================================
--  EAfrontImport.mcr - Menueeintrag "EAfront Tool" -> "SWBF2 importieren"
--
--  Oeffnet das Figurenfenster aus der .dlu ueber Swbf2Cpp.showDialog().
--
--  Ist Swbf2Cpp nicht erreichbar, MISST das Skript, woran es liegt, statt
--  nur "nicht geladen" zu sagen: Fehlertext, ob das Kerninterface
--  registriert ist, ob die Importer-Klasse da ist, WELCHE SWBF2Import.dlu
--  im Prozess steckt (Pfad und Dateifassung) und welche msvcp140.dll.
--  Das geht in den Listener UND nach Downloads\Import swbf2.log - START.bat
--  haengt die Datei ans START.log, eine Datei genuegt also weiter.
--
--  Rueckweg: ist die .dlu geladen, oeffnet ein Import der layout.toc des
--  Spiels das Fenster auch ohne Swbf2Cpp (Datei -> Importieren -> .toc).
--
--  MAXScript loest Namen strikt von oben nach unten auf.
-- ============================================================

macroScript EAfrontImport_Open
    category:"EAfront Tool"
    buttonText:"Import SWBF2"
    toolTip:"Star Wars Battlefront II - import a character straight from the game"
(
    on execute do
    (
        local sErwartet = "1.43.0"
        -- Die Schnittstelle zuerst ueber ihren globalen Namen, sonst ueber die
        -- Liste aller Kerninterfaces. Der zweite Weg haengt nicht daran, dass
        -- MAXScript fuer das Interface einen globalen Namen angelegt hat -
        -- genau das hat in 0.33.0 und 0.33.1 nicht geklappt.
        local oSwbf = undefined
        local sWeg = "global"
        try (if (Swbf2Cpp != undefined) then oSwbf = Swbf2Cpp) catch (oSwbf = undefined)
        if (oSwbf == undefined) then
        (
            sWeg = "getCoreInterfaces"
            try (for i in getCoreInterfaces() where matchPattern (i as string) pattern:"*Swbf2Cpp*" do oSwbf = i) catch ()
        )
        local sPlugin = undefined
        if (oSwbf != undefined) then (try (sPlugin = oSwbf.version()) catch (sPlugin = undefined))

        if (sPlugin != undefined) then
        (
            if (sPlugin != sErwartet) then
                format "EAfront Tool: script % but plugin % - please run START.bat again.\n" sErwartet sPlugin
            if (sWeg != "global") then
                format "EAfront Tool: Swbf2Cpp only found via getCoreInterfaces() - global name missing.\n"
            oSwbf.showDialog()
        )
        else
        (
            local aZeilen = #()
            append aZeilen ("EAfront Tool " + sErwartet + " - diagnostics, Swbf2Cpp is not reachable")
            append aZeilen ("  3ds Max: " + ((maxVersion())[1] as string))
            local sFehler = "(kein Fehlertext)"
            try (Swbf2Cpp.version()) catch (sFehler = getCurrentException())
            append aZeilen ("  Swbf2Cpp.version(): " + sFehler)
            local bKern = false
            try (for i in getCoreInterfaces() where matchPattern (i as string) pattern:"*Swbf2Cpp*" do bKern = true) catch ()
            append aZeilen ("  core interface registered: " + (if bKern then "yes" else "no"))
            local bImporter = false
            try (for c in importerPlugin.classes where matchPattern (c as string) pattern:"*SWBF2*" do bImporter = true) catch ()
            append aZeilen ("  importer class loaded: " + (if bImporter then "yes" else "no"))
            local sAlt = "?"
            try (sAlt = (SWBF2Import as string) + " / " + ((classOf SWBF2Import) as string)) catch (sAlt = "?")
            append aZeilen ("  name SWBF2Import (up to 0.33.0) refers to: " + sAlt)
            try
            (
                local oMods = ((dotNetClass "System.Diagnostics.Process").GetCurrentProcess()).Modules
                for i = 0 to (oMods.Count - 1) do
                (
                    local oMod = oMods.Item[i]
                    local sName = oMod.ModuleName
                    if (matchPattern sName pattern:"SWBF2Import*" or matchPattern sName pattern:"msvcp140*" or matchPattern sName pattern:"vcruntime140*") then
                        append aZeilen ("  module: " + oMod.FileName + "  version " + (oMod.FileVersionInfo.FileVersion as string))
                )
            )
            catch (append aZeilen ("  modules not readable: " + getCurrentException()))
            local sMaxLog = "?"
            try (sMaxLog = logsystem.getNetLogFileName()) catch (sMaxLog = "?")
            append aZeilen ("  Max.log: " + sMaxLog)
            local sStart = (systemTools.getEnvVariable "LOCALAPPDATA") + "\\SWBF2Import\\start.log"
            append aZeilen ("  start.log of the .dlu present: " + (if (doesFileExist sStart) then "yes" else "no"))

            -- Eigene Datei: das Plugin ueberschreibt Import swbf2.log beim naechsten
            -- Import (in 0.33.1 ging die Diagnose so verloren, sobald ueber die
            -- layout.toc importiert wurde).
            local sLog = (systemTools.getEnvVariable "USERPROFILE") + "\\Downloads\\EAfront Diagnose.log"
            local fLog = undefined
            try (fLog = createFile sLog) catch (fLog = undefined)
            for z in aZeilen do
            (
                format "%\n" z
                if (fLog != undefined) then format "%\n" z to:fLog
            )
            if (fLog != undefined) then close fLog

            if (bImporter) then
            (
                -- Rueckweg ueber den Importer: layout.toc importieren oeffnet das Fenster.
                local sIni = (systemTools.getEnvVariable "LOCALAPPDATA") + "\\SWBF2Import\\einstellungen.ini"
                local sOrdner = ""
                try (sOrdner = getINISetting sIni "Fenster" "Spielordner") catch (sOrdner = "")
                local sToc = sOrdner + "\\Data\\layout.toc"
                if (sOrdner == "" or not (doesFileExist sToc)) then
                    sToc = getOpenFileName caption:"STAR WARS Battlefront II - select Data\\layout.toc" types:"layout.toc|layout.toc|All files (*.*)|*.*|"
                if (sToc != undefined) then importFile sToc
            )
            else
            (
                messageBox ("SWBF2Import.dlu " + sErwartet + " is not loaded.\n\nThe diagnostics are in the Listener (F11) and in Downloads\\EAfront Diagnose.log.\nPlease run START.bat and send the START.log.") title:"EAfront Tool"
            )
        )
    )
)

macroScript EAfrontImport_Anim
    category:"EAfront Tool"
    buttonText:"SWBF2 Animations"
    toolTip:"Star Wars Battlefront II - pick animations and load them onto the skeleton"
(
    on execute do
    (
        local oSwbf = undefined
        try (if (Swbf2Cpp != undefined) then oSwbf = Swbf2Cpp) catch (oSwbf = undefined)
        if (oSwbf == undefined) then
            try (for i in getCoreInterfaces() where matchPattern (i as string) pattern:"*Swbf2Cpp*" do oSwbf = i) catch ()
        if (oSwbf != undefined) then
        (
            local bOk = false
            try (oSwbf.showAnimDialog(); bOk = true) catch (format "EAfront Tool: showAnimDialog missing - please run START.bat again (%).\n" (getCurrentException()))
        )
        else
            format "EAfront Tool: Swbf2Cpp not reachable - run \"Import SWBF2\" first, it prints the diagnostics.\n"
    )
)
