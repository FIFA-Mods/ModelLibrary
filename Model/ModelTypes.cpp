#include "ModelTypes.h"
#include "ModelTypeConversion.h"

Vector2::Vector2() { x = y = 0.0f; }

Vector2::Vector2(float X, float Y) { x = X; y = Y; }

void Vector2::Set(float X, float Y) { x = X; y = Y; }

float Vector2::SquareLength() const { return x * x + y * y; }

float Vector2::Length() const { return std::sqrt(SquareLength()); }

Vector2 &Vector2::Normalize() {
    float l = Length();
    if (l != 0.0f) *this /= Length();
    return *this;
}

Vector2 const &Vector2::operator+=(Vector2 const &o) { x += o.x; y += o.y; return *this; }

Vector2 const &Vector2::operator-=(Vector2 const &o) { x -= o.x; y -= o.y; return *this; }

Vector2 const &Vector2::operator*=(float f) { x *= f; y *= f; return *this; }

Vector2 const &Vector2::operator/=(float f) { x /= f; y /= f; return *this; }

float Vector2::operator[](unsigned int i) const { return (i == 1) ? y : x; }

bool Vector2::operator==(Vector2 const &other) const { return x == other.x && y == other.y; }

bool Vector2::operator!=(Vector2 const &other) const { return x != other.x || y != other.y; }

bool Vector2::Equal(Vector2 const &other, float epsilon) const { return std::abs(x - other.x) <= epsilon && std::abs(y - other.y) <= epsilon; }

Vector2 &Vector2::operator=(float f) { x = y = f; return *this; }

Vector2 const Vector2::SymMul(Vector2 const &o) { return Vector2(x * o.x, y * o.y); }

Vector2 operator+(Vector2 const &v1, Vector2 const &v2) { return Vector2(v1.x + v2.x, v1.y + v2.y); }

Vector2 operator-(Vector2 const &v1, Vector2 const &v2) { return Vector2(v1.x - v2.x, v1.y - v2.y); }

float operator*(Vector2 const &v1, Vector2 const &v2) { return v1.x * v2.x + v1.y * v2.y; }

Vector2 operator*(float f, Vector2 const &v) { return Vector2(f * v.x, f * v.y); }

Vector2 operator*(Vector2 const &v, float f) { return Vector2(f * v.x, f * v.y); }

Vector2 operator/(const Vector2 &v, float f) { return v * (1 / f); }

Vector2 operator/(Vector2 const &v, Vector2 const &v2) { return Vector2(v.x / v2.x, v.y / v2.y); }

Vector2 operator-(const Vector2 &v) { return Vector2(-v.x, -v.y); }

Vector3::Vector3() { x = y = z = 0.0f; }

Vector3::Vector3(float X, float Y, float Z) { x = X; y = Y; z = Z; }

void Vector3::Set(float X, float Y, float Z) { x = X; y = Y; z = Z; }

float Vector3::SquareLength() const { return x * x + y * y + z * z; }

float Vector3::Length() const { return std::sqrt(SquareLength()); }

Vector3 &Vector3::Normalize() {
    float l = Length();
    if (l != 0.0f) *this /= Length();
    return *this;
}

Vector3 &Vector3::NormalizeSafe() {
    float len = Length();
    if (len > 0.0f) *this /= len;
    return *this;
}

Vector3 const &Vector3::operator+=(Vector3 const &o) { x += o.x; y += o.y; z += o.z; return *this; }

Vector3 const &Vector3::operator-=(Vector3 const &o) { x -= o.x; y -= o.y; z -= o.z; return *this; }

Vector3 const &Vector3::operator*=(float f) { x *= f; y *= f; z *= f; return *this; }

Vector3 const &Vector3::operator/=(float f) {
    if (f == 0.0f) return *this;
    float invF = 1.0f / f;
    x *= invF; y *= invF; z *= invF;
    return *this;
}

float Vector3::operator[](unsigned int i) const {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    }
    return x;
}

float &Vector3::operator[](unsigned int i) {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    }
    return x;
}

bool Vector3::operator==(Vector3 const &other) const { return x == other.x && y == other.y && z == other.z; }

bool Vector3::operator!=(Vector3 const &other) const { return x != other.x || y != other.y || z != other.z; }

bool Vector3::Equal(Vector3 const &other, float epsilon) const {
    return std::abs(x - other.x) <= epsilon && std::abs(y - other.y) <= epsilon && std::abs(z - other.z) <= epsilon;
}

