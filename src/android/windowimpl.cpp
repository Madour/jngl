// Copyright 2015-2018 Jan Niklas Hasse <jhasse@bixense.com>
// For conditions of distribution and use, see copyright notice in LICENSE.txt

#include "windowimpl.hpp"

#include "../opengl.hpp"
#include "../jngl/other.hpp"
#include "../jngl/debug.hpp"
#include "../window.hpp"
#include "../main.hpp"
#include "fopen.hpp"

#include <android_native_app_glue.h>
#include <android/storage_manager.h>
#include <android/obb.h>
#include <stdexcept>
#include <cassert>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace jngl {

android_app* androidApp;

static void engine_handle_cmd(struct android_app* app, int32_t cmd) {
	jngl::debugLn("Received event");
	WindowImpl& impl = *reinterpret_cast<WindowImpl*>(app->userData);
	switch (cmd) {
		case APP_CMD_SAVE_STATE:
			// TODO: The system has asked us to save our current state.  Do so.
			break;
		case APP_CMD_INIT_WINDOW:
			// The window is being shown, get it ready.
			if (androidApp->window) {
				impl.init();
			}
			break;
		case APP_CMD_TERM_WINDOW:
			// TODO: The window is being hidden or closed, clean it up.
			break;
		case APP_CMD_GAINED_FOCUS:
			// TODO
			break;
		case APP_CMD_LOST_FOCUS:
			// TODO
			break;
	}
}

static int32_t engine_handle_input(struct android_app* app, AInputEvent* event) {
	WindowImpl& impl = *reinterpret_cast<WindowImpl*>(app->userData);
	switch (AInputEvent_getType(event)) {
		case AINPUT_EVENT_TYPE_MOTION:
			impl.mouseX = AMotionEvent_getX(event, 0);
			impl.mouseY = AMotionEvent_getY(event, 0);
			switch (AMotionEvent_getAction(event)) {
				case AMOTION_EVENT_ACTION_DOWN:
					++impl.numberOfTouches;
					break;
				case AMOTION_EVENT_ACTION_UP:
					--impl.numberOfTouches;
					break;
			};
			return 1;
	}
	return 0;
}

WindowImpl::WindowImpl(Window* window, const std::pair<int, int> minAspectRatio,
                       const std::pair<int, int> maxAspectRatio)
: minAspectRatio(minAspectRatio), maxAspectRatio(maxAspectRatio), app(androidApp), window(window) {
	app->userData = this;
	app->onAppCmd = engine_handle_cmd;
	app->onInputEvent = engine_handle_input;
	jngl::debugLn("Handler set.");

	android_asset_manager = app->activity->assetManager;
	assert(android_asset_manager);

	JNIEnv* env = nullptr;
	app->activity->vm->AttachCurrentThread(&env, nullptr);

	jclass activityClass = env->FindClass("android/app/NativeActivity");
	jmethodID getWindow = env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;");

	jclass windowClass = env->FindClass("android/view/Window");
	jmethodID getDecorView = env->GetMethodID(windowClass, "getDecorView", "()Landroid/view/View;");

	jclass viewClass = env->FindClass("android/view/View");
	jmethodID setSystemUiVisibility = env->GetMethodID(viewClass, "setSystemUiVisibility", "(I)V");

	jobject androidWindow = env->CallObjectMethod(app->activity->clazz, getWindow);

	jobject decorView = env->CallObjectMethod(androidWindow, getDecorView);

	jfieldID flagFullscreenID = env->GetStaticFieldID(viewClass, "SYSTEM_UI_FLAG_FULLSCREEN", "I");
	jfieldID flagHideNavigationID =
	    env->GetStaticFieldID(viewClass, "SYSTEM_UI_FLAG_HIDE_NAVIGATION", "I");
	jfieldID flagImmersiveStickyID =
	    env->GetStaticFieldID(viewClass, "SYSTEM_UI_FLAG_IMMERSIVE_STICKY", "I");

	const int flagFullscreen = env->GetStaticIntField(viewClass, flagFullscreenID);
	const int flagHideNavigation = env->GetStaticIntField(viewClass, flagHideNavigationID);
	const int flagImmersiveSticky = env->GetStaticIntField(viewClass, flagImmersiveStickyID);
	const int flag = flagFullscreen | flagHideNavigation | flagImmersiveSticky;

	env->CallVoidMethod(decorView, setSystemUiVisibility, flag);

	app->activity->vm->DetachCurrentThread();
}

