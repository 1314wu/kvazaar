/**
 *  Part of HEVC Encoder
 *  By Marko Viitanen ( fador at iki.fi ), Tampere University of Technology, Department of Pervasive Computing.
 */

/*! \file picture.c
    \brief Functions to handle pictures
    \author Marko Viitanen
    \date 2013-06
    
  This file contains all the needed functions to handle pictures

*/

#include "picture.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>


/** \defgroup picture_group Picture handler group
 *  This group contains all picture related stuff
 *  @{
 */



/*!
 \brief Set block splitflag
 \param pic picture to use
 \param xCtb x CU position (smallest CU)
 \param yCtb y CU position (smallest CU)
 \param depth current CU depth
 \param mode mode to set
 \returns Void
*/
void picture_setBlockSplit(picture* pic,uint32_t xCtb, uint32_t yCtb, uint8_t depth, int8_t split)
{
  uint32_t x,y;//,d;
  //Width in smallest CU
  int width_in_SCU = pic->width_in_lcu<<MAX_DEPTH;
  int block_SCU_width = (LCU_WIDTH>>depth)/(LCU_WIDTH>>MAX_DEPTH);
  for(y = yCtb; y < yCtb+block_SCU_width; y++)
  {
    int CUpos = y*width_in_SCU;
    for(x = xCtb; x < xCtb+block_SCU_width; x++)
    {
      pic->CU[depth][CUpos+x].split = split;
    }
  }
}

/*!
 \brief Set block coded status
 \param pic picture to use
 \param xCtb x CU position (smallest CU)
 \param yCtb y CU position (smallest CU)
 \param depth current CU depth
 \param coded coded status
 \returns Void
*/
void picture_setBlockCoded(picture* pic,uint32_t xCtb, uint32_t yCtb, uint8_t depth, int8_t coded)
{
  uint32_t x,y,d;
  //Width in smallest CU
  int width_in_SCU = pic->width_in_lcu<<MAX_DEPTH;
  int block_SCU_width = (LCU_WIDTH>>depth)/(LCU_WIDTH>>MAX_DEPTH);
  for(y = yCtb; y < yCtb+block_SCU_width; y++)
  {
    int CUpos = y*width_in_SCU;
    for(x = xCtb; x < xCtb+block_SCU_width; x++)
    {
      for(d = 0; d < MAX_DEPTH+1; d++)
      {
        pic->CU[d][CUpos+x].coded = coded;
      }
    }
  }
}



