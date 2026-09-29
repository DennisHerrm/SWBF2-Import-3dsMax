cmake_minimum_required(VERSION 3.20)
# C UND CXX. Ohne das "C" schaltet CMake die C-Sprache gar nicht erst ein,
# und die .c-Dateien der Entpacker unter vendor/ werden STILL uebergangen -
# die Bibliothek entsteht, enthaelt aber nur fbcas.obj, und der Binder meldet
# spaeter LZ4_decompress_safe, mz_uncompress und die ZSTD_-Funktionen als
# nicht aufgeloest. Genau daran ist der erste Bau gescheitert.
project(SWBF2Import LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ------------------------------------------------------------
#  Aufbau nach dem Vorbild von XFBIN Import: EIN Quellensatz fuer
#  den Leser, daraus zwei Ziele.
#
#    fbdump.exe       braucht KEIN Max-SDK  -> Gegenprobe gegen Python
#    SWBF2Import.dlu  braucht das Max-SDK   -> das eigentliche Plugin
#
#  Beide benutzen denselben Leser. Damit kann nie auseinanderlaufen,
#  was das Plugin liest und was geprueft wurde.
# ------------------------------------------------------------

# C++17, nicht 20: davon gebraucht wird wenig, und C++17 laesst sich
# auch gegen die Header aelterer Max-SDKs uebersetzen.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MAX_VERSION "0" CACHE STRING "3ds Max Jahrgang, z.B. 2027")
set(3DSMAX_SDK_DIR "" CACHE PATH "3ds Max SDK (maxsdk-Ordner)")

option(SWBF2_BUILD_TOOL   "Eigenstaendiges fbdump.exe mitbauen" ON)
option(SWBF2_BUILD_PLUGIN "Max-Plugin bauen (braucht das Max SDK)" OFF)
option(SWBF2_STRICT       "Strenge Compiler-Warnungen" ON)

# 3ds Max gibt es nur als 64-Bit-Anwendung; eine 32-Bit-DLL laedt dort nicht.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "32-Bit-Bau. Es braucht x64:  cmake -S . -B build -A x64")
endif()

# Die Laufzeit muss die mehrfaedige DLL-Fassung sein (/MD), in JEDER
# Konfiguration. Debug- und Release-Laufzeit verwalten getrennte Heaps;
# was Max anlegt und das Plugin freigibt, muss aus demselben kommen.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# ---- Haertung (docs/DEVELOPMENT.md, "Rules for plugin code") -----------------------------------
# /guard:cf  Control Flow Guard: Ziele indirekter Aufrufe werden geprueft.
#            Braucht den Schalter beim Uebersetzen UND beim Binden.
# _MSVC_STL_HARDENING=1  seit VS 2022 17.14: bestimmte undefinierte Zugriffe
#            der Standardbibliothek (z.B. operator[] ausserhalb) werden zu einem
#            sofortigen Abbruch statt still Speicher zu lesen oder zu schreiben.
# _MSVC_STL_DESTRUCTOR_TOMBSTONES=1  gegen Zugriffe nach dem Freigeben.
# Aeltere Werkzeuge kennen die beiden Namen nicht und uebergehen sie.
# /sdl bewusst noch NICHT: es macht Warnungen zu Fehlern, und die fremden
# Entpacker unter vendor/ liegen im selben Ziel - erst per Probe einfuehren.
if(MSVC)
    add_compile_options(/guard:cf)
    add_link_options(/guard:cf)
    add_compile_definitions(_MSVC_STL_HARDENING=1 _MSVC_STL_DESTRUCTOR_TOMBSTONES=1)
    # std::mutex: seit VS 2022 17.10 ist der Konstruktor constexpr und ruft die
    # Laufzeit nicht mehr auf. Laedt der Wirt (hier: 3ds Max) eine AELTERE
    # msvcp140.dll, stuerzt der erste lock() ab (mtx_do_lock). Microsoft nennt
    # genau diesen Schalter als Abhilfe; Firefox, OpenCPN und pybind11 setzen
    # ihn aus demselben Grund. Unser Fenster sperrt beim Start des
    # Arbeitsthreads - ohne den Schalter waere das die erste Absturzstelle.
    add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
endif()

set(SWBF2_LESER src/fbdump.h src/fbdump.cpp src/fbdatei.h src/fbdatei.cpp)

# ============================================================
#  Containerschicht: der Weg direkt ins Spiel
#
#  Entschleierung, cas.cat und die Blockentpackung. Braucht drei
#  fremde Entpacker, die unter vendor/ liegen - alle drei nur zum
#  ENTPACKEN, nie zum Packen, und alle drei mit freizuegiger
#  Lizenz (siehe vendor/HERKUNFT.md).
# ============================================================
set(SWBF2_CONTAINER
    src/fbcas.h src/fbcas.cpp
    src/fbdb.h src/fbdb.cpp
    src/fbgame.h src/fbgame.cpp
    src/fbbundle.h src/fbbundle.cpp
    src/fbindex.h src/fbindex.cpp
    src/fbmeshset.h src/fbmeshset.cpp
    src/fbebx.h src/fbebx.cpp
    src/fbgd.h src/fbgd.cpp
    src/fbgdwerte.h src/fbgdwerte.cpp
    src/fbanim.h src/fbanim.cpp
    src/fbgesicht.h src/fbgesicht.cpp
    src/fbzuordnung.h src/fbzuordnung.cpp
    src/fbfahrzeug.h src/fbfahrzeug.cpp
    src/fbfigur.h src/fbfigur.cpp
    src/fbteile.h src/fbteile.cpp
    src/fbdump.h src/fbdump.cpp
    src/fbdatei.h src/fbdatei.cpp
    src/fbauswahl.h src/fbauswahl.cpp
    src/fbbeipack.h src/fbbeipack.cpp
    src/fbtextur.h src/fbtextur.cpp
    src/fbmaterial.h src/fbmaterial.cpp
    vendor/lz4/lz4.c
    vendor/miniz/miniz.c vendor/miniz/miniz_tinfl.c
    vendor/miniz/miniz_tdef.c vendor/miniz/miniz_zip.c
    vendor/zstd/zstddeclib.c)

# Fehlt eine der fremden Quellen, soll das JETZT auffallen und nicht erst
# beim Binden mit acht unaufgeloesten Symbolen.
foreach(_q IN LISTS SWBF2_CONTAINER)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_q}")
        message(FATAL_ERROR "Quelle fehlt: ${_q}\n"
                            "Das ZIP ist unvollstaendig entpackt worden.")
    endif()
