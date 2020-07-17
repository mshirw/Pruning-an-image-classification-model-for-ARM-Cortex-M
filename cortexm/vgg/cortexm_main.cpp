#include <stdint.h>
#include <stdio.h>
#include "arm_math.h"
#include "cortexm_weight.h"
#include "arm_nnfunctions.h"
#include "arm_nnsupportfunctions.h"
#include "cmsis_gcc.h"
#ifdef _RTE_
#include "RTE_Components.h"
#ifdef RTE_Compiler_EventRecorder
#include "EventRecorder.h"
#endif
#endif
#include "cortexm_main.h"
#include "matmul.h"
#include "add.h"
#if defined(ONNC_PC_SIM)
#else
	#include "mbed.h"
#endif

//#define ARM_MATH_DSP

Timer t;

#define CONV1_CHANNEL 8
#define CONV2_CHANNEL 16
#define CONV3_CHANNEL 32
#define CONV4_CHANNEL 32
#define CONV5_CHANNEL 40
#define CONV6_CHANNEL 40
#define FC1_WEIGHT_ALL 160
#define CONV1_BIAS_SHIFTRIGHT 0
#define CONV2_BIAS_SHIFTRIGHT 0
#define FC1_BIAS_SHIFTRIGHT 0

/*================================================*/

static q7_t conv1_wt[CONV1_CHANNEL*3*3*3] = CONV0_WEIGHT;
static q7_t conv1_bias[CONV1_CHANNEL] = CONV0_BIAS;

static q7_t conv2_wt[CONV2_CHANNEL*CONV1_CHANNEL*3*3] = CONV1_WEIGHT;
static q7_t conv2_bias[CONV2_CHANNEL] = CONV1_BIAS;

static q7_t conv3_wt[CONV3_CHANNEL*CONV2_CHANNEL*3*3] = CONV2_WEIGHT;
static q7_t conv3_bias[CONV3_CHANNEL] = CONV2_BIAS;

static q7_t conv4_wt[CONV4_CHANNEL*CONV3_CHANNEL*3*3] = CONV3_WEIGHT;
static q7_t conv4_bias[CONV4_CHANNEL] = CONV3_BIAS;

static q7_t conv5_wt[CONV5_CHANNEL*CONV4_CHANNEL*3*3] = CONV4_WEIGHT;
static q7_t conv5_bias[CONV5_CHANNEL] = CONV4_BIAS;

static q7_t conv6_wt[CONV6_CHANNEL*CONV5_CHANNEL*3*3] = CONV5_WEIGHT;
static q7_t conv6_bias[CONV6_CHANNEL] = CONV5_BIAS;

static q7_t fc0_wt[FC1_WEIGHT_ALL * 10] = FC0_WEIGHT;
static q7_t fc0_bias[10] = FC0_BIAS;


q7_t output_data[10];
q7_t col_buffer[2*5*5*32*2];
//q7_t col_buffer[6400];
//q7_t col_buffer[256];
q15_t bufferA_conv1[2*3*3*3];
q15_t bufferA_conv2[2*CONV1_CHANNEL*3*3];
q15_t bufferA_conv3[2*CONV2_CHANNEL*3*3];
q15_t bufferA_conv4[2*CONV3_CHANNEL*3*3];
q15_t bufferA_conv5[2*CONV4_CHANNEL*3*3];
q15_t bufferA_conv6[2*CONV5_CHANNEL*3*3];

q7_t scratch_buffer[3*8*32*32];
q7_t scratch_buffer2[3*8*32*32];
q15_t fc_buffer[FC1_WEIGHT_ALL];

bool bias_shiftright = false;
//Serial port2(USBTX, USBRX);

void fc_test(const q7_t* pV,
             const q7_t* pM,
             const uint16_t dim_vec,
             const uint16_t num_of_rows,
             const uint16_t bias_shift,
             const uint16_t out_shift, const q7_t* bias, q7_t* pOut, q15_t* vec_buffer)
{
    int       i, j;

    /* Run the following code as reference implementation for Cortex-M0 and Cortex-M3 */
    for (i = 0; i < num_of_rows; i++)
    {
        int       ip_out = ((q31_t)(bias[i]) << bias_shift) + NN_ROUND(out_shift);
        for (j = 0; j < dim_vec; j++)
        {
            ip_out += pV[j] * pM[i * dim_vec + j];
        }
        pOut[i] = (q7_t) __SSAT((ip_out >> out_shift), 8);
    }

}

