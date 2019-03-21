/*
  ==============================================================================

    Delay.cpp
    Created: 14 Mar 2019 7:31:38am
    Author:  Joonas

  ==============================================================================
*/

#include "Delay.h"

//==============================================================================
DelayEffect::DelayEffect(AudioProcessorValueTreeState& state)
	: mState(state)
{
	// Empty constructor
}

//==============================================================================
DelayEffect::~DelayEffect()
{
	// Emtyp destructor
}

//==============================================================================
void DelayEffect::prepare(dsp::ProcessSpec spec)
{
	mSampleRate = spec.sampleRate;
	mSamplesPerBlock = spec.maximumBlockSize;
	mNumChannels = spec.numChannels;
	mDelayBufferLen = 2 * (mSampleRate + mSamplesPerBlock);
	mDelayBuffer.setSize(mNumChannels, mDelayBufferLen, false, true);
	mDelayBuffer.clear();
	mDryBuffer.setSize(mNumChannels, mDelayBufferLen);
	mDryBuffer.clear();
}

//==============================================================================
void DelayEffect::reset()
{
	// Empty reset function
}

//==============================================================================
void DelayEffect::process(AudioBuffer<float>& buffer)
{
	float feedback = *mState.getRawParameterValue(IDs::feedback);
	float wet	   = *mState.getRawParameterValue(IDs::wetness);
	float time	   = *mState.getRawParameterValue(IDs::time);

	float FB = feedback / 100.f;
	float W  = wet / 100.f;
	float G  = 1 - W;

	// Copy input buffer to dry buffer
	for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
	{
		mDryBuffer.copyFromWithRamp(channel, 0, buffer.getReadPointer(channel), buffer.getNumSamples(), mLastG, G);
	}
	mLastG = G;

	// Write input buffer to delay buffer
	for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
	{
		fillDelayBuffer(buffer, channel);
	}

	// delay index
	float readPos = mWriteIndex - (time * (mSampleRate / 1000.f));
	if (readPos < 0)
		readPos += mDelayBufferLen;
	
	int readIndex = static_cast<int> (readPos);
	float fracRatio = readPos - readIndex;
	// Add delay into buffer
	for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
	{
		copyFromDelayBuffer(buffer, channel, readIndex, fracRatio);
	}

	// Add feedback form output buffer to delay buffer with gain FB
	for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
	{
		feedbackDelayBuffer(buffer, channel, mLastFB, FB);
	}
	mLastFB = FB;

	// Dry/wet
	buffer.applyGainRamp(0, buffer.getNumSamples(), mLastW, W);
	mLastW = W;

	// Add dry buffer to wet buffer
	for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
	{
		buffer.addFrom(channel, 0, mDryBuffer.getReadPointer(channel), buffer.getNumSamples());
	}

	// Advance delay buffer index
	mWriteIndex += buffer.getNumSamples();
	mWriteIndex %= mDelayBufferLen;
}

//==============================================================================
// Write input buffer to delay buffer
//
void DelayEffect::fillDelayBuffer(AudioBuffer<float>& buffer, const int& channel)
{
	if (mWriteIndex + buffer.getNumSamples() <= mDelayBufferLen)
	{
		mDelayBuffer.copyFrom(channel, mWriteIndex, buffer.getReadPointer(channel), buffer.getNumSamples());
	}
	else
	{
		// Samples remaining at the end of the delay buffer
		const int samplesRemaining = mDelayBufferLen - mWriteIndex;
		mDelayBuffer.copyFrom(channel, mWriteIndex, buffer.getReadPointer(channel), samplesRemaining);
		mDelayBuffer.copyFrom(channel, 0, buffer.getReadPointer(channel, samplesRemaining), buffer.getNumSamples() - samplesRemaining);
	}
}

//==============================================================================
void DelayEffect::copyFromDelayBuffer(AudioBuffer<float>& buffer, const int& channel, const int& readIndex, const float& fracRatio)
{
	if (readIndex + buffer.getNumSamples() <= mDelayBufferLen)
	{
		if (fracRatio == 0)
			buffer.copyFrom(channel, 0, mDelayBuffer.getReadPointer(channel, readIndex), buffer.getNumSamples());
		else
		{
			// Fractal delay
			buffer.copyFromWithRamp(channel, 0, mDelayBuffer.getReadPointer(channel, readIndex), buffer.getNumSamples(), fracRatio, fracRatio);
			buffer.addFromWithRamp(channel, 0, mDelayBuffer.getReadPointer(channel, readIndex - 1), buffer.getNumSamples(), 1 - fracRatio, 1 - fracRatio);
		}
	}
	else
	{
		// Samples remaining at the end of the delay buffer
		const int samplesRemaining = mDelayBufferLen - readIndex;

		if (fracRatio == 0)
		{
			buffer.copyFrom(channel, 0, mDelayBuffer.getReadPointer(channel, readIndex), samplesRemaining);
			buffer.copyFrom(channel, samplesRemaining, mDelayBuffer.getReadPointer(channel), buffer.getNumSamples() - samplesRemaining);
		}
		else
		{
			// Fractal delay
			buffer.copyFromWithRamp(channel, 0, mDelayBuffer.getReadPointer(channel, readIndex - 1), samplesRemaining + 1, 1 - fracRatio, 1 - fracRatio);
			buffer.addFromWithRamp(channel, 0, mDelayBuffer.getReadPointer(channel, readIndex), samplesRemaining, fracRatio, fracRatio);

			buffer.copyFromWithRamp(channel, samplesRemaining + 1, mDelayBuffer.getReadPointer(channel), buffer.getNumSamples() - samplesRemaining - 1, 1 - fracRatio, 1 - fracRatio);
			buffer.addFromWithRamp(channel, samplesRemaining, mDelayBuffer.getReadPointer(channel), buffer.getNumSamples() - samplesRemaining, fracRatio, fracRatio);
		}
	}
}

//==============================================================================
// Add feedback to delay buffer
//
void DelayEffect::feedbackDelayBuffer(AudioBuffer<float>& buffer, const int & channel, const float & startGain, const float & endGain)
{
	if (mWriteIndex + buffer.getNumSamples() <= mDelayBufferLen)
	{
		mDelayBuffer.addFromWithRamp(channel, mWriteIndex, buffer.getWritePointer(channel), buffer.getNumSamples(), startGain, endGain);
	}
	else
	{
		// Samples remaining at the end of the delay buffer
		const int samplesRemaining = mDelayBufferLen - mWriteIndex;
		const float midGain = mLastFB + ((endGain - startGain) / mSamplesPerBlock) * (samplesRemaining / mSamplesPerBlock);
		mDelayBuffer.addFromWithRamp(channel, mWriteIndex, buffer.getWritePointer(channel), samplesRemaining, startGain, midGain);
		mDelayBuffer.addFromWithRamp(channel, 0, buffer.getWritePointer(channel, samplesRemaining), buffer.getNumSamples() - samplesRemaining, midGain, endGain);
	}
}