endforeach()

add_library(fbcontainer STATIC ${SWBF2_CONTAINER})
target_include_directories(fbcontainer PUBLIC
    src vendor/lz4 vendor/miniz vendor/zstd vendor/bcdec)
if(MSVC)
    target_compile_options(fbcontainer PRIVATE /EHsc /utf-8 /bigobj)
    target_compile_definitions(fbcontainer PRIVATE _CRT_SECURE_NO_WARNINGS)
    # Die fremden Entpacker werden NICHT mit /W4 geprueft - ihre Warnungen
    # sind nicht unsere, und sie zu unterdruecken ist ehrlicher, als sie
    # zu ignorieren.
    set_source_files_properties(
        vendor/lz4/lz4.c vendor/miniz/miniz.c vendor/miniz/miniz_tinfl.c
        vendor/miniz/miniz_tdef.c vendor/miniz/miniz_zip.c vendor/zstd/zstddeclib.c
        PROPERTIES COMPILE_FLAGS "/W0")
else()
    set_source_files_properties(
        vendor/lz4/lz4.c vendor/miniz/miniz.c vendor/miniz/miniz_tinfl.c
        vendor/miniz/miniz_tdef.c vendor/miniz/miniz_zip.c vendor/zstd/zstddeclib.c
        PROPERTIES COMPILE_FLAGS "-w")
