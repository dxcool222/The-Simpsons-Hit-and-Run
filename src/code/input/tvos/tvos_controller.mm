//=============================================================================
// Copyright (c) 2024 Radical Games Ltd.  All rights reserved.
//=============================================================================
//
// File:        tvos_controller.mm
//
// Subsystem:   Foundation Technologies - Controller System
//
// Description: Native tvOS GameController.framework input backend
//              Bypasses SDL2 for controller input on tvOS
//
//=============================================================================

#import "tvos_controller.h"

#ifdef RAD_TVOS

#import <GameController/GameController.h>
#import <Foundation/Foundation.h>
#if !defined(RAD_MACOS)
#import <UIKit/UIKit.h>
#endif

#include <stdio.h>
#include <string.h>
#include <diagnostics/tvosdiagnostics.h>

#if defined( RAD_TVOS_INPUT_DIAGNOSTICS )
    #define TVOS_INPUT_DIAG(...) SRR2::Diagnostics::AutoLogf(SRR2::Diagnostics::INPUT, __VA_ARGS__)
#else
    #define TVOS_INPUT_DIAG(...) ((void)0)
#endif

#define TVOS_INPUT_WARN(...) SRR2::Diagnostics::Anomalyf(SRR2::Diagnostics::INPUT, __VA_ARGS__)

//=============================================================================
// Constants
//=============================================================================
#define TVOS_MAX_CONTROLLERS 4
#define TVOS_STICK_DEADZONE 0.15f
#define TVOS_LOG_INTERVAL 300  // Log every N frames

//=============================================================================
// Static state
//=============================================================================
static TvOSPadState g_padStates[TVOS_MAX_CONTROLLERS];
static GCController* g_controllers[TVOS_MAX_CONTROLLERS] = {nil, nil, nil, nil};
static int g_connectedCount = 0;
static int g_initialized = 0;
static int g_frameCount = 0;
static int g_firstInputLogged[TVOS_MAX_CONTROLLERS] = {0, 0, 0, 0};

static id g_connectObserver = nil;
static id g_disconnectObserver = nil;

//=============================================================================
// Helper: Apply deadzone to stick value
//=============================================================================
static float ApplyDeadzone(float value, float deadzone)
{
    if (value > -deadzone && value < deadzone) {
        return 0.0f;
    }
    // Scale the remaining range to 0..1 or -1..0
    if (value > 0) {
        return (value - deadzone) / (1.0f - deadzone);
    } else {
        return (value + deadzone) / (1.0f - deadzone);
    }
}

//=============================================================================
// Helper: Find free slot for controller
//=============================================================================
static int FindFreeSlot(void)
{
    for (int i = 0; i < TVOS_MAX_CONTROLLERS; i++) {
        if (g_controllers[i] == nil) {
            return i;
        }
    }
    return -1;
}

//=============================================================================
// Helper: Find slot for existing controller
//=============================================================================
static int FindControllerSlot(GCController* controller)
{
    for (int i = 0; i < TVOS_MAX_CONTROLLERS; i++) {
        if (g_controllers[i] == controller) {
            return i;
        }
    }
    return -1;
}

//=============================================================================
// Helper: Update pad state from extended gamepad
//=============================================================================
static void UpdateExtendedGamepadState(int index, GCExtendedGamepad* pad)
{
    TvOSPadState* state = &g_padStates[index];
    
    // Sticks with deadzone
    // Y axes are inverted to match game convention (positive = forward/up on stick)
    state->leftStickX = ApplyDeadzone(pad.leftThumbstick.xAxis.value, TVOS_STICK_DEADZONE);
    state->leftStickY = -ApplyDeadzone(pad.leftThumbstick.yAxis.value, TVOS_STICK_DEADZONE);
    state->rightStickX = ApplyDeadzone(pad.rightThumbstick.xAxis.value, TVOS_STICK_DEADZONE);
    state->rightStickY = -ApplyDeadzone(pad.rightThumbstick.yAxis.value, TVOS_STICK_DEADZONE);
    
    // Triggers (no deadzone, raw 0..1)
    state->leftTrigger = pad.leftTrigger.value;
    state->rightTrigger = pad.rightTrigger.value;
    
    // Buttons
    unsigned int buttons = 0;
    
    // D-pad
    if (pad.dpad.up.isPressed)    buttons |= TVOS_BTN_DPAD_UP;
    if (pad.dpad.down.isPressed)  buttons |= TVOS_BTN_DPAD_DOWN;
    if (pad.dpad.left.isPressed)  buttons |= TVOS_BTN_DPAD_LEFT;
    if (pad.dpad.right.isPressed) buttons |= TVOS_BTN_DPAD_RIGHT;
    
    // Face buttons (A/B/X/Y)
    if (pad.buttonA.isPressed) buttons |= TVOS_BTN_A;
    if (pad.buttonB.isPressed) buttons |= TVOS_BTN_B;
    if (pad.buttonX.isPressed) buttons |= TVOS_BTN_X;
    if (pad.buttonY.isPressed) buttons |= TVOS_BTN_Y;
    
    // Shoulders
    if (pad.leftShoulder.isPressed)  buttons |= TVOS_BTN_LEFT_SHOULDER;
    if (pad.rightShoulder.isPressed) buttons |= TVOS_BTN_RIGHT_SHOULDER;
    
    // Thumbstick buttons (L3/R3) - check availability
    if (@available(tvOS 12.1, *)) {
        if (pad.leftThumbstickButton && pad.leftThumbstickButton.isPressed) {
            buttons |= TVOS_BTN_LEFT_THUMB;
        }
        if (pad.rightThumbstickButton && pad.rightThumbstickButton.isPressed) {
            buttons |= TVOS_BTN_RIGHT_THUMB;
        }
    }
    
    // Menu buttons - check availability
    if (@available(tvOS 13.0, *)) {
        if (pad.buttonMenu.isPressed) buttons |= TVOS_BTN_START;
        if (pad.buttonOptions && pad.buttonOptions.isPressed) buttons |= TVOS_BTN_BACK;
    }
    
    state->buttons = buttons;
    state->isExtended = 1;
}

