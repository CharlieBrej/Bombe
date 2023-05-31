#include "ImgClipBoard.h"

#ifndef TARGET_LINUX_XXX

#include "clip/clip.h"
void ImgClipBoard::init()
{
    clip::set_error_handler(NULL);
}
void ImgClipBoard::shutdown()
{
}

void ImgClipBoard::send(uint32_t* data, XYPos siz)
{
    clip::image_spec spec;
    spec.width = siz.x;
    spec.height = siz.y;
    spec.bits_per_pixel = 32;
    spec.bytes_per_row = spec.width * 4;
    spec.red_mask = 0xff00;
    spec.green_mask = 0xff0000;
    spec.blue_mask = 0xff000000;
    spec.alpha_mask = 0xff;
    spec.red_shift = 8;
    spec.green_shift = 16;
    spec.blue_shift = 24;
    spec.alpha_shift = 0;
    clip::image img(data, spec);
    clip::set_image(img);
}

XYPos ImgClipBoard::recieve(std::vector<uint32_t>& reply)
{
    clip::image img;
    if (!clip::get_image(img))
        return XYPos(0,0);
    clip::image_spec spec = img.spec();
    uint32_t* dat = (uint32_t*) img.data();
    XYPos siz(spec.width, spec.height);
    for (int y = 0; y < siz.y; y++)
    for (int x = 0; x < siz.x; x++)
    {
        int i = y * (spec.bytes_per_row / 4) + x;
        uint32_t r = (dat[i] & spec.red_mask) >> spec.red_shift;
        uint32_t g = (dat[i] & spec.green_mask) >> spec.green_shift;
        uint32_t b = (dat[i] & spec.blue_mask) >> spec.blue_shift;
        uint32_t a = (dat[i] & spec.alpha_mask) >> spec.alpha_shift;
        uint32_t bgra = (b << 24) | (g << 16) | (r << 8) | a;
        reply.push_back(bgra);
    }
    return siz;
}
#else

#include <SDL.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <map>
#include <string>

static SDL_Thread *sdl_clipboard_thread;
static Atom XA_TARGETS;
static Display* disp;
static Atom sel;
static Window w;

static std::string GetAtomName(Display* disp, Atom a)
{
	if(a == None)
		return "None";
	else
		return XGetAtomName(disp, a);
}
struct Property
{
	unsigned char *data;
	int format;
	unsigned long nitems;
	Atom type;
};

//This fetches all the data from a property
Property read_property(Display* disp, Window w, Atom property)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems;
	unsigned long bytes_after;
	unsigned char *ret = 0;

	int read_bytes = 1024;

	//Keep trying to read the property until there are no
	//bytes unread.
	do
	{
		if(ret != 0)
			XFree(ret);
		XGetWindowProperty(disp, w, property, 0, read_bytes, False, AnyPropertyType,
							&actual_type, &actual_format, &nitems, &bytes_after,
							&ret);

		read_bytes *= 2;
	}while(bytes_after != 0);

	std::cerr << std::endl;
	std::cerr << "Actual type: " << GetAtomName(disp, actual_type) << std::endl;
	std::cerr << "Actual format: " << actual_format << std::endl;
	std::cerr << "Number of items: " << nitems <<  std::endl;

	Property p = {ret, actual_format, nitems, actual_type};

	return p;
}

static int clipboard_thread(void *ptr)
{
	disp = XOpenDisplay(NULL);
	int screen = DefaultScreen(disp);
	Window root = RootWindow(disp, screen);
    std::map<std::string, int> datatypes;

	datatypes["image/png"] = 3;

	sel = XInternAtom(disp, "CLIPBOARD", 0);
	w = XCreateSimpleWindow(disp, root, 0, 0, 100, 100, 0, BlackPixel(disp, screen), BlackPixel(disp, screen));
	XA_TARGETS = XInternAtom(disp, "TARGETS", False);

	Atom image_png_atom = XInternAtom(disp, "image/png", False);

	XEvent e;
	for(;;)
	{
		XNextEvent(disp, &e);

		if(e.type == SelectionNotify)
		{
			Atom target = e.xselection.target;
			if(e.xselection.property == None)
			{
				continue;
			}
			else
			{
				Property prop = read_property(disp, w, sel);
				if(target == XA_TARGETS)
				{
                    Atom *atom_list = (Atom*)prop.data;
                    int atom_count  = prop.nitems;
                    for (int i = 0; i < atom_count; i++)
                    {
                        std::string atom_name = GetAtomName(disp, atom_list[i]);
                        std::cerr << "--------" << atom_name <<  std::endl;
                        if (atom_name == "image/png")
                        {
                            XConvertSelection(disp, sel, atom_list[i], sel, w, CurrentTime);
                        }
                    }
				}
				else if(target == image_png_atom)
				{
					//Dump the binary data
					std::cerr << "Data begins:" << std::endl;
					std::cerr << "--------\n";
					std::cout.write((char*)prop.data, prop.nitems * prop.format/8);
					std::cout << std::flush;
					std::cerr << std::endl << "--------" << std::endl << "Data ends\n";

					continue;
				}
				else
                {
					std::cerr << "else:" << std::endl;

                    continue;
                }

				XFree(prop.data);
			}
			std::cerr << std::endl;
		}
	}



    return 0;
}

void ImgClipBoard::init()
{
    sdl_clipboard_thread = SDL_CreateThread(clipboard_thread, "ClipBoardThread", (void *)NULL);
}

void ImgClipBoard::send(uint32_t* data, XYPos siz)
{
}

XYPos ImgClipBoard::recieve(std::vector<uint32_t>& reply)
{
    XConvertSelection(disp, sel, XA_TARGETS, sel, w, CurrentTime);
    XFlush(disp);

    XYPos siz(0,0);
    return siz;
}



#endif
