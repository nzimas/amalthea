/*
Copyright (c) 2024 Ghost Note Engineering Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#pragma once

#define _USE_MATH_DEFINES 1
#include <math.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdint.h>
#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace Cloudseed
{
    namespace Utils
    {
        // Four adjacent samples at once. Caller guarantees history cannot overlap
        // the current write span; small/zero delays retain the scalar path.
        inline void DelaySpan(const float *in,float *out,float *write,const float *a,const float *b,int n,float ga,float gb) {
            int i=0;
#if defined(__aarch64__) || defined(__ARM_NEON)
            for(;i+4<=n;i+=4){auto x=vld1q_f32(in+i);auto y=vaddq_f32(vmulq_n_f32(vld1q_f32(a+i),ga),vmulq_n_f32(vld1q_f32(b+i),gb));vst1q_f32(write+i,x);vst1q_f32(out+i,y);}
#endif
            for(;i<n;i++){float x=in[i],y=a[i]*ga+b[i]*gb;write[i]=x;out[i]=y;}
        }
        inline void AllpassSpan(const float *in,float *out,float *write,const float *a,const float *b,int n,float ga,float gb,float feedback) {
            int i=0;
#if defined(__aarch64__) || defined(__ARM_NEON)
            for(;i+4<=n;i+=4){auto y=vaddq_f32(vmulq_n_f32(vld1q_f32(a+i),ga),vmulq_n_f32(vld1q_f32(b+i),gb));auto w=vaddq_f32(vld1q_f32(in+i),vmulq_n_f32(y,feedback));auto o=vsubq_f32(y,vmulq_n_f32(w,feedback));vst1q_f32(write+i,w);vst1q_f32(out+i,o);}
#endif
            for(;i<n;i++){float y=a[i]*ga+b[i]*gb,w=in[i]+y*feedback;write[i]=w;out[i]=y-w*feedback;}
        }
        inline float ModSine(float phase) {
            static const std::array<float,2049> table=[] {
                std::array<float,2049> t{};
                for(int i=0;i<=2048;i++)t[i]=std::sin(float(i)*float(2*M_PI/2048));
                return t;
            }();
            float p=phase*2048.f;int i=int(p);float f=p-i;i&=2047;
            return table[i]+(table[i+1]-table[i])*f;
        }
        template<typename T>
        inline void ZeroBuffer(T* buffer, int len)
        {
            for (int i = 0; i < len; i++)
                buffer[i] = 0;
        }

        template<typename T>
        inline void Copy(T* dest, T* source, int len)
        {
            memcpy(dest, source, len * sizeof(T));
        }

        template<typename T>
        inline void Gain(T* buffer, T gain, int len)
        {
            for (int i = 0; i < len; i++)
            {
                buffer[i] *= gain;
            }
        }

        template<typename T>
        inline void Mix(T* target, T* source, T gain, int len)
        {
            for (int i = 0; i < len; i++)
                target[i] += source[i] * gain;
        }

        inline float DB2Gainf(float input)
        {
            //return std::pow(10.0f, input / 20.0f);
            return powf(10, input * 0.05f);
        }

        template<typename T>
        inline double DB2Gain(T input)
        {
            return pow10f(input / 20.0);
        }

        template<typename T>
        inline double Gain2DB(T input)
        {
            //if (input < 0.0000001)
            //    return -100000;

            return 20.0f * log10f(input);
        }

        const float dec1Mult = (10 / 9.0) * 0.1;
        const float dec2Mult = (100 / 99.0) * 0.01;
        const float dec3Mult = (1000 / 999.0) * 0.001;
        const float dec4Mult = (10000 / 9999.0) * 0.0001;

        const float oct1Mult = (2 / 1.0) * 0.5;
        const float oct2Mult = (4 / 3.0) * 0.25;
        const float oct3Mult = (8 / 7.0) * 0.125;
        const float oct4Mult = (16 / 15.0) * 0.0625;
        const float oct5Mult = (32 / 31.0) * 0.03125;
        const float oct6Mult = (64 / 63.0) * 0.015625;
        const float oct7Mult = (128 / 127.0) * 0.0078125;
        const float oct8Mult = (256 / 255.0) * 0.00390625;

        inline float Resp1dec(float x) { return (powf(10, x) - 1) * dec1Mult; }
        inline float Resp2dec(float x) { return (powf(10, 2 * x) - 1) * dec2Mult; }
        inline float Resp3dec(float x) { return (powf(10, 3 * x) - 1) * dec3Mult; }
        inline float Resp4dec(float x) { return (powf(10, 4 * x) - 1) * dec4Mult; }

        inline float Resp1oct(float x) { return (powf(2, x) - 1) * oct1Mult; }
        inline float Resp2oct(float x) { return (powf(2, 2 * x) - 1) * oct2Mult; }
        inline float Resp3oct(float x) { return (powf(2, 3 * x) - 1) * oct3Mult; }
        inline float Resp4oct(float x) { return (powf(2, 4 * x) - 1) * oct4Mult; }
        inline float Resp5oct(float x) { return (powf(2, 5 * x) - 1) * oct5Mult; }
        inline float Resp6oct(float x) { return (powf(2, 6 * x) - 1) * oct6Mult; }
        inline float Resp7oct(float x) { return (powf(2, 7 * x) - 1) * oct7Mult; }
        inline float Resp8oct(float x) { return (powf(2, 8 * x) - 1) * oct8Mult; }
       
    }
}
