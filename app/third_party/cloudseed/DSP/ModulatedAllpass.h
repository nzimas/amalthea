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

#include "ModulatedAllpass.h"
#include "Utils.h"
#include <cmath>

namespace Cloudseed
{
	class ModulatedAllpass
	{
	public:
		static const int DelayBufferSize = 4096; // Native 44.1kHz profile: diffusion delays <=70ms
		static const int ModulationUpdateRate = 8;

	private:
		float delayBuffer[DelayBufferSize] = { 0 };
		int index{};
		uint64_t samplesProcessed{};

		float modPhase{};
		int delayA{};
		int delayB{};
		float gainA{};
		float gainB{};

	public:

		int SampleDelay{};
		float Feedback{};
		float ModAmount{};
		float ModRate{};

		bool InterpolationEnabled{};
		bool ModulationEnabled{};

		ModulatedAllpass()
		{
			index = DelayBufferSize - 1;
			samplesProcessed = 0;

			modPhase = 0.618f;
			delayA = 0;
			delayB = 0;
			gainA = 0;
			gainB = 0;

			SampleDelay = 100;
			Feedback = 0.5;
			ModAmount = 0.0;
			ModRate = 0.0;

			InterpolationEnabled = true;
			ModulationEnabled = true;
			Update();
		}

		void ClearBuffers()
		{
			Utils::ZeroBuffer(delayBuffer, DelayBufferSize);
            index=DelayBufferSize-1;samplesProcessed=0;modPhase=0.618f;Update();
		}

		void Process(float* input, float* output, int sampleCount)
		{
			if (ModulationEnabled)
				ProcessWithMod(input, output, sampleCount);
			else
				ProcessNoMod(input, output, sampleCount);
		}

	private:
		void ProcessNoMod(float* input, float* output, int sampleCount)
		{
			auto delayedIndex = index - SampleDelay;
			if (delayedIndex < 0) delayedIndex += DelayBufferSize;

			for (int i = 0; i < sampleCount; i++)
			{
				auto bufOut = delayBuffer[delayedIndex];
				auto inVal = input[i] + bufOut * Feedback;

				delayBuffer[index] = inVal;
				output[i] = bufOut - inVal * Feedback;

				index++;
				delayedIndex++;
				if (index >= DelayBufferSize) index -= DelayBufferSize;
				if (delayedIndex >= DelayBufferSize) delayedIndex -= DelayBufferSize;
				samplesProcessed++;
			}
		}

		void ProcessWithMod(float* input,float* output,int sampleCount)
        {
            int at=0;
            while(at<sampleCount){
                if(samplesProcessed>=ModulationUpdateRate){Update();samplesProcessed=0;}
                int a=index-delayA,b=index-delayB;if(a<0)a+=DelayBufferSize;if(b<0)b+=DelayBufferSize;
                int n=std::min(sampleCount-at,int(ModulationUpdateRate-samplesProcessed));
                n=std::min(n,std::min(DelayBufferSize-index,std::min(DelayBufferSize-a,DelayBufferSize-b)));
                // A short delay needs sample-by-sample recurrence. Native profile
                // delays are longer than a block, but retain the general fallback.
                if(delayA<n || delayB<n)n=1;
                if(InterpolationEnabled)Utils::AllpassSpan(input+at,output+at,delayBuffer+index,delayBuffer+a,delayBuffer+b,n,gainA,gainB,Feedback);
                else Utils::AllpassSpan(input+at,output+at,delayBuffer+index,delayBuffer+a,delayBuffer+a,n,1,0,Feedback);
                index+=n;if(index>=DelayBufferSize)index-=DelayBufferSize;samplesProcessed+=n;at+=n;
            }
        }

		inline float Get(int delay)
		{
			int idx = index - delay;
			if (idx < 0)
				idx += DelayBufferSize;

			return delayBuffer[idx];
		}

		void Update()
		{
			modPhase += ModRate * ModulationUpdateRate;
			if (modPhase > 1)
				modPhase = std::fmod(modPhase, 1.0);

			auto mod = Utils::ModSine(modPhase);

			if (ModAmount >= SampleDelay) // don't modulate to negative value
				ModAmount = SampleDelay - 1;


			auto totalDelay = std::clamp(SampleDelay + ModAmount * mod, 1.0f, float(DelayBufferSize-2));

			if (totalDelay <= 0) // should no longer be required
				totalDelay = 1;

			delayA = (int)totalDelay;
			delayB = (int)totalDelay + 1;

			auto partial = totalDelay - delayA;

			gainA = 1 - partial;
			gainB = partial;
		}
	};
}
