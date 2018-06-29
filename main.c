#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>         /*function log*/
#include <linux/time.h>
#include <memory.h>
#include <portaudio.h>
#include <stdbool.h>

/*SAMPLE_RATE,  FRAMES_PER_BUFFER, NUM_SECONDS, NUM_CHANNELS*/
#define SAMPLE_RATE (16000)
#define FRAMES_PER_BUFFER (400)
#define NUM_SECONDS     (60)
#define NUM_CHANNELS    (1)

/* Select sample format. */
#if 0
#define PA_SAMPLE_TYPE  paFloat32
typedef float SAMPLE;
#define SAMPLE_SILENCE  (0.0f)
#define PRINTF_S_FORMAT "%.8f"
#elif 1
#define PA_SAMPLE_TYPE  paInt16
typedef short SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#elif 0
#define PA_SAMPLE_TYPE  paInt8
typedef char SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#else
#define PA_SAMPLE_TYPE  paUInt8
typedef unsigned char SAMPLE;
#define SAMPLE_SILENCE  (128)
#define PRINTF_S_FORMAT "%d"
#endif

typedef struct
{
	int          frameIndex;  /* Index into sample array. */
	int          maxFrameIndex;
	SAMPLE      *recordedSamples;
}
paTestData;


/*An Adaptive Endpointing Algorithm*/
const float forgetfactor = 1;
const float adjustment = 0.05;
/*key value for classifyFrame(), need to adjust to different environment.*/
const float threshold = 30; /*Energy cut*/
float background = 0;
float level = 0;

uint64_t silenceInterval = 10000000000ULL;  /*10 sec*/
//uint32_t silenceInterval = 10;  /*10 sec*/
uint64_t currentTime = 0;
uint64_t speakTime = 0;
bool isSpeech = false;

uint64_t os_gettime_ns(void)
{
    struct timespec ts;
    memset(&ts, 0, sizeof(ts));
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec);
}


/*calculate the energy in decibe of a frame segment*/
float energyPerSampleInDecibe(const SAMPLE *ptr)
{
	float energy = 0.0f;
	SAMPLE temp;
	for (unsigned long i = 0; i<FRAMES_PER_BUFFER; i++)
	{
		temp = *(ptr + i);
		energy += temp * temp;
	}
	energy = 10 * log(energy);
	return energy;
}


bool classifyFrame(const SAMPLE *ptr)
{
	float current = energyPerSampleInDecibe(ptr);
	printf("current = %f\n", current);
	//bool isSpeech = false;
	level = ((level * forgetfactor) + current) / (forgetfactor + 1);
	printf("level_before = %f\n", level);
	if (current < background)
		background = current;
	else
		background += (current - background) * adjustment;
	printf("background = %f\n", background);
	if (level < background)
		level = background;
	printf("level_after = %f\n", level);
	if (level - background > threshold){
		isSpeech = true;
		speakTime = os_gettime_ns();
		printf("Now there is sound=======================================\n");
	}else{
		currentTime = os_gettime_ns();
		printf("currentTime - speakTime = %llu\n", currentTime - speakTime);
		if( (currentTime - speakTime) >= silenceInterval ){
			isSpeech = false;
			printf("no sound#####################################################\n");
		}
	}
	return isSpeech;
}
/* This routine will be called by the PortAudio engine when audio is needed. 
 ** It may be called at interrupt level on some machines so don't do anything 
 ** that could mess up the system like calling malloc() or free(). 
 */  
