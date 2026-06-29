#pragma once
#include "ModelTypes.h"
#include "ModelFbxHeader.h"

FbxDouble2 ToFbx(Vector2 const &v);
Vector2 FromFbx(FbxDouble2 const &v);
FbxDouble3 ToFbx(Vector3 const &v);
Vector3 FromFbx(FbxDouble3 const &v);
FbxDouble4 ToFbx(Vector4 const &v);
Vector4 FromFbx(FbxDouble4 const &v);
FbxDouble4 ToFbx(RGBA const &v);
FbxAMatrix ToFbx(Matrix4x4 const &mat);
Matrix4x4 FromFbx(FbxAMatrix const &mat);
FbxQuaternion ToFbx(Quaternion const &mat);
Quaternion FromFbx(FbxQuaternion const &mat);