/*!
    \brief Allocate memory for picture_list
    \param size initial array size
    \return picture_list pointer, NULL on failure
*/
  picture_list *picture_list_init(int size)
  {
    picture_list *list = (picture_list *)malloc(sizeof(picture_list));
    list->size = size;
    if(size > 0)
    {
      list->pics = (picture**)malloc(sizeof(picture*)*size);
    }

    list->used_size = 0;

    return list;
  }

  /*!
    \brief Resize picture_list array
    \param list picture_list pointer
    \param size new array size
    \return 1 on success, 0 on failure
  */
  int picture_list_resize(picture_list *list, int size)
  {
    unsigned int i;
    picture** old_pics = NULL;

    //No need to do anything when resizing to same size
    if(size == list->size)
    {
      return 1;
    }

    //Save the old list
    if(list->used_size > 0)
    {
      old_pics = list->pics;
    }

    //allocate space for the new list
    list->pics = (picture**)malloc(sizeof(picture*)*size);

    //Copy everything from the old list to the new if needed.
    if(old_pics != NULL)
    {
      for(i = 0; i < list->used_size; i++)
      {
        list->pics[i] = old_pics[i];
      }

      free(old_pics);
    }

    return 1;
  }

  /*!
    \brief Free memory allocated to the picture_list
    \param list picture_list pointer
    \return 1 on success, 0 on failure
  */
  int picture_list_destroy(picture_list *list)
  {
    unsigned int i;
    if(list->used_size > 0)
    {
      for(i = 0; i < list->used_size; i++)
      {
        picture_destroy(list->pics[i]);
      }
    }
    
    if(list->size > 0)
    {
      free(list->pics);
    }
    free(list);
    return 1;
  }

  /*!
    \brief Add picture to picturelist
    \param pic picture pointer to add
    \param picture_list list to use
    \return 1 on success
  */
  int picture_list_add(picture_list *list,picture* pic)
  {
    if(list->size == list->used_size)
    {
      if(!picture_list_resize(list, list->size*2))
      {
        return 0;
      }
    }

    list->pics[list->used_size] = pic;
    list->used_size++;
    return 1;
  }

  /*!
    \brief Add picture to picturelist
    \param pic picture pointer to add
    \param picture_list list to use
    \return 1 on success
  */
  int picture_list_rem(picture_list *list,int n, int8_t destroy)
  {
    int i;
    //Must be within list boundaries
    if((unsigned int)n >= list->used_size)
    {
      return 0;
    }

    if(destroy)
    {
      picture_destroy(list->pics[n]);
      free(list->pics[n]);
    }

    //The last item is easy to remove
    if(n == list->used_size-1)
    {
      list->pics[n] = NULL;
      list->used_size--;
    }
    else
    {
      //Shift all following pics one backward in the list
      for(i = n; (unsigned int)n < list->used_size-1; n++)
      {
        list->pics[n] = list->pics[n+1];
      }
      list->pics[list->used_size-1] = NULL;
      list->used_size--;
    }

    return 1;
  }
  
  /*!
    \brief Allocate new picture
    \param pic picture pointer
    \return picture pointer
  */
  picture *picture_init(int32_t width, int32_t height, int32_t width_in_LCU, int32_t height_in_LCU)
  {
    picture *pic = (picture *)malloc(sizeof(picture));    
    unsigned int luma_size = width * height;
    unsigned int chroma_size = luma_size / 4;
    int i = 0;

    if(!pic)
    {
      return 0;
    }

    memset(pic, 0, sizeof(picture));

    pic->width  = width;
    pic->height = height;
    pic->width_in_lcu  = width_in_LCU;
    pic->height_in_lcu = height_in_LCU;
    pic->referenced = 0;
    /* Allocate buffers */
    pic->y_data = (uint8_t *)malloc(luma_size);
    pic->u_data = (uint8_t *)malloc(chroma_size);
    pic->v_data = (uint8_t *)malloc(chroma_size);

    /* Reconstruction buffers */
    pic->y_recdata = (uint8_t *)malloc(luma_size);
    pic->u_recdata = (uint8_t *)malloc(chroma_size);
    pic->v_recdata = (uint8_t *)malloc(chroma_size);

    memset(pic->u_recdata, 128, (chroma_size));
    memset(pic->v_recdata, 128, (chroma_size));

    /* Allocate memory for CU info 2D array */
    //TODO: we don't need this much space on LCU...MAX_DEPTH-1
    pic->CU = (CU_info**)malloc((MAX_DEPTH+1)*sizeof(CU_info*));
    for(i=0; i<MAX_DEPTH+1; i++)
    {
      /* Allocate height_in_SCU x width_in_SCU x sizeof(CU_info) */
      pic->CU[i] = (CU_info*)malloc((height_in_LCU<<MAX_DEPTH)*(width_in_LCU<<MAX_DEPTH)*sizeof(CU_info));
      memset(pic->CU[i], 0, (height_in_LCU<<MAX_DEPTH)*(width_in_LCU<<MAX_DEPTH)*sizeof(CU_info));
    }

    return pic;
  }

  /*!
    \brief Free memory allocated to picture
    \param pic picture pointer
    \return 1 on success, 0 on failure
  */
  int picture_destroy(picture *pic)
  {
    int i;
        
    free(pic->u_data);
    free(pic->v_data);
    free(pic->y_data);
    pic->y_data = pic->u_data = pic->v_data = NULL;

    free(pic->y_recdata);
    free(pic->u_recdata);
    free(pic->v_recdata);
    pic->y_recdata = pic->u_recdata = pic->v_recdata = NULL;

    for(i=0; i<MAX_DEPTH+1; i++)
    {
      free(pic->CU[i]);
      pic->CU[i] = NULL;
    }

    free(pic->CU);
    pic->CU = NULL;

    return 1;
  }


  /** @} */ // end of group1


#include <math.h>
#define PSNRMAX (255.0*255.0)

//Calculates image PSNR value
double image_psnr(uint8_t *frame1, uint8_t *frame2, int32_t x, int32_t y)
{   
  uint64_t MSE=0;
  int32_t MSEtemp=0;
  double psnr=0.0;
  int32_t pixels = x*y;
  int32_t index;

  //Calculate MSE
  for(index = 0; index < x*y; index++)
  {
    MSEtemp=frame1[index]-frame2[index];
    MSE+=MSEtemp*MSEtemp;
  }

  //Avoid division by zero
  if(MSE==0) return 99.0;

  //The PSNR
  psnr=10*log10((pixels*PSNRMAX)/((double)MSE));

  //Thats it.
  return psnr;
}

