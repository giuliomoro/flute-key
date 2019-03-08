#include <Bela.h>
#include <Keys.h>
#include <algorithm>
#include <cmath>
#include "KeyboardState.h"

// communicate to faust
extern float gKey;
extern float gPos;
extern float gNonLinearity;
extern float gGate;
extern float gPerc;
extern float gGain;
extern float gEmbRatio;

// communicate between keys thread and render
float gPercFlag;
float gAnalogIn0Read;
float gAnalogIn1Read;
float gAnalogIn2Read;
uint64_t gTimestamp;

#define SCOPE
#define SCANNER
#define FILE_PLAYBACK
#define LOOKUP
#define LOGGING

#ifdef LOOKUP
#include "tuning.h"
#endif /* LOOKUP */
#ifdef LOGGING
#include <WriteFile.h>
WriteFile gSensorFile;
WriteFile gAudioFile;
#endif /* LOGGING */
#ifdef SCOPE
#include <Scope.h>
Scope scope;
#endif /* SCOPE */
#include <Keys.h>
Keys* keys;
BoardsTopology bt;
int gKeyOffset = 34; //34
int gNumKeys = 38; // 38
#include "KeyPositionTracker.h"
#include "KeyboardState.h"
KeyboardState keyboardState;
KeyBuffers keyBuffers;
std::vector<KeyBuffer> keyBuffer;
std::vector<KeyPositionTracker> keyPositionTrackers;
#ifdef FILE_PLAYBACK
#include "SampleLoader.h"
std::vector<std::vector<float>> gPlaybackBuffers;
int gStartPlay = -1;
#endif /* FILE_PLAYBACK */

class DR
{
public:
	DR() {};
	DR(float sampleRate){setup(sampleRate);};
	~DR() {};
	void setup(float sampleRate)
	{
		sampleRate_ = sampleRate;
	}

// shape: small number such as 0.0001 to 0.01 for mostly-exponential, large numbers like 100 for virtually linear
	void start(float delayMs, float duration, float gain, float shape)
	{
		delay_ = delayMs/1000.f * sampleRate_;
		rate_ = expf(-logf((1 + shape) / shape) / (duration*sampleRate_));
		count_ = 0;
		value_ = 1;
		gain_ = gain;
		//rt_printf("delay: %d, rate: %f, count_: %d, value_: %f\n", delay_, rate_, count_, value_);
	}
	float render()
	{
		float ret;
		if(count_ < delay_)
		{
			ret = 0;
		} else {
			value_ *= rate_;
			ret = value_ * gain_;
		}
		++count_;
		return ret;
	}
private:
	float sampleRate_ = 1;
	unsigned int delay_;
	float rate_;
	unsigned int count_;
	float value_;
	float gain_;
};

//#define SINGLE_KEY
void Bela_userSettings2(BelaInitSettings* settings)
{
	settings->pruNumber = 0;	
	settings->useDigital = 0;
}

