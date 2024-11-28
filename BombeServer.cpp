#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <list>
#include <stdexcept>
#include <signal.h>
#include <codecvt>
#include <locale>
#include <time.h>
#include <set>
#include <curl/curl.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "Compress.h"
#include "SaveState.h"

bool power_down = false;
const int LEVEL_TYPES = 6;
const int GAME_MODE_TYPES = 5;

typedef int64_t Score;

struct LevelStats
{
    uint32_t completed = 0;
    uint32_t total = 0;
};

class ScoreTable
{
public:
    std::multimap<Score, uint64_t> sorted_scores;
    std::map<uint64_t, Score> user_score;
    LevelStats stats[30][200];
    
    ~ScoreTable()
    {
   
    }
    SaveObject* save()
    {
        SaveObjectMap* obj = new SaveObjectMap;
        SaveObjectList* score_list = new SaveObjectList;
        for(auto const &score : sorted_scores)
        {
            SaveObjectMap* score_map = new SaveObjectMap;
            score_map->add_num("id", score.second);
            score_map->add_num("score", user_score[score.second]);
            score_list->add_item(score_map);
        }
        obj->add_item("scores", score_list);

        SaveObjectList* stats_list = new SaveObjectList;
        for (int i=0; i<30; i++)
        {
            SaveObjectList* sub_stats_list = new SaveObjectList;
            for (int j = 0; j < 200; j++)
            {
                SaveObjectList* stat = new SaveObjectList;
                SaveObjectNumber* comp = new SaveObjectNumber(stats[i][j].completed);
                SaveObjectNumber* total = new SaveObjectNumber(stats[i][j].total);
                stat->add_item(comp);
                stat->add_item(total);
                sub_stats_list->add_item(stat);
            }
            stats_list->add_item(sub_stats_list);
        }
        obj->add_item("stats", stats_list);

        return obj;
    }
    void load(SaveObject* sobj)
    {
        SaveObjectMap* smap = sobj->get_map();

        SaveObjectList* score_list = smap->get_item("scores")->get_list();
        for (unsigned i = 0; i < score_list->get_count(); i++)
        {
            SaveObjectMap* omap = score_list->get_item(i)->get_map();
            add_score(omap->get_num("id"), omap->get_num("score"));
        }

        SaveObjectList* stats_list = smap->get_item("stats")->get_list();
        for (int i=0; i<30; i++)
        {
            SaveObjectList* sub_stats_list = stats_list->get_item(i)->get_list();
            for (int j = 0; j < 200; j++)
            {
                SaveObjectList* stat = sub_stats_list->get_item(j)->get_list();
                stats[i][j].completed = stat->get_item(0)->get_num();
                stats[i][j].total = stat->get_item(1)->get_num();
                if (stats[i][j].completed < 120)
                    stats[i][j].completed = 0;
            }
        }
    }

    void add_score(uint64_t steam_id, Score score, bool force = false)
    {
        if (!force && (user_score.count(steam_id)) && (score <= user_score[steam_id]))
            return;
        if (!steam_id)
            return;

        for (auto it = sorted_scores.begin(); it != sorted_scores.end(); )
        {
           if (it->second == steam_id)
                it = sorted_scores.erase(it);
           else
               ++it;
        }
        user_score.erase(steam_id);
        if (!score)
            return;
        sorted_scores.insert({-score, steam_id});
        user_score[steam_id] = score;
    } 
    void reset()
    {
        sorted_scores.clear();
        user_score.clear();
        for (int i = 0; i < 30; i++)
        for (int j = 0; j < 200; j++)
            stats[i][j] = {0,0};
    }


};

    // (hex/sqr/tri)(x)(y)(wrap)(merged)(rows)(+-)(x_y)(x_y3)(x_y_z)(exc)(parity)(xor1)(xor11)(prime)(neg)
    //  0            1  2  3     4       5    6    7       8    9     10    11    12     13     14     15

