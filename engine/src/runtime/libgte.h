#pragma once

#include <cstddef>

#include "core/types.h"
#include "psx/gte.h"

// PsyQ libgte, reimplemented on the software GTE. Each function issues the same
// register loads and GTE commands as Sony's library (PsyQ 4.2, the version the
// game links), so the GTE is left in the same state and side effects the
// original relies on survive: MulMatrix0, ApplyMatrix and CompMatrix all load
// their first matrix into the rotation matrix, for example. PsyQ's `long` is s32.
namespace ss::psyq {

struct MATRIX {
  s16 m[3][3];
  s32 t[3];
};
struct VECTOR {
  s32 vx, vy, vz, pad;
};
struct SVECTOR {
  s16 vx, vy, vz, pad;
};
struct CVECTOR {
  u8 r, g, b, cd;
};
struct DVECTOR {
  s16 vx, vy;
};
static_assert(offsetof(MATRIX, t) == 20 && sizeof(MATRIX) == 32, "MATRIX keeps PsyQ's layout");

// The console's GTE, shared by everything ported (one per runtime, like cop2).
psx::Gte& gte();

void InitGeom();

void SetRotMatrix(const MATRIX* m);
void SetLightMatrix(const MATRIX* m);
void SetColorMatrix(const MATRIX* m);
void SetTransMatrix(const MATRIX* m);
void SetBackColor(s32 rbk, s32 gbk, s32 bbk);  // each << 4 into BK
void SetFarColor(s32 rfc, s32 gfc, s32 bfc);   // each << 4 into FC
void SetGeomOffset(s32 ofx, s32 ofy);          // each << 16 into OFX/OFY
void SetGeomScreen(s32 h);
s32 ReadGeomScreen();  // H as cfc2 reads it: sign-extended
void SetFogNear(s32 a, s32 h);
void SetFogNearFar(s32 a, s32 b, s32 h);

// Perspective transforms. sxy outputs are packed DVECTORs (x low, y high); p is
// the depth-cue value IR0 and flag the FLAG register. The return value is SZ3 / 4
// (RotTransPers*) or OTZ (RotAverage*).
s32 RotTransPers(const SVECTOR* v0, s32* sxy, s32* p, s32* flag);
s32 RotTransPers3(const SVECTOR* v0, const SVECTOR* v1, const SVECTOR* v2, s32* sxy0, s32* sxy1, s32* sxy2, s32* p,
                  s32* flag);
s32 RotTransPers4(const SVECTOR* v0, const SVECTOR* v1, const SVECTOR* v2, const SVECTOR* v3, s32* sxy0, s32* sxy1,
                  s32* sxy2, s32* sxy3, s32* p, s32* flag);
void RotTrans(const SVECTOR* v0, VECTOR* v1, s32* flag);
s32 RotAverage3(const SVECTOR* v0, const SVECTOR* v1, const SVECTOR* v2, s32* sxy0, s32* sxy1, s32* sxy2, s32* p,
                s32* flag);
s32 RotAverage4(const SVECTOR* v0, const SVECTOR* v1, const SVECTOR* v2, const SVECTOR* v3, s32* sxy0, s32* sxy1,
                s32* sxy2, s32* sxy3, s32* p, s32* flag);
s32 NormalClip(s32 sxy0, s32 sxy1, s32 sxy2);
s32 AverageZ3(s32 sz0, s32 sz1, s32 sz2);
s32 AverageZ4(s32 sz0, s32 sz1, s32 sz2, s32 sz3);

// Lighting: v0 is a normal, v1 the material colour (RGBC), v2 the lit colour.
void LocalLight(const SVECTOR* v0, VECTOR* v1);
void NormalColor(const SVECTOR* v0, CVECTOR* v1);
void NormalColorCol(const SVECTOR* v0, const CVECTOR* v1, CVECTOR* v2);
void NormalColorCol3(const SVECTOR* v0, const SVECTOR* v1, const SVECTOR* v2, const CVECTOR* v3, CVECTOR* v4,
                     CVECTOR* v5, CVECTOR* v6);
void NormalColorDpq(const SVECTOR* v0, const CVECTOR* v1, s32 p, CVECTOR* v2);
void NormalColorDpq3(const SVECTOR* v0, const SVECTOR* v1, const SVECTOR* v2, const CVECTOR* v3, s32 p, CVECTOR* v4,
                     CVECTOR* v5, CVECTOR* v6);
void ColorCol(const VECTOR* v0, const CVECTOR* v1, CVECTOR* v2);
void ColorDpq(const VECTOR* v0, const CVECTOR* v1, s32 p, CVECTOR* v2);
void DpqColor(const CVECTOR* v0, s32 p, CVECTOR* v1);

// Matrix arithmetic in 4.12 fixed point.
VECTOR* ApplyMatrix(const MATRIX* m, const SVECTOR* v0, VECTOR* v1);      // v1 = m * v0 (MAC)
SVECTOR* ApplyMatrixSV(const MATRIX* m, const SVECTOR* v0, SVECTOR* v1);  // v1 = m * v0 (saturated IR)
VECTOR* ApplyMatrixLV(const MATRIX* m, const VECTOR* v0, VECTOR* v1);     // 32-bit input, split in two
MATRIX* MulMatrix0(const MATRIX* m0, const MATRIX* m1, MATRIX* m2);       // m2 = m0 * m1 (rotation only)
MATRIX* MulMatrix(MATRIX* m0, const MATRIX* m1);                          // m0 = m0 * m1
MATRIX* CompMatrix(const MATRIX* m0, const MATRIX* m1, MATRIX* m2);       // m2 = m0 * m1 with translation
MATRIX* RotMatrix(const SVECTOR* r, MATRIX* m);  // rotation X(r.vx) * Y(r.vy) * Z(r.vz), 4096 = 360 degrees
MATRIX* TransMatrix(MATRIX* m, const VECTOR* v);
MATRIX* ScaleMatrix(MATRIX* m, const VECTOR* v);  // column j scaled by v[j] / 4096
// m = R * m for a rotation about X / Y / Z by r (4096 = 360 degrees); every product is
// shifted down by 12 before the sum, as PsyQ's C versions do (field 0x8006e71c..).
MATRIX* RotMatrixX(s32 r, MATRIX* m);
MATRIX* RotMatrixY(s32 r, MATRIX* m);
MATRIX* RotMatrixZ(s32 r, MATRIX* m);
// v1 = (vx^2, vy^2, vz^2) through the GTE (gte_ldlvl / sqr0 / gte_stlvnl): each
// component is truncated to 16 bits on its way into IR, squared without shift.
VECTOR* Square0(const VECTOR* v0, VECTOR* v1);
void PushMatrix();                                 // saves RT and TR (20 deep)
void PopMatrix();

}  // namespace ss::psyq
