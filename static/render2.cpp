#include <Bela.h>
#include <Keys.h>
#include <algorithm>
#include <cmath>
#include "KeyboardState.h"
extern float gKey;
extern float gPos;
extern float gGate;
extern float gPerc;
extern float gGain;
extern float gAux;
float gPercFlag;

float gAnalogInRead;

#define SCOPE
#define SCANNER
#define FILE_PLAYBACK
//#define LOOKUP

#ifdef SCOPE
#include <Scope.h>
Scope scope;
#endif /* SCOPE */
#include <Keys.h>
Keys* keys;
BoardsTopology bt;
int gKeyOffset = 46; //34
int gNumKeys = 24; // 38
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

const unsigned int attenuationStart = 60;
float slope = 50;

float getGain(float key)
{
	if(key < attenuationStart)
		return 1;
	return 1.f + 10.f*(key - attenuationStart)/slope;
}
float getMaxPressure(float key)
{
	if(key < attenuationStart)
		return 1;
	return 1.f - (key - attenuationStart)/slope;
}

#ifdef LOOKUP
typedef struct {
	float emb;
	float freq;
	float res;
} TuningData;

TuningData gTuning2Up[] = {
{1.0, 0, 60},
{1.05, 0, 60.2},
{1.1, 0, 60.39},
{1.15, 0, 60.56},
{1.2, 0, 60.71},
{1.25, 0.5, 60.86},
{1.3, 0, 61.0},
{1.25, 0.3, 61.16},
{1.2, 0.62, 61.33},
{1.15, 0.95, 61.5},
{1.1,  1.28, 61.66},
{1.05, 1.64, 61.83},
{1, 2, 62.0}
};

void getEmbFreq(int range, float idx, float& freq, float& emb)
{
	int sizeDen = sizeof(TuningData);
	TuningData* buf = NULL;
	int nrow = 0;
	switch (range){
		case 2:
			buf = gTuning2Up;
			nrow = sizeof(gTuning2Up)/sizeDen;
			break;
	}
	if(!buf)
	{
		freq = 0;
		emb = 0;
		return;
	}
	float i = (idx * (nrow - 1));
	int iint = (int)i;
	float ifrac = i - iint;
	// linearly interpolate between the values
	freq = buf[iint].freq * (1.f - ifrac) + buf[iint+1].freq * ifrac;
	emb = buf[iint].emb * (1.f - ifrac) + buf[iint+1].emb * ifrac;
	//rt_printf("nrow: %d, idx: %f, i: %f, iint: %d, ifrac: %f, emb: %f, freq: %f\n", 
		//nrow, idx, i, iint, ifrac, emb, freq);

}
#endif /* LOOKUP */

float positionToPressure(float idx)
{
	return idx;
}

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
	printf("Bela__user_settings2\n");
}

