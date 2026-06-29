#include "ModelTypeConversion.h"

FbxDouble2 ToFbx(Vector2 const &v) { return FbxDouble2(v.x, v.y); }

Vector2 FromFbx(FbxDouble2 const &v) { return Vector2((float)v[0], (float)v[1]); }

FbxDouble3 ToFbx(Vector3 const &v) { return FbxDouble3(v.x, v.y, v.z); }

Vector3 FromFbx(FbxDouble3 const &v) { return Vector3((float)v[0], (float)v[1], (float)v[2]); }

FbxDouble4 ToFbx(Vector4 const &v) { return FbxDouble4(v.x, v.y, v.z, v.w); }

Vector4 FromFbx(FbxDouble4 const &v) { return Vector4((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }

FbxDouble4 ToFbx(RGBA const &v) {
    return FbxDouble4((double)v.r / 255.0, (double)v.g / 255.0, (double)v.b / 255.0, (double)v.a / 255.0);
}

FbxAMatrix ToFbx(Matrix4x4 const &mat) {
    FbxAMatrix result;
    for (unsigned int i = 0; i < 4; i++)
        result.mData[i] = FbxDouble4(mat.m[i][0], mat.m[i][1], mat.m[i][2], mat.m[i][3]);
    return result;
}

Matrix4x4 FromFbx(FbxAMatrix const &mat) {
    Matrix4x4 result;
    for (unsigned int i = 0; i < 4; i++) {
        for (unsigned int j = 0; j < 4; j++)
            result.m[i][j] = static_cast<float>(mat.mData[i][j]);
    }
    return result;
}

FbxQuaternion ToFbx(Quaternion const &q) { return FbxQuaternion(q.x, q.y, q.z, q.w); }

Quaternion FromFbx(FbxQuaternion const &q) { return Quaternion((float)q[0], (float)q[1], (float)q[2], (float)q[3]); }
