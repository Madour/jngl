// Copyright 2012-2018 Jan Niklas Hasse <jhasse@bixense.com>
// For conditions of distribution and use, see copyright notice in LICENSE.txt

#pragma once

#include "drawable.hpp"

#include <string>
#include <vector>
#include <memory>

#include "dll.hpp"

namespace jngl {
	enum class Alignment {
		LEFT,
		RIGHT,
		CENTER
	};

	class Font;
	class FontImpl;
	class Line;

	class Text : public Drawable {
	public:
		JNGLDLL_API Text(const std::string& text = "");
		void JNGLDLL_API setText(const std::string&);
		void JNGLDLL_API setFont(std::shared_ptr<Font>);
		void JNGLDLL_API setAlign(Alignment);
		void JNGLDLL_API step();
		void JNGLDLL_API draw() const;
	private:
		std::vector<std::shared_ptr<Line>> lines;
		std::shared_ptr<FontImpl> font;
		Alignment align = Alignment::LEFT;
	};
}
