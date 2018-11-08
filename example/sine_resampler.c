/*
 * Copyright (C) 2014 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */

/*
 * Sine Playback to test and validate audio output with simple wave form
 *
 */

#include "btstack.h"
#include "btstack_resample.h"

#ifdef HAVE_POSIX_FILE_IO
#include "wav_util.h"
#endif

#define TABLE_SIZE_441HZ            100

#define STRINGIZE2(s) #s
#define STRINGIZE(s) STRINGIZE2(s)

static const int16_t sine_int16[] = {
     0,    2057,    4107,    6140,    8149,   10126,   12062,   13952,   15786,   17557,
 19260,   20886,   22431,   23886,   25247,   26509,   27666,   28714,   29648,   30466,
 31163,   31738,   32187,   32509,   32702,   32767,   32702,   32509,   32187,   31738,
 31163,   30466,   29648,   28714,   27666,   26509,   25247,   23886,   22431,   20886,
 19260,   17557,   15786,   13952,   12062,   10126,    8149,    6140,    4107,    2057,
     0,   -2057,   -4107,   -6140,   -8149,  -10126,  -12062,  -13952,  -15786,  -17557,
-19260,  -20886,  -22431,  -23886,  -25247,  -26509,  -27666,  -28714,  -29648,  -30466,
-31163,  -31738,  -32187,  -32509,  -32702,  -32767,  -32702,  -32509,  -32187,  -31738,
-31163,  -30466,  -29648,  -28714,  -27666,  -26509,  -25247,  -23886,  -22431,  -20886,
-19260,  -17557,  -15786,  -13952,  -12062,  -10126,   -8149,   -6140,   -4107,   -2057,
};

#define NUM_CHANNELS 2

int btstack_main(int argc, const char * argv[]);
int btstack_main(int argc, const char * argv[]){
    (void)argc;
    (void)argv;

#ifdef HAVE_POSIX_FILE_IO
    wav_writer_open("sine_resampled-" STRINGIZE(NUM_CHANNELS) ".wav", NUM_CHANNELS, 44100);
#endif
    btstack_resample_t resample;
    btstack_resample_init(&resample, NUM_CHANNELS);
    btstack_resample_set_factor(&resample, 0xff00);

    int16_t input_buffer[TABLE_SIZE_441HZ * NUM_CHANNELS];
    int16_t output_buffer[200*NUM_CHANNELS];   // double the input size

    // generate multi channel sine
    int i, j;
    int src_pos = 0;
    for (i=0;i<TABLE_SIZE_441HZ;i++){
        for (j=0;j<NUM_CHANNELS;j++){
            input_buffer[src_pos++] = sine_int16[i];
        }
    }
    
    for (i=0;i<440*5;i++){
        uint16_t resampled_samples = btstack_resample_block(&resample, input_buffer, TABLE_SIZE_441HZ, output_buffer);
#ifdef HAVE_POSIX_FILE_IO
        wav_writer_write_int16(resampled_samples * NUM_CHANNELS, output_buffer);
#endif
    }

#ifdef HAVE_POSIX_FILE_IO                  
    wav_writer_close();
#endif

    printf("Done\n");

    return 0;
}
