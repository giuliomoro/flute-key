/*
  TouchKeys: multi-touch musical keyboard control software
  Copyright (c) 2013 Andrew McPherson

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 
  =====================================================================
 
  KeyPositionTracker.cpp: parses continuous key position and detects the
  state of the key.
*/
#include <Scope.h>
extern Scope scope;
float gMaxThreshold;
float gMinThreshold;
float gPercussed;

#include "KeyPositionTracker.h"
#include <iostream>
extern "C" int rt_printf(const char *format, ...);
int gPrint = 1;

bool KeyBuffers::setup(unsigned int numKeys, unsigned int bufferLength)
{
	if(numKeys == 0 || bufferLength == 0)
		return false;
	positionBuffer.resize(numKeys);
	timestamps.resize(numKeys);
	for(auto &p : positionBuffer)
		p.resize(bufferLength);
	for(auto &p : timestamps)
		p.resize(bufferLength);
	return true;
}

void KeyBuffers::postCallback(void* arg, float* buffer, unsigned int length)
{
	KeyBuffers* that = (KeyBuffers*)arg;
	static int count = 0;
	that->postCallback(buffer, length, count/1000.f);
	++count;
}

void KeyBuffers::postCallback(float* buffer, unsigned int length, timestamp_type timestamp)
{
	for(unsigned int n = 0; n < std::min(positionBuffer.size(), length); ++n)
	{
		positionBuffer[n][writeIdx] = buffer[n];
		//positionBuffer[n][writeIdx] = ((int)ts % 1000)/1000.f;
		/*
TODO: fix this instead of using static ts
		if(full)
			ts = firstSampleIndex + positionBuffer[0].size();
		else
			ts = writeIdx;
			*/
		timestamps[n][writeIdx] = timestamp;
	}
	++writeIdx;
	if(writeIdx >= positionBuffer[0].size())
	{
		writeIdx = 0;
		full = true;
	}
	if(full)
	{
		++firstSampleIndex;
	}
}

const std::array<std::string, KeyPositionTrackerNotification::kNotificationTypeNewMaximum + 1> KeyPositionTrackerNotification::desc = {{
	"none",
        "kNotificationTypeStateChange",
        "kNotificationTypeFeatureAvailableVelocity",
        "kNotificationTypeFeatureAvailableReleaseVelocity",
        "kNotificationTypeFeatureAvailablePercussiveness",
        "kNotificationTypeNewMinimum",
        "kNotificationTypeNewMaximum",
}};
// Default constructor
KeyPositionTracker::KeyPositionTracker(capacity_type capacity, /*Node<key_position>&*/ KeyBuffer& keyBuffer)
: /*Node<KeyPositionTrackerNotification>(capacity),*/ keyBuffer_(keyBuffer), engaged_(false) {
    reset();
}

// Copy constructor
/*KeyPositionTracker::KeyPositionTracker(KeyPositionTracker const& obj)
: Node<int>(obj), keyBuffer_(obj.keyBuffer_), engaged_(obj.engaged_) {
    if(engaged_)
        registerForTrigger(&keyBuffer_);
}*/

// Calculate (MIDI-style) key press velocity from continuous key position
std::pair<timestamp_type, key_velocity> KeyPositionTracker::pressVelocity() {
    return pressVelocity(pressVelocityEscapementPosition_);
}

std::pair<timestamp_type, key_velocity> KeyPositionTracker::pressVelocity(key_position escapementPosition) {
    // Check that we have a valid start point from which to calculate
    if(missing_value<timestamp_type>::isMissing(startTimestamp_)) {
        return std::pair<timestamp_type, key_velocity>(missing_value<timestamp_type>::missing(),
                                                       missing_value<key_velocity>::missing());
    }
    
    // Find where the key position crosses the indicated level
    key_buffer_index index = startIndex_;
    if(index < keyBuffer_.beginIndex() + 2)
        index = keyBuffer_.beginIndex() + 2;

    while(index < keyBuffer_.endIndex() - kPositionTrackerSamplesNeededForPressVelocityAfterEscapement) {
        // If the key press has a defined end, make sure we don't go past it
        if(pressIndex_ != 0 && index >= pressIndex_)
            break;
        
        if(keyBuffer_[index] > escapementPosition) {
            // Found the place the position crosses the indicated threshold
            // Now find the exact (interpolated) timestamp and velocity
            timestamp_type exactPressTimestamp = keyBuffer_.timestampAt(index); // TODO
            
            // Velocity is calculated by an average of 2 samples before and 1 after
            key_position diffPosition = keyBuffer_[index + kPositionTrackerSamplesNeededForPressVelocityAfterEscapement] - keyBuffer_[index - 2];
            timestamp_diff_type diffTimestamp = keyBuffer_.timestampAt(index + kPositionTrackerSamplesNeededForPressVelocityAfterEscapement) - keyBuffer_.timestampAt(index - 2);
            key_velocity velocity = calculate_key_velocity(diffPosition, diffTimestamp);
            
            return std::pair<timestamp_type, key_velocity>(exactPressTimestamp, velocity);
        }
        index++;
    }
    
    // Didn't find anything matching that threshold
    return std::pair<timestamp_type, key_velocity>(missing_value<timestamp_type>::missing(),
                                                   missing_value<key_velocity>::missing());
}

// Calculate (MIDI-style) key release velocity from continuous key position
std::pair<timestamp_type, key_velocity> KeyPositionTracker::releaseVelocity() {
    return releaseVelocity(releaseVelocityEscapementPosition_);
}

