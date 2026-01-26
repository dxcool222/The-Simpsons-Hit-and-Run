//=============================================================================
// Copyright (c) 2024 Radical Games Ltd.  All rights reserved.
//=============================================================================
//
// File:        tvoscontroller.cpp
//
// Subsystem:   Foundation Technologies - Controller System
//
// Description: Native tvOS controller implementation using GameController.framework
//              Bypasses SDL2 for controller input on tvOS
//
//=============================================================================

#ifdef RAD_TVOS

#include "pch.hpp"
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <radobject.hpp>
#include <radcontroller.hpp>
#include <raddebug.hpp>
#include <radstring.hpp>
#include <radobjectlist.hpp>
#include <radtime.hpp>
#include <radmemorymonitor.hpp>
#include "radcontrollerbuffer.hpp"

#include <input/tvos/tvos_controller.h>

//============================================================================
// Internal Interfaces
//============================================================================

struct IRadControllerInputPointTvOS
    :
    public IRadControllerInputPoint
{
    virtual void iInitialize( void ) = 0;
    virtual void iVirtualTimeReMapped( unsigned int virtualTime ) = 0;
    virtual void iVirtualTimeChanged( unsigned int virtualTime, const TvOSPadState* padState ) = 0;
};

struct IRadControllerTvOS
    :
    public IRadController
{
    virtual void iPoll( unsigned int virtualTime ) = 0;
    virtual void iVirtualTimeReMapped( unsigned int virtualTime ) = 0;
    virtual void iVirtualTimeChanged( unsigned int virtualTime ) = 0;
    virtual void iSetBufferTime( unsigned int milliseconds, unsigned int pollingRate ) = 0;
    virtual void iSetPadIndex( int index ) = 0;
};

//============================================================================
// Globals
//============================================================================

struct TvOSInputPoint
{
    const char * m_pType;
    const char * m_pName;
    int m_Index;  // Index into button mask or axis
};

static const char * g_TvOSipt[] =
{
    "Button",
    "AnalogButton",
    "XAxis",
    "YAxis"
};

// Input point definitions matching game's eButtonMap order
static TvOSInputPoint g_TvOSPoints[] =
{
    { g_TvOSipt[ 0 ], "DPadUp",           0 },   // TVOS_BTN_DPAD_UP
    { g_TvOSipt[ 0 ], "DPadDown",         1 },   // TVOS_BTN_DPAD_DOWN
    { g_TvOSipt[ 0 ], "DPadLeft",         2 },   // TVOS_BTN_DPAD_LEFT
    { g_TvOSipt[ 0 ], "DPadRight",        3 },   // TVOS_BTN_DPAD_RIGHT
    { g_TvOSipt[ 0 ], "Start",            4 },   // TVOS_BTN_START (Menu)
    { g_TvOSipt[ 0 ], "Back",             5 },   // TVOS_BTN_BACK (Options)
    { g_TvOSipt[ 0 ], "LeftThumb",        6 },   // TVOS_BTN_LEFT_THUMB (L3)
    { g_TvOSipt[ 0 ], "RightThumb",       7 },   // TVOS_BTN_RIGHT_THUMB (R3)
    { g_TvOSipt[ 0 ], "A",                8 },   // TVOS_BTN_A
    { g_TvOSipt[ 0 ], "B",                9 },   // TVOS_BTN_B
    { g_TvOSipt[ 0 ], "X",                10 },  // TVOS_BTN_X
    { g_TvOSipt[ 0 ], "Y",                11 },  // TVOS_BTN_Y
    { g_TvOSipt[ 0 ], "Black",            12 },  // TVOS_BTN_LEFT_SHOULDER (L1)
    { g_TvOSipt[ 0 ], "White",            13 },  // TVOS_BTN_RIGHT_SHOULDER (R1)
    { g_TvOSipt[ 1 ], "LeftTrigger",      0 },   // Left trigger axis
    { g_TvOSipt[ 1 ], "RightTrigger",     1 },   // Right trigger axis
    { g_TvOSipt[ 2 ], "LeftStickX",       0 },   // Left stick X axis
    { g_TvOSipt[ 3 ], "LeftStickY",       1 },   // Left stick Y axis
    { g_TvOSipt[ 2 ], "RightStickX",      2 },   // Right stick X axis
    { g_TvOSipt[ 3 ], "RightStickY",      3 }    // Right stick Y axis
};

