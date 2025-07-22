#ifndef KISS_FFT_LOG_H
#define KISS_FFT_LOG_H

#include <stdio.h>

/*
 * KissFFT optionally uses logging macros for errors and warnings. The original
 * library allows users to provide custom implementations. For this embedded
 * copy we simply discard the messages to avoid pulling in additional
 * dependencies, while still satisfying the required symbols during linking.
 */

#define KISS_FFT_LOG(...)     do { (void)0; } while(0)
#define KISS_FFT_ERROR(...)   do { (void)0; } while(0)
#define KISS_FFT_WARNING(...) do { (void)0; } while(0)

#endif /* KISS_FFT_LOG_H */
