#pragma once
#ifdef __cplusplus
#if defined( _WIN32 ) && !defined( _X360 )
#if defined( GAME_CAPTURE_EXPORTS )
#define GAME_CAPTURE_EXPORT __declspec( dllexport ) 
#elif defined( GAME_CAPTURE_NODLL )
#define GAME_CAPTURE_EXPORT 
#else
#define GAME_CAPTURE_EXPORT  __declspec( dllimport ) 
#endif 
#elif defined( GNUC )
#if defined( GAME_CAPTURE_EXPORTS )
#define GAME_CAPTURE_EXPORT  __attribute__ ((visibility("default"))) 
#else
#define GAME_CAPTURE_EXPORT 
#endif 
#else // !WIN32
#if defined( GAME_CAPTURE_EXPORTS )
#define GAME_CAPTURE_EXPORT 
#else
#define GAME_CAPTURE_EXPORT 
#endif 
#endif
#else
#define GAME_CAPTURE_EXPORT 
#endif