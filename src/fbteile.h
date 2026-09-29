// ============================================================
//  fbteile.h - Teile eines Composite-MeshSets an Bones binden (1.41.0).
//
//  Gemessen am AT-AT (castool --meshprobe / --teilbones):
//   * vehicle_ground_at-at_sp_mesh ist ein COMPOSITE-MeshSet (meshTypeId 2)
//     mit 67 Teilen. Die "BoneIndices" im Vertex sind TEILNUMMERN, keine
//     Plaetze in der boneList - die boneList der Section nennt nur, welche
//     Teile in ihr vorkommen (Kopf: 0 26 27 52 53 55..66). Frosty
//     (FBXExporter.cs) gibt Composite-Vertices ebenso Gewicht 1 auf Teil
//     boneIndices[0].
//   * Welchen Bone ein Teil bewegt, steht NICHT im MeshSet, sondern im
//     Fahrzeug-Blueprint: MultiBodyPhysicsComponentData.Parts[i].TransformNode
//     zeigt auf ein PartComponentData, das als Kind (Components) unter einem
//     BoneComponentData haengt. Die BoneComponentData-Kette ergibt die
//     Weltlage des Bones - 109 von 117 liegen exakt (0,0 mm, 0,00 Grad) auf
//     einem Bone der ModelPose; die uebrigen sind Zwischenglieder der
//     Halsringe ohne eigenen Skelett-Bone (0,14..0,55 m, 0 Grad).
//   * Das Blueprint wird ueber den Mesh-Import gefunden: ein Objekt mit
//     IMPORT Mesh <Datei-GUID des Mesh-EBX> und Parts in passender Zahl.
//   * Die Vertices liegen im MODELLRAUM (auch beim AT-ST, dessen Teile
//     Lagen tragen: Schwerpunkte 0,04..0,4 m neben der Teillage, Teile mit
//     Identitaet 1,4..6 m vom Ursprung) - die Teillagen sind nur Drehpunkte.
//
//  Kein SDK, reine Rechnerei: laeuft im Arbeitsthread und in castool.
// ============================================================
#pragma once
#include "fbgame.h"
#include "fbindex.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fbteile {

struct Zuordnung {
    std::vector<int> bone;                 // je Teil der Skelettindex, -1 = unbekannt
    std::string blueprint;                 // EBX, aus dem die Zuordnung stammt
    size_t exakt = 0;                      // Eltern-Bone < 2 mm und < 1 Grad
    size_t naeherung = 0;                  // naechster Bone, aber weiter weg
    size_t ohne = 0;                       // Teil ohne Eltern-BoneComponentData
    double groessterAbstand = 0.0;         // Meter, bei den Naeherungen
    std::vector<std::string> protokoll;
};

// boneWelt: Weltlage (Modellraum, Spielachsen) je Skelett-Bone als 4x3-
// Zeilenmatrix (right, up, forward, trans) - dieselbe Reihenfolge wie
// fbfigur::Bone::ruhelage, nur die Kette bereits aufmultipliziert.
// Liefert false, wenn kein passendes Blueprint gefunden wurde.
// boneEltern: Elternindex je Bone (-1 = Wurzel). Er loest Gleichstaende auf:
// beim AT-AT liegen die IK-Ziele *FutureFoot in der Ruhelage deckungsgleich
// auf den Fuessen - rein geometrisch hingen die Fuesse sonst an Bones, die
// in der Animation etwas anderes tun. Ein BoneComponentData unter einem
// anderen bekommt deshalb nur einen Nachfahren von dessen Bone.
bool TeileZuBones(fbgame::Spiel& spiel, const fbindex::Index& idx, const std::string& meshsetName, size_t teile,
                  const std::vector<std::array<double, 12>>& boneWelt, const std::vector<int>& boneEltern, Zuordnung& aus);

} // namespace fbteile
