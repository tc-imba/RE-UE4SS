#pragma once

#ifdef RC_DYNAMIC_OUTPUT_BUILD_STATIC
#ifndef RC_DYNOUT_API
#define RC_DYNOUT_API
#endif
#elif !defined(RC_DYNAMIC_OUTPUT_EXPORTS)
#ifndef RC_DYNOUT_API
#define RC_DYNOUT_API __declspec(dllimport)
#endif
#else
#ifndef RC_DYNOUT_API
#define RC_DYNOUT_API __declspec(dllexport)
#endif
#endif