static const char* server_level_types[] = { "A6500000700000700", "B5500000700000700","C8500000700000700","A8710000700000700", "B7710000700000700",
                                            "A6500000070000700", "B5500000070000700","C8500000070000700","A8710000070000700", "B7710000070000700",
                                            "A6500000007000700", "B5500000007000700","C8500000007000700","A8710000007000700", "B7710000007000700",
                                            "A6500000000700700", "B5500000000700700","C8500000000700700","A8710000000700700", "B7710000000700700",
                                            "A6500000000070700", "B5500000000070700","C8500000000070700","A8710000000070700", "B7710000000070700",
                                            "A6500000000007700", "B5500000000007700","C8500000000007700","A8710000000007700", "B7710000000007700",
                                            NULL};
static const char* neg_server_level_types[] = { "A6500000700000730", "B5500000700000730","C8500000700000730","A8710000700000730", "B7710000700000730",
                                                "A6500000070000730", "B5500000070000730","C8500000070000730","A8710000070000730", "B7710000070000730",
                                                "A6500000007000730", "B5500000007000730","C8500000007000730","A8710000007000730", "B7710000007000730",
                                                "A6500000000700730", "B5500000000700730","C8500000000700730","A8710000000700730", "B7710000000700730",
                                                "A6500000000070730", "B5500000000070730","C8500000000070730","A8710000000070730", "B7710000000070730",
                                                "A6500000000007730", "B5500000000007730","C8500000000007730","A8710000000007730", "B7710000000007730",
                                            NULL};
static const int game_version = 14;

class Player
{
public:
    std::string name;
    uint64_t weekly_scores[GAME_MODE_TYPES][10] = {};
    Player(std::string _name) :
        name(_name)
    {}
    Player()
    {}
};


class Database
{
public:
    std::map<uint64_t, Player> players;
    ScoreTable scores[GAME_MODE_TYPES][LEVEL_TYPES];
    std::map<std::string, uint64_t> steam_sessions;
    std::vector<std::vector<std::string>> server_levels;
    std::vector<std::vector<std::string>> next_server_levels;
    std::vector<std::vector<std::string>> neg_server_levels;
    std::vector<std::vector<std::string>> next_neg_server_levels;
    int server_levels_version = 0;



    void update_name(uint64_t steam_id, std::string& steam_username)
    {
        players[steam_id].name = steam_username;
    }