void postCallback(void* arg, float* buffer, unsigned int length){
	//Keys* keys = (Keys*)arg;
	int firstKey = gKeyOffset;
	int lastKey = gKeyOffset + gNumKeys + 1;
	unsigned int key = 46;
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
	//scope.log(buffer[59], buffer[60], buffer[61]);
	keyboardState.render(buffer, keyPositionTrackers, firstKey, lastKey);
	float pressure = keyboardState.getPosition();
	float expo = gAnalogInRead * 2.f + 0.5;
	if(pressure <= 0)
		pressure = 0;
	else if(pressure <= 1)
		pressure = powf(pressure, expo);
	else if(pressure > 1)
		pressure = powf(pressure, 2); // fixed curve for aftertouch
	pressure = pressure > 1 ? 1 : pressure; // TODO: do proper key-bottom calibration and remove this one

	float bendFreq = 0;
	static float bendEmbouchureOffset = 0;
	float idx = 0;
	bool highPressure = false;
	if(0)
	{
		// basic bending
		bendFreq = keyboardState.getBend();
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

		static int embClipped = 0;
		static int bendState = kBendStateLow;
		static float transitioningEmbouchureOffset = 0;
		static float transitionStartEmb;
		static float transitionStartIdx;
		static float lowStartEmb;
		static float lowStartIdx;
		static int bendStateHighKey;
		static float bendStateHighKeyInitialPos;
		if(bendState == kBendStateHigh)
		{
			if(buffer[bendStateHighKey] < bendStateHighToLowThreshold)
			{
				bendState = kBendStateLow;
				rt_fprintf(stderr, "bendState reset to low\n");
				lowStartEmb = 0;
				lowStartIdx = 0;
			}
			embClipped = 0;
		}

		float alpha = 0.99;
		static float embNormLeaky = 0;
		float bendRange = keyboardState.getBendRange(); //positive (bending up) or negative (bending down)
		if(std::abs(bendRange) < 0.05) {
			bendEmbouchureOffset = 0;
			lowStartEmb = 0;
			lowStartIdx = 0;
		} else {
			float embouchureRange = bendRange > 0 ? maxEmbouchure : minEmbouchure;
			//rt_printf("bendRange: %f keyboardState.getBend(): %f\n", bendRange, keyboardState.getBend());
			idx = keyboardState.getBend() / bendRange; // normalized current bend, always positive
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
			embNormalized = embNormalized > 1 ? (++embClipped, 1) : (--embClipped, embNormalized);

			embNormLeaky = embNormalized * (1.f-alpha) + embNormLeaky * alpha; 
			//rt_printf("leaky: %.5f\n", embNormLeaky);
			if(embNormLeaky > 0.9)
			{
				if(kBendStateLow == bendState)
				{
					bendState = kBendStateTransitioning;
					embNormalized = sqrtf(embNormalized);
					//bendEmbouchureOffset = embNormalized * embouchureRange;
#ifdef LOOKUP
					transitionStartEmb = emb;
#else /* LOOKUP */
					transitionStartEmb = bendEmbouchureOffset;
#endif /* LOOKUP */
					transitionStartIdx = idx;
					rt_fprintf(stderr, "bendState from low to transitioning\n");
					rt_fprintf(stderr, "transitionStartEmb: %.4f, transitionStartIdx: %.4f\n", transitionStartEmb, transitionStartIdx);
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
					rt_fprintf(stderr, "bendState from transitioning to low\n");
					rt_fprintf(stderr, "lowStartEmb: %.4f, lowStartIdx: %.4f\n", lowStartEmb, lowStartIdx);
				}
			}

			if(kBendStateLow == bendState)
			{
				embNormalized = sqrtf(embNormalized);
				bendEmbouchureOffset = embNormalized * embouchureRange;
				bendEmbouchureOffset = map(idx, lowStartIdx, 1, lowStartEmb, 0);
				bendEmbouchureOffset = constrain(bendEmbouchureOffset, -0.8, 1);
#ifdef LOOKUP
				bendEmbouchureOffset = emb;
				bendFreq = freq;
#endif /* LOOKUP */
				//rt_printf("ha idx: %f, bendEmbouchureOffset: %f\n", idx, bendEmbouchureOffset);
			} else if(kBendStateTransitioning == bendState) {
				// keep ramping the embouchure, till we get to the high octave
				float transitionMaxEmbIdx = 0.97;
				transitioningEmbouchureOffset = map(idx, transitionStartIdx, transitionMaxEmbIdx, transitionStartEmb, bendRange > 0 ? 1 : -0.995);
				transitioningEmbouchureOffset = constrain(transitioningEmbouchureOffset, -1, 1);

				// only allow StateHigh when bending up
				if(idx > transitionMaxEmbIdx && bendRange > 0)
				{
					transitioningEmbouchureOffset = 1;
					bendState = kBendStateHigh;
					bendStateHighKey = keyboardState.getOtherKey();
					bendStateHighKeyInitialPos = buffer[bendStateHighKey];
					rt_fprintf(stderr, "bendState from transitioning to high (on %d)\n", bendStateHighKey);
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
	float maxPressureAtLow = 1.3f/1.9f;
	// higher notes don't speak with too much pressure in one go. The easy way is to just rescale them
	// although it would be better TODO: limit the slew rate at high pitches
	maxPressureAtLow *= getMaxPressure(keyboardState.getKey());
	{
		float key  = keyboardState.getKey() + bendFreq;
		rt_printf("getMaxPressure: %f, getGain: %f\n", getMaxPressure(key), getGain(key));
	}
	const float pressureScaleSmoother = 0.9;
	float targetPressureScale = highPressure ? 1 : maxPressureAtLow;
	static float oldPressureScale;
	float pressureScale = targetPressureScale * (1.f - pressureScaleSmoother) + pressureScaleSmoother * oldPressureScale;
	oldPressureScale = pressureScale;
	float candidatePos = pressure * pressureScale;

	const float maxGain = 0.3;
	float aboveThreshold = constrain(candidatePos - maxPressureAtLow, 0, 1);
	float gainNormalized = (1.f - 1.3f*(sqrtf(aboveThreshold)));
	gainNormalized = constrain(gainNormalized, 0, 1);
	float realKey = keyboardState.getKey();
	gGain = maxGain * gainNormalized;
		
	gKey = realKey - gKeyOffset + bendFreq;

	//if(percEmbouchureOffset > 0.001)
		//rt_printf("percEmbouchureOffset = %.4f\n", percEmbouchureOffset);
	gAux = 1.f + bendEmbouchureOffset;
	if(0 && (count % 50 == 0))
		rt_printf("candidatePos: %.4f, key: %4d, other: %2d, idx: %.4f, emb: %.4f, freq: %.4f\n", candidatePos, keyboardState.getKey(), keyboardState.getOtherKey(), idx, gAux, bendFreq);
#if 0
	{
		// low/high mode state machine, triggered by fully pressing one key while releasing the other one
		enum {
		kStateLower,
		kStateWaitingForHigher,
		kStateHigher,
		kStateWaitingForLower,
		kStateReleased,
		};
		int key = keyboardState.getKey();
		static int boardState = kStateReleased;
		static int keyState;
		int oldBoardState = boardState;
		keyState = keyPositionTrackers[key].currentState();
		if(kPositionTrackerStateReleaseFinished == keyState 
			|| kPositionTrackerStateUnknown == keyState)
		{
			boardState = kStateReleased;
		}
		if(kStateLower == boardState)
		{
			if(kPositionTrackerStateReleaseInProgress == keyState)
			{
				boardState = kStateWaitingForHigher;
			}
		} else if(kStateWaitingForHigher == boardState)
		{
			if(kPositionTrackerStatePartialPressAwaitingMax == keyState 
			|| kPositionTrackerStatePartialPressFoundMax == keyState
			|| kPositionTrackerStatePressInProgress == keyState
			|| kPositionTrackerStateDown == keyState
			) {
				boardState = kStateHigher;
			}
		} else if(kStateHigher == boardState)
		{
			if(kPositionTrackerStateReleaseInProgress == keyState)
			{
				boardState = kStateWaitingForLower;
			}
		} else if(kStateWaitingForLower == boardState)
		{
			if(kPositionTrackerStatePartialPressAwaitingMax == keyState 
			|| kPositionTrackerStatePartialPressFoundMax == keyState
			|| kPositionTrackerStatePressInProgress == keyState
			|| kPositionTrackerStateDown == keyState
			) {
				boardState = kStateLower;
			}
		} else if(kStateReleased == boardState)
		{
			if(kPositionTrackerStatePartialPressAwaitingMax == keyState 
			|| kPositionTrackerStatePartialPressFoundMax == keyState
			|| kPositionTrackerStatePressInProgress == keyState
			|| kPositionTrackerStateDown == keyState
			) {
				boardState = kStateLower;
			}
		}

		static int latestHiLo = kStateLower;
		if(kStateLower == boardState
			|| kStateHigher == boardState)
		{
			latestHiLo = boardState;
		}
		if(boardState != oldBoardState)
		{
			rt_printf("%d (from %d)\n", boardState, oldBoardState);
		}
		if(kStateHigher == latestHiLo)
		{
			gAux = 2;
		} else if(kStateLower == latestHiLo){
			gAux = 1;
		}
	}
#endif
#if 0
	{ // after state
		enum {
			kAfterStateOff,
			kAfterStateOn
		}
		static int afterState = kAfterStateOff;
		static int keyState;
		static int oldKeyState;
		int key = keyboardState.getKey();
		keyState = keyPositionTrackers[key].currentState();
		static float afterStartPos = 0;


		int oldAfterState = a
		if(kPositionTrackerStateDown == keyState
			&& kPositionTrackerStateDown != oldKeyState)
		{
			afterState = kAfterStateOn;
			afterStartPos = keyboardState.getPosition();
		}
		if(kPositionTrackerStateDown != keyState
			&& kPositionTrackerStateDown == oldKeyState)
		{
			afterState = kAfterStateOff;
		}
	
		if(
		oldKeyState = keyState;
	}
#endif

	static float lastPerc = 0;
	float newPerc = keyboardState.getPercussiveness();
	if(newPerc != lastPerc && newPerc)
	{
		gPerc = newPerc;
		gPerc *= 20.f;
		gPerc *= gPerc;
#ifdef FILE_PLAYBACK
		static int nextBuffer;
		rt_printf("nextBuffer: %d\n\n", nextBuffer);
		gStartPlay = nextBuffer;
		static int thisBuffer = 0;
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
	gPerc *= 0.95f;
	lastPerc = newPerc;
	float posThreshold = 0.13;
	if(candidatePos < posThreshold)
		candidatePos = 0;
	else 
		candidatePos = (candidatePos - posThreshold) / (1.f - posThreshold);
	static float pastPos = 0;

	// avoid clicks:
	const float clickThreshold = 0.07;
	const float clickSmoothAlpha = 0.92
	if(std::abs(candidatePos - pastPos) > clickThreshold)
		gPos = gPos * clickSmoothAlpha + candidatePos * (1.f - clickSmoothAlpha);
	else
		gPos = candidatePos;
	pastPos = gPos;
	gGate = 1;
	//printing
	{
		static int count = 0;
		count++;
		int newKey = keyboardState.getKey();
		int newOtherKey = keyboardState.getOtherKey();
		static int oldKey = 0;
		static int oldOtherKey = 0;
		if( (newKey != oldKey && newKey != 0)
&& (newOtherKey != oldOtherKey && newOtherKey != 0)
				)
		{
			if(0)
			rt_printf("perc: %6.4f, bend: %6.3f (%3d at %6.3f %50s), key: %3d, %6.3f, %s\n",
					gPerc,
					keyboardState.getBend(),
					keyboardState.getOtherKey(),
					keyboardState.getOtherPosition(),
					statesDesc[keyPositionTrackers[keyboardState.getOtherKey()].currentState()].c_str(),
					keyboardState.getKey(),
					keyboardState.getPosition(),
					statesDesc[keyPositionTrackers[keyboardState.getKey()].currentState()].c_str()
				 );
			count = 0;
		}
		oldKey = newKey;
		oldOtherKey = newOtherKey;
	}
	return;
}

bool setup2(BelaContext *context, void *userData)
{
	printf("Setup2\n");
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
	gAnalogInRead = analogRead(context, 0, 0);
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
}

void cleanup2(BelaContext *context, void *userData)
{
#ifdef SCANNER
	delete keys;
#endif /* SCANNER */

}
