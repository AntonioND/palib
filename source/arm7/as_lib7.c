/*

  Advanced Sound Library (ASlib)
  ------------------------------

  file        : sound7.c 
  author      : Lasorsa Yohan (Noda)
  description : ARM7 sound functions

  history : 

    02/12/2007 - v1.0
      = Original release
      
    21/12/2007 - v1.1
      = corrected arm7/amr9 initialization (fix M3S/R4 problems)
      = fixed stereo detection problem (thanks to ThomasS)
      
      
  credits :
    - IPC sound system and streaming is based on cold_as_ice streaming example
    - audio stream filling / mp3 decoding based on ThomasS mp3 decoding example
    - ASM stereo desinterleave function was made by Thoduv

*/

#include <nds.h>
#include <string.h>

#include <PA_Transfer.h>

#include "helix/mp3dec.h"
#include "helix/mp3common.h"
#include "helix/real/coder.h"
#include <arm7/as_lib7.h>

// internal functions
void AS_InitMP3();
void AS_MP3Stop();
static void AS_StartTimer(int freq);
static void AS_SetTimer(int freq);
static void AS_StopTimer();
void AS_RegenStream();
void AS_RegenStreamCallback(s16 *stream, u32 numsamples);
void AS_StereoDesinterleave(s16 *input, s16 *outputL, s16 *outputR, u32 samples);
void AS_MP3ClearBuffers();

// variables for the mp3 player
HMP3Decoder hMP3Decoder;
MP3FrameInfo mp3FrameInfo;
u8 *readPtr;
int bytesLeft;

s16 audioBuf[AS_DECODEBUFFER_SIZE];     // buffer for the decoded mp3 audio
int nAudioBufStart;
int nAudioBuf;
u8 stereo;


// the sound engine, must be called each VBlank
void AS_SoundVBL()
{
    int i;

    // adjust master volume
    if(IPC_Sound->chan[0].cmd & SNDCMD_SETMASTERVOLUME) {
        REG_MASTER_VOLUME = SOUND_VOL(IPC_Sound->volume & 127);
        IPC_Sound->chan[0].cmd &= ~SNDCMD_SETMASTERVOLUME;
    }

    // manage sounds
    for(i = 0; i < 16; i++) {

        if(IPC_Sound->chan[i].cmd & SNDCMD_DELAY) 
        {
            if(IPC_Sound->chan[i].snd.delay == 0) {
                IPC_Sound->chan[i].cmd &= ~SNDCMD_DELAY;
                IPC_Sound->chan[i].cmd |= SNDCMD_PLAY;
            } else {
                IPC_Sound->chan[i].snd.delay -= 1;
            }
        }

        if(IPC_Sound->chan[i].cmd & SNDCMD_STOP) 
        {
            SCHANNEL_CR(i) = 0;
            IPC_Sound->chan[i].cmd &= ~SNDCMD_STOP;
        }

        if(IPC_Sound->chan[i].cmd & SNDCMD_PLAY) 
        {
            SCHANNEL_TIMER(i) = 0x10000 - (0x1000000 / IPC_Sound->chan[i].snd.rate);
            SCHANNEL_SOURCE(i) = (u32)IPC_Sound->chan[i].snd.data;
            SCHANNEL_REPEAT_POINT(i) = 0;
            SCHANNEL_LENGTH(i) = IPC_Sound->chan[i].snd.size >> 2 ;
            SCHANNEL_CR(i) = SCHANNEL_ENABLE | ((SOUND_ONE_SHOT) >> (IPC_Sound->chan[i].snd.loop)) | SOUND_VOL(IPC_Sound->chan[i].volume & 127) | SOUND_PAN(IPC_Sound->chan[i].pan & 127) | ((IPC_Sound->chan[i].snd.format) << 29);
            IPC_Sound->chan[i].cmd &= ~SNDCMD_PLAY;
        }

        if(IPC_Sound->chan[i].cmd & SNDCMD_SETVOLUME) 
        {
            SCHANNEL_VOL(i) = IPC_Sound->chan[i].volume & 127;
            IPC_Sound->chan[i].cmd &= ~SNDCMD_SETVOLUME;
        }

        if(IPC_Sound->chan[i].cmd & SNDCMD_SETPAN) 
        {
            SCHANNEL_PAN(i) = IPC_Sound->chan[i].pan & 127;
            IPC_Sound->chan[i].cmd &= ~SNDCMD_SETPAN;
        }

        if(IPC_Sound->chan[i].cmd & SNDCMD_SETRATE) 
        {
            SCHANNEL_TIMER(i) = 0x10000 - (0x1000000 / IPC_Sound->chan[i].snd.rate);
            IPC_Sound->chan[i].cmd &= ~SNDCMD_SETRATE;
        }
        
        IPC_Sound->chan[i].busy = SCHANNEL_CR(i) >> 31;
    }
}

