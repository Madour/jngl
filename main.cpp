/*
Copyright 2007-2008  Jan Niklas Hasse <jhasse@gmail.com>

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

#include "jngl.hpp"
#include "window.hpp"
#include "debug.hpp"

#include <boost/shared_ptr.hpp>
#include <GL/gl.h>
#include <GL/glu.h>
#include <stdexcept>
#include <sstream>

#ifndef NDEBUG
#include <iostream>
#endif

extern "C"
{
	void InitCallbacks(); // see callbacks.c
}

namespace jngl
{
    float bgRed = 1.0f, bgGreen = 1.0f, bgBlue = 1.0f; // Background Colors
	bool Init(const int width, const int height)
	{
		glShadeModel(GL_SMOOTH);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glClearColor(bgRed, bgGreen, bgBlue, 0.0f);
		glClearDepth(1.0f);
		glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
		glViewport(0, 0, width, height);
		Scale(width, height);
		glFlush();
		InitCallbacks();
		return true;
	}

	int scaleWidth = -1, scaleHeight = -1;
	void Scale(const int width, const int height)
	{
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0.0f, width, height, 0.0f, -100.0f, 100.0f);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		scaleWidth = width;
		scaleHeight = height;
	}

	int ScaleWidth()
	{
		return scaleWidth;
	}

	int ScaleHeight()
	{
		return scaleHeight;
	}

	boost::shared_ptr<Window> pWindow;

	void ShowWindow(const std::string& title, const int width, const int height, bool fullscreen)
	{
		if(width == 0)
			throw std::runtime_error("Width Is 0");
		if(height == 0)
			throw std::runtime_error("Height Is 0");
		pWindow.reset(new Window(title, width, height, fullscreen));
	}

	void HideWindow()
	{
		pWindow.reset((Window*)0);
	}

	void BeginDraw()
	{
		pWindow->BeginDraw();
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	void EndDraw()
	{
		pWindow->EndDraw();
	}

	bool Running()
	{
		return pWindow->Running();
	}

	void Quit()
	{
		pWindow->Quit();
	}

	void BackgroundColor(const unsigned char red, const unsigned char green, const unsigned char blue)
	{
		bgRed = red / 255.0f;
		bgGreen = green / 255.0f;
		bgBlue = blue / 255.0f;
		glClearColor(bgRed, bgGreen, bgBlue, 0.0f);
	}

	int MouseX()
	{
		return pWindow->MouseX();
	}

	int MouseY()
	{
		return pWindow->MouseY();
	}

	bool KeyDown(const int key)
	{
		return pWindow->KeyDown(key);
	}

	bool KeyPressed(const int key)
	{
		return pWindow->KeyPressed(key);
	}

	bool KeyDown(const char key)
	{
		return pWindow->KeyDown(key);
	}

	bool KeyPressed(const char key)
	{
		return pWindow->KeyPressed(key);
	}

	bool MouseDown(mouse::Button button)
	{
		return pWindow->MouseDown(button);
	}

	bool MousePressed(mouse::Button button)
	{
		return pWindow->MousePressed(button);
	}

	void SetMouse(const int xposition, const int yposition)
	{
		pWindow->SetMouse(xposition, yposition);
	}

	void MouseVisible(const bool visible)
	{
		return pWindow->MouseVisible(visible);
	}

	void SetTitle(const std::string& title)
	{
		return pWindow->SetTitle(title);
	}

	unsigned char colorRed = 255, colorGreen = 255, colorBlue = 255, colorAlpha = 255;

	void Color(const unsigned char red, const unsigned char green, const unsigned char blue, const unsigned char alpha)
	{
		colorRed = red;
		colorGreen = green;
		colorBlue = blue;
		colorAlpha = alpha;
		glColor4ub(colorRed, colorGreen, colorBlue, colorAlpha);
	}

	void Print(const std::string& text, const int xposition, const int yposition)
	{
		pWindow->Print(text, xposition, yposition);
		glColor4ub(colorRed, colorGreen,  colorBlue, colorAlpha);
	}

	void FontSize(const int size)
	{
		pWindow->FontSize(size);
	}

	void SetFont(const std::string& filename)
	{
		pWindow->SetFont(filename);
	}

	void SetFontByName(const std::string& name)
	{
		pWindow->SetFontByName(name);
	}

	bool Window::MouseDown(mouse::Button button)
	{
		return mouseDown_.at(button);
	}

	bool Window::MousePressed(mouse::Button button)
	{
		if(mousePressed_.at(button))
		{
			mousePressed_[button] = false;
			return true;
		}
		return false;
	}

	double FPS()
	{
		static double lastDraw = 0.0;
		double fps = 1/(jngl::Time() - lastDraw);
		lastDraw = jngl::Time();
		return fps;
	}

	void Reset()
	{
		glLoadIdentity();
	}

	void Rotate(const double radian)
	{
		glRotated(radian, 0, 0, 1);
	}

	void Translate(const int x, const int y)
	{
		glTranslatef(x, y, 0);
	}

	void DrawRect(const int xposition, const int yposition, const int width, const int height)
	{
		glPushMatrix();
		glTranslatef(xposition, yposition, 0);
		glBegin(GL_QUADS);
			glVertex2i(0, 0);
			glVertex2i(width, 0);
			glVertex2i(width, height);
			glVertex2i(0, height);
		glEnd();
		glPopMatrix();
	}

	void DrawLine(const double xstart, const double ystart, const double xend, const double yend)
	{
		glEnable(GL_LINE_SMOOTH);
		glPushMatrix();
		glBegin(GL_LINES);
			glVertex2f(xstart, ystart);
			glVertex2f(xend, yend);
		glEnd();
		glPopMatrix();
		glDisable(GL_LINE_SMOOTH);
	}
}
