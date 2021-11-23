// Copyright 2015-2021 Jan Niklas Hasse <jhasse@bixense.com>
// For conditions of distribution and use, see copyright notice in LICENSE.txt

#include "windowimpl.hpp"

#include "AndroidController.hpp"
#include "../jngl/other.hpp"
#include "../jngl/debug.hpp"
#include "../jngl/screen.hpp"
#include "../jngl/work.hpp"
#include "../audio.hpp"
#include "../windowptr.hpp"
#include "../main.hpp"
#include "fopen.hpp"

#include <android_native_app_glue.h>
#include <android/storage_manager.h>
#include <android/window.h>

namespace jngl {

android_app* androidApp;

static void engine_handle_cmd(struct android_app* app, int32_t cmd) {
	jngl::debug("Received event "); jngl::debugLn(cmd);
	WindowImpl& impl = *reinterpret_cast<WindowImpl*>(app->userData);
	switch (cmd) {
		case APP_CMD_SAVE_STATE:
			// TODO: The system has asked us to save our current state.  Do so.
			break;
		case APP_CMD_INIT_WINDOW:
			// The window is being shown, get it ready.
			assert(androidApp == app);
			if (androidApp->window) {
				impl.init();
			}
			break;
		case APP_CMD_TERM_WINDOW:
			// TODO: The window is being hidden or closed, clean it up.
			break;
		case APP_CMD_GAINED_FOCUS:
			impl.makeCurrent();
			break;
		case APP_CMD_PAUSE:
			impl.pause();
			break;
	}
}

static int32_t engine_handle_input(struct android_app* app, AInputEvent* event) {
	WindowImpl& impl = *reinterpret_cast<WindowImpl*>(app->userData);
	const auto source = AInputEvent_getSource(event);
	if ((source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK ||
	    (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD ||
	    (source & AINPUT_SOURCE_DPAD) == AINPUT_SOURCE_DPAD) {
		return impl.handleJoystickEvent(event);
	}
	switch (AInputEvent_getType(event)) {
	case AINPUT_EVENT_TYPE_KEY:
		if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN) {
			return impl.handleKeyEvent(event);
		}
		break;
	case AINPUT_EVENT_TYPE_MOTION: {
		const auto action = AMotionEvent_getAction(event);
		switch (action & AMOTION_EVENT_ACTION_MASK) {
		case AMOTION_EVENT_ACTION_DOWN: {
			const int32_t index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
			                      AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
			const auto id = AMotionEvent_getPointerId(event, index);
			auto [touch, created] = impl.touches.insert(
			    { id, { AMotionEvent_getX(event, index), AMotionEvent_getY(event, index) } });
			assert(created);
			impl.mouseX = touch->second.x;
			impl.mouseY = touch->second.y;
			return 1;
		}
		case AMOTION_EVENT_ACTION_POINTER_DOWN: {
			const int32_t index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
			                      AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
			const auto id = AMotionEvent_getPointerId(event, index);
			[[maybe_unused]] auto [_, created] = impl.touches.insert(
			    { id, { AMotionEvent_getX(event, index), AMotionEvent_getY(event, index) } });
			assert(created);
			return 1;
		}
		case AMOTION_EVENT_ACTION_POINTER_UP: {
			const int32_t index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
			                      AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
			const auto id = AMotionEvent_getPointerId(event, index);
			const auto it = impl.touches.find(id);
			assert(it != impl.touches.end());
			impl.touches.erase(it);
			assert(!impl.touches.empty());
			// For the case that the mouse position pointed to this pointer we need to update it so
			// that it still makes sense. This is very important when going from 2 pointer to 1
			// since getTouchPositions() will switch to getMousePos() then.
			impl.mouseX = impl.touches.begin()->second.x;
			impl.mouseY = impl.touches.begin()->second.y;
			return 1;
		}
		case AMOTION_EVENT_ACTION_MOVE: {
			for (int32_t index = 0; index < AMotionEvent_getPointerCount(event); ++index) {
				const auto x = AMotionEvent_getX(event, index);
				const auto y = AMotionEvent_getY(event, index);
				const auto id = AMotionEvent_getPointerId(event, index);
				const auto it = impl.touches.find(id);
				if (it == impl.touches.end()) {
					debug("WARNING: Unknown touch id: ");
					debugLn(id);
					return 0;
				}
				it->second.x = x;
				it->second.y = y;
				if (index == 0) {
					impl.mouseX = x;
					impl.mouseY = y;
				}
			}
			return 1;
		}
		case AMOTION_EVENT_ACTION_UP:
			impl.touches.clear();
			return 1;
		}
	}
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
	app->activity->vm->AttachCurrentThread(&env, nullptr);
}

WindowImpl::~WindowImpl() {
	if (!initialized) { // Exception thrown during initialization?
		return; // Skip any clean up code
		// TODO: The code should probably made exception-safe, but in a cleaner way.
	}
	// We need to destroy the surface before finishing our activity, otherwise the app won't start
	// again.
	pause();
	// jngl::quit() has been called. We need to gracefully quit the activity, too, and handle any
	// pending events (this will result in destroyRequested != 0):
	ANativeActivity_finish(app->activity);
	updateInput();
	app->activity->vm->DetachCurrentThread();
}

void WindowImpl::init() {
	if (initialized) {
		return;
	}
	ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);

	// Here specify the attributes of the desired configuration. Below, we select an EGLConfig with
	// at least 8 bits per color component compatible with on-screen windows
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

	display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

	eglInitialize(display, 0, 0);

	// Here, the application chooses the configuration it desires. In this sample, we have a very
	// simplified selection process, where we pick the first EGLConfig that matches our criteria
	eglChooseConfig(display, attribs, &config, 1, &numConfigs);

	// EGL_NATIVE_VISUAL_ID is an attribute of the EGLConfig that is guaranteed to be accepted by
	// ANativeWindow_setBuffersGeometry(). As soon as we picked a EGLConfig, we can safely
	// reconfigure the ANativeWindow buffers to match, using EGL_NATIVE_VISUAL_ID.
	eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);

	if (!app) {
		throw std::runtime_error("android_app struct not set. "
		                         "Use JNGL_MAIN_BEGIN and JNGL_MAIN_END.");
	}
	ANativeWindow_setBuffersGeometry(app->window, 0, 0, format);

	const EGLint contextAttribList[] = {
			EGL_CONTEXT_CLIENT_VERSION, 3,
			EGL_NONE
	};
	context = eglCreateContext(display, config, NULL, contextAttribList);

	makeCurrent();

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
		window->mousex_ = window->mousey_ = 0;
	} else {
		relativeX = relativeY = 0;
	}
}

