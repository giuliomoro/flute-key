#include <Bela.h>
#include <Keys.h>
#include <algorithm>
#include <cmath>
extern float gKey;
extern float gPos;
extern float gGate;
extern float gPerc;
extern float gAux;

#define SCOPE
#define SCANNER
//#define LOG_KEYS_AUDIO
#define FEATURE
#define KEYPOSITIONTRACKER
//#define PIEZOS
//#define LOWPASS_PIEZOS
//#define DELAY_INPUTS

#ifdef SCOPE
#include <Scope.h>
Scope scope;
#endif /* SCOPE */
#ifdef SCANNER
#include <Keys.h>
Keys* keys;
BoardsTopology bt;
int gKeyOffset = 34;
int gNumKeys = 38;
float gBendingKeyTwoThreshold = 0.3;
float gGatePosThresholdOn = 0.5;
float gGatePosThresholdOff = 0.1;
#ifdef FEATURE
#include "KeyFeature.h"
KeyFeature keyFeature;
#endif /* FEATURE */
#ifdef KEYPOSITIONTRACKER
#include "KeyPositionTracker.h"
KeyBuffers keyBuffers;
std::vector<KeyBuffer> keyBuffer;
std::vector<KeyPositionTracker> keyPositionTrackers;
#endif /* KEYPOSITIONTRACKER */
#else /* SCANNER */
#undef LOG_KEYS_AUDIO
#undef FEATURE
#endif /* SCANNER */

static const unsigned int gNumPiezos = 8; // let's keep this even if no piezos
#ifdef PIEZOS
unsigned int gMicPin[gNumPiezos] = {7, 6, 5, 4, 3, 2, 1, 0};
#ifdef LOWPASS_PIEZOS
#include <IirFilter.h>
std::vector<IirFilter> piezoFilters;
std::array<std::array<double, IIR_FILTER_STAGE_COEFFICIENTS>, 2> piezoFiltersCoefficients 
{{
	//[B, A] = butter(2, 500/22050); fprintf('%.8f, ', [B A(2:3)]); fprintf('\n')
	{{ 0.00120741, 0.00241481, 0.00120741, -1.89933342, 0.90416304 }},
	{{1, 0, 0, 0, 0}},
}};
#endif /* LOWPASS_PIEZOS */

unsigned int gInputDelayIdx = 0;
float gInputDelayTime = 0.0067;
unsigned int gInputDelaySize;
std::vector<std::vector<float>> gInputDelayBuffers;

#else /* PIEZOS */
#undef LOWPASS_PIEZOS
#undef DELAY_INPUTS
#endif /* PIEZOS */

#ifdef SCANNER
void Bela_userSettings2(BelaInitSettings* settings)
{
	settings->pruNumber = 0;	
	settings->useDigital = 0;
	printf("Bela__user_settings2\n");
}

#ifndef LOG_KEYS_AUDIO
void postCallback(void* arg, float* buffer, unsigned int length){
	Keys* keys = (Keys*)arg;
	unsigned int key = gKeyOffset + 13;
#ifdef FEATURE
	KeyFeature::postCallback((void*)&keyFeature, buffer, length);
#endif /* FEATURE */
#ifdef KEYPOSITIONTRACKER
	{
		static int count;
		keyBuffers.postCallback(buffer, length);
		for(unsigned int n = 0; n < length; ++n)
		{
			if(n >= 45 && n < 82)
			{
				keyPositionTrackers[n].triggerReceived(count);
			}
		}
		count++;
	}
#endif /* KEYPOSITIONTRACKER */
	
	static float oldVal;
	float val = keys->getNoteValue(key);
#ifdef FEATURE
	static float feature;
	feature = keyFeature.percussiveness[key].getFeature();
#ifdef KEYPOSITIONTRACKER
	static float feature3;
	// use state machine from KeyPositionTracker to gate feature update
	int state = keyPositionTrackers[key].currentState();
	bool shouldUpdate = 
		(state != kPositionTrackerStateDown)
		&& (state != kPositionTrackerStateReleaseInProgress)
		&& (state != kPositionTrackerStateReleaseFinished);
	feature3 = state /(float) kPositionTrackerStateReleaseFinished;
	//if(shouldUpdate)
#endif /* KEYPOSITIONTRACKER */
#endif /* FEATURE */
#ifdef KEYPOSITIONTRACKER
	float feature2 = keyPositionTrackers[key].pressPercussiveness().percussiveness;
	feature2 -= 0.02;
	feature2 *= 15;
	feature2 = feature2 < 0 ? 0 : feature2;
#endif /* KEYPOSITIONTRACKER */
#ifdef SCOPE
	scope.log(1.f-val, feature, feature2, -feature3);
#endif /* SCOPE */
	float* start = buffer + gKeyOffset;
	float* min = std::min_element(start, start + gNumKeys);
	float pos  = 1 - *min;
	if(pos < 0)
		pos = 0;
	else if (pos > 1)
		pos = 1;
	if(gGate)
	{
		if(pos < gGatePosThresholdOff)
			gGate = 0;
	} else {
		if(pos > gGatePosThresholdOn)
			gGate = 1;
	}
	gPos = pos;
	int keyOne = min - buffer;
	float keyFloat = keyOne;
#ifdef FEATURE
	static float perc;
	static float smoothedPerc;
	static float lastTrigPerc;
	if(1) // pitch bend is the new polyphony
	{
		float keyOnePos = 1.f - buffer[keyOne];
		// set the minimum value to a high value and repeat the search
		*min = 1;
		min = std::min_element(start, start + gNumKeys);
		int keyTwo = min - buffer;
		float keyTwoPos = 1.f - buffer[keyTwo];
		float keyOnePerc = keyFeature.percussiveness[keyOne].getFeature();
		float keyTwoPerc = keyFeature.percussiveness[keyTwo].getFeature();
		if(keyTwoPos > gBendingKeyTwoThreshold)
		{
			// we are bending
			if(0)
			if(keyTwoPos > gBendingKeyTwoThreshold && std::abs(keyTwo - keyOne) <= 2)
			{ // weighted average:
				float keyOneW = std::max(0.f, std::min(1.f, keyOnePos));
				float keyTwoW = std::max(0.f, std::min(1.f, keyTwoPos));
				float totW = keyOneW + keyTwoW;
				keyOneW = keyOneW / totW;
				keyTwoW = keyTwoW / totW;
				keyFloat = (keyOneW * keyOne + keyTwoW * keyTwo);
			}
			perc = keyTwoPerc;
		} else {
			perc = keyOnePerc;
		}
		perc = perc;//std::pow(1.4f, perc);
	} else {
		keyFloat = keyOne;
	}
	if(lastTrigPerc != perc)
	{
		lastTrigPerc = perc;
		smoothedPerc = perc;
	} else {
		smoothedPerc *= 0.99;
	}
	gPerc = smoothedPerc;
	float percussNorm = 0.15;
	float auxRange = 0.3;
	float auxMin = 0;
	float aux = smoothedPerc > 0.1;
	aux = aux < 0 ? 0 : aux;
	aux = aux > 1 ? 1 : aux;
	gAux = aux * auxRange + auxMin;

	if(gGate)
		gKey = keyFloat - gKeyOffset;

	static int count = 0;
	if(count++ == 100)
	{
		rt_printf("gKey: %5.2f, gPos: %.2f, gPerc: %.2f, gAux: %.3f, gGate: %.1f\n", gKey, gPos, gPerc, gAux, gGate);
		count = 0;
	}
	//scope.log(val, (val - oldVal), val2, (val2 - oldVal2), feature, feature2, 0, 0);
	oldVal = val;
#endif /* FEATURE */
}
#endif /* LOG_KEYS_AUDIO */
#else
#undef LOG_KEYS_AUDIO
#endif /* SCANNER */

