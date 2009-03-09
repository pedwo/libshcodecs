/*
  Copyright (C) Renesas Technology Corp., 2003-2005. All rights reserved.
 */

#ifndef __SHCODECS_ENCODER_H__
#define __SHCODECS_ENCODER_H__

#include <shcodecs/shcodecs_common.h>

/** \file
 *
 * Encoder functions.
 */

/**
 * An opaque handle to the VPU4 for encoding.
 */
typedef void SHCodecs_Encoder;

/**
 * Signature of a callback for libshcodecs to call when it requires YUV 4:2:0 data.
 * To pause encoding, return 1 from this callback.
 * \param encoder The SHCodecs_Encoder* handle
 * \param user_data Arbitrary data supplied by user
 * \retval 0 Continue encoding
 * \retval 1 Pause encoding, return from shcodecs_encode()
 */
typedef int (*SHCodecs_Encoder_Input) (SHCodecs_Encoder * encoder, void * user_data);

/**
 * Signature of a callback for libshcodecs to call when it has encoded data.
 * To pause encoding, return 1 from this callback.
 * \param encoder The SHCodecs_Encoder* handle
 * \param data The encoded data
 * \param length Length of encoded data in bytes
 * \param user_data Arbitrary data supplied by user
 * \retval 0 Continue encoding
 * \retval 1 Pause encoding, return from shcodecs_encode()
 */
typedef int (*SHCodecs_Encoder_Output) (SHCodecs_Encoder * encoder,
                                        unsigned char * data, int length,
                                        void * user_data);

/**
 * Initialize the VPU4 for encoding a given video format.
 * \param width The video image width
 * \param height The video image height
 * \param format SHCodecs_Format_MPEG4 or SHCODECS_Format_H264
 * \return encoder The SHCodecs_Encoder* handle
 */
SHCodecs_Encoder *
shcodecs_encoder_init(int width, int height, SHCodecs_Format format);

/**
 * Deallocate resources used to initialize the VPU4 for encoding,
 * and reset it for future use.
 * \param encoder The SHCodecs_Encoder* handle
 */
void
shcodecs_encoder_close (SHCodecs_Encoder * encoder);

/**
 * Set the callback for libshcodecs to call when encoded data is available.
 * \param encoder The SHCodecs_Encoder* handle
 * \param encodec_cb The callback function
 * \param user_data Additional data to pass to the callback function
 */
int
shcodecs_encoder_set_output_callback (SHCodecs_Encoder * encoder,
                                      SHCodecs_Encoder_Output output_cb,
                                      void * user_data);

/**
 * Set the callback for libshcodecs to call when raw YUV data is required.
 * \param encoder The SHCodecs_Encoder* handle
 * \param get_input_cb The callback function
 * \param user_data Additional data to pass to the callback function
 */
int
shcodecs_encoder_set_input_callback (SHCodecs_Encoder * encoder,
                                     SHCodecs_Encoder_Input input_cb,
                                     void * user_data);

/**
 * Provide input data to the encoder.
 * This function MUST be called from within an SHCodecs_Encoder_Input callback,
 * otherwise the encode hardware will not see any data.
 * The byte sizes of these planes do not vary unless the image dimensions vary,
 * and can be retrieved with shcodecs_encoder_get_y_bytes() and
 * shcodecs_encoder_get_c_bytes().
 * \param encoder The SHCodecs_Encoder* handle
 * \param y_input Pointer to the Y plane of input data
 * \param c_input Pointer to the CbCr plane of input data
 * \retval 0 Success
 */
int
shcodecs_encoder_input_provide (SHCodecs_Encoder * encoder, 
                                unsigned char * y_input, unsigned char * c_input);


/**
 * Get the size in bytes of a Y plane of input data.
 * \param encoder The SHCodecs_Encoder* handle
 * \retval size in bytes of Y plane.
 */
int
shcodecs_encoder_get_y_bytes (SHCodecs_Encoder * encoder);

/**
 * Get the size in bytes of a CbCr plane of input data.
 * \param encoder The SHCodecs_Encoder* handle
 * \retval size in bytes of CbCr plane.
 */
int
shcodecs_encoder_get_c_bytes (SHCodecs_Encoder * encoder);

/**
 * Run the encoder.
 * \param encoder The SHCodecs_Encoder* handle
 * \retval 0 Success
 */
int
shcodecs_encoder_run (SHCodecs_Encoder * encoder);

/*
 * \param encoder The SHCodecs_Encoder* handle
 * \retval -1 \a encoder invalid
 */
int shcodecs_encoder_get_width (SHCodecs_Encoder * encoder);

/*
 * \param encoder The SHCodecs_Encoder* handle
 * \retval -1 \a encoder invalid
 */
int shcodecs_encoder_get_height (SHCodecs_Encoder * encoder);

/*
 * \param encoder The SHCodecs_Encoder* handle
 * \retval -1 \a encoder invalid
 */
long shcodecs_encoder_get_frame_number_to_encode (SHCodecs_Encoder * encoder);

/*
 * \param encoder The SHCodecs_Encoder* handle
 * \retval -1 \a encoder invalid
 */
long shcodecs_encoder_set_frame_number_to_encode (SHCodecs_Encoder * encoder, long frame_number_to_encode);

/*
 * Set the "frame_no_increment" field
 * \param encoder The SHCodecs_Encoder* handle
 * \retval -1 \a encoder invalid
 */
long
shcodecs_encoder_set_frame_no_increment (SHCodecs_Encoder * encoder, long frame_no_increment);

/*
 * Set the "ref_frame_num" field
 * H.264 only
 * \param encoder The SHCodecs_Encoder* handle
 * \retval -1 \a encoder invalid
 */
int
shcodecs_encoder_set_ref_frame_num (SHCodecs_Encoder * encoder, int ref_frame_num);

/*
 * Set the "output_filler_enable" field
 * H.264 only
 */
int
shcodecs_encoder_set_output_filler_enable (SHCodecs_Encoder * encoder, int output_filler_enable);

#include <shcodecs/encode_properties.h>
#include <shcodecs/encode_h264.h>
#include <shcodecs/encode_mpeg4.h>

#endif /* __SHCODECS_ENCODER_H__ */