    void load(SaveObject* sobj)
    {
        SaveObjectMap* omap = sobj->get_map();
        SaveObjectList* player_list = omap->get_item("players")->get_list();

        unsigned load_game_version = 0;
        if (omap->has_key("game_version"))
            load_game_version = omap->get_num("game_version");

        for (unsigned i = 0; i < player_list->get_count(); i++)
        {
            SaveObjectMap* omap = player_list->get_item(i)->get_map();
            std::string name;
            uint64_t id = omap->get_num("id");
            omap->get_string("steam_username", name);
            update_name(id, name);

            if (omap->has_key("scores"))
            {
                SaveObjectList* score_list2 = omap->get_item("scores")->get_list();
                for (unsigned j = 0; j < GAME_MODE_TYPES && (j < score_list2->get_count()); j++)
                {
                    SaveObjectList* score_list = score_list2->get_item(j)->get_list();
                    for (unsigned i = 0; (i < 10) && (i < score_list->get_count()); i++)
                        players[id].weekly_scores[j][i] = score_list->get_num(i);
                }
            }
        }
//        if (load_game_version == game_version)
        {
            SaveObjectList* score_list2 = omap->get_item("scores")->get_list();
            for (unsigned j = 0; j < GAME_MODE_TYPES && (j < score_list2->get_count()); j++)
            {
                SaveObjectList* score_list = score_list2->get_item(j)->get_list();
                for (unsigned i = 0; (i < LEVEL_TYPES) && (i < score_list->get_count()); i++)
                {
                    scores[j][i].load(score_list->get_item(i));
                }
            }
        }

        SaveObjectList* lvl_sets = omap->get_item("server_levels")->get_list();
        server_levels.resize(lvl_sets->get_count());
        for (unsigned k = 0; k < lvl_sets->get_count(); k++)
        {
            SaveObjectList* plist = lvl_sets->get_item(k)->get_list();
            server_levels[k].clear();
            for (unsigned i = 0; i < plist->get_count(); i++)
            {
                std::string s = plist->get_string(i);
                server_levels[k].push_back(s);
            }
        }
        lvl_sets = omap->get_item("next_server_levels")->get_list();
        next_server_levels.resize(lvl_sets->get_count());
        for (unsigned k = 0; k < lvl_sets->get_count(); k++)
        {
            SaveObjectList* plist = lvl_sets->get_item(k)->get_list();
            next_server_levels[k].clear();
            for (unsigned i = 0; i < plist->get_count(); i++)
            {
                std::string s = plist->get_string(i);
                next_server_levels[k].push_back(s);
            }
        }

        server_levels_version = omap->get_num("server_levels_version");

        if (omap->has_key("neg_server_levels"))
        {
            lvl_sets = omap->get_item("neg_server_levels")->get_list();
            neg_server_levels.resize(lvl_sets->get_count());
            for (unsigned k = 0; k < lvl_sets->get_count(); k++)
            {
                SaveObjectList* plist = lvl_sets->get_item(k)->get_list();
                neg_server_levels[k].clear();
                for (unsigned i = 0; i < plist->get_count(); i++)
                {
                    std::string s = plist->get_string(i);
                    neg_server_levels[k].push_back(s);
                }
            }
            lvl_sets = omap->get_item("next_neg_server_levels")->get_list();
            next_neg_server_levels.resize(lvl_sets->get_count());
            for (unsigned k = 0; k < lvl_sets->get_count(); k++)
            {
                SaveObjectList* plist = lvl_sets->get_item(k)->get_list();
                next_neg_server_levels[k].clear();
                for (unsigned i = 0; i < plist->get_count(); i++)
                {
                    std::string s = plist->get_string(i);
                    next_neg_server_levels[k].push_back(s);
                }
            }
        }



    }

    SaveObject* save(bool lite)
    {
        SaveObjectMap* omap = new SaveObjectMap;
        
        SaveObjectList* player_list = new SaveObjectList;
        
        for(auto &player_pair : players)
        {
            SaveObjectMap* player_map = new SaveObjectMap;
            player_map->add_num("id", player_pair.first);
            player_map->add_string("steam_username", player_pair.second.name);
            {
                SaveObjectList* score_list2 = new SaveObjectList;
                for (unsigned j = 0; j < GAME_MODE_TYPES; j++)
                {
                    SaveObjectList* score_list = new SaveObjectList;
                    for (unsigned i = 0; i < 10; i++)
                        score_list->add_num(player_pair.second.weekly_scores[j][i]);
                    score_list2->add_item(score_list);
                }
                player_map->add_item("scores", score_list2);
            }

            player_list->add_item(player_map);
        }
        omap->add_item("players", player_list);

        SaveObjectList* score_list2 = new SaveObjectList;
        for (int j = 0; j < GAME_MODE_TYPES; j++)
        {
            SaveObjectList* score_list = new SaveObjectList;
            for (int i = 0; i < LEVEL_TYPES; i++)
            {
                score_list->add_item(scores[j][i].save());
            }
            score_list2->add_item(score_list);
        }
        omap->add_item("scores", score_list2);
        
        SaveObjectList* sl_list = new SaveObjectList;
        for (std::vector<std::string>& lvl_set : server_levels)
        {
            SaveObjectList* ssl_list = new SaveObjectList;
            for (std::string& lvl : lvl_set)
            {
                ssl_list->add_string(lvl);
            }
            sl_list->add_item(ssl_list);
        }
        omap->add_item("server_levels", sl_list);

        sl_list = new SaveObjectList;
        for (std::vector<std::string>& lvl_set : next_server_levels)
        {
            SaveObjectList* ssl_list = new SaveObjectList;
            for (std::string& lvl : lvl_set)
            {
                ssl_list->add_string(lvl);
            }
            sl_list->add_item(ssl_list);
        }
        omap->add_item("next_server_levels", sl_list);

        sl_list = new SaveObjectList;
        for (std::vector<std::string>& lvl_set : neg_server_levels)
        {
            SaveObjectList* ssl_list = new SaveObjectList;
            for (std::string& lvl : lvl_set)
            {
                ssl_list->add_string(lvl);
            }
            sl_list->add_item(ssl_list);
        }
        omap->add_item("neg_server_levels", sl_list);

        sl_list = new SaveObjectList;
        for (std::vector<std::string>& lvl_set : next_neg_server_levels)
        {
            SaveObjectList* ssl_list = new SaveObjectList;
            for (std::string& lvl : lvl_set)
            {
                ssl_list->add_string(lvl);
            }
            sl_list->add_item(ssl_list);
        }
        omap->add_item("next_neg_server_levels", sl_list);
        omap->add_num("server_levels_version", server_levels_version);
        omap->add_num("game_version", game_version);

        return omap;
    }

