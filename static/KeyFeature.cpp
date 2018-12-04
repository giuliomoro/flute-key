#include "KeyFeature.h"
#include <algorithm>

float Percussiveness::process(std::vector<float>& positionBuffer, std::vector<float>&velocityBuffer, unsigned int idx)
{
	// if smaller than activation threshold, look back lookBack samples
	// to check when the press started

	// look for a rebound: the velocity changes direction above a threshold

	const unsigned int historyLength = 5;
	float pos[historyLength];
	float vel[historyLength];
	retrieveElementsFromRingBuffer(positionBuffer, pos, idx, historyLength);
	retrieveElementsFromRingBuffer(velocityBuffer, vel, idx, historyLength);
	float oldOldVelocity = vel[historyLength - 3];
	float oldVelocity = vel[historyLength - 2];
	float velocity = vel[historyLength - 1];

#if 1
	if(
		feature == 0 
		&& count - lastChange > holdoff
		&& oldOldVelocity <= oldVelocity
		&& oldVelocity >= oldVelocity
	  )
	{
		{
			float* min = std::min_element(vel, vel + historyLength);
			float* max = std::max_element(vel, vel + historyLength);
			float range = *max - *min;
			if(range > velThreshold)
			{
				//feature = *min;
				feature = (range/* - velThreshold * 0.5f*/) * 7.f;
				lastChange = count;
			}
			else
				;//feature = 0;
		}
	} else if (
		feature != 0
		&& pos[historyLength - 1] > releaseThreshold
		&& count - lastChange >= holdoff
	  )
	{
		feature = 0;
	}
#else
	if(
		feature == 0 
		&& count - lastChange > holdoff
		&& detectRebound(
	  )
	{
		{
			float* min = std::min_element(vel, vel + historyLength);
			float* max = std::max_element(vel, vel + historyLength);
			float range = *max - *min;
			if(range > velThreshold)
			{
				//feature = *min;
				feature = (range/* - velThreshold * 0.5f*/) * 7.f;
				lastChange = count;
			}
			else
				;//feature = 0;
		}
	} else if (
		feature != 0
		&& pos[historyLength - 1] > releaseThreshold
	  )
	{
		feature = 0;
	}

#endif

	++count;
	return feature;
}

// read count elements from ringBuffer, the last one being ringBuffer[idx]
void Percussiveness::retrieveElementsFromRingBuffer(std::vector<float>& ringBuffer, float* outputBuffer, unsigned int idx, unsigned int count)
{
	int start = (int) idx - (int) count;
	unsigned int firstCount = 0;
	if(start < 0)
	{
		// read from the end of the buffer if appropriate
		firstCount = -start;
		// start is actually negative in here
		unsigned int firstStart = (int)ringBuffer.size() + start;
		for(unsigned int n = 0; n < firstCount; ++n)
			outputBuffer[n] = ringBuffer[firstStart + n];
		start = 0;
	}
	// whatever (if anything)  is left, read it from the beginning of the
	// ringBuffer
	for(unsigned int n = 0; n < count - firstCount; ++n)
	{
		outputBuffer[firstCount + n] = ringBuffer[start + n];
	}
}

void KeyFeature::postCallback(void* arg, float* buffer, unsigned int length)
{
	KeyFeature* that = (KeyFeature*)arg;
	that->postCallback(buffer, length);
}

void KeyFeature::postCallback(float* buffer, unsigned int length)
{
	for(unsigned int n = 0; n < std::min(numKeys, length); ++n)
	{
		float position = buffer[n];
		positionBuffer[n][writeIdx] = position;
		float oldPosition = writeIdx == 0 ? positionBuffer[n].back() : positionBuffer[n][writeIdx - 1];
		float velocity = position - oldPosition;
		velocityBuffer[n][writeIdx] = velocity;
		percussiveness[n].process(positionBuffer[n], velocityBuffer[n], writeIdx);
	}
	++writeIdx;
	if(writeIdx >= positionBuffer[0].size())
	{
		writeIdx = 0;
	}
}

bool KeyFeature::setup(unsigned int newNumKeys, unsigned int bufferLength)
{
	numKeys = newNumKeys;
	if(numKeys == 0 || bufferLength == 0)
		return false;
	percussiveness.resize(numKeys);
	positionBuffer.resize(numKeys);
	velocityBuffer.resize(numKeys);
	for(auto &p : positionBuffer)
		p.resize(bufferLength);
	for(auto &v : velocityBuffer)
		v.resize(bufferLength);
	return true;
}