std::pair<timestamp_type, key_velocity> KeyPositionTracker::releaseVelocity(key_position returnPosition) {
    // Check that we have a valid start point from which to calculate
    if(missing_value<timestamp_type>::isMissing(releaseBeginTimestamp_)) {
        return std::pair<timestamp_type, key_velocity>(missing_value<timestamp_type>::missing(),
                                                       missing_value<key_velocity>::missing());
    }
    
    // Find where the key position crosses the indicated level
    key_buffer_index index = releaseBeginIndex_;
    if(index < keyBuffer_.beginIndex() + 2)
        index = keyBuffer_.beginIndex() + 2;

    while(index < keyBuffer_.endIndex() - kPositionTrackerSamplesNeededForReleaseVelocityAfterEscapement) {
        // Check for whether we've hit the end of the release interval, assuming
        // the interval exists yet
        if(releaseEndIndex_ != 0 && index >= releaseEndIndex_)
            break;
        
        if(keyBuffer_[index] < returnPosition) {
            // Found the place the position crosses the indicated threshold
            // Now find the exact (interpolated) timestamp and velocity
            timestamp_type exactPressTimestamp = keyBuffer_.timestampAt(index); // TODO
            
            // Velocity is calculated by an average of 2 samples before and 1 after
            key_position diffPosition = keyBuffer_[index + kPositionTrackerSamplesNeededForReleaseVelocityAfterEscapement] - keyBuffer_[index - 2];
            timestamp_diff_type diffTimestamp = keyBuffer_.timestampAt(index + kPositionTrackerSamplesNeededForReleaseVelocityAfterEscapement) - keyBuffer_.timestampAt(index - 2);
            key_velocity velocity = calculate_key_velocity(diffPosition, diffTimestamp);
            
            //std::cout << "found release velocity " << velocity << "(diffp " << diffPosition << ", diffT " << diffTimestamp << ")" << std::endl;
            
            return std::pair<timestamp_type, key_velocity>(exactPressTimestamp, velocity);
        }
        index++;
    }
    
    // Didn't find anything matching that threshold
    return std::pair<timestamp_type, key_velocity>(missing_value<timestamp_type>::missing(),
                                                   missing_value<key_velocity>::missing());
}

const int kSamplesNeededForPercussiveness = 6;
// Calculate and return features about the percussiveness of the key press
KeyPositionTracker::PercussivenessFeatures KeyPositionTracker::pressPercussiveness() {
    PercussivenessFeatures features;
    features.hasBeenRead = false;
    key_buffer_index index;
    key_velocity maximumVelocity, largestVelocityDifference;
    key_buffer_index maximumVelocityIndex, largestVelocityDifferenceIndex;
    
    startIndex_ = keyBuffer_.endIndex() - kSamplesNeededForPercussiveness - 1;
    // Check that we have a valid start point from which to calculate
    if(missing_value<timestamp_type>::isMissing(startTimestamp_) || keyBuffer_.beginIndex() > startIndex_ - 1) {
        //std::cout << "*** no start time\n";
        features.percussiveness = missing_value<float>::missing();
        return features;
    }
    
    // From the start of the key press, look for an initial maximum in velocity
    index = startIndex_;
    
    maximumVelocity = scale_key_velocity(0);
    maximumVelocityIndex = startIndex_;
    largestVelocityDifference = scale_key_velocity(0);
    largestVelocityDifferenceIndex = startIndex_;
    
    //std::cout << "*** start index " << index << std::endl;

    
//rt_printf("pressIndex: %d, startIndex: %d\n", pressIndex_, startIndex_);
    while(index < keyBuffer_.endIndex()) {
        
        key_position diffPosition = keyBuffer_[index] - keyBuffer_[index - 1];
        timestamp_diff_type diffTimestamp = keyBuffer_.timestampAt(index) - keyBuffer_.timestampAt(index - 1);
        key_velocity velocity = calculate_key_velocity(diffPosition, diffTimestamp);
        
        // Look for maximum of velocity
        if(velocity > maximumVelocity) {
            maximumVelocity = velocity;
            maximumVelocityIndex = index;
            //std::cout << "*** found new max velocity " << maximumVelocity << " at index " << index << std::endl;
	    if(gPrint > 1)
                rt_printf("*** found new max velocity %f at index %u\n", maximumVelocity, index);
        }
        
        // And given the difference between the max and the current sample,
        // look for the largest rebound (velocity hitting a peak and falling)
        if(maximumVelocity - velocity > largestVelocityDifference) {
            largestVelocityDifference = maximumVelocity - velocity;
            largestVelocityDifferenceIndex = index;

            //std::cout << "*** found new diff velocity " << largestVelocityDifference << " at index " << index << std::endl;
	    if(gPrint > 1)
		    rt_printf("*** found new diff velocity %f at index %u\n", largestVelocityDifference, index);
        }
        
        // Only look at the early part of the key press: if the key position
        // makes it more than a certain amount down, assume the initial spike
        // has passed and finish up. But always allow at least 5 points for the
        // fastest key presses to be considered.
        //if(index - startIndex_ >= 4 && keyBuffer_[index] > kPositionTrackerPositionThresholdForPercussivenessCalculation) {
		//rt_printf("dafuck %d\n", index-startIndex_);
            //break;
        //}
        
        index++;
    }

	bool notPercussive = false;
    if(maximumVelocity < kPositionTrackerMaxVelocityPercussiveThreshold)
    {
	    notPercussive = true;
	    //rt_printf("notPercussive ");
    }
    // Now transfer what we've found to the data structure
    features.velocitySpikeMaximum = Event(maximumVelocityIndex, maximumVelocity, keyBuffer_.timestampAt(maximumVelocityIndex));
    features.velocitySpikeMinimum = Event(largestVelocityDifferenceIndex, maximumVelocity - largestVelocityDifference,
                                          keyBuffer_.timestampAt(largestVelocityDifferenceIndex));
    features.timeFromStartToSpike = keyBuffer_.timestampAt(maximumVelocityIndex) - keyBuffer_.timestampAt(startIndex_);
    features.velocityAverageAroundSpike = (2.f * maximumVelocity - largestVelocityDifference) / 2;
    
    // Check if we found a meaningful difference. If not, percussiveness is set to 0
    if(largestVelocityDifference == scale_key_velocity(0)
		    || notPercussive
		    ) {
        features.percussiveness = 0.0;
        features.areaPrecedingSpike = scale_key_velocity(0);
        features.areaFollowingSpike = scale_key_velocity(0);
	    gPercussed = 1;
        return features;
    } else {
	    gPercussed = -1;
    }
    
    // Calculate the area under the velocity curve before and after the maximum
    features.areaPrecedingSpike = scale_key_velocity(0);
    for(index = startIndex_; index < maximumVelocityIndex; index++) {
        key_position diffPosition = keyBuffer_[index] - keyBuffer_[index - 1];
        timestamp_diff_type diffTimestamp = keyBuffer_.timestampAt(index) - keyBuffer_.timestampAt(index - 1);
        features.areaPrecedingSpike += calculate_key_velocity(diffPosition, diffTimestamp);
    }
    features.areaFollowingSpike = scale_key_velocity(0);
    for(index = maximumVelocityIndex; index < largestVelocityDifferenceIndex; index++) {
        key_position diffPosition = keyBuffer_[index] - keyBuffer_[index - 1];
        timestamp_diff_type diffTimestamp = keyBuffer_.timestampAt(index) - keyBuffer_.timestampAt(index - 1);
        features.areaFollowingSpike += calculate_key_velocity(diffPosition, diffTimestamp);
    }
    
    //std::cout << "area before = " << features.areaPrecedingSpike << " after = " << features.areaFollowingSpike << std::endl;
    if(gPrint > 1)
    	rt_printf("area before = %f , after = %f\n", features.areaPrecedingSpike, features.areaFollowingSpike);
    
    features.percussiveness = features.velocitySpikeMaximum.position;

    return features;
}

