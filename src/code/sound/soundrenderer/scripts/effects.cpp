#include <sound/soundrenderer/soundrenderingmanager.h>

namespace Sound {

#pragma GCC push_options
#pragma GCC optimize("O1")
void daSoundRenderingManager::RunSoundEffectScripts( void )
{
    SetCurrentNameSpace( GetSoundNamespace() );
    GetSoundManager()->GetSoundLoader()->SetCurrentCluster( SC_FRONTEND );
    #include "frontend.inl"
    GetSoundManager()->GetSoundLoader()->SetCurrentCluster( SC_INGAME );
    #include "collide.inl"
    #include "carsound.inl"
    #include "world.inl"
    #include "positionalsounds.inl"
}
#pragma GCC pop_options

}
