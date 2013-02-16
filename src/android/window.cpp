/*
Copyright 2013 Jan Niklas Hasse <jhasse@gmail.com>
For conditions of distribution and use, see copyright notice in LICENSE.txt
*/

#include "../window.hpp"
#include "../jngl.hpp"

#include <stdexcept>

namespace jngl
{
	Window::Window(const std::string& title, const int width, const int height, const bool fullscreen)
	: fullscreen_(fullscreen), running_(false), isMouseVisible_(true),
	  relativeMouseMode(false), isMultisampleSupported_(true),
	  anyKeyPressed_(false), mousex_(0), mousey_(0), fontSize_(12), width_(width), height_(height),
	  mouseWheel_(0), fontName_(""), oldTime(0), changeWork_(false)
	{
		mouseDown_.assign(false);
		mousePressed_.assign(false);

		Init(height, width);

		running_ = true;
	}

	std::string Window::GetFontFileByName(const std::string& fontname)
	{
		std::string tmp = fontname;
		if(fontname == "sans-serif") {
			tmp = "Arial";
		}
		return tmp + ".ttf";
	}

	Window::~Window() {
	}

	int Window::GetKeyCode(key::KeyType key) {
		return key;
	}

	bool Window::getKeyDown(const std::string& key)	{
		return characterDown_[key];
	}

	bool Window::getKeyPressed(const std::string& key) {
		if (characterPressed_[key]) {
			characterPressed_[key] = false;
			return true;
		}
		return characterPressed_[key];
	}

	void Window::UpdateInput() {
		// TODO
	}

	void Window::SwapBuffers()
	{
	}

	void Window::SetMouseVisible(const bool visible)
	{
	}

	void Window::SetTitle(const std::string& windowTitle)
	{
	}

	int Window::MouseX() {
		return mousex_;
	}

	int Window::MouseY() {
		return mousey_;
	}

	void Window::SetMouse(const int xposition, const int yposition)
	{
	}

	void Window::SetRelativeMouseMode(const bool relative) {
		relativeMouseMode = relative;
		// TODO
	}

	void Window::SetIcon(const std::string&)
	{
	}

	int getDesktopWidth()
	{
		return jngl::getWindowWidth();
	}

	int getDesktopHeight() {
		return jngl::getWindowHeight();
	}
}