/*
clang --target=wasm32-unknown-unknown -O3 -fno-builtin-memset -fno-builtin-memcpy -nostdlib -Wl,--no-entry -Wl,--export-all -Wl,--allow-undefined -o fm.wasm fm.c
*/

extern float js_sin(float x);
extern float js_cos(float x);

#define meta_exported __attribute__((visibility("default")))
#define M_PI 3.1415926535897932384626433832795
const float PI = M_PI;
const float PI_D_4 = M_PI / 4;
const float PI_D_2 = M_PI / 2;
const float PI_3_4 = M_PI / 4 * 3;
const float PI_M_2 = M_PI * 2;

#define M_AUDIO_SAMPLERATE 48000
#define M_FM_SAMPLERATE 500000
#define M_CHUNK_DURATION_MS 250
#define M_CHUNK_DURATION_S M_CHUNK_DURATION_MS/1000.0
#define M_FM_BUFFER_LEN (M_FM_SAMPLERATE*M_CHUNK_DURATION_MS/1000)
#define M_AUDIO_BUFFER_LEN (M_AUDIO_SAMPLERATE*M_CHUNK_DURATION_MS/1000)
#define M_LOWPASS_RING_MAX_SIZE 200

float lowpass_ring_buffer[M_LOWPASS_RING_MAX_SIZE];
float FM_buffer[M_FM_BUFFER_LEN];
float I[M_FM_BUFFER_LEN];
float Q[M_FM_BUFFER_LEN];
float AUDIO_MHz_buffer[M_FM_BUFFER_LEN]; // Store 1MHz & 48KHz version here

const float AUDIO_SAMPLERATE = M_AUDIO_SAMPLERATE;
const float FM_SAMPLERATE = M_FM_SAMPLERATE;
const float AUD_PER_FM_SAMPLERATE = AUDIO_SAMPLERATE / FM_SAMPLERATE;
const float CHUNK_DURATION_MS = M_CHUNK_DURATION_MS;
const float CHUNK_DURATION_S = M_CHUNK_DURATION_S;
const int FM_BUFFER_LEN = M_FM_BUFFER_LEN;
const int AUDIO_BUFFER_LEN = M_AUDIO_BUFFER_LEN;
const int LOWPASS_RING_MAX_SIZE = M_LOWPASS_RING_MAX_SIZE;

float min(float a, float b)
{
    return a > b ? b : a;
}

float max(float a, float b)
{
    return a < b ? b : a;
}

float fabsf(float x)
{
    *(int*)(&x) &= 0x7fffffff;
    return x;
}

float fatan(float x) {
  return PI_D_4*x - x*(fabsf(x) - 1)*(0.2447 + 0.0663*fabsf(x));
}

float fatan2(float y, float x)
{
    if (x == 0)
    {
        if (y > 0)
        {
            return PI_D_2;
        }
        else if (y < 0)
        {
            return -PI_D_2;
        }
        return 0;
    }
    else if (x < 0)
    {
        if (y >= 0)
        {
            return fatan(y / x) + PI;
        }
        return fatan(y / x) - PI;
    }
    else
    {
        return fatan(y / x);
    }
}

void lowPass(float *data, int length, int N)
{
    float sum = 0;
    int ringPos = 0;

    if (N > LOWPASS_RING_MAX_SIZE)
    {
        N = LOWPASS_RING_MAX_SIZE;
    }

    for (int i = 0; i < N; i++)
    {
        lowpass_ring_buffer[i] = 0;
    }

    for (int i = 0; i < length; i++)
    {
        sum -= lowpass_ring_buffer[ringPos];
        lowpass_ring_buffer[ringPos] = data[i];
        sum += data[i];
        ringPos = (ringPos + 1) % N;
        data[i] = sum / N;
    }
}

meta_exported
extern float getFMSamplerate()
{
    return FM_SAMPLERATE;
}

meta_exported
extern float getAUDSamplerate()
{
    return AUDIO_SAMPLERATE;
}

meta_exported
extern float getChunkMS()
{
    return CHUNK_DURATION_MS;
}

meta_exported
extern float getChunkS()
{
    return CHUNK_DURATION_S;
}

meta_exported
extern float *getFMBufPtr()
{
    return &FM_buffer[0];
}

meta_exported
extern float *getAudioBufPtr()
{
    return &AUDIO_MHz_buffer[0];
}