uint32_t Hadamard8x8(int16_t *piOrg, int32_t iStrideOrg, int16_t *piCur, int32_t iStrideCur)
{
  int32_t k, i, j, jj, sad=0;
  int32_t diff[64], m1[8][8], m2[8][8], m3[8][8];
  for( k = 0; k < 64; k += 8 )
  {
    diff[k+0] = piOrg[0] - piCur[0];
    diff[k+1] = piOrg[1] - piCur[1];
    diff[k+2] = piOrg[2] - piCur[2];
    diff[k+3] = piOrg[3] - piCur[3];
    diff[k+4] = piOrg[4] - piCur[4];
    diff[k+5] = piOrg[5] - piCur[5];
    diff[k+6] = piOrg[6] - piCur[6];
    diff[k+7] = piOrg[7] - piCur[7];
    
    piCur += iStrideCur;
    piOrg += iStrideOrg;
  }
  
  //horizontal
  for (j=0; j < 8; j++)
  {
    jj = j << 3;
    m2[j][0] = diff[jj  ] + diff[jj+4];
    m2[j][1] = diff[jj+1] + diff[jj+5];
    m2[j][2] = diff[jj+2] + diff[jj+6];
    m2[j][3] = diff[jj+3] + diff[jj+7];
    m2[j][4] = diff[jj  ] - diff[jj+4];
    m2[j][5] = diff[jj+1] - diff[jj+5];
    m2[j][6] = diff[jj+2] - diff[jj+6];
    m2[j][7] = diff[jj+3] - diff[jj+7];
    
    m1[j][0] = m2[j][0] + m2[j][2];
    m1[j][1] = m2[j][1] + m2[j][3];
    m1[j][2] = m2[j][0] - m2[j][2];
    m1[j][3] = m2[j][1] - m2[j][3];
    m1[j][4] = m2[j][4] + m2[j][6];
    m1[j][5] = m2[j][5] + m2[j][7];
    m1[j][6] = m2[j][4] - m2[j][6];
    m1[j][7] = m2[j][5] - m2[j][7];
    
    m2[j][0] = m1[j][0] + m1[j][1];
    m2[j][1] = m1[j][0] - m1[j][1];
    m2[j][2] = m1[j][2] + m1[j][3];
    m2[j][3] = m1[j][2] - m1[j][3];
    m2[j][4] = m1[j][4] + m1[j][5];
    m2[j][5] = m1[j][4] - m1[j][5];
    m2[j][6] = m1[j][6] + m1[j][7];
    m2[j][7] = m1[j][6] - m1[j][7];
  }
  
  //vertical
  for (i=0; i < 8; i++)
  {
    m3[0][i] = m2[0][i] + m2[4][i];
    m3[1][i] = m2[1][i] + m2[5][i];
    m3[2][i] = m2[2][i] + m2[6][i];
    m3[3][i] = m2[3][i] + m2[7][i];
    m3[4][i] = m2[0][i] - m2[4][i];
    m3[5][i] = m2[1][i] - m2[5][i];
    m3[6][i] = m2[2][i] - m2[6][i];
    m3[7][i] = m2[3][i] - m2[7][i];
    
    m1[0][i] = m3[0][i] + m3[2][i];
    m1[1][i] = m3[1][i] + m3[3][i];
    m1[2][i] = m3[0][i] - m3[2][i];
    m1[3][i] = m3[1][i] - m3[3][i];
    m1[4][i] = m3[4][i] + m3[6][i];
    m1[5][i] = m3[5][i] + m3[7][i];
    m1[6][i] = m3[4][i] - m3[6][i];
    m1[7][i] = m3[5][i] - m3[7][i];
    
    m2[0][i] = m1[0][i] + m1[1][i];
    m2[1][i] = m1[0][i] - m1[1][i];
    m2[2][i] = m1[2][i] + m1[3][i];
    m2[3][i] = m1[2][i] - m1[3][i];
    m2[4][i] = m1[4][i] + m1[5][i];
    m2[5][i] = m1[4][i] - m1[5][i];
    m2[6][i] = m1[6][i] + m1[7][i];
    m2[7][i] = m1[6][i] - m1[7][i];
  }
  
  for (i = 0; i < 8; i++)
  {
    for (j = 0; j < 8; j++)
    {
      sad += abs(m2[i][j]);
    }
  }
  
  sad=((sad+2)>>2);
  
  return sad;
}

