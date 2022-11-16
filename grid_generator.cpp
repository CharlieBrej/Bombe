#include "Grid.h"

int main( int argc, char* argv[] )
{
    grid_set_rnd(10);
    Grid g(XYPos(8,8));
    g.print();

    g.make_harder();
    g.print();
	return 0;
}
