/*
Copyright 2007-2012 Jan Niklas Hasse <jhasse@gmail.com>
For conditions of distribution and use, see copyright notice in LICENSE.txt
*/

#include <png.h> // We need to include it first, I don't know why

#include "spriteimpl.hpp"
#include "window.hpp"
#include "jngl.hpp"
#include "finally.hpp"
#include "windowptr.hpp"
#include "texture.hpp"
#include "main.hpp"

#include <map>
#include <boost/shared_ptr.hpp>
#include <boost/shared_array.hpp>
#include <boost/function.hpp>
#include <stdexcept>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#ifdef _WIN32
	// These defines are needed to prevent conflicting types declarations in jpeglib.h:
	#define XMD_H
	#define HAVE_BOOLEAN
#endif
#ifndef NOJPEG
	extern "C" {
		#include <jpeglib.h>
	}
#endif
#ifndef NOWEBP
	#include <webp/decode.h>
#endif

namespace jngl
{
	unsigned char spriteColorRed = 255, spriteColorGreen = 255, spriteColorBlue = 255, spriteColorAlpha = 255;

	void setSpriteColor(unsigned char red, unsigned char green, unsigned char blue) {
		spriteColorRed = red;
		spriteColorGreen = green;
		spriteColorBlue = blue;
		glColor4ub(red, green, blue, spriteColorAlpha);
	}

	void setSpriteColor(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha) {
		spriteColorRed = red;
		spriteColorGreen = green;
		spriteColorBlue = blue;
		spriteColorAlpha = alpha;
		glColor4ub(red, green, blue, alpha);
	}

	void setSpriteAlpha(unsigned char alpha) {
		spriteColorAlpha = alpha;
		setSpriteColor(spriteColorRed, spriteColorGreen, spriteColorBlue, alpha);
	}

	std::map<std::string, boost::shared_ptr<Sprite>> sprites_;

	// halfLoad is used, if we only want to find out the width or height of an image. Load won't throw an exception then
	Sprite& GetSprite(const std::string& filename, const bool halfLoad) {
		std::map<std::string, boost::shared_ptr<Sprite> >::iterator i;
		if ((i = sprites_.find(filename)) == sprites_.end()) { // texture hasn't been loaded yet?
			if (!halfLoad) {
				pWindow.ThrowIfNull();
				debug("Loading "); debug(filename); debug(" ...\n");
			}
			auto s = new Sprite(pathPrefix + filename, halfLoad);
			sprites_[filename].reset(s);
			return *s;
		}
		return *(i->second);
	}

	void draw(const std::string& filename, const double xposition, const double yposition) {
		GetSprite(filename).draw(xposition, yposition);
	}

	void drawScaled(const std::string& filename, const double xposition, const double yposition,
	                const float xfactor, const float yfactor) {
		GetSprite(filename).drawScaled(xposition, yposition, xfactor, yfactor);
	}

	void drawScaled(const std::string& filename, const double xposition, const double yposition,
	                const float factor) {
		GetSprite(filename).drawScaled(xposition, yposition, factor, factor);
	}

	void drawClipped(const std::string& filename, double xposition, double yposition, float xstart, float xend, float ystart, float yend) {
		GetSprite(filename).drawClipped(xposition, yposition, xstart, xend, ystart, yend);
	}

	void loadSprite(const std::string& filename) {
		GetSprite(filename);
	}

	void unload(const std::string& filename) {
		std::map<std::string, boost::shared_ptr<Sprite> >::iterator i;
		if((i = sprites_.find(filename)) != sprites_.end())
			sprites_.erase(i);
	}

	void unloadAll() {
		sprites_.clear();
	}

	int getWidth(const std::string& filename) {
		const int width = GetSprite(filename, true).getWidth();
		if (!pWindow) {
			unload(filename);
		}
		return width;
	}

	int getHeight(const std::string& filename) {
		const int height = GetSprite(filename, true).getHeight();
		if (!pWindow) {
			unload(filename);
		}
		return height;
	}

	bool drawButton(const std::string& sprite, const double xposition, const double yposition, const std::string& mouseover) {
		if (xposition <= getMouseX() && getMouseX() < (xposition + getWidth(sprite)) &&
		    yposition <= getMouseY() && getMouseY() < (yposition + getHeight(sprite))) {

			GetSprite(mouseover).draw(xposition, yposition);
			if (mousePressed()) {
				return true;
			}
		} else {
			GetSprite(sprite).draw(xposition, yposition);
		}
		return false;
	}
}