void avepool_q7_HWC(q7_t * Im_in,
                   const uint16_t dim_im_in,
                   const uint16_t ch_im_in,
                   const uint16_t dim_kernel,
                   const uint16_t padding,
                   const uint16_t stride, const uint16_t dim_im_out, q7_t * bufferA, q7_t * Im_out)
{
    int16_t   i_ch_in, i_x, i_y;
    int16_t   k_x, k_y;

    for (i_ch_in = 0; i_ch_in < ch_im_in; i_ch_in++)
    {
        for (i_y = 0; i_y < dim_im_out; i_y++)
        {
            for (i_x = 0; i_x < dim_im_out; i_x++)
            {
                int       sum = 0;
                int       count = 0;
                for (k_y = i_y * stride - padding; k_y < i_y * stride - padding + dim_kernel; k_y++)
                {
                    for (k_x = i_x * stride - padding; k_x < i_x * stride - padding + dim_kernel; k_x++)
                    {
                        if (k_y >= 0 && k_x >= 0 && k_y < dim_im_in && k_x < dim_im_in)
                        {
                            sum += Im_in[i_ch_in + ch_im_in * (k_x + k_y * dim_im_in)];
                            count++;
                        }
                    }
                }
                Im_out[i_ch_in + ch_im_in * (i_x + i_y * dim_im_out)] = sum / count;
            }
        }
    }

}

/*void bias_shift(){

  for(int i = 0; i < CONV1_CHANNEL; i++){
    conv1_bias[i] = conv1_bias[i] >> CONV1_BIAS_SHIFTRIGHT;
  }

  for(int i = 0; i < CONV2_CHANNEL; i++){
    conv2_bias[i] = conv2_bias[i] >> CONV2_BIAS_SHIFTRIGHT;
  }

  for(int i = 0; i < 10; i++){
    fully_connected_bias[i] = fully_connected_bias[i] >> FC1_BIAS_SHIFTRIGHT;
  }
}*/

q7_t* cortexm_main(int *image_data){

  #ifdef RTE_Compiler_EventRecorder
    EventRecorderInitialize (EventRecordAll, 1);
  #endif

  q7_t *img_buffer1 = scratch_buffer;
  q7_t *img_buffer2 = scratch_buffer2;

  for(int loop = 0 ; loop<3072 ; loop++ ){
      img_buffer2[loop] = image_data[loop];
  }

  /*if (bias_shiftright == false){
    bias_shift();
    bias_shiftright = true;
  }*/
  
  t.start();

  arm_convolve_HWC_q7_basic(img_buffer2, 32, 3, conv1_wt, CONV1_CHANNEL, 3, 1, 1, conv1_bias, 0, CONV0_OUT_SHIFT, img_buffer1, 32, bufferA_conv1, NULL);
  arm_relu_q7(img_buffer1, 1 * CONV1_CHANNEL * 32 * 32);
  arm_maxpool_q7_HWC(img_buffer1, 32, CONV1_CHANNEL, 2, 0, 2, 16, NULL, img_buffer2);

  arm_convolve_HWC_q7_basic(img_buffer2, 16, CONV1_CHANNEL, conv2_wt, CONV2_CHANNEL, 3, 1, 1, conv2_bias, 0, CONV1_OUT_SHIFT, img_buffer1, 16, bufferA_conv2, NULL);
  arm_relu_q7(img_buffer1, 1 * CONV2_CHANNEL * 16 * 16);
  arm_maxpool_q7_HWC(img_buffer1, 16, CONV2_CHANNEL, 2, 0, 2, 8, NULL, img_buffer2);

  arm_convolve_HWC_q7_basic(img_buffer2, 8, CONV2_CHANNEL, conv3_wt, CONV3_CHANNEL, 3, 1, 1, conv3_bias, 0, CONV2_OUT_SHIFT, img_buffer1, 8, bufferA_conv3, NULL);
  arm_relu_q7(img_buffer1, 1 * CONV3_CHANNEL * 8 * 8);


  arm_convolve_HWC_q7_basic(img_buffer1, 8, CONV3_CHANNEL, conv4_wt, CONV4_CHANNEL, 3, 1, 1, conv4_bias, 0, CONV3_OUT_SHIFT, img_buffer2, 8, bufferA_conv4, NULL);
  arm_relu_q7(img_buffer2, 1 * CONV4_CHANNEL * 8 * 8);
  arm_maxpool_q7_HWC(img_buffer2, 8, CONV4_CHANNEL, 2, 0, 2, 4, NULL, img_buffer1);

  arm_convolve_HWC_q7_basic(img_buffer1, 4, CONV4_CHANNEL, conv5_wt, CONV5_CHANNEL, 3, 1, 1, conv5_bias, 0, CONV4_OUT_SHIFT, img_buffer2, 4, bufferA_conv5, NULL);
  arm_relu_q7(img_buffer2, 1 * CONV5_CHANNEL * 4 * 4);

  arm_convolve_HWC_q7_basic(img_buffer2, 4, CONV5_CHANNEL, conv6_wt, CONV6_CHANNEL, 3, 1, 1, conv6_bias, 0, CONV5_OUT_SHIFT, img_buffer1, 4, bufferA_conv6, NULL);
  arm_relu_q7(img_buffer1, 1 * CONV6_CHANNEL * 4 * 4);

  avepool_q7_HWC(img_buffer1, 4, CONV6_CHANNEL, 2, 0, 2, 2, NULL, img_buffer2);

  fc_test(img_buffer2, fc0_wt, FC1_WEIGHT_ALL, 10, 0, FC0_OUT_SHIFT, fc0_bias, img_buffer1, fc_buffer);

  t.stop();
  printf("This inference taken was %d ms.\n", t.read_ms());
  t.reset();


  return img_buffer1;
}