// Register to receive messages from the key buffer on each new sample
void KeyPositionTracker::engage() {
    if(engaged_)
        return;

    registerForTrigger(&keyBuffer_);
    engaged_ = true;
}

// Unregister from receiving message on new samples
void KeyPositionTracker::disengage() {
    if(!engaged_)
        return;
    
    unregisterForTrigger(&keyBuffer_);
    engaged_ = false;
}

// Clear current state and reset to unknown state
void KeyPositionTracker::reset() {
	percussivenessFeatures_.percussiveness = missing_value<float>::missing();
	percussivenessFeatures_.hasBeenRead = true;
	percussivenessFeatures_.velocityAverageAroundSpike = missing_value<float>::missing();
	//Node<KeyPositionTrackerNotification>::clear();
	empty_ = true; // kind of equivalent to clear() above if we are not a circular buffer. This should be unset by "insert"
    
    currentState_ = kPositionTrackerStateUnknown;
    //rt_fprintf(stderr, "\n%d %s\n", currentState_, statesDesc[currentState_].c_str());
    currentlyAvailableFeatures_ = KeyPositionTrackerNotification::kFeaturesNone;
    currentMinIndex_ = currentMaxIndex_ = startIndex_ = pressIndex_ = 0;
    releaseBeginIndex_ = releaseEndIndex_ = 0;
    lastMinMaxPosition_ = startPosition_ = pressPosition_ = missing_value<key_position>::missing();
    releaseBeginPosition_ = releaseEndPosition_ = missing_value<key_position>::missing();
    currentMinPosition_ = currentMaxPosition_ = missing_value<key_position>::missing();
    startTimestamp_ = pressTimestamp_ = missing_value<timestamp_type>::missing();
    currentMinTimestamp_ = currentMaxTimestamp_ = missing_value<timestamp_type>::missing();
    releaseBeginTimestamp_ = releaseEndTimestamp_ = missing_value<timestamp_type>::missing();
    pressVelocityEscapementPosition_ = kPositionTrackerDefaultPositionForPressVelocityCalculation;
    releaseVelocityEscapementPosition_ = kPositionTrackerDefaultPositionForReleaseVelocityCalculation;
    pressVelocityAvailableIndex_ = releaseVelocityAvailableIndex_ = percussivenessAvailableIndex_ = 0;
    releaseVelocityWaitingForThresholdCross_ = false;
    releaseMaxPosition_ = missing_value<key_position>::missing();
    releaseMaxTimestamp_  = missing_value<timestamp_type>::missing();
}

