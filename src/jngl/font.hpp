// Copyright 2012-2019 Jan Niklas Hasse <jhasse@bixense.com>
// For conditions of distribution and use, see copyright notice in LICENSE.txt
/// @file
#pragma once

#include "color.hpp"
#include "dll.hpp"
#include "Vec2.hpp"

#include <memory>
#include <string>

namespace jngl {

class FontImpl;

/// Font loaded from a TTF or OTF file
class Font {
public:
	JNGLDLL_API Font(const std::string& filename, unsigned int size);
	std::shared_ptr<FontImpl> JNGLDLL_API getImpl();
	void JNGLDLL_API print(const std::string&, int x, int y);
	void JNGLDLL_API print(const std::string&, Vec2 position);

private:
	std::shared_ptr<FontImpl> impl;
};

void JNGLDLL_API print(const std::string& text, jngl::Vec2 position);

void JNGLDLL_API print(const std::string& text, int xposition, int yposition);

/// Get the font size used by print()
int JNGLDLL_API getFontSize();

/// Change the font size used by print()
void JNGLDLL_API setFontSize(int size);

std::string JNGLDLL_API getFont();

void JNGLDLL_API setFont(const std::string& filename);

void JNGLDLL_API setFontByName(const std::string& name);

void JNGLDLL_API setFontColor(jngl::Color);

void JNGLDLL_API setFontColor(unsigned char red, unsigned char green, unsigned char blue,
                              unsigned char alpha = 255);

void JNGLDLL_API pushFontColor(unsigned char red, unsigned char green, unsigned char blue);

void JNGLDLL_API popFontColor();

/// Get line height used py print() in pixel
int JNGLDLL_API getLineHeight();

/// Set line height used by print() in pixel
void JNGLDLL_API setLineHeight(int);

double JNGLDLL_API getTextWidth(const std::string& text);

} // namespace jngl
