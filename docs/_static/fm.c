/*
clang --target=wasm32-unknown-unknown -O3 -fno-builtin-memset -fno-builtin-memcpy -nostdlib -Wl,--no-entry -Wl,--export-all -Wl,--allow-undefined -o fm.wasm fm.c
*/

extern float js_sin(float x);
extern float js_cos(float x);
extern float js_atan2(float x, float y);

#define meta_exported __attribute__((visibility("default")))

const float FM_MAX_MHZ = 1.0;
const float AUDIO_SAMPLERATE = 48000;
const float FM_SAMPLERATE = FM_MAX_MHZ * 1e6;
const float CHUNK_DURATION_MS = 500.0;
const float CHUNK_DURATION_S = CHUNK_DURATION_MS / 1000.0;

const int FM_buffer_length = (int)(FM_SAMPLERATE * CHUNK_DURATION_S);
const int AUDIO_buffer_length = (int)(AUDIO_SAMPLERATE * CHUNK_DURATION_S);
const int LOWPASS_RING_MAX_SIZE = 200;

float lowpass_ring_buffer[LOWPASS_RING_MAX_SIZE];
float FM_buffer[FM_buffer_length];
float I[FM_buffer_length];
float Q[FM_buffer_length];
float AUDIO_MHz_buffer[FM_buffer_length]; // Store 1MHz & 48KHz version here

const float PI = 3.14159265358979323846f;

float min(float a, float b)
{
    return a > b ? b : a;
}

float max(float a, float b)
{
    return a < b ? b : a;
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
extern float getMaxMHz()
{
    return FM_MAX_MHZ;
}

meta_exported
extern float *getFMBufPtr()
{
    return FM_buffer;
}

meta_exported
extern float *getAudioBufPtr()
{
    return AUDIO_MHz_buffer;
}

meta_exported
extern float *getAudioScratchBufPtr()
{
    return AUDIO_MHz_buffer + AUDIO_buffer_length;
}

meta_exported
extern float getFMBufLength()
{
    return FM_buffer_length;
}

meta_exported
extern float getAudioBufLength()
{
    return AUDIO_buffer_length;
}

meta_exported
extern void FM_modulate(float carrierFreq, float bandWidth)
{
    float pos = 0.0;
    float amplitude = 0.125;
    float carrier_freq = 2 * PI * carrierFreq;
    float freq_sens = 2 * PI * bandWidth / 4 / FM_SAMPLERATE; // Since the thing is discrete

    float *track = getAudioScratchBufPtr();

    float cs_re = js_cos(2*PI*carrierFreq/FM_SAMPLERATE);
    float cs_im = js_sin(2*PI*carrierFreq/FM_SAMPLERATE);
    
    float c_re = 1.0f, c_im = 0.0f;
    float m_re = 1.0f, m_im = 0.0f;

    float audio_step = (float)AUDIO_SAMPLERATE / FM_SAMPLERATE;
    
    for (int i = 0; i < FM_buffer_length; i++)
    {
        pos += track[(int)(i * audio_step)];
        // Calculate mod delta
        float mod_angle = freq_sens * track[(int)(i * audio_step)]; // Δ only
        float ms_re = js_cos(mod_angle); 
        float ms_im = js_sin(mod_angle);
        // Spin by delta
        float new_mre = m_re*ms_re - m_im*ms_im;
        float new_mim = m_re*ms_im + m_im*ms_re;
        m_re = new_mre; m_im = new_mim;
        // Take real part
        float total_re = c_re*m_re - c_im*m_im;
        FM_buffer[i] += amplitude * total_re;
        // Spin normally
        float new_cre = c_re*cs_re - c_im*cs_im;
        float new_cim = c_re*cs_im + c_im*cs_re;
        c_re = new_cre; c_im = new_cim;
    }
}

meta_exported
extern void FM_demod(float carrierFreq)
{
    const int IQ_LOWPASS = 23;
    const int AUDIO_LOWPASS = 100;
    const float OUT_MAXAMPL = 2;

    float step_re = js_cos(2 * PI * carrierFreq / FM_SAMPLERATE);
    float step_im = -js_sin(2 * PI * carrierFreq / FM_SAMPLERATE);

    float osc_re = 1.0f;  // current oscillator state, starts at angle 0
    float osc_im = 0.0f;

    for (int i = 0; i < FM_buffer_length; i++) {
        float s = FM_buffer[i];

        I[i] = s * osc_re;
        Q[i] = s * osc_im;

        float new_re = osc_re * step_re - osc_im * step_im;
        float new_im = osc_re * step_im + osc_im * step_re;
        osc_re = new_re;
        osc_im = new_im;
    }

    lowPass(I, FM_buffer_length, IQ_LOWPASS);
    lowPass(Q, FM_buffer_length, IQ_LOWPASS);

    AUDIO_MHz_buffer[0] = 0.0f;

    for (int i = 1; i < FM_buffer_length; i++) {
        float re = I[i]*I[i-1] + Q[i]*Q[i-1];
        float im = Q[i]*I[i-1] - I[i]*Q[i-1];
        AUDIO_MHz_buffer[i] = js_atan2(im, re);
    }

    lowPass(AUDIO_MHz_buffer, FM_buffer_length, AUDIO_LOWPASS);

    for (int i = 0; i < AUDIO_buffer_length; i++)
    {
        AUDIO_MHz_buffer[i] = min(OUT_MAXAMPL, max(-OUT_MAXAMPL, AUDIO_MHz_buffer[ (int)(i / AUDIO_SAMPLERATE * FM_SAMPLERATE) ] ) );
    }
}