extern "C" int rt_printf(const char *format, ...);

typedef struct {
	float emb;
	float freq;
	float finalPitch;
} TuningData;

static TuningData gTuning1Up[] = {
{1.0, 0, 60},
{1.05, 0, 60.20},
{1.1, 0, 60.39},
{1.15, 0, 60.56},
{1.10, 0.333, 60.72},
{1.05, 0.666, 60.86},
{1.00, 1, 61}
};
static TuningData gTuning2Up[] = {
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
static TuningData gTuning3Up[] = {
{1.0, 0, 60},
{1.05, 0, 60.2},
{1.1, 0, 60.39},
{1.15, 0, 60.56},
{1.2, 0, 60.71},
{1.25, 0.5, 60.86},
{1.3, 0, 61.0},
{1.35, 0, 61.13},
{1.4, 0, 61.25},
{1.45, 0, 61.36},
{1.5, 0, 61.47},
{1.55, 0, 61.57},
{1.6, 0, 61.67},
{1.55, 0.21, 61.78},
{1.50, 0.42, 61.89},
{1.45, 0.64, 62.00},
{1.40, 0.88, 62.12},
{1.35, 1.12, 62.24},
{1.30, 1.37, 62.36},
{1.25, 1.53, 62.48},
{1.2, 1.9, 62.60},
{1.15, 2.18, 62.72},
{1.1, 2.48, 62.85},
{1.05, 2.72, 62.90},
{1.00, 3.0, 63.00} // was 3.02, rounded because now that is handled by fixTuning
};
static TuningData gTuning4Up[] = {
{1.0, 0, 60},
{1.05, 0, 60.2},
{1.1, 0, 60.39},
{1.15, 0, 60.56},
{1.2, 0, 60.71},
{1.25, 0.5, 60.86},
{1.3, 0, 61.0},
{1.35, 0, 61.13},
{1.4, 0, 61.25},
{1.45, 0, 61.36},
{1.5, 0, 61.47},
{1.55, 0, 61.57},
{1.6, 0, 61.67},
{1.65, 0, 61.75},
{1.7, 0, 61.84},
{1.75, 0, 61.93},
{1.7, 0.22, 62.06},
{1.65, 0.44, 62.19},
{1.6, 0.66, 62.32},
{1.55, 0.89, 62.45},
{1.5, 1.12, 62.58},
{1.45, 1.37, 62.72},
{1.4, 1.61, 62.85},
{1.35, 1.87, 62.98},
{1.3, 2.14, 63.12},
{1.25, 2.43, 63.27},
{1.2, 2.82, 63.52},
{1.15, 3.13, 63.67},
{1.1, 3.45, 63.81},
{1.05, 3.73, 63.91},
{1.00, 4.00, 64.00}, // was 4.03, rounded because now that is handled by fixTuning
};
static TuningData gTuning1Down[] = {
{1.0, 0, 60},
{0.95, 0, 59.78},
{0.9, 0, 59.52},
{0.95, -0.5, 59.28},
{1, -1, 59}
};
static TuningData gTuning2Down[] = {
{1.0, 0, 60},
{0.95, 0, 59.78},
{0.9, 0, 59.52},
{0.85, 0, 59.24},
{0.8, 0, 58.92},
{0.85, -0.56, 58.68},
{0.9, -1.08, 58.45},
{0.95, -1.56, 58.23},
{1.0, -2.00, 58.00}, // was -2.01, rounded because now that is handled by fixTuning
};

static TuningData gTuning3Down[] = {
{1.0, 0, 60},
{0.95, 0, 59.78},
{0.9, 0, 59.52},
{0.85, 0, 59.24},
{0.8, 0, 58.92},
{0.75, 0, 58.56},
{0.8, -0.67,  58.25},
{0.85, -1.3, 57.94},
{0.9, -1.9, 57.63},
{0.95, -2.47, 57.32},
{1.00, -3.00, 57.00}, // was -3.02, rounded because now that is handled by fixTuning
};

static TuningData gTuning4Down[] = {
{1.0, 0, 60},
{0.95, 0, 59.78},
{0.9, 0, 59.52},
{0.85, 0, 59.24},
{0.8, 0, 58.92},
{0.75, 0, 58.56},
{0.7, 0, 58.15},
{0.65, 0, 57.66},
{0.7, -0.74, 57.42},
{0.75, -1.38, 57.19},
{0.8, -1.97, 56.96},
{0.85, -2.53, 56.72},
{0.9, -3.06, 56.48},
{0.95, -3.56, 56.24},
{1.00, -4.00, 56.00}, // was -4.02, rounded because now that is handled by fixTuning
};
void getEmbFreq(int range, float idx, float& freq, float& emb)
{
#define ASSIGN_TUNING(vec) buf = vec; nrow = sizeof(vec)/sizeDen;
	int sizeDen = sizeof(TuningData);
	TuningData* buf = nullptr;
	int nrow = 0;
	switch (range){
		case 1:
			ASSIGN_TUNING(gTuning1Up);
			break;
		case 2:
			ASSIGN_TUNING(gTuning2Up);
			break;
		case 3:
			ASSIGN_TUNING(gTuning3Up);
			break;
		case 4:
			ASSIGN_TUNING(gTuning4Up);
			break;
		case -1:
			ASSIGN_TUNING(gTuning1Down);
			break;
		case -2:
			ASSIGN_TUNING(gTuning2Down);
			break;
		case -3:
			ASSIGN_TUNING(gTuning3Down);
			break;
		case -4:
			ASSIGN_TUNING(gTuning4Down);
			break;
		default:
			;
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
	emb -= 1;
}

/* fit a parabola to the experimentally-obtained points in Matlab
x = [
48, 0.07
54, 0.09
60, 0.125
66, 0.17
72, 0.23
78, 0.32
];

poly = polyfit(x(:,1), x(:,2), 1);
*/

// values computed by the Matlab code above. Only reliable within the given range
static float tuningCorrectionCoefficients[] = {0.00022321, -0.01995833, 0.51550000};

float fixTuning(float nominalFreq, float pressure)
{
	float x = nominalFreq;
	float a = tuningCorrectionCoefficients[0];
	float b = tuningCorrectionCoefficients[1];
	float c = tuningCorrectionCoefficients[2];
	float correction = a * x*x + b *x + c;
	float out = correction + nominalFreq;
	//rt_printf("tuning: %f %f at %f\n", nominalFreq, out, pressure);
	return out;
}
