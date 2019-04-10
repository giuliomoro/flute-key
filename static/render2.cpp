#include <Bela.h>
#include <Keys.h>
#include <algorithm>
#include <cmath>
#include "KeyboardState.h"
#include "command-line-data.h"

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
float gAnalogIn0Smoothed;
float gAnalogIn1Read;
float gAnalogIn2Read;
uint64_t gTimestamp;

#define SCOPE
#define SCANNER
#define FILE_PLAYBACK
#define LOGGING
#define MIDI

#include "tuning.h"
#ifdef MIDI
#include <Midi.h>
Midi gMidi;
#endif /* MIDI */
#ifdef LOGGING
#include <chrono>
#include <iomanip>
#include <WriteFile.h>
int gShouldLog;
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
//#define AUTOPLAY
void postCallback(void* arg, float* buffer, unsigned int length){
	//Keys* keys = (Keys*)arg;
	int firstKey = gKeyOffset;
	int lastKey = gKeyOffset + gNumKeys + 1;
#ifdef SINGLE_KEY
	unsigned int key = 46;
#endif /* SINGLE_KEY */
	static int count;
	//if(count % 1000 == 0)
		//rt_printf("Tunables %f %f\n", gAnalogIn0Read, gAnalogIn1Read);
	{
		for(unsigned int n = 0; n < length; ++n)
		{
			// INVERTING INPUTS
			buffer[n] = 1.f - buffer[n];
#ifdef AUTOPLAY
			// ramp up the primary key so that KeyPositionTracker gets the
			// state transitions
			buffer[n] = 0;
			if(50 == n)
			{
				if(count > 100)
					buffer[n] = (count - 100) / 200.f;
				// simulate rebound at key-bottom
				if(buffer[n] > 1)
					buffer[n] = 0.95;
			}
			const int period = 2000;
			if(n == 52 && count > 150)
			{
				int periodIdx = (count - 150) % period;
				float keyPos = (periodIdx > period / 2 ? period - periodIdx : periodIdx)/(float)(period / 2);
				buffer[n] = keyPos * 0.8f;
			}
#endif /* AUTOPLAY */
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
	enum {
		kCrossFadeDip,
		kExpo,
		kNumCcs,
	};
	const int kControllerDefault = 64;
	static int midiParams[kNumCcs] = {-1};
	if(-1 == midiParams[0])
	{
		for(unsigned int n = 0; n < kNumCcs; ++n)
			midiParams[n] = kControllerDefault;
	}

#ifdef MIDI
	enum {
		kCcCrossFadeDip = 16,
		kCcExpo = 20,
	};
	const int kNoteReset = 3;
	while(gMidi.getParser()->numAvailableMessages())
	{
		MidiChannelMessage msg = gMidi.getParser()->getNextChannelMessage();
		MidiMessageType type = msg.getType();
		if(kmmControlChange == type)
		{
			int cc = msg.getDataByte(0);
			int val = msg.getDataByte(1);
			rt_printf("On Channel: %d, controlchange %d, value %d\n", msg.getChannel(), cc, val);
			switch (cc) {
			case kCcCrossFadeDip:
				midiParams[kCrossFadeDip] = val;
				rt_printf("dip: %d\n", val);
				break;
			case kCcExpo:
				rt_printf("expo: %d\n", val);
				midiParams[kExpo] = val;
				break;
			default:
				break;
			}
		} else if(kmmNoteOn == type) {
			int note = msg.getDataByte(0);
			int vel = msg.getDataByte(1);
			rt_printf("On Channel: %d, note %d, velocity %d\n", msg.getChannel(), note, vel);
			if(kNoteReset == note && vel)
			{
				//reset all params:
				for(unsigned int n = 0; n < kNumCcs; ++n)
					midiParams[n] = 64;
			}
		}
	}
	bool areControllersDefault = true;
	for(unsigned int n = 0; n < kNumCcs; ++n)
	{
		if(kControllerDefault != midiParams[n])
		{
			areControllersDefault = false;
			break;
		}
	}
	if(areControllersDefault)
		gMidi.writeNoteOn(0, kNoteReset, 0);
	else
		gMidi.writeNoteOn(0, kNoteReset, 127);
#endif /* MIDI */
	float crossFadeDip = 0.7f * 4.f * 2.f * midiParams[kCrossFadeDip]/127.f;

	keyboardState.setPositionCrossFadeDip(crossFadeDip);
	keyboardState.render(buffer, keyPositionTrackers, firstKey, lastKey);

	static float bendFreq = 0;
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
			kBendStatePost, // a smoothing state from Transitioning to Low
			kBendStateHigh
		};

		typedef struct {
			float idx;
			float freq;
			float emb;
			int key;
			int otherKey;
		} InitialState;
		#define STORE_STATE(state) state.idx = idx; state.freq = bendFreq; state.emb = bendEmbouchureOffset;\
				state.otherKey = keyboardState.getOtherKey(); state.key = keyboardState.getKey();\
				rt_fprintf(stderr, "     idx: %.4f, freq: %.4f, emb: %.4f\n", state.idx, state.freq, state.emb)

		const float embPeakIdx = 0.7;
		const float embStopIdx = 0.9;
		const float embClip = 1.2;
		const float bendStateHighToLowThreshold = 0.6;
		const float leakyAlpha = 0.99;
		const float kLeakyLowToTransThreshold = 0.95;
		const float transitionMaxIdx = 0.97;
		const float postToLowRange = 0.2;

		static InitialState trans;
		static InitialState post;
		static InitialState high;
		static float lowTargetIdx;

		static int bendStateHighKey;
		static float bendStateHighKeyInitialPos;
		static float embNormLeaky = 0;
		if(kBendStateHigh == bendState)
		{
			if(buffer[bendStateHighKey] < bendStateHighToLowThreshold)
			{
				bendState = kBendStateLow;
				rt_fprintf(stderr, "%d bendState reset to low\n", count);
			}
		}

		float bendRange = keyboardState.getBendRange(); //positive (bending up) or negative (bending down)
		float gTargetFreq = 0;
		float gTargetEmb = 0;
		float embNormalized;
		{
			//rt_printf("bendRange: %f keyboardState.getBend(): %f\n", bendRange, keyboardState.getBend());
			if(bendRange)
				idx = keyboardState.getBend() / bendRange; // normalized current bend, always positive
			else
				idx = 0;
			 embNormalized = idx < embPeakIdx ?
				idx/embPeakIdx : (embStopIdx-idx)/(embStopIdx - embPeakIdx);
			embNormalized *= embClip;
			embNormalized = embNormalized < 0 ? 0 : embNormalized;
			embNormalized = embNormalized > 1 ? 1 : embNormalized;

			embNormLeaky = embNormalized * (1.f-leakyAlpha) + embNormLeaky * leakyAlpha;
			//if(count % 20 == 0)
				//rt_printf("%d_ leaky: %.5f\n", count, embNormLeaky);
			if(embNormLeaky > kLeakyLowToTransThreshold)
			{
				if(kBendStateLow == bendState || kBendStatePost == bendState)
				{
					rt_fprintf(stderr, "%d bendState from _%s_ %d to transitioning\n", count, (bendState == kBendStateLow ? "low" : "post"));
					bendState = kBendStateTransitioning;
					STORE_STATE(trans);
				}
			} else
			if(embNormLeaky < 0.3)
			{
				if(kBendStateTransitioning == bendState)
				{
					bendState = kBendStatePost;
					rt_fprintf(stderr, "%d bendState from transitioning to post\n", count);
					STORE_STATE(post);
				}
			}
			if(kBendStatePost == bendState)
			{
				if(idx < 0.03
					|| (lowTargetIdx > post.idx && idx > lowTargetIdx)
					|| (lowTargetIdx < post.idx && idx < lowTargetIdx)
					|| (post.otherKey != keyboardState.getOtherKey() && post.key != keyboardState.getKey())
				)
				{
					bendState = kBendStateLow;
					rt_fprintf(stderr, "%d bendState from post to low\n", count);
				}
			}
			// only allow StateHigh when bending up
			if(kBendStateTransitioning == bendState && idx > transitionMaxIdx && bendRange > 0)
			{
				bendState = kBendStateHigh;
				bendStateHighKey = keyboardState.getOtherKey();
				bendStateHighKeyInitialPos = buffer[bendStateHighKey];
				rt_fprintf(stderr, "%d bendState from transitioning to high (on %d)\n", count, bendStateHighKey);
				STORE_STATE(high);
			}
			//TODO: transition to low also when changing key

			if(kBendStateLow == bendState)
			{
				float freq, emb;
				getEmbFreq(bendRange, idx, freq, emb);
				bendEmbouchureOffset = emb;
				bendFreq = freq;
			} else if(kBendStatePost == bendState) {
				// target the equivalent StateLow position located at ± postToLowRange
				float targetFreq, targetEmb;
				lowTargetIdx = idx > post.idx ? post.idx + postToLowRange : post.idx - postToLowRange;
				lowTargetIdx = lowTargetIdx > 0.95 ? 0.95 : lowTargetIdx < 0.05 ? 0.05 : lowTargetIdx;
				getEmbFreq(bendRange, lowTargetIdx, targetFreq, targetEmb);
				gTargetFreq = targetFreq;
				gTargetEmb = targetEmb;
				bendEmbouchureOffset = map(idx, post.idx, lowTargetIdx, post.emb, targetEmb);
				bendFreq = map(idx, post.idx, lowTargetIdx, post.freq, targetFreq);
			} else if(kBendStateTransitioning == bendState) {
				// keep ramping the embouchure till we get to the high octave
				bendEmbouchureOffset = map(idx, trans.idx, transitionMaxIdx, trans.emb, bendRange > 0 ? 1 : -0.995);
				// also ramp the frequency to complete the bend
				bendFreq = map(idx, trans.idx, transitionMaxIdx, trans.freq, bendRange);
			}
			if (kBendStateHigh == bendState) {
				bendFreq = keyboardState.getBend();
				bendStateHighKeyInitialPos = bendStateHighKeyInitialPos < buffer[bendStateHighKey] ? buffer[bendStateHighKey] : bendStateHighKeyInitialPos;
				bendEmbouchureOffset = map(buffer[bendStateHighKey], bendStateHighKeyInitialPos, bendStateHighToLowThreshold, 1, 0);
				bendEmbouchureOffset = constrain(bendEmbouchureOffset, 0, 1);
				highPressure = true;
			}
			if(0)
			scope.log(
				bendEmbouchureOffset, // red
				bendFreq/bendRange, // blue
				bendState / 4.f - 0.99, // green
				embNormLeaky-1, // pink
				idx - 1, // light blue
				embNormalized - 1, // fuchsia
				gTargetEmb, // red
				gTargetFreq/bendRange // blue
			);
		}
	}
	const float posThreshold = 0.13; // ignore values below this
	const float pressureRange = 1.4;
	const float pressureOffset = 0.5;
	const float absoluteMaxPressure = pressureRange + pressureOffset + 0.2;
	const float maxPressureAtLow = 1.1f / 1.9f;
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
	//float expo = gAnalogIn0Read * 2.f + 0.5;
	float expo = 0.146f * 2.f * 2.f * midiParams[kExpo]/127.f + 0.5f;
	float afterTouchThreshold = 1.03;
	if(pressure <= 0)
		pressure = 0;
	else if(pressure <= afterTouchThreshold)
		pressure = powf(pressure, expo); // some e
	else if(pressure > afterTouchThreshold)
		pressure = powf(pressure, 4); // fixed curve for aftertouch, very steep
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

	static float tempPos;
	if(isSmoothingPosition) {
		 tempPos = tempPos * clickSmoothAlpha + candidatePressure * (1.f - clickSmoothAlpha);
	} else {
		tempPos = candidatePressure;
	}
	float pedalPressure = 0.85f + (gAnalogIn0Smoothed * 0.15f);
	float pedalGain = 0.2f + gAnalogIn0Smoothed * gAnalogIn0Smoothed * 0.8f;
	//gPos = 1.2; // use this to ignore the above and test tuning

	// handle percussiveness
	static float lastPerc = 0;
	float newPerc = keyboardState.getPercussiveness();
	if(newPerc != lastPerc && newPerc)
	{
		gPerc = newPerc;
		rt_printf("Perc: %f\n", gPerc);
		gPerc *= 13.f;
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
	bendEmbouchureOffset = constrain(bendEmbouchureOffset, -0.8, 1.2);
	gEmbRatio = 1.f + bendEmbouchureOffset;
	gGate = 1;
	const float maxGain = 0.2;
	float tempGain = maxGain * gain;
	gKey = fixTuning(keyboardState.getKey() + bendFreq + 12, gPos);
	// higher notes don't speak with too much pressure in one go, for high
	// nonLinearity values. Therefore, we adjust nonLinearity with the
	// frequency.
	gNonLinearity = getNonLinearity(gKey, gEmbRatio);

#ifdef LOGGING
	if(gShouldLog)
	{
		float logs[] = {(float)gTimestamp, (float)count, gKey, tempPos, gNonLinearity, gGate, gPerc, tempGain, gEmbRatio, (float)bendState, gAnalogIn0Smoothed, keyboardState.getPosition()};
		gSensorFile.log(logs, sizeof(logs)/sizeof(float));
		gSensorFile.log(buffer + firstKey, lastKey - firstKey + 1);
	}
#endif /* LOGGING */
	gGain = tempGain * pedalGain;
	gPos = tempPos * pedalPressure;
	return;
}

bool setup2(BelaContext *context, void *userData)
{
#ifdef MIDI
	char midiInterface[] = "hw:1,0,0";
	gMidi.readFrom(midiInterface);
	gMidi.writeTo(midiInterface);
	gMidi.enableParser(true);
#endif /* MIDI */
#ifdef LOGGING /* LOGGING */
	struct command_line_data_t* command_line_data = (struct command_line_data_t*)userData;
	gShouldLog = !command_line_data->dont_log;
	if(gShouldLog) {
		const char* path;
		if(command_line_data->path)
			path = command_line_data->path;
		else
			path = "/mnt/storage/";

		auto time = std::time(nullptr);
		char timestr[500];
		std::strftime(timestr, sizeof(timestr), "%Y-%m-%d-%H_%M_%S", std::localtime(&time));
		char filestr[1000];
		sprintf(filestr, "%s/audio_log-%s.bin", path, timestr);
		if(gAudioFile.setup(filestr))
			return false;
		gAudioFile.setFileType(kBinary);

		sprintf(filestr, "%s/analog_log-%s.bin", path, timestr);
		if(gSensorFile.setup(filestr))
			return false;
		gSensorFile.setFileType(kBinary);
	}
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
	scope.setup(2, context->audioSampleRate);
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
	gAnalogIn1Read = analogReadNI(context, 0, 1);
	gAnalogIn2Read = analogReadNI(context, 0, 2);
	const float analogIn0Alpha = 0.995;
	for(unsigned int n = 0; n < context->analogFrames; ++n)
	{
		gAnalogIn0Smoothed = analogReadNI(context, n, 0) * (1.f - analogIn0Alpha) + analogIn0Alpha * gAnalogIn0Smoothed;
	}
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
#ifdef LOGGING
	if(gShouldLog)
	{
		float timestamp = gTimestamp;
		gAudioFile.log(&timestamp, 1);
		gAudioFile.log(context->audioOut, context->audioFrames);
	}
#endif /* LOGGING */
	gTimestamp += context->audioFrames;
}

void cleanup2(BelaContext *context, void *userData)
{
#ifdef SCANNER
	delete keys;
#endif /* SCANNER */

}
