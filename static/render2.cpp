#include <Bela.h>
#include <Keys.h>
#include <algorithm>
#include <cmath>
#include "KeyboardState.h"
extern float gKey;
extern float gPos;
extern float gGate;
extern float gPerc;
extern float gAux;
float gPercFlag;

#define SCOPE
#define SCANNER
#define KEYPOSITIONTRACKER

#ifdef SCOPE
#include <Scope.h>
Scope scope;
#endif /* SCOPE */
#include <Keys.h>
Keys* keys;
BoardsTopology bt;
int gKeyOffset = 46; //34
int gNumKeys = 26; // 38
float gBendingKeyTwoThreshold = 0.3;
float gGatePosThresholdOn = 0.5;
float gGatePosThresholdOff = 0.1;
#ifdef KEYPOSITIONTRACKER
#include "KeyPositionTracker.h"
#include "KeyboardState.h"
KeyboardState keyboardState;
KeyBuffers keyBuffers;
std::vector<KeyBuffer> keyBuffer;
std::vector<KeyPositionTracker> keyPositionTrackers;
#endif /* KEYPOSITIONTRACKER */

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
#ifdef KEYPOSITIONTRACKER
	static int count;
	{
		for(unsigned int n = 0; n < length; ++n)
		{
			// INVERTING INPUTS
			buffer[n] = 1.f - buffer[n];
		}
		keyBuffers.postCallback(buffer, length);
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
	keyboardState.render(buffer, keyPositionTrackers, firstKey, lastKey);
	gKey = keyboardState.getKey() - gKeyOffset + keyboardState.getBend();
	gPos = keyboardState.getPosition();
	//another state machine
	{
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
			gAux = 1/8.f;
		} else if(kStateLower == latestHiLo){
			gAux = 0;
		}
	}
	static float lastPerc = 0;
	float newPerc = keyboardState.getPercussiveness();
	if(1)
	if(newPerc != lastPerc && newPerc)
	{
		gPerc = newPerc;
		gPerc *= 20.f;
		gPerc *= gPerc;
		gPercFlag = gPerc;
		rt_printf("%d]oldPerc: %f, newPerc: %f\n", count, newPerc, gPerc);
	}
	gPerc *= 0.99f;
	lastPerc = newPerc;
#ifndef SINGLE_KEY
	static float prevPos = 0;
	scope.log(
			buffer[keyboardState.getKey()],
			//(gKey - key + gKeyOffset) / 8.f,
			gPos - prevPos,
			//keyPositionTrackers[key].dynamicOnsetThreshold_,
			//newPerc,
			keyPositionTrackers[key].currentState() / (float) kPositionTrackerStateReleaseFinished,
			0,
			0
			);
	prevPos = gPos;
#endif /* SINGLE_KEY */
	float posThreshold = 0.13;
	if(gPos < posThreshold)
		gPos = 0;
	else 
		gPos = (gPos - posThreshold) / (1 - posThreshold);
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
#endif /* KEYPOSITIONTRACKER */
}

bool setup2(BelaContext *context, void *userData)
{
	printf("Setup2\n");
#ifdef SCOPE
	scope.setup(8, 1000);
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
#ifdef KEYPOSITIONTRACKER
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
#endif /* KEYPOSITIONTRACKER */
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

void render2(BelaContext *context, void *userData)
{

	float* audioIn = (float*)context->audioIn;
	for(unsigned int n = 0; n < context->audioFrames; ++n)
	{
		unsigned int channel = 0;
		// inject extra signal in the audio input 
		float out = gPercFlag * 5;
		audioIn[channel + n * context->audioInChannels ] = out;
	}
	if(gPercFlag)
		gPercFlag = 0;
}

void cleanup2(BelaContext *context, void *userData)
{
#ifdef SCANNER
	delete keys;
#endif /* SCANNER */

}
