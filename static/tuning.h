#pragma once

void getEmbFreq(int range, float idx, float& freq, float& emb);
float fixTuning(float nominalFreq, float pressure);
float getNonLinearity(float freq, float emb);