void WindowImpl::pause() {
	if (!surface) { return; }
	if (eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) ==
		EGL_FALSE) {
		debugLn("Couldn't unbind surfaces!");
	}
	if (eglDestroySurface(display, surface) == EGL_FALSE) {
		debugLn("Couldn't destroy surface!");
	}
	surface = nullptr;
	pauseAudioDevice();
}

void WindowImpl::makeCurrent() {
	if (surface) { return; }
	surface = eglCreateWindowSurface(display, config, app->window, nullptr);
	assert(eglGetError() == EGL_SUCCESS);
	if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
		// Source: https://stackoverflow.com/a/60611870/647898
		JNIEnv* jni = nullptr;
		app->activity->vm->AttachCurrentThread(&jni, nullptr);

		jclass clazz = jni->GetObjectClass(app->activity->clazz);

		// Get the ID of the method we want to call
		// This must match the name and signature from the Java side Signature has to match java
		// implementation (second string hints a java string parameter)
		jmethodID methodID = jni->GetMethodID(clazz, "showAlert", "(Ljava/lang/String;Z)I");

		if (methodID) {
			std::stringstream sstream;
			sstream << "Your device doesn't support OpenGL ES 3.0!\n\nError code: 0x" << std::hex
			        << eglGetError();

			// Strings passed to the function need to be converted to a java string object
			jstring jmessage = jni->NewStringUTF(sstream.str().c_str());

			jint result = jni->CallIntMethod(app->activity->clazz, methodID, jmessage, true);

			// Remember to clean up passed values
			jni->DeleteLocalRef(jmessage);
		}

		app->activity->vm->DetachCurrentThread();

		throw std::runtime_error("Unable to eglMakeCurrent");
	}
	resumeAudioDevice();
}