// the mp3 decoding engine, must be called on a regular basis (like after VBlank)
void AS_MP3Engine()
{
    s32 curtimer, numsamples;

    // All MP3 handling code needs to go here to avoid race conditions

    if (IPC_Sound->mp3.cmd & MP3CMD_INIT)
    {
        AS_InitMP3();
        IPC_Sound->mp3.cmd &= ~MP3CMD_INIT;
    }

    if (IPC_Sound->mp3.cmd & MP3CMD_SETRATE)
    {
        IPC_Sound->mp3.cmd &= ~MP3CMD_SETRATE;
        AS_SetTimer(IPC_Sound->mp3.rate);
        SCHANNEL_TIMER(IPC_Sound->mp3.channelL) = 0x10000 - (0x1000000 / IPC_Sound->mp3.rate);
        SCHANNEL_TIMER(IPC_Sound->mp3.channelR) = 0x10000 - (0x1000000 / IPC_Sound->mp3.rate);
    }   

    if (IPC_Sound->mp3.cmd & MP3CMD_PAUSE)
    {
        IPC_Sound->mp3.cmd &= ~MP3CMD_PAUSE;

        // disable mp3 channels
        SCHANNEL_CR(IPC_Sound->mp3.channelL) = 0;
        SCHANNEL_CR(IPC_Sound->mp3.channelR) = 0;
        AS_StopTimer();
        
        // then wait for the restart
        IPC_Sound->mp3.cmd |= MP3CMD_WAITING;
        IPC_Sound->mp3.state = MP3ST_PAUSED;
        
    }

    if (IPC_Sound->mp3.cmd & MP3CMD_STOP)
    {
        IPC_Sound->mp3.cmd &= ~MP3CMD_STOP;
        AS_MP3Stop();
        return;
    }

    if (IPC_Sound->mp3.cmd & MP3CMD_PLAY)
    {
        IPC_Sound->mp3.cmd &= ~MP3CMD_PLAY;

        if(IPC_Sound->mp3.state == MP3ST_PAUSED) {

            // restart on a fresh basis
            IPC_Sound->mp3.prevtimer = 0;
            AS_RegenStreamCallback((s16*)IPC_Sound->mp3.mixbuffer, IPC_Sound->mp3.buffersize >> 1);
            IPC_Sound->mp3.soundcursor = IPC_Sound->mp3.buffersize >> 1;
            AS_StartTimer(IPC_Sound->mp3.rate);
            
            IPC_Sound->mp3.cmd |= MP3CMD_MIX;

        } else {
        
            // set variables
            IPC_Sound->mp3.prevtimer = 0;
            IPC_Sound->mp3.numsamples = 0;
            readPtr = IPC_Sound->mp3.mp3buffer;
            bytesLeft = IPC_Sound->mp3.mp3filesize;
            nAudioBuf = 0;
            nAudioBufStart = 0;
            
            // find the first sync word
            u32 offset = MP3FindSyncWord(readPtr, bytesLeft);
            readPtr += offset;
            bytesLeft -= offset;  
  
            // gather information about the format
            MP3GetNextFrameInfo(hMP3Decoder, &mp3FrameInfo, readPtr);
            stereo = mp3FrameInfo.nChans >> 1;
    
            // fill the half of the buffer
            AS_RegenStreamCallback((s16*)IPC_Sound->mp3.mixbuffer, IPC_Sound->mp3.buffersize >> 1);
            IPC_Sound->mp3.soundcursor = IPC_Sound->mp3.buffersize >> 1;
    
            // set the mp3 to play at its original sampling rate
            MP3GetLastFrameInfo(hMP3Decoder, &mp3FrameInfo);
            IPC_Sound->mp3.rate = mp3FrameInfo.samprate;
            AS_StartTimer(mp3FrameInfo.samprate);
    
            // start playing
            IPC_Sound->mp3.cmd |= MP3CMD_MIX;
        }
        IPC_Sound->mp3.state = MP3ST_PLAYING;
        
    }

    // do the decoding
    if (IPC_Sound->mp3.cmd & MP3CMD_MIXING)
    {
        curtimer = TIMER1_DATA;
        
        if (IPC_Sound->mp3.cmd & MP3CMD_WAITING) {
            IPC_Sound->mp3.cmd &= ~MP3CMD_WAITING;
        } else {
        
            numsamples = curtimer - IPC_Sound->mp3.prevtimer;

            if(numsamples < 0) 
                numsamples += 65536;

            IPC_Sound->mp3.numsamples = numsamples;
        }

        IPC_Sound->mp3.prevtimer = curtimer;
        AS_RegenStream();

        IPC_Sound->mp3.soundcursor += IPC_Sound->mp3.numsamples;
        if (IPC_Sound->mp3.soundcursor > IPC_Sound->mp3.buffersize)
            IPC_Sound->mp3.soundcursor -= IPC_Sound->mp3.buffersize;
    }
    else if(IPC_Sound->mp3.cmd & MP3CMD_MIX)
    {
        // set up the left channel
        IPC_Sound->chan[IPC_Sound->mp3.channelL].snd.data = (u8*)IPC_Sound->mp3.mixbuffer;
        IPC_Sound->chan[IPC_Sound->mp3.channelL].snd.size = IPC_Sound->mp3.buffersize << 1;
        IPC_Sound->chan[IPC_Sound->mp3.channelL].snd.format = AS_PCM_16BIT;
        IPC_Sound->chan[IPC_Sound->mp3.channelL].snd.rate = IPC_Sound->mp3.rate;
        IPC_Sound->chan[IPC_Sound->mp3.channelL].snd.loop = true;
        IPC_Sound->chan[IPC_Sound->mp3.channelL].snd.delay = 0;
        IPC_Sound->chan[IPC_Sound->mp3.channelL].cmd |= SNDCMD_PLAY;

        // set up the right channel
        IPC_Sound->chan[IPC_Sound->mp3.channelR].snd.data = (stereo ? (u8*)IPC_Sound->mp3.mixbuffer + (IPC_Sound->mp3.buffersize << 1) : (u8*)IPC_Sound->mp3.mixbuffer);
        IPC_Sound->chan[IPC_Sound->mp3.channelR].snd.size = IPC_Sound->mp3.buffersize << 1;
        IPC_Sound->chan[IPC_Sound->mp3.channelR].snd.format = AS_PCM_16BIT;
        IPC_Sound->chan[IPC_Sound->mp3.channelR].snd.rate = IPC_Sound->mp3.rate;
        IPC_Sound->chan[IPC_Sound->mp3.channelR].snd.loop = true;
        IPC_Sound->chan[IPC_Sound->mp3.channelR].snd.delay = IPC_Sound->mp3.delay;
        IPC_Sound->chan[IPC_Sound->mp3.channelR].cmd |= SNDCMD_DELAY;

        IPC_Sound->mp3.cmd &= ~MP3CMD_MIX;
        IPC_Sound->mp3.cmd |= MP3CMD_MIXING;
    }
}