    void add_server_level(std::string req, std::string resp)
    {
        for (int i = 0;  server_level_types[i]; i++)
        {
            if (req == server_level_types[i] && next_server_levels[i].size() < 200)
            {
                for (std::string& s : next_server_levels[i])
                {
                    if (s == resp)
                        return;
                }
                printf("got server level\n");
                next_server_levels[i].push_back(resp);
                return;
            }
        }
        for (int i = 0;  neg_server_level_types[i]; i++)
        {
            if (req == neg_server_level_types[i] && next_neg_server_levels[i].size() < 200)
            {
                for (std::string& s : next_neg_server_levels[i])
                {
                    if (s == resp)
                        return;
                }
                printf("got neg server level\n");
                next_neg_server_levels[i].push_back(resp);
                return;
            }
        }
    }

    SaveObjectMap* get_scores(int mode, uint64_t user_id, std::set<uint64_t> friends)
    {
        SaveObjectMap* resp = new SaveObjectMap();
        SaveObjectList* top_list = new SaveObjectList();
        for (int i = 0; i < LEVEL_TYPES; i++)
        {
            SaveObjectList* score_list = new SaveObjectList();
            int pos = 0;
            for (std::multimap<Score, uint64_t>::iterator rit = scores[mode][i].sorted_scores.begin(); rit != scores[mode][i].sorted_scores.end(); rit++)
            {
                pos++;
                int fr = friends.count(rit->second);
                if (rit->second == user_id)
                    fr = 2;
                if (pos > 100 && fr == 0)
                    continue;
                if (scores[mode][i].user_score[rit->second] == 0 && fr == 0)
                    continue;
                // if ((prevpos + 1) < pos)
                // {
                //     SaveObjectMap* score_map = new SaveObjectMap();
                //     score_map->add_num("pos", 0);
                //     score_map->add_string("name", "");
                //     score_map->add_num("score", 0);
                //     score_map->add_num("hidden", 1);
                //     score_list->add_item(score_map);
                // }
                SaveObjectMap* score_map = new SaveObjectMap();
                score_map->add_num("pos", pos);
                score_map->add_string("name", players[rit->second].name);
                score_map->add_num("score", scores[mode][i].user_score[rit->second]);
                if (fr)
                    score_map->add_num("friend", fr);
                score_list->add_item(score_map);
            }
            top_list->add_item(score_list);
        }
        resp->add_item("scores", top_list);

        SaveObjectList* top_slist = new SaveObjectList;
        for (int l = 0; l < LEVEL_TYPES - 1; l++)
        {
            SaveObjectList* stats_list = new SaveObjectList;
            for (int i = 0; i < 30; i++)
            {
                SaveObjectList* sub_stats_list = new SaveObjectList;
                for (int j = 0; j < 200; j++)
                {
                    uint64_t t = 0;
                    if (scores[mode][l].stats[i][j].total)
                        t = ceil(((double)scores[mode][l].stats[i][j].completed * 10000) / scores[mode][l].stats[i][j].total);
                    SaveObjectNumber* stat = new SaveObjectNumber(t);
                    sub_stats_list->add_item(stat);
                }
                stats_list->add_item(sub_stats_list);
            }
            top_slist->add_item(stats_list);
        }
        resp->add_item("stats", top_slist);
        
        bool got_lev_req = false;
        for (unsigned i = 0;  server_level_types[i]; i++)
        {
            if (i >= next_server_levels.size())
                next_server_levels.resize(i + 1);
            if (next_server_levels[i].size() < 200)
            {
                resp->add_string("level_gen_req", server_level_types[i]);
                got_lev_req = true;
                break;
            }
        }
        if (!got_lev_req)
            for (unsigned i = 0;  neg_server_level_types[i]; i++)
            {
                if (i >= next_neg_server_levels.size())
                    next_neg_server_levels.resize(i + 1);
                if (next_neg_server_levels[i].size() < 200)
                {
                    resp->add_string("level_gen_req", neg_server_level_types[i]);
                    break;
                }
            }
        resp->add_num("game_mode", mode);
        return resp;
    }

};

