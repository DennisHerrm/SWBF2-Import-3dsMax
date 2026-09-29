// ============================================================
//  fbgesicht.h - die Gesichtspose einer Figur (0.44.0).
//
//  Gemessen (0.43.1 bei DH): der VisualUnlock der Figur traegt ein AntRef
//  "FacePoserLibrary" mit AssetGuid 02cbffe4-3719-4888-0000-000000000000.
//  Nach Frostys Regel (AntAsset.SafeGuid: ID = GUID aus den acht Bytes des
//  u64-Keys) ist das der Bank-Key e4ffcb0219378848 - dort liegt ein
//  FacePoseLibraryAsset "Heads_Anakin_01" mit BindPoseAsset,
//  FacePoseJointDofsAsset, 97 Posen. Das BindPoseAsset haelt (laut fbtools)
//  BindPoseQuaternionIndexList/-List, BindPoseTranslationIndexList/-List
//  und Scale-Listen - nur fuer die Joints, die abweichen.
//
//  Daraus wird eine Basispose (fbfigur::Basispose, lokale Lage je Bone)
//  gebaut, die der Import nach dem Skin anlegt. Die Joint-Indizes werden
//  als Indizes ins Skelett der Figur (Walrus, 248 Bones) gelesen; das wird
//  gemessen (Abstand zur Ruhelage) und im Protokoll ausgewiesen.
// ============================================================
#pragma once

#include "fbanim.h"
#include "fbfigur.h"
#include "fbgame.h"
#include "fbindex.h"

#include <string>
#include <vector>

namespace fbgesicht {

// GUID-Text -> Bank-Key (16 Hexzeichen): die ersten drei Gruppen byteweise
// gedreht (so stehen die Rohbytes im GUID), die ersten acht Rohbytes.
std::string KeyAusGuid(const std::string& guidText);

// Sucht im Figuren-Bundle den FacePoserLibrary-Verweis, loest ihn in den
// Baenken auf und baut aus dem BindPoseAsset die Basispose.
bool LiesGesichtspose(fbgame::Spiel& spiel, const fbindex::Index& idx, fbanim::Quelle& q,
                      const fbfigur::Modell& m, fbfigur::Basispose& aus);

} // namespace fbgesicht