void AS_StartTimer(int freq)
{
    if(freq == 0)
        return;

    TIMER1_CR = 0;
    TIMER0_CR = 0;

    TIMER0_DATA = 0x10000 - (0x1000000 / freq) * 2;
    TIMER1_DATA = 0;

    TIMER1_CR = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;
    TIMER0_CR = TIMER_ENABLE | TIMER_DIV_1;
}

// set timer to the given period
void AS_SetTimer(int freq)
{
    if(freq == 0)
        return;

    // Timer 0 is the one that controls the frequency, timer 1 is used as a
    // reference to know how many samples need to be generated. It is
    // important to not reset it to 0 or the decoder loop won't know how
    // much to decode next time.
    TIMER0_DATA = 0x10000 - (0x1000000 / freq) * 2;
    //TIMER1_DATA = 0;
}

void AS_StopTimer()
{
    TIMER1_CR = 0;
    TIMER0_CR = 0;
}

// clear some buffers to avoid clicking on new mp3 start
inline void AS_MP3ClearBuffers(){
    MP3DecInfo *mp3DecInfo = (MP3DecInfo*)hMP3Decoder;
	memset(mp3DecInfo->FrameHeaderPS, 0, sizeof(FrameHeader));
	memset(mp3DecInfo->SideInfoPS, 0, sizeof(SideInfo));
	memset(mp3DecInfo->ScaleFactorInfoPS, 0, sizeof(ScaleFactorInfo));
	memset(mp3DecInfo->HuffmanInfoPS, 0, sizeof(HuffmanInfo));
	memset(mp3DecInfo->DequantInfoPS, 0, sizeof(DequantInfo));
	memset(mp3DecInfo->IMDCTInfoPS, 0, sizeof(IMDCTInfo));
	memset(mp3DecInfo->SubbandInfoPS, 0, sizeof(SubbandInfo));    
	memset(mp3DecInfo->IMDCTInfoPS, 0, sizeof(IMDCTInfo));
	memset(mp3DecInfo->SubbandInfoPS, 0, sizeof(SubbandInfo));
}