uint32_t sad64x64(int16_t *block,uint32_t stride1,int16_t* block2, uint32_t stride2)
{
  int32_t y,x;
  uint32_t sum=0;
  /*

  for(y=0;y<64;y++)
  {
    i = y*stride1; 
    ii = y*stride2;
    for(x = 0; x < 64;x++)
    {
      sum+=abs((int16_t)block[i+x]-(int16_t)block2[ii+x]);
    }

  }*/
  int32_t  iOffsetOrg = stride1<<3;
  int32_t  iOffsetCur = stride2<<3;
  for ( y=0; y<64; y+= 8 )
  {
    for ( x=0; x<64; x+= 8 )
    {
      sum += Hadamard8x8( &block[x], stride1,&block2[x],  stride2 );
    }
    block += iOffsetOrg;
    block2 += iOffsetCur;
  }

  return sum;    
}

uint32_t sad32x32(int16_t *block,uint32_t stride1,int16_t* block2, uint32_t stride2)
{
  int32_t y;
  
  int32_t x,sum = 0;
  int32_t  iOffsetOrg = stride1<<3;
  int32_t  iOffsetCur = stride2<<3;
  for ( y=0; y<32; y+= 8 )
  {
    for ( x=0; x<32; x+= 8 )
    {
      sum += Hadamard8x8( &block[x], stride1,&block2[x],  stride2 );
    }
    block += iOffsetOrg;
    block2 += iOffsetCur;
  }

  /*
  uint32_t sum=0;
  int32_t i,ii;
  for(y=0;y<32;y++)
  {
    i = y*stride1; 
    ii = y*stride2;
    sum+=abs((int32_t)block[i]-(int32_t)block2[ii]);
    sum+=abs((int32_t)block[i+1]-(int32_t)block2[ii+1]);
    sum+=abs((int32_t)block[i+2]-(int32_t)block2[ii+2]);
    sum+=abs((int32_t)block[i+3]-(int32_t)block2[ii+3]);
    sum+=abs((int32_t)block[i+4]-(int32_t)block2[ii+4]);
    sum+=abs((int32_t)block[i+5]-(int32_t)block2[ii+5]);
    sum+=abs((int32_t)block[i+6]-(int32_t)block2[ii+6]);
    sum+=abs((int32_t)block[i+7]-(int32_t)block2[ii+7]);
    sum+=abs((int32_t)block[i+8]-(int32_t)block2[ii+8]);
    sum+=abs((int32_t)block[i+9]-(int32_t)block2[ii+9]);
    sum+=abs((int32_t)block[i+10]-(int32_t)block2[ii+10]);
    sum+=abs((int32_t)block[i+11]-(int32_t)block2[ii+11]);
    sum+=abs((int32_t)block[i+12]-(int32_t)block2[ii+12]);
    sum+=abs((int32_t)block[i+13]-(int32_t)block2[ii+13]);
    sum+=abs((int32_t)block[i+14]-(int32_t)block2[ii+14]);
    sum+=abs((int32_t)block[i+15]-(int32_t)block2[ii+15]);
    sum+=abs((int32_t)block[i+16]-(int32_t)block2[ii+16]);
    sum+=abs((int32_t)block[i+17]-(int32_t)block2[ii+17]);
    sum+=abs((int32_t)block[i+18]-(int32_t)block2[ii+18]);
    sum+=abs((int32_t)block[i+19]-(int32_t)block2[ii+19]);
    sum+=abs((int32_t)block[i+20]-(int32_t)block2[ii+20]);
    sum+=abs((int32_t)block[i+21]-(int32_t)block2[ii+21]);
    sum+=abs((int32_t)block[i+22]-(int32_t)block2[ii+22]);
    sum+=abs((int32_t)block[i+23]-(int32_t)block2[ii+23]);
    sum+=abs((int32_t)block[i+24]-(int32_t)block2[ii+24]);
    sum+=abs((int32_t)block[i+25]-(int32_t)block2[ii+25]);
    sum+=abs((int32_t)block[i+26]-(int32_t)block2[ii+26]);
    sum+=abs((int32_t)block[i+27]-(int32_t)block2[ii+27]);
    sum+=abs((int32_t)block[i+28]-(int32_t)block2[ii+28]);
    sum+=abs((int32_t)block[i+29]-(int32_t)block2[ii+29]);
    sum+=abs((int32_t)block[i+30]-(int32_t)block2[ii+30]);
    sum+=abs((int32_t)block[i+31]-(int32_t)block2[ii+31]);
  }
  */
  return sum;    
}


