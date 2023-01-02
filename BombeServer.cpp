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
#include <curl/curl.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "Compress.h"
#include "SaveState.h"

bool power_down = false;

class Database
{
public:
    std::map<uint64_t, std::string> players;
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
        omap->add_num("version", 0);
        return omap;
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
                    inbuf.erase(0, length);
                    length = -1;
                    if (omap->get_num("steam_id") != SECRET_ID)
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
                            std::string url = "https://partner.steam-api.com/ISteamUserAuth/AuthenticateUserTicket/v1/?key=44D5549D3DC57BCF2492489740F0354A&appid=" + std::string(omap->get_num("demo") ? "2263470" : "2262930") + "&ticket=" + steam_session;

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
                                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                                close();
                                break;
                            }
                            curl_easy_cleanup(curl);
                            std::cout << response << "\n";

                            db.steam_sessions[steam_session] = 0;

                            SaveObjectMap* omap = SaveObject::load(response)->get_map()->get_item("response")->get_map()->get_item("params")->get_map();
                            uint64_t server_steam_id = std::stoull(omap->get_string("steamid"));
                            db.steam_sessions[steam_session] = server_steam_id;
                        }
                        
                        if ((omap->get_num("steam_id") == 0) ||
                            (db.steam_sessions[steam_session] != omap->get_num("steam_id")))
                        {
                            std::cout << "failed\n";
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
                        db.update_name(omap->get_num("steam_id"), steam_username);
                        printf("save: %s %lld\n", steam_username.c_str(), omap->get_num("steam_id"));
                        omap->save(std::cout);
                        std::cout << "\n";
                        
                        close();
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
                    std::cerr << error.what() << "\n";
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
        shutdown(conn_fd, SHUT_WR);
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
        std::cerr << error.what() << "\n";
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
            std::ofstream outfile_lite ("db.save.lite");
            SaveObject* savobj_lite = db.save(true);
            savobj_lite->save(outfile_lite);
            delete savobj_lite;
        }
    }
    close(sockid);
    {
        std::ofstream outfile ("db.save");
        SaveObject* savobj = db.save(false);
        savobj->save(outfile);
        delete savobj;
        std::ofstream outfile_lite ("db.save.lite");
        SaveObject* savobj_lite = db.save(true);
        savobj_lite->save(outfile_lite);
        delete savobj_lite;
    }
    curl_global_cleanup();
    return 0;
}