static class radControllerSystemTvOS* s_pTheTvOSControllerSystem = NULL;
static radMemoryAllocator g_ControllerSystemAllocator = RADMEMORY_ALLOC_DEFAULT;

//============================================================================
// Component: radControllerOutputPointTvOS
//============================================================================

class radControllerOutputPointTvOS
    :
    public IRadControllerOutputPoint,
    public radRefCount
{
    public:

    IMPLEMENT_REFCOUNTED( "radControllerOutputPointTvOS" )

    radControllerOutputPointTvOS( const char * pName )
        :
        radRefCount( 0 ),
        m_pName( pName ),
        m_Gain( 0.0f )
    {
        radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "radControllerOutputPointTvOS" );
    }

    ~radControllerOutputPointTvOS( void ) { }

    virtual const char * GetName( void ) { return m_pName; }
    virtual const char * GetType( void ) { return "Analog"; }
    virtual float GetGain( void ) { return m_Gain; }

    virtual void SetGain( float value )
    {
        m_Gain = (value < 0.0f) ? 0.0f : ((value > 1.0f) ? 1.0f : value);
    }

    const char * m_pName;
    float m_Gain;
};

//============================================================================
// Component: radControllerInputPointTvOS
//============================================================================

class radControllerInputPointTvOS
    :
    public IRadControllerInputPointTvOS,
    public radRefCount
{
    public:

    IMPLEMENT_REFCOUNTED( "radControllerInputPointTvOS" )

    virtual void iVirtualTimeReMapped( unsigned int virtualTime )
    {
        m_TimeInState = 0;
        m_TimeOfStateChange = virtualTime;
    }

    float CalculateNewValue( const TvOSPadState* padState )
    {
        if ( padState == NULL )
        {
            return 0.0f;
        }

        float newValue = 0.0f;

        if ( m_pType == g_TvOSipt[ 0 ] ) // Button
        {
            // Check button bitmask
            unsigned int mask = (1 << m_Identifier);
            newValue = (padState->buttons & mask) ? 1.0f : 0.0f;
        }
        else if ( m_pType == g_TvOSipt[ 1 ] ) // Analog Button (triggers)
        {
            if ( m_Identifier == 0 )
            {
                newValue = padState->leftTrigger;
            }
            else
            {
                newValue = padState->rightTrigger;
            }
        }
        else if ( m_pType == g_TvOSipt[ 2 ] || m_pType == g_TvOSipt[ 3 ] ) // X/Y Axis
        {
            float axisValue = 0.0f;
            switch ( m_Identifier )
            {
                case 0: axisValue = padState->leftStickX; break;
                case 1: axisValue = padState->leftStickY; break;
                case 2: axisValue = padState->rightStickX; break;
                case 3: axisValue = padState->rightStickY; break;
            }
            
            // Convert from -1..1 to 0..1 range (game expects 0..1 with 0.5 as center)
            newValue = (axisValue + 1.0f) * 0.5f;
            
            // Invert Y axis if needed (game may expect up = higher value)
            if ( m_pType == g_TvOSipt[ 3 ] ) // YAxis
            {
                newValue = 1.0f - newValue;
            }
        }

        return newValue;
    }

    virtual void iVirtualTimeChanged( unsigned int virtualTime, const TvOSPadState* padState )
    {
        float newValue = CalculateNewValue( padState );
        
        if ( ( newValue != m_Value ) && 
             ( fabsf( newValue - m_Value ) >= m_Tolerance ) )
        {
#ifdef RAD_TVOS
            const float oldValue = m_Value;
            if ( m_pType == g_TvOSipt[ 0 ] )
            {
                if ( oldValue > 0.5f || newValue > 0.5f )
                {
                    printf( "[radControllerTvOS] Input '%s' changed %.2f -> %.2f (identifier=%d)\n",
                            m_pName ? m_pName : "(null)",
                            oldValue,
                            newValue,
                            m_Identifier );
                    fflush( stdout );
                }
            }
#endif
            m_Value = newValue;
            m_TimeOfStateChange = virtualTime;
            m_TimeInState = 0;

            AddRef( );

            IRadWeakCallbackWrapper * pIWcr;
            m_xIOl_Callbacks->Reset( );

            if ((pIWcr = reinterpret_cast< IRadWeakCallbackWrapper * >( m_xIOl_Callbacks->GetNext( ) )))
            {
                IRadControllerInputPointCallback * pCallback = ( IRadControllerInputPointCallback* ) pIWcr->GetWeakInterface( );
                unsigned int userData = reinterpret_cast< uintptr_t >( pIWcr->GetUserData( ) );
                pCallback->OnControllerInputPointChange( userData, m_Value );           
            }

            Release( );
        }
        else
        {
            m_TimeInState = virtualTime - m_TimeOfStateChange;
        }
    }

    virtual void iInitialize( void )
    {
        m_Value = 0.0f;
    }

    virtual const char * GetName( void ) { return m_pName; }
    virtual const char * GetType( void ) { return m_pType; }

    virtual void SetTolerance( float percentage )
    {
        percentage = (percentage < 0.0f) ? 0.0f : ((percentage > 1.0f) ? 1.0f : percentage);
        m_Tolerance = percentage;
    }

    virtual float GetTolerance( void ) { return m_Tolerance; }

    virtual void RegisterControllerInputPointCallback( IRadControllerInputPointCallback * pCallback, unsigned int userData = 0 )
    {
        rAssert( pCallback != NULL );
        ref< IRadWeakCallbackWrapper > xIWcr;
        radWeakCallbackWrapperCreate( &xIWcr, g_ControllerSystemAllocator ); 
        rAssert( xIWcr != NULL );
        if ( xIWcr != NULL )
        {
            xIWcr->SetWeakInterface( pCallback );
            xIWcr->SetUserData( (void*)(uintptr_t) userData );
        }
        m_xIOl_Callbacks->AddObject( xIWcr );
    }

    virtual void UnRegisterControllerInputPointCallback( IRadControllerInputPointCallback * pCallback )
    {
        rAssert( pCallback != NULL );
        IRadWeakCallbackWrapper * pIWcr;
        m_xIOl_Callbacks->Reset( );
        if ((pIWcr = reinterpret_cast< IRadWeakCallbackWrapper * >( m_xIOl_Callbacks->GetNext( ) )))
        {
            if ( pIWcr->GetWeakInterface( ) == pCallback )
            {
                m_xIOl_Callbacks->RemoveObject( pIWcr );
                return;
            }
        }
        rAssertMsg( false, "Controller Input Point Callback Not Registered." );
    }

    virtual float GetCurrentValue( unsigned int * pTime = NULL )
    {
        if ( pTime != NULL )
        {
            *pTime = m_TimeInState;
        }                
        return (( m_MaxRange - m_MinRange ) * m_Value ) + m_MinRange;
    }

    virtual void SetRange( float min, float max )
    {
        m_MinRange = min;
        m_MaxRange = max;
    }
    
    virtual void GetRange( float * pMin, float * pMax )
    {
        rAssert( pMin != NULL || pMax != NULL );
        if ( pMin != NULL ) *pMin = m_MinRange;
        if ( pMax != NULL ) *pMax = m_MaxRange;
    }

    radControllerInputPointTvOS( const char * pType, const char * pName, int id )
        :
        radRefCount( 0 ),
        m_Value( 0.0f ),
        m_MinRange( 0.0f ),
        m_MaxRange( 1.0f ),
        m_Tolerance( 0.0f ),
        m_TimeInState( 0 ),
        m_TimeOfStateChange( 0 ),
        m_pType( pType ),
        m_pName( pName ),
        m_Identifier( id )
    {
        radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "radControllerInputPointTvOS" );
        ::radObjectListCreate( & m_xIOl_Callbacks, g_ControllerSystemAllocator );
    }
    
    ~radControllerInputPointTvOS( void )
    {        
        rAssertMsg( m_xIOl_Callbacks->GetSize() == 0, "Somebody forgot to UnRegister an input point callback" );
    }

    float m_Value;
    float m_MinRange;
    float m_MaxRange;
    float m_Tolerance;
    unsigned int m_TimeInState;
    unsigned int m_TimeOfStateChange;
    const char * m_pType;
    const char * m_pName;
    int m_Identifier;
    ref< IRadObjectList > m_xIOl_Callbacks;
};

