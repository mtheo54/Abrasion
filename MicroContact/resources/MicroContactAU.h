
#include <TargetConditionals.h>
#if TARGET_OS_IOS == 1 || TARGET_OS_VISION == 1
#import <UIKit/UIKit.h>
#else
#import <Cocoa/Cocoa.h>
#endif

#define IPLUG_AUVIEWCONTROLLER IPlugAUViewController_vMicroContact
#define IPLUG_AUAUDIOUNIT IPlugAUAudioUnit_vMicroContact
#import <MicroContactAU/IPlugAUViewController.h>
#import <MicroContactAU/IPlugAUAudioUnit.h>

//! Project version number for MicroContactAU.
FOUNDATION_EXPORT double MicroContactAUVersionNumber;

//! Project version string for MicroContactAU.
FOUNDATION_EXPORT const unsigned char MicroContactAUVersionString[];

@class IPlugAUViewController_vMicroContact;
