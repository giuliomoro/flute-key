#include <Keys.h>

class Percussiveness
{
public:
	float process(std::vector<float>& positionBuffer, std::vector<float>&velocityBuffer, unsigned int idx);
	float getFeature() { return feature; };
private:
	float feature;
	float velThreshold = 0.02;
	void retrieveElementsFromRingBuffer(std::vector<float>& ringBuffer, float* outputBuffer, unsigned int idx, unsigned int count);
	unsigned int count = 0;
	unsigned int lastChange = 0;
	static constexpr unsigned int holdoff = 60;
	static constexpr float releaseThreshold = 0.8;
};

class KeyFeature
{
public:
	bool setup(unsigned int newNumKeys, unsigned int bufferLength);
	void postCallback(float* buffer, unsigned int length);
	static void postCallback(void* arg, float* buffer, unsigned int length);
	std::vector<Percussiveness> percussiveness;
private:
	unsigned int numKeys;
	std::vector<std::vector<float>> positionBuffer;
	std::vector<std::vector<float>> velocityBuffer;
	unsigned int writeIdx;
};