//=============================================================================
// Helper: Update pad state from micro gamepad (Siri Remote style)
//=============================================================================
static void UpdateMicroGamepadState(int index, GCMicroGamepad* pad)
{
    TvOSPadState* state = &g_padStates[index];
    
    // Micro gamepad uses dpad as a touchpad/stick
    state->leftStickX = ApplyDeadzone(pad.dpad.xAxis.value, TVOS_STICK_DEADZONE);
    state->leftStickY = ApplyDeadzone(pad.dpad.yAxis.value, TVOS_STICK_DEADZONE);
    state->rightStickX = 0.0f;
    state->rightStickY = 0.0f;
    
    // No triggers on micro gamepad
    state->leftTrigger = 0.0f;
    state->rightTrigger = 0.0f;
    
    // Buttons
    unsigned int buttons = 0;
    
    // D-pad directions (from touchpad position)
    if (pad.dpad.up.isPressed)    buttons |= TVOS_BTN_DPAD_UP;
    if (pad.dpad.down.isPressed)  buttons |= TVOS_BTN_DPAD_DOWN;
    if (pad.dpad.left.isPressed)  buttons |= TVOS_BTN_DPAD_LEFT;
    if (pad.dpad.right.isPressed) buttons |= TVOS_BTN_DPAD_RIGHT;
    
    // A and X buttons (micro gamepad has buttonA and buttonX)
    if (pad.buttonA.isPressed) buttons |= TVOS_BTN_A;
    if (pad.buttonX.isPressed) buttons |= TVOS_BTN_X;
    
    // Menu button
    if (@available(tvOS 13.0, *)) {
        if (pad.buttonMenu.isPressed) buttons |= TVOS_BTN_START;
    }
    
    state->buttons = buttons;
    state->isExtended = 0;
}

//=============================================================================
// Helper: Clear pad state
//=============================================================================
static void ClearPadState(int index)
{
    memset(&g_padStates[index], 0, sizeof(TvOSPadState));
    g_firstInputLogged[index] = 0;
}

//=============================================================================
// Controller connect handler
//=============================================================================
static void OnControllerConnected(GCController* controller)
{
    int slot = FindControllerSlot(controller);
    if (slot >= 0) {
        // Already tracked
        return;
    }
    
    slot = FindFreeSlot();
    if (slot < 0) {
        TVOS_INPUT_WARN("[TvOSInput] WARNING: No free slots for controller '%s'\n",
               controller.vendorName ? controller.vendorName.UTF8String : "Unknown");
        return;
    }
    
    g_controllers[slot] = controller;
    ClearPadState(slot);
    g_padStates[slot].connected = 1;
    g_padStates[slot].vendorName = controller.vendorName ? controller.vendorName.UTF8String : "Unknown";
    g_padStates[slot].productCategory = controller.productCategory ? controller.productCategory.UTF8String : "Unknown";
    g_connectedCount++;
    
    // Determine profile type
    const char* profileType = "None";
    if (controller.extendedGamepad) {
        profileType = "ExtendedGamepad";
        // Configure extended gamepad for polling (we poll rather than use handlers for simplicity)
    } else if (controller.microGamepad) {
        profileType = "MicroGamepad";
        // Enable absolute dpad values for better stick-like behavior
        controller.microGamepad.reportsAbsoluteDpadValues = YES;
    }
    
    TVOS_INPUT_DIAG("[TvOSInput] CONNECTED: slot=%d vendor='%s' category='%s' profile=%s\n",
           slot,
           g_padStates[slot].vendorName,
           g_padStates[slot].productCategory,
           profileType);
    SRR2::Diagnostics::RecordControllerConnection( true, slot, g_padStates[slot].vendorName );
}