meta_exported
extern float *getAudioScratchBufPtr()
{
    return &(AUDIO_MHz_buffer + AUDIO_BUFFER_LEN)[0];
}

meta_exported
extern float getFMBufLength()
{
    return FM_BUFFER_LEN;
}

meta_exported
extern float getAudioBufLength()
{
    return AUDIO_BUFFER_LEN;
}

meta_exported
extern void init()
{
    //
}

meta_exported
extern void FM_modulate(float carrierFreq, float bandWidth)
{
    float *track = getAudioScratchBufPtr();
    float pos = 0.0;
    float amplitude = 0.125;
    float carrier_freq = 2 * PI * carrierFreq;
    float freq_sens = 2 * PI * bandWidth / 4 / FM_SAMPLERATE; // Since the thing is discrete
    // Carrier oscilator
    float cs_re = js_cos(2*PI*carrierFreq/FM_SAMPLERATE);
    float cs_im = js_sin(2*PI*carrierFreq/FM_SAMPLERATE);
    float c_re = 1.0f, c_im = 0.0f;
    // Mod oscilator
    float m_re = 1.0f, m_im = 0.0f;

    for (int i = 0; i < FM_BUFFER_LEN; i++)
    {
        // Calculate audio integral with a little smoothing
        float t = i * AUD_PER_FM_SAMPLERATE;
        float sample_0 = track[(int)t];
        float sample_1 = i == FM_BUFFER_LEN - 1 ? sample_0 : track[(int)t + 1];
        float factor = t - (int)t;
        float sample = sample_0 * factor + sample_1 * (1 - factor);

        pos += sample;
        // Calculate mod delta, I should do something for this later
        float mod_angle = freq_sens * sample;
        float ms_re = js_cos(mod_angle); 
        float ms_im = js_sin(mod_angle);
        // Spin by delta using the rotation matrix
        float new_mre = m_re*ms_re - m_im*ms_im;
        m_im = m_re*ms_im + m_im*ms_re;
        m_re = new_mre;
        // Take real part
        float total_re = c_re*m_re - c_im*m_im;
        FM_buffer[i] += amplitude * total_re;
        // Spin normally
        float new_cre = c_re*cs_re - c_im*cs_im;
        c_im = c_re*cs_im + c_im*cs_re;
        c_re = new_cre;
    }
}

meta_exported
extern void FM_demod(float carrierFreq)
{
    const int IQ_LOWPASS = 23;
    const int AUDIO_LOWPASS = 100;
    // A new oscillator, starts at 0 rad
    float step_re = js_cos(2 * PI * carrierFreq / FM_SAMPLERATE);
    float step_im = -js_sin(2 * PI * carrierFreq / FM_SAMPLERATE);
    float osc_re = 1.0f;
    float osc_im = 0.0f;
    for (int i = 0; i < FM_BUFFER_LEN; i++) {
        float s = FM_buffer[i];
        // I,Q = s*cos(x), s*sin(x)
        I[i] = s * osc_re;
        Q[i] = s * osc_im;
        /*
        This dude simulates sin and cos with a rotation matrix:
        A = |step_re,-step_im| = |cos S,-sin S|
            |step_im, step_re|   |sin S, cos S|
        osc = A*osc
        
        ↓ ¿Why this?: because we the need old osc_re for calculating new osc_im
        */
        float new_re = osc_re * step_re - osc_im * step_im;
        osc_im = osc_re * step_im + osc_im * step_re;
        osc_re = new_re;
    }

    lowPass(I, FM_BUFFER_LEN, IQ_LOWPASS);
    lowPass(Q, FM_BUFFER_LEN, IQ_LOWPASS);

    AUDIO_MHz_buffer[0] = 0.0f;

    for (int i = 1; i < FM_BUFFER_LEN; i++) {
        float re = I[i]*I[i-1] + Q[i]*Q[i-1];
        float im = Q[i]*I[i-1] - I[i]*Q[i-1];
        AUDIO_MHz_buffer[i] = fatan2(im, re);
    }

    lowPass(AUDIO_MHz_buffer, FM_BUFFER_LEN, AUDIO_LOWPASS);

    for (int i = 0; i < AUDIO_BUFFER_LEN; i++)
    {
        // Downsample and amplify
        AUDIO_MHz_buffer[i] = 6 * AUDIO_MHz_buffer[ (int)(i / AUDIO_SAMPLERATE * FM_SAMPLERATE) ];
    }
}