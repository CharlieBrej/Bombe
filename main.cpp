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


int main2( int argc, char* argv[] )
{
    Grid g(XYPos(6,6));
    g.print();

    g.make_harder();
    g.print();

    g.solve(100);
    g.print();
	return 0;
}



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


	return 0;
}