int WindowImpl::handleKeyEvent(AInputEvent* const event) {
	const auto key = AKeyEvent_getKeyCode(event);
	if (key == AKEYCODE_DEL) {
		jngl::setKeyPressed(jngl::key::BackSpace, true);
		return 1;
	} else if (key == AKEYCODE_BACK) {
		window->getWork()->onBackEvent();
		return 1;
	}
	const auto metaState = AKeyEvent_getMetaState(event);

	const jclass keyEventClass = env->FindClass("android/view/KeyEvent");

	const jmethodID getUnicodeCharMethod =
	    env->GetMethodID(keyEventClass, "getUnicodeChar", (metaState == 0) ? "()I" : "(I)I");

	const jmethodID eventConstructor = env->GetMethodID(keyEventClass, "<init>", "(II)V");

	const jobject eventObj =
	    env->NewObject(keyEventClass, eventConstructor, AKEY_EVENT_ACTION_DOWN, key);

	const int unicode = (metaState == 0)
	                        ? env->CallIntMethod(eventObj, getUnicodeCharMethod)
	                        : env->CallIntMethod(eventObj, getUnicodeCharMethod, metaState);

	if (unicode == 0) { // For example the back button
		return 0;
	}

	char bytes[5];
	std::memcpy(bytes, &unicode, 4);
	if (bytes[0] == '\n') {
		setKeyPressed(jngl::key::Return, true);
		return 1;
	}
	bytes[4] = '\0';
	window->textInput += std::string(bytes);
	return 1;
}

void WindowImpl::updateInput() {
	// Read all pending events.
	int ident;
	int events;
	android_poll_source* source;

	while ((ident = ALooper_pollAll(
	            surface ? 0 : 1e9, // This is the timeout. When we're in the background, we don't
	                               // want to busy-wait for events.
	            nullptr, &events, (void**)&source)) >= 0 ||
	       !initialized /* wait for WindowImpl::init to get called by engine_handle_cmd */ ||
	       !surface /* we're in the background, don't leave this event loop */) {

		// Process this event.
		if (source != NULL) {
			source->process(app, source);
		}

		// Check if we are exiting.
		if (app->destroyRequested != 0) {
			if (eglDestroyContext(display, context) == EGL_FALSE) {
				debugLn("Couldn't destroy context!");
			}
			if (eglTerminate(display) == EGL_FALSE) {
				debugLn("Couldn't terminate display!");
			}
			jngl::quit();
			return; // surface == nullptr, we need to exit this loop
		}
	}

	if (!window->mouseDown_[0]) {
		window->mousePressed_[0] = !touches.empty();
	}
	window->mouseDown_[0] = !touches.empty();
	window->multitouch = touches.size() > 1;
	window->mousex_ = mouseX - relativeX;
	window->mousey_ = mouseY - relativeY;
	if (window->relativeMouseMode) {
		relativeX = mouseX;
		relativeY = mouseY;
	}
}

void WindowImpl::swapBuffers() {
	if (surface) {
		eglSwapBuffers(display, surface);
	}
}

WindowImpl* Window::getImpl() const { return impl.get(); }

void setKeyboardVisible(const bool visible) {
	pWindow->getImpl()->setKeyboardVisible(visible);
}

