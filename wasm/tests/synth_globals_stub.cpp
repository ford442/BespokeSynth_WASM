#include <algorithm>
#include <cmath>
#include <random>

int gSampleRate = 44100;
double gInvSampleRateMs = 1000.0 / 44100.0;
double gSampleRateMs = 44.1;

float Bias(float value, float bias)
{
   bias = std::max(0.0001f, std::min(0.9999f, bias));
   return value / ((((1.0f / bias) - 2.0f) * (1.0f - value)) + 1.0f);
}

float ofClamp(float value, float min, float max)
{
   return std::max(min, std::min(max, value));
}

float ofRandom(float max)
{
   return max * 0.5f;
}

float ofRandom(float x, float y)
{
   return x + (y - x) * 0.5f;
}
