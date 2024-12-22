#pragma once

#define PI 3.1415926535f

struct Vector4
{
	Vector4() {}

	Vector4(float inX, float inY, float inZ, float inW) : x(inX), y(inY), z(inZ), w(inW) {}

	float x{ 0.f };
	float y{ 0.f };
	float z{ 0.f };
	float w{ 0.f };
};


struct Color4
{
	Color4(){}

	Color4(float inR, float inG, float inB, float inA) : r(inR), g(inG), b(inB), a(inA) {}

	float r{ 0.f };
	float g{ 0.f };
	float b{ 0.f };
	float a{ 0.f };
};
