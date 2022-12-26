#include <SDL.h>
#include <SDL_mixer.h>
#include <SDL_image.h>
#include <SDL_net.h>
#include <SDL_ttf.h>

#include <assert.h>
#include <iostream>
#include <fstream>

#include "Grid.h"
#include "GameState.h"

#ifdef _WIN32
    #include <filesystem>
#endif

#ifndef NO_STEAM
#define STEAM
#endif

#ifdef STEAM
#include "steam/steam_api.h"
#endif
#ifdef STEAM

static const char* const achievement_names[] = {NULL};

class SteamGameManager
{
private:
	STEAM_CALLBACK( SteamGameManager, OnUserStatsReceived, UserStatsReceived_t, m_CallbackUserStatsReceived);
	STEAM_CALLBACK( SteamGameManager, OnGameOverlayActivated, GameOverlayActivated_t, m_CallbackGameOverlayActivated );
    STEAM_CALLBACK( SteamGameManager, OnGetAuthSessionTicketResponse, GetAuthSessionTicketResponse_t, m_OnGetAuthSessionTicketResponse);
    ISteamUserStats *m_pSteamUserStats;
    bool stats_ready = false;
    bool achievement_got[10] = {false};
    bool needs_send = false;
    bool auth_buffer_ready = false;
    uint32 auth_buffer_size;
    uint8 auth_buffer[1024];

public:
    SteamGameManager():
    	m_CallbackUserStatsReceived( this, &SteamGameManager::OnUserStatsReceived ),
	    m_CallbackGameOverlayActivated( this, &SteamGameManager::OnGameOverlayActivated ),
	    m_OnGetAuthSessionTicketResponse( this, &SteamGameManager::OnGetAuthSessionTicketResponse )
    {
        m_pSteamUserStats = SteamUserStats();
        m_pSteamUserStats->RequestCurrentStats();


	{
		HAuthTicket handle = SteamUser()->GetAuthSessionTicket(auth_buffer, 1024, &auth_buffer_size);
	}


    };
    void set_achievements(unsigned index)
    {
        if (achievement_got[index])
            return;
        achievement_got[index] = true;
        m_pSteamUserStats->SetAchievement(achievement_names[index]);
        needs_send = true;
    }
    void update_achievements(GameState* game_state);
};

void SteamGameManager::OnUserStatsReceived( UserStatsReceived_t *pCallback )
{
    stats_ready = true;
    for (int i = 0; achievement_names[i]; i++)
        m_pSteamUserStats->GetAchievement( achievement_names[i], &achievement_got[i]);
}

void SteamGameManager::OnGameOverlayActivated( GameOverlayActivated_t* pCallback )
{
}

void SteamGameManager::OnGetAuthSessionTicketResponse( GetAuthSessionTicketResponse_t* pCallback )
{
    auth_buffer_ready = true;
}

void SteamGameManager::update_achievements(GameState* game_state)
{
    for (int i = 0; i < 10; i++)
    {
        if (game_state->achievement[i])
            set_achievements(i);
    }

    if (needs_send)
    {
        m_pSteamUserStats->StoreStats();
        needs_send = false;
    }
    if (auth_buffer_ready && game_state->steam_session_string.empty())
    {
        const char* lut = "0123456789ABCDEF";
        std::string str;
        for (int i = 0; i < auth_buffer_size; i++)
        {
            uint8_t c = auth_buffer[i];
            str += lut[c >> 4];
            str += lut[c & 0xF];
        }
        game_state->steam_session_string = str;
    }
}

#endif


void mainloop()
{
    int save_index = 0;
    char* save_path = SDL_GetPrefPath("CharlieBrej", "Bombe");
    std::string save_filename = std::string(save_path) + "bombe.save";

    SDL_free(save_path);

    GameState* game_state;
    {
#ifdef _WIN32
        std::ifstream loadfile(std::filesystem::path((char8_t*)save_filename.c_str()));
#else
        std::ifstream loadfile(save_filename.c_str());
#endif
        game_state = new GameState(loadfile);
    }
#ifdef STEAM
    SteamGameManager steam_manager;
//    game_state->set_steam_user(SteamUser()->GetSteamID().CSteamID::ConvertToUint64(), SteamFriends()->GetPersonaName());

    int friend_count = SteamFriends()->GetFriendCount( k_EFriendFlagImmediate );
    for (int i = 0; i < friend_count; ++i)
    {
	    CSteamID friend_id = SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate);
        // game_state->add_friend(friend_id.ConvertToUint64());
    }
#else
    game_state->steam_session_string = "dummy";
#endif

    int frame = 0;
    SDL_Thread *save_thread = NULL;

	while(true)
	{
        unsigned oldtime = SDL_GetTicks();
		if (game_state->events())
            break;
        game_state->advance();
        game_state->audio();
#ifdef STEAM
        steam_manager.update_achievements(game_state);
        SteamAPI_RunCallbacks();
#endif
        frame++;
        if (frame > 100 * 60)
        {
            game_state->render(true);
            SaveObject* omap = game_state->save();
            std::string my_save_filename = save_filename + std::to_string(save_index);

#ifdef _WIN32
            std::ofstream outfile1 (std::filesystem::path((char8_t*)save_filename.c_str()));
            std::ofstream outfile2 (std::filesystem::path((char8_t*)my_save_filename.c_str()));
#else
            std::ofstream outfile1 (save_filename.c_str());
            std::ofstream outfile2 (my_save_filename.c_str());
#endif
            omap->save(outfile1);
            omap->save(outfile2);
            frame = 0;
        }
        else
        {
            game_state->render();
        }

        unsigned newtime = SDL_GetTicks();
        if ((newtime - oldtime) < 5)
        {
            SDL_Delay(5 - (newtime - oldtime));
        }
	}
    SDL_HideWindow(game_state->sdl_window);
    {
        SaveObject* omap = game_state->save();
        std::string my_save_filename = save_filename + std::to_string(save_index);
#ifdef _WIN32
        std::ofstream outfile1 (std::filesystem::path((char8_t*)save_filename.c_str()));
#else
        std::ofstream outfile1 (save_filename.c_str());
#endif
        omap->save(outfile1);
        frame = 0;
    }
    delete game_state;
}


int main( int argc, char* argv[] )
{
    {
       std::ifstream ifile;
       ifile.open("FULL");
       IS_DEMO = !ifile;
    }

    int game_id = IS_DEMO ? 2263470 : 2262930;
#ifdef STEAM
	if (SteamAPI_RestartAppIfNecessary(game_id))
		return 1;
	if (!SteamAPI_Init())
		return 1;
#endif

    SDL_Init(SDL_INIT_VIDEO| SDL_INIT_AUDIO);
    IMG_Init(IMG_INIT_PNG);
    SDLNet_Init();
    TTF_Init();
    Mix_Init(0);

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_SetHint("SDL_MOUSE_AUTO_CAPTURE", "0");

    mainloop();

    Mix_Quit();
    TTF_Quit();
    SDLNet_Quit();
	IMG_Quit();
	SDL_Quit();

    #ifdef STEAM
        SteamAPI_Shutdown();
    #endif


	return 0;
}
