#pragma once
#include <array>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <variant>
#include <filesystem>
#include <cmath>

struct Vector2 {
    float x, y;
    Vector2();
    Vector2(float X, float Y);
    void Set(float X, float Y);
    float SquareLength() const;
    float Length() const;
    Vector2 &Normalize();
    Vector2 const &operator+=(Vector2 const &o);
    Vector2 const &operator-=(Vector2 const &o);
    Vector2 const &operator*=(float f);
    Vector2 const &operator/=(float f);
    float operator[](unsigned int i) const;
    bool operator==(Vector2 const &other) const;
    bool operator!=(Vector2 const &other) const;
    bool Equal(Vector2 const &other, float epsilon) const;
    Vector2 &operator=(float f);
    Vector2 const SymMul(Vector2 const &o);
    std::string ToString() const;
    void FromString(std::string const &str);
};

Vector2 operator+(Vector2 const &v1, Vector2 const &v2);
Vector2 operator-(Vector2 const &v1, Vector2 const &v2);
float operator*(Vector2 const &v1, Vector2 const &v2);
Vector2 operator*(float f, Vector2 const &v);
Vector2 operator*(Vector2 const &v, float f);
Vector2 operator/(const Vector2 &v, float f);
Vector2 operator/(Vector2 const &v, Vector2 const &v2);
Vector2 operator-(const Vector2 &v);

struct Vector3 {
    float x, y, z;
    Vector3();
    Vector3(float X, float Y, float Z);
    void Set(float X, float Y, float Z);
    float SquareLength() const;
    float Length() const;
    Vector3 &Normalize();
    Vector3 &NormalizeSafe();
    Vector3 const &operator+=(Vector3 const &o);
    Vector3 const &operator-=(Vector3 const &o);
    Vector3 const &operator*=(float f);
    Vector3 const &operator/=(float f);
    float operator[](unsigned int i) const;
    float &operator[](unsigned int i);
    bool operator==(Vector3 const &other) const;
    bool operator!= (Vector3 const &other) const;
    bool Equal(Vector3 const &other, float epsilon) const;
    bool operator<(Vector3 const &other) const;
    Vector3 const SymMul(Vector3 const &o);
    Vector2 ToVector2() const;
    std::string ToString() const;
    void FromString(std::string const &str);
};

Vector3 operator-(Vector3 const &v);
Vector3 operator+(Vector3 const &v1, Vector3 const &v2);
Vector3 operator-(Vector3 const &v1, Vector3 const &v2);
float operator*(Vector3 const &v1, Vector3 const &v2);
Vector3 operator*(float f, Vector3 const &v);
Vector3 operator*(Vector3 const &v, float f);
Vector3 operator/(Vector3 const &v, float f);
Vector3 operator/(Vector3 const &v, Vector3 const &v2);
Vector3 operator^(Vector3 const &v1, Vector3 const &v2);

struct Vector4 {
    float x, y, z, w;
    Vector4();
    Vector4(float X, float Y, float Z, float W);
    void Set(float X, float Y, float Z, float W);
    float operator[](unsigned int i) const;
    float &operator[](unsigned int i);
    Vector3 ToVector3() const;
    std::string ToString() const;
    void FromString(std::string const &str);
};

struct Quaternion {
    float x, y, z, w;
    Quaternion();
    Quaternion(float X, float Y, float Z, float W);
    void Set(float X, float Y, float Z, float W);
    float operator[](unsigned int i) const;
    float &operator[](unsigned int i);
    std::string ToString() const;
    void FromString(std::string const &str);
};

struct Matrix4x4 {
    float m[4][4];

    Matrix4x4();
    void SetIdentity();
    Vector3 GetTranslation() const;
    void SetTranslation(Vector3 const &t);
    Vector3 GetScaling() const;
    void SetScaling(Vector3 const &s);
    Vector3 GetRotation() const;
    void SetRotation(Vector3 const &r);
    Quaternion GetQuaternion() const;
    void SetQuaternion(Quaternion const &q);
    static Matrix4x4 Identity();
    Matrix4x4 Inversed() const;
    std::string ToString() const;
    void FromString(std::string const &str);
};

Vector3 operator*(const Matrix4x4 &mat, const Vector3 &vec);
Matrix4x4 operator*(const Matrix4x4 &a, const Matrix4x4 &b);

struct RGBA {
    unsigned char r, g, b, a;
    RGBA();
    RGBA(unsigned char R, unsigned char G, unsigned char B, unsigned char A);
    void Set(unsigned char R, unsigned char G, unsigned char B, unsigned char A);
    void Set(float R, float G, float B, float A);
    void Set(double R, double G, double B, double A);
    RGBA const &operator+=(RGBA const &o);
    RGBA const &operator-=(RGBA const &o);
    RGBA const &operator*=(RGBA const &o);
    RGBA const &operator/=(RGBA const &o);
    unsigned char operator[](unsigned int i) const;
    unsigned char &operator[](unsigned int i);
    bool operator==(RGBA const &other) const;
    bool operator!=(RGBA const &other) const;
    bool operator<(RGBA const &other) const;
    bool IsBlack() const;
    std::string ToString() const;
    void FromString(std::string const &str);
};

RGBA operator+(RGBA const &v1, RGBA const &v2);
RGBA operator-(RGBA const &v1, RGBA const &v2);
RGBA operator*(RGBA const &v1, RGBA const &v2);
RGBA operator/(RGBA const &v1, RGBA const &v2);
RGBA operator+(RGBA const &v, unsigned char f);
RGBA operator-(RGBA const &v, unsigned char f);
RGBA operator+(unsigned char f, RGBA const &v);
RGBA operator-(unsigned char f, RGBA const &v);
