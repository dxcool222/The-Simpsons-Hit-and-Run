//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef RADSOUNDWIN_HPP
#define RADSOUNDWIN_HPP

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

//============================================================================
// Helper Functions
//============================================================================

unsigned int radSoundFloatAngleToULongWin( float angle );
float        radSoundULongAngleToFloatWin( unsigned int angle );
float        radSoundVolumeDbToHardwareWin( float volume );
float        radSoundVolumeHardwareToDbWin( float hardwareVolume );

#endif // RADSOUNDWIN32_HPP