bool Vector3::operator<(Vector3 const &other) const { return x != other.x ? x < other.x : y != other.y ? y < other.y : z < other.z; }

Vector3 const Vector3::SymMul(Vector3 const &o) { return Vector3(x * o.x, y * o.y, z * o.z); }

Vector2 Vector3::ToVector2() const { return Vector2(x, y); }

Vector3 operator-(Vector3 const &v) { return Vector3(-v.x, -v.y, -v.z); }

Vector3 operator+(Vector3 const &v1, Vector3 const &v2) { return Vector3(v1.x + v2.x, v1.y + v2.y, v1.z + v2.z); }

Vector3 operator-(Vector3 const &v1, Vector3 const &v2) { return Vector3(v1.x - v2.x, v1.y - v2.y, v1.z - v2.z); }

float operator*(Vector3 const &v1, Vector3 const &v2) { return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z; }

Vector3 operator*(float f, Vector3 const &v) { return Vector3(f * v.x, f * v.y, f * v.z); }

Vector3 operator*(Vector3 const &v, float f) { return Vector3(f * v.x, f * v.y, f * v.z); }

Vector3 operator/(Vector3 const &v, float f) { return v * (1 / f); }

Vector3 operator/(Vector3 const &v, Vector3 const &v2) { return Vector3(v.x / v2.x, v.y / v2.y, v.z / v2.z); }

Vector3 operator^(Vector3 const &v1, Vector3 const &v2) {
    return Vector3(v1.y * v2.z - v1.z * v2.y, v1.z * v2.x - v1.x * v2.z, v1.x * v2.y - v1.y * v2.x);
}

float Dot(const Vector3 &a, const Vector3 &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 Cross(const Vector3 &a, const Vector3 &b) {
    return Vector3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

Vector4::Vector4() { x = y = z = w = 0.0f; }

Vector4::Vector4(float X, float Y, float Z, float W) { x = X; y = Y; z = Z; w = W; }

void Vector4::Set(float X, float Y, float Z, float W) { x = X; y = Y; z = Z; w = W; }

float Vector4::operator[](unsigned int i) const {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    case 3: return w;
    }
    return x;
}

float &Vector4::operator[](unsigned int i) {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    case 3: return w;
    }
    return x;
}

Vector3 Vector4::ToVector3() const {
    return Vector3(x, y, z);
}

Quaternion::Quaternion() { x = y = z = 0.0f; w = 1.0f; }

Quaternion::Quaternion(float X, float Y, float Z, float W) { x = X; y = Y; z = Z; w = W; }

void Quaternion::Set(float X, float Y, float Z, float W) { x = X; y = Y; z = Z; w = W; }

float Quaternion::operator[](unsigned int i) const {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    case 3: return w;
    }
    return x;
}

float &Quaternion::operator[](unsigned int i) {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    case 3: return w;
    }
    return x;
}

Matrix4x4::Matrix4x4() {
    SetIdentity();
}