// Evaluator function. Update the current state
void KeyPositionTracker::triggerReceived(/*TriggerSource* who,*/ timestamp_type timestamp) {
	gPercussed = 0;

	//if(who != &keyBuffer_)
		//return;
    
    key_position currentKeyPosition = keyBuffer_.latest();

#define kDefaultKeyIdleThreshold (scale_key_position(0.03))
    if(kPositionTrackerStateReleaseFinished == currentState_)
    {
        // account for bounces: following key release we may have
        // multiple dampened oscillations. In order to detect a new keypress,
        // we set a dynamic threshold to be always slightly above the
        // latest max (due to the latest oscillation), or - if there is
        // no bounce, or enough time has passed - a fixed threshold
        timestamp_type refTimestamp;
        key_position thresholdMagnitude;
        if(missing_value<key_position>::isMissing(releaseMaxPosition_))
        {
            // waiting for the first bounce, 
            thresholdMagnitude = kPositionTrackerReleaseInitialMax;
//TODO: thresholdMagnitude -= releaseFinishedPosition_; // if the calibration is a bit off, and key bottom is not 0, try to compensate for it
            refTimestamp = releaseFinishedTimestamp_;
        } else {
            thresholdMagnitude = releaseMaxPosition_ + kPositionTrackerReleaseMaxysteresis;
            refTimestamp = releaseMaxTimestamp_;
        }
        float decay = 0.4;
        thresholdMagnitude *= 1.f - (timestamp - refTimestamp)/decay;
        dynamicOnsetThreshold_ = thresholdMagnitude;
	bool shouldReset = false;
	if(dynamicOnsetThreshold_ < kPositionTrackerReleaseMinDynamicOnsetThreshold)
	{
		// the latest max was small enough: the bouncing oscillation
		// has probably ended.
		// Let's just check if we are back to the rest position:
		if(currentKeyPosition < kDefaultKeyIdleThreshold)
		{
			shouldReset = true;
			//rt_fprintf(stderr, "key back to idle\n");
		}
	} else {
		// we are still bouncing
		if(dynamicOnsetThreshold_ < currentKeyPosition)
		{
			// but we are above the latest peak: a new 
			// press must have started
		    // this really seems like a brand new key press: the
		    // old one is done and we reset the state machine
		    rt_fprintf(stderr, "key restarting\n");
		    shouldReset = true;
		}
	}
	if(shouldReset)
	{
	    reset();
	}
    }
    if(empty()) {
	    if(currentKeyPosition > kDefaultKeyIdleThreshold) {
		    // Always start in the partial press state after a reset,
		    // retroactively locating the start position for this key press
		    //rt_printf("EMPTY\n");
		findKeyPressStart(timestamp);
		changeState(kPositionTrackerStatePartialPressAwaitingMax, timestamp);
	    } else {
		    if(kPositionTrackerStateUnknown != currentState_) {
			    rt_printf("unknown\n");
			    changeState(kPositionTrackerStateUnknown, timestamp);
		    }
	    }
    } else {
	    if(kDefaultKeyIdleThreshold >= currentKeyPosition 
			    && kPositionTrackerStateReleaseInProgress != currentState_
			    && kPositionTrackerStateReleaseFinished != currentState_
			    && kPositionTrackerStateUnknown != currentState_
	      )
	    {
		    // we are not releasing, therefore we are not bouncing
		    // most likely we were in one of the partial key press
		    // states, and the key is now back to the idle position
		    //changeState(kPositionTrackerStateUnknown, timestamp);
		    //reset();
		    //currentState_ = kPositionTrackerStateUnknown;
	    }
    }
    


	//std::cout << timestamp << ": " << currentKeyPosition << "\n";
    if(currentState_ == kPositionTrackerStatePressInProgress)
    {
	if(gPrint > 4)
	{
		std::cout << "o" << timestamp << ": " << currentKeyPosition << "\n";
	}
    } else 
    {
	if(gPrint > 4)
	{
		std::cout << "_" << timestamp << ": " << currentKeyPosition << "\n";
	}
    }
    key_buffer_index currentBufferIndex = keyBuffer_.endIndex() - 1;
    
    // First, check queued actions to see if we can calculate a new feature
    // ** Press Velocity **
    if(pressVelocityAvailableIndex_ != 0) {
        if(currentBufferIndex >= pressVelocityAvailableIndex_) {
		//rt_printf("pressVelocityAvailable: %d %d\n", currentBufferIndex, pressVelocityAvailableIndex_);
	    //std::cout << "timestamp: " << timestamp << ", currentBufferIndex: " << currentBufferIndex << ", pressVelocityAvailableIndex_: " << pressVelocityAvailableIndex_ << "\n";
            // Can now calculate press velocity
            currentlyAvailableFeatures_ |= KeyPositionTrackerNotification::kFeaturePressVelocity;
            notifyFeature(KeyPositionTrackerNotification::kNotificationTypeFeatureAvailableVelocity, timestamp);
            pressVelocityAvailableIndex_ = 0;
        }
    }
    // ** Release Velocity **
    if(releaseVelocityWaitingForThresholdCross_) {
        if(currentKeyPosition < releaseVelocityEscapementPosition_)
            prepareReleaseVelocityFeature(currentBufferIndex, timestamp);
    }
    else if(releaseVelocityAvailableIndex_ != 0) {
        if(currentBufferIndex >= releaseVelocityAvailableIndex_) {
            // Can now calculate release velocity
            currentlyAvailableFeatures_ |= KeyPositionTrackerNotification::kFeatureReleaseVelocity;
            notifyFeature(KeyPositionTrackerNotification::kNotificationTypeFeatureAvailableReleaseVelocity, timestamp);
            releaseVelocityAvailableIndex_ = 0;
        }
    }
    // ** Percussiveness **
    if(percussivenessAvailableIndex_ != 0) {
        if(currentBufferIndex >= percussivenessAvailableIndex_) {
		//rt_printf("notify percussivness: %u %u\n", currentBufferIndex, percussivenessAvailableIndex_);
            // Can now calculate percussiveness
            currentlyAvailableFeatures_ |= KeyPositionTrackerNotification::kFeaturePercussiveness;
            notifyFeature(KeyPositionTrackerNotification::kNotificationTypeFeatureAvailablePercussiveness, timestamp);
            percussivenessAvailableIndex_ = 0;
        }
    }
    
    // Major state transitions next, centered on whether the key is pressed
    // fully or partially
    if(currentState_ == kPositionTrackerStatePartialPressAwaitingMax ||
       currentState_ == kPositionTrackerStatePartialPressFoundMax) {
        // These are collectively the pre-press states
        if(currentKeyPosition >= kPositionTrackerPressPosition + kPositionTrackerPressHysteresis) {
		//rt_printf("%f statepressin progress from partial\n", timestamp);
            // Key has gone far enough down to be considered pressed, but hasn't necessarily
            // made it down yet.
            pressIndex_ = 0;
            pressPosition_ = missing_value<key_position>::missing();
            pressTimestamp_ = missing_value<timestamp_type>::missing();
            
            changeState(kPositionTrackerStatePressInProgress, timestamp);
        }
    }
    else if(currentState_ == kPositionTrackerStateReleaseInProgress ||
            currentState_ == kPositionTrackerStateReleaseFinished) {
        if(currentKeyPosition >= kPositionTrackerPressPosition + kPositionTrackerPressHysteresis) {
		//rt_printf("%f statepressin progress\n", timestamp);
            // Key was releasing but is now back down. Need to reprime the start
            // position information, which will be taken as the last minimum.
            startIndex_ = currentMinIndex_;
            startPosition_ = currentMinPosition_;
            startTimestamp_ = currentMinTimestamp_;
            pressIndex_ = 0;
            pressPosition_ = missing_value<key_position>::missing();
            pressTimestamp_ = missing_value<timestamp_type>::missing();
            
            changeState(kPositionTrackerStatePressInProgress, timestamp);
        }
    }
    else if(currentState_ == kPositionTrackerStatePressInProgress) {
        // Press has started, wait to find its max position before labeling the key as "down"
        if(currentKeyPosition < kPositionTrackerPressPosition - kPositionTrackerPressHysteresis) {
            // Key is on its way back up: find where release began
            findKeyReleaseStart(timestamp);

            changeState(kPositionTrackerStateReleaseInProgress, timestamp);
        }
    }
    else if(currentState_ == kPositionTrackerStateDown) {
        if(currentKeyPosition < kPositionTrackerPressPosition - kPositionTrackerPressHysteresis) {
            // Key is on its way back up: find where release began
            findKeyReleaseStart(timestamp);
            
            changeState(kPositionTrackerStateReleaseInProgress, timestamp);
        }
    }

    // Find the maxima and minima of the key motion
    if(missing_value<key_position>::isMissing(currentMaxPosition_) ||
       currentKeyPosition > currentMaxPosition_) {
        // Found a new local maximum
        currentMaxIndex_ = currentBufferIndex;
        currentMaxPosition_ = currentKeyPosition;
        currentMaxTimestamp_ = timestamp;
        
        // If we previously found a maximum, go back to the original
        // state so we can process the new max that is in progress
        if(currentState_ == kPositionTrackerStatePartialPressFoundMax)
            changeState(kPositionTrackerStatePartialPressAwaitingMax, timestamp);
    }
    else if(missing_value<key_position>::isMissing(currentMinPosition_) ||
            currentKeyPosition < currentMinPosition_) {
        // Found a new local minimum
        currentMinIndex_ = currentBufferIndex;
        currentMinPosition_ = currentKeyPosition;
        currentMinTimestamp_ = timestamp;
    }
    
    // Check if the deviation between min and max exceeds the threshold of significance,
    // and if so, figure out when a peak occurs
    //gMaxThreshold = std::numeric_limits<float>::quiet_NaN();
    if(!missing_value<key_position>::isMissing(currentMaxPosition_) &&
       !missing_value<key_position>::isMissing(lastMinMaxPosition_)) {
	auto diff = currentMaxPosition_ - lastMinMaxPosition_;
        if(currentMaxPosition_ - lastMinMaxPosition_ >= kPositionTrackerMinMaxSpacingThreshold && currentBufferIndex != currentMaxIndex_) {
            // We need to come down off the current maximum before we can be sure that we've found the right location.
            // Implement a sliding threshold that gets lower the farther away from the maximum we get
            key_position triggerThreshold = kPositionTrackerMinMaxSpacingThreshold / (key_position)(currentBufferIndex - currentMaxIndex_);
	    gMaxThreshold = currentMaxPosition_ - triggerThreshold;
            
            if(currentKeyPosition < currentMaxPosition_ - triggerThreshold) {
                // Found the local maximum and the position has already retreated from it
                lastMinMaxPosition_ = currentMaxPosition_;

		if(currentState_ == kPositionTrackerStatePressInProgress) {
		    //rt_printf("==============================STATEDOWN: max: %f(at %f), current: %f(at %f), diff: %f, limit: %f\n",
				    //currentMaxPosition_, currentMaxTimestamp_, currentKeyPosition, timestamp, diff, kPositionTrackerMinMaxSpacingThreshold);
                    // If we were waiting for a press to complete, this is it.
                    pressIndex_ = currentMaxIndex_;
                    pressPosition_ = currentMaxPosition_;
                    pressTimestamp_ = currentMaxTimestamp_;
                    
                    // Insert the state change into the buffer timestamped according to
                    // when the maximum arrived, unless that would put it earlier than what's already there
                    timestamp_type stateChangeTimestamp = latestTimestamp() > currentMaxTimestamp_ ? latestTimestamp() : currentMaxTimestamp_;
                    changeState(kPositionTrackerStateDown, stateChangeTimestamp);
                }
                else if(currentState_ == kPositionTrackerStatePartialPressAwaitingMax) {
                    // Otherwise if we were waiting for a maximum to occur that was
                    // short of a full press, this might be it if it is of sufficient size
			//gPercussed = 1.0;
                    if(currentMaxPosition_ >= kPositionTrackerFirstMaxThreshold
				    ) {
			    //gPercussed = 0.75;
			key_velocity diffPosition = keyBuffer_[currentMaxIndex_ - 1] - keyBuffer_[currentMaxIndex_ - 2];
			timestamp_diff_type diffTimestamp = keyBuffer_.timestampAt(currentMaxIndex_ - 1) - keyBuffer_.timestampAt(currentMaxIndex_ - 2);
			key_velocity instantaneousVelocity = calculate_key_velocity(diffPosition, diffTimestamp);
			//rt_printf("inst: %f\n", instantaneousVelocity);
			if(instantaneousVelocity > kPositionTrackerPeakInstantaneousVelocityMinThreshold)
			{
				//rt_printf("==========gotit %f\n", instantaneousVelocity);
				//gPercussed = 0.5;
				percussivenessAvailableIndex_ = currentBufferIndex + kSamplesNeededForPercussiveness;
				timestamp_type stateChangeTimestamp = latestTimestamp() > currentMaxTimestamp_ ? latestTimestamp() : currentMaxTimestamp_;
				changeState(kPositionTrackerStatePartialPressFoundMax, stateChangeTimestamp);
			}
                    } else {
			    //if(currentMaxPosition_ > 0)
				    //rt_printf("Max at %f didn't meet requirements\n", currentMaxPosition_);
		    
		    }
                }
                else if (kPositionTrackerStateReleaseFinished == currentState_) {
                    releaseMaxPosition_ = currentMaxPosition_;
                    releaseMaxTimestamp_  = currentMaxTimestamp_;
                }

                
                // Reinitialize the minimum value for the next search
                currentMinIndex_ = currentBufferIndex;
                currentMinPosition_ = currentKeyPosition;
                currentMinTimestamp_ = timestamp;
            }
        }
    }
    if(!missing_value<key_position>::isMissing(currentMinPosition_) &&
       !missing_value<key_position>::isMissing(lastMinMaxPosition_)) {
        if(lastMinMaxPosition_ - currentMinPosition_ >= kPositionTrackerMinMaxSpacingThreshold && currentBufferIndex != currentMinIndex_) {
            // We need to come up from the current minimum before we can be sure that we've found the right location.
            // Implement a sliding threshold that gets lower the farther away from the minimum we get
            key_position triggerThreshold = kPositionTrackerMinMaxSpacingThreshold / (key_position)(currentBufferIndex - currentMinIndex_);

	    gMinThreshold = currentMinPosition_ + triggerThreshold;
            if(currentKeyPosition > currentMinPosition_ + triggerThreshold) {
                // Found the local minimum and the position has already retreated from it
                lastMinMaxPosition_ = currentMinPosition_;
                
                if(currentState_ == kPositionTrackerStatePressInProgress
				|| currentState_ == kPositionTrackerStatePartialPressAwaitingMax
				|| currentState_ == kPositionTrackerStatePartialPressFoundMax
		) {
			//rt_printf("scheduling percussivenessAvailable: %u, state: %s\n", currentBufferIndex + 1, statesDesc[currentState_].c_str());
			//percussivenessAvailableIndex_ = currentBufferIndex + 1;
		}
                // If in the middle of releasing, see whether this minimum appears to have completed the release
                if(currentState_ == kPositionTrackerStateReleaseInProgress) {
			//rt_printf("currentMinPosition: %f, comp: %f\n", currentMinPosition_, kPositionTrackerReleaseFinishPosition);
                    if(currentMinPosition_ < kPositionTrackerReleaseFinishPosition) {
                        releaseEndIndex_ = currentMinIndex_;
                        releaseEndPosition_ = currentMinPosition_;
                        releaseEndTimestamp_ = currentMinTimestamp_;
                        
                        timestamp_type stateChangeTimestamp = latestTimestamp() > currentMinTimestamp_ ? latestTimestamp() : currentMinTimestamp_;
                        changeState(kPositionTrackerStateReleaseFinished, stateChangeTimestamp);
                    }
                }
                
                // Reinitialize the maximum value for the next search
                currentMaxIndex_ = currentBufferIndex;
                currentMaxPosition_ = currentKeyPosition;
                currentMaxTimestamp_ = timestamp;
            }
        }
    }
    static float oldPosition = currentKeyPosition;
    static float oldVelocity = 0;
    float velocity = currentKeyPosition - oldPosition;
oldPosition = currentKeyPosition;
float acc = velocity - oldVelocity;
oldVelocity = velocity;
float percVelThreshold = 1.3;
float newPerc = percussivenessFeatures_.percussiveness;
float avVel = percussivenessFeatures_.velocityAverageAroundSpike;
    float myArr[] = {
	    currentKeyPosition, //1 red
	    gPercussed*0.5f, //2 blue
             velocity, // 3 green
	    currentState_/(float)kPositionTrackerStateReleaseFinished, //4 pink
	     currentMaxPosition_, // 5 light blue
	     acc, // 6 purple
	     releaseMaxPosition_, //7 red
	     percussivenessFeatures_.percussiveness // 8 blue
	    //lastMinMaxPosition_, //2 blue
	    //currentMaxPosition_, //3 green
	    //currentMinPosition_, //5 light blue
	    //gMaxThreshold, //6 purple
	     //-newPerc/avVel, //10 heavy pink
	     //newPerc / avVel > percVelThreshold ? -2.f*newPerc : 0 // 10 heavy pink
    };
    for(int n = 0; n < sizeof(myArr)/sizeof(float); ++n)
    {
	    myArr[n] *= 1;
    }
    myArr[3] = currentState_/(float)kPositionTrackerStateReleaseFinished;
    //scope.log(myArr);
}