void postCallback(void* arg, float* buffer, unsigned int length){
	//Keys* keys = (Keys*)arg;
	int firstKey = gKeyOffset;
	int lastKey = gKeyOffset + gNumKeys + 1;
#ifdef SINGLE_KEY
	unsigned int key = 46;
#endif /* SINGLE_KEY */
	static int count;
	{
		for(unsigned int n = 0; n < length; ++n)
		{
			// INVERTING INPUTS
			buffer[n] = 1.f - buffer[n];
		}
		keyBuffers.postCallback(buffer, length, count);
		for(unsigned int n = 0; n < length; ++n)
		{
			if(n >= firstKey && n < lastKey)
			{
#ifdef SINGLE_KEY
				if(n == key)
#endif /* SINGLE_KEY */
				keyPositionTrackers[n].triggerReceived(count / 1000.f);
			}
		}
		count++;
	}
	keyboardState.setPositionCrossFadeDip(gAnalogIn1Read * 4.f);
	keyboardState.render(buffer, keyPositionTrackers, firstKey, lastKey);

	float bendFreq = 0;
	static float bendEmbouchureOffset = 0;
	float idx = 0;
	bool highPressure = false;
	static int bendState = 0;
	if(0)
	{
		// basic bending
		float bendRange = keyboardState.getBendRange(); //positive (bending up) or negative (bending down)
		if(bendRange)
		{
			bendFreq = powf(keyboardState.getBend()/bendRange, 1) * bendRange;
		} else {
			bendFreq = 0;
		}
		bendEmbouchureOffset = 0;
	} else {
		// improved bending, with embouchure
		enum {
			kBendStateLow,
			kBendStateTransitioning,
			kBendStateHigh
		};
		const float maxEmbouchure = 0.60;
		const float minEmbouchure = -0.28;
		const float freqRampStartIdx = 0.7;
		const float embPeakIdx = freqRampStartIdx;
		const float embStopIdx = 0.9;
		const float embClip = 1.2;
		const float bendStateHighToLowThreshold = 0.6;

		static float transitioningEmbouchureOffset = 0;
		static float transitionStartEmb;
		static float transitioningStartFreq;
		static float transitionStartIdx;
		static float lowStartEmb;
		static float lowStartIdx;
		static int bendStateHighKey;
		static float bendStateHighKeyInitialPos;
		if(kBendStateHigh == bendState)
		{
			if(buffer[bendStateHighKey] < bendStateHighToLowThreshold)
			{
				bendState = kBendStateLow;
				rt_fprintf(stderr, "%d bendState reset to low\n", count);
				lowStartEmb = 0;
				lowStartIdx = 0;
			}
		}

		float leakyAlpha = 0.99;
		float kLeakyLowToTransThreshold = 0.95;
		static float embNormLeaky = 0;
		float bendRange = keyboardState.getBendRange(); //positive (bending up) or negative (bending down)
		//if(std::abs(bendRange) < 0.05) {
			//bendEmbouchureOffset = 0;
			//lowStartEmb = 0;
			//lowStartIdx = 0;
		//} else {
		{
			float embouchureRange = bendRange > 0 ? maxEmbouchure : minEmbouchure;
			//rt_printf("bendRange: %f keyboardState.getBend(): %f\n", bendRange, keyboardState.getBend());
			if(bendRange)
				idx = keyboardState.getBend() / bendRange; // normalized current bend, always positive
			else
				idx = 0;
#ifdef LOOKUP
			float freq, emb;
			getEmbFreq(bendRange, idx, freq, emb);
#endif /* LOOKUP */
			bendFreq = bendRange * (idx < freqRampStartIdx ? 0 
				: (idx - freqRampStartIdx)/(1.f - freqRampStartIdx) * idx);
			float embNormalized = idx < embPeakIdx ?
				idx/embPeakIdx : (embStopIdx-idx)/(embStopIdx - embPeakIdx);
			embNormalized *= embClip;
			embNormalized = embNormalized < 0 ? 0 : embNormalized;
			embNormalized = embNormalized > 1 ? 1 : embNormalized;

			embNormLeaky = embNormalized * (1.f-leakyAlpha) + embNormLeaky * leakyAlpha;
			if(count % 20 == 0)
				rt_printf("%d_ leaky: %.5f\n", count, embNormLeaky);
			if(embNormLeaky > kLeakyLowToTransThreshold)
			{
				if(kBendStateLow == bendState)
				{
					bendState = kBendStateTransitioning;
					embNormalized = sqrtf(embNormalized);
					//bendEmbouchureOffset = embNormalized * embouchureRange;
#ifdef LOOKUP
					transitionStartEmb = emb;
					transitioningStartFreq = freq;
#else /* LOOKUP */
					transitionStartEmb = bendEmbouchureOffset;
#endif /* LOOKUP */
					transitionStartIdx = idx;

					rt_fprintf(stderr, "%d bendState from low to transitioning\n", count);
					//rt_fprintf(stderr, "transitionStartEmb: %.4f, transitionStartIdx: %.4f\n", transitionStartEmb, transitionStartIdx);
				}
			} else
			if(embNormLeaky < 0.3)
			{
				if(kBendStateTransitioning == bendState)
				{
					bendState = kBendStateLow;
					embNormalized = sqrtf(embNormalized);
					lowStartEmb = bendEmbouchureOffset;
					lowStartIdx = idx;
					rt_fprintf(stderr, "%d bendState from transitioning to low\n", count);
					//rt_fprintf(stderr, "lowStartEmb: %.4f, lowStartIdx: %.4f\n", lowStartEmb, lowStartIdx);
				}
			}

			if(kBendStateLow == bendState)
			{
#ifdef LOOKUP
				bendEmbouchureOffset = emb;
				bendFreq = freq;
#else /* LOOKUP */
				embNormalized = sqrtf(embNormalized);
				bendEmbouchureOffset = embNormalized * embouchureRange;
				bendEmbouchureOffset = map(idx, lowStartIdx, 1, lowStartEmb, 0);
				bendEmbouchureOffset = constrain(bendEmbouchureOffset, -0.8, 1);
#endif /* LOOKUP */
				//rt_printf("ha idx: %f, bendEmbouchureOffset: %f\n", idx, bendEmbouchureOffset);
			} else if(kBendStateTransitioning == bendState) {
				// keep ramping the embouchure, till we get to the high octave
				float transitionMaxEmbIdx = 0.97;
				bendFreq = transitioningStartFreq;
				transitioningEmbouchureOffset = map(idx, transitionStartIdx, transitionMaxEmbIdx, transitionStartEmb, bendRange > 0 ? 1 : -0.995);
				transitioningEmbouchureOffset = constrain(transitioningEmbouchureOffset, -1, 1);

				// only allow StateHigh when bending up
				if(idx > transitionMaxEmbIdx && bendRange > 0)
				{
					transitioningEmbouchureOffset = 1;
					bendState = kBendStateHigh;
					bendStateHighKey = keyboardState.getOtherKey();
					bendStateHighKeyInitialPos = buffer[bendStateHighKey];
					rt_fprintf(stderr, "%d bendState from transitioning to high (on %d)\n", count, bendStateHighKey);
				}
				bendEmbouchureOffset = transitioningEmbouchureOffset;
			}
		}
		if (kBendStateHigh == bendState) {
			bendStateHighKeyInitialPos = bendStateHighKeyInitialPos < buffer[bendStateHighKey] ? buffer[bendStateHighKey] : bendStateHighKeyInitialPos;
			bendEmbouchureOffset = map(buffer[bendStateHighKey], bendStateHighKeyInitialPos, bendStateHighToLowThreshold, 1, 0);
			bendEmbouchureOffset = constrain(bendEmbouchureOffset, 0, 1);
			highPressure = true;
		}
	}
	const float posThreshold = 0.13; // ignore values below this
	const float pressureRange = 1.4;
	const float pressureOffset = 0.5;
	const float absoluteMaxPressure = pressureRange + pressureOffset + 0.5;
	const float maxPressureAtLow = 1.3f / 1.9f;
	const float gainAtHighPressure = 0.2;
	// when we are in highPressure mode, we change the overall pressure range and compensate for the gain (smoothed by Faust)
	float pressureScale;
	float gain;
	if(highPressure)
	{
		pressureScale = 1;
		gain = gainAtHighPressure;
	} else {
		pressureScale = maxPressureAtLow;
		gain = 1;
	}

	float pressure = keyboardState.getPosition();
	float expo = gAnalogIn0Read * 2.f + 0.5;
	if(pressure <= 0)
		pressure = 0;
	else if(pressure <= 1)
		pressure = powf(pressure, expo); // some e
	else if(pressure > 1)
		pressure = powf(pressure, 6); // fixed curve for aftertouch, very steep
	float candidatePressure = pressure * pressureScale;

	if(candidatePressure < posThreshold)
		candidatePressure = 0;
	else
		candidatePressure = (candidatePressure - posThreshold) / (1.f - posThreshold);

	candidatePressure = candidatePressure * pressureRange + pressureOffset;
	candidatePressure = candidatePressure > absoluteMaxPressure ? absoluteMaxPressure : candidatePressure; // clip!

	// avoid clicks in the pressure, but also avoid smoothing always: smoothing always is for birds
	const float clickThreshold = 0.06;
	const float clickSmoothAlpha = 0.90;
	const float smoothingEndThreshold = 0.01;
	static bool isSmoothingPosition = false;
	bool isKeyBottom = candidatePressure > 0.95;
	float pastPos = gPos;
	if(!isSmoothingPosition && std::abs(candidatePressure - pastPos) > clickThreshold)
	{
		isSmoothingPosition = true;
	}
	if(!isKeyBottom && isSmoothingPosition && std::abs(candidatePressure - pastPos) < smoothingEndThreshold)
	{
		isSmoothingPosition = false;
	}
	if(isKeyBottom)
	{
		isSmoothingPosition = true;
	}

	if(isSmoothingPosition) {
		 gPos = gPos * clickSmoothAlpha + candidatePressure * (1.f - clickSmoothAlpha);
	} else {
		gPos = candidatePressure;
	}
	//gPos = 1.2; // use this to ignore the above and test tuning

	// handle percussiveness
	static float lastPerc = 0;
	float newPerc = keyboardState.getPercussiveness();
	if(newPerc != lastPerc && newPerc)
	{
		gPerc = newPerc;
		gPerc *= 16.f;
		gPerc *= gPerc;
#ifdef FILE_PLAYBACK
		static int nextBuffer;
		static int thisBuffer = 0;
		gStartPlay = nextBuffer;
		thisBuffer++;
		if(thisBuffer == 1)
		{
			nextBuffer++;
			thisBuffer = 0;
		}
		if(nextBuffer == gPlaybackBuffers.size())
			nextBuffer = 0;
#endif /* FILE_PLAYBACK */
	}
	lastPerc = newPerc;

	// set the other global variables
	gEmbRatio = 1.f + bendEmbouchureOffset;
	gGate = 1;
	const float maxGain = 0.3;
	gGain = maxGain * gain;
	gKey = fixTuning(keyboardState.getKey() + bendFreq + 12, gPos);
	// higher notes don't speak with too much pressure in one go, for high
	// nonLinearity values. Therefore, we adjust nonLinearity with the
	// frequency.
	gNonLinearity = getNonLinearity(gKey, gEmbRatio);

	float logs[] = {(float)gTimestamp, (float)count, gKey, gPos, gNonLinearity, gGate, gPerc, gGain, gEmbRatio, (float)bendState, gAnalogIn0Read, gAnalogIn1Read, gAnalogIn2Read};
	gSensorFile.log(logs, sizeof(logs)/sizeof(float));
	gSensorFile.log(buffer + firstKey, lastKey - firstKey + 1);
	return;
}