uint32_t sad16x16(int16_t *block,uint32_t stride1,int16_t* block2, uint32_t stride2)
{
  int32_t y;
    
  int32_t x,sum = 0;
  int32_t  iOffsetOrg = stride1<<3;
  int32_t  iOffsetCur = stride2<<3;
  for ( y=0; y<16; y+= 8 )
  {
    for ( x=0; x<16; x+= 8 )
    {
      sum += Hadamard8x8( &block[x], stride1,&block2[x],  stride2 );
    }
    block += iOffsetOrg;
    block2 += iOffsetCur;
  }
  
  
  /*
  uint32_t sum=0;
  int32_t i,ii;
  for(y=0;y<16;y++)
  {
    i = y*stride1; 
    ii = y*stride2;
    sum+=abs((int32_t)block[i]-(int32_t)block2[ii]);
    sum+=abs((int32_t)block[i+1]-(int32_t)block2[ii+1]);
    sum+=abs((int32_t)block[i+2]-(int32_t)block2[ii+2]);
    sum+=abs((int32_t)block[i+3]-(int32_t)block2[ii+3]);
    sum+=abs((int32_t)block[i+4]-(int32_t)block2[ii+4]);
    sum+=abs((int32_t)block[i+5]-(int32_t)block2[ii+5]);
    sum+=abs((int32_t)block[i+6]-(int32_t)block2[ii+6]);
    sum+=abs((int32_t)block[i+7]-(int32_t)block2[ii+7]);
    sum+=abs((int32_t)block[i+8]-(int32_t)block2[ii+8]);
    sum+=abs((int32_t)block[i+9]-(int32_t)block2[ii+9]);
    sum+=abs((int32_t)block[i+10]-(int32_t)block2[ii+10]);
    sum+=abs((int32_t)block[i+11]-(int32_t)block2[ii+11]);
    sum+=abs((int32_t)block[i+12]-(int32_t)block2[ii+12]);
    sum+=abs((int32_t)block[i+13]-(int32_t)block2[ii+13]);
    sum+=abs((int32_t)block[i+14]-(int32_t)block2[ii+14]);
    sum+=abs((int32_t)block[i+15]-(int32_t)block2[ii+15]);
  }  
  */
  return sum;    
}


uint32_t sad8x8(int16_t *block,uint32_t stride1,int16_t* block2, uint32_t stride2)
{
  uint32_t sum=0;
  sum = Hadamard8x8( block, stride1,block2,  stride2 );
  /*
  
  for(y=0;y<8;y++)
  {
    i = y*stride1; 
    ii = y*stride2;
    sum+=abs((int32_t)block[i]-(int32_t)block2[ii]);
    sum+=abs((int32_t)block[i+1]-(int32_t)block2[ii+1]);
    sum+=abs((int32_t)block[i+2]-(int32_t)block2[ii+2]);
    sum+=abs((int32_t)block[i+3]-(int32_t)block2[ii+3]);
    sum+=abs((int32_t)block[i+4]-(int32_t)block2[ii+4]);
    sum+=abs((int32_t)block[i+5]-(int32_t)block2[ii+5]);
    sum+=abs((int32_t)block[i+6]-(int32_t)block2[ii+6]);
    sum+=abs((int32_t)block[i+7]-(int32_t)block2[ii+7]);
  }
  */

  return sum;    
}

uint32_t sad4x4(int16_t *block,uint32_t stride1,int16_t* block2, uint32_t stride2)
{
  int32_t i,ii,y;
  uint32_t sum=0;
  for(y=0;y<4;y++)
  {
    i = y*stride1; 
    ii = y*stride2;
    sum+=abs((int32_t)block[i]-(int32_t)block2[ii]);
    sum+=abs((int32_t)block[i+1]-(int32_t)block2[ii+1]);
    sum+=abs((int32_t)block[i+2]-(int32_t)block2[ii+2]);
    sum+=abs((int32_t)block[i+3]-(int32_t)block2[ii+3]);
  }

  return sum;
}

/**
 * Calculate Sum of Absolute Differences (SAD) between two rectangular regions located in arbitrary points in the picture.
 * 
 * data1 is the starting point of the first picture.
 * data2 is the starting point of the second picture.
 * width is the width of the region for which SAD is calculated.
 * height is the height of the region for which SAD is calculated.
 * stride is the width of the pixel array.
 */
uint32_t sad(uint8_t *data1, uint8_t *data2, unsigned width, unsigned height, unsigned stride)
{
  unsigned y, x;
  unsigned sad = 0;

  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
      sad += abs((int)data1[y * stride + x] - (int)data2[y * stride + x]);
    }
  }

  return sad;
}