void WindowImpl::setKeyboardVisible(const bool visible) {
	const jclass nativeActivityClass = env->GetObjectClass(app->activity->clazz);

	const jmethodID getApplicationContextMethod = env->GetMethodID(
			nativeActivityClass, "getApplicationContext", "()Landroid/content/Context;");

	const jobject contextObject =
	    env->CallObjectMethod(app->activity->clazz, getApplicationContextMethod);

	const jclass contextClass = env->FindClass("android/content/Context");

	const jmethodID getSystemServiceMethod = env->GetMethodID(
			contextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");

	const jfieldID INPUT_METHOD_SERVICE_FIELD =
			env->GetStaticFieldID(contextClass, "INPUT_METHOD_SERVICE", "Ljava/lang/String;");

	const jobject INPUT_METHOD_SERVICE =
			env->GetStaticObjectField(contextClass, INPUT_METHOD_SERVICE_FIELD);

	const jobject inputMethodManager =
			env->CallObjectMethod(contextObject, getSystemServiceMethod, INPUT_METHOD_SERVICE);

	const jclass inputMethodManagerClass = env->GetObjectClass(inputMethodManager);

	const jmethodID getWindowMethod =
	    env->GetMethodID(nativeActivityClass, "getWindow", "()Landroid/view/Window;");

	const jobject window = env->CallObjectMethod(app->activity->clazz, getWindowMethod);

	const jclass windowClass = env->FindClass("android/view/Window");

	const jmethodID getDecorViewMethod =
	    env->GetMethodID(windowClass, "getDecorView", "()Landroid/view/View;");

	const jobject decorView = env->CallObjectMethod(window, getDecorViewMethod);

	if (visible) {
		const jmethodID showSoftInputMethod =
		    env->GetMethodID(inputMethodManagerClass, "showSoftInput", "(Landroid/view/View;I)Z");

		const jfieldID SHOW_FORCED_FIELD =
		    env->GetStaticFieldID(inputMethodManagerClass, "SHOW_FORCED", "I");

		const int showFlags = env->GetStaticIntField(inputMethodManagerClass, SHOW_FORCED_FIELD);

		env->CallBooleanMethod(inputMethodManager, showSoftInputMethod, decorView, showFlags);
	} else {
		const jclass viewClass = env->FindClass("android/view/View");

		const jmethodID getWindowTokenMethod =
		    env->GetMethodID(viewClass, "getWindowToken", "()Landroid/os/IBinder;");

		const jobject windowToken = env->CallObjectMethod(decorView, getWindowTokenMethod);

		const jmethodID hideSoftInputFromWindowMethod = env->GetMethodID(
		    inputMethodManagerClass, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");

		env->CallBooleanMethod(inputMethodManager, hideSoftInputFromWindowMethod, windowToken, 0);
	}
}

std::vector<std::shared_ptr<Controller>> WindowImpl::getConnectedControllers() const {
	std::vector<std::shared_ptr<Controller>> rtn;
	for (auto [id, controller] : controllers) {
		rtn.emplace_back(controller);
	}
	return rtn;
}

int32_t WindowImpl::handleJoystickEvent(const AInputEvent* const event) {
	const int32_t deviceId = AInputEvent_getDeviceId(event);
	auto& controller = controllers[deviceId];
	if (!controller) {
		controller = std::make_shared<AndroidController>();
		if (window->controllerChangedCallback) {
			window->controllerChangedCallback();
		}
	}
	switch (AInputEvent_getType(event)) {
	case AINPUT_EVENT_TYPE_KEY: {
		const bool down = AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN;
		switch (AKeyEvent_getKeyCode(event)) {
		case AKEYCODE_BUTTON_A:
		case AKEYCODE_DPAD_CENTER:
			controller->buttonA = down;
			break;
		case AKEYCODE_BUTTON_B:
			controller->buttonB = down;
			break;
		case AKEYCODE_BUTTON_START:
			controller->buttonStart = down;
			break;
		case AKEYCODE_DPAD_LEFT:
			controller->dpadX = down ? -1 : 0;
			break;
		case AKEYCODE_DPAD_RIGHT:
			controller->dpadX = down ? 1 : 0;
			break;
		case AKEYCODE_DPAD_UP:
			controller->dpadY = down ? -1 : 0;
			break;
		case AKEYCODE_DPAD_DOWN:
			controller->dpadY = down ? 1 : 0;
			break;
		}
		break;
	}
	case AINPUT_EVENT_TYPE_MOTION:
		assert(AMotionEvent_getPointerCount(event) == 1);
		controller->leftStickX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
		controller->leftStickY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
		controller->rightStickX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
		controller->rightStickY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
		controller->dpadX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
		controller->dpadY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
		break;
	default:
		debugLn("Unknown joystick event!");
	}
	return 1;
}

std::vector<Vec2> Window::getTouchPositions() const {
	std::vector<Vec2> positions;
	for (auto [id, pos] : impl->touches) {
		positions.emplace_back(
		    (pos.x - (width_ - canvasWidth) / 2) / getScaleFactor() - getScreenWidth() / 2,
		    (pos.y - (height_ - canvasHeight) / 2) / getScaleFactor() - getScreenHeight() / 2);
	}
	return positions;
}

std::string getSystemConfigPath() {
	return jngl::androidApp->activity->internalDataPath;
}

std::string getPreferredLanguage() {
	const auto env = reinterpret_cast<WindowImpl*>(androidApp->userData)->env;
	const jclass localeClass = env->FindClass("java/util/Locale");
	const jobject defaultLocale = env->CallStaticObjectMethod(
	    localeClass, env->GetStaticMethodID(localeClass, "getDefault", "()Ljava/util/Locale;"));

	const jobject language = env->CallObjectMethod(
	    defaultLocale, env->GetMethodID(localeClass, "getLanguage", "()Ljava/lang/String;"));

	const jmethodID getBytesMethod =
	    env->GetMethodID(env->GetObjectClass(language), "getBytes", "(Ljava/lang/String;)[B");
	const auto bytesObject = static_cast<jbyteArray>(
	    env->CallObjectMethod(language, getBytesMethod, env->NewStringUTF("UTF-8")));
	const size_t length = env->GetArrayLength(bytesObject);
	if (length != 2) {
		debugLn("ERROR: Couldn't get preferred language. Falling back to \"en\".");
		return "en";
	}
	return std::string(
	    reinterpret_cast<const char*>(env->GetByteArrayElements(bytesObject, nullptr)), length);
}

} // namespace jngl
