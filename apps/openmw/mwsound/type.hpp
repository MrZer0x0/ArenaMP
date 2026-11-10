#ifndef GAME_SOUND_TYPE_H
#define GAME_SOUND_TYPE_H

namespace MWSound
{
    enum class Type
    {
        Sfx       = 1 << 4, /* Normal SFX sound */
        Voice     = 1 << 5, /* NPC/dialogue voice sound */
        Foot      = 1 << 6, /* Footstep sound */
        Music     = 1 << 7, /* Music track */
        Movie     = 1 << 8, /* Movie audio track */
        VoiceChat = 1 << 9, /* ArenaMP realtime player voice */
        Mask      = Sfx | Voice | Foot | Music | Movie | VoiceChat
    };
}

#endif