void Matrix4x4::SetIdentity() {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            m[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
}

Vector3 Matrix4x4::GetTranslation() const {
    return FromFbx(ToFbx(*this).GetT()).ToVector3();
}

void Matrix4x4::SetTranslation(Vector3 const &t) {
    FbxAMatrix fbxMat = ToFbx(*this);
    fbxMat.SetT(ToFbx(t));
    *this = FromFbx(fbxMat);
}

Vector3 Matrix4x4::GetScaling() const {
    return FromFbx(ToFbx(*this).GetS()).ToVector3();
}

void Matrix4x4::SetScaling(Vector3 const &s) {
    FbxAMatrix fbxMat = ToFbx(*this);
    fbxMat.SetS(ToFbx(s));
    *this = FromFbx(fbxMat);
}

Vector3 Matrix4x4::GetRotation() const {
    return FromFbx(ToFbx(*this).GetR()).ToVector3();
}

void Matrix4x4::SetRotation(Vector3 const &r) {
    FbxAMatrix fbxMat = ToFbx(*this);
    fbxMat.SetR(ToFbx(r));
    *this = FromFbx(fbxMat);
}

Quaternion Matrix4x4::GetQuaternion() const {
    return FromFbx(ToFbx(*this).GetQ());
}

void Matrix4x4::SetQuaternion(Quaternion const &q) {
    FbxAMatrix fbxMat = ToFbx(*this);
    fbxMat.SetQ(ToFbx(q));
    *this = FromFbx(fbxMat);
}

Matrix4x4 Matrix4x4::Identity() {
    Matrix4x4 result;
    result.SetIdentity();
    return result;
}

Matrix4x4 Matrix4x4::Inversed() const {
    return FromFbx(ToFbx(*this).Inverse());
}

Vector3 operator*(const Matrix4x4 &mat, const Vector3 &vec) {
    return FromFbx(ToFbx(mat).MultT(ToFbx(vec))).ToVector3();
}

Matrix4x4 operator*(const Matrix4x4 &a, const Matrix4x4 &b) {
    return FromFbx(ToFbx(a) * ToFbx(b));
}

RGBA::RGBA() { r = g = b = a = 255; }

RGBA::RGBA(unsigned char R, unsigned char G, unsigned char B, unsigned char A) { r = R; g = G; b = B; a = A; }

void RGBA::Set(unsigned char R, unsigned char G, unsigned char B, unsigned char A) { r = R; g = G; b = B; a = A; }

void RGBA::Set(float R, float G, float B, float A) {
    r = (unsigned char)(R * 255.0f); g = (unsigned char)(G * 255.0f);
    b = (unsigned char)(B * 255.0f); a = (unsigned char)(A * 255.0f);
}

void RGBA::Set(double R, double G, double B, double A) {
    r = (unsigned char)(R * 255.0); g = (unsigned char)(G * 255.0);
    b = (unsigned char)(B * 255.0); a = (unsigned char)(A * 255.0);
}

RGBA const &RGBA::operator+=(RGBA const &o) { r += o.r; g += o.g; b += o.b; a += o.a; return *this; }

RGBA const &RGBA::operator-=(RGBA const &o) { r -= o.r; g -= o.g; b -= o.b; a -= o.a; return *this; }

RGBA const &RGBA::operator*=(RGBA const &o) { r *= o.r; g *= o.g; b *= o.b; a *= o.a; return *this; }

RGBA const &RGBA::operator/=(RGBA const &o) { r /= o.r; g /= o.g; b /= o.b; a /= o.a; return *this; }

unsigned char RGBA::operator[](unsigned int i) const {
    switch (i) {
    case 0: return r;
    case 1: return g;
    case 2: return b;
    case 3: return a;
    }
    return r;
}

unsigned char &RGBA::operator[](unsigned int i) {
    switch (i) {
    case 0: return r;
    case 1: return g;
    case 2: return b;
    case 3: return a;
    }
    return r;
}

bool RGBA::operator==(RGBA const &other) const { return r == other.r && g == other.g && b == other.b && a == other.a; }

bool RGBA::operator!=(RGBA const &other) const { return r != other.r || g != other.g || b != other.b || a != other.a; }

bool RGBA::operator<(RGBA const &other) const {
    return r < other.r || (r == other.r && (g < other.g || (g == other.g && (b < other.b || (b == other.b && (a < other.a))))));
}

bool RGBA::IsBlack() const {
    static const float epsilon = 10e-3f;
    return std::fabs(r) < epsilon && std::fabs(g) < epsilon && std::fabs(b) < epsilon;
}

RGBA operator+(RGBA const &v1, RGBA const &v2) { return RGBA(v1.r + v2.r, v1.g + v2.g, v1.b + v2.b, v1.a + v2.a); }

RGBA operator-(RGBA const &v1, RGBA const &v2) { return RGBA(v1.r - v2.r, v1.g - v2.g, v1.b - v2.b, v1.a - v2.a); }

RGBA operator*(RGBA const &v1, RGBA const &v2) { return RGBA(v1.r * v2.r, v1.g * v2.g, v1.b * v2.b, v1.a * v2.a); }

RGBA operator/(RGBA const &v1, RGBA const &v2) { return RGBA(v1.r / v2.r, v1.g / v2.g, v1.b / v2.b, v1.a / v2.a); }

RGBA operator+(RGBA const &v, unsigned char f) { return RGBA(f + v.r, f + v.g, f + v.b, f + v.a); }

RGBA operator-(RGBA const &v, unsigned char f) { return RGBA(v.r - f, v.g - f, v.b - f, v.a - f); }

RGBA operator+(unsigned char f, RGBA const &v) { return RGBA(f + v.r, f + v.g, f + v.b, f + v.a); }

RGBA operator-(unsigned char f, RGBA const &v) { return RGBA(f - v.r, f - v.g, f - v.b, f - v.a); }