//=============================================================================
// Controller disconnect handler
//=============================================================================
static void OnControllerDisconnected(GCController* controller)
{
    int slot = FindControllerSlot(controller);
    if (slot < 0) {
        return;
    }
    
    TVOS_INPUT_DIAG("[TvOSInput] DISCONNECTED: slot=%d vendor='%s'\n",
           slot, g_padStates[slot].vendorName);
    SRR2::Diagnostics::RecordControllerConnection( false, slot, g_padStates[slot].vendorName );
    
    g_controllers[slot] = nil;
    ClearPadState(slot);
    g_connectedCount--;
}

//=============================================================================
// Public API Implementation
//=============================================================================

void TvOSInput_Init(void)
{
    if (g_initialized) {
        TVOS_INPUT_DIAG("[TvOSInput] Already initialized, skipping.\n");
        return;
    }
    
    TVOS_INPUT_DIAG("[TvOSInput] ========================================\n");
    TVOS_INPUT_DIAG("[TvOSInput] Initializing native GameController.framework backend...\n");
    
#if !defined(RAD_MACOS)
    // Log app state for debugging (UIKit / tvOS only)
    UIApplicationState appState = [UIApplication sharedApplication].applicationState;
    const char* stateStr = "Unknown";
    switch (appState) {
        case UIApplicationStateActive: stateStr = "Active"; break;
        case UIApplicationStateInactive: stateStr = "Inactive"; break;
        case UIApplicationStateBackground: stateStr = "Background"; break;
    }
    TVOS_INPUT_DIAG("[TvOSInput] UIApplication state: %s (%d)\n", stateStr, (int)appState);
#endif
    
    // Clear state
    memset(g_padStates, 0, sizeof(g_padStates));
    memset(g_controllers, 0, sizeof(g_controllers));
    memset(g_firstInputLogged, 0, sizeof(g_firstInputLogged));
    g_connectedCount = 0;
    g_frameCount = 0;
    
#if defined(RAD_MACOS)
    if (@available(macOS 11.3, *)) {
        GCController.shouldMonitorBackgroundEvents = YES;
        TVOS_INPUT_DIAG("[TvOSInput] Background event monitoring enabled (macOS)\n");
    }
#else
    // Enable background controller monitoring (iOS 14.5+ / tvOS 14.5+)
    if (@available(tvOS 14.5, *)) {
        GCController.shouldMonitorBackgroundEvents = YES;
        TVOS_INPUT_DIAG("[TvOSInput] Background event monitoring enabled\n");
    }
#endif
    
    // Register for connect notifications
    TVOS_INPUT_DIAG("[TvOSInput] Registering for controller connect/disconnect notifications...\n");
    g_connectObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:GCControllerDidConnectNotification
        object:nil
        queue:[NSOperationQueue mainQueue]
        usingBlock:^(NSNotification* note) {
            GCController* controller = note.object;
            TVOS_INPUT_DIAG("[TvOSInput] NOTIFICATION: GCControllerDidConnectNotification received\n");
            OnControllerConnected(controller);
        }];
    
    // Register for disconnect notifications
    g_disconnectObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:GCControllerDidDisconnectNotification
        object:nil
        queue:[NSOperationQueue mainQueue]
        usingBlock:^(NSNotification* note) {
            GCController* controller = note.object;
            TVOS_INPUT_DIAG("[TvOSInput] NOTIFICATION: GCControllerDidDisconnectNotification received\n");
            OnControllerDisconnected(controller);
        }];
    
    // Process any already-connected controllers
    TVOS_INPUT_DIAG("[TvOSInput] Querying [GCController controllers]...\n");
    NSArray<GCController*>* controllers = [GCController controllers];
    TVOS_INPUT_DIAG("[TvOSInput] Found %lu already-connected controller(s)\n", (unsigned long)controllers.count);
    
    for (GCController* controller in controllers) {
        TVOS_INPUT_DIAG("[TvOSInput] Processing controller: vendor='%s' extended=%d micro=%d\n",
               controller.vendorName ? controller.vendorName.UTF8String : "(null)",
               controller.extendedGamepad != nil ? 1 : 0,
               controller.microGamepad != nil ? 1 : 0);
        OnControllerConnected(controller);
    }
    
    // Start wireless discovery
    TvOSInput_StartDiscovery();
    
    g_initialized = 1;
    TVOS_INPUT_DIAG("[TvOSInput] Initialization complete. Connected controllers: %d\n", g_connectedCount);
    TVOS_INPUT_DIAG("[TvOSInput] ========================================\n");
}

