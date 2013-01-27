/*
Copyright 2012 Jan Niklas Hasse <jhasse@gmail.com>
For conditions of distribution and use, see copyright notice in LICENSE.txt
*/

#pragma once

#include "drawable.hpp"

#include <string>
#include <boost/shared_ptr.hpp>

#ifndef _MSC_VER
#pragma GCC visibility push(default)
#endif
namespace jngl {
	enum Alignment {
		LEFT,
		RIGHT,
		CENTER
	};

	class Font;
	class FontImpl;

	class Text : public Drawable {
	public:
		Text(const std::string& text = "");
		void setText(const std::string&);
		void setFont(boost::shared_ptr<Font>);
		void setAlign(Alignment);
		void step();
		void draw() const;
	private:
		std::string text;
		boost::shared_ptr<FontImpl> font;
		Alignment align;
	};
}
#ifndef _MSC_VER
#pragma GCC visibility pop
#endif