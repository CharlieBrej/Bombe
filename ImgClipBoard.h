#pragma once
#include "Misc.h"

class ImgClipBoard
{
public:
    static void init();
    static void send(uint32_t* data, XYPos siz);
    static XYPos recieve(std::vector<uint32_t>& data);
    static void shutdown();

};
