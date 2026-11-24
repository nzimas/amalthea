#pragma once
#include <array>
namespace nornios {
constexpr int QDelayValues=128;
int qdelayParameterCount();
const char *qdelayParameterName(int);
int qdelayParameterStep(int);
float qdelayParameterValue(int,int);
std::array<unsigned char,QDelayValues> defaultQDelayValues();
}