endif()

# ============================================================
#  Werkzeug -> fbdump.exe   (ohne Max-SDK)
# ============================================================
if(SWBF2_BUILD_TOOL)
    add_executable(meshtool tools/meshtool.cpp)
    target_link_libraries(meshtool PRIVATE fbcontainer)
    if(MSVC)
        target_compile_options(meshtool PRIVATE /EHsc /utf-8 /Zc:__cplusplus)
        if(SWBF2_STRICT)
            target_compile_options(meshtool PRIVATE /W4 /permissive-)
        endif()
        target_compile_definitions(meshtool PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(meshtool PRIVATE -Wall -Wextra)
    endif()
    set_target_properties(meshtool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin")

    add_executable(castool tools/castool.cpp tools/codeclab.cpp tools/bericht.cpp tools/meshprobe.cpp tools/teilbones.cpp tools/clipcheck.cpp)
    target_link_libraries(castool PRIVATE fbcontainer)
    if(MSVC)
        target_compile_options(castool PRIVATE /EHsc /utf-8 /Zc:__cplusplus)
        if(SWBF2_STRICT)
            target_compile_options(castool PRIVATE /W4 /permissive-)
        endif()
        target_compile_definitions(castool PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(castool PRIVATE -Wall -Wextra)
    endif()
    set_target_properties(castool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin")

    add_executable(fbdump ${SWBF2_LESER} tools/fbdumptool.cpp)
    # fbdumptool.cpp macht #include "fbdump.h" - die Datei liegt in src\.
    # Ohne diese Zeile findet der Compiler sie nicht (error C1083).
    target_include_directories(fbdump PRIVATE src)
    if(MSVC)
        target_compile_options(fbdump PRIVATE /EHsc /utf-8 /Zc:__cplusplus)
        if(SWBF2_STRICT)
            target_compile_options(fbdump PRIVATE /W4 /permissive-)
        endif()
        target_compile_definitions(fbdump PRIVATE
            _UNICODE UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(fbdump PRIVATE -Wall -Wextra)
    endif()
    set_target_properties(fbdump PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin")
endif()

# ============================================================
#  Plugin -> SWBF2Import.dlu
# ============================================================
if(SWBF2_BUILD_PLUGIN)

    if(NOT EXISTS "${3DSMAX_SDK_DIR}/include/max.h")
        message(FATAL_ERROR
            "3ds Max SDK nicht gefunden unter: ${3DSMAX_SDK_DIR}\n"
            "Entweder -D3DSMAX_SDK_DIR=... setzen oder das Plugin weglassen.")
    endif()

    # Die Containerschicht kommt jetzt als ganze Bibliothek herein (Index,
    # Figurenliste, Figur bauen) - fbdump.cpp steckt darin, deshalb hier nicht
    # noch einmal. Das Fenster ist reines Win32, die Vorlage liegt in der .rc.
    add_library(SWBF2Import SHARED
        src/swbf2import.h
        src/swbf2import_max.cpp
        src/swbf2import_dll.cpp
        src/swbf2import_fenster.h
        src/swbf2import_fenster.cpp
        src/swbf2import_animfenster.cpp
        src/swbf2import_res.h
        src/swbf2import.rc
        swbf2import.def)

    # Festes Ausgabeverzeichnis. Ohne das legt der VS-Generator die Datei je
    # nach Konfiguration mal hier, mal dort ab, und das Suchen danach wird zur
    # Fehlerquelle - siehe die .dlu.recipe weiter unten in START.bat.
    set_target_properties(SWBF2Import PROPERTIES
        SUFFIX ".dlu" PREFIX "" OUTPUT_NAME "SWBF2Import"
        RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin")

    target_include_directories(SWBF2Import PRIVATE src)
    # SDK-Header als "external" einbinden, sonst rauschen sie bei /W4.
    target_include_directories(SWBF2Import SYSTEM PRIVATE "${3DSMAX_SDK_DIR}/include")
    target_link_directories(SWBF2Import PRIVATE "${3DSMAX_SDK_DIR}/lib/x64/Release")

    # fbcontainer ohne /Zp8: zwischen Plugin und Bibliothek wandern nur eigene
    # Typen, und keiner braucht mehr als 8 Byte Ausrichtung - das prueft
    # fbauswahl.cpp per static_assert in einer Datei OHNE /Zp8.
    # ole32/uuid/shell32: Ordnerauswahl (IFileOpenDialog), dwmapi: dunkle Titelleiste.
    target_link_libraries(SWBF2Import PRIVATE fbcontainer
        core.lib maxutil.lib bmm.lib Geom.lib mesh.lib
        Maxscrpt.lib MNMath.lib Paramblk2.lib comctl32.lib
        ole32.lib uuid.lib shell32.lib dwmapi.lib uxtheme.lib)

    target_compile_definitions(SWBF2Import PRIVATE
        _UNICODE UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        # _wfopen und _snwprintf melden sonst C4996. Die _s-Fassungen gibt es
        # nur bei Microsoft; mit ihnen liesse sich der Quelltext nicht mehr
        # gegen die SDK-Attrappe uebersetzen, und genau die faengt uns die
        # Fehler ab, bevor du sie siehst.
        _CRT_SECURE_NO_WARNINGS)

    if(MSVC)
        # /Zp8 ist beim Max-SDK PFLICHT. Stimmt das Struct-Packing nicht,
        # liest das Plugin die SDK-Strukturen falsch aus - und zwar ohne
        # jede Fehlermeldung.
        target_compile_options(SWBF2Import PRIVATE
            /EHsc /Zp8 /bigobj /utf-8 /Zc:__cplusplus /external:W0)

        # /permissive- nur, wenn die SDK-Header es aushalten. Aeltere SDKs
        # scheitern daran selbst (strictStrings, rvalueCast). Statt eine
        # Jahresgrenze zu raten, wird es einmal ausprobiert; das Ergebnis
        # steht im Cache.
        set(_permissive_ok FALSE)
        if(SWBF2_STRICT)
            include(CheckCXXSourceCompiles)
            set(CMAKE_REQUIRED_INCLUDES "${3DSMAX_SDK_DIR}/include")
            set(CMAKE_REQUIRED_FLAGS    "/permissive- /Zp8 /EHsc /bigobj")
            set(CMAKE_REQUIRED_DEFINITIONS
                -D_UNICODE -DUNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
            set(CMAKE_REQUIRED_QUIET TRUE)
            check_cxx_source_compiles(
                "#include <max.h>\nint main() { return 0; }"
                SWBF2_SDK_LIKES_PERMISSIVE)
            unset(CMAKE_REQUIRED_INCLUDES)
            unset(CMAKE_REQUIRED_FLAGS)
            unset(CMAKE_REQUIRED_DEFINITIONS)
            unset(CMAKE_REQUIRED_QUIET)
            if(SWBF2_SDK_LIKES_PERMISSIVE)
                set(_permissive_ok TRUE)
            endif()
        endif()

        if(SWBF2_STRICT AND _permissive_ok)
            message(STATUS "SDK vertraegt /permissive- - Konformitaetspruefung an")
            target_compile_options(SWBF2Import PRIVATE /W4 /permissive- /wd4100 /wd4458)
        elseif(SWBF2_STRICT)
            message(STATUS "SDK-Header vertragen /permissive- nicht - nur /W4")
            target_compile_options(SWBF2Import PRIVATE /W4 /wd4100 /wd4458)
        else()
            target_compile_options(SWBF2Import PRIVATE /W3)
        endif()

        target_link_options(SWBF2Import PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    endif()
endif()

message(STATUS "SWBF2Import: Max ${MAX_VERSION}  Plugin=${SWBF2_BUILD_PLUGIN}  "
               "Werkzeug=${SWBF2_BUILD_TOOL}  Strict=${SWBF2_STRICT}  C++${CMAKE_CXX_STANDARD}")
