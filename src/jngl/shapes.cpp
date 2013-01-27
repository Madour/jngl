/*
Copyright 2012 Jan Niklas Hasse <jhasse@gmail.com>
For conditions of distribution and use, see copyright notice in LICENSE.txt
*/

#include "shapes.hpp"
#include "../opengl.hpp"
#include "../draw.hpp"
#include "../spriteimpl.hpp"

namespace jngl {
	unsigned char colorRed = 0, colorGreen = 0, colorBlue = 0, colorAlpha = 255;

	void setColor(unsigned char red, unsigned char green, unsigned char blue) {
		colorRed = red;
		colorGreen = green;
		colorBlue = blue;
	}

	void setColor(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha) {
		colorRed = red;
		colorGreen = green;
		colorBlue = blue;
		colorAlpha = alpha;
	}

	void setAlpha(unsigned char alpha) {
		colorAlpha = alpha;
	}

	std::stack<unsigned char> alphas;

	void pushAlpha(unsigned char alpha) {
		alphas.push(colorAlpha);
		setAlpha(colorAlpha * alpha / 255);
	}

	void popAlpha() {
		setAlpha(alphas.top());
		alphas.pop();
	}

	void drawEllipse(float xmid, float ymid, float width, float height, float startAngle) {
		glColor4ub(colorRed, colorGreen, colorBlue, colorAlpha);
		draw::Ellipse(xmid, ymid, width, height, startAngle);
		glColor4ub(spriteColorRed, spriteColorGreen, spriteColorBlue, spriteColorAlpha);
	}
}