//============================================================================
// Component: radControllerTvOS
//============================================================================

class radControllerTvOS
    :
    public IRadControllerTvOS,
    public radRefCount
{
    public:

    IMPLEMENT_REFCOUNTED( "radControllerTvOS" )

    virtual void iPoll( unsigned int virtualTime )
    {
        // Rumble output (if supported)
        if ( GetRefCount( ) > 1 && m_xIOl_OutputPoints != NULL )
        {
            IRadControllerOutputPoint * pLeft  = reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetAt( 0 ) );
            IRadControllerOutputPoint * pRight = reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetAt( 1 ) );
            // Note: Rumble not yet implemented for tvOS native backend
        }
    }

    virtual void iVirtualTimeReMapped( unsigned int virtualTime )
    {
        IRadControllerInputPointTvOS * pICip2;
        m_xIOl_InputPoints->Reset( );
        while ((pICip2 = reinterpret_cast< IRadControllerInputPointTvOS * >( m_xIOl_InputPoints->GetNext( ) )))
        {
            pICip2->iVirtualTimeReMapped( virtualTime );
        }
    }

    virtual void iVirtualTimeChanged( unsigned int virtualTime )
    {
        if( GetRefCount( ) > 1 && m_xIOl_InputPoints != NULL )
        {
            const TvOSPadState* padState = TvOSInput_GetState( m_PadIndex );
            
            m_xIOl_InputPoints->Reset();
            IRadControllerInputPointTvOS* pIXbcip2;
            while((pIXbcip2 = reinterpret_cast<IRadControllerInputPointTvOS*>(m_xIOl_InputPoints->GetNext())))
            {
                pIXbcip2->iVirtualTimeChanged( virtualTime, padState );
            }
        }
    }

    virtual void iSetBufferTime( unsigned int milliseconds, unsigned int pollingRate ) { }
    
    virtual void iSetPadIndex( int index ) { m_PadIndex = index; }

    virtual bool IsConnected( void )
    {
        const TvOSPadState* state = TvOSInput_GetState( m_PadIndex );
        return state != NULL && state->connected;
    }

    virtual const char * GetType( void ) { return "TvOSGamepad"; }
    virtual const char * GetClassification( void ) { return "Joystick"; }

    virtual unsigned int GetNumberOfInputPointsOfType( const char * pType )
    {
        rAssert( pType != NULL );
        unsigned int count = 0;
        m_xIOl_InputPoints->Reset( );
        IRadControllerInputPoint * pICip2;
        while ((pICip2 = reinterpret_cast< IRadControllerInputPointTvOS * >( m_xIOl_InputPoints->GetNext( ) )))
        {
            if ( strcmp( pICip2->GetType( ), pType ) == 0 )
            {
                count++;
            }        
        }
        return count;
    }

    unsigned int GetNumberOfOutputPointsOfType( const char * pType )
    {
        rAssert( pType != NULL );
        unsigned int count = 0;
        m_xIOl_OutputPoints->Reset( );
        IRadControllerOutputPoint * pICip2;
        while ((pICip2 = reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetNext( ) )))
        {
            if ( strcmp( pICip2->GetType( ), pType ) == 0 )
            {
                count++;
            }        
        }
        return count;
    }

    virtual IRadControllerInputPoint * GetInputPointByTypeAndIndex( const char * pType, unsigned int index )
    {
        rAssert( pType != NULL );
        unsigned int count = 0;
        m_xIOl_InputPoints->Reset( );
        IRadControllerInputPoint * pICip2;
        while ((pICip2 = reinterpret_cast< IRadControllerInputPointTvOS * >( m_xIOl_InputPoints->GetNext( ) )))
        {
            if ( strcmp( pICip2->GetType( ), pType ) == 0 )
            {
                if ( count == index ) return pICip2;
                count++;
            }
        }
        return NULL;
    }

    IRadControllerOutputPoint * GetOutputPointByTypeAndIndex( const char * pType, unsigned int index )
    {
        rAssert( pType != NULL );
        unsigned int count = 0;
        m_xIOl_OutputPoints->Reset( );
        IRadControllerOutputPoint * pICip2;
        while ((pICip2 = reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetNext( ) )))
        {
            if ( strcmp( pICip2->GetType( ), pType ) == 0 )
            {
                if ( count == index ) return pICip2;
                count++;
            }
        }
        return NULL;
    }

    virtual IRadControllerInputPoint * GetInputPointByName( const char * pName )
    {
        rAssert( pName != NULL );
        m_xIOl_InputPoints->Reset( );
        IRadControllerInputPoint * pICip2;
        while ((pICip2 = reinterpret_cast< IRadControllerInputPointTvOS * >( m_xIOl_InputPoints->GetNext( ) )))
        {
            if ( strcmp( pName, pICip2->GetName( ) ) == 0 )
            {
                return pICip2;
            }
        }
        return NULL;
    }

    IRadControllerOutputPoint * GetOutputPointByName( const char * pName )
    {
        rAssert( pName != NULL );
        m_xIOl_OutputPoints->Reset( );
        IRadControllerOutputPoint * pICip2;
        while ((pICip2 = reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetNext( ) )))
        {
            if ( strcmp( pName, pICip2->GetName( ) ) == 0 )
            {
                return pICip2;
            }
        }
        return NULL;
    }

    virtual const char * GetLocation( void )
    {
        return m_xIString_Location->GetChars( );
    }

    virtual unsigned int GetNumberOfInputPoints( void )
    {
        return m_xIOl_InputPoints->GetSize( );
    }

    virtual IRadControllerInputPoint * GetInputPointByIndex( unsigned int index )
    {
        return reinterpret_cast< IRadControllerInputPointTvOS * >( m_xIOl_InputPoints->GetAt( index ) );
    }

    virtual unsigned int GetNumberOfOutputPoints( void )
    {
        return m_xIOl_OutputPoints->GetSize( );
    }

    virtual IRadControllerOutputPoint * GetOutputPointByIndex( unsigned int index )
    {
        return reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetAt( index ) );
    }

    radControllerTvOS( unsigned int thisAllocator, int padIndex, unsigned int virtualTime, unsigned int bufferTime, unsigned int pollingRate )
        :
        radRefCount( 0 ),
        m_PadIndex( padIndex )
    {
        radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "radControllerTvOS" );

        ::radObjectListCreate( & m_xIOl_InputPoints, g_ControllerSystemAllocator );
        ::radObjectListCreate( & m_xIOl_OutputPoints, g_ControllerSystemAllocator );
        ::radStringCreate( & m_xIString_Location, g_ControllerSystemAllocator );

        m_xIString_Location->SetSize( 12 );
        m_xIString_Location->Append( "Port" );
        m_xIString_Location->Append( (unsigned int) padIndex );
        m_xIString_Location->Append( "\\Slot0" );

        // Create input points
        for ( unsigned int i = 0; i < ( sizeof( g_TvOSPoints ) / sizeof( TvOSInputPoint ) ); i++ )
        {
            ref< radControllerInputPointTvOS > pInputPoint = new( g_ControllerSystemAllocator ) radControllerInputPointTvOS(
                g_TvOSPoints[ i ].m_pType, 
                g_TvOSPoints[ i ].m_pName,
                g_TvOSPoints[ i ].m_Index
            );
            m_xIOl_InputPoints->AddObject( pInputPoint );
            pInputPoint->iInitialize( );
        }

        // Create output points (for rumble)
        if ( m_xIOl_OutputPoints != NULL )
        {
            radControllerOutputPointTvOS * pLeft = new( g_ControllerSystemAllocator ) radControllerOutputPointTvOS( "LeftMotor" );
            radControllerOutputPointTvOS * pRight = new( g_ControllerSystemAllocator ) radControllerOutputPointTvOS( "RightMotor" );
            m_xIOl_OutputPoints->AddObject( reinterpret_cast< IRefCount * >( pLeft ) );
            m_xIOl_OutputPoints->AddObject( reinterpret_cast< IRefCount * >( pRight ) );
        }

        iSetBufferTime( bufferTime, pollingRate );
        iVirtualTimeReMapped( virtualTime );
        
        rDebugPrintf( "radControllerTvOS: Created controller for pad index %d at '%s'\n", 
            padIndex, m_xIString_Location->GetChars() );
    }

    ~radControllerTvOS( void ) { }

    int m_PadIndex;
    ref< IRadObjectList > m_xIOl_InputPoints;
    ref< IRadObjectList > m_xIOl_OutputPoints;
    ref< IRadString > m_xIString_Location;
};