bool setup2(BelaContext *context, void *userData)
{
#ifdef LOGGING
	  gAudioFile.init("/root/audio_log.bin");
	  gAudioFile.setFileType(kBinary);
	  gSensorFile.init("/root/sensor_log.bin");
	  gSensorFile.setFileType(kBinary);
#endif /* LOGGING */

#ifdef FILE_PLAYBACK
	std::vector<std::string> files;
	for(unsigned int n = 4; n <= 6; ++n)
	//for(unsigned int n = 1; n <= 5; ++n)
	{
		std::string name = "spit" + std::to_string(n) + ".wav";
		files.push_back(name);
	}
	for(auto& file : files)
	{
		unsigned int numFrames = getNumFrames(file);
		gPlaybackBuffers.emplace_back(numFrames);
		getSamples(file, gPlaybackBuffers.back().data(), 0, 0, numFrames);
	}
#endif /* FILE_PLAYBACK */
#ifdef SCOPE
	scope.setup(2, 44100);
#endif /* SCOPE */
#ifdef SCANNER
	printf("Setting up scanner\n");
	if(context->digitalFrames != 0)
	{
		fprintf(stderr, "You should disable digitals and run on PRU 0 for the scanner to work.\n");
		return false;
	}

	keys = new Keys;
	bt.setLowestNote(0);
	bt.setBoard(0, 0, 24);
	bt.setBoard(1, 0, 23);
	bt.setBoard(2, 0, 23);
	int bottomKey = bt.getLowestNote();
	int topKey = bt.getHighestNote();
	int numKeys = topKey - bottomKey + 1;
	keyBuffers.setup(numKeys, 1000);
	keyBuffer.reserve(numKeys); // avoid reallocation in the loop below
	for(unsigned int n = 0; n < numKeys; ++n)
	{
		keyBuffer.emplace_back(
			keyBuffers.positionBuffer[n],
			keyBuffers.timestamps[n],
			keyBuffers.firstSampleIndex,
			keyBuffers.writeIdx
		);
		keyPositionTrackers.emplace_back(
				10, keyBuffer[n]
				);
		keyPositionTrackers.back().engage();
	}
	keyboardState.setup(numKeys);
	keys->setPostCallback(postCallback, keys);
	int ret = keys->start(&bt, NULL);
	if(ret < 0)
	{
		fprintf(stderr, "Error while starting the scan of the keys: %d %s\n", ret, strerror(-ret));
		delete keys;
		keys = NULL;
	}
	keys->startTopCalibration();
	keys->loadInverseSquareCalibrationFile("/root/out.calib", 0);
#endif /* SCANNER */
	return true;
}

