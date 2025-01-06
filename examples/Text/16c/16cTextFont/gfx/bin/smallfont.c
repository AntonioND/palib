#include <PA_BgStruct.h>

extern const char smallfont_Tiles[];
extern const char smallfont_Map[];
extern const char smallfont_Sizes[];

const PA_BgStruct smallfont = {
	PA_Font4bit,
	256, 64,

	smallfont_Tiles,
	smallfont_Map,
	{smallfont_Sizes},

	3168,
	{6}
};
