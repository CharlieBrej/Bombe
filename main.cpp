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
#include "Compress.h"

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

class SteamGameManager
{
private:
	STEAM_CALLBACK( SteamGameManager, OnUserStatsReceived, UserStatsReceived_t, m_CallbackUserStatsReceived);
	STEAM_CALLBACK( SteamGameManager, OnGameOverlayActivated, GameOverlayActivated_t, m_CallbackGameOverlayActivated );
    STEAM_CALLBACK( SteamGameManager, OnGetTicketForWebApiResponse, GetTicketForWebApiResponse_t, m_OnGetTicketForWebApiResponse);
    ISteamUserStats *m_pSteamUserStats;
    bool stats_ready = false;
    std::set <std::string> achievements;
    bool needs_send = false;
    bool steam_session_ready = false;
    std::string steam_session_string;

public:
    SteamGameManager():
    	m_CallbackUserStatsReceived( this, &SteamGameManager::OnUserStatsReceived ),
	    m_CallbackGameOverlayActivated( this, &SteamGameManager::OnGameOverlayActivated ),
	    m_OnGetTicketForWebApiResponse( this, &SteamGameManager::OnGetTicketForWebApiResponse )
    {
        SteamUserStats()->RequestCurrentStats();
        HAuthTicket handle = SteamUser()->GetAuthTicketForWebApi(NULL);
    };
    void set_achievements(std::string name)
    {
        if (!stats_ready || !steam_session_ready)
            return;
        if (achievements.count(name))
            return;
        achievements.insert(name);
        m_pSteamUserStats->SetAchievement(name.c_str());
        needs_send = true;
    }
    void update_achievements(GameState* game_state);
    void get_new_ticket();
};

void SteamGameManager::OnUserStatsReceived( UserStatsReceived_t *pCallback )
{
    needs_send = false;
    for (const std::string& name : achievements)
    {
        bool rep;
        m_pSteamUserStats->GetAchievement(name.c_str(), &rep);
        if (!rep)
            achievements.erase(name);
    }
    stats_ready = true;
}

void SteamGameManager::OnGameOverlayActivated( GameOverlayActivated_t* pCallback )
{
}

void SteamGameManager::OnGetTicketForWebApiResponse( GetTicketForWebApiResponse_t* pCallback )
{
    // auth_buffer = 
    // auth_buffer_size = 

    const char* lut = "0123456789ABCDEF";
    steam_session_string.clear();
    for (int i = 0; i < pCallback->m_cubTicket; i++)
    {
        uint8_t c = pCallback->m_rgubTicket[i];
        steam_session_string += lut[c >> 4];
        steam_session_string += lut[c & 0xF];
    }
    steam_session_ready = true;
}

void SteamGameManager::update_achievements(GameState* game_state)
{
    for (const std::string& name : game_state->achievements)
    {
        set_achievements(name);
    }

    if (needs_send)
    {
        m_pSteamUserStats->StoreStats();
        needs_send = false;
    }
    if (steam_session_ready && game_state->steam_session_string.empty())
    {
        game_state->steam_session_string = steam_session_string;
        game_state->fetch_scores();
    }
}

void SteamGameManager::get_new_ticket()
{
    if (steam_session_ready)
    {
		HAuthTicket handle = SteamUser()->GetAuthTicketForWebApi("brej.org");
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
        std::ifstream loadfile(std::filesystem::path((char8_t*)save_filename.c_str()), std::ios::binary);
#else
        std::ifstream loadfile(save_filename.c_str());
#endif
        std::stringstream str_stream;
        str_stream << loadfile.rdbuf();
        std::string str = str_stream.str();
        bool json = true;
        try
        {
            str = decompress_string(str);
            json = false;
        }
        catch (const std::runtime_error& error)
        {
            std::cerr << error.what() << "\n";
            try
            {
                while (str.find ("\r\n") != std::string::npos)
                {
                    str.erase(str.find ("\r\n"), 1);
                }
                str = decompress_string(str);
                json = false;
            }
            catch (const std::runtime_error& error)
            {
                std::cerr << error.what() << "\n";
            }

        }

        game_state = new GameState(str, json);
    }
#ifdef STEAM
    SteamGameManager steam_manager;
    game_state->steam_username = SteamFriends()->GetPersonaName();
    game_state->steam_id = SteamUser()->GetSteamID().CSteamID::ConvertToUint64();

    int friend_count = SteamFriends()->GetFriendCount( k_EFriendFlagImmediate );
    for (int i = 0; i < friend_count; ++i)
    {
	    CSteamID friend_id = SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate);
        game_state->steam_friends.insert(friend_id.ConvertToUint64());
    }
#else
    game_state->steam_session_string = "dummy";
    game_state->steam_id = SECRET_ID;
#endif
    {
        std::ifstream ifs("version");
        game_state->version_text = std::string((std::istreambuf_iterator<char>(ifs)),
                                               (std::istreambuf_iterator<char>()));
    }

    int save_time = 0;
    unsigned oldtime = SDL_GetTicks();
	while(true)
	{
#ifdef STEAM
        if (game_state->pirate)
            steam_manager.get_new_ticket();
#endif

		if (game_state->events())
            break;
        game_state->audio();
#ifdef STEAM
        steam_manager.update_achievements(game_state);
        SteamAPI_RunCallbacks();
#endif
        if (save_time < 0)
        {
            game_state->render(true);
            game_state->fetch_scores();
            SaveObject* omap = game_state->save();
            std::string my_save_filename = save_filename + std::to_string(save_index);
            save_index = (save_index + 1) % 10;

#ifdef _WIN32
            std::ofstream outfile1 (std::filesystem::path((char8_t*)save_filename.c_str()), std::ios::binary);
            std::ofstream outfile2 (std::filesystem::path((char8_t*)my_save_filename.c_str()), std::ios::binary);
#else
            std::ofstream outfile1 (save_filename.c_str());
            std::ofstream outfile2 (my_save_filename.c_str());
#endif
            std::string out_data = compress_string(omap->to_string());
            outfile1 << out_data;
            outfile2 << out_data;
            delete omap;
            save_time = 1000 * 60;
        }
        else
        {
            game_state->render();
        }

        unsigned newtime = SDL_GetTicks();
        unsigned diff = newtime - oldtime;
        if (diff > 1000)
            diff = 1000;
        if (diff < 10)
        {
            SDL_Delay(10 - diff);;
            newtime = SDL_GetTicks();
            diff = newtime - oldtime;
        }
        save_time -= diff;
        game_state->advance(diff);
        oldtime = newtime;
	}
    SDL_HideWindow(game_state->sdl_window);
    {
        SaveObject* omap = game_state->save();
#ifdef _WIN32
        std::ofstream outfile1 (std::filesystem::path((char8_t*)save_filename.c_str()), std::ios::binary);
#else
        std::ofstream outfile1 (save_filename.c_str());
#endif
        std::string out_data = compress_string(omap->to_string());
        outfile1 << out_data;
        delete omap;
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
    {
       std::ifstream ifile;
       ifile.open("PLAYTEST");
       IS_PLAYTEST = bool(ifile);
    }

#ifdef STEAM
    int game_id = IS_DEMO ? 2263470 : IS_PLAYTEST ? 2263480 : 2262930;
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
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
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