unsigned int gSampleCount = 0;

DR dr(44100);
void render2(BelaContext *context, void *userData)
{
	gAnalogIn0Read = analogReadNI(context, 0, 0);
	gAnalogIn1Read = analogReadNI(context, 0, 1);
	gAnalogIn2Read = analogReadNI(context, 0, 2);
	float* audioIn = (float*)context->audioIn;
	for(unsigned int n = 0; n < context->audioFrames; ++n)
	{
		unsigned int channel = 0;
		float out = 0;
		// inject extra signal in the audio input 
#ifdef FILE_PLAYBACK
		static int readPtr;
		static int file = -1;
		if(gStartPlay >= 0)
		{
			file = gStartPlay;
			gStartPlay = -1;
			readPtr = 0;
		}
		if(file >= 0 )
		{
			out = 1.f*gPlaybackBuffers[file][readPtr++] * gPerc;
			if(readPtr == gPlaybackBuffers[file].size())
			{
				file = -1;
			}
		}
#endif /* FILE_PLAYBACK */
		
		audioIn[n + channel * context->audioFrames] = out;
	}
	if(gPercFlag)
		gPercFlag = 0;
}

void renderPost(BelaContext *context, void *userData)
{
	for(unsigned int f = 0; f < context->audioFrames; ++f)
	{
		unsigned int ch = 0;
		scope.log(context->audioOut[context->audioFrames * ch + f], gPos);
	}
	float timestamp = gTimestamp;
	gAudioFile.log(&timestamp, 1);
	gAudioFile.log(context->audioOut, context->audioFrames);
	gTimestamp += context->audioFrames;
}

void cleanup2(BelaContext *context, void *userData)
{
#ifdef SCANNER
	delete keys;
#endif /* SCANNER */

}