static int recordCallback(const void *inputBuffer, void *outputBuffer,  
		unsigned long framesPerBuffer,  
		const PaStreamCallbackTimeInfo* timeInfo,  
		PaStreamCallbackFlags statusFlags,  
		void *userData)  
{  
	paTestData *data = (paTestData*)userData;  
	const SAMPLE *rptr = (const SAMPLE*)inputBuffer;  
	SAMPLE *wptr = &data->recordedSamples[data->frameIndex * NUM_CHANNELS];  
	long framesToCalc;  
	long i;  
	int finished;  
	unsigned long framesLeft = data->maxFrameIndex - data->frameIndex;  

	(void)outputBuffer; /* Prevent unused variable warnings. */  
	(void)timeInfo;  
	(void)statusFlags;  
	(void)userData;  
	
	if (framesLeft < framesPerBuffer)  
	{  
		framesToCalc = framesLeft;  
		finished = paContinue;  
	}  
	else  
	{  
		framesToCalc = framesPerBuffer;  
		finished = paContinue;  
	}  


	if (inputBuffer == NULL)  
	{  
		for (i = 0; i<framesToCalc; i++)  
		{  
			*wptr++ = SAMPLE_SILENCE;  /* left */  
			if (NUM_CHANNELS == 2) *wptr++ = SAMPLE_SILENCE;  /* right */  
		}  
	}  
	else  
	{  
		for (i = 0; i<framesToCalc; i++)  
		{  
			*wptr++ = *rptr++;  /* left */  
			if (NUM_CHANNELS == 2) *wptr++ = *rptr++;  /* right */  
		}  
	}  
	data->frameIndex += framesToCalc;  
	/* calculate the initial background and initial level, 
	 ** which will be used for classify frame 
	 **  
	 */  
	if (data->frameIndex == 0)  
	{  
		level = energyPerSampleInDecibe(&data->recordedSamples[0]);  
		background = 0.0f;  
		SAMPLE temp;  
		for (i = 0; i < 10 * framesPerBuffer; i++)  
		{  
			temp = data->recordedSamples[i];  
			background += temp * temp;  
		}  
		background = log(background);  
	}  
	/*Silence in "silenceNum" seconds means the end of audio capture  */
	if (classifyFrame(rptr)) 

	return finished;  
}


/*******************************************************************/
int main(void)
{
	PaStreamParameters  inputParameters,
			    outputParameters;
	PaStream*           stream;
	PaError             err = paNoError;
	paTestData          data;
	int                 i;
	int                 totalFrames;
	int                 numSamples;
	int                 numBytes;
	SAMPLE              max, val;
	double              average;


	data.maxFrameIndex = totalFrames = NUM_SECONDS * SAMPLE_RATE;    /* Record for a few seconds. */
	data.frameIndex = 0;
	numSamples = totalFrames * NUM_CHANNELS;
	numBytes = numSamples * sizeof(SAMPLE);
	data.recordedSamples = (SAMPLE *)malloc(numBytes); /* From now on, recordedSamples is initialised. */
	if (data.recordedSamples == NULL)
	{
		printf("Could not allocate record array.\n");
		goto done;
	}
	for (i = 0; i<numSamples; i++) data.recordedSamples[i] = 0;

	err = Pa_Initialize();
	if (err != paNoError)
		goto done;

	inputParameters.device = Pa_GetDefaultInputDevice(); /* default input device */
	if (inputParameters.device == paNoDevice) {
		printf("Error: No default input device.\n");
		goto done;
	}
	inputParameters.channelCount = 2;                    /* stereo input */
	inputParameters.sampleFormat = PA_SAMPLE_TYPE;
	inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
	inputParameters.hostApiSpecificStreamInfo = NULL;

	/*set a keyboard hit to start recording.*/
	printf("Press any key to start recording\n");
	getchar();

	/* Record some audio. -------------------------------------------- */
	err = Pa_OpenStream(
			&stream,
			&inputParameters,
			NULL,                  /* &outputParameters, */
			SAMPLE_RATE,
			FRAMES_PER_BUFFER,
			paClipOff,      /* we won't output out of range samples so don't bother clipping them */
			recordCallback,
			&data);
	if (err != paNoError)
	{
		printf("Pa_OpenStream err = %d\n", err);
		goto done;
	}

	err = Pa_StartStream(stream);
	if (err != paNoError) 
	{
		printf("Pa_StartStream err = %d\n", err);
		goto done;
	}
	printf("\n=== Now start recording!!\n"); fflush(stdout);
	/* Pa_IsStreamActive: Determine whether the stream is active. A stream
	   is active after a successful call to Pa_StartStream(), until it becomes
	   inactive either as a result of a call to Pa_StopStream() or Pa_AbortStream(),
	   or as a result of a return value other than paContinue from the stream callback.
	   In the latter case, the stream is considered inactive after the last buffer has finished playing. */
	while ((err = Pa_IsStreamActive(stream)) == 1)
	{
		Pa_Sleep(1000);
		/*printf("index = %d\n", data.frameIndex);*/
	}
	if (err < 0) 
		goto done;

	err = Pa_CloseStream(stream);
	if (err != paNoError) 
		goto done;


done:
	Pa_Terminate();
	if (data.recordedSamples)       /* Sure it is NULL or valid. */
		free(data.recordedSamples);
	if (err != paNoError)
	{
		printf("An error occured while using the portaudio stream\n");
		printf("Error number: %d\n", err);
		printf("Error message: %s\n", Pa_GetErrorText(err));
		err = 1;          /* Always return 0 or 1, but no other return codes. */
	}
	//system("pause");
	return err;
}
