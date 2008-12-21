/*
Copyright 2007-2008 Jan Niklas Hasse <jhasse@gmail.com>

Most of this code is based on http://nehe.gamedev.net/data/lessons/lesson.asp?lesson=43

This file is part of jngl.

jngl is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

jngl is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with jngl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "freetype.hpp"
#include "jngl.hpp"
#include "debug.hpp"

#include "ConvertUTF.h"

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>

namespace jngl
{
	unsigned char fontColorRed = 0, fontColorGreen = 0, fontColorBlue = 0, fontColorAlpha = 255;

	void FontColor(const unsigned char red, const unsigned char green, const unsigned char blue, const unsigned char alpha)
	{
		fontColorRed = red;
		fontColorGreen = green;
		fontColorBlue = blue;
		fontColorAlpha = alpha;
	}

	///This function gets the first power of 2 >= the
	///int that we pass it.
	int next_p2 ( int a )
	{
		if(a == 1) // A texture with this width does not work for some reason
		{
			return 2;
		}
		int rval=1;
		while(rval<a) rval<<=1;
		return rval;
	}

	void LoadCharacter(FT_Face face, unsigned long ch, GLuint displayList, GLuint texture, const float fontHeight)
	{
		if(FT_Load_Glyph(face, FT_Get_Char_Index(face, ch) , FT_LOAD_TARGET_LIGHT))
		{
			std::string msg = std::string("FT_Load_Glyph failed. Character: ") + boost::lexical_cast<std::string>(ch);
			Debug(msg);
			// Load a question mark instead
			if(FT_Load_Glyph(face, FT_Get_Char_Index(face, '?') , FT_LOAD_TARGET_LIGHT))
			{
				throw std::runtime_error(msg);
			}
		}
		FT_Glyph glyph;
		if(FT_Get_Glyph(face->glyph, &glyph ))
			throw std::runtime_error("FT_Get_Glyph failed");
		FT_Glyph_To_Bitmap( &glyph, ft_render_mode_normal, 0, 1 );
		FT_BitmapGlyph bitmap_glyph = (FT_BitmapGlyph)glyph;
		FT_Bitmap& bitmap=bitmap_glyph->bitmap;

		int width = next_p2( bitmap.width );
		int height = next_p2( bitmap.rows );

		std::vector<GLubyte> data(width * height * 4);

		for(int y = 0; y < height; y++)
		{
			for(int x = 0; x < width; x++)
			{
				data[x*4 + y*4*width] = 255;
				data[x*4 + y*4*width + 1] = 255;
				data[x*4 + y*4*width + 2] = 255;
				if(x >= bitmap.width || y >= bitmap.rows) // Are we in the padding zone? (see next_p2)
				{
					data[x*4 + y*4*width + 3] = 0; // Alpha = 0 = not visible
				}
				else
				{
					data[x*4 + y*4*width + 3] = bitmap.buffer[x + bitmap.width*y];
				}
			}
		}

		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // preventing wrapping artifacts
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexImage2D(GL_TEXTURE_2D, 0, 4, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, &data[0]);

		glNewList(displayList, GL_COMPILE);
		glBindTexture(GL_TEXTURE_2D, texture);
		glPushMatrix();

		//first we need to move over a little so that
		//the character has the right amount of space
		//between it and the one before it.
		glTranslatef(bitmap_glyph->left,0,0);

		//Now we need to account for the fact that many of
		//our textures are filled with empty padding space.
		//We figure what portion of the texture is used by
		//the actual character and store that information in
		//the x and y variables, then when we draw the
		//quad, we will only reference the parts of the texture
		//that we contain the character itself.
		float	x=(float)bitmap.width / (float)width,
				y=(float)bitmap.rows / (float)height;

		//Move down a little for small characters
		glTranslatef(0, fontHeight - bitmap_glyph->top,0);

		//Here we draw the texturemaped quads.
		//The bitmap that we got from FreeType was not
		//oriented quite like we would like it to be,
		//so we need to link the texture to the quad
		//so that the result will be properly aligned.
		glBegin(GL_QUADS);
		glTexCoord2d(0,0); glVertex2f(0,0);
		glTexCoord2d(0,y); glVertex2f(0,bitmap.rows);
		glTexCoord2d(x,y); glVertex2f(bitmap.width,bitmap.rows);
		glTexCoord2d(x,0); glVertex2f(bitmap.width,0);
		glEnd();
		glPopMatrix();
		glTranslatef(face->glyph->advance.x >> 6 ,0,0);

		glEndList();
	}

	void UnicodeCharacter::Init(const unsigned long ch, const float fontHeight, FT_Face face)
	{
		glGenTextures(1, &texture_);
		displayList_ = glGenLists(1);
		LoadCharacter(face, ch, displayList_, texture_, fontHeight);
	}

	void UnicodeCharacter::Draw()
	{
		glCallList(displayList_);
	}

	UnicodeCharacter::~UnicodeCharacter()
	{
		glDeleteTextures(1, &texture_);
		glDeleteLists(displayList_, 1);
	}

	void Font::MakeDisplayList(const unsigned long ch)
	{
		LoadCharacter(face_, ch, listBase_ + ch, textures_[ch], height_);
	}

	Font::Font()
	{
		throw std::runtime_error("Attempting to use an unitialized Font object");
	}

	Font::Font(const char* filename, unsigned int height) : height_(height)
	{
		if(!initialized_)
		{
			if(FT_Init_FreeType(&library_))
			{
				throw std::runtime_error("FT_Init_FreeType failed");
			}
			else
			{
				initialized_ = true;
			}
		}
		Debug("Loading font "); Debug(filename); Debug("... ");
		if(FT_New_Face(library_, filename, 0, &face_))
		{
			throw std::runtime_error("FT_New_Face failed");
		}
		Debug("OK\n");
		freeFace_.reset(new Finally(boost::bind(FT_Done_Face, face_))); // Finally will call FT_Done_Face when the Font class is destroyed

		// For some twisted reason, Freetype measures font size
		// in terms of 1/64ths of pixels.  Thus, to make a font
		// h pixels high, we need to request a size of h*64.
		FT_Set_Char_Size(face_, height * 64, height * 64, 96, 96);

		listBase_ = glGenLists(128);
		glGenTextures(128, textures_);

		// Create ech ANSI character
		for(unsigned char i = 0; i < 128; i++)
		{
			MakeDisplayList(i);
		}
	}

	Font::~Font()
	{
		glDeleteLists(listBase_, 128);
		glDeleteTextures(128, textures_);
		freeFace_.reset((Finally*)0); // free face_ with FT_Done_Face
		if(initialized_)
		{
			FT_Done_FreeType(library_);
			initialized_ = false;
		}
	}

	void Font::Print(double x, double y, const std::string& text)
	{
		GLuint font = listBase_;
		double h = height_ / .63; // We make the height about 1.5* that of

		const char *start_line=text.c_str();
		std::vector<std::string> lines;
		const char *c;
		for(c = text.c_str(); *c; c++)
		{
			if(*c == '\n')
			{
				std::string line;
				for(const char *n = start_line; n < c; ++n) line.append(1, *n);
				lines.push_back(line);
				start_line = c + 1;
			}
		}
		if(start_line)
		{
			std::string line;
			for(const char *n = start_line; n < c; ++n) line.append(1, *n);
			lines.push_back(line);
		}

		glPushAttrib(GL_LIST_BIT | GL_CURRENT_BIT  | GL_ENABLE_BIT | GL_TRANSFORM_BIT);
		glMatrixMode(GL_MODELVIEW);
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4ub(fontColorRed, fontColorGreen, fontColorBlue, fontColorAlpha);
		glListBase(font);

//		float modelview_matrix[16];
//		glGetFloatv(GL_MODELVIEW_MATRIX, modelview_matrix);

		//This is where the text display actually happens.
		//For each line of text we reset the modelview matrix
		//so that the line's text will start in the correct position.
		//Notice that we need to reset the matrix, rather than just translating
		//down by h. This is because when each character is
		//draw it modifies the current matrix so that the next character
		//will be drawn immediatly after it.
		for(std::size_t j = 0; j < lines.size(); j++)
		{
			glPushMatrix();
			glTranslated(x,y+h*j,0);
//			glMultMatrixf(modelview_matrix);

			// Check if there is an Unicode character
			for(std::size_t i = 0; i < lines[j].size(); ++i)
			{
				const char& ch = lines[j].c_str()[i]; // Just to have less code
				if(ch & 0x80) // first bit
				{
					const UTF8* sourceEnd = (const unsigned char*)&ch + 2; // sourceEnd has to be the next character after the utf-8 sequence
					++i;
					if(ch & 0x20) // third bit
					{
						++i;
						++sourceEnd;
						if(ch & 0x10) // fourth bit
						{
							++i;
							++sourceEnd;
						}
					}
					UTF32 unicode;
					const UTF8* temp = (const unsigned char*)&ch;
					const UTF8** sourceStart = &temp;
					UTF32* temp2 = &unicode;
					UTF32** targetStart = &temp2;
					UTF32* targetEnd = &unicode + sizeof(unsigned long);
					ConversionResult result = ConvertUTF8toUTF32(sourceStart, sourceEnd, targetStart, targetEnd, lenientConversion);
					if(result == conversionOK)
					{
						std::map<unsigned long, UnicodeCharacter>::iterator i;
						if((i = characters_.find(unicode)) == characters_.end()) // Texture isn't loaded yet?
						{
							characters_[unicode].Init(unicode, height_, face_);
						}
						characters_[unicode].Draw();
					}
					else
					{
						Debug(" ERROR - " + boost::lexical_cast<std::string>(result));
					}
				}
				else
				{
					glCallList(listBase_ + ch);
				}
			}

			glPopMatrix();
		}

		glPopAttrib();
	}

	FT_Library Font::library_;
	bool Font::initialized_ = false;
}