void TvOSInput_Shutdown(void)
{
    if (!g_initialized) {
        return;
    }
    
    TVOS_INPUT_DIAG("[TvOSInput] Shutting down...\n");
    
    TvOSInput_StopDiscovery();
    
    // Remove observers
    if (g_connectObserver) {
        [[NSNotificationCenter defaultCenter] removeObserver:g_connectObserver];
        g_connectObserver = nil;
    }
    if (g_disconnectObserver) {
        [[NSNotificationCenter defaultCenter] removeObserver:g_disconnectObserver];
        g_disconnectObserver = nil;
    }
    
    // Clear state
    for (int i = 0; i < TVOS_MAX_CONTROLLERS; i++) {
        g_controllers[i] = nil;
        ClearPadState(i);
    }
    g_connectedCount = 0;
    g_initialized = 0;
    
    TVOS_INPUT_DIAG("[TvOSInput] Shutdown complete\n");
}

void TvOSInput_Pump(void)
{
    if (!g_initialized) {
        return;
    }
    
    g_frameCount++;
    
    int anyInput = 0;

    // Update state for each connected controller
    for (int i = 0; i < TVOS_MAX_CONTROLLERS; i++) {
        GCController* controller = g_controllers[i];
        if (controller == nil) {
            if (g_padStates[i].connected) {
                ClearPadState(i);
            }
            continue;
        }
        
        // Poll the controller's current state
        if (controller.extendedGamepad) {
            UpdateExtendedGamepadState(i, controller.extendedGamepad);
        } else if (controller.microGamepad) {
            UpdateMicroGamepadState(i, controller.microGamepad);
        }

        TvOSPadState* state = &g_padStates[i];
        if (state->leftStickX != 0.0f || state->leftStickY != 0.0f ||
            state->rightStickX != 0.0f || state->rightStickY != 0.0f ||
            state->leftTrigger != 0.0f || state->rightTrigger != 0.0f ||
            state->buttons != 0) {
            anyInput = 1;
        }
        
#if defined( RAD_TVOS_INPUT_DIAGNOSTICS )
        // Log first input from this controller
        if (!g_firstInputLogged[i]) {
            // Check if there's any non-zero input
            if (state->leftStickX != 0.0f || state->leftStickY != 0.0f ||
                state->rightStickX != 0.0f || state->rightStickY != 0.0f ||
                state->leftTrigger != 0.0f || state->rightTrigger != 0.0f ||
                state->buttons != 0) {
                TVOS_INPUT_DIAG("[TvOSInput] FIRST INPUT slot=%d: LX=%.2f LY=%.2f RX=%.2f RY=%.2f LT=%.2f RT=%.2f BTN=0x%04X\n",
                       i, state->leftStickX, state->leftStickY,
                       state->rightStickX, state->rightStickY,
                       state->leftTrigger, state->rightTrigger,
                       state->buttons);
                g_firstInputLogged[i] = 1;
            }
        }
#endif
    }

    SRR2::Diagnostics::RecordControllerSample( g_connectedCount > 0, anyInput != 0 );
    
#if defined( RAD_TVOS_INPUT_DIAGNOSTICS )
    // Periodic status log (every TVOS_LOG_INTERVAL frames)
    if ((g_frameCount % TVOS_LOG_INTERVAL) == 0) {
        SRR2::Diagnostics::Summaryf(SRR2::Diagnostics::INPUT,
               "[INPUT_SUMMARY] frame=%d connected=%d hasInput=%d",
               g_frameCount, g_connectedCount, anyInput);
    }
#endif
}

int TvOSInput_GetPadCount(void)
{
    return g_connectedCount;
}

const TvOSPadState* TvOSInput_GetState(int index)
{
    if (index < 0 || index >= TVOS_MAX_CONTROLLERS) {
        return NULL;
    }
    if (!g_padStates[index].connected) {
        return NULL;
    }
    return &g_padStates[index];
}

int TvOSInput_HasAnyController(void)
{
    return g_connectedCount > 0 ? 1 : 0;
}

void TvOSInput_StartDiscovery(void)
{
    TVOS_INPUT_DIAG("[TvOSInput] Starting wireless controller discovery...\n");
    [GCController startWirelessControllerDiscoveryWithCompletionHandler:^{
        TVOS_INPUT_DIAG("[TvOSInput] Wireless controller discovery completed\n");
    }];
}

void TvOSInput_StopDiscovery(void)
{
    TVOS_INPUT_DIAG("[TvOSInput] Stopping wireless controller discovery\n");
    [GCController stopWirelessControllerDiscovery];
}

#endif // RAD_TVOS