// Change the current state of the tracker and generate a notification
void KeyPositionTracker::changeState(int newState, timestamp_type timestamp) {
    KeyPositionTracker::key_buffer_index index;
    KeyPositionTracker::key_buffer_index mostRecentIndex = 0;
    
    if(keyBuffer_.empty())
        mostRecentIndex = keyBuffer_.endIndex() - 1;
    //rt_fprintf(stderr, "%d %s\n", newState, statesDesc[newState].c_str());
    
    // Manage features based on state
    switch(newState) {
        case kPositionTrackerStatePressInProgress:
            // Clear features for a retrigger
            if(currentState_ == kPositionTrackerStateReleaseInProgress ||
               currentState_ == kPositionTrackerStateReleaseFinished)
                currentlyAvailableFeatures_ = KeyPositionTrackerNotification::kFeaturesNone;
            
            // Look for percussiveness first since it will always be available by the time of
            // key press. That means we can count on it arriving before velocity every time.
            if((currentlyAvailableFeatures_ & KeyPositionTrackerNotification::kFeaturePercussiveness) == 0
               && percussivenessAvailableIndex_ == 0) {
		    //TODO REENABLE
                //currentlyAvailableFeatures_ |= KeyPositionTrackerNotification::kFeaturePercussiveness;
                //notifyFeature(KeyPositionTrackerNotification::kNotificationTypeFeatureAvailablePercussiveness, timestamp);
                //percussivenessAvailableIndex_ = 0;
            }
            
            // Start looking for the data needed for MIDI onset velocity.
            // Where did the key cross the escapement position? How many more samples do
            // we need to calculate velocity?
            index = findMostRecentKeyPositionCrossing(pressVelocityEscapementPosition_, false, 1000);
            if(index + kPositionTrackerSamplesNeededForPressVelocityAfterEscapement <= mostRecentIndex) {
		    std::cout << "NOOOOOOOOOOOOO\n";
                // Here, we already have the velocity information
                currentlyAvailableFeatures_ |= KeyPositionTrackerNotification::kFeaturePressVelocity;
                notifyFeature(KeyPositionTrackerNotification::kNotificationTypeFeatureAvailableVelocity, timestamp);
            }
            else {
                // Otherwise, we need to send a notification when the information becomes available
                pressVelocityAvailableIndex_ = index + kPositionTrackerSamplesNeededForPressVelocityAfterEscapement;
		    //std::cout << "pressVelocityAvailableIndex_: " << pressVelocityAvailableIndex_ << "(set at index " << index << "\n";
            }
            break;
        case kPositionTrackerStateReleaseInProgress:
            // Start looking for the data needed for MIDI release velocity.
            // Where did the key cross the release escaoentb position? How many more samples do
            // we need to calculate velocity?
            prepareReleaseVelocityFeature(mostRecentIndex, timestamp);
            break;
        case kPositionTrackerStatePartialPressFoundMax:
            // Also look for the percussiveness features, if not already present
            if((currentlyAvailableFeatures_ & KeyPositionTrackerNotification::kFeaturePercussiveness) == 0
               && percussivenessAvailableIndex_ == 0) {
                currentlyAvailableFeatures_ |= KeyPositionTrackerNotification::kFeaturePercussiveness;
                notifyFeature(KeyPositionTrackerNotification::kNotificationTypeFeatureAvailablePercussiveness, timestamp);
                percussivenessAvailableIndex_ = 0;
            }
            break;
        case kPositionTrackerStateReleaseFinished:
            releaseFinishedTimestamp_  = timestamp;
            releaseFinishedPosition_ =  keyBuffer_.latest();
            break;
        case kPositionTrackerStatePartialPressAwaitingMax:
        case kPositionTrackerStateUnknown:
            // Reset all features
            currentlyAvailableFeatures_ = KeyPositionTrackerNotification::kFeaturesNone;
            break;
        case kPositionTrackerStateDown:
        default:
            // Don't change features
            break;
    }
    
    currentState_ = newState;
    
    KeyPositionTrackerNotification notification;
    notification.type = KeyPositionTrackerNotification::kNotificationTypeStateChange;
    notification.state = newState;
    notification.features = currentlyAvailableFeatures_;
    
    insert(notification, timestamp);
}

