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
const int LEVEL_TYPES = 4;

typedef int64_t Score;

class ScoreTable
{
public:
    std::multimap<Score, int64_t> sorted_scores;
    std::map<uint64_t, Score> user_score;
    
    ~ScoreTable()
    {
   
    }
    SaveObject* save()
    {
        SaveObjectList* score_list = new SaveObjectList;
        for(auto const &score : sorted_scores)
        {
            SaveObjectMap* score_map = new SaveObjectMap;
            score_map->add_num("id", score.second);
            score_map->add_num("score", user_score[score.second]);
            score_list->add_item(score_map);
        }
        return score_list;
    }
    void load(SaveObject* sobj)
    {
        SaveObjectList* score_list = sobj->get_list();
        for (unsigned i = 0; i < score_list->get_count(); i++)
        {
            SaveObjectMap* omap = score_list->get_item(i)->get_map();
            add_score(omap->get_num("id"), omap->get_num("score"));
        }
    }

    void add_score(uint64_t steam_id, Score score)
    {
        if ((user_score.count(steam_id)) && (score <= user_score[steam_id]))
            return;

        for (auto it = sorted_scores.begin(); it != sorted_scores.end(); )
        {
           if (it->second == steam_id)
                it = sorted_scores.erase(it);
           else
               ++it;
        }
        user_score.erase(steam_id);
        sorted_scores.insert({-score, steam_id});
        user_score[steam_id] = score;
    }
};


class Database
{
public:
    std::map<uint64_t, std::string> players;
    ScoreTable scores[LEVEL_TYPES];
    std::map<std::string, uint64_t> steam_sessions;


    void update_name(uint64_t steam_id, std::string& steam_username)
    {
        players[steam_id] = steam_username;
    }

