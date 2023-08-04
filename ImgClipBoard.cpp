#include "ImgClipBoard.h"

#ifndef TARGET_LINUX

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

    uint8_t* dat = (uint8_t*) img.data();
    XYPos siz(spec.width, spec.height);
	int bytes_per_pixel = spec.bits_per_pixel / 8;
    for (int y = 0; y < siz.y; y++)
    for (int x = 0; x < siz.x; x++)
    {
		uint32_t d = 0;
		for (int i = 0; i < bytes_per_pixel; i++)
			d |= uint32_t(dat[y * spec.bytes_per_row + x * bytes_per_pixel + i]) << i * 8;
        int i = y * (spec.bytes_per_row / 4) + x;
        uint32_t r = (d & spec.red_mask) >> spec.red_shift;
        uint32_t g = (d & spec.green_mask) >> spec.green_shift;
        uint32_t b = (d & spec.blue_mask) >> spec.blue_shift;
        uint32_t a = (d & spec.alpha_mask) >> spec.alpha_shift;
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
#include "clip/clip.h"
#include "clip/clip_x11_png.h"

static SDL_mutex* clip_mutex;
static SDL_Thread *sdl_clipboard_thread;
static Atom XA_TARGETS;
static Display* disp;
static Atom sel;
static Window win;

static std::vector<uint32_t> reply_data;
static XYPos reply_size = XYPos(0, 0);
static bool SHUTDOWN = false;

struct Property
{
	unsigned char *data;
	int format;
	unsigned long nitems;
	Atom type;
};

//This fetches all the data from a property
Property read_property(Display* disp, Window win, Atom property)
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
		XGetWindowProperty(disp, win, property, 0, read_bytes, False, AnyPropertyType,
							&actual_type, &actual_format, &nitems, &bytes_after,
							&ret);

		read_bytes *= 2;
	}while(bytes_after != 0);

	Property p = {ret, actual_format, nitems, actual_type};

	return p;
}

static int clipboard_thread(void *ptr)
{
	disp = XOpenDisplay(NULL);
	int screen = DefaultScreen(disp);
	Window root = RootWindow(disp, screen);
    std::map<std::string, int> datatypes;

	sel = XInternAtom(disp, "CLIPBOARD", 0);
	win = XCreateSimpleWindow(disp, root, 0, 0, 100, 100, 0, BlackPixel(disp, screen), BlackPixel(disp, screen));
	XA_TARGETS = XInternAtom(disp, "TARGETS", False);

	Atom image_png_atom = XInternAtom(disp, "image/png", False);
	Atom XA_multiple = XInternAtom(disp, "MULTIPLE", False);


	XEvent e;
	for(;;)
	{
		XNextEvent(disp, &e);
		if (SHUTDOWN)
			break;

		if(e.type == SelectionClear)
		{
		}
		if(e.type == SelectionRequest)
		{
			Atom target = e.xselectionrequest.target;

			XEvent s;
			s.xselection.type      = SelectionNotify;
			s.xselection.requestor = e.xselectionrequest.requestor;
			s.xselection.selection = e.xselectionrequest.selection;
			s.xselection.target    = e.xselectionrequest.target;
			s.xselection.time      = e.xselectionrequest.time;
			s.xselection.property  = e.xselectionrequest.property;

			if(target == XA_TARGETS)
			{
				Atom targets[3] = {XA_TARGETS, XA_multiple, image_png_atom};
				XChangeProperty(disp, e.xselectionrequest.requestor, e.xselectionrequest.property, XA_ATOM, 32, PropModeReplace, (unsigned char*)targets, 3);
				XSendEvent(disp, e.xselectionrequest.requestor, True, 0, &s);
			}
			else if(target == image_png_atom)
			{
				SDL_LockMutex(clip_mutex);
				clip::image_spec spec;
				spec.width = reply_size.x;
				spec.height = reply_size.y;
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
				clip::image img(&reply_data[0], spec);
				std::vector<uint8_t> png_data;

				if (clip::x11::write_png(img, png_data))
				{
					XChangeProperty(disp, e.xselectionrequest.requestor, e.xselectionrequest.property, target, 8, PropModeReplace,
						&png_data[0], png_data.size());
				}
				XSendEvent(disp, e.xselectionrequest.requestor, True, 0, &s);
				SDL_UnlockMutex(clip_mutex);
			}
			else
			{
				s.xselection.property  = None;
				XSendEvent(disp, e.xselectionrequest.requestor, True, 0, &s);
			}
		}
		if(e.type == SelectionNotify)
		{
			Atom target = e.xselection.target;
			if(e.xselection.property == None)
			{
				continue;
			}
			else
			{
				bool tgts = false;
				Property prop = read_property(disp, win, sel);
				if(target == XA_TARGETS)
				{
                    Atom *atom_list = (Atom*)prop.data;
                    int atom_count  = prop.nitems;
                    for (int i = 0; i < atom_count; i++)
                    {
						if (atom_list[i] == image_png_atom)
						{
							XConvertSelection(disp, sel, image_png_atom, sel, win, CurrentTime);
							tgts = true;
						}
                    }
					if (!tgts)
					{
						SDL_LockMutex(clip_mutex);
						reply_size = XYPos(0,0);
						SDL_UnlockMutex(clip_mutex);
					}
				}
				else if(target == image_png_atom)
				{
					clip::image img;
					if (clip::x11::read_png(prop.data, prop.nitems * prop.format / 8, &img, NULL))
					{
						SDL_LockMutex(clip_mutex);
						clip::image_spec spec = img.spec();
						uint32_t* dat = (uint32_t*) img.data();
						XYPos siz(spec.width, spec.height);
						reply_data.clear();
						for (int y = 0; y < siz.y; y++)
						for (int x = 0; x < siz.x; x++)
						{
							int i = y * (spec.bytes_per_row / 4) + x;
							uint32_t r = (dat[i] & spec.red_mask) >> spec.red_shift;
							uint32_t g = (dat[i] & spec.green_mask) >> spec.green_shift;
							uint32_t b = (dat[i] & spec.blue_mask) >> spec.blue_shift;
							uint32_t a = (dat[i] & spec.alpha_mask) >> spec.alpha_shift;
							uint32_t bgra = (b << 24) | (g << 16) | (r << 8) | a;
							reply_data.push_back(bgra);
						}
						reply_size = siz;
						SDL_UnlockMutex(clip_mutex);
					}
				}
				XFree(prop.data);
			}
		}
	}
    return 0;
}

void ImgClipBoard::init()
{
	clip_mutex = SDL_CreateMutex();
    sdl_clipboard_thread = SDL_CreateThread(clipboard_thread, "ClipBoardThread", (void *)NULL);
}

void ImgClipBoard::shutdown()
{
	SHUTDOWN = true;
    XConvertSelection(disp, sel, XA_TARGETS, sel, win, CurrentTime);
    XFlush(disp);
	SDL_WaitThread(sdl_clipboard_thread, NULL);
}


void ImgClipBoard::send(uint32_t* data, XYPos siz)
{
	SDL_LockMutex(clip_mutex);
	reply_data.clear();
	reply_data.insert(reply_data.begin(), data, data + (siz.x * siz.y));
	reply_size = siz;
	SDL_UnlockMutex(clip_mutex);
	XSetSelectionOwner(disp, sel, win, CurrentTime);
}

XYPos ImgClipBoard::recieve(std::vector<uint32_t>& reply)
{
    XConvertSelection(disp, sel, XA_TARGETS, sel, win, CurrentTime);
    XFlush(disp);
    reply = reply_data;
    return reply_size;
}



#endif