// fill the given stream buffer with numsamples samples. (based on ThomasS code)
void AS_RegenStreamCallback(s16 *stream, u32 numsamples)
{
    int outSample, restSample, minSamples, offset, err;
    outSample = 0;
    
    // fill buffered data into stream
    minSamples = MIN(nAudioBuf, (int)numsamples);
    if (minSamples > 0) {
    
        // copy data from audioBuf to stream
        numsamples -= minSamples;

        if(stereo)
            AS_StereoDesinterleave(audioBuf + nAudioBufStart, stream + outSample, stream + outSample + IPC_Sound->mp3.buffersize, minSamples);    
        else
            memcpy(stream + outSample, audioBuf + nAudioBufStart, minSamples * sizeof(s16));

        outSample += minSamples;

        nAudioBufStart += (stereo ? minSamples << 1 : minSamples);
        nAudioBuf -= minSamples;

        if (nAudioBuf <= 0) {
            nAudioBufStart = 0;
            nAudioBuf = 0;
        }
    }

    // if more data is still needed then decode some mp3 frames
    if (numsamples > 0)  {

        // if mp3 is set to loop indefinitely, don't bother with how many data is left
        if((bytesLeft < 2*MAINBUF_SIZE) && IPC_Sound->mp3.loop && IPC_Sound->mp3.stream)
            bytesLeft += IPC_Sound->mp3.mp3filesize;

        // decode a mp3 frame to outBuf
        do {

            // find the start of the next MP3 frame (assume EOF if no sync found)
            offset = MP3FindSyncWord(readPtr, bytesLeft);
            if (offset < 0) {
            
                // if mp3 is set to loop & no frame is found, retry from the start
                if(IPC_Sound->mp3.loop && !IPC_Sound->mp3.stream) {
                    bytesLeft = IPC_Sound->mp3.mp3filesize;
                    readPtr = IPC_Sound->mp3.mp3buffer;
                    offset = MP3FindSyncWord(readPtr, bytesLeft);
                } else {
                    AS_MP3Stop();
                    IPC_Sound->mp3.state = MP3ST_OUT_OF_DATA;
                }
                return;
            }
            readPtr += offset;
            bytesLeft -= offset;
    
            // decode one MP3 frame to the audio buffer
            err = MP3Decode(hMP3Decoder, &readPtr, &bytesLeft, audioBuf, 0);
            if (err) {
                AS_MP3Stop();
                IPC_Sound->mp3.state = MP3ST_DECODE_ERROR;
                return;
            }
    
            // copy the decoded data to the stream
            MP3GetLastFrameInfo(hMP3Decoder, &mp3FrameInfo);
            if(stereo)
                mp3FrameInfo.outputSamps = mp3FrameInfo.outputSamps >> 1;
            
            minSamples = MIN(mp3FrameInfo.outputSamps, (int)numsamples);
            restSample = mp3FrameInfo.outputSamps - numsamples;
            numsamples -= minSamples;

            if(stereo)
                AS_StereoDesinterleave(audioBuf, stream + outSample, stream + outSample + IPC_Sound->mp3.buffersize, minSamples);    
            else
                memcpy(stream + outSample, audioBuf, minSamples * sizeof(s16));

            outSample += minSamples;
            
        // if still more data is needed, then decode the next frame
        } while (numsamples > 0);

        // set the rest of the decoded data to be used for the next frame
        nAudioBufStart = (stereo ? minSamples << 1 : minSamples);
        nAudioBuf = restSample;
    }
    
    // check if we moved onto the 2nd file data buffer, if so move it to the 1st one and request a refill
    if(IPC_Sound->mp3.stream && (readPtr >= IPC_Sound->mp3.mp3buffer + IPC_Sound->mp3.mp3buffersize )) {
        memcpy(IPC_Sound->mp3.mp3buffer, IPC_Sound->mp3.mp3buffer + IPC_Sound->mp3.mp3buffersize, IPC_Sound->mp3.mp3buffersize);
        readPtr = readPtr - IPC_Sound->mp3.mp3buffersize;
        IPC_Sound->mp3.needdata = true;
    }
    
}