static size_t curl_write_data(void *ptr, size_t size, size_t nmemb, void *usr)
{
    size *= nmemb;
    std::string& s = *((std::string*) usr);
    s.append((char*)ptr, size);
    return size;
}

class Connection
{
public:
    struct sockaddr_in clientaddr;
    int conn_fd;
    int length;
    std::string inbuf;
    std::string outbuf;
    Connection(int conn_fd_):
        conn_fd(conn_fd_),
        length(-1)
    {
    }

    void recieve(Database& db)
    {
        static char buf[1024];
        while (true)
        {
            ssize_t num_bytes_received = recv(conn_fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (num_bytes_received  == -1)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    close();
                break;
            }
            else if (num_bytes_received  == 0)
            {
                close();
                break;
            }
            inbuf.append(buf, num_bytes_received);
        }

        while (true)
        {
            ssize_t num_bytes_received = send(conn_fd, outbuf.c_str(), outbuf.length(), MSG_DONTWAIT);
            if (num_bytes_received  == -1)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    close();
                break;
            }
            else if (num_bytes_received  == 0)
            {
                break;
            }
            outbuf.erase(0, num_bytes_received);
            if (num_bytes_received && outbuf.empty())
            {
                close();
                return;
            }
        }
        while (true)
        {
            if (length < 0 && inbuf.length() >= 4)
            {
                length = *(uint32_t*)inbuf.c_str(),
                inbuf.erase(0, 4);
                if (length > 1024*1024)
                {
                    close();
                    break;
                }
            }
            else if (length > 0 && (int)inbuf.length() >= length)
            {
                bool pirate = false;
                try
                {
                    std::string decomp = decompress_string(inbuf);
                    std::istringstream decomp_stream(decomp);
                    SaveObjectMap* omap = SaveObject::load(decomp_stream)->get_map();
                    uint64_t steam_id = omap->get_num("steam_id");
                    inbuf.erase(0, length);
                    length = -1;
                    if (steam_id != SECRET_ID && steam_id != 0)
                    {
                        std::string steam_session;
                        omap->get_string("steam_session", steam_session);
                        if (!db.steam_sessions.count(steam_session))
                        {
                            CURL *curl;
                            CURLcode res;
                            curl = curl_easy_init();
                            if (!curl)
                            {
                                close();
                                throw(std::runtime_error("curl_easy_init() failed"));
                            }

                            std::string url = "https://partner.steam-api.com/ISteamUserAuth/AuthenticateUserTicket/v1/?key=44D5549D3DC57BCF2492489740F0354A&appid=" + std::string(omap->get_num("demo") ? "2263470" : omap->get_num("playtest") ? "2263480" : "2262930") + "&ticket=" + steam_session;

                            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    //                        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    //                        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
                            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

                            std::string response;

                            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_data);
                            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

                            res = curl_easy_perform(curl);
                            curl_easy_cleanup(curl);
                            if(res != CURLE_OK)
                            {
                                printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                                throw(std::runtime_error("curl_easy_perform() failed"));
                            }
                            std::cout << steam_id  << " " << response << "\n";

                            try
                            {
                                SaveObjectMap* omap = SaveObject::load(response)->get_map()->get_item("response")->get_map();
                                db.steam_sessions[steam_session] = 0;
                                uint64_t server_steam_id = std::stoull(omap->get_item("params")->get_map()->get_string("steamid"));
                                db.steam_sessions[steam_session] = server_steam_id;
                            }
                            catch (const std::runtime_error& error)
                            {
                                std::cout << "Exception " << error.what() << "\n";
                            }
                        }
                        
                        if (db.steam_sessions.count(steam_session) &&
                            (db.steam_sessions[steam_session] != steam_id))
                        {
                            char ip4[INET_ADDRSTRLEN];
                            inet_ntop(AF_INET, &(clientaddr.sin_addr), ip4, INET_ADDRSTRLEN);
                            printf("pirate check failed %s\n", ip4);
                            pirate = true;
                            std::cout << "failed:" << steam_id << " - " << db.steam_sessions[steam_session] << "\n";
                            // omap->save(std::cout);
                            // close();
                            // break;
                        }
                    }
                    
                    std::string command;
                    omap->get_string("command", command);
                    int player_version = omap->get_num("version");
                    if (!pirate && player_version != game_version)
                    {
                        throw(std::runtime_error("player_version != game_version"));
                        // pirate = true;
                        // printf("old version\n");
                        // close();
                        // break;
                    }
                    if (pirate)
                    {
                        SaveObjectMap* scr = new SaveObjectMap();
                        scr->add_num("pirate", 1);
                        std::string s = scr->to_string();
                        std::string comp = compress_string(s);
                        uint32_t length = comp.length();
                        outbuf.append((char*)&length, 4);
                        outbuf.append(comp);
                        delete scr;
                    }
                    else if (command == "scores")
                    {
                        std::string steam_username;
                        omap->get_string("steam_username", steam_username);
                        db.update_name(steam_id, steam_username);
                        int mode = 0;
                        if (omap->has_key("game_mode"))
                            mode = omap->get_num("game_mode");
                        char ip4[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &(clientaddr.sin_addr), ip4, INET_ADDRSTRLEN);
                        printf("scores: %s [%s] %lu (%d)", steam_username.c_str(), ip4, steam_id, mode);

                        SaveObjectList* progress_list = omap->get_item("level_progress")->get_list();
                        for (unsigned lset = 0; lset < progress_list->get_count() && lset < LEVEL_TYPES - 1; lset++)
                        {
                            if (steam_id == SECRET_ID)
                                continue;
                            if (steam_id == 0ull)
                                continue;
                            if (omap->has_key("server_levels_version") && (omap->get_num("server_levels_version") != db.server_levels_version))
                                continue;


                            unsigned score = 0;
                            SaveObjectList* l = progress_list->get_item(lset)->get_list();
                            for (unsigned lgrp = 0; lgrp < l->get_count() && lgrp < 30; lgrp++)
                            {
                                std::string s = l->get_item(lgrp)->get_string();
                                for (unsigned i = 0; i < s.length() && i < 200; i++)
                                {
                                    bool c = (s[i] == '1');
                                    db.scores[mode][lset].stats[lgrp][i].total++;
                                    if (c)
                                    {
                                        db.scores[mode][lset].stats[lgrp][i].completed++;
                                        score++;
                                    }
                                }
                            }
                            db.scores[mode][lset].add_score(steam_id, score);
                            printf("%u ", score);
                        }
                        printf("\n");
                        std::set<uint64_t> friends;
                        if (omap->has_key("friends"))
                        {
                            SaveObjectList* friend_list = omap->get_item("friends")->get_list();
                            for (unsigned i = 0; i < friend_list->get_count(); i++)
                            {
                                friends.insert(friend_list->get_num(i));
                            }
                            friends.insert(omap->get_num("steam_id"));
                        }
                        if(omap->has_key("level_gen_req") && omap->has_key("level_gen_resp"))
                        {
                            std::string req = omap->get_string("level_gen_req");
                            std::string resp = omap->get_string("level_gen_resp");
                            db.add_server_level(req, resp);
                        }

                        SaveObjectMap* scr = db.get_scores(mode, steam_id, friends);

                        if(omap->has_key("server_levels_version"))
                        {
                            if (omap->get_num("server_levels_version") != db.server_levels_version)
                            {
                                scr->add_num("server_levels_version", db.server_levels_version);
                                SaveObjectList* sl_list = new SaveObjectList;
                                for (std::vector<std::string>& lvl_set : db.server_levels)
                                {
                                    SaveObjectList* ssl_list = new SaveObjectList;
                                    for (std::string& lvl : lvl_set)
                                    {
                                        ssl_list->add_string(lvl);
                                    }
                                    sl_list->add_item(ssl_list);
                                }
                                scr->add_item("server_levels", sl_list);
                                if (db.neg_server_levels.size())
                                {
                                    sl_list = new SaveObjectList;
                                    for (std::vector<std::string>& lvl_set : db.neg_server_levels)
                                    {
                                        
                                        SaveObjectList* ssl_list = new SaveObjectList;
                                        for (std::string& lvl : lvl_set)
                                        {
                                            ssl_list->add_string(lvl);
                                        }
                                        sl_list->add_item(ssl_list);
                                    }
                                    scr->add_item("neg_server_levels", sl_list);
                                }
                            }
                        }
                        scr->add_num("festivus", 1);

                        std::string s = scr->to_string();
                        std::string comp = compress_string(s);
                        uint32_t length = comp.length();
                        outbuf.append((char*)&length, 4);
                        outbuf.append(comp);
                        delete scr;
                    }
                    else
                    {
                        printf("unknown command: %s \n", command.c_str());
                        close();
                    }
                    
                    delete omap;
                }
                catch (const std::runtime_error& error)
                {
                    std::cout << "Exception " << error.what() << "\n";
                    close();
                    break;
                }
            }
            else
                break;
        }
    }
    
    void close()
    {
        if (conn_fd < 0)
            return;
        shutdown(conn_fd, SHUT_RDWR);
        ::close(conn_fd);
        conn_fd = -1;
    }

};