// Notify listeners that a given feature has become available
void KeyPositionTracker::notifyFeature(int notificationType, timestamp_type timestamp) {
    // Can now calculate press velocity
    KeyPositionTrackerNotification notification;
    
    notification.state = currentState_;
    notification.type = notificationType;
    notification.features = currentlyAvailableFeatures_;
    
    insert(notification, timestamp);
}

// When starting from a blank state, retroactively locate
// the start of the key press so it can be used to calculate
// features of key motion
void KeyPositionTracker::findKeyPressStart(timestamp_type timestamp) {
    if(keyBuffer_.size() < kPositionTrackerSamplesToAverageForStartVelocity + 1)
        return;
    
    key_buffer_index index = keyBuffer_.endIndex() - 1;
    int searchBackCounter = 0;
    
    while(index >= keyBuffer_.beginIndex() + kPositionTrackerSamplesToAverageForStartVelocity && searchBackCounter <= kPositionTrackerSamplesToSearchForStartLocation) {
        // Take the N-sample velocity average and compare to a minimum threshold
        key_position diffPosition = keyBuffer_[index] - keyBuffer_[index - kPositionTrackerSamplesToAverageForStartVelocity];
        timestamp_diff_type diffTimestamp = keyBuffer_.timestampAt(index) - keyBuffer_.timestampAt(index - kPositionTrackerSamplesToAverageForStartVelocity);
        key_velocity velocity = calculate_key_velocity(diffPosition, diffTimestamp);
        
        if(velocity < kPositionTrackerStartVelocityThreshold) {
            break;
        }
        
        searchBackCounter++;
        index--;
    }
    
    // Having either found the minimum velocity or reached the beginning of the search period,
    // store the key start information. Since the velocity is calculated over a window, choose
    // a start position in the middle of the window.
    startIndex_ = index - kPositionTrackerSamplesToAverageForStartVelocity/2;
    startPosition_ = keyBuffer_[index - kPositionTrackerSamplesToAverageForStartVelocity/2];
    startTimestamp_ = keyBuffer_.timestampAt(index - kPositionTrackerSamplesToAverageForStartVelocity/2);
//rt_printf("findKeyPressStart() resets lastMinMaxPosition_: %f (was %f)\n", startPosition_, lastMinMaxPosition_);
    lastMinMaxPosition_ = startPosition_;
    
    // After saving that information, look further back for a specified number of samples to see if there
    // is another mini-spike at the beginning of the key press. This can happen with highly percussive presses.
    // If so, the start is actually the earlier time.
    
    // Leave index where it was...
    searchBackCounter = 0;
    bool haveFoundVelocitySpike = false, haveFoundNewMinimum = false;
    
    while(index >= keyBuffer_.beginIndex() + kPositionTrackerSamplesToAverageForStartVelocity && searchBackCounter <= kPositionTrackerSamplesToSearchBeyondStartLocation) {
        // Take the N-sample velocity average and compare to a minimum threshold
        key_position diffPosition = keyBuffer_[index] - keyBuffer_[index - kPositionTrackerSamplesToAverageForStartVelocity];
        timestamp_diff_type diffTimestamp = keyBuffer_.timestampAt(index) - keyBuffer_.timestampAt(index - kPositionTrackerSamplesToAverageForStartVelocity);
        key_velocity velocity = calculate_key_velocity(diffPosition, diffTimestamp);
        
        if(velocity > kPositionTrackerStartVelocitySpikeThreshold) {
            std::cout << "At index " << index << ", velocity is " << velocity << std::endl;
            haveFoundVelocitySpike = true;
        }
        
        if(velocity < kPositionTrackerStartVelocityThreshold && haveFoundVelocitySpike) {
            std::cout << "At index " << index << ", velocity is " << velocity << std::endl;
            haveFoundNewMinimum = true;
            break;
        }
        
        searchBackCounter++;
        index--;
    }
    
    if(haveFoundNewMinimum) {
        // Here we looked back beyond a small spike and found an earlier start time
        startIndex_ = index - kPositionTrackerSamplesToAverageForStartVelocity/2;
        startPosition_ = keyBuffer_[index - kPositionTrackerSamplesToAverageForStartVelocity/2];
        startTimestamp_ = keyBuffer_.timestampAt(index - kPositionTrackerSamplesToAverageForStartVelocity/2);
rt_printf("haveFoundNewMinimum resets lastMinMaxPosition_: %f (was %f)\n", startPosition_, lastMinMaxPosition_);
        lastMinMaxPosition_ = startPosition_;
        
        std::cout << "Found previous location\n";
    }
}