void WindowImpl::init() {
	/*
	 * Here specify the attributes of the desired configuration.
	 * Below, we select an EGLConfig with at least 8 bits per color
	 * component compatible with on-screen windows
	 */
	const EGLint attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_BLUE_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_RED_SIZE, 8,
		EGL_NONE
	};
	EGLint format;
	EGLint numConfigs;
	EGLConfig config;

	display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

	eglInitialize(display, 0, 0);

	/* Here, the application chooses the configuration it desires. In this
	 * sample, we have a very simplified selection process, where we pick
	 * the first EGLConfig that matches our criteria */
	eglChooseConfig(display, attribs, &config, 1, &numConfigs);

	/* EGL_NATIVE_VISUAL_ID is an attribute of the EGLConfig that is
	 * guaranteed to be accepted by ANativeWindow_setBuffersGeometry().
	 * As soon as we picked a EGLConfig, we can safely reconfigure the
	 * ANativeWindow buffers to match, using EGL_NATIVE_VISUAL_ID. */
	eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);

	if (!app) {
		throw std::runtime_error("android_app struct not set. "
		                         "Use JNGL_MAIN_BEGIN and JNGL_MAIN_END.");
	}
	ANativeWindow_setBuffersGeometry(app->window, 0, 0, format);

	surface = eglCreateWindowSurface(display, config, app->window, NULL);
	context = eglCreateContext(display, config, NULL, NULL);

	if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
		throw std::runtime_error("Unable to eglMakeCurrent");
	}

	EGLint w, h;
	eglQuerySurface(display, surface, EGL_WIDTH, &w);
	eglQuerySurface(display, surface, EGL_HEIGHT, &h);
	window->width_ = w;
	window->height_ = h;
	window->calculateCanvasSize(minAspectRatio, maxAspectRatio);

	// Initialize GL state.
	Init(window->width_, window->height_, window->canvasWidth, window->canvasHeight);

	initialized = true;
}

void WindowImpl::setRelativeMouseMode(const bool relativeMouseMode) {
	if (relativeMouseMode) {
		relativeX = mouseX;
		relativeY = mouseY;
	} else {
		relativeX = relativeY = 0;
	}
}

void WindowImpl::updateInput() {
	// Read all pending events.
	int ident;
	int events;
	android_poll_source* source;

	while ((ident = ALooper_pollAll(0, NULL, &events, (void**)&source)) >= 0 ||
	       !initialized /* wait for WindowImpl::init to get called by engine_handle_cmd */) {

		// Process this event.
		if (source != NULL) {
			source->process(app, source);
		}

		// Check if we are exiting.
		if (app->destroyRequested != 0) {
			if (eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) ==
			    EGL_FALSE) {
				debugLn("Couldn't unbind surfaces!");
			}
			if (eglDestroyContext(display, context) == EGL_FALSE) {
				debugLn("Couldn't destroy context!");
			}
			if (eglDestroySurface(display, surface) == EGL_FALSE) {
				debugLn("Couldn't destroy surface!");
			}
			if (eglTerminate(display) == EGL_FALSE) {
				debugLn("Couldn't terminate display!");
			}
			jngl::quit();
		}
	}

	if (!window->mouseDown_[0]) {
		window->mousePressed_[0] = numberOfTouches > 0;
	}
	window->mouseDown_[0] = numberOfTouches > 0;
	window->multitouch = numberOfTouches > 1;
	window->mousex_ = mouseX - relativeX;
	window->mousey_ = mouseY - relativeY;
	if (window->relativeMouseMode) {
		relativeX = mouseX;
		relativeY = mouseY;
	}
}

void WindowImpl::swapBuffers() {
	eglSwapBuffers(display, surface);
}

} // namespace jngl