//============================================================================
// Component: radControllerSystemTvOS
//============================================================================

class radControllerSystemTvOS
    :
    public IRadControllerSystem,
    public IRadTimerCallback,
    public radRefCount
{
    public:

    IMPLEMENT_REFCOUNTED( "radControllerSystemTvOS" )

    virtual void OnTimerDone( unsigned int elapsedtime, void* pUserData )
    {
        // Pump native input
        TvOSInput_Pump();
        
        // Update controllers
        m_xIOl_Controllers->Reset( );
        IRadControllerTvOS * pIXbc2;
        while ((pIXbc2 = reinterpret_cast< IRadControllerTvOS * >( m_xIOl_Controllers->GetNext( ) )))
        {
            pIXbc2->iPoll( radTimeGetMilliseconds( ) + m_VirtualTimeAdjust );
        }

        // Check for connection changes
        CheckConnectionChanges();

        if ( m_UsingVirtualTime == false )
        {
            SetVirtualTime( radTimeGetMilliseconds( ) );
        }
    }

    void CheckConnectionChanges()
    {
        // Check each slot for connection changes
        for ( int i = 0; i < 4; i++ )
        {
            const TvOSPadState* state = TvOSInput_GetState( i );
            bool isConnected = (state != NULL && state->connected);
            
            if ( isConnected && !m_WasConnected[i] )
            {
                // Controller connected
                m_WasConnected[i] = true;
                
                // Create new controller if needed
                ref< IRadController > xIController2 = GetControllerForPadIndex( i );
                if ( xIController2 == NULL )
                {
                    CreateControllerForPadIndex( i );
                    xIController2 = GetControllerForPadIndex( i );
                }
                
                // Notify callbacks
                if ( xIController2 != NULL )
                {
                    NotifyConnectionChange( xIController2 );
                }
            }
            else if ( !isConnected && m_WasConnected[i] )
            {
                // Controller disconnected
                m_WasConnected[i] = false;
                
                ref< IRadController > xIController2 = GetControllerForPadIndex( i );
                if ( xIController2 != NULL )
                {
                    NotifyConnectionChange( xIController2 );
                }
            }
        }
    }

    void NotifyConnectionChange( IRadController* pController )
    {
        IRadWeakInterfaceWrapper * pIWir;
        m_xIOl_Callbacks->Reset( );
        while((pIWir = reinterpret_cast< IRadWeakInterfaceWrapper * >( m_xIOl_Callbacks->GetNext( ) )))
        {
            IRadControllerConnectionChangeCallback * pCallback = (IRadControllerConnectionChangeCallback *) pIWir->GetWeakInterface( );
            pCallback->OnControllerConnectionStatusChange( pController );
        }
    }

    IRadController* GetControllerForPadIndex( int index )
    {
        char location[32];
        sprintf( location, "Port%d\\Slot0", index );
        return GetControllerAtLocation( location );
    }

    void CreateControllerForPadIndex( int index )
    {
        unsigned int virtualTime = radTimeGetMilliseconds() + m_VirtualTimeAdjust;
        unsigned int pollingRate = m_xITimer ? m_xITimer->GetTimeout() : 10;

        ref< IRadController > xIController2 = new (g_ControllerSystemAllocator) radControllerTvOS(
            g_ControllerSystemAllocator,
            index,
            virtualTime,
            m_EventBufferTime,
            pollingRate
        );

        m_xIOl_Controllers->AddObject( xIController2 );
        
        rDebugPrintf( "radControllerSystemTvOS: Created controller for pad %d\n", index );
    }

    virtual unsigned int GetNumberOfControllers( void )
    {
        return m_xIOl_Controllers->GetSize( );
    }

    virtual IRadController * GetControllerByIndex( unsigned int myindex )
    {
        return reinterpret_cast< IRadControllerTvOS * >( m_xIOl_Controllers->GetAt( myindex ) );
    }

    virtual IRadController * GetControllerAtLocation( const char * pLocation )
    {
        rAssert( pLocation != NULL );
        m_xIOl_Controllers->Reset( );
        IRadController * pIC2;
        while ((pIC2 = reinterpret_cast< IRadControllerTvOS * >( m_xIOl_Controllers->GetNext( ) )))
        {
            if ( strcmp( pIC2->GetLocation(), pLocation ) == 0 )
            {
                return pIC2;
            }
        }
        return NULL;
    }

    virtual void SetBufferTime( unsigned int milliseconds )
    {
        if ( milliseconds == 0 )
        {
            m_UsingVirtualTime = false;
            MapVirtualTime( 0, 0 );
            milliseconds = 10;
        }
        else
        {
            m_UsingVirtualTime = true;
        }
        
        unsigned int pollingRate = m_xITimer->GetTimeout( );
        m_EventBufferTime = milliseconds;

        m_xIOl_Controllers->Reset( );
        IRadControllerTvOS * pIDipc2;
        while ((pIDipc2 = reinterpret_cast< IRadControllerTvOS * >( m_xIOl_Controllers->GetNext( ) )))
        {
            pIDipc2->iSetBufferTime( milliseconds, pollingRate );
        }
    }

    virtual void MapVirtualTime( unsigned int systemTicks, unsigned int virtualTicks )
    {
        m_VirtualTimeAdjust = virtualTicks - systemTicks;

        m_xIOl_Controllers->Reset( );
        IRadControllerTvOS * pIXbc2;
        while ((pIXbc2 = reinterpret_cast< IRadControllerTvOS * >( m_xIOl_Controllers->GetNext( ) )))
        {
            pIXbc2->iVirtualTimeReMapped( radTimeGetMilliseconds() + m_VirtualTimeAdjust );
        }       
    }

    virtual void SetVirtualTime( unsigned int virtualTime )
    {
        m_xIOl_Controllers->Reset( );
        IRadControllerTvOS * pIXbc2;
        while ((pIXbc2 = reinterpret_cast< IRadControllerTvOS * >( m_xIOl_Controllers->GetNext( ) )))
        {
            pIXbc2->iVirtualTimeChanged( virtualTime );
        }
    }

    virtual void SetCaptureRate( unsigned int ms )
    {
        m_xITimer->SetTimeout( ms );
        SetBufferTime( m_EventBufferTime );
    }

    virtual void RegisterConnectionChangeCallback( IRadControllerConnectionChangeCallback * pCallback )
    {
        rAssert( pCallback != NULL );
        ref< IRadWeakInterfaceWrapper > xIWir;
        ::radWeakInterfaceWrapperCreate( & xIWir, g_ControllerSystemAllocator );
        xIWir->SetWeakInterface( pCallback );
        m_xIOl_Callbacks->AddObject( xIWir );
    }

    virtual void UnRegisterConnectionChangeCallback( IRadControllerConnectionChangeCallback * pCallback )
    {
        rAssert( pCallback != NULL );
        IRadWeakInterfaceWrapper * pIWir;
        m_xIOl_Callbacks->Reset( );
        while ((pIWir = reinterpret_cast< IRadWeakInterfaceWrapper * >( m_xIOl_Callbacks->GetNext( ) )))
        {
            if ( pIWir->GetWeakInterface() == pCallback )
            {
                m_xIOl_Callbacks->RemoveObject( pIWir );
                return;
            }
        }
        rAssertMsg( false, "Controller connection change callback not registered." );            
    }

    void Service( void )
    {
        m_xITimerList->Service( );   
    }

    radControllerSystemTvOS( IRadControllerConnectionChangeCallback* pConnectionChangeCallback, radMemoryAllocator thisAllocator )
        :
        m_UsingVirtualTime( false ),
        m_VirtualTimeAdjust( 0 ),
        m_EventBufferTime( 0 ),
        m_DefaultConnectionChangeCallback( NULL )
    {
        radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "radControllerSystemTvOS" );

        rAssert( s_pTheTvOSControllerSystem == NULL );
        s_pTheTvOSControllerSystem = this;
        
        g_ControllerSystemAllocator = thisAllocator;

        // Initialize native tvOS input
        TvOSInput_Init();

        radTimeCreateList( &m_xITimerList, 1, g_ControllerSystemAllocator );
        m_xITimerList->CreateTimer( & m_xITimer, 10, this );

        ::radObjectListCreate( & m_xIOl_Controllers, g_ControllerSystemAllocator );
        rAssert( m_xIOl_Controllers != NULL );

        ::radObjectListCreate( & m_xIOl_Callbacks, g_ControllerSystemAllocator );
        rAssert( m_xIOl_Callbacks != NULL );

        m_DefaultConnectionChangeCallback = pConnectionChangeCallback;
        if( pConnectionChangeCallback )
        {
            RegisterConnectionChangeCallback( pConnectionChangeCallback );
        }

        // Initialize connection state
        for ( int i = 0; i < 4; i++ )
        {
            m_WasConnected[i] = false;
        }

        // Create controllers for any already-connected pads
        int padCount = TvOSInput_GetPadCount();
        rDebugPrintf( "radControllerSystemTvOS: Found %d connected controller(s)\n", padCount );
        
        for ( int i = 0; i < 4; i++ )
        {
            const TvOSPadState* state = TvOSInput_GetState( i );
            if ( state != NULL && state->connected )
            {
                m_WasConnected[i] = true;
                CreateControllerForPadIndex( i );
                
                // Notify about initial connection
                IRadController* pController = GetControllerForPadIndex( i );
                if ( pController != NULL )
                {
                    NotifyConnectionChange( pController );
                }
            }
        }

        SetCaptureRate( 10 );
        MapVirtualTime( 0, 0 );
        SetBufferTime( 0 );
        
        rDebugPrintf( "radControllerSystemTvOS: Initialization complete\n" );
    }
        
    ~radControllerSystemTvOS( void )
    {
        if( m_DefaultConnectionChangeCallback != NULL )
        {
            UnRegisterConnectionChangeCallback( m_DefaultConnectionChangeCallback );
            m_DefaultConnectionChangeCallback = NULL;
        }

        rAssertMsg( m_xIOl_Callbacks->GetSize() == 0, "Somebody forgot to unregister a controller connection change callback" );

        TvOSInput_Shutdown();

        g_ControllerSystemAllocator = RADMEMORY_ALLOC_DEFAULT;

        rAssert( s_pTheTvOSControllerSystem == this );
        s_pTheTvOSControllerSystem = NULL;
    }

    bool m_UsingVirtualTime;
    unsigned int m_VirtualTimeAdjust;
    unsigned int m_EventBufferTime;
    bool m_WasConnected[4];

    IRadControllerConnectionChangeCallback* m_DefaultConnectionChangeCallback;

    ref< IRadObjectList > m_xIOl_Callbacks;
    ref< IRadObjectList > m_xIOl_Controllers;
    ref< IRadTimer > m_xITimer;
    ref< IRadTimerList > m_xITimerList;
};

//============================================================================
// Public API
//============================================================================

void radControllerInitialize( IRadControllerConnectionChangeCallback* pConnectionChangeCallback, radMemoryAllocator alloc )
{
    rAssert( s_pTheTvOSControllerSystem == NULL );
    new ( alloc ) radControllerSystemTvOS( pConnectionChangeCallback, alloc );
}

void radControllerTerminate( void )
{
    radRelease( s_pTheTvOSControllerSystem, NULL );
}

IRadControllerSystem* radControllerSystemGet( void )
{
    rAssert( s_pTheTvOSControllerSystem != NULL );
    return( s_pTheTvOSControllerSystem );
}

void radControllerSystemService( void )
{
    if( s_pTheTvOSControllerSystem != NULL )
    {
        s_pTheTvOSControllerSystem->Service( );
    }
}

#endif // RAD_TVOS