// When a key is released, retroactively locate where the release started
void KeyPositionTracker::findKeyReleaseStart(timestamp_type timestamp) {
    if(keyBuffer_.size() < kPositionTrackerSamplesToAverageForStartVelocity + 1)
        return;
    
    key_buffer_index index = keyBuffer_.endIndex() - 1;
    int searchBackCounter = 0;
    
    while(index >= keyBuffer_.beginIndex() + kPositionTrackerSamplesToAverageForStartVelocity && searchBackCounter <= kPositionTrackerSamplesToSearchForReleaseLocation) {
        // Take the N-sample velocity average and compare to a minimum threshold
        key_position diffPosition = keyBuffer_[index] - keyBuffer_[index - kPositionTrackerSamplesToAverageForStartVelocity];
        timestamp_diff_type diffTimestamp = keyBuffer_.timestampAt(index) - keyBuffer_.timestampAt(index - kPositionTrackerSamplesToAverageForStartVelocity);
        key_velocity velocity = calculate_key_velocity(diffPosition, diffTimestamp);
        
        if(velocity > kPositionTrackerReleaseVelocityThreshold) {
            //std::cout << "Found release at index " << index << " (vel = " << velocity << ")\n";
            break;
        }
        
        searchBackCounter++;
        index--;
    }
    
    // Having either found the minimum velocity or reached the beginning of the search period,
    // store the key release information.
    releaseBeginIndex_ = index - kPositionTrackerSamplesToAverageForStartVelocity/2;
    releaseBeginPosition_ = keyBuffer_[index - kPositionTrackerSamplesToAverageForStartVelocity/2];
    releaseBeginTimestamp_ = keyBuffer_.timestampAt(index - kPositionTrackerSamplesToAverageForStartVelocity/2);
//rt_printf("releaseBeginPosition resets lastMinMaxPosition_: %f (was %f)\n", releaseBeginPosition_, lastMinMaxPosition_);
    lastMinMaxPosition_ = releaseBeginPosition_;
    
    // Clear the release end position so there's no possibility of an inconsistent state
    releaseEndIndex_ = 0;
    releaseEndPosition_ = missing_value<key_position>::missing();
    releaseEndTimestamp_ = missing_value<timestamp_type>::missing();
}