    void load(SaveObject* sobj)
    {
        SaveObjectMap* omap = sobj->get_map();
        SaveObjectList* player_list = omap->get_item("players")->get_list();

        unsigned load_game_version = 0;
        if (omap->has_key("version"))
            load_game_version = omap->get_num("version");

        for (unsigned i = 0; i < player_list->get_count(); i++)
        {
            SaveObjectMap* omap = player_list->get_item(i)->get_map();
            std::string name;
            uint64_t id = omap->get_num("id");
            omap->get_string("steam_username", name);
            update_name(id, name);
        }
        SaveObjectList* score_list = omap->get_item("scores")->get_list();
        for (int i = 0; i < LEVEL_TYPES; i++)
        {
            scores[i].load(score_list->get_item(i));
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
            player_map->add_string("steam_username", player_pair.second);
            player_list->add_item(player_map);
        }
        omap->add_item("players", player_list);

        SaveObjectList* score_list = new SaveObjectList;
        for (int i = 0; i < LEVEL_TYPES; i++)
        {
            score_list->add_item(scores[i].save());
        }
        omap->add_item("scores", score_list);
        return omap;
    }
    SaveObject* get_scores(uint64_t user_id, std::set<uint64_t> friends)
    {
        SaveObjectMap* resp = new SaveObjectMap();
        SaveObjectList* top_list = new SaveObjectList();
        for (int i = 0; i < LEVEL_TYPES; i++)
        {
            SaveObjectList* score_list = new SaveObjectList();
            int pos = 0;
            int prevpos = 0;
            for (std::multimap<Score, int64_t>::iterator rit = scores[i].sorted_scores.begin(); rit != scores[i].sorted_scores.end(); rit++)
            {
                pos++;
                int fr = friends.count(rit->second);
                if (rit->second == user_id)
                    fr = 2;
                if (pos > 100 && fr == 0)
                    continue;
                if ((prevpos + 1) < pos)
                {
                    SaveObjectMap* score_map = new SaveObjectMap();
                    score_map->add_num("pos", 0);
                    score_map->add_string("name", "");
                    score_map->add_num("score", 0);
                    score_map->add_num("hidden", 1);
                    score_list->add_item(score_map);
                }
                SaveObjectMap* score_map = new SaveObjectMap();
                score_map->add_num("pos", pos);
                score_map->add_string("name", players[rit->second]);
                score_map->add_num("score", scores[i].user_score[rit->second]);
                if (fr)
                    score_map->add_num("friend", fr);
                score_list->add_item(score_map);
                prevpos = pos;
            }
            top_list->add_item(score_list);
        }
        resp->add_item("scores", top_list);
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
            else if (length > 0 && inbuf.length() >= length)
            {
                try
                {
                    std::string decomp = decompress_string(inbuf);
                    std::istringstream decomp_stream(decomp);
                    SaveObjectMap* omap = SaveObject::load(decomp_stream)->get_map();
                    uint64_t steam_id = omap->get_num("steam_id");
                    inbuf.erase(0, length);
                    length = -1;
                    if (steam_id != SECRET_ID)
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
                                break;
                            }

                            std::string url = "https://partner.steam-api.com/ISteamUserAuth/AuthenticateUserTicket/v1/?key=44D5549D3DC57BCF2492489740F0354A&appid=" + std::string(omap->get_num("demo") ? "2263470" : omap->get_num("playtest") ? "2263480" : "2262930") + "&ticket=" + steam_session;

                            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    //                        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    //                        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
                            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1L);

                            std::string response;

                            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_data);
                            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);


                            res = curl_easy_perform(curl);
                            if(res != CURLE_OK)
                            {
                                printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                                close();
                                break;
                            }
                            curl_easy_cleanup(curl);
                            std::cout << steam_id  << " " << response << "\n";

                            db.steam_sessions[steam_session] = 0;

                            SaveObjectMap* omap = SaveObject::load(response)->get_map()->get_item("response")->get_map()->get_item("params")->get_map();
                            uint64_t server_steam_id = std::stoull(omap->get_string("steamid"));
                            db.steam_sessions[steam_session] = server_steam_id;
                        }
                        
                        if ((steam_id == 0) ||
                            (db.steam_sessions[steam_session] != steam_id))
                        {
                            std::cout << "\nfailed:" << steam_id << " - " << db.steam_sessions[steam_session] << "\n";
                            omap->save(std::cout);
                            close();
                            break;
                        }
                    }
                    
                    std::string command;
                    omap->get_string("command", command);
                    if (command == "save") 
                    {
                        std::string steam_username;
                        omap->get_string("steam_username", steam_username);
                        db.update_name(steam_id, steam_username);
                        printf("save: %s %lld\n", steam_username.c_str(), steam_id);
                        omap->save(std::cout);
                        std::cout << "\n";
                        
                        close();
                    }
                    else if (command == "scores")
                    {
                        std::string steam_username;
                        omap->get_string("steam_username", steam_username);
                        db.update_name(steam_id, steam_username);
                        printf("scores: %s %lld\n", steam_username.c_str(), steam_id);
                        omap->save(std::cout);
                        std::cout << "\n";

                        SaveObjectList* progress_list = omap->get_item("scores")->get_list();
                        for (int lset = 0; lset < progress_list->get_count() && lset < LEVEL_TYPES; lset++)
                        {
                            Score s = progress_list->get_num(lset);
                            db.scores[lset].add_score(steam_id, s);
                        }
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

                        SaveObject* scr = db.get_scores(steam_id, friends);
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
                    std::cout << error.what() << "\n";
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


    while(true)
    {
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

        while (true)
        {
            int conn_fd = accept(sockid,(struct sockaddr *)&clientaddr, &len);
            if (conn_fd == -1)
                break;
            if (conn_fd >= 1024)
                ::close(conn_fd);
            conns.push_back(conn_fd);
        }

        for (std::list<Connection>::iterator it = conns.begin(); it != conns.end();)
        {
            Connection& conn = (*it);
            conn.recieve(db);
            if (conn.conn_fd < 0)
            {
                it = conns.erase(it);
            }
            else
                it++;
        }

        fflush(stdout);
        if (power_down)
            break;

        time_t new_time;
        time(&new_time);
        if ((old_time + 60) < new_time)
        {
            old_time = new_time;
            std::ofstream outfile ("db.save");
            SaveObject* savobj = db.save(false);
            savobj->save(outfile);
            delete savobj;
        }
    }
    close(sockid);
    if (0)
    {
        std::ofstream outfile ("db.save");
        SaveObject* savobj = db.save(false);
        savobj->save(outfile);
        delete savobj;
    }
    curl_global_cleanup();
    return 0;
}