bool setup2(BelaContext *context, void *userData)
{
	printf("Setup2\n");
#ifdef SCOPE
#ifdef LOG_KEYS_AUDIO
	scope.setup(4, context->audioSampleRate);
#else /* LOG_KEYS_AUDIO */
	scope.setup(4, 1000);
#endif /* LOG_KEYS_AUDIO */
#endif /* SCOPE */
#ifdef LOWPASS_PIEZOS
	for(unsigned int n = 0; n < gNumPiezos; ++n) {
		piezoFilters.emplace_back(2);
	}
	for(auto &f : piezoFilters)
	{
		for(unsigned int n = 0; n < piezoFiltersCoefficients.size(); ++n)
			f.setCoefficients(piezoFiltersCoefficients[n].data(), n);
	}
#endif /* LOWPASS_PIEZOS */
#ifdef DELAY_INPUTS
	gInputDelaySize = gInputDelayTime * context->audioSampleRate;
	gInputDelayBuffers.resize(gNumPiezos);
	for(auto &v : gInputDelayBuffers)
		v.resize(gInputDelaySize);
#endif /* DELAY_INPUTS */
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
#ifdef FEATURE
	keyFeature.setup(bt.getNumNotes(), 500);
#endif /* FEATURE */
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
#endif /* KEYPOSITIONTRACKER */
#ifndef LOG_KEYS_AUDIO
	keys->setPostCallback(postCallback, keys);
#endif /* LOG_KEYS_AUDIO */
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
	for(unsigned int n = 0; n < context->audioFrames; ++n)
	{
		++gSampleCount;
#ifdef SCANNERAA
		if((gSampleCount & 4095) == 0){
			for(int n = bt.getLowestNote(); n <= bt.getHighestNote(); ++n)
				if(n >= gKeyOffset && n < gKeyOffset + gNumPiezos) rt_printf("%.4f ", keys->getNoteValue(n));
			rt_printf("\n");
		}
#endif /* SCANNER */
		int iMax = context->audioInChannels < gNumPiezos ? context->audioInChannels : gNumPiezos;
		for (int i = 0; i < iMax; i++)
		{
#ifdef PIEZOS
			float piezoInput = audioRead(context, n, gMicPin[i]);
		
#ifdef LOWPASS_PIEZOS
			float filteredPiezoInput = piezoFilters[i].process(piezoInput);
			piezoInput = filteredPiezoInput;
#endif /* LOWPASS_PIEZOS */
			// read from delay line
			float input = gInputDelayBuffers[i][gInputDelayIdx];
			// write into delay line
			gInputDelayBuffers[i][gInputDelayIdx] = piezoInput;
#endif /* PIEZOS */
#ifdef SCOPE
#if (defined SCANNER && defined PIEZOS && defined LOG_KEYS_AUDIO)
			if(i == 0)
				scope.log(input, keys->getNoteValue(gKeyOffset + i));
#endif /* SCANNER && PIEZOS */
#endif /* SCOPE */
		}
	}
}

void cleanup2(BelaContext *context, void *userData)
{
#ifdef SCANNER
	delete keys;
#endif /* SCANNER */

}
