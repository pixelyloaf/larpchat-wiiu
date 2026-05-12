#pragma once

extern float scaleX;
extern float scaleY;

// Macros that scale coordinates and sizes based on the TV resolution
#define SX(x) ((int)((x) * scaleX))
#define SY(y) ((int)((y) * scaleY))
#define SF(s) ((int)((s) * std::min(scaleX, scaleY)))