void sig_handler(int signo)
{
    if (signo == SIGUSR1)
        printf("received SIGUSR1\n");
    else if (signo == SIGTERM)
        printf("received SIGTERM\n");
    power_down = true;
    printf("shutting down\n");
}

int main(int argc, char *argv[])
{
    Database db;
    signal(SIGUSR1, sig_handler);
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    curl_global_init(CURL_GLOBAL_DEFAULT);
  
    try 
    {
        std::ifstream loadfile("db.save");
        if (!loadfile.fail() && !loadfile.eof())
        {
            SaveObjectMap* omap = SaveObject::load(loadfile)->get_map();
            db.load(omap);
            delete omap;
        }
    }
    catch (const std::runtime_error& error)
    {
        std::cout << error.what() << "\n";
    }

    struct sockaddr_in myaddr ,clientaddr;
    int sockid;
    sockid=socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK , 0);
//    memset(&myaddr,'0',sizeof(myaddr));
    myaddr.sin_family=AF_INET;
    myaddr.sin_port=htons(42071);
    myaddr.sin_addr.s_addr=INADDR_ANY;
    if(sockid==-1)
    {
        perror("socket");
        return 1;
    }
    socklen_t len=sizeof(myaddr);
    if(bind(sockid,( struct sockaddr*)&myaddr,len)==-1)
    {
        perror("bind");
        return 1;
    }
    if(listen(sockid,10)==-1)
    {
        perror("listen");
        return 1;
    }
    
    
    std::list<Connection> conns;

    time_t old_time = 0;
    int week = time(NULL) / 604800;

    while(true)
    {
        {
            std::ifstream loadfile("NEW_SERVER_LEVELS");
            if (!loadfile.fail() && !loadfile.eof())
            {
                week = 0;
                std::remove("NEW_SERVER_LEVELS");
            }
        }
        {
            std::ifstream loadfile("CLEAR_NEXT_SERVER_LEVELS");
            if (!loadfile.fail() && !loadfile.eof())
            {
                db.next_server_levels.clear();
                db.next_neg_server_levels.clear();
                std::remove("CLEAR_NEXT_SERVER_LEVELS");
            }
        }
        {
            fd_set w_fds;
            fd_set r_fds;
            struct timeval timeout;
            timeout.tv_sec = 5;
            timeout.tv_usec = 0;

            FD_ZERO(&r_fds);
            FD_ZERO(&w_fds);
            FD_SET(sockid, &r_fds);
            
            for (Connection &conn :conns)
            {
                FD_SET(conn.conn_fd, &r_fds);
                if (!conn.outbuf.empty())
                    FD_SET(conn.conn_fd, &w_fds);
            }
            
            select(1024, &r_fds, &w_fds, NULL, &timeout);
        }
        {
            int new_week = time(NULL) / 604800;
            if (week != new_week)
            {
                db.server_levels = db.next_server_levels;
                db.next_server_levels.clear();
                db.neg_server_levels = db.next_neg_server_levels;
                db.next_neg_server_levels.clear();
                db.server_levels_version++;
                week = new_week;

                int windex = db.server_levels_version % 10;
                for (int i = 0; i < GAME_MODE_TYPES; i++)
                {
                    db.scores[i][LEVEL_TYPES - 1].reset();
                    for(auto& [id, val] : db.players)
                    {
                        uint64_t score = 0;
                        if (db.scores[i][LEVEL_TYPES-2].user_score.count(id))
                            score = db.scores[i][LEVEL_TYPES-2].user_score[id];
                        val.weekly_scores[i][windex] = score;

                        score = 0;
                        for (int j = 1 ; j <= 10; j++)
                        {
                            score += val.weekly_scores[i][(j + windex) % 10] * j / 10;
                        }
                        if (score)
                            db.scores[i][LEVEL_TYPES - 1].add_score(id, score);
                    }
                    db.scores[i][LEVEL_TYPES - 2].reset();
                }
            }
        }

        while (true)
        {
            int conn_fd = accept(sockid,(struct sockaddr *)&clientaddr, &len);
            if (conn_fd == -1)
                break;
            if (conn_fd >= 1024)
                ::close(conn_fd);
            conns.push_back(conn_fd);
            conns.back().clientaddr = clientaddr;
        }

        for (std::list<Connection>::iterator it = conns.begin(); it != conns.end();)
        {
            Connection& conn = (*it);
            conn.recieve(db);
            if (conn.conn_fd < 0)
                it = conns.erase(it);
            else
                it++;
        }

        fflush(stdout);
        if (power_down)
            break;

        time_t new_time;
        time(&new_time);
        if ((old_time + 60 * 5) < new_time)
        {
            SaveObject* savobj = db.save(false);
            std::string out_data = savobj->to_string();
            std::ofstream outfile ("db.save");
            outfile << out_data;
            delete savobj;
            old_time = new_time;
        }
    }
    close(sockid);
    if (1)
    {
        SaveObject* savobj = db.save(false);
        std::string out_data = savobj->to_string();
        std::ofstream outfile ("db.save");
        outfile << out_data;
        delete savobj;
    }
    curl_global_cleanup();
    return 0;
}
