/**
 * @file easylogging_impl.cpp
 * @brief Implementation file for easylogging++ (header-only library)
 *
 * easylogging++ requires EXACTLY ONE compilation unit to contain
 * INITIALIZE_EASYLOGGINGPP. This file serves that purpose for all benchmarks.
 */

#ifdef HAVE_EASYLOGGINGPP

#define ELPP_THREAD_SAFE
#define ELPP_NO_DEFAULT_LOG_FILE
#include <easylogging++.h>

// this must appear in exactly one .cpp file
INITIALIZE_EASYLOGGINGPP

#endif // HAVE_EASYLOGGINGPP