// regenerate the sound stream into the ring buffer.
void AS_RegenStream()
{
    int remain;

    // decode data to the ring buffer
    if((IPC_Sound->mp3.soundcursor + IPC_Sound->mp3.numsamples) >= IPC_Sound->mp3.buffersize) {

        AS_RegenStreamCallback((s16*)&IPC_Sound->mp3.mixbuffer[IPC_Sound->mp3.soundcursor << 1], IPC_Sound->mp3.buffersize - IPC_Sound->mp3.soundcursor);
        remain = IPC_Sound->mp3.numsamples - (IPC_Sound->mp3.buffersize - IPC_Sound->mp3.soundcursor);
        AS_RegenStreamCallback((s16*)IPC_Sound->mp3.mixbuffer, remain);

    } else {

        AS_RegenStreamCallback((s16*)&IPC_Sound->mp3.mixbuffer[IPC_Sound->mp3.soundcursor << 1], IPC_Sound->mp3.numsamples);
    }
}

// stop playing the mp3
void AS_MP3Stop()
{
    SCHANNEL_CR(IPC_Sound->mp3.channelL) = 0;
    SCHANNEL_CR(IPC_Sound->mp3.channelR) = 0;
    AS_StopTimer();
    IPC_Sound->mp3.rate = 0;
    IPC_Sound->mp3.cmd = MP3CMD_NONE;
    IPC_Sound->mp3.state = MP3ST_STOPPED;
    AS_MP3ClearBuffers();
}

// initialize the mp3 system
void AS_InitMP3()
{
    // init the timers
    AS_StopTimer();

    // wait for the arm 9 to allocate data on main ram
    while( !(IPC_Sound->mp3.cmd & MP3CMD_ARM9ALLOCDONE) )
        swiWaitForVBlank(); 
        
    // init the helix mp3 decoder
    hMP3Decoder = MP3InitDecoder(IPC_Sound->mp3.alloc_ram);
}

// initialize the main system
void AS_Init()
{
    // clear the sound structure
    memset(IPC_Sound, 0, sizeof(IPC_SoundSystem));
    
    // get the needed allocation size for the mp3 decoder
    IPC_Sound->mp3.alloc_ram = (void*)getAllocationSize();
    
    // tell the arm9 that we are ready
    IPC_Sound->chan[0].cmd = SNDCMD_ARM7READY;
}