// Find the index at which the key position crosses the given threshold. Returns 0 if not found.
KeyPositionTracker::key_buffer_index KeyPositionTracker::findMostRecentKeyPositionCrossing(key_position threshold, bool greaterThan, int maxDistance) {
    if(keyBuffer_.empty())
        return 0;
    
    key_buffer_index index = keyBuffer_.endIndex() - 1;
    int pos = keyBuffer_.posOf(index);
    int searchBackCounter = 0;
    
    // Check if the most recent sample already meets the criterion. If so,
    // there's no crossing yet.
    if(keyBuffer_[index] >= threshold && greaterThan)
        return 0;
    if(keyBuffer_[index] <= threshold && !greaterThan)
        return 0;
    
    while(index >= keyBuffer_.beginIndex() && searchBackCounter <= maxDistance) {
        if(keyBuffer_[index] >= threshold && greaterThan)
            return index;
        else if(keyBuffer_[index] <= threshold && !greaterThan)
            return index;
        
        searchBackCounter++;
        index--;
    }
    
    return 0;
}

void KeyPositionTracker::prepareReleaseVelocityFeature(KeyPositionTracker::key_buffer_index mostRecentIndex, timestamp_type timestamp) {
    KeyPositionTracker::key_buffer_index index;

    // Find the sample where the key position crosses the release threshold. What is returned
    // will be the last sample which is above the threshold. What we need is the first sample
    // below the threshold plus at least one more (SamplesNeededForReleaseVelocity...) to
    // perform a local velocity calculation.
    index = findMostRecentKeyPositionCrossing(releaseVelocityEscapementPosition_, true, 1000);

    if(index == 0) {
        // Haven't crossed the threshold yet
        releaseVelocityWaitingForThresholdCross_ = true;
    }
    else if(index + kPositionTrackerSamplesNeededForReleaseVelocityAfterEscapement + 1 <= mostRecentIndex) {
        // Here, we already have the velocity information
        //std::cout << "release available, at index = " << keyBuffer_[index] << ", most recent position = " << keyBuffer_[mostRecentIndex] << std::endl;
        currentlyAvailableFeatures_ |= KeyPositionTrackerNotification::kFeatureReleaseVelocity;
        notifyFeature(KeyPositionTrackerNotification::kNotificationTypeFeatureAvailableReleaseVelocity, timestamp);
        releaseVelocityWaitingForThresholdCross_ = false;
    }
    else {
        // Otherwise, we need to send a notification when the information becomes available
        //std::cout << "release available at index " << index + kPositionTrackerSamplesNeededForReleaseVelocityAfterEscapement + 1 << std::endl;
        releaseVelocityAvailableIndex_ = index + kPositionTrackerSamplesNeededForReleaseVelocityAfterEscapement + 1;
        releaseVelocityWaitingForThresholdCross_ = false;
    }
}
void KeyPositionTracker::insert(KeyPositionTrackerNotification notification, timestamp_type timestamp)
{
	empty_ = false;
    //std::cout << "---Notification: " << KeyPositionTrackerNotification::desc[notification.type] << ", state: " << statesDesc[notification.state] << " at " << timestamp;
    if(notification.type == KeyPositionTrackerNotification::kNotificationTypeFeatureAvailableVelocity)
    {
	    if(gPrint > 1)
		    rt_printf("   v %7.5f\n", pressVelocity().second);
    } else if(notification.type == KeyPositionTrackerNotification::kNotificationTypeFeatureAvailablePercussiveness)
    {
		percussivenessFeatures_ = pressPercussiveness();
		auto& p = percussivenessFeatures_;
		if(gPrint > 0) {
			if(!missing_value<float>::isMissing(p.percussiveness)) {
				if(p.percussiveness)
				{
				if(gPrint > 1)
				{
				rt_printf("p: %10.5f, ", p.percussiveness);
				rt_printf("velspikemax: %10.5f, ", p.velocitySpikeMaximum.position);
				rt_printf("velspikemin: %10.5f, ", p.velocitySpikeMinimum.position);
				rt_printf("velAverage: %10.5f, ",  p.velocityAverageAroundSpike);
				rt_printf("timeFromSTartToSpike: %10.5f, ", p.timeFromStartToSpike);
				rt_printf("areaPrecedingSpike: %10.5f, ", p.areaPrecedingSpike);
				rt_printf("areaFollowingSpike: %10.5f, ", p.areaFollowingSpike);
				rt_printf("\n");
				} else {
					//rt_printf("p: perc at %f\n", timestamp);
				}
				}
			} else {
				rt_printf("------------Percussiveness was nan\n");
			}
		}
    }
    if(kPositionTrackerStatePartialPressFoundMax == currentState_ && notification.type != KeyPositionTrackerNotification::kNotificationTypeFeatureAvailablePercussiveness)
    {
	    //rt_printf("didn't get perc\n");
    }
    latestTimestamp_ = timestamp;
}

KeyPositionTracker::Event KeyPositionTracker::getPercussiveness()
{
	Event event;
	if(percussivenessFeatures_.hasBeenRead == false && percussivenessFeatures_.percussiveness)
	{
		event = percussivenessFeatures_.velocitySpikeMaximum;
		//rt_printf("sharing percussiveness: %f\n", event.position);
		percussivenessFeatures_.hasBeenRead = true;
	} else {
		event.index = missing_value<key_buffer_index>::missing();
		event.position = missing_value<key_position>::missing();
		event.timestamp = missing_value<timestamp_type>::missing();
	}
	return event;
}
