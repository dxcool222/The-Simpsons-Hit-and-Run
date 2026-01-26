//=============================================================================
// Copyright (c) 2024 Radical Games Ltd.  All rights reserved.
//=============================================================================
//
// File:        tvos_controller.h
//
// Subsystem:   Foundation Technologies - Controller System
//
// Description: Native tvOS GameController.framework input backend
//              Bypasses SDL2 for controller input on tvOS
//
//=============================================================================

#ifndef TVOS_CONTROLLER_H
#define TVOS_CONTROLLER_H

#ifdef RAD_TVOS

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Button bit definitions - matches game's eButtonMap order
//=============================================================================
typedef enum TvOSButton {
    TVOS_BTN_DPAD_UP        = (1 << 0),
    TVOS_BTN_DPAD_DOWN      = (1 << 1),
    TVOS_BTN_DPAD_LEFT      = (1 << 2),
    TVOS_BTN_DPAD_RIGHT     = (1 << 3),
    TVOS_BTN_START          = (1 << 4),   // Menu button
    TVOS_BTN_BACK           = (1 << 5),   // Options button
    TVOS_BTN_LEFT_THUMB     = (1 << 6),   // L3
    TVOS_BTN_RIGHT_THUMB    = (1 << 7),   // R3
    TVOS_BTN_A              = (1 << 8),   // Cross on PlayStation
    TVOS_BTN_B              = (1 << 9),   // Circle on PlayStation
    TVOS_BTN_X              = (1 << 10),  // Square on PlayStation
    TVOS_BTN_Y              = (1 << 11),  // Triangle on PlayStation
    TVOS_BTN_LEFT_SHOULDER  = (1 << 12),  // L1/LB
    TVOS_BTN_RIGHT_SHOULDER = (1 << 13),  // R1/RB
} TvOSButton;

//=============================================================================
// Controller state structure
//=============================================================================
typedef struct TvOSPadState {
    int connected;              // 1 if controller is connected, 0 otherwise
    int isExtended;             // 1 if extended gamepad, 0 if micro gamepad
    
    // Analog sticks (-1.0 to 1.0)
    float leftStickX;
    float leftStickY;
    float rightStickX;
    float rightStickY;
    
    // Triggers (0.0 to 1.0)
    float leftTrigger;
    float rightTrigger;
    
    // Button bitmask
    unsigned int buttons;
    
    // Controller info (for debugging)
    const char* vendorName;
    const char* productCategory;
} TvOSPadState;

//=============================================================================
// Public API
//=============================================================================

// Initialize the native controller system
// Call once at startup after SDL_Init but before game loop
void TvOSInput_Init(void);

// Shutdown the native controller system
// Call once at shutdown
void TvOSInput_Shutdown(void);

// Pump input - call once per frame on main thread
// Updates all controller states
void TvOSInput_Pump(void);

// Get the number of connected controllers (0-4)
int TvOSInput_GetPadCount(void);

// Get controller state by index (0-3)
// Returns NULL if index is invalid or controller not connected
const TvOSPadState* TvOSInput_GetState(int index);

// Check if any controller is connected
int TvOSInput_HasAnyController(void);

// Start wireless controller discovery
void TvOSInput_StartDiscovery(void);

// Stop wireless controller discovery
void TvOSInput_StopDiscovery(void);

#ifdef __cplusplus
}
#endif

#endif // RAD_TVOS

#endif // TVOS_CONTROLLER_H
